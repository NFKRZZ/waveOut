#include "BPMDetection.h"
#include <algorithm>
#include <limits>
#include <iostream>
#include <cmath>
#include <vector>

extern "C"
{
    #include <aubio/aubio.h>
}

namespace
{
    constexpr double kFoldLo = 80.0;
    constexpr double kFoldHi = 180.0;

    static double foldBpm(double bpm)
    {
        if (!std::isfinite(bpm) || bpm <= 0.0) return 0.0;
        while (bpm > kFoldHi) bpm *= 0.5;
        while (bpm < kFoldLo) bpm *= 2.0;
        return bpm;
    }

    static std::vector<float> ToMonoFloat(const std::vector<double>& monoPcm)
    {
        std::vector<float> y;
        y.reserve(monoPcm.size());
        for (double v : monoPcm) y.push_back(static_cast<float>(v));
        return y;
    }

    static std::vector<float> NormalizeForAnalysis(std::vector<float> x)
    {
        float mx = 0.0f;
        for (float v : x) mx = (std::max)(mx, std::fabs(v));
        // Mirror Python load_mono(): only normalize if the data looks "int-like".
        if (mx > 2.0f)
        {
            const float inv = 1.0f / (mx + 1e-12f);
            for (float& v : x) v *= inv;
        }
        return x;
    }

    static std::vector<float> onePoleLowpass(const std::vector<float>& x, int sr, double fcHz)
    {
        std::vector<float> y(x.size(), 0.0f);
        if (x.empty() || sr <= 0 || fcHz <= 0.0) return y;
        constexpr double pi = 3.14159265358979323846;
        const double dt = 1.0 / static_cast<double>(sr);
        const double rc = 1.0 / (2.0 * pi * fcHz);
        const double a = dt / (rc + dt);
        double z = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            z = z + a * (static_cast<double>(x[i]) - z);
            y[i] = static_cast<float>(z);
        }
        return y;
    }

    static double firstAudioTimeByRms(const std::vector<float>& mono, int sr,
        double frameS = 0.02, double hopS = 0.01, double thresholdDb = -45.0)
    {
        const int frame = (std::max)(16, static_cast<int>(frameS * sr));
        const int hop = (std::max)(8, static_cast<int>(hopS * sr));
        if (mono.size() < static_cast<size_t>(frame) || sr <= 0) return 0.0;
        const double thr = std::pow(10.0, thresholdDb / 20.0);
        constexpr double eps = 1e-12;
        for (size_t i = 0; i + static_cast<size_t>(frame) <= mono.size(); i += static_cast<size_t>(hop))
        {
            double sum2 = 0.0;
            for (int j = 0; j < frame; ++j)
            {
                const double s = mono[i + j];
                sum2 += s * s;
            }
            const double rms = std::sqrt(sum2 / static_cast<double>(frame) + eps);
            if (rms >= thr)
                return static_cast<double>(i) / static_cast<double>(sr);
        }
        return 0.0;
    }

    static double aubioFirstOnsetTime(const std::vector<float>& mono, int sr, double startS,
        uint_t winSize = 1024, uint_t hopSize = 128,
        const char* method = "hfc",
        smpl_t threshold = 0.25f, smpl_t silenceDb = -60.0f, smpl_t minIoiS = 0.08f)
    {
        aubio_onset_t* onset = new_aubio_onset(method, winSize, hopSize, static_cast<uint_t>(sr));
        if (!onset) return startS;
        aubio_onset_set_threshold(onset, threshold);
        aubio_onset_set_silence(onset, silenceDb);
        aubio_onset_set_minioi_s(onset, minIoiS);

        fvec_t* in = new_fvec(hopSize);
        fvec_t* out = new_fvec(1);
        if (!in || !out)
        {
            if (in) del_fvec(in);
            if (out) del_fvec(out);
            del_aubio_onset(onset);
            return startS;
        }

        size_t pos = static_cast<size_t>((std::max)(0.0, startS) * sr);
        while (pos < mono.size())
        {
            for (uint_t i = 0; i < hopSize; ++i)
            {
                in->data[i] = (pos < mono.size()) ? mono[pos++] : 0.0f;
            }
            aubio_onset_do(onset, in, out);
            if (out->data[0] != 0.0f)
            {
                double t = static_cast<double>(aubio_onset_get_last_s(onset));
                del_fvec(in); del_fvec(out); del_aubio_onset(onset);
                return (t >= startS) ? t : startS;
            }
        }

        del_fvec(in); del_fvec(out); del_aubio_onset(onset);
        return startS;
    }

    static double findKickAttackStart(const std::vector<float>& mono, int sr, double approxOnsetS,
        double lookbackMs = 200.0, double baselineMs = 80.0, double smoothMs = 2.5,
        double riseSigma = 6.0, double sustainMs = 8.0, double lpFcHz = 180.0)
    {
        if (mono.empty() || sr <= 0) return approxOnsetS;
        const int onsetI = static_cast<int>(approxOnsetS * sr);
        const int L = static_cast<int>((lookbackMs / 1000.0) * sr);
        const int i0 = (std::max)(0, onsetI - L);
        const int i1 = (std::min)(onsetI, static_cast<int>(mono.size()));
        if (i1 - i0 < 32) return approxOnsetS;

        std::vector<float> seg(mono.begin() + i0, mono.begin() + i1);
        std::vector<float> segLp = onePoleLowpass(seg, sr, lpFcHz);
        std::vector<float> env(segLp.size(), 0.0f);
        for (size_t i = 0; i < segLp.size(); ++i) env[i] = std::fabs(segLp[i]);

        const int win = (std::max)(1, static_cast<int>((smoothMs / 1000.0) * sr));
        if (win > 1)
        {
            std::vector<float> sm(env.size(), 0.0f);
            for (size_t i = 0; i < env.size(); ++i)
            {
                const int a = (std::max)(0, static_cast<int>(i) - win / 2);
                const int b = (std::min)(static_cast<int>(env.size()), static_cast<int>(i) + win / 2 + 1);
                double s = 0.0;
                for (int j = a; j < b; ++j) s += env[(size_t)j];
                sm[i] = static_cast<float>(s / static_cast<double>(b - a));
            }
            env.swap(sm);
        }

        int bN = (std::max)(16, static_cast<int>((baselineMs / 1000.0) * sr));
        bN = (std::min)(bN, static_cast<int>(env.size()));
        double mu = 0.0;
        for (int i = 0; i < bN; ++i) mu += env[(size_t)i];
        mu /= (std::max)(1, bN);
        double var = 0.0;
        for (int i = 0; i < bN; ++i)
        {
            const double d = env[(size_t)i] - mu;
            var += d * d;
        }
        const double sig = std::sqrt(var / (std::max)(1, bN)) + 1e-12;
        const double thr = mu + riseSigma * sig;
        int susN = (std::max)(1, static_cast<int>((sustainMs / 1000.0) * sr));
        susN = (std::min)(susN, (std::max)(1, static_cast<int>(env.size()) - 1));

        for (int idx = 0; idx + susN < static_cast<int>(env.size()); ++idx)
        {
            if (env[(size_t)idx] <= thr) continue;
            bool ok = true;
            for (int k = 0; k < susN; ++k)
            {
                if (env[(size_t)(idx + k)] <= thr) { ok = false; break; }
            }
            if (ok) return static_cast<double>(i0 + idx) / static_cast<double>(sr);
        }

        for (size_t i = 0; i < env.size(); ++i)
        {
            if (env[i] > thr)
                return static_cast<double>(i0 + static_cast<int>(i)) / static_cast<double>(sr);
        }
        return approxOnsetS;
    }

    static std::vector<float> onePoleHighpass(const std::vector<float>& x, int sr, double fcHz)
    {
        std::vector<float> lp = onePoleLowpass(x, sr, fcHz);
        std::vector<float> y(x.size(), 0.0f);
        for (size_t i = 0; i < x.size(); ++i) y[i] = x[i] - lp[i];
        return y;
    }

    static double bpmAutocorrOnsetLike(const std::vector<float>& mono, int sr, int hopLength = 256,
        double bpmMin = 60.0, double bpmMax = 200.0)
    {
        if (mono.empty() || sr <= 0 || hopLength <= 0) return 0.0;

        // Lightweight onset-strength proxy (librosa-like intent, not exact):
        // emphasize transients, then summarize energy per hop.
        std::vector<float> hp = onePoleHighpass(mono, sr, 120.0);
        std::vector<double> oenv;
        oenv.reserve(mono.size() / (size_t)hopLength + 1);

        for (size_t i = 0; i < mono.size(); i += (size_t)hopLength)
        {
            const size_t end = (std::min)(mono.size(), i + (size_t)hopLength);
            double s = 0.0;
            for (size_t j = i; j < end; ++j)
            {
                const double a = std::fabs((double)hp[j]);
                s += a;
            }
            oenv.push_back(s / (double)(end - i));
        }

        if (oenv.size() < 8) return 0.0;

        double mean = 0.0;
        for (double v : oenv) mean += v;
        mean /= (double)oenv.size();
        double var = 0.0;
        for (double v : oenv) { double d = v - mean; var += d * d; }
        double sd = std::sqrt(var / (double)oenv.size()) + 1e-9;
        for (double& v : oenv) v = (v - mean) / sd;

        const size_t N = oenv.size();
        std::vector<double> ac(N, 0.0);
        for (size_t lag = 0; lag < N; ++lag)
        {
            double s = 0.0;
            for (size_t i = 0; i + lag < N; ++i) s += oenv[i] * oenv[i + lag];
            ac[lag] = s;
        }
        if (ac.size() < 2) return 0.0;
        ac[0] = 0.0;

        const double fps = (double)sr / (double)hopLength;
        int lagMin = (int)std::floor(60.0 * fps / bpmMax);
        int lagMax = (int)std::ceil(60.0 * fps / bpmMin);
        lagMin = (std::max)(1, lagMin);
        lagMax = (std::min)(lagMax, (int)ac.size() - 1);
        if (lagMax <= lagMin) return 0.0;

        int bestLag = lagMin;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            if (ac[(size_t)lag] > bestScore)
            {
                bestScore = ac[(size_t)lag];
                bestLag = lag;
            }
        }
        return 60.0 * fps / (double)bestLag;
    }

    static double refineBpmLocalAutocorr(const std::vector<float>& mono, int sr, double bpm0,
        double search = 2.0, double step = 0.01, int hopLength = 256)
    {
        bpm0 = foldBpm(bpm0);
        if (!std::isfinite(bpm0) || bpm0 <= 0.0) return 0.0;
        if (mono.empty() || sr <= 0 || hopLength <= 0) return bpm0;

        std::vector<float> hp = onePoleHighpass(mono, sr, 120.0);
        std::vector<double> oenv;
        oenv.reserve(mono.size() / (size_t)hopLength + 1);
        for (size_t i = 0; i < mono.size(); i += (size_t)hopLength)
        {
            const size_t end = (std::min)(mono.size(), i + (size_t)hopLength);
            double s = 0.0;
            for (size_t j = i; j < end; ++j) s += std::fabs((double)hp[j]);
            oenv.push_back(s / (double)(end - i));
        }
        if (oenv.size() < 8) return bpm0;

        double mean = 0.0;
        for (double v : oenv) mean += v;
        mean /= (double)oenv.size();
        double var = 0.0;
        for (double v : oenv) { double d = v - mean; var += d * d; }
        double sd = std::sqrt(var / (double)oenv.size()) + 1e-9;
        for (double& v : oenv) v = (v - mean) / sd;

        const size_t N = oenv.size();
        std::vector<double> ac(N, 0.0);
        for (size_t lag = 0; lag < N; ++lag)
        {
            double s = 0.0;
            for (size_t i = 0; i + lag < N; ++i) s += oenv[i] * oenv[i + lag];
            ac[lag] = s;
        }
        if (ac.size() < 2) return bpm0;
        ac[0] = 0.0;

        const double fps = (double)sr / (double)hopLength;
        auto scoreBpm = [&](double bpm) -> double
        {
            if (!std::isfinite(bpm) || bpm <= 0.0) return -std::numeric_limits<double>::infinity();
            const int lag = (int)std::llround(60.0 * fps / bpm);
            if (lag < 1 || lag >= (int)ac.size()) return -std::numeric_limits<double>::infinity();
            return ac[(size_t)lag];
        };

        double bestBpm = bpm0;
        double bestScore = scoreBpm(bpm0);
        const double centers[3] = { bpm0, bpm0 * 0.5, bpm0 * 2.0 };
        for (double c : centers)
        {
            c = foldBpm(c);
            for (double b = c - search; b <= c + search + 0.5 * step; b += step)
            {
                const double bf = foldBpm(b);
                const double sc = scoreBpm(bf);
                if (sc > bestScore)
                {
                    bestScore = sc;
                    bestBpm = bf;
                }
            }
        }
        return foldBpm(bestBpm);
    }

    static double medianOfVector(std::vector<double> v)
    {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        const size_t m = v.size() / 2;
        return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
    }

    struct DriftLossInfo
    {
        double loss = std::numeric_limits<double>::infinity();
        double medianAbs = std::numeric_limits<double>::infinity();
        double slope = std::numeric_limits<double>::infinity();
        int nResiduals = 0;
    };

    static double clusterPickMedianFolded(const std::vector<double>& candidates, double tol = 1.5)
    {
        std::vector<double> vals;
        vals.reserve(candidates.size());
        for (double b : candidates)
        {
            b = foldBpm(b);
            if (std::isfinite(b) && b > 0.0) vals.push_back(b);
        }
        if (vals.empty()) return 0.0;
        std::sort(vals.begin(), vals.end());

        std::vector<std::vector<double>> clusters;
        for (double v : vals)
        {
            bool placed = false;
            for (auto& c : clusters)
            {
                double med = medianOfVector(c);
                if (std::fabs(v - med) <= tol)
                {
                    c.push_back(v);
                    placed = true;
                    break;
                }
            }
            if (!placed) clusters.push_back({ v });
        }
        if (clusters.empty()) return 0.0;
        auto bestIt = std::max_element(clusters.begin(), clusters.end(),
            [](const auto& a, const auto& b) { return a.size() < b.size(); });
        return medianOfVector(*bestIt);
    }

    static std::vector<double> detectKickAttacksTimes(const std::vector<float>& mono, int sr, double tStart, double tEnd,
        double lpFcHz = 180.0, double smoothMs = 2.5, double thrSigma = 2.0, double minSepS = 0.18)
    {
        std::vector<double> out;
        if (mono.empty() || sr <= 0 || tEnd <= tStart) return out;

        const int i0 = (std::max)(0, (int)std::llround(tStart * sr));
        const int i1 = (std::min)((int)mono.size(), (int)std::llround(tEnd * sr));
        if (i1 - i0 < (int)(0.2 * sr)) return out;

        std::vector<float> seg(mono.begin() + i0, mono.begin() + i1);
        std::vector<float> segLp = onePoleLowpass(seg, sr, lpFcHz);
        std::vector<float> env(segLp.size(), 0.0f);
        for (size_t i = 0; i < segLp.size(); ++i) env[i] = std::fabs(segLp[i]);

        const int win = (std::max)(1, (int)std::llround((smoothMs / 1000.0) * sr));
        if (win > 1)
        {
            std::vector<float> sm(env.size(), 0.0f);
            for (size_t i = 0; i < env.size(); ++i)
            {
                const int a = (std::max)(0, (int)i - win / 2);
                const int b = (std::min)((int)env.size(), (int)i + win / 2 + 1);
                double s = 0.0;
                for (int j = a; j < b; ++j) s += env[(size_t)j];
                sm[i] = (float)(s / (double)(b - a));
            }
            env.swap(sm);
        }

        const int bN = (std::min)((int)env.size(), (std::max)(32, (int)(0.2 * sr)));
        double mu = 0.0;
        for (int i = 0; i < bN; ++i) mu += env[(size_t)i];
        mu /= (double)(std::max)(1, bN);
        double var = 0.0;
        for (int i = 0; i < bN; ++i) { double d = env[(size_t)i] - mu; var += d * d; }
        const double sig = std::sqrt(var / (double)(std::max)(1, bN)) + 1e-12;
        const double thr = mu + thrSigma * sig;

        std::vector<int> rising;
        for (size_t i = 1; i < env.size(); ++i)
        {
            if (env[i - 1] <= thr && env[i] > thr) rising.push_back((int)i);
        }
        if (rising.empty()) return out;

        const int minSep = (int)std::llround(minSepS * sr);
        std::vector<int> picks;
        int last = std::numeric_limits<int>::min() / 4;
        for (int j : rising)
        {
            if (j - last >= minSep)
            {
                picks.push_back(j);
                last = j;
            }
        }

        const double thr2 = mu + 2.0 * sig;
        const int lookback = (int)(0.08 * sr);
        out.reserve(picks.size());
        for (int j : picks)
        {
            const int lb = (std::max)(0, j - lookback);
            int jj = j;
            for (int k = lb; k <= j; ++k)
            {
                if (env[(size_t)k] > thr2) { jj = k; break; }
            }
            out.push_back((double)(i0 + jj) / (double)sr);
        }
        return out;
    }

    static std::vector<double> residualsForBpm(const std::vector<double>& kickTimes, double t0, double bpm)
    {
        std::vector<double> r;
        if (kickTimes.empty() || bpm <= 0.0) return r;
        const double T = 60.0 / bpm;
        r.reserve(kickTimes.size());
        for (double kt : kickTimes)
        {
            double k = std::nearbyint((kt - t0) / T);
            double tg = t0 + k * T;
            r.push_back(kt - tg);
        }
        return r;
    }

    static DriftLossInfo driftLoss(const std::vector<std::vector<double>>& kicksByWindow,
        const std::vector<double>& centers, double t0, double bpm, double lam = 0.25)
    {
        DriftLossInfo info{};
        std::vector<double> meds;
        std::vector<double> usedCenters;
        std::vector<double> absAll;

        for (size_t i = 0; i < kicksByWindow.size() && i < centers.size(); ++i)
        {
            std::vector<double> r = residualsForBpm(kicksByWindow[i], t0, bpm);
            if (r.empty()) continue;
            std::vector<double> absr;
            absr.reserve(r.size());
            for (double v : r) absr.push_back(std::fabs(v));
            meds.push_back(medianOfVector(r));
            usedCenters.push_back(centers[i]);
            absAll.insert(absAll.end(), absr.begin(), absr.end());
        }

        if (absAll.empty()) return info;
        info.medianAbs = medianOfVector(absAll);
        info.nResiduals = (int)absAll.size();

        if (meds.size() >= 2)
        {
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            const double n = (double)meds.size();
            for (size_t i = 0; i < meds.size(); ++i)
            {
                const double x = usedCenters[i];
                const double y = meds[i];
                sx += x; sy += y; sxx += x * x; sxy += x * y;
            }
            const double den = n * sxx - sx * sx;
            info.slope = (std::fabs(den) > 1e-18) ? ((n * sxy - sx * sy) / den) : 0.0;
        }
        else
        {
            info.slope = 0.0;
        }

        info.loss = info.medianAbs + lam * std::fabs(info.slope);
        return info;
    }

    static double refineBpmByDrift(const std::vector<float>& mono, int sr, double t0, double bpm0,
        double search = 1.0, double step = 0.01, int windows = 7, double windowLenS = 18.0,
        double startFrac = 0.10, double endFrac = 0.90, double lam = 0.25)
    {
        const double duration = (sr > 0) ? ((double)mono.size() / (double)sr) : 0.0;
        if (duration < 30.0 || bpm0 <= 0.0 || sr <= 0) return bpm0;

        std::vector<double> centers;
        centers.reserve((size_t)(std::max)(1, windows));
        if (windows <= 1)
        {
            centers.push_back(duration * 0.5);
        }
        else
        {
            for (int i = 0; i < windows; ++i)
            {
                double a = (double)i / (double)(windows - 1);
                centers.push_back(duration * (startFrac + a * (endFrac - startFrac)));
            }
        }

        std::vector<std::vector<double>> kicksByWindow;
        kicksByWindow.reserve(centers.size());
        for (double c : centers)
        {
            double ts = (std::max)(0.0, c - 0.5 * windowLenS);
            double te = (std::min)(duration, c + 0.5 * windowLenS);
            kicksByWindow.push_back(detectKickAttacksTimes(mono, sr, ts, te, 180.0, 2.5, 2.0, 0.18));
        }

        double bestBpm = bpm0;
        DriftLossInfo best = driftLoss(kicksByWindow, centers, t0, bpm0, lam);
        for (double b = bpm0 - search; b <= bpm0 + search + 0.5 * step; b += step)
        {
            double cand = foldBpm(b);
            DriftLossInfo cur = driftLoss(kicksByWindow, centers, t0, cand, lam);
            if (cur.loss < best.loss)
            {
                best = cur;
                bestBpm = cand;
            }
        }

        std::cout << "Drift refine best BPM: " << bestBpm
            << "  median_abs=" << best.medianAbs
            << "  slope=" << best.slope
            << "  n=" << best.nResiduals << "\n";
        std::cout << "Drift kicks/window: [";
        for (size_t i = 0; i < kicksByWindow.size(); ++i)
        {
            if (i) std::cout << ", ";
            std::cout << kicksByWindow[i].size();
        }
        std::cout << "]\n";

        return bestBpm;
    }

    static double aubioTempoMedianAndReported(const std::vector<float>& mono, int sampleRate, double* outReported = nullptr)
    {
        if (sampleRate <= 0 || mono.empty()) return 0.0;
        const uint_t win_size = 1024;
        const uint_t hop_size = win_size / 4;

        aubio_tempo_t* tempo = new_aubio_tempo("default", win_size, hop_size, (uint_t)sampleRate);
        if (!tempo) return 0.0;
        fvec_t* in = new_fvec(hop_size);
        fvec_t* out = new_fvec(1);
        if (!in || !out)
        {
            if (in) del_fvec(in);
            if (out) del_fvec(out);
            del_aubio_tempo(tempo);
            return 0.0;
        }

        std::vector<double> beatTimes;
        beatTimes.reserve(256);
        size_t pos = 0;
        while (pos < mono.size())
        {
            for (uint_t i = 0; i < hop_size; ++i) in->data[i] = (pos < mono.size()) ? mono[pos++] : 0.0f;
            aubio_tempo_do(tempo, in, out);
            if (out->data[0] != 0) beatTimes.push_back((double)aubio_tempo_get_last_s(tempo));
        }

        double bpm = 0.0;
        if (beatTimes.size() >= 2)
        {
            std::vector<double> periods;
            periods.reserve(beatTimes.size() - 1);
            for (size_t i = 1; i < beatTimes.size(); ++i)
            {
                double dt = beatTimes[i] - beatTimes[i - 1];
                if (dt > 1e-4) periods.push_back(dt);
            }
            if (!periods.empty())
            {
                bpm = 60.0 / medianOfVector(periods);
            }
        }

        if (outReported) *outReported = (double)aubio_tempo_get_bpm(tempo);
        del_fvec(in);
        del_fvec(out);
        del_aubio_tempo(tempo);
        return foldBpm(bpm);
    }
}


double BPMDetection::getBpmMonoAubio(const std::vector<double>& monoPcm, int sampleRate)
{
    if (sampleRate <= 0 || monoPcm.empty()) return 0.0;
    std::vector<float> mono = NormalizeForAnalysis(ToMonoFloat(monoPcm));
    double aubioReported = 0.0;
    double aubioMedian = aubioTempoMedianAndReported(mono, sampleRate, &aubioReported);

	std::cout << "Detected BPM using median period: " << aubioMedian << "\n";
	std::cout << "Detected BPM using aubio_get_bpm: " << aubioReported << "\n";

    double onsetAc = bpmAutocorrOnsetLike(mono, sampleRate, 256, 60.0, 200.0);
    std::cout << "Detected BPM using onset autocorr: " << onsetAc << "\n";
    double bpm0 = clusterPickMedianFolded({ aubioMedian, aubioReported, onsetAc }, 1.5);
    std::cout << "Clustered/folded BPM seed: " << bpm0 << "\n";
    double refined = refineBpmLocalAutocorr(mono, sampleRate, bpm0, 2.0, 0.01, 256);
    std::cout << "Refined BPM (autocorr local): " << refined << "\n";

    if (refined > 0.0) return refined;
    if (bpm0 > 0.0) return bpm0;
    if (aubioMedian > 0.0) return aubioMedian;
    return foldBpm(aubioReported);
}

BPMDetection::BeatGridEstimate BPMDetection::estimateBeatGridMonoAubio(const std::vector<double>& monoPcm, int sampleRate)
{
    BeatGridEstimate out{};
    if (sampleRate <= 0 || monoPcm.empty()) return out;

    std::vector<float> mono = NormalizeForAnalysis(ToMonoFloat(monoPcm));
    out.bpm = getBpmMonoAubio(monoPcm, sampleRate);
    out.audioStart = firstAudioTimeByRms(mono, sampleRate, 0.02, 0.01, -45.0);
    out.approxOnset = aubioFirstOnsetTime(mono, sampleRate, out.audioStart, 1024, 128, "hfc", 0.25f, -60.0f, 0.08f);
    out.kickAttack = findKickAttackStart(mono, sampleRate, out.approxOnset, 200.0, 80.0, 2.5, 6.0, 8.0, 180.0);

    // Mirrors current Python default: ANCHOR_MODE="audio_start", SNAP_AFTER_AUDIO_START=False
    out.t0 = out.audioStart;
    if (!std::isfinite(out.t0) || out.t0 < 0.0) out.t0 = 0.0;

    if (out.bpm > 0.0)
    {
        const double beforeDrift = out.bpm;
        out.bpm = refineBpmByDrift(mono, sampleRate, out.t0, out.bpm, 1.0, 0.01, 7, 18.0, 0.10, 0.90, 0.25);
        std::cout << "BPM before drift: " << beforeDrift << "  after drift: " << out.bpm << "\n";
    }

    return out;
}
