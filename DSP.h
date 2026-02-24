// dsp.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <fftw3.h>

namespace dsp
{
    // -------------------------
    // Small structs
    // -------------------------
    struct RGB8
    {
        uint8_t r = 0, g = 0, b = 0;
    };

    // Windows COLORREF packing (0x00BBGGRR)
    inline uint32_t to_colorref(RGB8 c)
    {
        return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16);
    }

    struct BandConfig
    {
        // Split frequencies into low / mid / high using these cutoffs.
        // low:  [0, lowMaxHz)
        // mid:  [lowMaxHz, midMaxHz)
        // high: [midMaxHz, Nyquist]
        double lowMaxHz = 250.0;
        double midMaxHz = 2000.0;
    };

    struct BandEnergies
    {
        double low = 0.0;
        double mid = 0.0;
        double high = 0.0;
        double total = 0.0;
    };

    // -------------------------
    // Helpers
    // -------------------------
    double clamp(double x, double lo, double hi);

    double hann(int i, int N);

    double energy(const float* x, std::size_t n, std::size_t stride = 1);
    double rms(const float* x, std::size_t n, std::size_t stride = 1);

    inline float pcm16_to_float(short s)
    {
        return (float)((double)s / 32768.0);
    }

    // Spectral-color mapping (band ratios -> RGB) with brightness driven by total energy
    RGB8 color_from_bands(double low, double mid, double high,
        double total, double maxTotal,
        double brightGamma = 0.70,
        double chromaGamma = 0.80);

    // -------------------------
    // EMA smoother (handy for RMS/energy smoothing)
    // -------------------------
    class EMA
    {
    public:
        EMA() = default;
        explicit EMA(double alpha) : m_alpha(alpha) {}

        void setAlpha(double alpha);   // alpha in (0,1], higher = less smoothing
        void reset(double y = 0.0);

        double process(double x);

        double value() const { return m_y; }
        bool initialized() const { return m_init; }

    private:
        double m_alpha = 0.2;
        double m_y = 0.0;
        bool m_init = false;
    };

    // -------------------------
    // FFTW reusable R2C plan
    // -------------------------
    class FftwR2C
    {
    public:
        FftwR2C() = default;
        explicit FftwR2C(int nfft) { init(nfft); }
        ~FftwR2C();

        FftwR2C(const FftwR2C&) = delete;
        FftwR2C& operator=(const FftwR2C&) = delete;

        FftwR2C(FftwR2C&& other) noexcept;
        FftwR2C& operator=(FftwR2C&& other) noexcept;

        void init(int nfft);      // (re)create plan/buffers
        void destroy();

        int nfft() const { return m_nfft; }
        double* in() { return m_in; }
        fftw_complex* out() { return m_out; } // length nfft/2 + 1
        const fftw_complex* out() const { return m_out; }

        void execute();

    private:
        int m_nfft = 0;
        double* m_in = nullptr;
        fftw_complex* m_out = nullptr;
        fftw_plan m_plan = nullptr;
    };

    // -------------------------
    // STFT band analyzer (centered window)
    // -------------------------
    class StftBandAnalyzer
    {
    public:
        StftBandAnalyzer() = default;
        StftBandAnalyzer(int nfft, int sampleRate);

        void init(int nfft, int sampleRate); // safe to call again
        int nfft() const { return m_fft.nfft(); }
        int sampleRate() const { return m_sr; }

        // Compute band energies from a segment centered at 'centerIndex' in PCM16 samples.
        BandEnergies analyzeCenteredPcm16(const short* samples,
            std::size_t totalSamples,
            std::size_t centerIndex,
            const BandConfig& cfg);

    private:
        int m_sr = 44100;
        FftwR2C m_fft;
        std::vector<double> m_window; // Hann window, size nfft
    };

    // -------------------------
    // High-level helper:
    // Build min/max + spectral color arrays per pixel column
    // -------------------------
    void build_waveform_cache_pcm16(const short* samples,
        std::size_t totalSamples,
        int sampleRate,
        int widthPx,
        StftBandAnalyzer& analyzer,
        const BandConfig& cfg,
        std::vector<short>& outMinS,
        std::vector<short>& outMaxS,
        std::vector<uint32_t>& outColorref);
}
