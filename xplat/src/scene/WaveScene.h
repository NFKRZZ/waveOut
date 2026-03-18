#pragma once

#include <cstdint>
#include <string>
#include <vector>

class WaveScene
{
public:
    bool Initialize();
    bool LoadAudioFile(const std::string& path);
    void Tick(double deltaSeconds);

    void TogglePaused() { paused_ = !paused_; }
    void SetPaused(bool paused) { paused_ = paused; }
    void SeekToStart() { playbackSeconds_ = 0.0; }
    void NudgePlayback(double deltaSeconds);
    void AdjustZoom(double factor);

    bool paused() const { return paused_; }
    double zoomFactor() const { return zoomFactor_; }

    int visibleBins() const { return visibleBins_; }
    float playheadXNorm() const { return playheadXNorm_; }

    const std::vector<float>& baseMin() const { return baseMin_; }
    const std::vector<float>& baseMax() const { return baseMax_; }
    const std::vector<float>& lowMin() const { return lowMin_; }
    const std::vector<float>& lowMax() const { return lowMax_; }
    const std::vector<float>& midMin() const { return midMin_; }
    const std::vector<float>& midMax() const { return midMax_; }
    const std::vector<float>& highMin() const { return highMin_; }
    const std::vector<float>& highMax() const { return highMax_; }

    const std::vector<float>& beatLinesX() const { return beatLinesX_; }
    const std::vector<float>& barLinesX() const { return barLinesX_; }

    int spectrogramWidth() const { return songSpectrogramWidth_; }
    int spectrogramHeight() const { return spectrogramHeight_; }
    const std::vector<std::uint32_t>& spectrogramBgra() const { return songSpectrogramBgra_; }
    float spectrogramU0() const { return spectrogramU0_; }
    float spectrogramU1() const { return spectrogramU1_; }
    std::uint64_t spectrogramRevision() const { return spectrogramRevision_; }

private:
    void BuildSongEnvelopes();
    void BuildSongEnvelopesFromMono(const std::vector<float>& mono, int sampleRate);
    void BuildSongSpectrogram();
    void BuildSongSpectrogramFromEnvelopes();
    void UpdateViewport();
    void BuildVisibleEnvelopeWindow();
    void BuildVisibleGridLines();
    float SampleLinear(const std::vector<float>& src, double idx) const;

    bool initialized_ = false;

    double songDurationSeconds_ = 180.0;
    double visibleWindowSeconds_ = 8.0;
    double minVisibleWindowSeconds_ = 1.0;
    double maxVisibleWindowSeconds_ = 30.0;
    double bpm_ = 126.0;
    int beatsPerBar_ = 4;
    double t0Seconds_ = 0.0;
    double playbackRate_ = 1.0;

    double playbackSeconds_ = 0.0;
    float playheadXNorm_ = 0.25f;
    bool paused_ = false;
    double zoomFactor_ = 1.0;
    double viewportLeftSeconds_ = 0.0;
    double viewportRightSeconds_ = 8.0;

    int songBins_ = 32768;
    int visibleBins_ = 1024;

    int songSpectrogramWidth_ = 4096;
    int spectrogramHeight_ = 256;
    float spectrogramU0_ = 0.0f;
    float spectrogramU1_ = 1.0f;
    std::uint64_t spectrogramRevision_ = 0;

    std::vector<float> songBaseMin_;
    std::vector<float> songBaseMax_;
    std::vector<float> songLowMin_;
    std::vector<float> songLowMax_;
    std::vector<float> songMidMin_;
    std::vector<float> songMidMax_;
    std::vector<float> songHighMin_;
    std::vector<float> songHighMax_;

    std::vector<std::uint32_t> songSpectrogramBgra_;

    std::vector<float> baseMin_;
    std::vector<float> baseMax_;
    std::vector<float> lowMin_;
    std::vector<float> lowMax_;
    std::vector<float> midMin_;
    std::vector<float> midMax_;
    std::vector<float> highMin_;
    std::vector<float> highMax_;

    std::vector<float> beatLinesX_;
    std::vector<float> barLinesX_;
};
