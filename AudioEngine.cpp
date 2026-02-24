#include "AudioEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#if __has_include("third_party/miniaudio.h")
#define WAVEOUT_HAS_MINIAUDIO 1
#define MINIAUDIO_IMPLEMENTATION
#include "third_party/miniaudio.h"
#elif __has_include("miniaudio.h")
#define WAVEOUT_HAS_MINIAUDIO 1
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#else
#define WAVEOUT_HAS_MINIAUDIO 0
#endif

namespace audio
{
    struct AudioEngine::Impl
    {
        struct Biquad
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0;
            double a1 = 0.0, a2 = 0.0;
        };

        struct BiquadState
        {
            double z1 = 0.0, z2 = 0.0;
        };

#if WAVEOUT_HAS_MINIAUDIO
        ma_device device{};
        bool deviceInitialized = false;
        static void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
#endif
        std::mutex sourceMutex;
        std::vector<short>* source = nullptr; // non-owning
        int sampleRate = 0;
        int channels = 2;
        LiveMixConfig liveMix{};
        bool liveMixConfigured = false;
        bool eqCoeffsValid = false;
        int eqCoeffSampleRate = 0;
        double eqCoeffLowDb = 0.0;
        double eqCoeffMidDb = 0.0;
        double eqCoeffHighDb = 0.0;
        Biquad eqLow;
        Biquad eqMid;
        Biquad eqHigh;
        BiquadState eqLowState[2]{};
        BiquadState eqMidState[2]{};
        BiquadState eqHighState[2]{};
        std::atomic<unsigned long long> currentFrame{ 0 };
        std::atomic<bool> playing{ false };
        std::atomic<bool> initialized{ false };

        void resetEqStates()
        {
            eqLowState[0] = {};
            eqLowState[1] = {};
            eqMidState[0] = {};
            eqMidState[1] = {};
            eqHighState[0] = {};
            eqHighState[1] = {};
        }

        static Biquad normalizeBiquad(double b0, double b1, double b2, double a0, double a1, double a2)
        {
            const double invA0 = (std::fabs(a0) > 1e-18) ? (1.0 / a0) : 1.0;
            Biquad q{};
            q.b0 = b0 * invA0;
            q.b1 = b1 * invA0;
            q.b2 = b2 * invA0;
            q.a1 = a1 * invA0;
            q.a2 = a2 * invA0;
            return q;
        }

        static double processBiquad(const Biquad& q, double x, BiquadState& s)
        {
            const double y = q.b0 * x + s.z1;
            s.z1 = q.b1 * x - q.a1 * y + s.z2;
            s.z2 = q.b2 * x - q.a2 * y;
            return y;
        }

        void rebuildEqCoeffsIfNeeded(const LiveMixConfig& cfg)
        {
            if (sampleRate <= 0) return;
            if (eqCoeffsValid &&
                eqCoeffSampleRate == sampleRate &&
                std::fabs(eqCoeffLowDb - cfg.eqLowDb) < 1e-9 &&
                std::fabs(eqCoeffMidDb - cfg.eqMidDb) < 1e-9 &&
                std::fabs(eqCoeffHighDb - cfg.eqHighDb) < 1e-9)
            {
                return;
            }

            auto make_peaking = [&](double fc, double q, double gainDb) -> Biquad
            {
                const double fs = static_cast<double>(sampleRate);
                fc = (std::min)((std::max)(fc, 10.0), fs * 0.45);
                q = (std::max)(q, 0.1);
                const double A = std::pow(10.0, gainDb / 40.0);
                const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
                const double cw0 = std::cos(w0);
                const double sw0 = std::sin(w0);
                const double alpha = sw0 / (2.0 * q);
                return normalizeBiquad(
                    1.0 + alpha * A,
                    -2.0 * cw0,
                    1.0 - alpha * A,
                    1.0 + alpha / A,
                    -2.0 * cw0,
                    1.0 - alpha / A);
            };

            auto make_lowshelf = [&](double fc, double slope, double gainDb) -> Biquad
            {
                const double fs = static_cast<double>(sampleRate);
                fc = (std::min)((std::max)(fc, 10.0), fs * 0.45);
                slope = (std::max)(slope, 0.1);
                const double A = std::pow(10.0, gainDb / 40.0);
                const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
                const double cw0 = std::cos(w0);
                const double sw0 = std::sin(w0);
                const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
                const double t = 2.0 * std::sqrt(A) * alpha;
                return normalizeBiquad(
                    A * ((A + 1.0) - (A - 1.0) * cw0 + t),
                    2.0 * A * ((A - 1.0) - (A + 1.0) * cw0),
                    A * ((A + 1.0) - (A - 1.0) * cw0 - t),
                    (A + 1.0) + (A - 1.0) * cw0 + t,
                    -2.0 * ((A - 1.0) + (A + 1.0) * cw0),
                    (A + 1.0) + (A - 1.0) * cw0 - t);
            };

            auto make_highshelf = [&](double fc, double slope, double gainDb) -> Biquad
            {
                const double fs = static_cast<double>(sampleRate);
                fc = (std::min)((std::max)(fc, 10.0), fs * 0.45);
                slope = (std::max)(slope, 0.1);
                const double A = std::pow(10.0, gainDb / 40.0);
                const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
                const double cw0 = std::cos(w0);
                const double sw0 = std::sin(w0);
                const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
                const double t = 2.0 * std::sqrt(A) * alpha;
                return normalizeBiquad(
                    A * ((A + 1.0) + (A - 1.0) * cw0 + t),
                    -2.0 * A * ((A - 1.0) + (A + 1.0) * cw0),
                    A * ((A + 1.0) + (A - 1.0) * cw0 - t),
                    (A + 1.0) - (A - 1.0) * cw0 + t,
                    2.0 * ((A - 1.0) - (A + 1.0) * cw0),
                    (A + 1.0) - (A - 1.0) * cw0 - t);
            };

            eqLow = make_lowshelf(220.0, 0.9, cfg.eqLowDb);
            eqMid = make_peaking(1000.0, 0.75, cfg.eqMidDb);
            eqHigh = make_highshelf(4200.0, 0.9, cfg.eqHighDb);

            eqCoeffsValid = true;
            eqCoeffSampleRate = sampleRate;
            eqCoeffLowDb = cfg.eqLowDb;
            eqCoeffMidDb = cfg.eqMidDb;
            eqCoeffHighDb = cfg.eqHighDb;
        }

        static inline double pcm16ToNorm(short s)
        {
            return static_cast<double>(s) / 32768.0;
        }

        static inline short normToPcm16(double x)
        {
            if (!std::isfinite(x)) x = 0.0;
            if (x > 1.0) x = 1.0;
            if (x < -1.0) x = -1.0;
            return static_cast<short>(std::lround(x * 32767.0));
        }
    };

#if WAVEOUT_HAS_MINIAUDIO
    void AudioEngine::Impl::audio_callback(ma_device* pDevice, void* pOutput, const void* /*pInput*/, ma_uint32 frameCount)
    {
        auto* impl = static_cast<AudioEngine::Impl*>(pDevice ? pDevice->pUserData : nullptr);
        if (!impl || !pOutput)
            return;

        const ma_uint32 ch = (ma_uint32)(std::max)(1, impl->channels);
        short* out = static_cast<short*>(pOutput);
        std::memset(out, 0, static_cast<size_t>(frameCount) * ch * sizeof(short));

        if (!impl->initialized.load() || !impl->playing.load())
            return;

        std::lock_guard<std::mutex> lock(impl->sourceMutex);
        if (impl->sampleRate <= 0)
            return;

        LiveMixConfig fallbackCfg{};
        if (!impl->liveMixConfigured)
        {
            fallbackCfg.main.interleavedPcm16 = impl->source;
            fallbackCfg.main.sampleRate = impl->sampleRate;
            fallbackCfg.main.channels = impl->channels;
            fallbackCfg.stemPlaybackEnabled = false;
            fallbackCfg.eqLowDb = 0.0;
            fallbackCfg.eqMidDb = 0.0;
            fallbackCfg.eqHighDb = 0.0;
        }
        const LiveMixConfig& cfg = impl->liveMixConfigured ? impl->liveMix : fallbackCfg;

        auto getTotalFramesFor = [](const MixSourceView& s) -> std::size_t
        {
            if (!s.interleavedPcm16 || s.sampleRate <= 0) return 0;
            const std::size_t c = static_cast<std::size_t>((std::max)(1, (std::min)(2, s.channels)));
            return s.interleavedPcm16->size() / c;
        };

        const std::size_t totalFrames = getTotalFramesFor(cfg.main);
        if (totalFrames == 0)
            return;

        std::size_t cursor = static_cast<std::size_t>(impl->currentFrame.load());
        if (cursor >= totalFrames)
        {
            impl->playing.store(false);
            return;
        }

        const bool allStemsOn = cfg.stemEnabled[0] && cfg.stemEnabled[1] && cfg.stemEnabled[2] && cfg.stemEnabled[3];
        const bool useSourceDirect = (!cfg.stemPlaybackEnabled) || (cfg.preferSourceWhenAllStemsOn && allStemsOn);
        const bool eqActive = std::fabs(cfg.eqLowDb) > 1e-6 || std::fabs(cfg.eqMidDb) > 1e-6 || std::fabs(cfg.eqHighDb) > 1e-6;
        if (eqActive)
            impl->rebuildEqCoeffsIfNeeded(cfg);

        auto readFrameFromSource = [&](const MixSourceView& s, std::size_t outFrame, double& outL, double& outR)
        {
            outL = 0.0;
            outR = 0.0;
            if (!s.interleavedPcm16 || s.sampleRate <= 0 || s.interleavedPcm16->empty())
                return;

            const int srcCh = (std::max)(1, (std::min)(2, s.channels));
            const std::size_t srcFrames = s.interleavedPcm16->size() / static_cast<std::size_t>(srcCh);
            if (srcFrames == 0) return;

            std::size_t srcFrame = outFrame;
            if (s.sampleRate != impl->sampleRate)
            {
                srcFrame = static_cast<std::size_t>(
                    (static_cast<unsigned long long>(outFrame) * static_cast<unsigned long long>((std::max)(1, s.sampleRate))) /
                    static_cast<unsigned long long>((std::max)(1, impl->sampleRate)));
            }
            if (srcFrame >= srcFrames) return;

            const std::size_t base = srcFrame * static_cast<std::size_t>(srcCh);
            outL = pcm16ToNorm((*s.interleavedPcm16)[base]);
            outR = (srcCh >= 2 && (base + 1) < s.interleavedPcm16->size())
                ? pcm16ToNorm((*s.interleavedPcm16)[base + 1])
                : outL;
        };

        const std::size_t framesToRender = (std::min)(static_cast<std::size_t>(frameCount), totalFrames - cursor);
        for (std::size_t f = 0; f < framesToRender; ++f)
        {
            const std::size_t frameIndex = cursor + f;
            double l = 0.0, r = 0.0;

            if (useSourceDirect)
            {
                readFrameFromSource(cfg.main, frameIndex, l, r);
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (!cfg.stemEnabled[i]) continue;
                    double sl = 0.0, sr = 0.0;
                    readFrameFromSource(cfg.stems[i], frameIndex, sl, sr);
                    l += sl;
                    r += sr;
                }
            }

            if (eqActive)
            {
                l = processBiquad(impl->eqLow, l, impl->eqLowState[0]);
                l = processBiquad(impl->eqMid, l, impl->eqMidState[0]);
                l = processBiquad(impl->eqHigh, l, impl->eqHighState[0]);
                if (ch >= 2)
                {
                    r = processBiquad(impl->eqLow, r, impl->eqLowState[1]);
                    r = processBiquad(impl->eqMid, r, impl->eqMidState[1]);
                    r = processBiquad(impl->eqHigh, r, impl->eqHighState[1]);
                }
                else
                {
                    r = l;
                }
            }

            const std::size_t outBase = f * static_cast<std::size_t>(ch);
            if (ch >= 2)
            {
                out[outBase] = normToPcm16(l);
                out[outBase + 1] = normToPcm16(r);
            }
            else
            {
                out[outBase] = normToPcm16(0.5 * (l + r));
            }
        }

        cursor += framesToRender;
        impl->currentFrame.store(static_cast<unsigned long long>(cursor));
        if (cursor >= totalFrames)
            impl->playing.store(false);
    }
#endif

    AudioEngine::AudioEngine()
        : m_impl(new Impl())
    {
    }

    AudioEngine::~AudioEngine()
    {
        Shutdown();
        delete m_impl;
        m_impl = nullptr;
    }

    bool AudioEngine::Initialize(std::vector<short>* interleavedPcm16, int sampleRate, bool isStereo)
    {
        if (!m_impl) return false;
        Shutdown();

        if (!interleavedPcm16 || sampleRate <= 0)
            return false;

        m_impl->source = interleavedPcm16;
        m_impl->sampleRate = sampleRate;
        m_impl->channels = isStereo ? 2 : 1;
        m_impl->currentFrame.store(0);
        m_impl->playing.store(false);
        m_impl->liveMixConfigured = false;
        m_impl->liveMix = {};
        m_impl->eqCoeffsValid = false;
        m_impl->resetEqStates();

#if WAVEOUT_HAS_MINIAUDIO
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_s16;
        cfg.playback.channels = (ma_uint32)m_impl->channels;
        cfg.sampleRate = (ma_uint32)sampleRate;
        cfg.dataCallback = Impl::audio_callback;
        cfg.pUserData = m_impl;
        cfg.periodSizeInMilliseconds = 10;

        if (ma_device_init(nullptr, &cfg, &m_impl->device) != MA_SUCCESS)
        {
            m_impl->source = nullptr;
            m_impl->sampleRate = 0;
            m_impl->channels = 2;
            return false;
        }
        m_impl->deviceInitialized = true;
        if (ma_device_start(&m_impl->device) != MA_SUCCESS)
        {
            ma_device_uninit(&m_impl->device);
            m_impl->deviceInitialized = false;
            m_impl->source = nullptr;
            m_impl->sampleRate = 0;
            m_impl->channels = 2;
            return false;
        }
        m_impl->initialized.store(true);
        return true;
#else
        return false;
#endif
    }

    bool AudioEngine::ReplaceSource(std::vector<short>* interleavedPcm16, int sampleRate, bool isStereo)
    {
        if (!m_impl || !m_impl->initialized.load())
            return false;
        if (!interleavedPcm16 || sampleRate <= 0)
            return false;

        const int newChannels = isStereo ? 2 : 1;
        const bool needReinit = (sampleRate != m_impl->sampleRate) || (newChannels != m_impl->channels);
        const bool wasPlaying = m_impl->playing.load();
        const std::size_t curFrame = GetCurrentFrame();
        std::vector<short>* oldSource = m_impl->source;

        if (needReinit)
        {
            Shutdown();
            if (!Initialize(interleavedPcm16, sampleRate, isStereo))
                return false;
            SeekFrame(curFrame);
            if (wasPlaying) Play();
            return true;
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
            m_impl->source = interleavedPcm16;
            if (m_impl->liveMixConfigured &&
                (m_impl->liveMix.main.interleavedPcm16 == nullptr || m_impl->liveMix.main.interleavedPcm16 == oldSource))
            {
                m_impl->liveMix.main.interleavedPcm16 = interleavedPcm16;
                m_impl->liveMix.main.sampleRate = sampleRate;
                m_impl->liveMix.main.channels = newChannels;
            }
            m_impl->eqCoeffsValid = false;
            m_impl->resetEqStates();
        }
        SeekFrame(curFrame);
        return true;
    }

    bool AudioEngine::SetLiveMixConfig(const LiveMixConfig& cfg)
    {
        if (!m_impl || !m_impl->initialized.load())
            return false;
#if !WAVEOUT_HAS_MINIAUDIO
        (void)cfg;
        return false;
#else
        std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
        m_impl->liveMix = cfg;
        if (m_impl->liveMix.main.interleavedPcm16 == nullptr)
        {
            m_impl->liveMix.main.interleavedPcm16 = m_impl->source;
            m_impl->liveMix.main.sampleRate = m_impl->sampleRate;
            m_impl->liveMix.main.channels = m_impl->channels;
        }
        m_impl->liveMixConfigured = true;
        m_impl->eqCoeffsValid = false;
        return true;
#endif
    }

    void AudioEngine::Shutdown()
    {
        if (!m_impl) return;
        m_impl->playing.store(false);
        m_impl->initialized.store(false);

#if WAVEOUT_HAS_MINIAUDIO
        if (m_impl->deviceInitialized)
        {
            ma_device_uninit(&m_impl->device);
            m_impl->deviceInitialized = false;
        }
#endif
        std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
        m_impl->source = nullptr;
        m_impl->liveMix = {};
        m_impl->liveMixConfigured = false;
        m_impl->sampleRate = 0;
        m_impl->channels = 2;
        m_impl->eqCoeffsValid = false;
        m_impl->resetEqStates();
        m_impl->currentFrame.store(0);
    }

    bool AudioEngine::Play()
    {
        if (!m_impl || !m_impl->initialized.load()) return false;
        const std::size_t totalFrames = GetTotalFrames();
        if (totalFrames > 0 && GetCurrentFrame() >= totalFrames)
            SeekFrame(0);
        m_impl->playing.store(true);
        return true;
    }

    void AudioEngine::Pause()
    {
        if (!m_impl) return;
        m_impl->playing.store(false);
    }

    void AudioEngine::Stop()
    {
        Pause();
        SeekFrame(0);
    }

    void AudioEngine::SeekFrame(std::size_t frame)
    {
        if (!m_impl) return;
        const std::size_t totalFrames = GetTotalFrames();
        if (totalFrames == 0)
        {
            m_impl->currentFrame.store(0);
            return;
        }
        frame = (std::min)(frame, totalFrames - 1);
        {
            std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
            m_impl->resetEqStates();
        }
        m_impl->currentFrame.store(static_cast<unsigned long long>(frame));
    }

    std::size_t AudioEngine::GetCurrentFrame() const
    {
        if (!m_impl) return 0;
        return static_cast<std::size_t>(m_impl->currentFrame.load());
    }

    bool AudioEngine::IsPlaying() const
    {
        return m_impl && m_impl->playing.load();
    }

    bool AudioEngine::IsInitialized() const
    {
        return m_impl && m_impl->initialized.load();
    }

    int AudioEngine::GetSampleRate() const
    {
        return m_impl ? m_impl->sampleRate : 0;
    }

    bool AudioEngine::IsStereo() const
    {
        return m_impl ? (m_impl->channels >= 2) : false;
    }

    std::size_t AudioEngine::GetTotalFrames() const
    {
        if (!m_impl) return 0;
        std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
        if (!m_impl->source) return 0;
        const std::size_t ch = static_cast<std::size_t>((std::max)(1, m_impl->channels));
        return m_impl->source->size() / ch;
    }

    bool AudioEngine::BackendAvailable()
    {
#if WAVEOUT_HAS_MINIAUDIO
        return true;
#else
        return false;
#endif
    }
}
