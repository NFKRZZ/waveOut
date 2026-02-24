// dsp.cpp
#include "dsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dsp
{
    static constexpr double PI = 3.141592653589793238462643383279502884;
    static constexpr double EPS = 1e-20;

    double clamp(double x, double lo, double hi)
    {
        return (x < lo) ? lo : (x > hi ? hi : x);
    }

    double hann(int i, int N)
    {
        if (N <= 1) return 1.0;
        return 0.5 * (1.0 - std::cos(2.0 * PI * (double)i / (double)(N - 1)));
    }

    double energy(const float* x, std::size_t n, std::size_t stride)
    {
        if (!x || n == 0 || stride == 0) return 0.0;
        double acc = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            double v = (double)x[i * stride];
            acc += v * v;
        }
        return acc;
    }

    double rms(const float* x, std::size_t n, std::size_t stride)
    {
        if (n == 0) return 0.0;
        return std::sqrt(energy(x, n, stride) / (double)n);
    }

    static inline double clamp01(double x)
    {
        return (x < 0.0) ? 0.0 : (x > 1.0 ? 1.0 : x);
    }

    RGB8 color_from_bands(double low, double mid, double high,
        double total, double maxTotal,
        double brightGamma, double chromaGamma)
    {
        // ratios -> chroma; total -> brightness
        const double sum = low + mid + high + 1e-30;
        double r = low / sum;
        double g = mid / sum;
        double b = high / sum;

        // brighten by energy (log compressed)
        double bright = 0.0;
        if (maxTotal > 0.0)
            bright = std::log1p(std::max(total, 0.0)) / std::log1p(maxTotal + 1e-30);

        bright = clamp01(std::pow(bright, brightGamma));

        // punch colors a bit
        r = std::pow(r, chromaGamma);
        g = std::pow(g, chromaGamma);
        b = std::pow(b, chromaGamma);

        int R = (int)(255.0 * clamp01(bright * r));
        int G = (int)(255.0 * clamp01(bright * g));
        int B = (int)(255.0 * clamp01(bright * b));

        RGB8 out;
        out.r = (uint8_t)R;
        out.g = (uint8_t)G;
        out.b = (uint8_t)B;
        return out;
    }

    // -------------------------
    // EMA
    // -------------------------
    void EMA::setAlpha(double alpha)
    {
        // Keep it sane
        m_alpha = clamp(alpha, 1e-6, 1.0);
    }

    void EMA::reset(double y)
    {
        m_y = y;
        m_init = false;
    }

    double EMA::process(double x)
    {
        if (!m_init)
        {
            m_y = x;
            m_init = true;
            return m_y;
        }
        m_y = m_alpha * x + (1.0 - m_alpha) * m_y;
        return m_y;
    }

    // -------------------------
    // FftwR2C
    // -------------------------
    FftwR2C::~FftwR2C()
    {
        destroy();
    }

    FftwR2C::FftwR2C(FftwR2C&& other) noexcept
    {
        *this = std::move(other);
    }

    FftwR2C& FftwR2C::operator=(FftwR2C&& other) noexcept
    {
        if (this == &other) return *this;

        destroy();

        m_nfft = other.m_nfft;
        m_in = other.m_in;
        m_out = other.m_out;
        m_plan = other.m_plan;

        other.m_nfft = 0;
        other.m_in = nullptr;
        other.m_out = nullptr;
        other.m_plan = nullptr;

        return *this;
    }

    void FftwR2C::init(int nfft)
    {
        if (nfft <= 0) nfft = 1024;
        if (nfft == m_nfft && m_plan) return;

        destroy();

        m_nfft = nfft;
        m_in = (double*)fftw_malloc(sizeof(double) * (std::size_t)m_nfft);
        m_out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (std::size_t)(m_nfft / 2 + 1));

        // FFTW_ESTIMATE is fast to create; good for interactive UI. (MEASURE can stall.)
        m_plan = fftw_plan_dft_r2c_1d(m_nfft, m_in, m_out, FFTW_ESTIMATE);
    }

    void FftwR2C::destroy()
    {
        if (m_plan) { fftw_destroy_plan(m_plan); m_plan = nullptr; }
        if (m_out) { fftw_free(m_out); m_out = nullptr; }
        if (m_in) { fftw_free(m_in); m_in = nullptr; }
        m_nfft = 0;
    }

    void FftwR2C::execute()
    {
        if (m_plan) fftw_execute(m_plan);
    }

    // -------------------------
    // StftBandAnalyzer
    // -------------------------
    StftBandAnalyzer::StftBandAnalyzer(int nfft, int sampleRate)
    {
        init(nfft, sampleRate);
    }

    void StftBandAnalyzer::init(int nfft, int sampleRate)
    {
        m_sr = (sampleRate > 1) ? sampleRate : 44100;
        if (nfft <= 0) nfft = 1024;

        m_fft.init(nfft);

        m_window.resize((std::size_t)nfft);
        for (int i = 0; i < nfft; ++i)
            m_window[(std::size_t)i] = hann(i, nfft);
    }

    BandEnergies StftBandAnalyzer::analyzeCenteredPcm16(const short* samples,
        std::size_t totalSamples,
        std::size_t centerIndex,
        const BandConfig& cfg)
    {
        BandEnergies out{};

        if (!samples || totalSamples == 0 || m_fft.nfft() <= 0)
            return out;

        const int nfft = m_fft.nfft();
        const long long half = (long long)nfft / 2;
        const long long segStart = (long long)centerIndex - half;

        // Fill windowed FFT input
        double* in = m_fft.in();
        for (int i = 0; i < nfft; ++i)
        {
            long long idx = segStart + i;
            double s = 0.0;
            if (idx >= 0 && (std::size_t)idx < totalSamples)
                s = (double)samples[(std::size_t)idx] / 32768.0;

            in[i] = s * m_window[(std::size_t)i];
        }

        m_fft.execute();

        const fftw_complex* X = m_fft.out();

        const double lowMax = std::max(1.0, cfg.lowMaxHz);
        const double midMax = std::max(lowMax + 1.0, cfg.midMaxHz);

        // bins 1..nfft/2 (skip DC at 0)
        const int kMax = nfft / 2;
        for (int k = 1; k <= kMax; ++k)
        {
            const double re = X[k][0];
            const double im = X[k][1];
            const double mag2 = re * re + im * im;

            const double f = (double)k * (double)m_sr / (double)nfft;

            if (f < lowMax) out.low += mag2;
            else if (f < midMax) out.mid += mag2;
            else out.high += mag2;
        }

        out.total = out.low + out.mid + out.high;
        return out;
    }

    // -------------------------
    // build_waveform_cache_pcm16
    // -------------------------
    void build_waveform_cache_pcm16(const short* samples,
        std::size_t totalSamples,
        int sampleRate,
        int widthPx,
        StftBandAnalyzer& analyzer,
        const BandConfig& cfg,
        std::vector<short>& outMinS,
        std::vector<short>& outMaxS,
        std::vector<uint32_t>& outColorref)
    {
        outMinS.clear();
        outMaxS.clear();
        outColorref.clear();

        if (!samples || totalSamples == 0 || widthPx <= 0)
            return;

        if (analyzer.sampleRate() != sampleRate)
            analyzer.init(analyzer.nfft() > 0 ? analyzer.nfft() : 1024, sampleRate);

        outMinS.resize((std::size_t)widthPx);
        outMaxS.resize((std::size_t)widthPx);
        outColorref.resize((std::size_t)widthPx);

        const int samplesPerPx = (int)std::max<std::size_t>(1, totalSamples / (std::size_t)widthPx);

        // Store energies first so we can normalize brightness by maxTotal
        std::vector<BandEnergies> energies((std::size_t)widthPx);

        double maxTotal = 0.0;

        for (int x = 0; x < widthPx; ++x)
        {
            const std::size_t start = (std::size_t)x * (std::size_t)samplesPerPx;
            const std::size_t end = std::min(totalSamples, start + (std::size_t)samplesPerPx);

            // min/max in this pixel column
            short mn = SHRT_MAX;
            short mx = SHRT_MIN;

            if (start < end)
            {
                for (std::size_t i = start; i < end; ++i)
                {
                    short v = samples[i];
                    if (v < mn) mn = v;
                    if (v > mx) mx = v;
                }
            }
            else
            {
                mn = 0; mx = 0;
            }

            outMinS[(std::size_t)x] = mn;
            outMaxS[(std::size_t)x] = mx;

            // spectral features centered in the column
            const std::size_t center = (start + end) / 2;
            energies[(std::size_t)x] = analyzer.analyzeCenteredPcm16(samples, totalSamples, center, cfg);

            if (energies[(std::size_t)x].total > maxTotal)
                maxTotal = energies[(std::size_t)x].total;
        }

        // Convert energies to colors
        for (int x = 0; x < widthPx; ++x)
        {
            const auto& e = energies[(std::size_t)x];
            RGB8 c = color_from_bands(e.low, e.mid, e.high, e.total, maxTotal);
            outColorref[(std::size_t)x] = to_colorref(c);
        }
    }
}
