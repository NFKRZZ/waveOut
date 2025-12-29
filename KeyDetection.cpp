#include "KeyDetection.h"

#include <fftw3.h>
#include <cmath>
#include <algorithm>
#include <iostream>

//#include "keyfinder/keyfinder.h"

#ifndef FFTW_ESTIMATE
#define FFTW_ESTIMATE 64
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace KeyDetection {




    // -----------------------------------------------------------------------------
    // K–K raw profiles (C as index 0, then C#, D, ... B)
    const std::array<double, 12> C_MAJOR_RAW = { {
        6.35, 2.23, 3.48, 2.33,
        4.38, 4.09, 2.52, 5.19,
        2.39, 3.66, 2.29, 2.88
    } };
    const std::array<double, 12> C_MINOR_RAW = { {
        6.33, 2.68, 3.52, 5.38,
        2.60, 3.53, 2.54, 4.75,
        3.98, 2.69, 3.34, 5.29
    } };

    // -----------------------------------------------------------------------------
    // Internal helpers

    static std::vector<double> makeHann(int N) {
        std::vector<double> w(N);
        for (int n = 0; n < N; ++n)
            w[n] = 0.5 * (1.0 - std::cos(2.0 * M_PI * n / (N - 1)));
        return w;
    }

    // Rotate a base C-profile UP by k semitones (clockwise on chroma circle)
    static std::array<double, 12> rotateBy(const std::array<double, 12>& base, int k) {
        std::array<double, 12> out{};
        for (int i = 0; i < 12; ++i)
            out[i] = base[(i + (k % 12) + 12) % 12]; // +k rotation
        return out;
    }

    static inline int pcWrap(int x) { return (x % 12 + 12) % 12; }

    void normalize(std::array<double, 12>& v) {
        double sumsq = 0.0;
        for (double x : v) sumsq += x * x;
        if (sumsq <= 0.0) return;
        double n = std::sqrt(sumsq);
        for (double& x : v) x /= n;
    }

    void buildKeyProfiles(std::array<std::array<double, 12>, 24>& outProfiles,
        std::array<std::string, 24>& outNames)
    {
        auto maj0 = C_MAJOR_RAW;
        auto min0 = C_MINOR_RAW;
        normalize(maj0);
        normalize(min0);

        static constexpr const char* MAJ_NAMES[12] = {
            "C Major","C# Major","D Major","D# Major","E Major","F Major",
            "F# Major","G Major","G# Major","A Major","A# Major","B Major"
        };
        static constexpr const char* MIN_NAMES[12] = {
            "C Minor","C# Minor","D Minor","D# Minor","E Minor","F Minor",
            "F# Minor","G Minor","G# Minor","A Minor","A# Minor","B Minor"
        };

        for (int k = 0; k < 12; ++k) {
            outProfiles[k] = rotateBy(maj0, k);
            outProfiles[12 + k] = rotateBy(min0, k);
            outNames[k] = MAJ_NAMES[k];
            outNames[12 + k] = MIN_NAMES[k];
        }
    }

    double cosineSimilarity(const std::array<double, 12>& a,
        const std::array<double, 12>& b)
    {
        double dot = 0.0;
        for (int i = 0; i < 12; ++i) dot += a[i] * b[i];
        return dot;
    }

    int detectKey(const std::array<double, 12>& normalizedChroma,
        const std::array<std::array<double, 12>, 24>& profiles)
    {
        int best = 0;
        double bestScore = -1.0;
        for (int k = 0; k < 24; ++k) {
            double s = cosineSimilarity(normalizedChroma, profiles[k]);
            if (s > bestScore) { bestScore = s; best = k; }
        }
        return best;
    }

    // -----------------------------------------------------------------------------
    // Core chroma with band parameters (internal)

    static std::array<double, 12>
        computeMeanChromaWithBand(const std::vector<double>& audio, int fs,
            double MIN_FREQ, double MAX_FREQ)
    {
        const int N = 4096;
        const int H = 1024;
        const int Nf = (int)audio.size();

        const double ENERGY_GATE = 1e-8;   // skip near-silence frames
        const double FLAT_GATE = 0.28;   // spectral flatness threshold
        const int    TUNE_WARMUP = 400;    // voiced frames for tuning estimate

        // Hann + FFT
        const std::vector<double> hann = makeHann(N);
        std::vector<double> in(N);
        fftw_complex* out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (N / 2 + 1));
        fftw_plan plan = fftw_plan_dft_r2c_1d(N, in.data(), out, FFTW_ESTIMATE);

        std::array<double, 12> C{}; int frames = 0;

        // tuning accumulators
        double tuneNum = 0.0, tuneDen = 0.0; int tuneCnt = 0;

        auto add_pc = [](double freq, double mag, std::array<double, 12>& frame) {
            double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
            double pc = std::fmod(midi, 12.0); if (pc < 0) pc += 12.0;
            int i0 = (int)std::floor(pc), i1 = (i0 + 1) % 12;
            double d = pc - i0;
            double c0 = std::cos(0.5 * M_PI * d), s0 = std::sin(0.5 * M_PI * d);
            frame[i0] += mag * (c0 * c0);
            frame[i1] += mag * (s0 * s0);
        };

        for (int start = 0; start + N <= Nf; start += H)
        {
            // window
            for (int n = 0; n < N; ++n) in[n] = audio[start + n] * hann[n];
            fftw_execute(plan);

            // gates
            double E = 0.0, ar = 0.0, logsum = 0.0; int bins = 0;
            for (int k = 1; k <= N / 2; ++k) {
                double re = out[k][0], im = out[k][1];
                double mag = std::hypot(re, im);
                E += re * re + im * im;
                ar += mag;
                if (mag > 1e-12) { logsum += std::log(mag); ++bins; }
            }
            if (E < ENERGY_GATE) continue;
            double geo = bins ? std::exp(logsum / bins) : 0.0;
            double sfm = (bins && ar > 0.0) ? geo / (ar / bins) : 0.0;
            if (sfm > FLAT_GATE) continue;

            // tuning
            double tuneFactor = 1.0;
            if (tuneCnt < TUNE_WARMUP) {
                int kmax = 1; double maxMag = 0.0;
                for (int k = 1; k <= N / 2; ++k) {
                    double mag = std::hypot(out[k][0], out[k][1]);
                    if (mag > maxMag) { maxMag = mag; kmax = k; }
                }
                double fk = (double)kmax / N * fs;
                if (fk > MIN_FREQ && fk < MAX_FREQ && maxMag > 0.0) {
                    double midi = 69.0 + 12.0 * std::log2(fk / 440.0);
                    int    midiR = (int)std::round(midi);
                    double fRef = 440.0 * std::pow(2.0, (midiR - 69) / 12.0);
                    double ratio = fk / fRef;
                    tuneNum += ratio * maxMag; tuneDen += maxMag; ++tuneCnt;
                }
            }
            if (tuneCnt >= std::min(TUNE_WARMUP, 10) && tuneDen > 0.0) {
                double ratioAvg = tuneNum / tuneDen; // ~ A_est / 440
                tuneFactor = 1.0 / ratioAvg;
            }

            // soft chroma + light harmonic fold-down and bass tilt
            std::array<double, 12> frame{};
            for (int k = 1; k <= N / 2; ++k) {
                double mag = std::hypot(out[k][0], out[k][1]);
                if (mag <= 1e-12) continue;

                double fk = ((double)k / N) * fs * tuneFactor;
                if (fk < MIN_FREQ || fk > MAX_FREQ) continue;

                // bass tilt (gently prioritize <300 Hz)
                double tilt = fk < 300.0 ? 1.15 : 1.0;
                mag *= tilt;

                // fundamental
                add_pc(fk, mag, frame);
                // fold 2nd & 3rd harmonics (helps bright synth timbres)
                add_pc(fk / 2.0, 0.5 * mag, frame);
                add_pc(fk / 3.0, 0.33 * mag, frame);
            }

            // per-frame L2 norm (equal vote)
            double ss = 0.0; for (double v : frame) ss += v * v;
            if (ss > 0.0) { double n = std::sqrt(ss); for (double& v : frame) v /= n; }

            for (int i = 0; i < 12; ++i) C[i] += frame[i];
            ++frames;
        }

        fftw_destroy_plan(plan);
        fftw_free(out);

        normalize(C);
        return C;
    }

    // Public: EDM-friendly band limits for full-band chroma
    std::array<double, 12> getMeanChroma(const std::vector<double>& audio, int fs)
    {
        return computeMeanChromaWithBand(audio, fs, /*MIN*/70.0, /*MAX*/2000.0);
    }
    static Key mapKeyfinderToMajorKeyEnum(KeyFinder::key_t k)
    {
        using KeyFinder::key_t;

        // Majors (libKeyFinder uses flats for 5 keys)
        switch (k)
        {
        case KeyFinder::C_MAJOR:        return Key::C_MAJOR;
        case KeyFinder::D_FLAT_MAJOR:   return Key::C_SHARP_MAJOR; // Db -> C#
        case KeyFinder::D_MAJOR:        return Key::D_MAJOR;
        case KeyFinder::E_FLAT_MAJOR:   return Key::D_SHARP_MAJOR; // Eb -> D#
        case KeyFinder::E_MAJOR:        return Key::E_MAJOR;
        case KeyFinder::F_MAJOR:        return Key::F_MAJOR;
        case KeyFinder::G_FLAT_MAJOR:   return Key::F_SHARP_MAJOR; // Gb -> F#
        case KeyFinder::G_MAJOR:        return Key::G_MAJOR;
        case KeyFinder::A_FLAT_MAJOR:   return Key::G_SHARP_MAJOR; // Ab -> G#
        case KeyFinder::A_MAJOR:        return Key::A_MAJOR;
        case KeyFinder::B_FLAT_MAJOR:   return Key::A_SHARP_MAJOR; // Bb -> A#
        case KeyFinder::B_MAJOR:        return Key::B_MAJOR;

        case KeyFinder::SILENCE:        return Key::NO_KEY;
        default: break;
        }
        // Minors -> relative majors (same pitch-class set; good for scale clamping)
        switch (k)
        {
        case KeyFinder::A_MINOR:        return Key::C_MAJOR;
        case KeyFinder::B_FLAT_MINOR:   return Key::C_SHARP_MAJOR; // Db major
        case KeyFinder::B_MINOR:        return Key::D_MAJOR;
        case KeyFinder::C_MINOR:        return Key::D_SHARP_MAJOR; // Eb major
        case KeyFinder::D_FLAT_MINOR:   return Key::E_MAJOR;
        case KeyFinder::D_MINOR:        return Key::F_MAJOR;
        case KeyFinder::E_FLAT_MINOR:   return Key::F_SHARP_MAJOR; // Gb major
        case KeyFinder::E_MINOR:        return Key::G_MAJOR;
        case KeyFinder::F_MINOR:        return Key::G_SHARP_MAJOR; // Ab major
        case KeyFinder::G_FLAT_MINOR:   return Key::A_MAJOR;
        case KeyFinder::G_MINOR:        return Key::A_SHARP_MAJOR; // Bb major
        case KeyFinder::A_FLAT_MINOR:   return Key::B_MAJOR;
        default:                        return Key::NO_KEY;
        }
    }

    Key getKey(const std::vector<double>& pcmData, int sampleRate,  KeyFinder::KeyFinder& f)
    {
        KeyFinder::AudioData a;
        a.setFrameRate(sampleRate);
        a.setChannels(1);
        a.addToSampleCount(pcmData.size());
        for (int i = 0; i < static_cast<unsigned int>(pcmData.size()); ++i) {
            a.setSample(i, pcmData[i]); // expects normalized-ish audio (e.g., -1..1)
        }
        const KeyFinder::key_t key = f.keyOfAudio(a);
        std::cout << "Lol high" << key << std::endl;
        return mapKeyfinderToMajorKeyEnum(key);

        
    }

    std::array<double, 12> getMeanChromaBand(const std::vector<double>& audio,
        int Fs, double minFreq, double maxFreq)
    {
        return computeMeanChromaWithBand(audio, Fs, minFreq, maxFreq);
    }

    // -----------------------------------------------------------------------------
    // Diagnostics

    void printTopKeys(const std::array<double, 12>& chroma,
        const std::array<std::array<double, 12>, 24>& profiles,
        const std::array<std::string, 24>& names,
        int k)
    {
        struct Item { int idx; double s; };
        std::vector<Item> v; v.reserve(24);
        for (int i = 0; i < 24; ++i)
            v.push_back({ i, cosineSimilarity(chroma, profiles[i]) });

        if (k < 1) k = 1; if (k > 24) k = 24;
        std::partial_sort(v.begin(), v.begin() + k, v.end(),
            [](const Item& a, const Item& b) { return a.s > b.s; });

        for (int i = 0; i < k; ++i)
            std::cout << (i + 1) << ". " << names[v[i].idx] << "  " << v[i].s << "\n";

        if (k >= 2)
            std::cout << "margin(top-2) = " << (v[0].s - v[1].s) << "\n";
    }

    KeyDetection::KeyVoteSummary
        voteKeysOverTime(const std::vector<double>& audio, int Fs,
            const std::array<std::array<double, 12>, 24>& profiles,
            const std::array<std::string, 24>& /*names*/,
            double winSec, double hopSec)
    {
        KeyVoteSummary R;
        //const int W = std::max(1, (int)std::lround(winSec * Fs));
        //const int H = std::max(1, (int)std::lround(hopSec * Fs));

        const int W = 0;
        const int H = 0;

        int total = 0;

        for (int start = 0; start + W <= (int)audio.size(); start += H) {
            std::vector<double> chunk(audio.begin() + start, audio.begin() + start + W);
            auto chroma = getMeanChroma(chunk, Fs);
            int idx = detectKey(chroma, profiles);
            ++R.counts[idx];
            ++total;
        }

        if (total > 0) {
            int bestIdx = 0;
            for (int i = 1; i < 24; ++i)
                if (R.counts[i] > R.counts[bestIdx]) bestIdx = i;
            R.best = bestIdx;
            R.stability = (double)R.counts[bestIdx] / total;
        }
        return R;
    }

    // -----------------------------------------------------------------------------
    // Bass + Third aware final decision

    int detectKeyBassAware(const std::vector<double>& audio, int Fs,
        const std::array<std::array<double, 12>, 24>& profiles,
        const std::array<std::string, 24>& /*names*/,
        double lambda, double mu)
    {
        // Full-band chroma (narrowed to 70..2000 Hz)
        const auto cFull = getMeanChroma(audio, Fs);
        // Bass-only chroma
        const auto cBass = getMeanChromaBand(audio, Fs, /*min*/40.0, /*max*/300.0);

        auto score = [&](int k)->double {
            const bool isMajor = (k < 12);
            const int tonic = isMajor ? k : (k - 12);
            const int pcV = pcWrap(tonic + 7); // dominant
            const int pcIV = pcWrap(tonic + 5); // subdominant

            // Bass prior emphasizes I, then V, then IV (weights sum to 1.0)
            const double bassPrior = 0.60 * cBass[tonic] + 0.25 * cBass[pcV] + 0.15 * cBass[pcIV];

            // Third-consistency:
            //  For majors we want major 3rd strong (t+4) vs minor 3rd (t+3).
            //  For minors we want minor 3rd strong (t+3) vs major 3rd (t+4).
            const int pcMaj3 = pcWrap(tonic + 4);
            const int pcMin3 = pcWrap(tonic + 3);
            const double thirdCons = isMajor
                ? (cFull[pcMaj3] - cFull[pcMin3])
                : (cFull[pcMin3] - cFull[pcMaj3]);

            const double base = cosineSimilarity(cFull, profiles[k]);
            return base + lambda * bassPrior + mu * thirdCons;
        };

        int best = 0; double bestS = -1.0;
        for (int k = 0; k < 24; ++k) {
            double s = score(k);
            if (s > bestS) { bestS = s; best = k; }
        }
        return best;
    }

} // namespace KeyDetection
