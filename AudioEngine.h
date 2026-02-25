#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace audio
{
    struct MixSourceView
    {
        std::vector<short>* interleavedPcm16 = nullptr; // non-owning
        int sampleRate = 0;
        int channels = 2;
    };

    struct LiveMixConfig
    {
        MixSourceView main{};
        bool stemPlaybackEnabled = false;
        MixSourceView stems[4]{};
        bool stemEnabled[4]{ true, true, true, true };
        bool preferSourceWhenAllStemsOn = true;
        double eqLowDb = 0.0;
        double eqMidDb = 0.0;
        double eqHighDb = 0.0;
        double masterGainDb = 0.0;
    };

    class AudioEngine
    {
    public:
        AudioEngine();
        ~AudioEngine();

        AudioEngine(const AudioEngine&) = delete;
        AudioEngine& operator=(const AudioEngine&) = delete;

        bool Initialize(std::vector<short>* interleavedPcm16, int sampleRate, bool isStereo);
        bool InitializeFromWavFile(const std::wstring& wavPath);
        bool ReplaceSource(std::vector<short>* interleavedPcm16, int sampleRate, bool isStereo);
        bool SetLiveMixConfig(const LiveMixConfig& cfg);
        void Shutdown();

        bool Play();
        void Pause();
        void Stop();
        void SeekFrame(std::size_t frame);
        bool SetPlaybackRate(double rate);
        double GetPlaybackRate() const;

        std::size_t GetCurrentFrame() const;
        bool IsPlaying() const;
        bool IsInitialized() const;

        int GetSampleRate() const;
        bool IsStereo() const;
        std::size_t GetTotalFrames() const;

        static bool BackendAvailable();

    private:
        struct Impl;
        Impl* m_impl;
    };
}
