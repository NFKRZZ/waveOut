#pragma once
#include <windows.h>
#include <cstddef>
#include <vector>
#include <string>

namespace WaveformWindow
{
    struct GridOverlayConfig
    {
        bool enabled = false;
        double bpm = 0.0;
        double t0Seconds = 0.0;
        int beatsPerBar = 4;
        double audioStartSeconds = 0.0;
        double approxOnsetSeconds = 0.0;
        double kickAttackSeconds = 0.0;
    };

    struct PlaybackSyncSnapshot
    {
        bool valid = false;
        bool playing = false;
        bool isStereo = false;
        int sampleRate = 0;
        std::size_t totalFrames = 0;
        double currentFrame = 0.0;
        double zoomFactor = 1.0;
        long long panOffsetFrames = 0;
        double playheadXRatio = 0.25;
        GridOverlayConfig grid{};
    };

    struct StemPlaybackConfig
    {
        bool enabled = false;
        std::vector<short>* vocalsInterleavedStereo = nullptr;
        int vocalsSampleRate = 0;
        int vocalsChannels = 2;
        std::vector<short>* drumsInterleavedStereo = nullptr;
        int drumsSampleRate = 0;
        int drumsChannels = 2;
        std::vector<short>* bassInterleavedStereo = nullptr;
        int bassSampleRate = 0;
        int bassChannels = 2;
        std::vector<short>* chordsInterleavedStereo = nullptr;
        int chordsSampleRate = 0;
        int chordsChannels = 2;
    };

    // Copies the samples into the window thread (safer).
    void ShowWaveformAsyncCopy(const std::vector<short>& samples, int sampleRate, const std::wstring& title = L"Waveform");

    // Uses a pointer to your vector. YOU MUST ENSURE `samples` stays valid while the window is open.
    void ShowWaveformAsyncRef(std::vector<short>* samples, int sampleRate, const std::wstring& title = L"Waveform");

    // Variants that optionally start playback and enable scrolling playhead.
    // These write a temporary 16-bit WAV and use MCI for playback. Use the Stereo variants
    // if you pass interleaved stereo samples (L,R,L,R,...).
    void ShowWaveformAsyncCopyPlay(const std::vector<short>& samples, int sampleRate, bool startPlaying = false, const std::wstring& title = L"Waveform");
    void ShowWaveformAsyncRefPlay(std::vector<short>* samples, int sampleRate, bool startPlaying = false, const std::wstring& title = L"Waveform");

    // Stereo variants: samples must be interleaved stereo (L,R,L,R,...).
    void ShowWaveformAsyncCopyPlayStereo(const std::vector<short>& interleavedStereoSamples, int sampleRate, bool startPlaying = false, const std::wstring& title = L"Waveform");
    void ShowWaveformAsyncRefPlayStereo(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying = false, const std::wstring& title = L"Waveform");
    void ShowWaveformAsyncRefPlayStereoGrid(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const GridOverlayConfig& grid, const std::wstring& title = L"Waveform");
    void ShowWaveformAsyncRefPlayStereoGridStems(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const GridOverlayConfig& grid, const StemPlaybackConfig& stems, const std::wstring& title = L"Waveform");

    // Read-only snapshot for companion windows (e.g., spectrogram) to stay synced
    // with the currently active waveform playback/viewport.
    bool GetPlaybackSyncSnapshot(PlaybackSyncSnapshot& out);
}
