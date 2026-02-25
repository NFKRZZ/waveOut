// WaveformWindow.cpp
#include "WaveFormWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>   // for GET_X_LPARAM / GET_Y_LPARAM
#include <mmsystem.h>
#include <mciapi.h>
#include <timeapi.h>
#pragma comment(lib,"Winmm.lib")
#pragma comment(lib,"Winmm.lib") // mci lives here too

#include <memory>
#include <algorithm>
#include <vector>
#include <string>
#include <climits>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>

#include "DSP.h"   // dsp helpers (FFTW STFT + cache builder)
#include "PianoRollRenderer.h"
#include "PianoSpectrogramUI.h"
#include "AudioEngine.h"
#include "SpectrogramWindow.h"

using namespace WaveformWindow;

namespace
{
    constexpr UINT WM_WAVEFORM_TICK = WM_APP + 17;
    constexpr double kMaxVisibleWindowSeconds = 30.0;
    constexpr int kControlStripHeightPx = 58;
    constexpr int kPianoTabCount = 6;
    constexpr int kPianoSpectrogramTabIndex = 5;
    constexpr int kPianoTabStripHeightPx = 26;
    constexpr int kPianoTabGapPx = 2;
    constexpr int kStemButtonBaseId = 1101; // 1101..1104
    constexpr int kToolbarHalfSpeedCommandId = 1003;
    constexpr int kToolbarButtonCount = 7;  // play, pause, vocals, drums, bass, chords, 1/2x
    constexpr int kToolbarKnobCount = 4;    // low, mid, high, master volume
    static const int kEmbeddedPianoSpecNfftChoices[] = { 1024, 2048, 4096, 8192 };
    static const double kEmbeddedPianoSpecDbRanges[] = { 48.0, 60.0, 72.0, 84.0, 96.0, 108.0 };
    enum EmbeddedPianoGridMode
    {
        kEmbeddedPianoGrid_None = 0,
        kEmbeddedPianoGrid_1_6_Step,
        kEmbeddedPianoGrid_1_4_Step,
        kEmbeddedPianoGrid_1_3_Step,
        kEmbeddedPianoGrid_1_2_Step,
        kEmbeddedPianoGrid_Step,
        kEmbeddedPianoGrid_1_6_Beat,
        kEmbeddedPianoGrid_1_4_Beat,
        kEmbeddedPianoGrid_1_3_Beat,
        kEmbeddedPianoGrid_1_2_Beat,
        kEmbeddedPianoGrid_Beat,
        kEmbeddedPianoGrid_Bar,
        kEmbeddedPianoGrid_Count
    };

    struct SharedPlaybackState
    {
        std::atomic<bool> valid{ false };
        std::atomic<bool> playing{ false };
        std::atomic<bool> isStereo{ false };
        std::atomic<int> sampleRate{ 0 };
        std::atomic<long long> totalFrames{ 0 };
        std::atomic<long long> currentFrame{ 0 };
        std::atomic<double> playbackRate{ 1.0 };
        std::atomic<double> zoomFactor{ 1.0 };
        std::atomic<long long> panOffsetFrames{ 0 };
        std::atomic<double> playheadXRatio{ 0.25 };
        std::atomic<bool> gridEnabled{ false };
        std::atomic<double> gridBpm{ 0.0 };
        std::atomic<double> gridT0Seconds{ 0.0 };
        std::atomic<int> gridBeatsPerBar{ 4 };
        std::atomic<double> gridAudioStartSeconds{ 0.0 };
        std::atomic<double> gridApproxOnsetSeconds{ 0.0 };
        std::atomic<double> gridKickAttackSeconds{ 0.0 };
    };

    struct SharedPlaybackAudioState
    {
        std::atomic<bool> valid{ false };
        std::atomic<const std::vector<short>*> mainSamples{ nullptr };
        std::atomic<int> sampleRate{ 0 };
        std::atomic<int> mainChannels{ 2 };
        std::atomic<bool> stemPlaybackEnabled{ false };
        std::atomic<const std::vector<short>*> stems[4]{};
        std::atomic<int> stemSampleRate[4]{};
        std::atomic<int> stemChannels[4]{};
        std::atomic<bool> stemEnabled[4]{};
        std::atomic<double> eqLowDb{ 0.0 };
        std::atomic<double> eqMidDb{ 0.0 };
        std::atomic<double> eqHighDb{ 0.0 };
        std::atomic<double> masterGainDb{ 0.0 };
        std::atomic<double> playbackRate{ 1.0 };
    };

    SharedPlaybackState gSharedPlayback;
    SharedPlaybackAudioState gSharedPlaybackAudio;
    std::atomic<int> gSharedPianoGridMode{ PianoRollRenderer::Grid_Beat };
    std::atomic<HWND> gSharedWaveformHwnd{ nullptr };

    HFONT GetWaveUiMessageFont()
    {
        static HFONT sFont = nullptr;
        static bool sInit = false;
        if (!sInit)
        {
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
                sFont = CreateFontIndirectW(&ncm.lfMessageFont);
            sInit = true;
        }
        return sFont ? sFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }
}

struct EnvelopeMipLevel
{
    int block = 0;      // frames per envelope block at this level
    size_t blocks = 0;  // number of blocks
    std::vector<float> baseMinF, baseMaxF;
    std::vector<float> lowMinF, lowMaxF;
    std::vector<float> midMinF, midMaxF;
    std::vector<float> highMinF, highMaxF;
};

struct EnvelopeLevelView
{
    int block = 0;
    size_t blocks = 0;
    const std::vector<float>* baseMinF = nullptr;
    const std::vector<float>* baseMaxF = nullptr;
    const std::vector<float>* lowMinF = nullptr;
    const std::vector<float>* lowMaxF = nullptr;
    const std::vector<float>* midMinF = nullptr;
    const std::vector<float>* midMaxF = nullptr;
    const std::vector<float>* highMinF = nullptr;
    const std::vector<float>* highMaxF = nullptr;
};

struct ThreadParam
{
    std::vector<short>* samples = nullptr; // pointer to data (owned if ownsSamples==true)
    bool ownsSamples = false;
    int sampleRate = 44100;
    std::wstring title;

    // whether samples are interleaved stereo (L,R,L,R,...)
    bool isStereo = false;

    // ---- playback / mci ----
    std::wstring tempPath;    // temporary WAV path used for playback
    std::wstring mciAlias;    // alias used by MCI
    bool mciOpened = false;
    bool autoStart = false;
    HWND hwnd = nullptr;
    HWND hBtnPlay = nullptr;
    HWND hBtnPause = nullptr;
    HWND hBtnStem[4]{};
    audio::AudioEngine audioEngine;
    bool useAudioEngine = false;

    // ---- view & interaction ----
    double zoomFactor = 8.0;          // >1 = zoomed in (visibleSamples = totalSamples / zoom)
    double playheadXRatio = 0.25;     // playhead position within viewport (0..1)
    std::atomic<bool> playing{ false };
    std::chrono::steady_clock::time_point playStartTime;
    size_t pausedSampleIndex = 0;     // sample index when paused (frame index, NOT sample index if stereo)
    long long panOffsetSamples = 0;   // manual pan offset (in frames)
    int dragLastX = 0;
    bool dragScrubActive = false;
    bool dragPanActive = false;
    bool dragResumeAfterScrub = false;
    double dragScrubFrameAccum = 0.0;
    DWORD renderIntervalMs = 8;
    HANDLE renderTimerQueue = nullptr;
    HANDLE renderTimer = nullptr;
    std::atomic<bool> renderTickQueued{ false };
    bool buttonTextRefreshPending = true;
    int buttonTextRefreshRetries = 3;
    RECT toolbarButtonRects[kToolbarButtonCount]{};
    RECT toolbarKnobRects[kToolbarKnobCount]{};
    int toolbarPressedButton = -1;
    bool toolbarPressedInside = false;
    int toolbarDragKnob = -1;
    int toolbarDragStartY = 0;
    double toolbarDragStartDb = 0.0;
    bool toolbarKnobChangedDuringDrag = false;

    // overall playback EQ (applied when rebuilding MCI temp WAV)
    double eqLowDb = 0.0;
    double eqMidDb = 0.0;
    double eqHighDb = 0.0;
    double masterGainDb = 0.0;
    bool halfSpeedPlayback = false;

    // ---- drawing region config ----
    int regionDiv = 10; // legacy field (unused by current renderer)

    // ---- "aubioTest.py" style color-wave envelopes (base + low/mid/high) ----
    int envBlock = 512 + 256; // mirrors aubioTest.py ENV_BLOCK
    float displayHeadroom = 0.98f;
    float plotYRange = 1.05f;
    std::vector<float> baseMinF, baseMaxF;
    std::vector<float> lowMinF, lowMaxF;
    std::vector<float> midMinF, midMaxF;
    std::vector<float> highMinF, highMaxF;
    size_t envBlocks = 0;
    std::vector<EnvelopeMipLevel> envMipLevels; // coarser envelope caches (2x,4x,...) for zoomed-out draw
    bool envCacheKeyValid = false;
    std::uint64_t envCacheSampleHash = 0;
    std::wstring envCachePath;
    std::wstring sourceFilePathHint;

    // ---- beat grid overlay ----
    bool gridEnabled = false;
    double gridBpm = 0.0;
    double gridT0Seconds = 0.0;
    int gridBeatsPerBar = 4;
    double gridAudioStartSeconds = 0.0;
    double gridApproxOnsetSeconds = 0.0;
    double gridKickAttackSeconds = 0.0;

    // ---- piano roll tabs (main + stems) ----
    int activePianoRollTab = 0;
    int pianoRollGridMode = PianoRollRenderer::Grid_Beat;
    bool sharedPianoGridMenuOpen = false;
    RECT sharedPianoGridMenuRc{};
    int sharedPianoGridMenuHoverIndex = -1;
    bool tabDetachDragActive = false;
    int tabDetachDragTab = -1;
    POINT tabDetachDragStartPt{};
    RECT tabDetachDragTabsRc{};
    RECT pianoRollRcGridButton{};
    bool liveResizeActive = false;

    // ---- embedded piano spectrogram tab cache (viewport-synced) ----
    bool embeddedPianoSpecUseProcessedMix = true;
    double embeddedPianoSpecDbRange = 84.0;
    int embeddedPianoSpecNfft = 4096;
    int embeddedPianoSpecNfftChoiceIndex = 2;
    int embeddedPianoSpecGridMode = kEmbeddedPianoGrid_Beat;
    int embeddedPianoSpecMidiMin = 12;   // C0
    int embeddedPianoSpecMidiMax = 108;  // C8 default view; slider can move upward
    bool embeddedPianoSpecMidiSliderDragActive = false;
    double embeddedPianoSpecMinFreqHz = 20.0;
    dsp::FftwR2C embeddedPianoSpecFft;
    std::vector<double> embeddedPianoSpecWindow;
    std::vector<double> embeddedPianoSpecAnalysisMono;
    std::vector<double> embeddedPianoSpecRowDbScratch;
    double embeddedPianoSpecFftAmpScale = 1.0;
    std::vector<uint32_t> embeddedPianoSpecImageBgra;
    int embeddedPianoSpecImageW = 0;
    int embeddedPianoSpecImageH = 0;
    bool embeddedPianoSpecDirty = true;
    double embeddedPianoSpecCacheTLeft = std::numeric_limits<double>::quiet_NaN();
    double embeddedPianoSpecCacheTRight = std::numeric_limits<double>::quiet_NaN();
    RECT embeddedPianoSpecRcProcessedToggle{};
    RECT embeddedPianoSpecRcDbButton{};
    RECT embeddedPianoSpecRcResButton{};
    RECT embeddedPianoSpecRcGridButton{};

    // ---- optional stem playback mixing ----
    bool stemPlaybackEnabled = false;
    std::vector<short>* stemVocals = nullptr;
    int stemVocalsSampleRate = 0;
    int stemVocalsChannels = 2;
    std::vector<short>* stemDrums = nullptr;
    int stemDrumsSampleRate = 0;
    int stemDrumsChannels = 2;
    std::vector<short>* stemBass = nullptr;
    int stemBassSampleRate = 0;
    int stemBassChannels = 2;
    std::vector<short>* stemChords = nullptr;
    int stemChordsSampleRate = 0;
    int stemChordsChannels = 2;
    bool stemEnabled[4]{ true, true, true, true }; // vocals, drums, bass, chords
    std::vector<short> stemMixScratch; // current playback mix (interleaved stereo)

    // ---- cached render data (per pixel column) ----
    int cacheW = 0;
    bool cacheDirty = true;
    std::vector<short> cacheMinS;
    std::vector<short> cacheMaxS;
    std::vector<uint32_t> cacheColorref; // COLORREF packed 0x00BBGGRR

    // ---- spectral analyzer ----
    int nfft = 1024;
    dsp::StftBandAnalyzer analyzer;
    dsp::BandConfig bands;
};

struct PixelAggregate
{
    short minV = 0;
    short maxV = 0;
    double avgV = 0.0;
};

static void MciOpenIfNeeded(ThreadParam* tp);
static void MciClose(ThreadParam* tp);
static void SyncPausedFromMciPosition(ThreadParam* tp);
static void SeekToFrame(ThreadParam* tp, size_t frame, bool resumePlayback);
static size_t GetTotalFrames(const ThreadParam* tp);
static double GetCurrentFrameForView(const ThreadParam* tp);
static bool HasStemPlayback(const ThreadParam* tp);
static bool UsingAudioEngine(const ThreadParam* tp);
static bool ShouldUseSourcePlaybackDirect(const ThreadParam* tp);
static std::vector<short>* GetStemVectorByIndex(ThreadParam* tp, int idx);
static int GetStemSampleRateByIndex(const ThreadParam* tp, int idx);
static int GetStemChannelsByIndex(const ThreadParam* tp, int idx);
static bool PushAudioEngineLiveMixConfig(ThreadParam* tp);
static void LayoutTopButtons(HWND hwnd, const ThreadParam* tp);
static void RebuildStemPlaybackAndRetuneMci(ThreadParam* tp);
static void RebuildPlaybackAndRetuneMci(ThreadParam* tp);
static int HitTestToolbarButton(const ThreadParam* tp, POINT pt);
static int HitTestToolbarKnob(const ThreadParam* tp, POINT pt);
static void InvalidateToolbar(HWND hwnd);
static void InvalidateToolbarButton(HWND hwnd, const ThreadParam* tp, int idx);
static void InvalidateToolbarKnob(HWND hwnd, const ThreadParam* tp, int idx);
static EnvelopeLevelView SelectEnvelopeLevelForFramesPerPixel(const ThreadParam* tp, double framesPerPixel);
static bool TryLoadEnvelopeCache(ThreadParam* tp, std::size_t totalFrames);
static void TrySaveEnvelopeCache(const ThreadParam* tp, std::size_t totalFrames);
static void PublishPlaybackAudioState(const ThreadParam* tp);
static void ClearPlaybackAudioState();
static void InvalidateEmbeddedPianoSpec(ThreadParam* tp);
static void OpenPianoSpectrogramPopoutFromWaveform(ThreadParam* tp);
static void LayoutEmbeddedPianoSpecControls(ThreadParam* tp, const RECT& pianoRc);
static bool HandleEmbeddedPianoSpecClick(HWND hwnd, ThreadParam* tp, const RECT& pianoRc, POINT pt);
static void LayoutPianoRollGridControl(ThreadParam* tp, const RECT& pianoRc);
static bool HandlePianoRollGridClick(HWND hwnd, ThreadParam* tp, const RECT& pianoRc, POINT pt);
static void OpenSharedPianoGridMenu(ThreadParam* tp, const RECT& anchorRc, const RECT& clientRc);
static void CloseSharedPianoGridMenu(ThreadParam* tp);
static bool HandleSharedPianoGridMenuMouseDown(HWND hwnd, ThreadParam* tp, POINT pt);
static bool UpdateSharedPianoGridMenuHover(HWND hwnd, ThreadParam* tp, POINT pt);
static void DrawSharedPianoGridMenu(HDC hdc, const ThreadParam* tp);
static const wchar_t* PianoRollGridModeLabel(int mode);
static const wchar_t* EmbeddedPianoGridModeLabel(int mode);
static void DrawEmbeddedPianoSpectrogramMusicalGrid(HDC hdc, const RECT& plotRc, const RECT& timeRc,
    ThreadParam* tp, double tLeft, double tRight);
static RECT ComputeEmbeddedPianoSpecLeftScaleRect(const RECT& rc);
static RECT ComputeEmbeddedPianoSpecMidiSliderTrackRect(const RECT& leftRc);
static bool SetEmbeddedPianoSpecMidiSliderFromY(ThreadParam* tp, const RECT& trackRc, int y);
static void DrawEmbeddedPianoSpectrogramTab(HDC hdc, const RECT& pianoRc, ThreadParam* tp,
    double tLeft, double tRight, double curSeconds, int playheadX);

static void PublishPlaybackAudioState(const ThreadParam* tp)
{
    if (!tp)
    {
        ClearPlaybackAudioState();
        return;
    }

    gSharedPlaybackAudio.mainSamples.store(tp->samples);
    gSharedPlaybackAudio.sampleRate.store(tp->sampleRate);
    gSharedPlaybackAudio.mainChannels.store(tp->isStereo ? 2 : 1);
    gSharedPlaybackAudio.stemPlaybackEnabled.store(HasStemPlayback(tp));

    const std::vector<short>* stemPtrs[4] = { tp->stemVocals, tp->stemDrums, tp->stemBass, tp->stemChords };
    const int stemRates[4] = { tp->stemVocalsSampleRate, tp->stemDrumsSampleRate, tp->stemBassSampleRate, tp->stemChordsSampleRate };
    const int stemChs[4] = { tp->stemVocalsChannels, tp->stemDrumsChannels, tp->stemBassChannels, tp->stemChordsChannels };
    for (int i = 0; i < 4; ++i)
    {
        gSharedPlaybackAudio.stems[i].store(stemPtrs[i]);
        gSharedPlaybackAudio.stemSampleRate[i].store(stemRates[i]);
        gSharedPlaybackAudio.stemChannels[i].store(stemChs[i]);
        gSharedPlaybackAudio.stemEnabled[i].store(tp->stemEnabled[i]);
    }

    gSharedPlaybackAudio.eqLowDb.store(tp->eqLowDb);
    gSharedPlaybackAudio.eqMidDb.store(tp->eqMidDb);
    gSharedPlaybackAudio.eqHighDb.store(tp->eqHighDb);
    gSharedPlaybackAudio.masterGainDb.store(tp->masterGainDb);

    double rate = 1.0;
    if (UsingAudioEngine(tp))
        rate = tp->audioEngine.GetPlaybackRate();
    else if (tp->halfSpeedPlayback)
        rate = 0.5;
    if (!std::isfinite(rate) || rate <= 0.0) rate = 1.0;
    gSharedPlaybackAudio.playbackRate.store(rate);
    gSharedPlaybackAudio.valid.store(true);
}

static void ClearPlaybackAudioState()
{
    gSharedPlaybackAudio.valid.store(false);
    gSharedPlaybackAudio.mainSamples.store(nullptr);
    gSharedPlaybackAudio.sampleRate.store(0);
    gSharedPlaybackAudio.mainChannels.store(2);
    gSharedPlaybackAudio.stemPlaybackEnabled.store(false);
    for (int i = 0; i < 4; ++i)
    {
        gSharedPlaybackAudio.stems[i].store(nullptr);
        gSharedPlaybackAudio.stemSampleRate[i].store(0);
        gSharedPlaybackAudio.stemChannels[i].store(2);
        gSharedPlaybackAudio.stemEnabled[i].store(false);
    }
    gSharedPlaybackAudio.eqLowDb.store(0.0);
    gSharedPlaybackAudio.eqMidDb.store(0.0);
    gSharedPlaybackAudio.eqHighDb.store(0.0);
    gSharedPlaybackAudio.masterGainDb.store(0.0);
    gSharedPlaybackAudio.playbackRate.store(1.0);
}

static void PublishPlaybackSync(const ThreadParam* tp)
{
    if (!tp)
    {
        gSharedPlayback.valid.store(false);
        ClearPlaybackAudioState();
        return;
    }

    const size_t totalFrames = GetTotalFrames(tp);
    double curFrameD = GetCurrentFrameForView(tp);
    if (totalFrames > 0)
    {
        const double maxFrame = static_cast<double>(totalFrames - 1);
        if (!std::isfinite(curFrameD)) curFrameD = 0.0;
        curFrameD = std::clamp(curFrameD, 0.0, maxFrame);
    }
    else
    {
        curFrameD = 0.0;
    }

    gSharedPlayback.playing.store(tp->playing.load());
    gSharedPlayback.isStereo.store(tp->isStereo);
    gSharedPlayback.sampleRate.store(tp->sampleRate);
    gSharedPlayback.totalFrames.store(static_cast<long long>(totalFrames));
    gSharedPlayback.currentFrame.store(static_cast<long long>(std::llround(curFrameD)));
    gSharedPlayback.playbackRate.store(UsingAudioEngine(tp) ? tp->audioEngine.GetPlaybackRate() : (tp->halfSpeedPlayback ? 0.5 : 1.0));
    gSharedPlayback.zoomFactor.store(tp->zoomFactor);
    gSharedPlayback.panOffsetFrames.store(tp->panOffsetSamples);
    gSharedPlayback.playheadXRatio.store(tp->playheadXRatio);
    gSharedPlayback.gridEnabled.store(tp->gridEnabled);
    gSharedPlayback.gridBpm.store(tp->gridBpm);
    gSharedPlayback.gridT0Seconds.store(tp->gridT0Seconds);
    gSharedPlayback.gridBeatsPerBar.store(tp->gridBeatsPerBar);
    gSharedPlayback.gridAudioStartSeconds.store(tp->gridAudioStartSeconds);
    gSharedPlayback.gridApproxOnsetSeconds.store(tp->gridApproxOnsetSeconds);
    gSharedPlayback.gridKickAttackSeconds.store(tp->gridKickAttackSeconds);
    PublishPlaybackAudioState(tp);
    gSharedPlayback.valid.store(true);
}

static void ClearPlaybackSync()
{
    gSharedPlayback.valid.store(false);
    gSharedPlayback.playing.store(false);
    gSharedPlayback.playbackRate.store(1.0);
    ClearPlaybackAudioState();
}

static bool HasStemPlayback(const ThreadParam* tp)
{
    return tp && tp->stemPlaybackEnabled &&
        (tp->stemVocals || tp->stemDrums || tp->stemBass || tp->stemChords);
}

static bool UsingAudioEngine(const ThreadParam* tp)
{
    return tp && tp->useAudioEngine && tp->audioEngine.IsInitialized();
}

static bool ShouldUseSourcePlaybackDirect(const ThreadParam* tp)
{
    if (!tp || !tp->samples || tp->samples->empty())
        return false;

    // No stems configured: source is the only playback source.
    if (!HasStemPlayback(tp))
        return true;

    // If all stem toggles are ON, prefer the original source over recombining stems
    // to avoid separator artifacts/noise.
    for (int i = 0; i < 4; ++i)
    {
        if (!tp->stemEnabled[i])
            return false;
    }
    return true;
}

static bool PushAudioEngineLiveMixConfig(ThreadParam* tp)
{
    if (!tp || !UsingAudioEngine(tp))
        return false;

    audio::LiveMixConfig cfg{};
    cfg.main.interleavedPcm16 = tp->samples;
    cfg.main.sampleRate = tp->sampleRate;
    cfg.main.channels = tp->isStereo ? 2 : 1;
    cfg.stemPlaybackEnabled = HasStemPlayback(tp);
    cfg.preferSourceWhenAllStemsOn = true;
    cfg.eqLowDb = tp->eqLowDb;
    cfg.eqMidDb = tp->eqMidDb;
    cfg.eqHighDb = tp->eqHighDb;
    cfg.masterGainDb = tp->masterGainDb;

    for (int i = 0; i < 4; ++i)
    {
        cfg.stems[i].interleavedPcm16 = GetStemVectorByIndex(tp, i);
        cfg.stems[i].sampleRate = GetStemSampleRateByIndex(tp, i);
        cfg.stems[i].channels = GetStemChannelsByIndex(tp, i);
        cfg.stemEnabled[i] = tp->stemEnabled[i];
    }

    return tp->audioEngine.SetLiveMixConfig(cfg);
}

static std::vector<short>* GetStemVectorByIndex(ThreadParam* tp, int idx)
{
    if (!tp) return nullptr;
    switch (idx)
    {
    case 0: return tp->stemVocals;
    case 1: return tp->stemDrums;
    case 2: return tp->stemBass;
    case 3: return tp->stemChords;
    default: return nullptr;
    }
}

static int GetStemSampleRateByIndex(const ThreadParam* tp, int idx)
{
    if (!tp) return 0;
    switch (idx)
    {
    case 0: return tp->stemVocalsSampleRate;
    case 1: return tp->stemDrumsSampleRate;
    case 2: return tp->stemBassSampleRate;
    case 3: return tp->stemChordsSampleRate;
    default: return 0;
    }
}

static int GetStemChannelsByIndex(const ThreadParam* tp, int idx)
{
    if (!tp) return 2;
    switch (idx)
    {
    case 0: return tp->stemVocalsChannels;
    case 1: return tp->stemDrumsChannels;
    case 2: return tp->stemBassChannels;
    case 3: return tp->stemChordsChannels;
    default: return 2;
    }
}

static const wchar_t* GetStemButtonLabelByIndex(int idx)
{
    switch (idx)
    {
    case 0: return L"Vocals";
    case 1: return L"Drums";
    case 2: return L"Bass";
    case 3: return L"Chords";
    default: return L"Stem";
    }
}

static COLORREF GetStemButtonColorByIndex(int idx)
{
    switch (idx)
    {
    case 0: return RGB(220, 70, 70);   // red
    case 1: return RGB(70, 190, 90);   // green
    case 2: return RGB(70, 120, 230);  // blue
    case 3: return RGB(230, 165, 55);  // amber/orange
    default: return RGB(140, 140, 140);
    }
}

static int GetToolbarButtonCommandId(int toolbarIdx)
{
    if (toolbarIdx == 0) return 1001; // Play
    if (toolbarIdx == 1) return 1002; // Pause
    if (toolbarIdx == 6) return kToolbarHalfSpeedCommandId; // 1/2x
    if (toolbarIdx >= 2 && toolbarIdx < kToolbarButtonCount) return kStemButtonBaseId + (toolbarIdx - 2);
    return 0;
}

static const wchar_t* GetToolbarButtonLabel(int toolbarIdx)
{
    if (toolbarIdx == 0) return L"Play";
    if (toolbarIdx == 1) return L"Pause";
    if (toolbarIdx == 6) return L"1/2x";
    if (toolbarIdx >= 2 && toolbarIdx < kToolbarButtonCount) return GetStemButtonLabelByIndex(toolbarIdx - 2);
    return L"";
}

static COLORREF GetToolbarButtonAccent(const ThreadParam* tp, int toolbarIdx)
{
    (void)tp;
    if (toolbarIdx == 0) return RGB(55, 180, 95);
    if (toolbarIdx == 1) return RGB(230, 170, 55);
    if (toolbarIdx == 6) return RGB(80, 210, 220);
    if (toolbarIdx >= 2 && toolbarIdx < kToolbarButtonCount) return GetStemButtonColorByIndex(toolbarIdx - 2);
    return RGB(120, 120, 120);
}

static bool IsToolbarButtonVisible(const ThreadParam* tp, int toolbarIdx)
{
    if (toolbarIdx < 0 || toolbarIdx >= kToolbarButtonCount) return false;
    if (toolbarIdx < 2) return true;
    if (toolbarIdx == 6) return true;
    return HasStemPlayback(tp);
}

static bool IsToolbarButtonLogicalOn(const ThreadParam* tp, int toolbarIdx)
{
    if (!tp) return false;
    if (toolbarIdx == 0) return tp->playing.load();
    if (toolbarIdx == 1) return !tp->playing.load();
    if (toolbarIdx == 6) return tp->halfSpeedPlayback;
    if (toolbarIdx >= 2 && toolbarIdx < kToolbarButtonCount) return tp->stemEnabled[toolbarIdx - 2];
    return false;
}

static bool IsToolbarButtonClickable(const ThreadParam* tp, int toolbarIdx)
{
    if (toolbarIdx < 0 || toolbarIdx >= kToolbarButtonCount) return false;
    if (toolbarIdx < 2) return true;
    if (toolbarIdx == 6) return tp && tp->useAudioEngine && tp->audioEngine.IsInitialized();
    return HasStemPlayback(tp);
}

static void DrawToolbarButtonVisual(HDC hdc, const RECT& rc, const wchar_t* label, COLORREF accent, bool logicalOn, bool clickable, bool pressed)
{
    auto scale = [](BYTE c, double s) -> BYTE
    {
        int v = static_cast<int>(std::lround(static_cast<double>(c) * s));
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        return static_cast<BYTE>(v);
    };

    COLORREF fill = RGB(42, 42, 42);
    COLORREF border = RGB(78, 78, 78);
    COLORREF text = RGB(220, 220, 220);
    if (!clickable)
    {
        fill = RGB(28, 28, 28);
        border = RGB(55, 55, 55);
        text = RGB(110, 110, 110);
    }
    else if (logicalOn)
    {
        const double s = pressed ? 0.72 : 0.95;
        fill = RGB(scale(GetRValue(accent), s * 0.55), scale(GetGValue(accent), s * 0.55), scale(GetBValue(accent), s * 0.55));
        border = RGB(scale(GetRValue(accent), s * 0.95), scale(GetGValue(accent), s * 0.95), scale(GetBValue(accent), s * 0.95));
        text = RGB(248, 248, 248);
    }
    else
    {
        fill = pressed ? RGB(34, 34, 34) : RGB(30, 30, 30);
        border = RGB(82, 82, 82);
        text = RGB(175, 175, 175);
    }

    HBRUSH br = CreateSolidBrush(fill);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    RECT ir = rc;
    ir.left += 4;
    ir.right = ir.left + 6;
    ir.top += 5;
    ir.bottom -= 5;
    HBRUSH indBr = CreateSolidBrush((clickable && logicalOn) ? accent : RGB(70, 70, 70));
    FillRect(hdc, &ir, indBr);
    DeleteObject(indBr);

    if (clickable && logicalOn)
    {
        HPEN topPen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
        HGDIOBJ prevTop = SelectObject(hdc, topPen);
        MoveToEx(hdc, rc.left + 1, rc.top + 1, NULL);
        LineTo(hdc, rc.right - 1, rc.top + 1);
        SelectObject(hdc, prevTop);
        DeleteObject(topPen);
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text);
    RECT tr = rc;
    tr.left += 12;
    DrawTextW(hdc, label ? label : L"", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static const wchar_t* GetToolbarKnobLabel(int idx)
{
    switch (idx)
    {
    case 0: return L"LOW";
    case 1: return L"MID";
    case 2: return L"HIGH";
    case 3: return L"VOL";
    default: return L"EQ";
    }
}

static COLORREF GetToolbarKnobAccent(int idx)
{
    switch (idx)
    {
    case 0: return RGB(70, 120, 230);
    case 1: return RGB(230, 170, 55);
    case 2: return RGB(255, 70, 140);
    case 3: return RGB(80, 210, 220);
    default: return RGB(150, 150, 150);
    }
}

static double GetToolbarKnobDb(const ThreadParam* tp, int idx)
{
    if (!tp) return 0.0;
    switch (idx)
    {
    case 0: return tp->eqLowDb;
    case 1: return tp->eqMidDb;
    case 2: return tp->eqHighDb;
    case 3: return tp->masterGainDb;
    default: return 0.0;
    }
}

static bool SetToolbarKnobDb(ThreadParam* tp, int idx, double db)
{
    if (!tp) return false;
    db = std::clamp(db, -18.0, 18.0);
    double* dst = nullptr;
    switch (idx)
    {
    case 0: dst = &tp->eqLowDb; break;
    case 1: dst = &tp->eqMidDb; break;
    case 2: dst = &tp->eqHighDb; break;
    case 3: dst = &tp->masterGainDb; break;
    default: return false;
    }
    if (std::fabs(*dst - db) < 1e-6) return false;
    *dst = db;
    return true;
}

static bool HasAnyPlaybackEqBands(const ThreadParam* tp)
{
    if (!tp) return false;
    return std::fabs(tp->eqLowDb) > 1e-6 || std::fabs(tp->eqMidDb) > 1e-6 || std::fabs(tp->eqHighDb) > 1e-6;
}

static bool HasMasterPlaybackGainAdjustment(const ThreadParam* tp)
{
    if (!tp) return false;
    return std::fabs(tp->masterGainDb) > 1e-6;
}

// Legacy name kept to minimize churn in call sites: this now means any playback DSP/gain processing.
static bool HasAnyPlaybackEq(const ThreadParam* tp)
{
    return HasAnyPlaybackEqBands(tp) || HasMasterPlaybackGainAdjustment(tp);
}

static void DrawToolbarKnobVisual(HDC hdc, const RECT& rc, const wchar_t* label, COLORREF accent, double dbValue, bool pressed)
{
    const int w = (std::max)(1, static_cast<int>(rc.right - rc.left));
    const int h = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
    const int knobDia = (std::min)(26, (std::max)(16, h - 18));
    const int cx = rc.left + w / 2;
    const int cy = rc.top + knobDia / 2 + 2;
    const int r = knobDia / 2;

    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HBRUSH knobBr = CreateSolidBrush(pressed ? RGB(58, 58, 58) : RGB(46, 46, 46));
    HGDIOBJ oldBrush = SelectObject(hdc, knobBr);
    HPEN ringPen = CreatePen(PS_SOLID, 1, RGB(88, 88, 88));
    HGDIOBJ oldPen = SelectObject(hdc, ringPen);
    Ellipse(hdc, cx - r, cy - r, cx + r + 1, cy + r + 1);

    const double norm = std::clamp((dbValue + 18.0) / 36.0, 0.0, 1.0);
    const double angle = (-135.0 + norm * 270.0) * (3.14159265358979323846 / 180.0);
    const int ix = cx + static_cast<int>(std::lround(std::cos(angle) * (r - 4)));
    const int iy = cy + static_cast<int>(std::lround(std::sin(angle) * (r - 4)));
    HPEN indPen = CreatePen(PS_SOLID, 2, accent);
    SelectObject(hdc, indPen);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, ix, iy);
    DeleteObject(indPen);

    if (std::fabs(dbValue) > 0.05)
    {
        HPEN arcPen = CreatePen(PS_SOLID, 1, accent);
        SelectObject(hdc, arcPen);
        Arc(hdc, cx - r - 2, cy - r - 2, cx + r + 3, cy + r + 3,
            cx + r, cy, cx - r, cy);
        DeleteObject(arcPen);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(215, 215, 215));
    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));

    RECT lr{ rc.left, cy + r + 1, rc.right, rc.bottom - 1 };
    DrawTextW(hdc, label, -1, &lr, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    wchar_t vbuf[16];
    swprintf_s(vbuf, L"%+.0f", dbValue);
    RECT vr{ rc.left, rc.top, rc.right, rc.top + 12 };
    SetTextColor(hdc, std::fabs(dbValue) > 0.05 ? accent : RGB(150, 150, 150));
    DrawTextW(hdc, vbuf, -1, &vr, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(ringPen);
    DeleteObject(knobBr);
}

static void LayoutTopButtons(HWND hwnd, const ThreadParam* tp)
{
    if (!hwnd) return;

    constexpr int kBtnH = 24;
    constexpr int kPlayW = 60;
    constexpr int kPauseW = 60;
    constexpr int kStemW = 68;
    constexpr int kHalfW = 58;
    constexpr int kKnobW = 44;
    constexpr int kKnobH = 40;
    constexpr int kGapSmall = 8;
    constexpr int kGapGroup = 14;

    const bool showStems = HasStemPlayback(tp);
    const int stemCount = showStems ? 4 : 0;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int clientW = (std::max)(0, static_cast<int>(rc.right - rc.left));
    const int y = (std::max)(0, (kControlStripHeightPx - kBtnH) / 2);

    int totalW = kPlayW + kGapSmall + kPauseW;
    if (stemCount > 0)
        totalW += kGapGroup + stemCount * kStemW + (stemCount - 1) * kGapSmall;
    totalW += kGapGroup + kToolbarKnobCount * kKnobW + (kToolbarKnobCount - 1) * kGapSmall;
    // Leave room on the far right for the playback-rate toggle button.
    const int rightReserved = kHalfW + 10;
    const int centerAreaW = (std::max)(0, clientW - rightReserved);
    const int x0 = (std::max)(0, (centerAreaW - totalW) / 2);

    if (tp)
    {
        auto mutTp = const_cast<ThreadParam*>(tp);
        for (int i = 0; i < kToolbarButtonCount; ++i) SetRectEmpty(&mutTp->toolbarButtonRects[i]);
        for (int i = 0; i < kToolbarKnobCount; ++i) SetRectEmpty(&mutTp->toolbarKnobRects[i]);
    }

    int x = x0;
    if (tp)
    {
        auto mutTp = const_cast<ThreadParam*>(tp);
        mutTp->toolbarButtonRects[0] = RECT{ (LONG)x, (LONG)y, (LONG)(x + kPlayW), (LONG)(y + kBtnH) };
    }
    if (HWND hPlay = GetDlgItem(hwnd, 1001))
    {
        MoveWindow(hPlay, x, y, kPlayW, kBtnH, FALSE);
        ShowWindow(hPlay, SW_SHOW);
        x += kPlayW + kGapSmall;
    }
    else
    {
        x += kPlayW + kGapSmall;
    }
    if (tp)
    {
        auto mutTp = const_cast<ThreadParam*>(tp);
        mutTp->toolbarButtonRects[1] = RECT{ (LONG)x, (LONG)y, (LONG)(x + kPauseW), (LONG)(y + kBtnH) };
    }
    if (HWND hPause = GetDlgItem(hwnd, 1002))
    {
        MoveWindow(hPause, x, y, kPauseW, kBtnH, FALSE);
        ShowWindow(hPause, SW_SHOW);
        x += kPauseW;
    }
    else
    {
        x += kPauseW;
    }
    if (stemCount > 0) x += kGapGroup;

    for (int i = 0; i < 4; ++i)
    {
        const int tbIdx = 2 + i;
        if (tp && showStems)
        {
            auto mutTp = const_cast<ThreadParam*>(tp);
            mutTp->toolbarButtonRects[tbIdx] = RECT{ (LONG)x, (LONG)y, (LONG)(x + kStemW), (LONG)(y + kBtnH) };
        }
        HWND hb = GetDlgItem(hwnd, kStemButtonBaseId + i);
        if (hb && showStems)
        {
            MoveWindow(hb, x, y, kStemW, kBtnH, FALSE);
            ShowWindow(hb, SW_SHOW);
            EnableWindow(hb, TRUE);
        }
        else if (hb)
        {
            ShowWindow(hb, SW_HIDE);
            EnableWindow(hb, FALSE);
        }
        if (showStems) x += kStemW + kGapSmall;
    }

    x += kGapGroup;
    const int knobY = (std::max)(0, (kControlStripHeightPx - kKnobH) / 2);
    for (int i = 0; i < kToolbarKnobCount; ++i)
    {
        if (tp)
        {
            auto mutTp = const_cast<ThreadParam*>(tp);
            mutTp->toolbarKnobRects[i] = RECT{ (LONG)x, (LONG)knobY, (LONG)(x + kKnobW), (LONG)(knobY + kKnobH) };
        }
        x += kKnobW + kGapSmall;
    }

    if (tp)
    {
        auto mutTp = const_cast<ThreadParam*>(tp);
        const int sx = (std::max)(4, clientW - kHalfW - 10);
        mutTp->toolbarButtonRects[6] = RECT{ (LONG)sx, (LONG)y, (LONG)(sx + kHalfW), (LONG)(y + kBtnH) };
    }
}

static int HitTestToolbarButton(const ThreadParam* tp, POINT pt)
{
    if (!tp) return -1;
    for (int i = 0; i < kToolbarButtonCount; ++i)
    {
        if (!IsToolbarButtonVisible(tp, i)) continue;
        const RECT& r = tp->toolbarButtonRects[i];
        if (r.right <= r.left || r.bottom <= r.top) continue;
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

static int HitTestToolbarKnob(const ThreadParam* tp, POINT pt)
{
    if (!tp) return -1;
    for (int i = 0; i < kToolbarKnobCount; ++i)
    {
        const RECT& r = tp->toolbarKnobRects[i];
        if (r.right <= r.left || r.bottom <= r.top) continue;
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

static void InvalidateToolbar(HWND hwnd)
{
    if (!hwnd) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    rc.bottom = (LONG)(std::min)(static_cast<int>(rc.bottom), static_cast<int>(rc.top) + kControlStripHeightPx);
    InvalidateRect(hwnd, &rc, FALSE);
}

static void InvalidateToolbarButton(HWND hwnd, const ThreadParam* tp, int idx)
{
    if (!hwnd || !tp || idx < 0 || idx >= kToolbarButtonCount) return;
    const RECT& r = tp->toolbarButtonRects[idx];
    if (r.right > r.left && r.bottom > r.top)
        InvalidateRect(hwnd, &r, FALSE);
    else
        InvalidateToolbar(hwnd);
}

static void InvalidateToolbarKnob(HWND hwnd, const ThreadParam* tp, int idx)
{
    if (!hwnd || !tp || idx < 0 || idx >= kToolbarKnobCount) return;
    const RECT& r = tp->toolbarKnobRects[idx];
    if (r.right > r.left && r.bottom > r.top)
        InvalidateRect(hwnd, &r, FALSE);
    else
        InvalidateToolbar(hwnd);
}

static bool WriteWav16ToPath(const std::wstring& outPath, const std::vector<short>& samples, int sampleRate, bool stereo)
{
    if (outPath.empty()) return false;

    const uint16_t numChannels = stereo ? 2u : 1u;
    const size_t frameCount = stereo ? (samples.size() / 2) : samples.size();
    const uint32_t datasz = static_cast<uint32_t>(frameCount * numChannels * sizeof(short));
    const uint32_t fileSize = 36 + datasz;

    FILE* f = nullptr;
    _wfopen_s(&f, outPath.c_str(), L"wb");
    if (!f) return false;

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, sizeof(fileSize), 1, f);
    fwrite("WAVE", 1, 4, f);

    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1;
    uint32_t byteRate = sampleRate * numChannels * sizeof(short);
    uint16_t blockAlign = numChannels * sizeof(short);
    uint16_t bitsPerSample = 16;
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, sizeof(fmtSize), 1, f);
    fwrite(&audioFormat, sizeof(audioFormat), 1, f);
    fwrite(&numChannels, sizeof(numChannels), 1, f);
    fwrite(&sampleRate, sizeof(sampleRate), 1, f);
    fwrite(&byteRate, sizeof(byteRate), 1, f);
    fwrite(&blockAlign, sizeof(blockAlign), 1, f);
    fwrite(&bitsPerSample, sizeof(bitsPerSample), 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&datasz, sizeof(datasz), 1, f);
    if (!samples.empty())
        fwrite(samples.data(), sizeof(short), frameCount * numChannels, f);
    fclose(f);
    return true;
}

static std::wstring BuildOrRewriteTempWav16(const std::vector<short>& samples, int sampleRate, bool stereo, const std::wstring& preferredPath)
{
    if (!preferredPath.empty())
    {
        if (WriteWav16ToPath(preferredPath, samples, sampleRate, stereo))
            return preferredPath;
    }

    // fallback: create a temp file path
    wchar_t tmpPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpPath)) tmpPath[0] = L'\0';
    wchar_t tmpFile[MAX_PATH];
    if (!GetTempFileNameW(tmpPath, L"wfm", 0, tmpFile))
        return L"";
    std::wstring outPath(tmpFile);
    if (outPath.size() < 4 || outPath.substr(outPath.size() - 4) != L".wav")
        outPath += L".wav";
    if (!WriteWav16ToPath(outPath, samples, sampleRate, stereo))
        return L"";
    return outPath;
}

static void BuildCurrentStemMix(ThreadParam* tp)
{
    if (!tp) return;
    tp->stemMixScratch.clear();

    if (!HasStemPlayback(tp))
        return;

    const int outRate = (tp->sampleRate > 0) ? tp->sampleRate : 44100;
    const int outChannels = tp->isStereo ? 2 : 1;
    if (outChannels <= 0) return;

    size_t outFrames = 0;
    if (tp->samples && !tp->samples->empty())
    {
        outFrames = tp->samples->size() / static_cast<size_t>(outChannels); // align playback timeline to main waveform
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            auto* sv = GetStemVectorByIndex(tp, i);
            if (!sv || sv->empty()) continue;
            const int srcChannels = (std::max)(1, (std::min)(2, GetStemChannelsByIndex(tp, i)));
            const int srcRate = (GetStemSampleRateByIndex(tp, i) > 0) ? GetStemSampleRateByIndex(tp, i) : outRate;
            const size_t srcFrames = sv->size() / static_cast<size_t>(srcChannels);
            const size_t scaledFrames = static_cast<size_t>((static_cast<unsigned long long>(srcFrames) * static_cast<unsigned long long>(outRate)) / static_cast<unsigned long long>((std::max)(1, srcRate)));
            outFrames = (std::max)(outFrames, scaledFrames);
        }
    }
    if (outFrames == 0) return;

    const size_t outSamples = outFrames * static_cast<size_t>(outChannels);
    tp->stemMixScratch.assign(outSamples, 0);
    std::vector<int> accum(outSamples, 0);
    bool anyEnabled = false;

    for (int stemIdx = 0; stemIdx < 4; ++stemIdx)
    {
        if (!tp->stemEnabled[stemIdx]) continue;
        auto* sv = GetStemVectorByIndex(tp, stemIdx);
        if (!sv || sv->empty()) continue;

        const int srcRateRaw = GetStemSampleRateByIndex(tp, stemIdx);
        const int srcRate = (srcRateRaw > 0) ? srcRateRaw : outRate;
        const int srcChannels = (std::max)(1, (std::min)(2, GetStemChannelsByIndex(tp, stemIdx)));
        const size_t srcFrames = sv->size() / static_cast<size_t>(srcChannels);
        if (srcFrames == 0) continue;

        anyEnabled = true;

        for (size_t outFrame = 0; outFrame < outFrames; ++outFrame)
        {
            // Nearest-neighbor resample to the main playback rate (fast enough for toggle rebuilds).
            size_t srcFrame = static_cast<size_t>(
                (static_cast<unsigned long long>(outFrame) * static_cast<unsigned long long>(srcRate)) /
                static_cast<unsigned long long>((std::max)(1, outRate)));
            if (srcFrame >= srcFrames) break;

            const size_t srcBase = srcFrame * static_cast<size_t>(srcChannels);
            const short sL = (*sv)[srcBase];
            const short sR = (srcChannels >= 2 && (srcBase + 1) < sv->size()) ? (*sv)[srcBase + 1] : sL;

            const size_t outBase = outFrame * static_cast<size_t>(outChannels);
            if (outChannels == 1)
            {
                const int mono = (static_cast<int>(sL) + static_cast<int>(sR)) / 2;
                accum[outBase] += mono;
            }
            else
            {
                accum[outBase] += static_cast<int>(sL);
                accum[outBase + 1] += static_cast<int>(sR);
            }
        }
    }

    if (!anyEnabled)
        return; // keep silence

    for (size_t i = 0; i < outSamples; ++i)
    {
        int v = accum[i];
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        tp->stemMixScratch[i] = static_cast<short>(v);
    }
}

static void ApplyPlaybackEqInPlace(ThreadParam* tp, std::vector<short>& interleaved)
{
    if (!tp || interleaved.empty() || tp->sampleRate <= 0 || !HasAnyPlaybackEq(tp))
        return;

    const bool eqActive = HasAnyPlaybackEqBands(tp);
    const bool gainActive = HasMasterPlaybackGainAdjustment(tp);
    const double masterGain = gainActive ? std::pow(10.0, tp->masterGainDb / 20.0) : 1.0;

    if (!eqActive)
    {
        for (short& s : interleaved)
        {
            double y = (static_cast<double>(s) / 32768.0) * masterGain;
            if (!std::isfinite(y)) y = 0.0;
            y = std::clamp(y, -1.0, 1.0);
            s = static_cast<short>(std::lround(y * 32767.0));
        }
        return;
    }

    const int channels = tp->isStereo ? 2 : 1;
    if (channels <= 0) return;
    const size_t frames = interleaved.size() / static_cast<size_t>(channels);
    if (frames == 0) return;

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;

        double process(double x, double& z1, double& z2) const
        {
            // Transposed direct-form II
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    auto normalize_biquad = [](double b0, double b1, double b2, double a0, double a1, double a2) -> Biquad
    {
        const double invA0 = (std::fabs(a0) > 1e-18) ? (1.0 / a0) : 1.0;
        Biquad q{};
        q.b0 = b0 * invA0;
        q.b1 = b1 * invA0;
        q.b2 = b2 * invA0;
        q.a1 = a1 * invA0;
        q.a2 = a2 * invA0;
        return q;
    };

    auto make_peaking = [&](double fc, double q, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(tp->sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        q = (std::max)(0.1, q);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 / (2.0 * q);

        const double b0 = 1.0 + alpha * A;
        const double b1 = -2.0 * cw0;
        const double b2 = 1.0 - alpha * A;
        const double a0 = 1.0 + alpha / A;
        const double a1 = -2.0 * cw0;
        const double a2 = 1.0 - alpha / A;
        return normalize_biquad(b0, b1, b2, a0, a1, a2);
    };

    auto make_lowshelf = [&](double fc, double slope, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(tp->sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        slope = (std::max)(0.1, slope);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

        const double b0 = A * ((A + 1.0) - (A - 1.0) * cw0 + twoSqrtAAlpha);
        const double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw0);
        const double b2 = A * ((A + 1.0) - (A - 1.0) * cw0 - twoSqrtAAlpha);
        const double a0 = (A + 1.0) + (A - 1.0) * cw0 + twoSqrtAAlpha;
        const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw0);
        const double a2 = (A + 1.0) + (A - 1.0) * cw0 - twoSqrtAAlpha;
        return normalize_biquad(b0, b1, b2, a0, a1, a2);
    };

    auto make_highshelf = [&](double fc, double slope, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(tp->sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        slope = (std::max)(0.1, slope);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;

        const double b0 = A * ((A + 1.0) + (A - 1.0) * cw0 + twoSqrtAAlpha);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw0);
        const double b2 = A * ((A + 1.0) + (A - 1.0) * cw0 - twoSqrtAAlpha);
        const double a0 = (A + 1.0) - (A - 1.0) * cw0 + twoSqrtAAlpha;
        const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw0);
        const double a2 = (A + 1.0) - (A - 1.0) * cw0 - twoSqrtAAlpha;
        return normalize_biquad(b0, b1, b2, a0, a1, a2);
    };

    // DJ-style-ish 3-band EQ:
    // - low shelf
    // - broad mid bell
    // - high shelf
    const Biquad lowShelf = make_lowshelf(220.0, 0.9, tp->eqLowDb);
    const Biquad midBell = make_peaking(1000.0, 0.75, tp->eqMidDb);
    const Biquad highShelf = make_highshelf(4200.0, 0.9, tp->eqHighDb);

    std::vector<float> processed(interleaved.size(), 0.0f);
    double peak = 0.0;

    for (int ch = 0; ch < channels; ++ch)
    {
        double lz1 = 0.0, lz2 = 0.0;
        double mz1 = 0.0, mz2 = 0.0;
        double hz1 = 0.0, hz2 = 0.0;

        for (size_t i = 0; i < frames; ++i)
        {
            const size_t idx = i * static_cast<size_t>(channels) + static_cast<size_t>(ch);
            double x = static_cast<double>(interleaved[idx]) / 32768.0;
            x = lowShelf.process(x, lz1, lz2);
            x = midBell.process(x, mz1, mz2);
            x = highShelf.process(x, hz1, hz2);

            if (!std::isfinite(x)) x = 0.0;
            processed[idx] = static_cast<float>(x);
            peak = (std::max)(peak, std::fabs(x));
        }
    }

    // Peak protection with a little headroom; much cleaner than hard clipping.
    const double targetPeak = 0.985;
    const double scale = (peak > targetPeak && peak > 1e-12) ? (targetPeak / peak) : 1.0;

    for (size_t i = 0; i < interleaved.size(); ++i)
    {
        double y = static_cast<double>(processed[i]) * scale * masterGain;
        if (y > 1.0) y = 1.0;
        if (y < -1.0) y = -1.0;
        interleaved[i] = static_cast<short>(std::lround(y * 32767.0));
    }
}

static void BuildCurrentPlaybackBuffer(ThreadParam* tp)
{
    if (!tp) return;
    tp->stemMixScratch.clear();

    const bool useSourceDirect = ShouldUseSourcePlaybackDirect(tp);

    if (useSourceDirect)
    {
        // Only materialize a scratch buffer when playback DSP/gain processing is active.
        if (HasAnyPlaybackEq(tp) && tp->samples)
        {
            tp->stemMixScratch = *tp->samples;
            ApplyPlaybackEqInPlace(tp, tp->stemMixScratch);
        }
        return;
    }

    if (HasStemPlayback(tp))
    {
        BuildCurrentStemMix(tp);
    }
    else if (tp->samples)
    {
        tp->stemMixScratch = *tp->samples;
    }

    if (!tp->stemMixScratch.empty())
        ApplyPlaybackEqInPlace(tp, tp->stemMixScratch);
}

static void RebuildPlaybackAndRetuneMci(ThreadParam* tp)
{
    if (!tp || tp->sampleRate <= 0)
        return;

    // Miniaudio path: update live callback routing/EQ in-place for seamless changes.
    if (UsingAudioEngine(tp))
    {
        PushAudioEngineLiveMixConfig(tp);
        return;
    }

    const bool resume = tp->playing.load();
    size_t resumeFrame = tp->pausedSampleIndex;
    if (resume)
    {
        const size_t totalFrames = GetTotalFrames(tp);
        const double curFrame = std::clamp(GetCurrentFrameForView(tp), 0.0, totalFrames > 0 ? static_cast<double>(totalFrames - 1) : 0.0);
        resumeFrame = static_cast<size_t>(std::llround(curFrame));
        tp->pausedSampleIndex = resumeFrame;
    }
    else if (tp->mciOpened)
    {
        SyncPausedFromMciPosition(tp);
        resumeFrame = tp->pausedSampleIndex;
    }

    BuildCurrentPlaybackBuffer(tp);

    if (tp->mciOpened)
        MciClose(tp);
    tp->playing.store(false);

    const std::vector<short>* mixSrc = nullptr;
    if (!tp->stemMixScratch.empty()) mixSrc = &tp->stemMixScratch;
    else if (tp->samples) mixSrc = tp->samples;
    if (!mixSrc)
        return;

    tp->tempPath = BuildOrRewriteTempWav16(*mixSrc, tp->sampleRate, tp->isStereo, tp->tempPath);
    if (tp->tempPath.empty())
        return;

    // Keep the same alias; MciOpenIfNeeded will reopen it.
    if (resume || resumeFrame > 0)
        SeekToFrame(tp, resumeFrame, resume);
}

static void RebuildStemPlaybackAndRetuneMci(ThreadParam* tp)
{
    RebuildPlaybackAndRetuneMci(tp);
}

static short ClampShort16(double v)
{
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return static_cast<short>(std::lround(v));
}

static RECT ComputeWaveRect(const RECT& clientRc, const ThreadParam* tp)
{
    RECT waveRc = clientRc;
    const int fullH = static_cast<int>(clientRc.bottom - clientRc.top);
    const int usableTop = static_cast<int>(clientRc.top) + kControlStripHeightPx;
    const int usableH = (std::max)(0, static_cast<int>(clientRc.bottom) - usableTop);
    int div = 10;
    if (tp && tp->regionDiv > 0) div = tp->regionDiv;
    int waveH = (div > 0) ? (usableH / div) : (usableH / 10);
    waveH = (std::max)(waveH, 40);
    waveH = (std::min)(waveH, usableH);
    waveRc.top = (LONG)usableTop;
    waveRc.bottom = (LONG)(usableTop + waveH);
    return waveRc;
}

static RECT ComputeWaveInvalidateRect(HWND hwnd, const ThreadParam* tp)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT waveRc = ComputeWaveRect(rc, tp);
    RECT inv = rc;
    inv.top = waveRc.top;
    inv.bottom = rc.bottom;
    return inv;
}

static const wchar_t* GetPianoRollTabName(int idx)
{
    switch (idx)
    {
    case 0: return L"Main";
    case 1: return L"Vocals";
    case 2: return L"Drums";
    case 3: return L"Bass";
    case 4: return L"Chords";
    case 5: return L"Spec";
    default: return L"Main";
    }
}

static void ComputePianoRollLayout(const RECT& clientRc, const ThreadParam* tp, RECT* outTabsRc, RECT* outPianoRc)
{
    if (outTabsRc) SetRectEmpty(outTabsRc);
    if (outPianoRc) SetRectEmpty(outPianoRc);

    RECT waveRc = ComputeWaveRect(clientRc, tp);
    const int textY1 = static_cast<int>(waveRc.bottom) + 4;
    const int textY2 = textY1 + 16;
    const int tabsTop = textY2 + 20;
    const int tabsBottom = (std::min)(static_cast<int>(clientRc.bottom), tabsTop + kPianoTabStripHeightPx);

    RECT tabsRc{ clientRc.left, (LONG)tabsTop, clientRc.right, (LONG)tabsBottom };
    RECT pianoRc{ clientRc.left, (LONG)(tabsBottom + kPianoTabGapPx), clientRc.right, clientRc.bottom };
    if (pianoRc.bottom < pianoRc.top) pianoRc.bottom = pianoRc.top;

    if (outTabsRc) *outTabsRc = tabsRc;
    if (outPianoRc) *outPianoRc = pianoRc;
}

static int HitTestPianoRollTab(const RECT& tabsRc, int x, int y)
{
    if (tabsRc.right <= tabsRc.left || tabsRc.bottom <= tabsRc.top)
        return -1;
    POINT pt{ x, y };
    if (!PtInRect(&tabsRc, pt))
        return -1;

    const int totalW = (std::max)(1, static_cast<int>(tabsRc.right - tabsRc.left));
    for (int i = 0; i < kPianoTabCount; ++i)
    {
        RECT tr{
            tabsRc.left + (int)((long long)totalW * i / kPianoTabCount),
            tabsRc.top,
            tabsRc.left + (int)((long long)totalW * (i + 1) / kPianoTabCount),
            tabsRc.bottom
        };
        if (PtInRect(&tr, pt))
            return i;
    }
    return -1;
}

static void DrawPianoRollTabs(HDC hdc, const RECT& tabsRc, int activeTab)
{
    if (!hdc || tabsRc.right <= tabsRc.left || tabsRc.bottom <= tabsRc.top)
        return;

    HBRUSH stripBg = CreateSolidBrush(RGB(22, 22, 22));
    FillRect(hdc, &tabsRc, stripBg);
    DeleteObject(stripBg);

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);

    const int totalW = (std::max)(1, static_cast<int>(tabsRc.right - tabsRc.left));

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
    HPEN innerPen = CreatePen(PS_SOLID, 1, RGB(48, 48, 48));
    HPEN activeTopPen = CreatePen(PS_SOLID, 2, RGB(255, 170, 0));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    for (int i = 0; i < kPianoTabCount; ++i)
    {
        RECT tr{
            tabsRc.left + (int)((long long)totalW * i / kPianoTabCount),
            tabsRc.top,
            tabsRc.left + (int)((long long)totalW * (i + 1) / kPianoTabCount),
            tabsRc.bottom
        };

        const bool isActive = (i == activeTab);
        HBRUSH tabBg = CreateSolidBrush(isActive ? RGB(34, 34, 34) : RGB(18, 18, 18));
        FillRect(hdc, &tr, tabBg);
        DeleteObject(tabBg);

        SelectObject(hdc, borderPen);
        Rectangle(hdc, tr.left, tr.top, tr.right, tr.bottom);

        if (isActive)
        {
            SelectObject(hdc, activeTopPen);
            MoveToEx(hdc, tr.left + 1, tr.top + 1, NULL);
            LineTo(hdc, tr.right - 1, tr.top + 1);
        }
        else
        {
            SelectObject(hdc, innerPen);
            MoveToEx(hdc, tr.left, tr.top, NULL);
            LineTo(hdc, tr.right, tr.top);
        }

        const wchar_t* label = GetPianoRollTabName(i);
        SetTextColor(hdc, isActive ? RGB(235, 235, 235) : RGB(170, 170, 170));
        RECT textRc = tr;
        DrawTextW(hdc, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldFont);
    DeleteObject(borderPen);
    DeleteObject(innerPen);
    DeleteObject(activeTopPen);
}

static void InvalidateWaveRegion(HWND hwnd, const ThreadParam* tp)
{
    if (!hwnd) return;
    PublishPlaybackSync(tp);
    RECT inv = ComputeWaveInvalidateRect(hwnd, tp);
    InvalidateRect(hwnd, &inv, FALSE);
}

static std::wstring FormatTimeLabel(double seconds, bool withMillis = false)
{
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    int total = static_cast<int>(std::floor(seconds));
    int mins = total / 60;
    int secs = total % 60;
    if (!withMillis)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%d:%02d", mins, secs);
        return std::wstring(buf);
    }
    int ms = static_cast<int>(std::floor((seconds - std::floor(seconds)) * 1000.0));
    if (ms < 0) ms = 0;
    if (ms > 999) ms = 999;
    wchar_t buf[32];
    swprintf_s(buf, L"%d:%02d.%03d", mins, secs, ms);
    return std::wstring(buf);
}

static double ChooseTimeTickStep(double visibleSeconds)
{
    static const double steps[] = {
        0.1, 0.2, 0.5,
        1.0, 2.0, 5.0,
        10.0, 15.0, 30.0,
        60.0, 120.0, 300.0
    };
    for (double s : steps)
    {
        if (visibleSeconds / s <= 12.0) return s;
    }
    return 600.0;
}

static size_t GetTotalFrames(const ThreadParam* tp)
{
    if (!tp || !tp->samples) return 0;
    return tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
}

static double GetMinZoomFactorForMaxWindow(const ThreadParam* tp, size_t totalFrames)
{
    if (!tp || tp->sampleRate <= 0 || totalFrames == 0) return 1.0;
    const double maxVisibleFrames = (std::max)(1.0, kMaxVisibleWindowSeconds * static_cast<double>(tp->sampleRate));
    return (std::max)(1.0, static_cast<double>(totalFrames) / maxVisibleFrames);
}

static double GetEffectiveZoomFactor(const ThreadParam* tp, size_t totalFrames)
{
    if (!tp) return 1.0;
    return (std::max)(tp->zoomFactor, GetMinZoomFactorForMaxWindow(tp, totalFrames));
}

static double GetCurrentFrameForView(const ThreadParam* tp)
{
    if (!tp) return 0.0;
    if (UsingAudioEngine(tp))
        return static_cast<double>(tp->audioEngine.GetCurrentFrame());
    if (tp->playing.load())
    {
        auto now = std::chrono::steady_clock::now();
        const double elapsedSec = std::chrono::duration<double>(now - tp->playStartTime).count();
        return elapsedSec * tp->sampleRate;
    }
    return static_cast<double>(tp->pausedSampleIndex);
}

static bool TryMapWavePointToFrame(HWND hwnd, ThreadParam* tp, int x, int y, size_t& outFrame)
{
    if (!hwnd || !tp) return false;
    const size_t totalFrames = GetTotalFrames(tp);
    if (totalFrames == 0) return false;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT waveRc = ComputeWaveRect(rc, tp);
    POINT pt{ x, y };
    if (!PtInRect(&waveRc, pt)) return false;

    const int w = (std::max)(1, static_cast<int>(waveRc.right - waveRc.left));
    double curFrameD = GetCurrentFrameForView(tp);
    curFrameD = std::clamp(curFrameD, 0.0, static_cast<double>(((std::max))(size_t{ 1 }, totalFrames) - 1));

    double centerFrame = curFrameD + static_cast<double>(tp->panOffsetSamples);
    centerFrame = std::clamp(centerFrame, 0.0, static_cast<double>(((std::max))(size_t{ 1 }, totalFrames) - 1));
    const double zoom = GetEffectiveZoomFactor(tp, totalFrames);
    double visibleFrames = (std::max)(1.0, static_cast<double>(totalFrames) / zoom);
    double startFrame = centerFrame - tp->playheadXRatio * visibleFrames;
    if (startFrame < 0.0) startFrame = 0.0;
    if (startFrame + visibleFrames > static_cast<double>(totalFrames))
        startFrame = static_cast<double>(totalFrames) - visibleFrames;
    if (startFrame < 0.0) startFrame = 0.0;

    const double xNorm = std::clamp((static_cast<double>(x - waveRc.left) / static_cast<double>(w)), 0.0, 1.0);
    double frameD = startFrame + xNorm * visibleFrames;
    if (frameD >= static_cast<double>(totalFrames)) frameD = static_cast<double>(totalFrames - 1);
    outFrame = static_cast<size_t>((std::max)(0.0, std::floor(frameD)));
    return true;
}

static void SyncPausedFromMciPosition(ThreadParam* tp)
{
    if (!tp) return;

    if (UsingAudioEngine(tp))
    {
        tp->pausedSampleIndex = tp->audioEngine.GetCurrentFrame();
        return;
    }

    const size_t totalFrames = GetTotalFrames(tp);
    const double estFrameD = std::clamp(GetCurrentFrameForView(tp), 0.0, totalFrames > 0 ? static_cast<double>(totalFrames - 1) : 0.0);
    const size_t estFrame = static_cast<size_t>(std::llround(estFrameD));

    bool gotMci = false;
    size_t mciFrame = estFrame;

    if (tp->mciOpened && tp->sampleRate > 0)
    {
        wchar_t buf[64] = { 0 };
        std::wstring cmd = L"status " + tp->mciAlias + L" position";
        const MCIERROR err = mciSendStringW(cmd.c_str(), buf, (UINT)std::size(buf), NULL);
        if (err == 0 && buf[0] != 0)
        {
            const unsigned long posMs = static_cast<unsigned long>(wcstoul(buf, nullptr, 10));
            mciFrame = static_cast<size_t>((static_cast<unsigned long long>(posMs) * static_cast<unsigned long long>(tp->sampleRate)) / 1000ULL);
            if (totalFrames > 0)
                mciFrame = (std::min)(mciFrame, totalFrames - 1);
            gotMci = true;
        }
    }

    if (!gotMci)
    {
        tp->pausedSampleIndex = estFrame;
        return;
    }

    // MCI position queries occasionally return 0 or a large jump during device state churn.
    // When actively playing, trust the local high-resolution clock if the values diverge heavily.
    if (tp->playing.load())
    {
        const long long mciLL = static_cast<long long>(mciFrame);
        const long long estLL = static_cast<long long>(estFrame);
        const long long diff = (mciLL >= estLL) ? (mciLL - estLL) : (estLL - mciLL);
        const long long suspiciousJumpFrames = static_cast<long long>((std::max)(tp->sampleRate, 1) * 2); // >2s jump
        const bool suspiciousZero = (mciFrame == 0 && estFrame > static_cast<size_t>((std::max)(tp->sampleRate, 1) / 2));
        if (suspiciousZero || diff > suspiciousJumpFrames)
        {
            tp->pausedSampleIndex = estFrame;
            return;
        }
    }

    tp->pausedSampleIndex = mciFrame;
}

static void SeekToFrame(ThreadParam* tp, size_t frame, bool resumePlayback)
{
    if (!tp) return;
    const size_t totalFrames = GetTotalFrames(tp);
    if (totalFrames == 0 || tp->sampleRate <= 0) return;

    frame = (std::min)(frame, totalFrames - 1);
    tp->pausedSampleIndex = frame;

    if (UsingAudioEngine(tp))
    {
        tp->audioEngine.SeekFrame(frame);
        if (resumePlayback)
        {
            if (tp->audioEngine.Play())
            {
                tp->playing.store(true);
                return;
            }
        }
        tp->audioEngine.Pause();
        tp->playing.store(false);
        return;
    }

    if (!tp->mciOpened && !tp->tempPath.empty())
        MciOpenIfNeeded(tp);

    const DWORD posMs = static_cast<DWORD>((frame * 1000ULL) / static_cast<unsigned long long>(tp->sampleRate));

    if (tp->mciOpened)
    {
        std::wstring cmd = L"stop " + tp->mciAlias;
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);

        cmd = L"seek " + tp->mciAlias + L" to " + std::to_wstring(posMs);
        mciSendStringW(cmd.c_str(), NULL, 0, NULL);

        if (resumePlayback)
        {
            cmd = L"play " + tp->mciAlias + L" from " + std::to_wstring(posMs);
            if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) == 0)
            {
                tp->playStartTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(static_cast<long long>(posMs));
                tp->playing.store(true);
                return;
            }
        }
    }

    tp->playing.store(false);
}

// Helper: write WAV (16-bit) to temp. If stereo==true samples are expected interleaved (L,R,L,R...).
static std::wstring WriteTempWav16(const std::vector<short>& samples, int sampleRate, bool stereo)
{
    wchar_t tmpPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpPath)) tmpPath[0] = L'\0';
    wchar_t tmpFile[MAX_PATH];
    if (!GetTempFileNameW(tmpPath, L"wfm", 0, tmpFile)) // generates .tmp
    {
        std::wstring fallback = std::wstring(tmpPath) + L"waveform_playback.wav";
        std::filesystem::path p(fallback);
        tmpFile[0] = 0;
        wcscpy_s(tmpFile, p.wstring().c_str());
    }
    std::wstring outPath(tmpFile);
    if (outPath.size() >= 4 && outPath.substr(outPath.size() - 4) != L".wav")
    {
        outPath += L".wav";
    }

    uint16_t numChannels = stereo ? 2u : 1u;
    size_t frameCount = stereo ? (samples.size() / 2) : samples.size();
    uint32_t datasz = static_cast<uint32_t>(frameCount * numChannels * sizeof(short));
    uint32_t fileSize = 36 + datasz;

    FILE* f = nullptr;
    _wfopen_s(&f, outPath.c_str(), L"wb");
    if (!f) return L"";

    fwrite("RIFF", 1, 4, f);
    fwrite(&fileSize, sizeof(fileSize), 1, f);
    fwrite("WAVE", 1, 4, f);

    uint32_t fmtSize = 16;
    uint16_t audioFormat = 1; // PCM
    uint32_t byteRate = sampleRate * numChannels * sizeof(short);
    uint16_t blockAlign = numChannels * sizeof(short);
    uint16_t bitsPerSample = 16;
    fwrite("fmt ", 1, 4, f);
    fwrite(&fmtSize, sizeof(fmtSize), 1, f);
    fwrite(&audioFormat, sizeof(audioFormat), 1, f);
    fwrite(&numChannels, sizeof(numChannels), 1, f);
    fwrite(&sampleRate, sizeof(sampleRate), 1, f);
    fwrite(&byteRate, sizeof(byteRate), 1, f);
    fwrite(&blockAlign, sizeof(blockAlign), 1, f);
    fwrite(&bitsPerSample, sizeof(bitsPerSample), 1, f);

    fwrite("data", 1, 4, f);
    fwrite(&datasz, sizeof(datasz), 1, f);

    if (!samples.empty())
    {
        // If samples length matches frames*numChannels, write directly.
        fwrite(samples.data(), sizeof(short), (size_t)frameCount * numChannels, f);
    }

    fclose(f);
    return outPath;
}

static void MciOpenIfNeeded(ThreadParam* tp)
{
    if (!tp || tp->mciOpened || tp->tempPath.empty() || tp->mciAlias.empty())
        return;

    std::wstring cmd = L"open \"" + tp->tempPath + L"\" type waveaudio alias " + tp->mciAlias;
    if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) != 0)
        return;

    tp->mciOpened = true;
    cmd = L"set " + tp->mciAlias + L" time format milliseconds";
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
}

static void MciClose(ThreadParam* tp)
{
    if (!tp || !tp->mciOpened || tp->mciAlias.empty()) return;
    std::wstring cmd = L"stop " + tp->mciAlias;
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
    cmd = L"close " + tp->mciAlias;
    mciSendStringW(cmd.c_str(), NULL, 0, NULL);
    tp->mciOpened = false;
}

static inline float ClampFloat(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static std::vector<float> BuildMonoDisplayFrames(const ThreadParam* tp)
{
    std::vector<float> mono;
    if (!tp || !tp->samples || tp->samples->empty())
        return mono;

    const size_t totalFrames = tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
    mono.resize(totalFrames);

    if (tp->isStereo)
    {
        for (size_t i = 0; i < totalFrames; ++i)
        {
            const int l = (*tp->samples)[2 * i];
            const int r = (*tp->samples)[2 * i + 1];
            mono[i] = static_cast<float>((l + r) * (0.5 / 32768.0));
        }
    }
    else
    {
        for (size_t i = 0; i < totalFrames; ++i)
            mono[i] = static_cast<float>((*tp->samples)[i] / 32768.0);
    }

    float mx = 0.0f;
    for (float v : mono) mx = (std::max)(mx, static_cast<float>(std::fabs(v)));
    if (mx > 1e-12f)
    {
        for (float& v : mono) v /= mx;
    }

    for (float& v : mono)
        v = ClampFloat(v, -tp->displayHeadroom, tp->displayHeadroom);

    return mono;
}

static std::vector<float> OnePoleLowpass(const std::vector<float>& x, int sr, double fcHz)
{
    std::vector<float> y(x.size(), 0.0f);
    if (x.empty() || sr <= 0 || fcHz <= 0.0)
        return y;

    constexpr double kPi = 3.14159265358979323846;
    const double dt = 1.0 / static_cast<double>(sr);
    const double rc = 1.0 / (2.0 * kPi * fcHz);
    const double a = dt / (rc + dt);
    double z = 0.0;

    for (size_t i = 0; i < x.size(); ++i)
    {
        z = z + a * (static_cast<double>(x[i]) - z);
        y[i] = static_cast<float>(z);
    }
    return y;
}

static std::vector<float> OnePoleHighpass(const std::vector<float>& x, int sr, double fcHz)
{
    std::vector<float> lp = OnePoleLowpass(x, sr, fcHz);
    std::vector<float> y(x.size(), 0.0f);
    for (size_t i = 0; i < x.size(); ++i)
        y[i] = x[i] - lp[i];
    return y;
}

static void EnvelopeMinMax(const std::vector<float>& x, int block, std::vector<float>& outMin, std::vector<float>& outMax)
{
    outMin.clear();
    outMax.clear();
    if (x.empty() || block <= 0)
        return;

    const size_t nb = (x.size() + static_cast<size_t>(block) - 1) / static_cast<size_t>(block);
    outMin.resize(nb, 0.0f);
    outMax.resize(nb, 0.0f);

    for (size_t b = 0; b < nb; ++b)
    {
        const size_t i0 = b * static_cast<size_t>(block);
        const size_t i1 = std::min(i0 + static_cast<size_t>(block), x.size());
        float mn = std::numeric_limits<float>::max();
        float mx = -std::numeric_limits<float>::max();
        for (size_t i = i0; i < i1; ++i)
        {
            mn = std::min(mn, x[i]);
            mx = std::max(mx, x[i]);
        }
        if (i0 >= i1)
        {
            mn = 0.0f;
            mx = 0.0f;
        }
        outMin[b] = mn;
        outMax[b] = mx;
    }
}

static void ReduceEnvelopeMinMax2x(const std::vector<float>& srcMin, const std::vector<float>& srcMax,
    std::vector<float>& dstMin, std::vector<float>& dstMax)
{
    dstMin.clear();
    dstMax.clear();
    const size_t n = (std::min)(srcMin.size(), srcMax.size());
    if (n == 0) return;

    const size_t outN = (n + 1) / 2;
    dstMin.resize(outN, 0.0f);
    dstMax.resize(outN, 0.0f);

    for (size_t i = 0; i < outN; ++i)
    {
        const size_t j0 = i * 2;
        const size_t j1 = (std::min)(j0 + 1, n - 1);
        dstMin[i] = (std::min)(srcMin[j0], srcMin[j1]);
        dstMax[i] = (std::max)(srcMax[j0], srcMax[j1]);
    }
}

static void BuildEnvelopeMipPyramid(ThreadParam* tp)
{
    if (!tp)
        return;

    tp->envMipLevels.clear();
    if (tp->baseMinF.empty() || tp->baseMaxF.empty() || tp->envBlock <= 0)
        return;

    const std::vector<float>* curBaseMin = &tp->baseMinF;
    const std::vector<float>* curBaseMax = &tp->baseMaxF;
    const std::vector<float>* curLowMin = &tp->lowMinF;
    const std::vector<float>* curLowMax = &tp->lowMaxF;
    const std::vector<float>* curMidMin = &tp->midMinF;
    const std::vector<float>* curMidMax = &tp->midMaxF;
    const std::vector<float>* curHighMin = &tp->highMinF;
    const std::vector<float>* curHighMax = &tp->highMaxF;
    int curBlock = tp->envBlock;

    while (curBaseMin && curBaseMin->size() > 1)
    {
        EnvelopeMipLevel lvl{};
        lvl.block = curBlock * 2;

        ReduceEnvelopeMinMax2x(*curBaseMin, *curBaseMax, lvl.baseMinF, lvl.baseMaxF);
        ReduceEnvelopeMinMax2x(*curLowMin,  *curLowMax,  lvl.lowMinF,  lvl.lowMaxF);
        ReduceEnvelopeMinMax2x(*curMidMin,  *curMidMax,  lvl.midMinF,  lvl.midMaxF);
        ReduceEnvelopeMinMax2x(*curHighMin, *curHighMax, lvl.highMinF, lvl.highMaxF);

        lvl.blocks = lvl.baseMinF.size();
        if (lvl.blocks == 0 || lvl.blocks >= curBaseMin->size())
            break;

        tp->envMipLevels.push_back(std::move(lvl));

        const EnvelopeMipLevel& back = tp->envMipLevels.back();
        curBaseMin = &back.baseMinF;
        curBaseMax = &back.baseMaxF;
        curLowMin = &back.lowMinF;
        curLowMax = &back.lowMaxF;
        curMidMin = &back.midMinF;
        curMidMax = &back.midMaxF;
        curHighMin = &back.highMinF;
        curHighMax = &back.highMaxF;
        curBlock = back.block;
    }
}

static EnvelopeLevelView SelectEnvelopeLevelForFramesPerPixel(const ThreadParam* tp, double framesPerPixel)
{
    EnvelopeLevelView v{};
    if (!tp || tp->envBlock <= 0 || tp->baseMinF.empty() || tp->baseMaxF.empty())
        return v;

    v.block = tp->envBlock;
    v.blocks = tp->envBlocks;
    v.baseMinF = &tp->baseMinF;
    v.baseMaxF = &tp->baseMaxF;
    v.lowMinF = &tp->lowMinF;
    v.lowMaxF = &tp->lowMaxF;
    v.midMinF = &tp->midMinF;
    v.midMaxF = &tp->midMaxF;
    v.highMinF = &tp->highMinF;
    v.highMaxF = &tp->highMaxF;

    if (tp->envMipLevels.empty())
        return v;

    const double target = (std::max)(1.0, framesPerPixel);
    auto scoreBlock = [&](double blockFrames) -> double
    {
        blockFrames = (std::max)(1.0, blockFrames);
        return std::fabs(std::log(blockFrames / target));
    };

    double bestScore = scoreBlock(static_cast<double>(v.block));
    for (const EnvelopeMipLevel& lvl : tp->envMipLevels)
    {
        if (lvl.block <= 0 || lvl.blocks == 0) continue;
        const double s = scoreBlock(static_cast<double>(lvl.block));
        if (s >= bestScore) continue;

        v.block = lvl.block;
        v.blocks = lvl.blocks;
        v.baseMinF = &lvl.baseMinF;
        v.baseMaxF = &lvl.baseMaxF;
        v.lowMinF = &lvl.lowMinF;
        v.lowMaxF = &lvl.lowMaxF;
        v.midMinF = &lvl.midMinF;
        v.midMaxF = &lvl.midMaxF;
        v.highMinF = &lvl.highMinF;
        v.highMaxF = &lvl.highMaxF;
        bestScore = s;
    }

    return v;
}

static void Fnv1a64MixBytes(std::uint64_t& h, const void* data, std::size_t n)
{
    const auto* p = static_cast<const unsigned char*>(data);
    constexpr std::uint64_t kPrime = 1099511628211ull;
    for (std::size_t i = 0; i < n; ++i)
    {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kPrime;
    }
}

template <typename T>
static void Fnv1a64MixValue(std::uint64_t& h, const T& v)
{
    Fnv1a64MixBytes(h, &v, sizeof(T));
}

static std::filesystem::path GetEnvelopeCacheDirectoryPath()
{
    wchar_t tmpPath[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, tmpPath))
        return std::filesystem::current_path() / L"waveform_cache";

    std::filesystem::path dir = std::filesystem::path(tmpPath) / L"waveOut" / L"waveform_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static bool EnsureEnvelopeCacheKey(ThreadParam* tp, std::size_t totalFrames)
{
    if (!tp || !tp->samples || tp->samples->empty() || totalFrames == 0)
        return false;
    if (tp->envCacheKeyValid && !tp->envCachePath.empty())
        return true;

    std::uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
    const std::uint32_t sr = static_cast<std::uint32_t>((std::max)(tp->sampleRate, 0));
    const std::uint32_t ch = static_cast<std::uint32_t>(tp->isStereo ? 2 : 1);
    const std::uint32_t envBlock = static_cast<std::uint32_t>((std::max)(tp->envBlock, 0));
    const std::uint64_t frames64 = static_cast<std::uint64_t>(totalFrames);
    const std::uint64_t sampleCount64 = static_cast<std::uint64_t>(tp->samples->size());

    Fnv1a64MixValue(h, sr);
    Fnv1a64MixValue(h, ch);
    Fnv1a64MixValue(h, envBlock);
    Fnv1a64MixValue(h, frames64);
    Fnv1a64MixValue(h, sampleCount64);
    Fnv1a64MixValue(h, tp->displayHeadroom);
    std::uint32_t cacheKeyMode = 0; // 0 = PCM hash fallback, 1 = source path+mtime+size
    bool usedSourceHint = false;
    std::wstring sourcePathForKey;

    if (!tp->sourceFilePathHint.empty())
    {
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(tp->sourceFilePathHint.c_str(), GetFileExInfoStandard, &fad) &&
            !(fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            sourcePathForKey = tp->sourceFilePathHint;

            // Normalize to absolute path when possible to reduce cache misses from relative-path spelling.
            wchar_t fullBuf[MAX_PATH] = {};
            DWORD fullLen = GetFullPathNameW(tp->sourceFilePathHint.c_str(), MAX_PATH, fullBuf, nullptr);
            if (fullLen > 0 && fullLen < MAX_PATH)
                sourcePathForKey.assign(fullBuf, fullLen);

            cacheKeyMode = 1;
            Fnv1a64MixValue(h, cacheKeyMode);
            if (!sourcePathForKey.empty())
                Fnv1a64MixBytes(h, sourcePathForKey.data(), sourcePathForKey.size() * sizeof(wchar_t));
            Fnv1a64MixValue(h, fad.nFileSizeHigh);
            Fnv1a64MixValue(h, fad.nFileSizeLow);
            Fnv1a64MixValue(h, fad.ftLastWriteTime.dwHighDateTime);
            Fnv1a64MixValue(h, fad.ftLastWriteTime.dwLowDateTime);
            usedSourceHint = true;
        }
    }

    if (!usedSourceHint)
    {
        cacheKeyMode = 0;
        Fnv1a64MixValue(h, cacheKeyMode);
        if (!tp->samples->empty())
            Fnv1a64MixBytes(h, tp->samples->data(), tp->samples->size() * sizeof(short));
    }

    wchar_t fileName[256] = {};
    swprintf_s(
        fileName,
        L"env_m%u_sr%u_ch%u_b%u_f%llu_%016llx.wfc",
        cacheKeyMode,
        sr,
        ch,
        envBlock,
        static_cast<unsigned long long>(frames64),
        static_cast<unsigned long long>(h));

    tp->envCacheSampleHash = h;
    tp->envCachePath = (GetEnvelopeCacheDirectoryPath() / fileName).wstring();
    tp->envCacheKeyValid = true;
    return true;
}

static bool TryLoadEnvelopeCache(ThreadParam* tp, std::size_t totalFrames)
{
    if (!tp || totalFrames == 0)
        return false;
    if (!EnsureEnvelopeCacheKey(tp, totalFrames))
        return false;
    if (tp->envCachePath.empty())
        return false;

    FILE* f = nullptr;
    _wfopen_s(&f, tp->envCachePath.c_str(), L"rb");
    if (!f)
        return false;

    auto readExact = [&](void* dst, std::size_t bytes) -> bool
    {
        return bytes == 0 || fread(dst, 1, bytes, f) == bytes;
    };
    auto readU32 = [&](std::uint32_t& v) -> bool { return readExact(&v, sizeof(v)); };
    auto readU64 = [&](std::uint64_t& v) -> bool { return readExact(&v, sizeof(v)); };
    auto readF32 = [&](float& v) -> bool { return readExact(&v, sizeof(v)); };
    auto readFloatVec = [&](std::size_t count, std::vector<float>& out) -> bool
    {
        out.resize(count);
        return count == 0 || readExact(out.data(), count * sizeof(float));
    };

    bool ok = true;
    char magic[8] = {};
    std::uint32_t version = 0;
    std::uint32_t sr = 0, ch = 0, envBlock = 0;
    std::uint64_t frames64 = 0, hash64 = 0;
    std::uint32_t levelCount = 0;
    float headroom = 0.0f;
    std::uint32_t reserved = 0;

    ok = ok && readExact(magic, sizeof(magic));
    ok = ok && readU32(version);
    ok = ok && readU32(sr);
    ok = ok && readU32(ch);
    ok = ok && readU32(envBlock);
    ok = ok && readU64(frames64);
    ok = ok && readU64(hash64);
    ok = ok && readU32(levelCount);
    ok = ok && readF32(headroom);
    ok = ok && readU32(reserved);

    const char kMagic[8] = { 'W','E','N','V','C','H','1','\0' };
    const std::uint32_t expectedSr = static_cast<std::uint32_t>((std::max)(tp->sampleRate, 0));
    const std::uint32_t expectedCh = static_cast<std::uint32_t>(tp->isStereo ? 2 : 1);
    const std::uint32_t expectedBlock = static_cast<std::uint32_t>((std::max)(tp->envBlock, 0));
    const std::uint64_t expectedFrames = static_cast<std::uint64_t>(totalFrames);

    ok = ok && (std::memcmp(magic, kMagic, sizeof(kMagic)) == 0);
    ok = ok && (version == 1);
    ok = ok && (sr == expectedSr);
    ok = ok && (ch == expectedCh);
    ok = ok && (envBlock == expectedBlock);
    ok = ok && (frames64 == expectedFrames);
    ok = ok && (hash64 == tp->envCacheSampleHash);
    ok = ok && (std::fabs(headroom - tp->displayHeadroom) <= 1e-6f);
    ok = ok && (levelCount >= 1 && levelCount <= 64);

    struct LoadedLevel
    {
        int block = 0;
        std::vector<float> baseMin, baseMax, lowMin, lowMax, midMin, midMax, highMin, highMax;
    };

    std::vector<LoadedLevel> levels;
    levels.reserve(levelCount);

    for (std::uint32_t li = 0; ok && li < levelCount; ++li)
    {
        std::uint32_t block32 = 0;
        std::uint64_t blocks64 = 0;
        ok = ok && readU32(block32);
        ok = ok && readU64(blocks64);
        ok = ok && block32 > 0;
        ok = ok && blocks64 <= (1ull << 31); // sanity guard
        if (!ok) break;

        LoadedLevel lvl{};
        lvl.block = static_cast<int>(block32);
        const std::size_t blocks = static_cast<std::size_t>(blocks64);
        ok = ok && readFloatVec(blocks, lvl.baseMin);
        ok = ok && readFloatVec(blocks, lvl.baseMax);
        ok = ok && readFloatVec(blocks, lvl.lowMin);
        ok = ok && readFloatVec(blocks, lvl.lowMax);
        ok = ok && readFloatVec(blocks, lvl.midMin);
        ok = ok && readFloatVec(blocks, lvl.midMax);
        ok = ok && readFloatVec(blocks, lvl.highMin);
        ok = ok && readFloatVec(blocks, lvl.highMax);
        if (ok)
            levels.push_back(std::move(lvl));
    }

    fclose(f);

    if (!ok || levels.empty())
        return false;

    // Validate monotonic block sizes (base, then coarser levels).
    for (std::size_t i = 1; i < levels.size(); ++i)
    {
        if (levels[i].block <= levels[i - 1].block)
            return false;
    }

    tp->baseMinF = std::move(levels[0].baseMin);
    tp->baseMaxF = std::move(levels[0].baseMax);
    tp->lowMinF = std::move(levels[0].lowMin);
    tp->lowMaxF = std::move(levels[0].lowMax);
    tp->midMinF = std::move(levels[0].midMin);
    tp->midMaxF = std::move(levels[0].midMax);
    tp->highMinF = std::move(levels[0].highMin);
    tp->highMaxF = std::move(levels[0].highMax);
    tp->envBlocks = tp->baseMinF.size();
    tp->envMipLevels.clear();
    tp->envMipLevels.reserve(levels.size() > 0 ? levels.size() - 1 : 0);

    for (std::size_t i = 1; i < levels.size(); ++i)
    {
        EnvelopeMipLevel lvl{};
        lvl.block = levels[i].block;
        lvl.blocks = levels[i].baseMin.size();
        lvl.baseMinF = std::move(levels[i].baseMin);
        lvl.baseMaxF = std::move(levels[i].baseMax);
        lvl.lowMinF = std::move(levels[i].lowMin);
        lvl.lowMaxF = std::move(levels[i].lowMax);
        lvl.midMinF = std::move(levels[i].midMin);
        lvl.midMaxF = std::move(levels[i].midMax);
        lvl.highMinF = std::move(levels[i].highMin);
        lvl.highMaxF = std::move(levels[i].highMax);
        tp->envMipLevels.push_back(std::move(lvl));
    }

    return true;
}

static void TrySaveEnvelopeCache(const ThreadParam* tp, std::size_t totalFrames)
{
    if (!tp || totalFrames == 0 || tp->envBlocks == 0 || tp->envCachePath.empty())
        return;

    std::filesystem::path outPath(tp->envCachePath);
    std::error_code ec;
    std::filesystem::create_directories(outPath.parent_path(), ec);

    const std::wstring tmpPath = (outPath.wstring() + L".tmp");
    FILE* f = nullptr;
    _wfopen_s(&f, tmpPath.c_str(), L"wb");
    if (!f)
        return;

    auto writeExact = [&](const void* src, std::size_t bytes) -> bool
    {
        return bytes == 0 || fwrite(src, 1, bytes, f) == bytes;
    };
    auto writeU32 = [&](std::uint32_t v) -> bool { return writeExact(&v, sizeof(v)); };
    auto writeU64 = [&](std::uint64_t v) -> bool { return writeExact(&v, sizeof(v)); };
    auto writeF32 = [&](float v) -> bool { return writeExact(&v, sizeof(v)); };
    auto writeFloatVec = [&](const std::vector<float>& v) -> bool
    {
        return v.empty() || writeExact(v.data(), v.size() * sizeof(float));
    };
    auto writeLevel = [&](int block, std::size_t blocks,
        const std::vector<float>& baseMin, const std::vector<float>& baseMax,
        const std::vector<float>& lowMin, const std::vector<float>& lowMax,
        const std::vector<float>& midMin, const std::vector<float>& midMax,
        const std::vector<float>& highMin, const std::vector<float>& highMax) -> bool
    {
        if (block <= 0) return false;
        if (baseMin.size() != blocks || baseMax.size() != blocks ||
            lowMin.size() != blocks || lowMax.size() != blocks ||
            midMin.size() != blocks || midMax.size() != blocks ||
            highMin.size() != blocks || highMax.size() != blocks)
            return false;
        return writeU32(static_cast<std::uint32_t>(block)) &&
            writeU64(static_cast<std::uint64_t>(blocks)) &&
            writeFloatVec(baseMin) && writeFloatVec(baseMax) &&
            writeFloatVec(lowMin) && writeFloatVec(lowMax) &&
            writeFloatVec(midMin) && writeFloatVec(midMax) &&
            writeFloatVec(highMin) && writeFloatVec(highMax);
    };

    const char kMagic[8] = { 'W','E','N','V','C','H','1','\0' };
    const std::uint32_t levelCount = static_cast<std::uint32_t>(1 + tp->envMipLevels.size());
    bool ok = true;
    ok = ok && writeExact(kMagic, sizeof(kMagic));
    ok = ok && writeU32(1); // version
    ok = ok && writeU32(static_cast<std::uint32_t>((std::max)(tp->sampleRate, 0)));
    ok = ok && writeU32(static_cast<std::uint32_t>(tp->isStereo ? 2 : 1));
    ok = ok && writeU32(static_cast<std::uint32_t>((std::max)(tp->envBlock, 0)));
    ok = ok && writeU64(static_cast<std::uint64_t>(totalFrames));
    ok = ok && writeU64(tp->envCacheSampleHash);
    ok = ok && writeU32(levelCount);
    ok = ok && writeF32(tp->displayHeadroom);
    ok = ok && writeU32(0); // reserved

    ok = ok && writeLevel(tp->envBlock, tp->envBlocks,
        tp->baseMinF, tp->baseMaxF,
        tp->lowMinF, tp->lowMaxF,
        tp->midMinF, tp->midMaxF,
        tp->highMinF, tp->highMaxF);

    for (const EnvelopeMipLevel& lvl : tp->envMipLevels)
    {
        if (!ok) break;
        ok = writeLevel(lvl.block, lvl.blocks,
            lvl.baseMinF, lvl.baseMaxF,
            lvl.lowMinF, lvl.lowMaxF,
            lvl.midMinF, lvl.midMaxF,
            lvl.highMinF, lvl.highMaxF);
    }

    fclose(f);

    if (!ok)
    {
        DeleteFileW(tmpPath.c_str());
        return;
    }

    std::filesystem::remove(outPath, ec);
    std::filesystem::rename(std::filesystem::path(tmpPath), outPath, ec);
    if (ec)
    {
        // Best-effort fallback: leave tmp if rename fails.
    }
}

static void PrepareColorWaveEnvelopes(ThreadParam* tp)
{
    if (!tp)
        return;

    tp->baseMinF.clear(); tp->baseMaxF.clear();
    tp->lowMinF.clear();  tp->lowMaxF.clear();
    tp->midMinF.clear();  tp->midMaxF.clear();
    tp->highMinF.clear(); tp->highMaxF.clear();
    tp->envBlocks = 0;
    tp->envMipLevels.clear();

    if (!tp->samples || tp->samples->empty() || tp->sampleRate <= 0 || tp->envBlock <= 0)
        return;

    const size_t totalFrames = GetTotalFrames(tp);
    if (totalFrames == 0)
        return;

    if (TryLoadEnvelopeCache(tp, totalFrames))
        return;

    auto sampleMonoNorm = [&](size_t frameIndex) -> float
    {
        if (tp->isStereo)
        {
            const size_t base = frameIndex * 2;
            const int l = (base < tp->samples->size()) ? (*tp->samples)[base] : 0;
            const int r = (base + 1 < tp->samples->size()) ? (*tp->samples)[base + 1] : l;
            return static_cast<float>((l + r) * (0.5 / 32768.0));
        }
        return static_cast<float>((*tp->samples)[frameIndex] / 32768.0);
    };

    // Pass 1: global normalization factor (matches old BuildMonoDisplayFrames behavior).
    float maxAbs = 0.0f;
    for (size_t i = 0; i < totalFrames; ++i)
    {
        const float x = sampleMonoNorm(i);
        maxAbs = (std::max)(maxAbs, static_cast<float>(std::fabs(x)));
    }
    const float invNorm = (maxAbs > 1e-12f) ? (1.0f / maxAbs) : 1.0f;

    const size_t nb = (totalFrames + static_cast<size_t>(tp->envBlock) - 1) / static_cast<size_t>(tp->envBlock);
    tp->baseMinF.reserve(nb); tp->baseMaxF.reserve(nb);
    tp->lowMinF.reserve(nb);  tp->lowMaxF.reserve(nb);
    tp->midMinF.reserve(nb);  tp->midMaxF.reserve(nb);
    tp->highMinF.reserve(nb); tp->highMaxF.reserve(nb);

    auto alphaForFc = [&](double fcHz) -> double
    {
        if (fcHz <= 0.0) return 1.0;
        constexpr double kPi = 3.14159265358979323846;
        const double dt = 1.0 / static_cast<double>(tp->sampleRate);
        const double rc = 1.0 / (2.0 * kPi * fcHz);
        return dt / (rc + dt);
    };

    const double a200 = alphaForFc(200.0);
    const double a2000 = alphaForFc(2000.0);

    double lp200 = 0.0;      // lowpass(disp, 200)
    double lpDisp2000 = 0.0; // lowpass(disp, 2000)
    double lpHp2000 = 0.0;   // lowpass(hpLow, 2000) => mid

    float bBaseMin = std::numeric_limits<float>::max();
    float bBaseMax = -std::numeric_limits<float>::max();
    float bLowMin = std::numeric_limits<float>::max();
    float bLowMax = -std::numeric_limits<float>::max();
    float bMidMin = std::numeric_limits<float>::max();
    float bMidMax = -std::numeric_limits<float>::max();
    float bHighMin = std::numeric_limits<float>::max();
    float bHighMax = -std::numeric_limits<float>::max();
    int inBlock = 0;

    auto flushBlock = [&]()
    {
        if (inBlock <= 0) return;
        tp->baseMinF.push_back((bBaseMin == std::numeric_limits<float>::max()) ? 0.0f : bBaseMin);
        tp->baseMaxF.push_back((bBaseMax == -std::numeric_limits<float>::max()) ? 0.0f : bBaseMax);
        tp->lowMinF.push_back((bLowMin == std::numeric_limits<float>::max()) ? 0.0f : bLowMin);
        tp->lowMaxF.push_back((bLowMax == -std::numeric_limits<float>::max()) ? 0.0f : bLowMax);
        tp->midMinF.push_back((bMidMin == std::numeric_limits<float>::max()) ? 0.0f : bMidMin);
        tp->midMaxF.push_back((bMidMax == -std::numeric_limits<float>::max()) ? 0.0f : bMidMax);
        tp->highMinF.push_back((bHighMin == std::numeric_limits<float>::max()) ? 0.0f : bHighMin);
        tp->highMaxF.push_back((bHighMax == -std::numeric_limits<float>::max()) ? 0.0f : bHighMax);

        bBaseMin = bLowMin = bMidMin = bHighMin = std::numeric_limits<float>::max();
        bBaseMax = bLowMax = bMidMax = bHighMax = -std::numeric_limits<float>::max();
        inBlock = 0;
    };

    for (size_t i = 0; i < totalFrames; ++i)
    {
        float x = sampleMonoNorm(i) * invNorm;
        x = ClampFloat(x, -tp->displayHeadroom, tp->displayHeadroom);

        lp200 = lp200 + a200 * (static_cast<double>(x) - lp200);
        const double lowD = lp200;
        const double hpLowD = static_cast<double>(x) - lp200;

        lpHp2000 = lpHp2000 + a2000 * (hpLowD - lpHp2000);
        const double midD = lpHp2000;

        lpDisp2000 = lpDisp2000 + a2000 * (static_cast<double>(x) - lpDisp2000);
        const double highD = static_cast<double>(x) - lpDisp2000;

        const float low = ClampFloat(static_cast<float>(lowD), -tp->displayHeadroom, tp->displayHeadroom);
        const float mid = ClampFloat(static_cast<float>(midD), -tp->displayHeadroom, tp->displayHeadroom);
        const float high = ClampFloat(static_cast<float>(highD), -tp->displayHeadroom, tp->displayHeadroom);

        bBaseMin = (std::min)(bBaseMin, x);
        bBaseMax = (std::max)(bBaseMax, x);
        bLowMin = (std::min)(bLowMin, low);
        bLowMax = (std::max)(bLowMax, low);
        bMidMin = (std::min)(bMidMin, mid);
        bMidMax = (std::max)(bMidMax, mid);
        bHighMin = (std::min)(bHighMin, high);
        bHighMax = (std::max)(bHighMax, high);

        ++inBlock;
        if (inBlock >= tp->envBlock)
            flushBlock();
    }
    flushBlock();

    tp->envBlocks = tp->baseMinF.size();
    BuildEnvelopeMipPyramid(tp);
    TrySaveEnvelopeCache(tp, totalFrames);
}

static void DrawEnvelopeLayer(
    HDC hdc,
    const RECT& waveRc,
    int midY,
    double ampScale,
    COLORREF color,
    const std::vector<float>& vmin,
    const std::vector<float>& vmax,
    size_t b0,
    size_t b1,
    int envBlock,
    size_t totalFrames,
    double startFrame,
    double visibleFrames,
    float plotYRange)
{
    if (b1 <= b0 || vmin.empty() || vmax.empty() || envBlock <= 0 || totalFrames == 0 || visibleFrames <= 0.0)
        return;

    const int w = waveRc.right - waveRc.left;
    if (w <= 0) return;

    HGDIOBJ oldBrushObj = SelectObject(hdc, GetStockObject(DC_BRUSH));
    const COLORREF oldDcBrushColor = SetDCBrushColor(hdc, color);

    auto toY = [&](float v) -> int
    {
        const float clamped = ClampFloat(v, -plotYRange, plotYRange);
        return midY - static_cast<int>(std::lround(static_cast<double>(clamped) * ampScale));
    };

    for (size_t b = b0; b < b1 && b < vmin.size() && b < vmax.size(); ++b)
    {
        const double frameStart = static_cast<double>(b * static_cast<size_t>(envBlock));
        const double frameEnd = static_cast<double>((std::min)((b + 1) * static_cast<size_t>(envBlock), totalFrames));

        int x0 = waveRc.left + static_cast<int>(std::floor(((frameStart - startFrame) / visibleFrames) * w));
        int x1 = waveRc.left + static_cast<int>(std::ceil (((frameEnd   - startFrame) / visibleFrames) * w));

        if (x1 < waveRc.left || x0 >= waveRc.right)
            continue;

        x0 = (std::max)(x0, static_cast<int>(waveRc.left));
        x1 = (std::min)(x1, static_cast<int>(waveRc.right - 1));
        if (x1 < x0) x1 = x0;

        const int yPos = toY((std::max)(vmax[b], 0.0f));
        const int yNeg = toY((std::min)(vmin[b], 0.0f));

        if (yPos != midY)
        {
            RECT rPos{
                x0,
                (std::min)(midY, yPos),
                x1 + 1,
                (std::max)(midY, yPos) + 1
            };
            FillRect(hdc, &rPos, (HBRUSH)GetStockObject(DC_BRUSH));
        }
        if (yNeg != midY)
        {
            RECT rNeg{
                x0,
                (std::min)(midY, yNeg),
                x1 + 1,
                (std::max)(midY, yNeg) + 1
            };
            FillRect(hdc, &rNeg, (HBRUSH)GetStockObject(DC_BRUSH));
        }
    }
    SetDCBrushColor(hdc, oldDcBrushColor);
    SelectObject(hdc, oldBrushObj);
}

static void BuildCacheIfNeeded(ThreadParam* tp, int widthPx)
{
    if (!tp || !tp->samples || tp->samples->empty() || widthPx <= 0)
        return;

    if (!tp->cacheDirty && tp->cacheW == widthPx &&
        (int)tp->cacheMinS.size() == widthPx &&
        (int)tp->cacheColorref.size() == widthPx)
    {
        return;
    }

    if (tp->analyzer.nfft() <= 0 || tp->analyzer.sampleRate() != tp->sampleRate)
        tp->analyzer.init(tp->nfft, tp->sampleRate);

    // If stereo, build a temporary mono frame buffer (average L/R) for the analyzer/cache.
    std::vector<short> monoFrames;
    const short* dataPtr = tp->samples->data();
    size_t totalFrames = tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();

    if (tp->isStereo)
    {
        monoFrames.resize(totalFrames);
        for (size_t i = 0; i < totalFrames; ++i)
        {
            int l = dataPtr[2 * i];
            int r = dataPtr[2 * i + 1];
            monoFrames[i] = static_cast<short>((l + r) / 2);
        }
        dsp::build_waveform_cache_pcm16(
            monoFrames.data(),
            monoFrames.size(),
            tp->sampleRate,
            widthPx,
            tp->analyzer,
            tp->bands,
            tp->cacheMinS,
            tp->cacheMaxS,
            tp->cacheColorref
        );
    }
    else
    {
        dsp::build_waveform_cache_pcm16(
            dataPtr,
            tp->samples->size(),
            tp->sampleRate,
            widthPx,
            tp->analyzer,
            tp->bands,
            tp->cacheMinS,
            tp->cacheMaxS,
            tp->cacheColorref
        );
    }

    tp->cacheW = widthPx;
    tp->cacheDirty = false;
}

static inline short SampleFrameValue(ThreadParam* tp, size_t frameIndex)
{
    // return mono value to draw for frameIndex (averaged if stereo).
    if (!tp || !tp->samples) return 0;
    if (tp->isStereo)
    {
        size_t base = frameIndex * 2;
        if (base + 1 >= tp->samples->size()) return tp->samples->at(std::min(base, tp->samples->size() - 1));
        int l = tp->samples->at(base);
        int r = tp->samples->at(base + 1);
        return static_cast<short>((l + r) / 2);
    }
    else
    {
        if (frameIndex >= tp->samples->size()) return tp->samples->back();
        return tp->samples->at(frameIndex);
    }
}

static double SampleFrameValueLinear(ThreadParam* tp, double frameIndexD, size_t totalFrames)
{
    if (!tp || !tp->samples || tp->samples->empty() || totalFrames == 0)
        return 0.0;

    if (frameIndexD <= 0.0) return static_cast<double>(SampleFrameValue(tp, 0));
    double maxFrame = static_cast<double>(totalFrames - 1);
    if (frameIndexD >= maxFrame) return static_cast<double>(SampleFrameValue(tp, totalFrames - 1));

    size_t i0 = static_cast<size_t>(std::floor(frameIndexD));
    size_t i1 = std::min(i0 + 1, totalFrames - 1);
    double t = frameIndexD - static_cast<double>(i0);
    double v0 = static_cast<double>(SampleFrameValue(tp, i0));
    double v1 = static_cast<double>(SampleFrameValue(tp, i1));
    return v0 + (v1 - v0) * t;
}

static PixelAggregate AggregatePixelRange(ThreadParam* tp, size_t totalFrames, double frameStartD, double frameEndD)
{
    PixelAggregate out{};
    if (!tp || !tp->samples || tp->samples->empty() || totalFrames == 0)
        return out;

    if (frameEndD < frameStartD) std::swap(frameStartD, frameEndD);

    const double maxFrame = static_cast<double>(totalFrames - 1);
    frameStartD = std::clamp(frameStartD, 0.0, maxFrame);
    frameEndD = std::clamp(frameEndD, 0.0, static_cast<double>(totalFrames));

    const double width = frameEndD - frameStartD;
    if (width <= 1e-9)
    {
        double v = SampleFrameValueLinear(tp, frameStartD, totalFrames);
        short s = ClampShort16(v);
        out.minV = s;
        out.maxV = s;
        out.avgV = v;
        return out;
    }

    // Sub-frame / small ranges: use interpolation samples to avoid blocky zoom artifacts.
    if (width <= 8.0)
    {
        int sampleCount = std::clamp(static_cast<int>(std::ceil(width * 4.0)), 3, 64);
        double mn = 32767.0;
        double mx = -32768.0;
        double sum = 0.0;
        for (int i = 0; i < sampleCount; ++i)
        {
            double t = (sampleCount == 1) ? 0.5 : (static_cast<double>(i) / static_cast<double>(sampleCount - 1));
            double fp = frameStartD + t * width;
            double v = SampleFrameValueLinear(tp, fp, totalFrames);
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            sum += v;
        }
        out.minV = ClampShort16(mn);
        out.maxV = ClampShort16(mx);
        out.avgV = sum / static_cast<double>(sampleCount);
        return out;
    }

    // Wider ranges: zoom-aware weighted average plus exact min/max over touched frames.
    size_t i0 = static_cast<size_t>(std::floor(frameStartD));
    size_t i1 = static_cast<size_t>(std::ceil(frameEndD));
    if (i0 >= totalFrames) i0 = totalFrames - 1;
    if (i1 > totalFrames) i1 = totalFrames;

    double weightedSum = 0.0;
    double totalWeight = 0.0;
    short mn = SHRT_MAX;
    short mx = SHRT_MIN;

    for (size_t fi = i0; fi < i1; ++fi)
    {
        double segStart = std::max(frameStartD, static_cast<double>(fi));
        double segEnd = std::min(frameEndD, static_cast<double>(fi + 1));
        double w = segEnd - segStart;
        if (w <= 0.0) continue;

        short v = SampleFrameValue(tp, fi);
        mn = std::min(mn, v);
        mx = std::max(mx, v);
        weightedSum += static_cast<double>(v) * w;
        totalWeight += w;
    }

    if (mn == SHRT_MAX || mx == SHRT_MIN || totalWeight <= 0.0)
    {
        double v = SampleFrameValueLinear(tp, 0.5 * (frameStartD + frameEndD), totalFrames);
        short s = ClampShort16(v);
        out.minV = s;
        out.maxV = s;
        out.avgV = v;
        return out;
    }

    out.minV = mn;
    out.maxV = mx;
    out.avgV = weightedSum / totalWeight;
    return out;
}

static void CALLBACK RenderTimerCallback(PVOID lpParameter, BOOLEAN /*timerOrWaitFired*/)
{
    ThreadParam* tp = reinterpret_cast<ThreadParam*>(lpParameter);
    if (!tp || !tp->hwnd) return;
    bool expected = false;
    if (!tp->renderTickQueued.compare_exchange_strong(expected, true))
        return;
    PostMessageW(tp->hwnd, WM_WAVEFORM_TICK, 0, 0);
}

static void HandleRenderTick(HWND hwnd, ThreadParam* tp)
{
    if (!tp) return;
    tp->renderTickQueued.store(false);

    if (!tp->playing.load())
        return; // avoid continuous idle repaints while paused/stopped

    if (tp->playing.load() && tp->samples)
    {
        size_t totalFrames = tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
        size_t curFrame = 0;
        if (UsingAudioEngine(tp))
        {
            curFrame = tp->audioEngine.GetCurrentFrame();
            if (!tp->audioEngine.IsPlaying())
                tp->playing.store(false);
        }
        else
        {
            auto now = std::chrono::steady_clock::now();
            const double elapsedSec = std::chrono::duration<double>(now - tp->playStartTime).count();
            curFrame = static_cast<size_t>(elapsedSec * tp->sampleRate);
        }
        if (curFrame >= totalFrames)
        {
            tp->playing.store(false);
            tp->pausedSampleIndex = 0;
            if (UsingAudioEngine(tp))
                tp->audioEngine.SeekFrame(0);
        }
    }

    if (tp->liveResizeActive)
        return; // keep resize interactions responsive; repaint when the resize step itself invalidates

    InvalidateWaveRegion(hwnd, tp);
}

static void DrawBeatGridOverlay(HDC hdc, const RECT& waveRc, const ThreadParam* tp,
    double startFrame, double visibleFrames, size_t totalFrames, int sampleRate)
{
    if (!tp || !tp->gridEnabled || tp->gridBpm <= 0.0 || sampleRate <= 0) return;
    if (visibleFrames <= 0.0 || totalFrames == 0) return;
    const int w = static_cast<int>(waveRc.right - waveRc.left);
    if (w <= 0) return;

    const double beatPeriod = 60.0 / tp->gridBpm;
    if (!std::isfinite(beatPeriod) || beatPeriod <= 0.0) return;

    const double tLeft = startFrame / static_cast<double>(sampleRate);
    const double tRight = (startFrame + visibleFrames) / static_cast<double>(sampleRate);
    const int beatsPerBar = (std::max)(1, tp->gridBeatsPerBar);

    long long k0 = static_cast<long long>(std::floor((tLeft - tp->gridT0Seconds) / beatPeriod)) - 2;
    long long k1 = static_cast<long long>(std::ceil((tRight - tp->gridT0Seconds) / beatPeriod)) + 2;

    HPEN beatPen = CreatePen(PS_SOLID, 1, RGB(255, 165, 0));
    HPEN barPen = CreatePen(PS_SOLID, 2, RGB(255, 64, 64));
    HGDIOBJ oldPen = SelectObject(hdc, beatPen);

    for (long long k = k0; k <= k1; ++k)
    {
        const double tg = tp->gridT0Seconds + static_cast<double>(k) * beatPeriod;
        if (tg < tLeft || tg > tRight) continue;
        const double xNorm = (tg - tLeft) / (tRight - tLeft);
        int x = waveRc.left + static_cast<int>(std::lround(xNorm * static_cast<double>(w)));
        x = (std::max)(static_cast<int>(waveRc.left), (std::min)(x, static_cast<int>(waveRc.right - 1)));

        const bool isBar = ((k % beatsPerBar) == 0);
        SelectObject(hdc, isBar ? barPen : beatPen);
        MoveToEx(hdc, x, waveRc.top, NULL);
        LineTo(hdc, x, waveRc.bottom);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(beatPen);
    DeleteObject(barPen);
}

static void DrawTimeMarksOverlay(HDC hdc, const RECT& waveRc, double tLeft, double tRight, bool drawLabels = true)
{
    if (!std::isfinite(tLeft) || !std::isfinite(tRight) || tRight <= tLeft) return;
    const int w = static_cast<int>(waveRc.right - waveRc.left);
    const int h = static_cast<int>(waveRc.bottom - waveRc.top);
    if (w <= 0 || h <= 0) return;

    const double visibleSeconds = tRight - tLeft;
    const double tickStep = ChooseTimeTickStep(visibleSeconds);
    const double tFirst = std::floor(tLeft / tickStep) * tickStep;

    HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    HGDIOBJ oldPen = SelectObject(hdc, tickPen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(180, 180, 180));

    const bool showMillis = (tickStep < 1.0);
    for (double t = tFirst; t <= tRight + tickStep * 0.5; t += tickStep)
    {
        if (t < tLeft - 1e-9) continue;
        const double xNorm = (t - tLeft) / visibleSeconds;
        int x = waveRc.left + static_cast<int>(std::lround(xNorm * static_cast<double>(w)));
        x = (std::max)(static_cast<int>(waveRc.left), (std::min)(x, static_cast<int>(waveRc.right - 1)));

        const int tickTop = (std::max)(waveRc.top, waveRc.bottom - 8);
        MoveToEx(hdc, x, tickTop, NULL);
        LineTo(hdc, x, waveRc.bottom);

        if (drawLabels)
        {
            std::wstring label = FormatTimeLabel(t, showMillis);
            TextOutW(hdc, x + 2, (std::max)(waveRc.top + 2, waveRc.bottom - 20), label.c_str(), static_cast<int>(label.size()));
        }
    }

    SelectObject(hdc, oldPen);
    DeleteObject(tickPen);
}

static void InvalidateEmbeddedPianoSpec(ThreadParam* tp)
{
    if (!tp) return;
    tp->embeddedPianoSpecDirty = true;
}

static void OpenPianoSpectrogramPopoutFromWaveform(ThreadParam* tp)
{
    if (!tp) return;
    WaveformWindow::GridOverlayConfig grid{};
    grid.enabled = tp->gridEnabled;
    grid.bpm = tp->gridBpm;
    grid.t0Seconds = tp->gridT0Seconds;
    grid.beatsPerBar = tp->gridBeatsPerBar;
    grid.audioStartSeconds = tp->gridAudioStartSeconds;
    grid.approxOnsetSeconds = tp->gridApproxOnsetSeconds;
    grid.kickAttackSeconds = tp->gridKickAttackSeconds;

    std::wstring title = tp->title;
    if (title.empty()) title = L"Waveform";
    title += L" - Piano Spectrogram";
    SpectrogramWindow::ShowPianoSpectrogramAsyncRefStereoSynced(tp->samples, tp->sampleRate, grid, title);
}

static const wchar_t* EmbeddedPianoGridModeLabel(int mode)
{
    switch (mode)
    {
    case kEmbeddedPianoGrid_None: return L"None";
    case kEmbeddedPianoGrid_1_6_Step: return L"1/6 step";
    case kEmbeddedPianoGrid_1_4_Step: return L"1/4 step";
    case kEmbeddedPianoGrid_1_3_Step: return L"1/3 step";
    case kEmbeddedPianoGrid_1_2_Step: return L"1/2 step";
    case kEmbeddedPianoGrid_Step: return L"Step";
    case kEmbeddedPianoGrid_1_6_Beat: return L"1/6 beat";
    case kEmbeddedPianoGrid_1_4_Beat: return L"1/4 beat";
    case kEmbeddedPianoGrid_1_3_Beat: return L"1/3 beat";
    case kEmbeddedPianoGrid_1_2_Beat: return L"1/2 beat";
    case kEmbeddedPianoGrid_Beat: return L"Beat";
    case kEmbeddedPianoGrid_Bar: return L"Bar";
    default: return L"Beat";
    }
}

static double EmbeddedPianoGridModeBeats(int mode, int beatsPerBar)
{
    switch (mode)
    {
    case kEmbeddedPianoGrid_1_6_Step: return 1.0 / 24.0; // 1/6 of a 1/4-beat step
    case kEmbeddedPianoGrid_1_4_Step: return 1.0 / 16.0;
    case kEmbeddedPianoGrid_1_3_Step: return 1.0 / 12.0;
    case kEmbeddedPianoGrid_1_2_Step: return 1.0 / 8.0;
    case kEmbeddedPianoGrid_Step: return 1.0 / 4.0;
    case kEmbeddedPianoGrid_1_6_Beat: return 1.0 / 6.0;
    case kEmbeddedPianoGrid_1_4_Beat: return 1.0 / 4.0;
    case kEmbeddedPianoGrid_1_3_Beat: return 1.0 / 3.0;
    case kEmbeddedPianoGrid_1_2_Beat: return 1.0 / 2.0;
    case kEmbeddedPianoGrid_Beat: return 1.0;
    case kEmbeddedPianoGrid_Bar: return (double)(std::max)(1, beatsPerBar);
    default: return 0.0;
    }
}

static void DrawEmbeddedPianoSpectrogramMusicalGrid(HDC hdc, const RECT& plotRc, const RECT& timeRc,
    ThreadParam* tp, double tLeft, double tRight)
{
    if (!hdc || !tp) return;
    if (!tp->gridEnabled || tp->gridBpm <= 0.0) return;
    if (!std::isfinite(tLeft) || !std::isfinite(tRight) || !(tRight > tLeft)) return;
    const int sharedGridMode = WaveformWindow::GetSharedPianoGridMode();
    if (sharedGridMode == kEmbeddedPianoGrid_None) return;

    const int plotW = (std::max)(0, (int)(plotRc.right - plotRc.left));
    if (plotW <= 0) return;
    const double visibleSeconds = tRight - tLeft;
    if (!(visibleSeconds > 0.0)) return;

    const double beatSec = 60.0 / tp->gridBpm;
    if (!std::isfinite(beatSec) || beatSec <= 0.0) return;
    const int beatsPerBar = (std::max)(1, tp->gridBeatsPerBar);
    const double pxPerSec = (double)plotW / visibleSeconds;
    constexpr double kMinSubdivPx = 10.0;
    constexpr double kMinBeatPx = 12.0;
    constexpr double kMinBarPx = 14.0;

    struct Candidate { int mode; double beats; };
    Candidate cands[kEmbeddedPianoGrid_Count - 1]{};
    int candCount = 0;
    for (int m = 1; m < kEmbeddedPianoGrid_Count; ++m)
    {
        const double b = EmbeddedPianoGridModeBeats(m, beatsPerBar);
        if (b > 0.0 && std::isfinite(b))
            cands[candCount++] = Candidate{ m, b };
    }
    std::sort(cands, cands + candCount, [](const Candidate& a, const Candidate& b)
    {
        if (std::fabs(a.beats - b.beats) > 1e-12) return a.beats < b.beats;
        return a.mode < b.mode;
    });

    const double requestedBeats = EmbeddedPianoGridModeBeats(sharedGridMode, beatsPerBar);
    double chosenBeats = requestedBeats;
    int chosenMode = sharedGridMode;
    if (!(requestedBeats > 0.0))
        return;
    for (int i = 0; i < candCount; ++i)
    {
        if (cands[i].beats + 1e-12 < requestedBeats) continue; // never render finer than requested
        const double px = cands[i].beats * beatSec * pxPerSec;
        if (px >= kMinSubdivPx)
        {
            chosenBeats = cands[i].beats;
            chosenMode = cands[i].mode;
            break;
        }
        // fallback to coarsest if nothing is resolvable
        chosenBeats = cands[candCount - 1].beats;
        chosenMode = cands[candCount - 1].mode;
    }

    auto drawLinesAtBeats = [&](double lineBeats, HPEN pen)
    {
        if (!(lineBeats > 0.0) || !std::isfinite(lineBeats)) return;
        const double lineSec = lineBeats * beatSec;
        if (!(lineSec > 0.0)) return;
        long long k0 = (long long)std::floor((tLeft - tp->gridT0Seconds) / lineSec) - 2;
        long long k1 = (long long)std::ceil((tRight - tp->gridT0Seconds) / lineSec) + 2;
        HGDIOBJ oldPenLocal = SelectObject(hdc, pen);
        for (long long k = k0; k <= k1; ++k)
        {
            const double tg = tp->gridT0Seconds + (double)k * lineSec;
            if (tg < tLeft || tg > tRight) continue;
            const double xn = (tg - tLeft) / visibleSeconds;
            int x = plotRc.left + (int)std::lround(xn * (double)(plotW - 1));
            x = (std::max)((int)plotRc.left, (std::min)(x, (int)plotRc.right - 1));
            MoveToEx(hdc, x, plotRc.top, NULL);
            LineTo(hdc, x, plotRc.bottom);
        }
        SelectObject(hdc, oldPenLocal);
    };

    const double chosenPx = chosenBeats * beatSec * pxPerSec;
    HPEN subdivPen = CreatePen(PS_SOLID, 1, (chosenBeats < 1.0) ? RGB(80, 72, 36) : RGB(110, 95, 30));
    if (chosenPx >= kMinSubdivPx)
        drawLinesAtBeats(chosenBeats, subdivPen);
    DeleteObject(subdivPen);

    // Beat emphasis remains visible when subdivisions are shown and can be resolved.
    const double beatPx = beatSec * pxPerSec;
    if (chosenMode != kEmbeddedPianoGrid_Beat && chosenMode != kEmbeddedPianoGrid_Bar && beatPx >= kMinBeatPx)
    {
        HPEN beatPen = CreatePen(PS_SOLID, 1, RGB(120, 102, 28));
        drawLinesAtBeats(1.0, beatPen);
        DeleteObject(beatPen);
    }

    const double barPx = beatSec * (double)beatsPerBar * pxPerSec;
    if (barPx >= kMinBarPx)
    {
        HPEN barPen = CreatePen(PS_SOLID, 1, RGB(135, 55, 55));
        drawLinesAtBeats((double)beatsPerBar, barPen);
        DeleteObject(barPen);
    }

    (void)timeRc;
}

static const wchar_t* PianoRollGridModeLabel(int mode)
{
    using namespace PianoRollRenderer;
    switch (mode)
    {
    case Grid_None: return L"(none)";
    case Grid_1_6_Step: return L"1/6 step";
    case Grid_1_4_Step: return L"1/4 step";
    case Grid_1_3_Step: return L"1/3 step";
    case Grid_1_2_Step: return L"1/2 step";
    case Grid_Step: return L"Step";
    case Grid_1_6_Beat: return L"1/6 beat";
    case Grid_1_4_Beat: return L"1/4 beat";
    case Grid_1_3_Beat: return L"1/3 beat";
    case Grid_1_2_Beat: return L"1/2 beat";
    case Grid_Beat: return L"Beat";
    case Grid_Bar: return L"Bar";
    default: return L"Beat";
    }
}

static void LayoutPianoRollGridControl(ThreadParam* tp, const RECT& pianoRc)
{
    if (!tp)
        return;
    SetRectEmpty(&tp->pianoRollRcGridButton);
    if (tp->activePianoRollTab == kPianoSpectrogramTabIndex)
        return;
    if (pianoRc.right <= pianoRc.left || pianoRc.bottom <= pianoRc.top)
        return;

    constexpr int kPad = 6;
    constexpr int kH = 20;
    constexpr int kW = 118;
    if ((pianoRc.right - pianoRc.left) < 180 || (pianoRc.bottom - pianoRc.top) < (kH + 8))
        return;

    const int x = pianoRc.right - kPad;
    const int y = pianoRc.top + 6;
    tp->pianoRollRcGridButton = RECT{ (LONG)(x - kW), (LONG)y, (LONG)x, (LONG)(y + kH) };
}

static int SharedPianoGridMenuItemCount()
{
    return 12;
}

static int SharedPianoGridMenuModeAt(int idx)
{
    using namespace PianoRollRenderer;
    switch (idx)
    {
    case 0: return Grid_None;
    case 1: return Grid_1_6_Step;
    case 2: return Grid_1_4_Step;
    case 3: return Grid_1_3_Step;
    case 4: return Grid_1_2_Step;
    case 5: return Grid_Step;
    case 6: return Grid_1_6_Beat;
    case 7: return Grid_1_4_Beat;
    case 8: return Grid_1_3_Beat;
    case 9: return Grid_1_2_Beat;
    case 10: return Grid_Beat;
    case 11: return Grid_Bar;
    default: return Grid_Beat;
    }
}

static bool SharedPianoGridMenuSeparatorBefore(int idx)
{
    return idx == 1 || idx == 6;
}

static RECT SharedPianoGridMenuItemRect(const RECT& menuRc, int idx)
{
    constexpr int kPad = 4;
    constexpr int kRowH = 20;
    RECT r = menuRc;
    r.left += kPad;
    r.right -= kPad;
    r.top += kPad + idx * kRowH;
    r.bottom = r.top + kRowH;
    return r;
}

static int HitTestSharedPianoGridMenuItem(const ThreadParam* tp, POINT pt)
{
    if (!tp || !tp->sharedPianoGridMenuOpen) return -1;
    if (!PtInRect(&tp->sharedPianoGridMenuRc, pt)) return -1;
    const int count = SharedPianoGridMenuItemCount();
    for (int i = 0; i < count; ++i)
    {
        RECT ir = SharedPianoGridMenuItemRect(tp->sharedPianoGridMenuRc, i);
        if (PtInRect(&ir, pt))
            return i;
    }
    return -1;
}

static void OpenSharedPianoGridMenu(ThreadParam* tp, const RECT& anchorRc, const RECT& clientRc)
{
    if (!tp) return;
    constexpr int kMenuW = 156;
    constexpr int kPad = 4;
    constexpr int kRowH = 20;
    const int count = SharedPianoGridMenuItemCount();
    const int menuH = kPad * 2 + kRowH * count;

    RECT rc{
        anchorRc.left,
        anchorRc.bottom + 2,
        anchorRc.left + kMenuW,
        anchorRc.bottom + 2 + menuH
    };

    if (rc.right > clientRc.right - 4)
    {
        const int shift = rc.right - (clientRc.right - 4);
        OffsetRect(&rc, -shift, 0);
    }
    if (rc.left < clientRc.left + 4)
    {
        OffsetRect(&rc, (clientRc.left + 4) - rc.left, 0);
    }
    if (rc.bottom > clientRc.bottom - 4)
    {
        const int aboveTop = anchorRc.top - 2 - menuH;
        if (aboveTop >= clientRc.top + 4)
        {
            rc.top = aboveTop;
            rc.bottom = rc.top + menuH;
        }
        else
        {
            rc.bottom = (LONG)(std::max)(rc.top + 20, clientRc.bottom - 4);
        }
    }
    tp->sharedPianoGridMenuRc = rc;
    tp->sharedPianoGridMenuOpen = true;
    tp->sharedPianoGridMenuHoverIndex = -1;
    const int selectedMode = WaveformWindow::GetSharedPianoGridMode();
    for (int i = 0; i < count; ++i)
    {
        if (SharedPianoGridMenuModeAt(i) == selectedMode)
        {
            tp->sharedPianoGridMenuHoverIndex = i;
            break;
        }
    }
}

static void CloseSharedPianoGridMenu(ThreadParam* tp)
{
    if (!tp) return;
    tp->sharedPianoGridMenuOpen = false;
    tp->sharedPianoGridMenuHoverIndex = -1;
    SetRectEmpty(&tp->sharedPianoGridMenuRc);
}

static bool HandleSharedPianoGridMenuMouseDown(HWND hwnd, ThreadParam* tp, POINT pt)
{
    if (!tp || !hwnd || !tp->sharedPianoGridMenuOpen)
        return false;

    RECT oldRc = tp->sharedPianoGridMenuRc;
    if (!PtInRect(&tp->sharedPianoGridMenuRc, pt))
    {
        CloseSharedPianoGridMenu(tp);
        InvalidateRect(hwnd, &oldRc, FALSE);
        return false;
    }

    const int idx = HitTestSharedPianoGridMenuItem(tp, pt);
    if (idx >= 0)
    {
        WaveformWindow::SetSharedPianoGridMode(SharedPianoGridMenuModeAt(idx));
    }
    CloseSharedPianoGridMenu(tp);
    InvalidateRect(hwnd, &oldRc, FALSE);
    return true;
}

static bool UpdateSharedPianoGridMenuHover(HWND hwnd, ThreadParam* tp, POINT pt)
{
    if (!tp || !hwnd || !tp->sharedPianoGridMenuOpen)
        return false;
    const int hover = HitTestSharedPianoGridMenuItem(tp, pt);
    if (hover == tp->sharedPianoGridMenuHoverIndex)
        return true;
    tp->sharedPianoGridMenuHoverIndex = hover;
    InvalidateRect(hwnd, &tp->sharedPianoGridMenuRc, FALSE);
    return true;
}

static void DrawSharedPianoGridMenu(HDC hdc, const ThreadParam* tp)
{
    if (!hdc || !tp || !tp->sharedPianoGridMenuOpen) return;
    const RECT& menuRc = tp->sharedPianoGridMenuRc;
    if (menuRc.right <= menuRc.left || menuRc.bottom <= menuRc.top) return;

    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(hdc, &menuRc, bg);
    DeleteObject(bg);

    RECT inner = menuRc;
    InflateRect(&inner, -1, -1);
    HBRUSH innerBg = CreateSolidBrush(RGB(24, 27, 34));
    FillRect(hdc, &inner, innerBg);
    DeleteObject(innerBg);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(88, 92, 102));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, menuRc.left, menuRc.top, menuRc.right, menuRc.bottom);
    SelectObject(hdc, oldBrush);
    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));

    SetBkMode(hdc, TRANSPARENT);

    const int selectedMode = WaveformWindow::GetSharedPianoGridMode();
    const int count = SharedPianoGridMenuItemCount();
    for (int i = 0; i < count; ++i)
    {
        RECT ir = SharedPianoGridMenuItemRect(menuRc, i);
        const bool hovered = (i == tp->sharedPianoGridMenuHoverIndex);
        const bool selected = (SharedPianoGridMenuModeAt(i) == selectedMode);

        if (SharedPianoGridMenuSeparatorBefore(i))
        {
            HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(58, 62, 72));
            HGDIOBJ prevSep = SelectObject(hdc, sepPen);
            MoveToEx(hdc, ir.left, ir.top, NULL);
            LineTo(hdc, ir.right, ir.top);
            SelectObject(hdc, prevSep);
            DeleteObject(sepPen);
        }

        if (hovered || selected)
        {
            RECT fill = ir;
            fill.top += 1;
            HBRUSH hi = CreateSolidBrush(hovered ? RGB(42, 47, 58) : RGB(34, 40, 50));
            FillRect(hdc, &fill, hi);
            DeleteObject(hi);
        }

        RECT mark{ ir.left + 6, ir.top + 4, ir.left + 18, ir.top + 16 };
        HPEN boxPen = CreatePen(PS_SOLID, 1, RGB(92, 96, 106));
        HGDIOBJ prevBoxPen = SelectObject(hdc, boxPen);
        HGDIOBJ prevBoxBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, mark.left, mark.top, mark.right, mark.bottom);
        SelectObject(hdc, prevBoxBrush);
        SelectObject(hdc, prevBoxPen);
        DeleteObject(boxPen);

        if (selected)
        {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(80, 210, 220));
            HGDIOBJ prevTick = SelectObject(hdc, tickPen);
            MoveToEx(hdc, mark.left + 2, mark.top + 6, NULL);
            LineTo(hdc, mark.left + 5, mark.bottom - 2);
            LineTo(hdc, mark.right - 2, mark.top + 3);
            SelectObject(hdc, prevTick);
            DeleteObject(tickPen);
        }

        RECT tr = ir;
        tr.left = mark.right + 8;
        SetTextColor(hdc, hovered ? RGB(235, 240, 246) : RGB(214, 220, 228));
        DrawTextW(hdc, PianoRollGridModeLabel(SharedPianoGridMenuModeAt(i)), -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

static bool HandlePianoRollGridClick(HWND hwnd, ThreadParam* tp, const RECT& pianoRc, POINT pt)
{
    if (!tp || !hwnd)
        return false;
    if (tp->activePianoRollTab == kPianoSpectrogramTabIndex)
        return false;
    if (!PtInRect(&pianoRc, pt))
        return false;

    LayoutPianoRollGridControl(tp, pianoRc);
    if (tp->pianoRollRcGridButton.right <= tp->pianoRollRcGridButton.left)
        return false;
    if (!PtInRect(&tp->pianoRollRcGridButton, pt))
        return false;
    RECT clientRc{}; GetClientRect(hwnd, &clientRc);
    if (tp->sharedPianoGridMenuOpen)
    {
        RECT oldRc = tp->sharedPianoGridMenuRc;
        CloseSharedPianoGridMenu(tp);
        InvalidateRect(hwnd, &oldRc, FALSE);
        if (PtInRect(&tp->pianoRollRcGridButton, pt))
            return true;
    }
    OpenSharedPianoGridMenu(tp, tp->pianoRollRcGridButton, clientRc);
    InvalidateRect(hwnd, &tp->sharedPianoGridMenuRc, FALSE);
    return true;
}

static void LayoutEmbeddedPianoSpecControls(ThreadParam* tp, const RECT& pianoRc)
{
    if (!tp)
        return;

    SetRectEmpty(&tp->embeddedPianoSpecRcProcessedToggle);
    SetRectEmpty(&tp->embeddedPianoSpecRcDbButton);
    SetRectEmpty(&tp->embeddedPianoSpecRcResButton);
    SetRectEmpty(&tp->embeddedPianoSpecRcGridButton);
    if (pianoRc.right <= pianoRc.left || pianoRc.bottom <= pianoRc.top)
        return;

    constexpr int kPad = 6;
    constexpr int kH = 20;
    constexpr int kProcW = 118;
    constexpr int kBtnW = 86;
    constexpr int kGridW = 108;
    const int y = pianoRc.top + 6;
    int x = pianoRc.right - kPad;

    tp->embeddedPianoSpecRcProcessedToggle = RECT{ (LONG)(x - kProcW), (LONG)y, (LONG)x, (LONG)(y + kH) };
    x -= kProcW + kPad;
    tp->embeddedPianoSpecRcResButton = RECT{ (LONG)(x - kBtnW), (LONG)y, (LONG)x, (LONG)(y + kH) };
    x -= kBtnW + kPad;
    tp->embeddedPianoSpecRcDbButton = RECT{ (LONG)(x - kBtnW), (LONG)y, (LONG)x, (LONG)(y + kH) };
    x -= kBtnW + kPad;
    tp->embeddedPianoSpecRcGridButton = RECT{ (LONG)(x - kGridW), (LONG)y, (LONG)x, (LONG)(y + kH) };

    if (tp->embeddedPianoSpecRcGridButton.left < pianoRc.left + 100)
    {
        SetRectEmpty(&tp->embeddedPianoSpecRcProcessedToggle);
        SetRectEmpty(&tp->embeddedPianoSpecRcDbButton);
        SetRectEmpty(&tp->embeddedPianoSpecRcResButton);
        SetRectEmpty(&tp->embeddedPianoSpecRcGridButton);
    }
}

static void DrawEmbeddedPianoSpecButton(HDC hdc, const RECT& rc, const std::wstring& text)
{
    if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH bg = CreateSolidBrush(RGB(28, 30, 36));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(88, 92, 102));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(218, 222, 228));
    RECT tr = rc;
    DrawTextW(hdc, text.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

static void DrawEmbeddedPianoSpecCheckbox(HDC hdc, const RECT& rc, bool checked, const wchar_t* label)
{
    if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH bg = CreateSolidBrush(RGB(28, 30, 36));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(88, 92, 102));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    RECT cb{ rc.left + 4, rc.top + 4, rc.left + 16, rc.top + 16 };
    HBRUSH cbBg = CreateSolidBrush(RGB(14, 16, 20));
    FillRect(hdc, &cb, cbBg);
    DeleteObject(cbBg);
    Rectangle(hdc, cb.left, cb.top, cb.right, cb.bottom);
    if (checked)
    {
        HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(80, 210, 220));
        HGDIOBJ prevPen = SelectObject(hdc, tickPen);
        MoveToEx(hdc, cb.left + 2, cb.top + 6, NULL);
        LineTo(hdc, cb.left + 5, cb.bottom - 2);
        LineTo(hdc, cb.right - 2, cb.top + 3);
        SelectObject(hdc, prevPen);
        DeleteObject(tickPen);
    }

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, checked ? RGB(225, 245, 248) : RGB(190, 194, 200));
    RECT tr = rc;
    tr.left = cb.right + 6;
    DrawTextW(hdc, label ? label : L"", -1, &tr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static bool HandleEmbeddedPianoSpecClick(HWND hwnd, ThreadParam* tp, const RECT& pianoRc, POINT pt)
{
    if (!tp || !hwnd)
        return false;
    if (tp->activePianoRollTab != kPianoSpectrogramTabIndex)
        return false;

    LayoutEmbeddedPianoSpecControls(tp, pianoRc);
    {
        const RECT leftRc = ComputeEmbeddedPianoSpecLeftScaleRect(pianoRc);
        const RECT sliderTrackRc = ComputeEmbeddedPianoSpecMidiSliderTrackRect(leftRc);
        if (sliderTrackRc.right > sliderTrackRc.left && sliderTrackRc.bottom > sliderTrackRc.top &&
            PtInRect(&sliderTrackRc, pt))
        {
            tp->embeddedPianoSpecMidiSliderDragActive = true;
            SetCapture(hwnd);
            if (SetEmbeddedPianoSpecMidiSliderFromY(tp, sliderTrackRc, pt.y))
                InvalidateWaveRegion(hwnd, tp);
            else
                InvalidateRect(hwnd, &leftRc, FALSE);
            return true;
        }
    }
    if (tp->embeddedPianoSpecRcProcessedToggle.right > tp->embeddedPianoSpecRcProcessedToggle.left &&
        PtInRect(&tp->embeddedPianoSpecRcProcessedToggle, pt))
    {
        tp->embeddedPianoSpecUseProcessedMix = !tp->embeddedPianoSpecUseProcessedMix;
        InvalidateEmbeddedPianoSpec(tp);
        InvalidateWaveRegion(hwnd, tp);
        return true;
    }
    if (tp->embeddedPianoSpecRcDbButton.right > tp->embeddedPianoSpecRcDbButton.left &&
        PtInRect(&tp->embeddedPianoSpecRcDbButton, pt))
    {
        int idx = 0;
        for (int i = 0; i < (int)(sizeof(kEmbeddedPianoSpecDbRanges) / sizeof(kEmbeddedPianoSpecDbRanges[0])); ++i)
        {
            if (std::fabs(tp->embeddedPianoSpecDbRange - kEmbeddedPianoSpecDbRanges[i]) < 1e-6)
            {
                idx = i;
                break;
            }
        }
        idx = (idx + 1) % (int)(sizeof(kEmbeddedPianoSpecDbRanges) / sizeof(kEmbeddedPianoSpecDbRanges[0]));
        tp->embeddedPianoSpecDbRange = kEmbeddedPianoSpecDbRanges[idx];
        InvalidateEmbeddedPianoSpec(tp);
        InvalidateWaveRegion(hwnd, tp);
        return true;
    }
    if (tp->embeddedPianoSpecRcResButton.right > tp->embeddedPianoSpecRcResButton.left &&
        PtInRect(&tp->embeddedPianoSpecRcResButton, pt))
    {
        tp->embeddedPianoSpecNfftChoiceIndex =
            (tp->embeddedPianoSpecNfftChoiceIndex + 1) %
            (int)(sizeof(kEmbeddedPianoSpecNfftChoices) / sizeof(kEmbeddedPianoSpecNfftChoices[0]));
        tp->embeddedPianoSpecNfft = kEmbeddedPianoSpecNfftChoices[tp->embeddedPianoSpecNfftChoiceIndex];
        InvalidateEmbeddedPianoSpec(tp);
        InvalidateWaveRegion(hwnd, tp);
        return true;
    }
    if (tp->embeddedPianoSpecRcGridButton.right > tp->embeddedPianoSpecRcGridButton.left &&
        PtInRect(&tp->embeddedPianoSpecRcGridButton, pt))
    {
        RECT clientRc{}; GetClientRect(hwnd, &clientRc);
        if (tp->sharedPianoGridMenuOpen)
        {
            RECT oldRc = tp->sharedPianoGridMenuRc;
            CloseSharedPianoGridMenu(tp);
            InvalidateRect(hwnd, &oldRc, FALSE);
            return true;
        }
        OpenSharedPianoGridMenu(tp, tp->embeddedPianoSpecRcGridButton, clientRc);
        InvalidateRect(hwnd, &tp->sharedPianoGridMenuRc, FALSE);
        return true;
    }
    return false;
}

static RECT ComputeEmbeddedPianoSpecPlotRect(const RECT& rc)
{
    constexpr int kLeftScaleW = 94;
    constexpr int kBottomAxisH = 24;
    RECT plot = rc;
    plot.left += kLeftScaleW;
    plot.bottom -= kBottomAxisH;
    if (plot.right < plot.left) plot.right = plot.left;
    if (plot.bottom < plot.top) plot.bottom = plot.top;
    return plot;
}

static RECT ComputeEmbeddedPianoSpecLeftScaleRect(const RECT& rc)
{
    constexpr int kLeftScaleW = 94;
    constexpr int kBottomAxisH = 24;
    RECT left = rc;
    left.right = (LONG)((std::min)((int)rc.right, (int)rc.left + kLeftScaleW));
    left.bottom -= kBottomAxisH;
    if (left.right < left.left) left.right = left.left;
    if (left.bottom < left.top) left.bottom = left.top;
    return left;
}

static RECT ComputeEmbeddedPianoSpecBottomAxisRect(const RECT& rc)
{
    constexpr int kLeftScaleW = 94;
    constexpr int kBottomAxisH = 24;
    RECT axis = rc;
    axis.left += kLeftScaleW;
    axis.top = (LONG)((std::max)((int)rc.top, (int)rc.bottom - kBottomAxisH));
    if (axis.right < axis.left) axis.right = axis.left;
    if (axis.bottom < axis.top) axis.bottom = axis.top;
    return axis;
}

static constexpr int kEmbeddedPianoSpecMidiSliderHardMin = 12;   // C0
static constexpr int kEmbeddedPianoSpecMidiSliderHardMax = 135;  // ~20 kHz @ 44.1kHz

static RECT ComputeEmbeddedPianoSpecMidiSliderTrackRect(const RECT& leftRc)
{
    return PianoSpectrogramUI::ComputeMidiSliderTrackRect(leftRc);
}

static RECT ComputeEmbeddedPianoSpecMidiSliderThumbRect(const ThreadParam* tp, const RECT& trackRc)
{
    if (!tp) return trackRc;
    return PianoSpectrogramUI::ComputeMidiSliderThumbRect(
        trackRc,
        tp->embeddedPianoSpecMidiMin,
        tp->embeddedPianoSpecMidiMax,
        kEmbeddedPianoSpecMidiSliderHardMin,
        kEmbeddedPianoSpecMidiSliderHardMax);
}

static bool SetEmbeddedPianoSpecMidiSliderFromY(ThreadParam* tp, const RECT& trackRc, int y)
{
    if (!tp) return false;
    int newMin = tp->embeddedPianoSpecMidiMin;
    int newMax = tp->embeddedPianoSpecMidiMax;
    if (!PianoSpectrogramUI::ComputeMidiSliderWindowFromY(
        trackRc, y,
        tp->embeddedPianoSpecMidiMin, tp->embeddedPianoSpecMidiMax,
        newMin, newMax,
        kEmbeddedPianoSpecMidiSliderHardMin, kEmbeddedPianoSpecMidiSliderHardMax))
        return false;

    if (newMin == tp->embeddedPianoSpecMidiMin && newMax == tp->embeddedPianoSpecMidiMax)
        return false;

    tp->embeddedPianoSpecMidiMin = newMin;
    tp->embeddedPianoSpecMidiMax = newMax;
    InvalidateEmbeddedPianoSpec(tp);
    return true;
}

static bool EmbeddedPianoSpecIsBlackKey(int midi)
{
    return PianoSpectrogramUI::IsBlackKey(midi);
}

static std::wstring EmbeddedPianoSpecNoteName(int midi)
{
    return PianoSpectrogramUI::NoteName(midi);
}

static double EmbeddedPianoSpecMidiToFreq(double midi)
{
    return PianoSpectrogramUI::MidiToFreq(midi);
}

static double EmbeddedPianoSpecFreqToMidi(double hz)
{
    return PianoSpectrogramUI::FreqToMidi(hz);
}

static uint32_t EmbeddedPianoSpecHeatColor(double db, double dbRange)
{
    return PianoSpectrogramUI::HeatColor(db, dbRange);
}

static void EmbeddedPianoSpecEnsureFftReady(ThreadParam* tp)
{
    if (!tp) return;
    if (tp->embeddedPianoSpecNfft < 256) tp->embeddedPianoSpecNfft = 256;
    if (tp->embeddedPianoSpecFft.nfft() == tp->embeddedPianoSpecNfft &&
        (int)tp->embeddedPianoSpecWindow.size() == tp->embeddedPianoSpecNfft)
        return;

    tp->embeddedPianoSpecFft.init(tp->embeddedPianoSpecNfft);
    tp->embeddedPianoSpecWindow.assign((std::size_t)tp->embeddedPianoSpecNfft, 0.0);
    double winSum = 0.0;
    for (int i = 0; i < tp->embeddedPianoSpecNfft; ++i)
    {
        tp->embeddedPianoSpecWindow[(std::size_t)i] = dsp::hann(i, tp->embeddedPianoSpecNfft);
        winSum += tp->embeddedPianoSpecWindow[(std::size_t)i];
    }
    tp->embeddedPianoSpecAnalysisMono.assign((std::size_t)tp->embeddedPianoSpecNfft, 0.0);
    tp->embeddedPianoSpecRowDbScratch.clear();
    tp->embeddedPianoSpecFftAmpScale = (winSum > 1e-12)
        ? (2.0 / winSum)
        : (1.0 / (double)(std::max)(1, tp->embeddedPianoSpecNfft));
    InvalidateEmbeddedPianoSpec(tp);
}

static inline double EmbeddedPianoSpecSampleMonoAtFrame(const ThreadParam* tp, long long frame)
{
    if (!tp || !tp->samples || frame < 0) return 0.0;
    const std::size_t idx = static_cast<std::size_t>(frame);
    if (tp->isStereo)
    {
        const std::size_t base = idx * 2;
        if (base + 1 >= tp->samples->size()) return 0.0;
        const int l = (*tp->samples)[base];
        const int r = (*tp->samples)[base + 1];
        return ((double)l + (double)r) * (0.5 / 32768.0);
    }
    if (idx >= tp->samples->size()) return 0.0;
    return (double)(*tp->samples)[idx] / 32768.0;
}

static bool EmbeddedPianoSpecFillAnalysisMonoWindow(ThreadParam* tp, double centerFrameD)
{
    if (!tp) return false;
    EmbeddedPianoSpecEnsureFftReady(tp);
    const int nfft = tp->embeddedPianoSpecFft.nfft();
    if (nfft <= 0) return false;
    if ((int)tp->embeddedPianoSpecAnalysisMono.size() != nfft)
        tp->embeddedPianoSpecAnalysisMono.assign((std::size_t)nfft, 0.0);

    bool usedProcessed = false;
    if (tp->embeddedPianoSpecUseProcessedMix)
        usedProcessed = WaveformWindow::BuildPlaybackSpectrogramMonoWindow(centerFrameD, nfft, tp->embeddedPianoSpecAnalysisMono);

    if (!usedProcessed)
    {
        const long long center = (long long)std::llround(centerFrameD);
        const long long half = nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const long long src = center + i - half;
            tp->embeddedPianoSpecAnalysisMono[(std::size_t)i] = EmbeddedPianoSpecSampleMonoAtFrame(tp, src);
        }
    }
    return true;
}

static void EmbeddedPianoSpecComputeColumn(ThreadParam* tp, double centerFrameD, int plotH, std::vector<uint32_t>& outCol)
{
    if (!tp || plotH <= 0 || tp->sampleRate <= 0)
    {
        outCol.assign((std::size_t)(std::max)(0, plotH), EmbeddedPianoSpecHeatColor(-84.0, 84.0));
        return;
    }

    outCol.assign((std::size_t)plotH, EmbeddedPianoSpecHeatColor(-tp->embeddedPianoSpecDbRange, tp->embeddedPianoSpecDbRange));

    EmbeddedPianoSpecEnsureFftReady(tp);
    if (tp->embeddedPianoSpecFft.nfft() <= 0 || !EmbeddedPianoSpecFillAnalysisMonoWindow(tp, centerFrameD))
        return;

    const int nfft = tp->embeddedPianoSpecFft.nfft();
    if ((int)tp->embeddedPianoSpecRowDbScratch.size() != plotH)
        tp->embeddedPianoSpecRowDbScratch.assign((std::size_t)plotH, -1e12);
    else
        std::fill(tp->embeddedPianoSpecRowDbScratch.begin(), tp->embeddedPianoSpecRowDbScratch.end(), -1e12);

    double mean = 0.0;
    for (int i = 0; i < nfft; ++i)
        mean += tp->embeddedPianoSpecAnalysisMono[(std::size_t)i];
    mean /= (double)nfft;

    for (int i = 0; i < nfft; ++i)
        tp->embeddedPianoSpecFft.in()[i] =
        (tp->embeddedPianoSpecAnalysisMono[(std::size_t)i] - mean) * tp->embeddedPianoSpecWindow[(std::size_t)i];
    tp->embeddedPianoSpecFft.execute();

    const fftw_complex* X = tp->embeddedPianoSpecFft.out();
    if (!X) return;

    const double nyquist = 0.5 * (double)tp->sampleRate;
    const double fMin = std::max(10.0, tp->embeddedPianoSpecMinFreqHz);
    const double fMaxByMidi = EmbeddedPianoSpecMidiToFreq((double)tp->embeddedPianoSpecMidiMax + 0.5);
    const double fMax = std::clamp(fMaxByMidi, fMin + 1.0, nyquist);
    const double hzPerBin = (double)tp->sampleRate / (double)nfft;
    const double midiSpan = (double)(std::max)(1, tp->embeddedPianoSpecMidiMax - tp->embeddedPianoSpecMidiMin);

    auto rowForMidi = [&](double midi) -> int
    {
        const double yNorm = ((double)tp->embeddedPianoSpecMidiMax - midi) / midiSpan;
        int y = (int)std::lround(yNorm * (double)(plotH - 1));
        return (std::max)(0, (std::min)(plotH - 1, y));
    };

    const int kMax = nfft / 2;
    for (int k = 1; k <= kMax; ++k)
    {
        const double f = (double)k * hzPerBin;
        if (f < fMin) continue;
        if (f > fMax) break;

        const double midi = EmbeddedPianoSpecFreqToMidi(f);
        if (!std::isfinite(midi) ||
            midi < (double)tp->embeddedPianoSpecMidiMin - 0.5 ||
            midi > (double)tp->embeddedPianoSpecMidiMax + 0.5)
            continue;

        const double re = X[k][0];
        const double im = X[k][1];
        const double mag = std::sqrt(re * re + im * im);
        const double amp = mag * tp->embeddedPianoSpecFftAmpScale;
        double db = 20.0 * std::log10(amp + 1e-12);
        if (!std::isfinite(db)) db = -300.0;

        const int y = rowForMidi(midi);
        tp->embeddedPianoSpecRowDbScratch[(std::size_t)y] =
            (std::max)(tp->embeddedPianoSpecRowDbScratch[(std::size_t)y], db);
        if (y > 0)
            tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y - 1)] =
            (std::max)(tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y - 1)], db - 2.0);
        if (y + 1 < plotH)
            tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y + 1)] =
            (std::max)(tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y + 1)], db - 2.0);
    }

    for (int y = 1; y + 1 < plotH; ++y)
    {
        const double d0 = tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y - 1)] - 3.0;
        const double d1 = tp->embeddedPianoSpecRowDbScratch[(std::size_t)y];
        const double d2 = tp->embeddedPianoSpecRowDbScratch[(std::size_t)(y + 1)] - 3.0;
        tp->embeddedPianoSpecRowDbScratch[(std::size_t)y] = (std::max)(d1, (std::max)(d0, d2));
    }

    for (int y = 0; y < plotH; ++y)
        outCol[(std::size_t)y] = EmbeddedPianoSpecHeatColor(tp->embeddedPianoSpecRowDbScratch[(std::size_t)y], tp->embeddedPianoSpecDbRange);
}

static void EmbeddedPianoSpecEnsureImage(ThreadParam* tp, int w, int h)
{
    if (!tp) return;
    w = (std::max)(0, w);
    h = (std::max)(0, h);
    if (tp->embeddedPianoSpecImageW == w && tp->embeddedPianoSpecImageH == h &&
        (int)tp->embeddedPianoSpecImageBgra.size() == w * h)
        return;

    tp->embeddedPianoSpecImageW = w;
    tp->embeddedPianoSpecImageH = h;
    tp->embeddedPianoSpecImageBgra.assign((std::size_t)w * (std::size_t)h,
        EmbeddedPianoSpecHeatColor(-tp->embeddedPianoSpecDbRange, tp->embeddedPianoSpecDbRange));
    tp->embeddedPianoSpecCacheTLeft = std::numeric_limits<double>::quiet_NaN();
    tp->embeddedPianoSpecCacheTRight = std::numeric_limits<double>::quiet_NaN();
    tp->embeddedPianoSpecDirty = true;
}

static void EmbeddedPianoSpecClearImage(ThreadParam* tp)
{
    if (!tp || tp->embeddedPianoSpecImageW <= 0 || tp->embeddedPianoSpecImageH <= 0) return;
    std::fill(tp->embeddedPianoSpecImageBgra.begin(), tp->embeddedPianoSpecImageBgra.end(),
        EmbeddedPianoSpecHeatColor(-tp->embeddedPianoSpecDbRange, tp->embeddedPianoSpecDbRange));
}

static void EmbeddedPianoSpecShiftImageLeft(ThreadParam* tp, int cols)
{
    if (!tp || tp->embeddedPianoSpecImageW <= 0 || tp->embeddedPianoSpecImageH <= 0 || cols <= 0) return;
    if (cols >= tp->embeddedPianoSpecImageW)
    {
        EmbeddedPianoSpecClearImage(tp);
        return;
    }

    const int w = tp->embeddedPianoSpecImageW;
    const int h = tp->embeddedPianoSpecImageH;
    const uint32_t bg = EmbeddedPianoSpecHeatColor(-tp->embeddedPianoSpecDbRange, tp->embeddedPianoSpecDbRange);
    for (int y = 0; y < h; ++y)
    {
        uint32_t* row = tp->embeddedPianoSpecImageBgra.data() + (std::size_t)y * (std::size_t)w;
        std::memmove(row, row + cols, (std::size_t)(w - cols) * sizeof(uint32_t));
        std::fill(row + (w - cols), row + w, bg);
    }
}

static void EmbeddedPianoSpecWriteColumn(ThreadParam* tp, int x, const std::vector<uint32_t>& col)
{
    if (!tp || tp->embeddedPianoSpecImageW <= 0 || tp->embeddedPianoSpecImageH <= 0) return;
    if (x < 0 || x >= tp->embeddedPianoSpecImageW) return;
    if ((int)col.size() < tp->embeddedPianoSpecImageH) return;
    for (int y = 0; y < tp->embeddedPianoSpecImageH; ++y)
    {
        tp->embeddedPianoSpecImageBgra[(std::size_t)y * (std::size_t)tp->embeddedPianoSpecImageW + (std::size_t)x] =
            col[(std::size_t)y];
    }
}

static void RebuildEmbeddedPianoSpecViewport(ThreadParam* tp, double tLeft, double tRight, int plotW, int plotH)
{
    if (!tp) return;
    EmbeddedPianoSpecEnsureImage(tp, plotW, plotH);
    if (plotW <= 0 || plotH <= 0) return;

    EmbeddedPianoSpecClearImage(tp);
    if (!std::isfinite(tLeft) || !std::isfinite(tRight) || !(tRight > tLeft) || tp->sampleRate <= 0)
    {
        tp->embeddedPianoSpecCacheTLeft = std::numeric_limits<double>::quiet_NaN();
        tp->embeddedPianoSpecCacheTRight = std::numeric_limits<double>::quiet_NaN();
        tp->embeddedPianoSpecDirty = false;
        return;
    }

    const double secPerCol = (tRight - tLeft) / (double)(std::max)(1, plotW);
    std::vector<uint32_t> col;
    col.reserve((std::size_t)plotH);
    for (int x = 0; x < plotW; ++x)
    {
        const double t = tLeft + (static_cast<double>(x) + 0.5) * secPerCol;
        EmbeddedPianoSpecComputeColumn(tp, t * (double)tp->sampleRate, plotH, col);
        EmbeddedPianoSpecWriteColumn(tp, x, col);
    }

    tp->embeddedPianoSpecCacheTLeft = tLeft;
    tp->embeddedPianoSpecCacheTRight = tRight;
    tp->embeddedPianoSpecDirty = false;
}

static void UpdateEmbeddedPianoSpecViewport(ThreadParam* tp, double tLeft, double tRight, int plotW, int plotH, bool allowHeavyWork)
{
    if (!tp || plotW <= 0 || plotH <= 0) return;
    if (!std::isfinite(tLeft) || !std::isfinite(tRight) || !(tRight > tLeft)) return;
    if (tp->sampleRate <= 0) return;

    if (!allowHeavyWork)
        return;

    if (tp->embeddedPianoSpecDirty ||
        tp->embeddedPianoSpecImageW != plotW ||
        tp->embeddedPianoSpecImageH != plotH ||
        !std::isfinite(tp->embeddedPianoSpecCacheTLeft) ||
        !std::isfinite(tp->embeddedPianoSpecCacheTRight))
    {
        RebuildEmbeddedPianoSpecViewport(tp, tLeft, tRight, plotW, plotH);
        return;
    }

    const double span = tRight - tLeft;
    const double cacheSpan = tp->embeddedPianoSpecCacheTRight - tp->embeddedPianoSpecCacheTLeft;
    const double secPerCol = span / (double)(std::max)(1, plotW);
    if (!(secPerCol > 0.0) || !std::isfinite(secPerCol))
        return;

    if (!std::isfinite(cacheSpan) || std::fabs(cacheSpan - span) > secPerCol * 0.5)
    {
        RebuildEmbeddedPianoSpecViewport(tp, tLeft, tRight, plotW, plotH);
        return;
    }

    const double dLeft = tLeft - tp->embeddedPianoSpecCacheTLeft;
    const double dRight = tRight - tp->embeddedPianoSpecCacheTRight;
    if (std::fabs(dLeft - dRight) > secPerCol * 0.5)
    {
        RebuildEmbeddedPianoSpecViewport(tp, tLeft, tRight, plotW, plotH);
        return;
    }

    // View moved backwards (pan/scrub) or changed abruptly: rebuild.
    if (dLeft < -secPerCol * 0.5)
    {
        RebuildEmbeddedPianoSpecViewport(tp, tLeft, tRight, plotW, plotH);
        return;
    }

    int colsToAdvance = (int)std::floor(dLeft / secPerCol);
    if (colsToAdvance <= 0)
        return;
    if (colsToAdvance >= plotW)
    {
        RebuildEmbeddedPianoSpecViewport(tp, tLeft, tRight, plotW, plotH);
        return;
    }

    EmbeddedPianoSpecShiftImageLeft(tp, colsToAdvance);

    const double quantizedTLeft = tp->embeddedPianoSpecCacheTLeft + (double)colsToAdvance * secPerCol;
    std::vector<uint32_t> col;
    col.reserve((std::size_t)plotH);
    for (int i = 0; i < colsToAdvance; ++i)
    {
        const int x = plotW - colsToAdvance + i;
        const double t = quantizedTLeft + (static_cast<double>(x) + 0.5) * secPerCol;
        EmbeddedPianoSpecComputeColumn(tp, t * (double)tp->sampleRate, plotH, col);
        EmbeddedPianoSpecWriteColumn(tp, x, col);
    }

    tp->embeddedPianoSpecCacheTLeft = quantizedTLeft;
    tp->embeddedPianoSpecCacheTRight = quantizedTLeft + span;
}

static void DrawEmbeddedPianoSpecImage(HDC hdc, const RECT& plotRc, const ThreadParam* tp)
{
    if (!hdc || !tp) return;
    if (tp->embeddedPianoSpecImageW <= 0 || tp->embeddedPianoSpecImageH <= 0 || tp->embeddedPianoSpecImageBgra.empty())
        return;

    const int dstW = (std::max)(0, (int)(plotRc.right - plotRc.left));
    const int dstH = (std::max)(0, (int)(plotRc.bottom - plotRc.top));
    if (dstW <= 0 || dstH <= 0) return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tp->embeddedPianoSpecImageW;
    bmi.bmiHeader.biHeight = -tp->embeddedPianoSpecImageH; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        plotRc.left, plotRc.top, dstW, dstH,
        0, 0, tp->embeddedPianoSpecImageW, tp->embeddedPianoSpecImageH,
        tp->embeddedPianoSpecImageBgra.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);
}

static void DrawEmbeddedPianoSpecGridAndLabels(HDC hdc, const RECT& pianoRc, ThreadParam* tp,
    double reqTLeft, double reqTRight, int playheadX)
{
    if (!hdc || !tp) return;

    constexpr int kKeyStripW = 18;
    LayoutEmbeddedPianoSpecControls(tp, pianoRc);
    const RECT plotRc = ComputeEmbeddedPianoSpecPlotRect(pianoRc);
    const RECT leftRc = ComputeEmbeddedPianoSpecLeftScaleRect(pianoRc);
    const RECT timeRc = ComputeEmbeddedPianoSpecBottomAxisRect(pianoRc);
    if (plotRc.right <= plotRc.left || plotRc.bottom <= plotRc.top) return;

    const double axisTLeft = std::isfinite(tp->embeddedPianoSpecCacheTLeft) ? tp->embeddedPianoSpecCacheTLeft : reqTLeft;
    const double axisTRight = std::isfinite(tp->embeddedPianoSpecCacheTRight) ? tp->embeddedPianoSpecCacheTRight : reqTRight;
    const double visibleSeconds = (axisTRight > axisTLeft) ? (axisTRight - axisTLeft) : (reqTRight - reqTLeft);
    if (!(visibleSeconds > 0.0) || !std::isfinite(visibleSeconds)) return;

    const int plotW = (int)(plotRc.right - plotRc.left);
    const int plotH = (int)(plotRc.bottom - plotRc.top);
    const int midiMin = (std::min)(tp->embeddedPianoSpecMidiMin, tp->embeddedPianoSpecMidiMax);
    const int midiMax = (std::max)(tp->embeddedPianoSpecMidiMin, tp->embeddedPianoSpecMidiMax);
    const int noteCount = (std::max)(1, midiMax - midiMin + 1);
    const double rowH = (double)plotH / (double)noteCount;

    RECT keyStrip = leftRc;
    keyStrip.right = (LONG)((std::min)((int)leftRc.right, (int)leftRc.left + kKeyStripW));
    RECT labelRc = leftRc;
    labelRc.left = keyStrip.right + 2;
    const RECT midiSliderTrackRc = ComputeEmbeddedPianoSpecMidiSliderTrackRect(leftRc);
    if (midiSliderTrackRc.left > labelRc.left)
        labelRc.right = (LONG)((std::max)((int)labelRc.left, (int)midiSliderTrackRc.left - 2));

    SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(34, 38, 44));
    HPEN hStrong = CreatePen(PS_SOLID, 1, RGB(56, 60, 68));
    HPEN timePen = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
    HPEN timeStrong = CreatePen(PS_SOLID, 1, RGB(58, 64, 76));
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(76, 80, 90));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    for (int idx = 0; idx < noteCount; ++idx)
    {
        const int midi = midiMax - idx;
        const int y0 = plotRc.top + (int)std::floor((double)idx * rowH);
        const int y1 = plotRc.top + (int)std::floor((double)(idx + 1) * rowH);
        const RECT rowKey{ keyStrip.left, (LONG)y0, keyStrip.right, (LONG)(std::max)(y0 + 1, y1) };
        HBRUSH rowBr = CreateSolidBrush(EmbeddedPianoSpecIsBlackKey(midi) ? RGB(26, 30, 36) : RGB(36, 40, 46));
        FillRect(hdc, &rowKey, rowBr);
        DeleteObject(rowBr);

        const bool cNote = (((midi % 12) + 12) % 12) == 0;
        SelectObject(hdc, cNote ? hStrong : hPen);
        MoveToEx(hdc, plotRc.left, y0, NULL);
        LineTo(hdc, plotRc.right, y0);
        MoveToEx(hdc, leftRc.left, y0, NULL);
        LineTo(hdc, leftRc.right, y0);

        if (cNote || rowH >= 14.0)
        {
            std::wstring nm = EmbeddedPianoSpecNoteName(midi);
            const double hz = EmbeddedPianoSpecMidiToFreq((double)midi);
            wchar_t lbl[48];
            if (cNote)
                swprintf_s(lbl, L"%s %.0fHz", nm.c_str(), hz);
            else
                swprintf_s(lbl, L"%s", nm.c_str());
            SetTextColor(hdc, cNote ? RGB(230, 232, 236) : RGB(170, 174, 180));
            RECT lr{ labelRc.left + 2, (LONG)y0, leftRc.right - 2, (LONG)(std::max)(y0 + 12, y1) };
            DrawTextW(hdc, lbl, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    }

    const double tickStep = ChooseTimeTickStep(visibleSeconds);
    const double firstTick = std::floor(axisTLeft / tickStep) * tickStep;
    for (double t = firstTick; t <= axisTRight + tickStep * 0.5; t += tickStep)
    {
        if (t < axisTLeft - 1e-9 || t < 0.0) continue;
        const double xn = (t - axisTLeft) / visibleSeconds;
        int x = plotRc.left + (int)std::lround(xn * (double)(plotW - 1));
        x = (std::max)((int)plotRc.left, (std::min)(x, (int)plotRc.right - 1));
        const bool strong = (tickStep >= 5.0) || (std::fabs(std::fmod(tickStep > 0.0 ? t / tickStep : 0.0, 5.0)) < 1e-6);
        SelectObject(hdc, strong ? timeStrong : timePen);
        MoveToEx(hdc, x, timeRc.top, NULL);
        LineTo(hdc, x, timeRc.top + 7);
    }

    DrawEmbeddedPianoSpectrogramMusicalGrid(hdc, plotRc, timeRc, tp, axisTLeft, axisTRight);

    // Time marks use tick marks only in the plot (no full-height grey time grid lines),
    // but keep bottom-axis timestamp labels for readout.
    SetTextColor(hdc, RGB(185, 190, 198));
    const bool showMillis = tickStep < 1.0;
    for (double t = firstTick; t <= axisTRight + tickStep * 0.5; t += tickStep)
    {
        if (t < axisTLeft - 1e-9 || t < 0.0) continue;
        const double xn = (t - axisTLeft) / visibleSeconds;
        int x = timeRc.left + (int)std::lround(xn * (double)(((int)timeRc.right - (int)timeRc.left) - 1));
        x = (std::max)((int)timeRc.left, (std::min)(x, (int)timeRc.right - 1));
        std::wstring lbl = FormatTimeLabel(t, showMillis);
        RECT tr{ (LONG)(x + 2), timeRc.top + 7, timeRc.right - 2, timeRc.bottom - 2 };
        DrawTextW(hdc, lbl.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    MoveToEx(hdc, leftRc.right - 1, leftRc.top, NULL);
    LineTo(hdc, leftRc.right - 1, leftRc.bottom);
    MoveToEx(hdc, plotRc.left, timeRc.top, NULL);
    LineTo(hdc, plotRc.right, timeRc.top);
    Rectangle(hdc, plotRc.left, plotRc.top, plotRc.right, plotRc.bottom);
    Rectangle(hdc, leftRc.left, leftRc.top, leftRc.right, leftRc.bottom);
    Rectangle(hdc, timeRc.left, timeRc.top, timeRc.right, timeRc.bottom);
    SelectObject(hdc, oldBrush);

    // Match the waveform playhead x-position so the two white lines stay visually aligned.
    if (playheadX >= plotRc.left && playheadX < plotRc.right)
    {
        HPEN playPen = CreatePen(PS_SOLID, 2, RGB(248, 248, 248));
        HGDIOBJ prevPlayPen = SelectObject(hdc, playPen);
        MoveToEx(hdc, playheadX, plotRc.top + 1, NULL);
        LineTo(hdc, playheadX, plotRc.bottom - 1);
        MoveToEx(hdc, playheadX, timeRc.top + 1, NULL);
        LineTo(hdc, playheadX, timeRc.bottom - 1);
        SelectObject(hdc, prevPlayPen);
        DeleteObject(playPen);
    }

    wchar_t hud[256];
    swprintf_s(hud, L"Spec %s  FFT %d  dB %.0f%s",
        tp->embeddedPianoSpecUseProcessedMix ? L"PROC" : L"RAW",
        tp->embeddedPianoSpecNfft,
        tp->embeddedPianoSpecDbRange,
        tp->liveResizeActive ? L"  (resizing... cache hold)" : L"");
    SetTextColor(hdc, RGB(210, 214, 220));
    RECT hudRc{ leftRc.left + 4, leftRc.top + 2, plotRc.right - 4, leftRc.top + 18 };
    DrawTextW(hdc, hud, -1, &hudRc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT hintRc{ leftRc.left + 4, leftRc.top + 18, plotRc.right - 4, leftRc.top + 34 };
    SetTextColor(hdc, RGB(150, 156, 166));
    DrawTextW(hdc, L"Drag the Spec tab out to pop out", -1, &hintRc,
        DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    wchar_t dbLabel[64];
    swprintf_s(dbLabel, L"dB %.0f", tp->embeddedPianoSpecDbRange);
    DrawEmbeddedPianoSpecButton(hdc, tp->embeddedPianoSpecRcDbButton, dbLabel);

    wchar_t resLabel[64];
    swprintf_s(resLabel, L"Res %d", tp->embeddedPianoSpecNfft);
    DrawEmbeddedPianoSpecButton(hdc, tp->embeddedPianoSpecRcResButton, resLabel);

    wchar_t gridLabel[96];
    swprintf_s(gridLabel, L"Grid %s", EmbeddedPianoGridModeLabel(WaveformWindow::GetSharedPianoGridMode()));
    DrawEmbeddedPianoSpecButton(hdc, tp->embeddedPianoSpecRcGridButton, gridLabel);

    DrawEmbeddedPianoSpecCheckbox(hdc, tp->embeddedPianoSpecRcProcessedToggle,
        tp->embeddedPianoSpecUseProcessedMix, L"PROC");

    if (midiSliderTrackRc.right > midiSliderTrackRc.left && midiSliderTrackRc.bottom > midiSliderTrackRc.top)
    {
        RECT sliderFill = midiSliderTrackRc;
        HBRUSH trackBg = CreateSolidBrush(RGB(18, 21, 27));
        FillRect(hdc, &sliderFill, trackBg);
        DeleteObject(trackBg);

        RECT thumbRc = ComputeEmbeddedPianoSpecMidiSliderThumbRect(tp, midiSliderTrackRc);
        HBRUSH thumbBg = CreateSolidBrush(tp->embeddedPianoSpecMidiSliderDragActive ? RGB(120, 170, 210) : RGB(92, 116, 144));
        FillRect(hdc, &thumbRc, thumbBg);
        DeleteObject(thumbBg);

        HPEN sliderPen = CreatePen(PS_SOLID, 1, RGB(86, 92, 104));
        HGDIOBJ prevSliderPen = SelectObject(hdc, sliderPen);
        HGDIOBJ prevSliderBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, midiSliderTrackRc.left, midiSliderTrackRc.top, midiSliderTrackRc.right, midiSliderTrackRc.bottom);
        Rectangle(hdc, thumbRc.left, thumbRc.top, thumbRc.right, thumbRc.bottom);
        SelectObject(hdc, prevSliderBrush);
        SelectObject(hdc, prevSliderPen);
        DeleteObject(sliderPen);
    }

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldFont);
    DeleteObject(hPen);
    DeleteObject(hStrong);
    DeleteObject(timePen);
    DeleteObject(timeStrong);
    DeleteObject(borderPen);
}

static void DrawEmbeddedPianoSpectrogramTab(HDC hdc, const RECT& pianoRc, ThreadParam* tp,
    double tLeft, double tRight, double /*curSeconds*/, int playheadX)
{
    if (!hdc || !tp) return;
    if (pianoRc.right <= pianoRc.left || pianoRc.bottom <= pianoRc.top) return;

    const RECT plotRc = ComputeEmbeddedPianoSpecPlotRect(pianoRc);
    const RECT leftRc = ComputeEmbeddedPianoSpecLeftScaleRect(pianoRc);
    const RECT timeRc = ComputeEmbeddedPianoSpecBottomAxisRect(pianoRc);

    HBRUSH bg = CreateSolidBrush(RGB(16, 18, 22));
    FillRect(hdc, &pianoRc, bg);
    DeleteObject(bg);
    HBRUSH leftBg = CreateSolidBrush(RGB(22, 24, 28));
    FillRect(hdc, &leftRc, leftBg);
    DeleteObject(leftBg);
    HBRUSH plotBg = CreateSolidBrush(RGB(8, 10, 14));
    FillRect(hdc, &plotRc, plotBg);
    DeleteObject(plotBg);
    HBRUSH timeBg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &timeRc, timeBg);
    DeleteObject(timeBg);

    const int plotW = (std::max)(0, (int)(plotRc.right - plotRc.left));
    const int plotH = (std::max)(0, (int)(plotRc.bottom - plotRc.top));
    if (plotW > 0 && plotH > 0)
    {
        // The waveform timeline spans the full lower panel width, but the spectrogram plot starts
        // after the left piano-key scale. Project the waveform viewport time range into the plot's
        // x-range so spectral columns line up with the waveform/beat grid above.
        const double fullSpan = tRight - tLeft;
        const int fullW = (std::max)(1, (int)(pianoRc.right - pianoRc.left));
        const double plotLeftNorm = ((double)plotRc.left - (double)pianoRc.left) / (double)fullW;
        const double plotRightNorm = ((double)plotRc.right - (double)pianoRc.left) / (double)fullW;
        const double specTLeft = tLeft + fullSpan * plotLeftNorm;
        const double specTRight = tLeft + fullSpan * plotRightNorm;

        const bool suppressHeavySpecWork = tp->liveResizeActive || tp->dragPanActive || tp->dragScrubActive;
        UpdateEmbeddedPianoSpecViewport(tp, specTLeft, specTRight, plotW, plotH, !suppressHeavySpecWork);
        DrawEmbeddedPianoSpecImage(hdc, plotRc, tp);
        DrawEmbeddedPianoSpecGridAndLabels(hdc, pianoRc, tp, specTLeft, specTRight, playheadX);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto tp = reinterpret_cast<ThreadParam*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_CREATE:
    {
        if (tp)
        {
            tp->hBtnPlay = nullptr;
            tp->hBtnPause = nullptr;
            for (int i = 0; i < 4; ++i) tp->hBtnStem[i] = nullptr;
            tp->buttonTextRefreshPending = false;
            tp->buttonTextRefreshRetries = 0;
        }
        LayoutTopButtons(hwnd, tp);

        return 0;
    }

    case WM_SIZE:
    {
        if (tp)
        {
            tp->cacheDirty = true;
            CloseSharedPianoGridMenu(tp);
            // Rebuild the embedded spectrogram for the new viewport size after resize settles.
            InvalidateEmbeddedPianoSpec(tp);
        }
        LayoutTopButtons(hwnd, tp);
        InvalidateRect(hwnd, NULL, FALSE);
        if (!tp || !tp->liveResizeActive)
            UpdateWindow(hwnd);
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        if (tp) tp->liveResizeActive = true;
        return 0;

    case WM_EXITSIZEMOVE:
        if (tp)
        {
            tp->liveResizeActive = false;
            tp->cacheDirty = true;
            InvalidateEmbeddedPianoSpec(tp);
            InvalidateWaveRegion(hwnd, tp);
        }
        return 0;

    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (!dis || dis->CtlType != ODT_BUTTON) break;
        const int id = static_cast<int>(dis->CtlID);
        if (id != 1001 && id != 1002 && (id < kStemButtonBaseId || id >= kStemButtonBaseId + 4))
            break;

        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        const bool focus = (dis->itemState & ODS_FOCUS) != 0;
        const bool isPlay = (id == 1001);
        const bool isPause = (id == 1002);
        const bool isStem = (id >= kStemButtonBaseId && id < kStemButtonBaseId + 4);
        const int stemIdx = isStem ? (id - kStemButtonBaseId) : -1;

        const bool playState = tp && tp->playing.load();
        bool logicalOn = false;
        bool clickable = true;
        COLORREF accent = RGB(120, 120, 120);
        const wchar_t* label = L"";

        if (isPlay)
        {
            logicalOn = playState;
            accent = RGB(55, 180, 95);
            label = L"Play";
        }
        else if (isPause)
        {
            logicalOn = !playState;
            accent = RGB(230, 170, 55);
            label = L"Pause";
        }
        else
        {
            const bool hasStems = HasStemPlayback(tp);
            clickable = hasStems;
            logicalOn = hasStems && tp && tp->stemEnabled[stemIdx];
            accent = GetStemButtonColorByIndex(stemIdx);
            label = GetStemButtonLabelByIndex(stemIdx);
        }

        auto scale = [](BYTE c, double s) -> BYTE
        {
            int v = static_cast<int>(std::lround(static_cast<double>(c) * s));
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            return static_cast<BYTE>(v);
        };

        COLORREF fill = RGB(42, 42, 42);
        COLORREF border = RGB(78, 78, 78);
        COLORREF text = RGB(220, 220, 220);
        if (!clickable)
        {
            fill = RGB(28, 28, 28);
            border = RGB(55, 55, 55);
            text = RGB(110, 110, 110);
        }
        else if (logicalOn)
        {
            const double s = pressed ? 0.72 : 0.95;
            fill = RGB(scale(GetRValue(accent), s * 0.55), scale(GetGValue(accent), s * 0.55), scale(GetBValue(accent), s * 0.55));
            border = RGB(scale(GetRValue(accent), s * 0.95), scale(GetGValue(accent), s * 0.95), scale(GetBValue(accent), s * 0.95));
            text = RGB(248, 248, 248);
        }
        else
        {
            fill = pressed ? RGB(34, 34, 34) : RGB(30, 30, 30);
            border = RGB(82, 82, 82);
            text = RGB(175, 175, 175);
        }

        HBRUSH br = CreateSolidBrush(fill);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldPen = SelectObject(dis->hDC, pen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);

        // Status indicator (makes ON/OFF obvious and avoids ambiguous "highlighted opposite" look).
        RECT ir = dis->rcItem;
        ir.left += 4;
        ir.right = ir.left + 6;
        ir.top += 5;
        ir.bottom -= 5;
        HBRUSH indBr = CreateSolidBrush((clickable && logicalOn) ? accent : RGB(70, 70, 70));
        FillRect(dis->hDC, &ir, indBr);
        DeleteObject(indBr);

        if (clickable && logicalOn)
        {
            HPEN topPen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
            HGDIOBJ prevTop = SelectObject(dis->hDC, topPen);
            MoveToEx(dis->hDC, dis->rcItem.left + 1, dis->rcItem.top + 1, NULL);
            LineTo(dis->hDC, dis->rcItem.right - 1, dis->rcItem.top + 1);
            SelectObject(dis->hDC, prevTop);
            DeleteObject(topPen);
        }

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, text);
        HFONT oldFont = (HFONT)SelectObject(dis->hDC, (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        RECT tr = dis->rcItem;
        tr.left += 12;
        DrawTextW(dis->hDC, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (focus)
        {
            RECT fr = dis->rcItem;
            InflateRect(&fr, -3, -3);
            DrawFocusRect(dis->hDC, &fr);
        }

        SelectObject(dis->hDC, oldFont);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(pen);
        return TRUE;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (!tp) break;
        if (id == 1001) // Play
        {
            if (tp->playing.load())
                break;

            if (UsingAudioEngine(tp))
            {
                if (tp->audioEngine.Play())
                {
                    tp->pausedSampleIndex = tp->audioEngine.GetCurrentFrame();
                    tp->playing.store(true);
                    InvalidateToolbar(hwnd);
                    InvalidateWaveRegion(hwnd, tp);
                }
                break;
            }

            if (!tp->mciOpened && !tp->tempPath.empty())
            {
                MciOpenIfNeeded(tp);
            }
            if (!tp->mciOpened) break;

            DWORD startMs = 0;
            if (tp->pausedSampleIndex > 0)
            {
                startMs = static_cast<DWORD>((tp->pausedSampleIndex * 1000ULL) / tp->sampleRate);
            }

            std::wstring cmd;
            if (startMs > 0)
                cmd = L"play " + tp->mciAlias + L" from " + std::to_wstring(startMs);
            else
                cmd = L"play " + tp->mciAlias;

            if (mciSendStringW(cmd.c_str(), NULL, 0, NULL) != 0)
                break;

            tp->playStartTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(static_cast<long long>(startMs));
            tp->playing.store(true);
            if (HWND hPlay = GetDlgItem(hwnd, 1001)) InvalidateRect(hPlay, NULL, TRUE);
            if (HWND hPause = GetDlgItem(hwnd, 1002)) InvalidateRect(hPause, NULL, TRUE);
            InvalidateToolbar(hwnd);
            InvalidateWaveRegion(hwnd, tp);
        }
        else if (id == 1002) // Pause
        {
            if (UsingAudioEngine(tp))
            {
                tp->audioEngine.Pause();
                tp->pausedSampleIndex = tp->audioEngine.GetCurrentFrame();
                tp->playing.store(false);
                InvalidateToolbar(hwnd);
                InvalidateWaveRegion(hwnd, tp);
                break;
            }
            if (!tp->mciOpened) break;
            SyncPausedFromMciPosition(tp);

            // `pause` is unreliable for some waveaudio devices; stop + resume-from-position is robust.
            std::wstring cmd = L"stop " + tp->mciAlias;
            mciSendStringW(cmd.c_str(), NULL, 0, NULL);

            tp->playing.store(false);
            if (HWND hPlay = GetDlgItem(hwnd, 1001)) InvalidateRect(hPlay, NULL, TRUE);
            if (HWND hPause = GetDlgItem(hwnd, 1002)) InvalidateRect(hPause, NULL, TRUE);
            InvalidateToolbar(hwnd);
            InvalidateWaveRegion(hwnd, tp);
        }
        else if (id == kToolbarHalfSpeedCommandId) // 1/2x toggle
        {
            if (!UsingAudioEngine(tp))
                break;
            tp->halfSpeedPlayback = !tp->halfSpeedPlayback;
            const double rate = tp->halfSpeedPlayback ? 0.5 : 1.0;
            tp->audioEngine.SetPlaybackRate(rate);
            InvalidateEmbeddedPianoSpec(tp);
            InvalidateToolbar(hwnd);
            InvalidateWaveRegion(hwnd, tp);
        }
        else if (id >= kStemButtonBaseId && id < kStemButtonBaseId + 4)
        {
            if (!HasStemPlayback(tp))
                break;
            const int stemIdx = id - kStemButtonBaseId;
            tp->stemEnabled[stemIdx] = !tp->stemEnabled[stemIdx];
            RebuildStemPlaybackAndRetuneMci(tp);
            InvalidateEmbeddedPianoSpec(tp);
            if (HWND hb = GetDlgItem(hwnd, id))
                InvalidateRect(hb, NULL, TRUE);
            if (HWND hPlay = GetDlgItem(hwnd, 1001)) InvalidateRect(hPlay, NULL, TRUE);
            if (HWND hPause = GetDlgItem(hwnd, 1002)) InvalidateRect(hPause, NULL, TRUE);
            InvalidateToolbar(hwnd);
            InvalidateWaveRegion(hwnd, tp);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!tp) break;
        if (tp->sharedPianoGridMenuOpen)
        {
            RECT oldRc = tp->sharedPianoGridMenuRc;
            CloseSharedPianoGridMenu(tp);
            InvalidateRect(hwnd, &oldRc, FALSE);
        }
        SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        double factor = (delta > 0) ? 1.15 : 1.0 / 1.15;
        const size_t totalFrames = GetTotalFrames(tp);
        const double minZoom = GetMinZoomFactorForMaxWindow(tp, totalFrames);
        tp->zoomFactor = std::clamp(tp->zoomFactor * factor, minZoom, 256.0);
        tp->cacheDirty = true;
        InvalidateWaveRegion(hwnd, tp);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (HandleSharedPianoGridMenuMouseDown(hwnd, tp, pt))
            return 0;
        const int knobHit = HitTestToolbarKnob(tp, pt);
        if (knobHit >= 0)
        {
            tp->toolbarDragKnob = knobHit;
            tp->toolbarDragStartY = pt.y;
            tp->toolbarDragStartDb = GetToolbarKnobDb(tp, knobHit);
            tp->toolbarKnobChangedDuringDrag = false;
            SetCapture(hwnd);
            InvalidateToolbarKnob(hwnd, tp, knobHit);
            return 0;
        }
        const int toolbarHit = HitTestToolbarButton(tp, pt);
        if (toolbarHit >= 0 && IsToolbarButtonClickable(tp, toolbarHit))
        {
            tp->toolbarPressedButton = toolbarHit;
            tp->toolbarPressedInside = true;
            SetCapture(hwnd);
            InvalidateToolbarButton(hwnd, tp, toolbarHit);
            return 0;
        }
        RECT rc{}; GetClientRect(hwnd, &rc);
        RECT tabsRc{}, pianoRc{};
        ComputePianoRollLayout(rc, tp, &tabsRc, &pianoRc);
        const int tabHit = HitTestPianoRollTab(tabsRc, pt.x, pt.y);
        if (tabHit >= 0)
        {
            if (tp->activePianoRollTab != tabHit)
            {
                tp->activePianoRollTab = tabHit;
                InvalidateWaveRegion(hwnd, tp);
            }
            if (tabHit == kPianoSpectrogramTabIndex)
            {
                tp->tabDetachDragActive = true;
                tp->tabDetachDragTab = tabHit;
                tp->tabDetachDragStartPt = pt;
                tp->tabDetachDragTabsRc = tabsRc;
                SetCapture(hwnd);
            }
            else
            {
                tp->tabDetachDragActive = false;
                tp->tabDetachDragTab = -1;
            }
            return 0;
        }
        if (tp->activePianoRollTab == kPianoSpectrogramTabIndex &&
            PtInRect(&pianoRc, pt) &&
            HandleEmbeddedPianoSpecClick(hwnd, tp, pianoRc, pt))
        {
            return 0;
        }
        if (tp->activePianoRollTab != kPianoSpectrogramTabIndex &&
            HandlePianoRollGridClick(hwnd, tp, pianoRc, pt))
        {
            return 0;
        }
        size_t hitFrame = 0;
        if (TryMapWavePointToFrame(hwnd, tp, pt.x, pt.y, hitFrame))
        {
            tp->dragScrubActive = true;
            tp->dragPanActive = false;
            tp->dragResumeAfterScrub = tp->playing.load();
            tp->dragLastX = pt.x;
            SetCapture(hwnd);

            if (tp->dragResumeAfterScrub)
            {
                if (tp->mciOpened) SyncPausedFromMciPosition(tp);
                else
                {
                    const size_t totalFrames = GetTotalFrames(tp);
                    const double maxFrame = (totalFrames > 0) ? static_cast<double>(totalFrames - 1) : 0.0;
                    tp->pausedSampleIndex = static_cast<size_t>(std::clamp(GetCurrentFrameForView(tp), 0.0, maxFrame));
                }
            }
            // DJ-style scrub: start from current position and move relatively with mouse drag.
            tp->dragScrubFrameAccum = static_cast<double>(tp->pausedSampleIndex);
            InvalidateWaveRegion(hwnd, tp);
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (tp->sharedPianoGridMenuOpen)
        {
            RECT oldRc = tp->sharedPianoGridMenuRc;
            CloseSharedPianoGridMenu(tp);
            InvalidateRect(hwnd, &oldRc, FALSE);
        }
        RECT rc{}; GetClientRect(hwnd, &rc);
        RECT waveRc = ComputeWaveRect(rc, tp);
        // Allow horizontal timeline panning from both waveform and piano-roll regions.
        RECT timelineRc = rc;
        timelineRc.top = waveRc.top;
        if (PtInRect(&timelineRc, pt))
        {
            tp->dragPanActive = true;
            tp->dragScrubActive = false;
            tp->dragLastX = pt.x;
            SetCapture(hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (!tp) break;
        if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (UpdateSharedPianoGridMenuHover(hwnd, tp, pt))
                return 0;
        }
        if ((wParam & MK_LBUTTON) && tp->embeddedPianoSpecMidiSliderDragActive)
        {
            RECT rc{}; GetClientRect(hwnd, &rc);
            RECT tabsRc{}, pianoRc{};
            ComputePianoRollLayout(rc, tp, &tabsRc, &pianoRc);
            const RECT leftRc = ComputeEmbeddedPianoSpecLeftScaleRect(pianoRc);
            const RECT sliderTrackRc = ComputeEmbeddedPianoSpecMidiSliderTrackRect(leftRc);
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (SetEmbeddedPianoSpecMidiSliderFromY(tp, sliderTrackRc, pt.y))
                InvalidateWaveRegion(hwnd, tp);
            else
                InvalidateRect(hwnd, &leftRc, FALSE);
            return 0;
        }
        if ((wParam & MK_LBUTTON) && tp->toolbarDragKnob >= 0)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int dy = pt.y - tp->toolbarDragStartY;
            constexpr double kKnobDbPerPixel = 0.18;
            const double newDb = tp->toolbarDragStartDb - static_cast<double>(dy) * kKnobDbPerPixel;
            const int kidx = tp->toolbarDragKnob;
            if (SetToolbarKnobDb(tp, kidx, newDb))
            {
                tp->toolbarKnobChangedDuringDrag = true;
                if (UsingAudioEngine(tp))
                    PushAudioEngineLiveMixConfig(tp);
                InvalidateEmbeddedPianoSpec(tp);
                InvalidateToolbarKnob(hwnd, tp, kidx);
            }
            return 0;
        }
        if ((wParam & MK_LBUTTON) && tp->toolbarPressedButton >= 0)
        {
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const bool inside = (HitTestToolbarButton(tp, pt) == tp->toolbarPressedButton);
            if (inside != tp->toolbarPressedInside)
            {
                tp->toolbarPressedInside = inside;
                InvalidateToolbar(hwnd);
            }
            return 0;
        }
        if ((wParam & MK_LBUTTON) && tp->tabDetachDragActive && tp->tabDetachDragTab == kPianoSpectrogramTabIndex)
        {
            const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int dx = pt.x - tp->tabDetachDragStartPt.x;
            const int dy = pt.y - tp->tabDetachDragStartPt.y;
            constexpr int kDetachDragThresholdPx = 8;
            if ((std::abs)(dx) < kDetachDragThresholdPx && (std::abs)(dy) < kDetachDragThresholdPx)
                return 0;

            RECT detachBand = tp->tabDetachDragTabsRc;
            InflateRect(&detachBand, 8, 6);
            const bool leftTabStrip = !PtInRect(&detachBand, pt);
            const bool verticalDetach = (pt.y < detachBand.top) || (pt.y >= detachBand.bottom);
            if (leftTabStrip && verticalDetach)
            {
                tp->tabDetachDragActive = false;
                tp->tabDetachDragTab = -1;
                if (GetCapture() == hwnd) ReleaseCapture();
                OpenPianoSpectrogramPopoutFromWaveform(tp);
                return 0;
            }
            return 0;
        }
        if ((wParam & MK_LBUTTON) && tp->dragScrubActive)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int dx = pt.x - tp->dragLastX;
            tp->dragLastX = pt.x;

            RECT rc{}; GetClientRect(hwnd, &rc);
            RECT waveRc = ComputeWaveRect(rc, tp);
            const int waveW = (std::max)(1, static_cast<int>(waveRc.right - waveRc.left));
            const size_t totalFrames = GetTotalFrames(tp);
            const double zoom = GetEffectiveZoomFactor(tp, totalFrames);
            const double visibleFrames = (totalFrames > 0) ? ((double)totalFrames / zoom) : 1.0;
            const double framesPerPx = visibleFrames / (double)waveW;

            // Slow the scrub speed down to feel more like dragging the track under the playhead.
            constexpr double kScrubSensitivity = 0.5;
            tp->dragScrubFrameAccum -= (double)dx * framesPerPx * kScrubSensitivity;

            const double maxFrame = (totalFrames > 0) ? (double)(totalFrames - 1) : 0.0;
            tp->dragScrubFrameAccum = std::clamp(tp->dragScrubFrameAccum, 0.0, maxFrame);
            SeekToFrame(tp, static_cast<size_t>(std::llround(tp->dragScrubFrameAccum)), false);
            InvalidateWaveRegion(hwnd, tp);
            return 0;
        }
        if ((wParam & MK_RBUTTON) && tp->dragPanActive)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int dx = pt.x - tp->dragLastX;
            tp->dragLastX = pt.x;
            RECT rc; GetClientRect(hwnd, &rc);
            RECT waveRc = ComputeWaveRect(rc, tp);
            int waveW = (std::max)(1, static_cast<int>(waveRc.right - waveRc.left));
            size_t totalFrames = GetTotalFrames(tp);
            const double zoom = GetEffectiveZoomFactor(tp, totalFrames);
            double visibleFrames = (totalFrames > 0) ? ((double)totalFrames / zoom) : 1.0;
            double framesPerPx = visibleFrames / (double)waveW;
            constexpr double kPanSensitivity = 1.5;
            tp->panOffsetSamples -= static_cast<long long>(dx * framesPerPx * kPanSensitivity);
            tp->cacheDirty = true;
            InvalidateWaveRegion(hwnd, tp);
            return 0;
        }
        return 0;
    }

    case WM_ERASEBKGND:
        // Prevent default white background erase; we paint the full client area ourselves.
        return 1;

    case WM_LBUTTONUP:
    {
        if (tp && tp->embeddedPianoSpecMidiSliderDragActive)
        {
            tp->embeddedPianoSpecMidiSliderDragActive = false;
            InvalidateWaveRegion(hwnd, tp);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        if (tp && tp->tabDetachDragActive)
        {
            tp->tabDetachDragActive = false;
            tp->tabDetachDragTab = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        if (tp && tp->toolbarDragKnob >= 0)
        {
            const int knobIdx = tp->toolbarDragKnob;
            const bool changed = tp->toolbarKnobChangedDuringDrag;
            tp->toolbarDragKnob = -1;
            tp->toolbarKnobChangedDuringDrag = false;
            InvalidateToolbarKnob(hwnd, tp, knobIdx);
            if (changed)
            {
                InvalidateEmbeddedPianoSpec(tp);
                if (UsingAudioEngine(tp))
                    PushAudioEngineLiveMixConfig(tp);
                else
                    RebuildPlaybackAndRetuneMci(tp);
                InvalidateWaveRegion(hwnd, tp);
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        if (tp && tp->toolbarPressedButton >= 0)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            const int pressedIdx = tp->toolbarPressedButton;
            const int hit = HitTestToolbarButton(tp, pt);
            tp->toolbarPressedButton = -1;
            tp->toolbarPressedInside = false;
            InvalidateToolbar(hwnd);
            if (hit == pressedIdx && IsToolbarButtonClickable(tp, pressedIdx))
            {
                const int cmdId = GetToolbarButtonCommandId(pressedIdx);
                if (cmdId != 0)
                    SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(cmdId, 0), 0);
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        if (tp && tp->dragScrubActive)
        {
            const bool resume = tp->dragResumeAfterScrub;
            tp->dragScrubActive = false;
            tp->dragResumeAfterScrub = false;
            if (resume)
                SeekToFrame(tp, tp->pausedSampleIndex, true);
            InvalidateWaveRegion(hwnd, tp);
        }
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }

    case WM_LBUTTONDBLCLK:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const int knobHit = HitTestToolbarKnob(tp, pt);
        if (knobHit >= 0)
        {
            if (SetToolbarKnobDb(tp, knobHit, 0.0))
            {
                InvalidateEmbeddedPianoSpec(tp);
                InvalidateToolbarKnob(hwnd, tp, knobHit);
                if (UsingAudioEngine(tp))
                    PushAudioEngineLiveMixConfig(tp);
                else
                    RebuildPlaybackAndRetuneMci(tp);
                InvalidateWaveRegion(hwnd, tp);
            }
            return 0;
        }
        break;
    }

    case WM_RBUTTONUP:
    {
        if (tp)
        {
            const bool wasDraggingPan = tp->dragPanActive;
            tp->dragPanActive = false;
            if (wasDraggingPan)
                InvalidateWaveRegion(hwnd, tp); // rebuild embedded spectrogram after drag ends
        }
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }

    case WM_RBUTTONDBLCLK:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc{}; GetClientRect(hwnd, &rc);
        RECT waveRc = ComputeWaveRect(rc, tp);
        RECT timelineRc = rc;
        timelineRc.top = waveRc.top;

        // Double right-click anywhere in the timeline area (waveform + piano roll)
        // to cancel manual pan and re-align the fixed waveform anchor with playback.
        if (PtInRect(&timelineRc, pt))
        {
            tp->dragPanActive = false;
            tp->panOffsetSamples = 0;
            tp->cacheDirty = true;
            if (GetCapture() == hwnd) ReleaseCapture();
            PublishPlaybackSync(tp);
            InvalidateWaveRegion(hwnd, tp);
            return 0;
        }
        break;
    }

    case WM_WAVEFORM_TICK:
    {
        HandleRenderTick(hwnd, tp);
        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 1)
        {
            HandleRenderTick(hwnd, tp); // fallback path if timer-queue isn't active
        }
        else if (wParam == 2)
        {
            if (HWND hPlay = GetDlgItem(hwnd, 1001))
            {
                SetWindowTextW(hPlay, L"Play");
                SendMessageW(hPlay, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                RedrawWindow(hPlay, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
            }
            if (HWND hPause = GetDlgItem(hwnd, 1002))
            {
                SetWindowTextW(hPause, L"Pause");
                SendMessageW(hPause, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                RedrawWindow(hPause, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
            }
            for (int i = 0; i < 4; ++i)
            {
                if (HWND hStem = GetDlgItem(hwnd, kStemButtonBaseId + i))
                {
                    SetWindowTextW(hStem, GetStemButtonLabelByIndex(i));
                    SendMessageW(hStem, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
                    RedrawWindow(hStem, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
                }
            }
            if (tp)
            {
                tp->buttonTextRefreshPending = false;
                tp->buttonTextRefreshRetries--;
                if (tp->buttonTextRefreshRetries <= 0)
                    KillTimer(hwnd, 2);
            }
            else
            {
                KillTimer(hwnd, 2);
            }
        }
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC wndDC = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT paintRc = ps.rcPaint;
        RECT controlRc{ rc.left, rc.top, rc.right, (LONG)(std::min)(static_cast<int>(rc.bottom), static_cast<int>(rc.top) + kControlStripHeightPx) };
        const int fullW = rc.right - rc.left;
        const int fullH = rc.bottom - rc.top;
        auto excludeChildFromDc = [&](HDC targetDc, HWND child)
        {
            if (!targetDc || !child || !IsWindowVisible(child)) return;
            RECT cr{};
            if (!GetWindowRect(child, &cr)) return;
            POINT pts[2] = { {cr.left, cr.top}, {cr.right, cr.bottom} };
            MapWindowPoints(HWND_DESKTOP, hwnd, pts, 2);
            ExcludeClipRect(targetDc, pts[0].x, pts[0].y, pts[1].x, pts[1].y);
        };

        HDC hdc = wndDC;
        HDC memDC = NULL;
        HBITMAP memBmp = NULL;
        HBITMAP oldBmp = NULL;
        if (fullW > 0 && fullH > 0)
        {
            memDC = CreateCompatibleDC(wndDC);
            if (memDC)
            {
                memBmp = CreateCompatibleBitmap(wndDC, fullW, fullH);
                if (memBmp)
                {
                    oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                    hdc = memDC;
                }
            }
        }

        // Paint top control strip directly on the real window DC (the backbuffer blit excludes this strip).
        RECT controlPaint{};
        if (IntersectRect(&controlPaint, &controlRc, &paintRc))
        {
            int savedWnd = SaveDC(wndDC);
            IntersectClipRect(wndDC, controlPaint.left, controlPaint.top, controlPaint.right, controlPaint.bottom);
            excludeChildFromDc(wndDC, GetDlgItem(hwnd, 1001));
            excludeChildFromDc(wndDC, GetDlgItem(hwnd, 1002));
            for (int i = 0; i < 4; ++i)
                excludeChildFromDc(wndDC, GetDlgItem(hwnd, kStemButtonBaseId + i));
            HBRUSH controlBg = CreateSolidBrush(RGB(34, 34, 34));
            FillRect(wndDC, &controlPaint, controlBg);
            DeleteObject(controlBg);
            if (tp)
            {
                for (int i = 0; i < kToolbarButtonCount; ++i)
                {
                    if (!IsToolbarButtonVisible(tp, i)) continue;
                    RECT br = tp->toolbarButtonRects[i];
                    RECT clipped{};
                    if (!IntersectRect(&clipped, &br, &controlPaint)) continue;
                    DrawToolbarButtonVisual(
                        wndDC,
                        br,
                        GetToolbarButtonLabel(i),
                        GetToolbarButtonAccent(tp, i),
                        IsToolbarButtonLogicalOn(tp, i),
                        IsToolbarButtonClickable(tp, i),
                        (tp->toolbarPressedButton == i) && tp->toolbarPressedInside);
                }
                for (int i = 0; i < kToolbarKnobCount; ++i)
                {
                    RECT kr = tp->toolbarKnobRects[i];
                    RECT clipped{};
                    if (!IntersectRect(&clipped, &kr, &controlPaint)) continue;
                    DrawToolbarKnobVisual(
                        wndDC,
                        kr,
                        GetToolbarKnobLabel(i),
                        GetToolbarKnobAccent(i),
                        GetToolbarKnobDb(tp, i),
                        tp->toolbarDragKnob == i);
                }
            }
            RestoreDC(wndDC, savedWnd);
        }

        // Paint dark app background only below the control strip.
        RECT bgRc = paintRc;
        bgRc.top = (LONG)(std::max)(static_cast<int>(bgRc.top), static_cast<int>(controlRc.bottom));
        if (bgRc.bottom > bgRc.top)
        {
            HBRUSH bg = CreateSolidBrush(RGB(18, 18, 18));
            FillRect(hdc, &bgRc, bg);
            DeleteObject(bg);
        }

        RECT waveRc = ComputeWaveRect(rc, tp);

        HBRUSH waveBg = CreateSolidBrush(RGB(12, 12, 12));
        RECT waveFill{};
        if (IntersectRect(&waveFill, &waveRc, &paintRc))
            FillRect(hdc, &waveFill, waveBg);
        DeleteObject(waveBg);

        // Keep the old "waveform at top with divider line" proportion/layout.
        if (paintRc.top <= waveRc.bottom && paintRc.bottom >= waveRc.bottom)
        {
            HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            HGDIOBJ oldSepPen = SelectObject(hdc, sepPen);
            MoveToEx(hdc, rc.left, waveRc.bottom, NULL);
            LineTo(hdc, rc.right, waveRc.bottom);
            SelectObject(hdc, oldSepPen);
            DeleteObject(sepPen);
        }

        if (tp && tp->samples && !tp->samples->empty() && waveRc.right - waveRc.left > 0)
        {
            int w = (std::max)(1, static_cast<int>(waveRc.right - waveRc.left));
            int h = (std::max)(1, static_cast<int>(waveRc.bottom - waveRc.top));

            size_t totalFrames = tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
            if (tp->envBlocks == 0)
                PrepareColorWaveEnvelopes(tp);

            // compute current playhead frame
            double curFrameD = GetCurrentFrameForView(tp);
            if (totalFrames > 0)
                curFrameD = std::clamp(curFrameD, 0.0, static_cast<double>(totalFrames - 1));
            else
                curFrameD = 0.0;

            double centerFrame = curFrameD + (double)tp->panOffsetSamples;
            if (centerFrame < 0) centerFrame = 0;
            if (centerFrame >= (double)totalFrames) centerFrame = (double)totalFrames - 1.0;

            const double zoom = GetEffectiveZoomFactor(tp, totalFrames);
            double visibleFrames = std::max(1.0, (double)totalFrames / zoom);
            const double unclampedStartFrame = centerFrame - tp->playheadXRatio * visibleFrames;
            double startFrame = unclampedStartFrame;
            if (startFrame < 0) startFrame = 0;
            if (startFrame + visibleFrames > (double)totalFrames)
                startFrame = (double)totalFrames - visibleFrames;

            int saved = SaveDC(hdc);
            IntersectClipRect(hdc, waveRc.left, waveRc.top, waveRc.right, waveRc.bottom);
            const int midY = waveRc.top + (h / 2);
            const double ampScale = (static_cast<double>(h) * 0.5) / static_cast<double>(tp->plotYRange);

            {
                HPEN midPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
                HGDIOBJ oldPen = SelectObject(hdc, midPen);
                MoveToEx(hdc, waveRc.left, midY, NULL);
                LineTo(hdc, waveRc.right, midY);
                SelectObject(hdc, oldPen);
                DeleteObject(midPen);
            }

            if (tp->envBlocks > 0 && tp->envBlock > 0)
            {
                const double endFrame = std::min<double>(static_cast<double>(totalFrames), startFrame + visibleFrames);
                const double framesPerPixel = visibleFrames / static_cast<double>((std::max)(1, w));
                const EnvelopeLevelView envView = SelectEnvelopeLevelForFramesPerPixel(tp, framesPerPixel);
                const int drawEnvBlock = (envView.block > 0) ? envView.block : tp->envBlock;
                const size_t drawEnvBlocks = (envView.blocks > 0) ? envView.blocks : tp->envBlocks;

                size_t b0 = static_cast<size_t>(std::floor(startFrame / static_cast<double>(drawEnvBlock)));
                size_t b1 = static_cast<size_t>(std::ceil(endFrame / static_cast<double>(drawEnvBlock)));
                if (b0 > drawEnvBlocks) b0 = drawEnvBlocks;
                if (b1 > drawEnvBlocks) b1 = drawEnvBlocks;

                const std::vector<float>& baseMin = (envView.baseMinF ? *envView.baseMinF : tp->baseMinF);
                const std::vector<float>& baseMax = (envView.baseMaxF ? *envView.baseMaxF : tp->baseMaxF);
                const std::vector<float>& lowMin = (envView.lowMinF ? *envView.lowMinF : tp->lowMinF);
                const std::vector<float>& lowMax = (envView.lowMaxF ? *envView.lowMaxF : tp->lowMaxF);
                const std::vector<float>& midMin = (envView.midMinF ? *envView.midMinF : tp->midMinF);
                const std::vector<float>& midMax = (envView.midMaxF ? *envView.midMaxF : tp->midMaxF);
                const std::vector<float>& highMin = (envView.highMinF ? *envView.highMinF : tp->highMinF);
                const std::vector<float>& highMax = (envView.highMaxF ? *envView.highMaxF : tp->highMaxF);

                // Draw in the same stacking order as aubioTest.py: base -> low -> mid -> high.
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(120, 120, 120),
                    baseMin, baseMax, b0, b1, drawEnvBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(0, 140, 255),
                    lowMin, lowMax, b0, b1, drawEnvBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(255, 170, 0),
                    midMin, midMax, b0, b1, drawEnvBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(255, 60, 140),
                    highMin, highMax, b0, b1, drawEnvBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
            }

            const double tLeft = startFrame / static_cast<double>(tp->sampleRate);
            const double tRight = (startFrame + visibleFrames) / static_cast<double>(tp->sampleRate);
            DrawBeatGridOverlay(hdc, waveRc, tp, startFrame, visibleFrames, totalFrames, tp->sampleRate);
            // Draw waveform time labels after grid lines so they remain visible on the Spec tab too.
            DrawTimeMarksOverlay(hdc, waveRc, tLeft, tRight, true);

            // Draw playhead marker. During normal scrolling it stays at the fixed anchor ratio,
            // but once the viewport hits the song boundary (e.g. near the end), let the marker
            // move to the actual visible playback position so it can travel to the edge.
            const bool viewportClamped = std::fabs(startFrame - unclampedStartFrame) > 1e-6;
            double playheadXNorm = tp->playheadXRatio;
            if (viewportClamped && visibleFrames > 0.0)
            {
                const double denom = (visibleFrames > 1.0) ? (visibleFrames - 1.0) : visibleFrames;
                playheadXNorm = (denom > 0.0) ? ((curFrameD - startFrame) / denom) : tp->playheadXRatio;
                if (!std::isfinite(playheadXNorm))
                    playheadXNorm = tp->playheadXRatio;
                playheadXNorm = std::clamp(playheadXNorm, 0.0, 1.0);
            }
            int playheadX = waveRc.left + static_cast<int>(std::lround(playheadXNorm * (double)((std::max)(1, w) - 1)));
            HPEN ph = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ oldPen = SelectObject(hdc, ph);
            MoveToEx(hdc, playheadX, waveRc.top, NULL);
            LineTo(hdc, playheadX, waveRc.bottom);
            SelectObject(hdc, oldPen);
            DeleteObject(ph);

            RestoreDC(hdc, saved);

            const double curSeconds = curFrameD / static_cast<double>((std::max)(1, tp->sampleRate));
            const double durationSeconds = static_cast<double>(totalFrames) / static_cast<double>((std::max)(1, tp->sampleRate));
            const double beatPeriod = (tp->gridBpm > 0.0) ? (60.0 / tp->gridBpm) : 0.0;
            std::wstring curTime = FormatTimeLabel(curSeconds, true);
            std::wstring totalTime = FormatTimeLabel(durationSeconds, true);
            const wchar_t* mixModeLabel = ShouldUseSourcePlaybackDirect(tp) ? L"SRC" : L"STEMS";
            const wchar_t* audioBackendLabel = UsingAudioEngine(tp) ? L"miniaudio" : L"MCI";

            wchar_t buf1[384];
            swprintf_s(buf1,
                L"t=%s / %s  frame=%zu  zoom=%.2fx  play=%s  src=%s  audio=%s  mix=%s  grid=%s",
                curTime.c_str(),
                totalTime.c_str(),
                static_cast<size_t>((curFrameD < 0.0) ? 0 : static_cast<size_t>(curFrameD)),
                tp->zoomFactor,
                tp->playing.load() ? L"ON" : L"off",
                tp->isStereo ? L"Stereo" : L"Mono",
                audioBackendLabel,
                mixModeLabel,
                tp->gridEnabled ? L"ON" : L"off");

            wchar_t buf2[512];
            swprintf_s(buf2,
                L"BPM=%.3f  T=%.6fs  t0=%.6fs  beats/bar=%d  start=%.3fs  onset=%.3fs  kick=%.6fs  EQ[L/M/H]=%+.0f/%+.0f/%+.0f dB  VOL=%+.0f dB  view=[%.3f..%.3f]s",
                tp->gridBpm,
                beatPeriod,
                tp->gridT0Seconds,
                tp->gridBeatsPerBar,
                tp->gridAudioStartSeconds,
                tp->gridApproxOnsetSeconds,
                tp->gridKickAttackSeconds,
                tp->eqLowDb,
                tp->eqMidDb,
                tp->eqHighDb,
                tp->masterGainDb,
                tLeft,
                tRight);

            SetTextColor(hdc, RGB(235, 235, 235));
            SetBkMode(hdc, TRANSPARENT);
            const int textY1 = waveRc.bottom + 4;
            const int textY2 = textY1 + 16;
            if (paintRc.top <= textY1 + 14 && paintRc.bottom >= textY1)
                TextOutW(hdc, 10, textY1, buf1, (int)wcslen(buf1));
            if (paintRc.top <= textY2 + 14 && paintRc.bottom >= textY2)
                TextOutW(hdc, 10, textY2, buf2, (int)wcslen(buf2));

            RECT tabsRc{}, pianoRc{};
            ComputePianoRollLayout(rc, tp, &tabsRc, &pianoRc);
            if (tabsRc.bottom - tabsRc.top >= 18)
            {
                RECT tabsPaint{};
                if (IntersectRect(&tabsPaint, &tabsRc, &paintRc))
                    DrawPianoRollTabs(hdc, tabsRc, tp->activePianoRollTab);
            }
            if (pianoRc.bottom - pianoRc.top >= 40)
            {
                RECT pianoPaint{};
                if (IntersectRect(&pianoPaint, &pianoRc, &paintRc))
                {
                    if (tp->activePianoRollTab == kPianoSpectrogramTabIndex)
                    {
                        DrawEmbeddedPianoSpectrogramTab(hdc, pianoRc, tp, tLeft, tRight, curSeconds, playheadX);
                    }
                    else
                    {
                        PianoRollRenderer::ViewState pv{};
                        pv.tLeftSeconds = tLeft;
                        pv.tRightSeconds = tRight;
                        pv.playheadSeconds = curSeconds;
                        pv.showBeatGrid = tp->gridEnabled;
                        pv.bpm = tp->gridBpm;
                        pv.t0Seconds = tp->gridT0Seconds;
                        pv.beatsPerBar = tp->gridBeatsPerBar;
                        pv.gridMode = WaveformWindow::GetSharedPianoGridMode();

                        PianoRollRenderer::Config pc{};
                        PianoRollRenderer::Draw(hdc, pianoRc, pv, pc, nullptr);

                        LayoutPianoRollGridControl(tp, pianoRc);
                        if (tp->pianoRollRcGridButton.right > tp->pianoRollRcGridButton.left)
                        {
                            wchar_t gridLabel[64];
                            swprintf_s(gridLabel, L"Grid %s", PianoRollGridModeLabel(WaveformWindow::GetSharedPianoGridMode()));
                            DrawEmbeddedPianoSpecButton(hdc, tp->pianoRollRcGridButton, gridLabel);
                        }
                    }
                }
            }
            DrawSharedPianoGridMenu(hdc, tp);
        }

        if (memDC && memBmp)
        {
            const int bw = (std::max)(0, static_cast<int>(paintRc.right - paintRc.left));
            const int bh = (std::max)(0, static_cast<int>(paintRc.bottom - paintRc.top));
            if (bw > 0 && bh > 0)
            {
                // Don't blit over child controls (buttons). On resize/fullscreen the parent backbuffer
                // can otherwise overwrite their text until the child repaints.
                int savedWnd = SaveDC(wndDC);
                IntersectClipRect(wndDC, paintRc.left, paintRc.top, paintRc.right, paintRc.bottom);
                ExcludeClipRect(wndDC, controlRc.left, controlRc.top, controlRc.right, controlRc.bottom);

                if (tp)
                {
                    excludeChildFromDc(wndDC, tp->hBtnPlay);
                    excludeChildFromDc(wndDC, tp->hBtnPause);
                    for (int i = 0; i < 4; ++i)
                        excludeChildFromDc(wndDC, tp->hBtnStem[i]);
                }
                else
                {
                    excludeChildFromDc(wndDC, GetDlgItem(hwnd, 1001));
                    excludeChildFromDc(wndDC, GetDlgItem(hwnd, 1002));
                    for (int i = 0; i < 4; ++i)
                        excludeChildFromDc(wndDC, GetDlgItem(hwnd, kStemButtonBaseId + i));
                }

                BitBlt(wndDC, paintRc.left, paintRc.top, bw, bh, memDC, paintRc.left, paintRc.top, SRCCOPY);
                RestoreDC(wndDC, savedWnd);
            }
        }

        if (memDC)
        {
            if (oldBmp) SelectObject(memDC, oldBmp);
            if (memBmp) DeleteObject(memBmp);
            DeleteDC(memDC);
        }

        // One-time child-control refresh after first parent paint (fixes blank button labels on startup).
        if (tp && tp->buttonTextRefreshPending)
        {
            if (HWND hPlay = tp->hBtnPlay ? tp->hBtnPlay : GetDlgItem(hwnd, 1001))
                RedrawWindow(hPlay, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
            if (HWND hPause = tp->hBtnPause ? tp->hBtnPause : GetDlgItem(hwnd, 1002))
                RedrawWindow(hPause, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
            for (int i = 0; i < 4; ++i)
            {
                if (HWND hStem = tp->hBtnStem[i] ? tp->hBtnStem[i] : GetDlgItem(hwnd, kStemButtonBaseId + i))
                    RedrawWindow(hStem, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
            }
            tp->buttonTextRefreshPending = false;
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (tp)
        {
            ClearPlaybackSync();
            if (gSharedWaveformHwnd.load() == hwnd)
                gSharedWaveformHwnd.store(nullptr);
            tp->hwnd = nullptr;
            tp->audioEngine.Shutdown();
            tp->useAudioEngine = false;
            if (tp->renderTimer)
            {
                DeleteTimerQueueTimer(tp->renderTimerQueue, tp->renderTimer, INVALID_HANDLE_VALUE);
                tp->renderTimer = nullptr;
            }
            if (tp->renderTimerQueue)
            {
                DeleteTimerQueueEx(tp->renderTimerQueue, INVALID_HANDLE_VALUE);
                tp->renderTimerQueue = nullptr;
            }
            if (tp->mciOpened)
            {
                MciClose(tp);
            }
            if (!tp->tempPath.empty())
            {
                DeleteFileW(tp->tempPath.c_str());
            }
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

static DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
    std::unique_ptr<ThreadParam> tp(reinterpret_cast<ThreadParam*>(lpParameter));
    timeBeginPeriod(1); // request finer timer granularity for WM_TIMER
    HINSTANCE hInstance = GetModuleHandle(NULL);
    const wchar_t CLASS_NAME[] = L"WaveformWindowClass";

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = WndProc;
        wc.style = CS_DBLCLKS;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

        if (!RegisterClass(&wc))
        {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS)
                return 1;
        }
    }

    tp->bands.lowMaxHz = 250.0;
    tp->bands.midMaxHz = 2000.0;
    tp->analyzer.init(tp->nfft, tp->sampleRate);
    PrepareColorWaveEnvelopes(tp.get());

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        tp->title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 520,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hwnd)
        return 1;

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tp.get()));
    tp->hwnd = hwnd;
    gSharedWaveformHwnd.store(hwnd);
    tp->hBtnPlay = GetDlgItem(hwnd, 1001);
    tp->hBtnPause = GetDlgItem(hwnd, 1002);
    for (int i = 0; i < 4; ++i) tp->hBtnStem[i] = GetDlgItem(hwnd, kStemButtonBaseId + i);
    LayoutTopButtons(hwnd, tp.get());
    PublishPlaybackSync(tp.get());

    if (HasStemPlayback(tp.get()) || HasAnyPlaybackEq(tp.get()))
    {
        BuildCurrentPlaybackBuffer(tp.get());
        if (!tp->stemMixScratch.empty())
            tp->tempPath = BuildOrRewriteTempWav16(tp->stemMixScratch, tp->sampleRate, tp->isStereo, tp->tempPath);
    }

    if (audio::AudioEngine::BackendAvailable())
    {
        const bool needsStartupLiveMix =
            HasAnyPlaybackEq(tp.get()) || !ShouldUseSourcePlaybackDirect(tp.get());

        bool initialized = false;
        if (!needsStartupLiveMix && !tp->sourceFilePathHint.empty())
        {
            initialized = tp->audioEngine.InitializeFromWavFile(tp->sourceFilePathHint);
        }

        if (!initialized)
        {
            std::vector<short>* startSrc = nullptr;
            if (!tp->stemMixScratch.empty()) startSrc = &tp->stemMixScratch;
            else startSrc = tp->samples;
            initialized = tp->audioEngine.Initialize(startSrc, tp->sampleRate, tp->isStereo);
        }

        tp->useAudioEngine = initialized;
        if (tp->useAudioEngine && needsStartupLiveMix)
            PushAudioEngineLiveMixConfig(tp.get());
    }

    tp->renderTimerQueue = CreateTimerQueue();
    if (tp->renderTimerQueue)
    {
        CreateTimerQueueTimer(
            &tp->renderTimer,
            tp->renderTimerQueue,
            RenderTimerCallback,
            tp.get(),
            1,
            tp->renderIntervalMs,
            WT_EXECUTEDEFAULT
        );
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (tp->autoStart && (UsingAudioEngine(tp.get()) || !tp->tempPath.empty()))
    {
        PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(1001, BN_CLICKED), 0);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (tp->ownsSamples && tp->samples)
    {
        delete tp->samples;
        tp->samples = nullptr;
    }

    // close MCI if still open and delete temp file
    if (tp->renderTimer)
    {
        DeleteTimerQueueTimer(tp->renderTimerQueue, tp->renderTimer, INVALID_HANDLE_VALUE);
        tp->renderTimer = nullptr;
    }
    if (tp->renderTimerQueue)
    {
        DeleteTimerQueueEx(tp->renderTimerQueue, INVALID_HANDLE_VALUE);
        tp->renderTimerQueue = nullptr;
    }
    tp->audioEngine.Shutdown();
    tp->useAudioEngine = false;
    if (tp->mciOpened) MciClose(tp.get());
    if (!tp->tempPath.empty()) DeleteFileW(tp->tempPath.c_str());

    timeEndPeriod(1);
    return static_cast<DWORD>(msg.wParam);
}

void WaveformWindow::ShowWaveformAsyncCopy(const std::vector<short>& samples, int sampleRate, const std::wstring& title)
{
    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = true;
    tp->samples = new std::vector<short>(samples); // deep copy owned by thread
    tp->isStereo = false;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncRef(std::vector<short>* samples, int sampleRate, const std::wstring& title)
{
    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = false;
    tp->samples = samples; // not owned; caller must keep alive
    tp->isStereo = false;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncCopyPlay(const std::vector<short>& samples, int sampleRate, bool startPlaying, const std::wstring& title)
{
    std::wstring path = WriteTempWav16(samples, sampleRate, false);
    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = true;
    tp->samples = new std::vector<short>(samples);
    tp->tempPath = path;
    tp->mciAlias = L"wf" + std::to_wstring(GetTickCount64());
    tp->mciOpened = false;
    tp->isStereo = false;
    tp->autoStart = startPlaying;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncRefPlay(std::vector<short>* samples, int sampleRate, bool startPlaying, const std::wstring& title)
{
    std::wstring path;
    if (samples)
    {
        path = WriteTempWav16(*samples, sampleRate, false);
    }

    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = false;
    tp->samples = samples;
    tp->tempPath = path;
    tp->mciAlias = L"wf" + std::to_wstring(GetTickCount64());
    tp->mciOpened = false;
    tp->isStereo = false;
    tp->autoStart = startPlaying;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

// Stereo variants

void WaveformWindow::ShowWaveformAsyncCopyPlayStereo(const std::vector<short>& interleavedStereoSamples, int sampleRate, bool startPlaying, const std::wstring& title)
{
    std::wstring path = WriteTempWav16(interleavedStereoSamples, sampleRate, true);
    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = true;
    tp->samples = new std::vector<short>(interleavedStereoSamples);
    tp->tempPath = path;
    tp->mciAlias = L"wf" + std::to_wstring(GetTickCount64());
    tp->mciOpened = false;
    tp->isStereo = true;
    tp->autoStart = startPlaying;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncRefPlayStereo(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const std::wstring& title, const std::wstring& sourcePathHint)
{
    std::wstring path;
    if (interleavedStereoSamples)
    {
        path = WriteTempWav16(*interleavedStereoSamples, sampleRate, true);
    }

    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = false;
    tp->samples = interleavedStereoSamples;
    tp->tempPath = path;
    tp->mciAlias = L"wf" + std::to_wstring(GetTickCount64());
    tp->mciOpened = false;
    tp->isStereo = true;
    tp->autoStart = startPlaying;
    tp->sourceFilePathHint = sourcePathHint;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncRefPlayStereoGrid(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const GridOverlayConfig& grid, const std::wstring& title, const std::wstring& sourcePathHint)
{
    StemPlaybackConfig noStems{};
    ShowWaveformAsyncRefPlayStereoGridStems(interleavedStereoSamples, sampleRate, startPlaying, grid, noStems, title, sourcePathHint);
}

void WaveformWindow::ShowWaveformAsyncRefPlayStereoGridStems(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const GridOverlayConfig& grid, const StemPlaybackConfig& stems, const std::wstring& title, const std::wstring& sourcePathHint)
{
    std::wstring path;
    if (interleavedStereoSamples)
    {
        path = WriteTempWav16(*interleavedStereoSamples, sampleRate, true);
    }

    ThreadParam* tp = new ThreadParam();
    tp->sampleRate = sampleRate;
    tp->title = title;
    tp->ownsSamples = false;
    tp->samples = interleavedStereoSamples;
    tp->tempPath = path;
    tp->mciAlias = L"wf" + std::to_wstring(GetTickCount64());
    tp->mciOpened = false;
    tp->isStereo = true;
    tp->autoStart = startPlaying;
    tp->sourceFilePathHint = sourcePathHint;
    tp->gridEnabled = grid.enabled;
    tp->gridBpm = grid.bpm;
    tp->gridT0Seconds = grid.t0Seconds;
    tp->gridBeatsPerBar = grid.beatsPerBar;
    tp->gridAudioStartSeconds = grid.audioStartSeconds;
    tp->gridApproxOnsetSeconds = grid.approxOnsetSeconds;
    tp->gridKickAttackSeconds = grid.kickAttackSeconds;
    tp->stemPlaybackEnabled = stems.enabled;
    tp->stemVocals = stems.vocalsInterleavedStereo;
    tp->stemVocalsSampleRate = stems.vocalsSampleRate;
    tp->stemVocalsChannels = stems.vocalsChannels;
    tp->stemDrums = stems.drumsInterleavedStereo;
    tp->stemDrumsSampleRate = stems.drumsSampleRate;
    tp->stemDrumsChannels = stems.drumsChannels;
    tp->stemBass = stems.bassInterleavedStereo;
    tp->stemBassSampleRate = stems.bassSampleRate;
    tp->stemBassChannels = stems.bassChannels;
    tp->stemChords = stems.chordsInterleavedStereo;
    tp->stemChordsSampleRate = stems.chordsSampleRate;
    tp->stemChordsChannels = stems.chordsChannels;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

bool WaveformWindow::BuildPlaybackSpectrogramMonoWindow(double centerFrame, int frameCount, std::vector<double>& outMono)
{
    outMono.clear();
    if (frameCount <= 0 || !std::isfinite(centerFrame))
        return false;
    if (!gSharedPlaybackAudio.valid.load())
        return false;

    struct AudioSnap
    {
        const std::vector<short>* mainSamples = nullptr;
        int sampleRate = 0;
        int mainChannels = 2;
        bool stemPlaybackEnabled = false;
        const std::vector<short>* stems[4]{};
        int stemSampleRate[4]{};
        int stemChannels[4]{};
        bool stemEnabled[4]{};
        double eqLowDb = 0.0;
        double eqMidDb = 0.0;
        double eqHighDb = 0.0;
        double masterGainDb = 0.0;
        double playbackRate = 1.0;
    } s;

    s.mainSamples = gSharedPlaybackAudio.mainSamples.load();
    s.sampleRate = gSharedPlaybackAudio.sampleRate.load();
    s.mainChannels = gSharedPlaybackAudio.mainChannels.load();
    s.stemPlaybackEnabled = gSharedPlaybackAudio.stemPlaybackEnabled.load();
    for (int i = 0; i < 4; ++i)
    {
        s.stems[i] = gSharedPlaybackAudio.stems[i].load();
        s.stemSampleRate[i] = gSharedPlaybackAudio.stemSampleRate[i].load();
        s.stemChannels[i] = gSharedPlaybackAudio.stemChannels[i].load();
        s.stemEnabled[i] = gSharedPlaybackAudio.stemEnabled[i].load();
    }
    s.eqLowDb = gSharedPlaybackAudio.eqLowDb.load();
    s.eqMidDb = gSharedPlaybackAudio.eqMidDb.load();
    s.eqHighDb = gSharedPlaybackAudio.eqHighDb.load();
    s.masterGainDb = gSharedPlaybackAudio.masterGainDb.load();
    s.playbackRate = gSharedPlaybackAudio.playbackRate.load();

    if (s.sampleRate <= 0)
        return false;
    if (!std::isfinite(s.playbackRate) || s.playbackRate <= 0.0)
        s.playbackRate = 1.0;
    s.playbackRate = std::clamp(s.playbackRate, 0.125, 4.0);

    auto pcm16ToNorm = [](short v) -> double
    {
        return static_cast<double>(v) / 32768.0;
    };

    auto sampleSourceStereoAt = [&](const std::vector<short>* vec, int srcRate, int srcChannels, double timelineFramePos, double& outL, double& outR)
    {
        outL = 0.0;
        outR = 0.0;
        if (!vec || vec->empty() || srcRate <= 0 || !std::isfinite(timelineFramePos))
            return;

        const int ch = (std::max)(1, (std::min)(2, srcChannels));
        const std::size_t srcFrames = vec->size() / static_cast<std::size_t>(ch);
        if (srcFrames == 0) return;

        double srcPos = timelineFramePos;
        if (srcRate != s.sampleRate)
            srcPos *= static_cast<double>((std::max)(1, srcRate)) / static_cast<double>((std::max)(1, s.sampleRate));
        if (srcPos < 0.0 || srcPos > static_cast<double>(srcFrames - 1))
            return;

        const std::size_t i0 = static_cast<std::size_t>(std::floor(srcPos));
        const std::size_t i1 = (std::min)(i0 + 1, srcFrames - 1);
        const double t = std::clamp(srcPos - static_cast<double>(i0), 0.0, 1.0);

        const std::size_t b0 = i0 * static_cast<std::size_t>(ch);
        const std::size_t b1 = i1 * static_cast<std::size_t>(ch);
        const double l0 = pcm16ToNorm((*vec)[b0]);
        const double r0 = (ch >= 2 && (b0 + 1) < vec->size()) ? pcm16ToNorm((*vec)[b0 + 1]) : l0;
        const double l1 = pcm16ToNorm((*vec)[b1]);
        const double r1 = (ch >= 2 && (b1 + 1) < vec->size()) ? pcm16ToNorm((*vec)[b1 + 1]) : l1;
        outL = l0 + (l1 - l0) * t;
        outR = r0 + (r1 - r0) * t;
    };

    bool anyStemSource = false;
    bool allStemsOn = true;
    for (int i = 0; i < 4; ++i)
    {
        anyStemSource = anyStemSource || (s.stems[i] != nullptr);
        allStemsOn = allStemsOn && s.stemEnabled[i];
    }
    const bool useSourceDirect = (!s.stemPlaybackEnabled) || !anyStemSource || allStemsOn;
    if (useSourceDirect && !s.mainSamples)
        return false;

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0;
        double a1 = 0.0, a2 = 0.0;
        double process(double x, double& z1, double& z2) const
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    auto normalize_biquad = [](double b0, double b1, double b2, double a0, double a1, double a2) -> Biquad
    {
        const double invA0 = (std::fabs(a0) > 1e-18) ? (1.0 / a0) : 1.0;
        Biquad q{};
        q.b0 = b0 * invA0; q.b1 = b1 * invA0; q.b2 = b2 * invA0;
        q.a1 = a1 * invA0; q.a2 = a2 * invA0;
        return q;
    };
    auto make_peaking = [&](double fc, double q, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(s.sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        q = (std::max)(0.1, q);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 / (2.0 * q);
        return normalize_biquad(
            1.0 + alpha * A,
            -2.0 * cw0,
            1.0 - alpha * A,
            1.0 + alpha / A,
            -2.0 * cw0,
            1.0 - alpha / A);
    };
    auto make_lowshelf = [&](double fc, double slope, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(s.sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        slope = (std::max)(0.1, slope);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double t = 2.0 * std::sqrt(A) * alpha;
        return normalize_biquad(
            A * ((A + 1.0) - (A - 1.0) * cw0 + t),
            2.0 * A * ((A - 1.0) - (A + 1.0) * cw0),
            A * ((A + 1.0) - (A - 1.0) * cw0 - t),
            (A + 1.0) + (A - 1.0) * cw0 + t,
            -2.0 * ((A - 1.0) + (A + 1.0) * cw0),
            (A + 1.0) + (A - 1.0) * cw0 - t);
    };
    auto make_highshelf = [&](double fc, double slope, double gainDb) -> Biquad
    {
        const double fs = static_cast<double>(s.sampleRate);
        fc = std::clamp(fc, 10.0, fs * 0.45);
        slope = (std::max)(0.1, slope);
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        const double cw0 = std::cos(w0);
        const double sw0 = std::sin(w0);
        const double alpha = sw0 * 0.5 * std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double t = 2.0 * std::sqrt(A) * alpha;
        return normalize_biquad(
            A * ((A + 1.0) + (A - 1.0) * cw0 + t),
            -2.0 * A * ((A - 1.0) + (A + 1.0) * cw0),
            A * ((A + 1.0) + (A - 1.0) * cw0 - t),
            (A + 1.0) - (A - 1.0) * cw0 + t,
            2.0 * ((A - 1.0) - (A + 1.0) * cw0),
            (A + 1.0) - (A - 1.0) * cw0 - t);
    };

    const bool eqActive = std::fabs(s.eqLowDb) > 1e-6 || std::fabs(s.eqMidDb) > 1e-6 || std::fabs(s.eqHighDb) > 1e-6;
    const bool gainActive = std::fabs(s.masterGainDb) > 1e-6;
    const double masterGain = gainActive ? std::pow(10.0, s.masterGainDb / 20.0) : 1.0;
    const Biquad lowShelf = eqActive ? make_lowshelf(220.0, 0.9, s.eqLowDb) : Biquad{};
    const Biquad midBell = eqActive ? make_peaking(1000.0, 0.75, s.eqMidDb) : Biquad{};
    const Biquad highShelf = eqActive ? make_highshelf(4200.0, 0.9, s.eqHighDb) : Biquad{};
    double lz1 = 0.0, lz2 = 0.0, mz1 = 0.0, mz2 = 0.0, hz1 = 0.0, hz2 = 0.0;
    double lz1r = 0.0, lz2r = 0.0, mz1r = 0.0, mz2r = 0.0, hz1r = 0.0, hz2r = 0.0;

    outMono.assign(static_cast<std::size_t>(frameCount), 0.0);
    const double half = 0.5 * static_cast<double>(frameCount - 1);
    for (int i = 0; i < frameCount; ++i)
    {
        // Spectrogram analysis should reflect stems/EQ/VOL, but not playback-speed slowdown.
        // Keeping analysis at 1.0x preserves the true spectral content while half-speed playback
        // just gives the user more time to inspect it.
        const double timelineFramePos = centerFrame + (static_cast<double>(i) - half);
        double l = 0.0;
        double r = 0.0;

        if (useSourceDirect)
        {
            sampleSourceStereoAt(s.mainSamples, s.sampleRate, s.mainChannels, timelineFramePos, l, r);
        }
        else
        {
            for (int stemIdx = 0; stemIdx < 4; ++stemIdx)
            {
                if (!s.stemEnabled[stemIdx]) continue;
                double sl = 0.0, sr = 0.0;
                const int stemRate = (s.stemSampleRate[stemIdx] > 0) ? s.stemSampleRate[stemIdx] : s.sampleRate;
                sampleSourceStereoAt(s.stems[stemIdx], stemRate, s.stemChannels[stemIdx], timelineFramePos, sl, sr);
                l += sl;
                r += sr;
            }
        }

        if (eqActive)
        {
            l = lowShelf.process(l, lz1, lz2);
            l = midBell.process(l, mz1, mz2);
            l = highShelf.process(l, hz1, hz2);
            r = lowShelf.process(r, lz1r, lz2r);
            r = midBell.process(r, mz1r, mz2r);
            r = highShelf.process(r, hz1r, hz2r);
        }

        if (gainActive)
        {
            l *= masterGain;
            r *= masterGain;
        }

        if (!std::isfinite(l)) l = 0.0;
        if (!std::isfinite(r)) r = 0.0;
        l = std::clamp(l, -1.0, 1.0);
        r = std::clamp(r, -1.0, 1.0);
        outMono[static_cast<std::size_t>(i)] = 0.5 * (l + r);
    }
    return true;
}

bool WaveformWindow::GetPlaybackSyncSnapshot(PlaybackSyncSnapshot& out)
{
    out.valid = gSharedPlayback.valid.load();
    out.playing = gSharedPlayback.playing.load();
    out.isStereo = gSharedPlayback.isStereo.load();
    out.sampleRate = gSharedPlayback.sampleRate.load();
    const long long totalFrames = gSharedPlayback.totalFrames.load();
    const long long currentFrame = gSharedPlayback.currentFrame.load();
    out.totalFrames = static_cast<std::size_t>((std::max)(0LL, totalFrames));
    out.currentFrame = static_cast<double>((std::max)(0LL, currentFrame));
    out.playbackRate = gSharedPlayback.playbackRate.load();
    out.zoomFactor = gSharedPlayback.zoomFactor.load();
    out.panOffsetFrames = gSharedPlayback.panOffsetFrames.load();
    out.playheadXRatio = gSharedPlayback.playheadXRatio.load();
    out.grid.enabled = gSharedPlayback.gridEnabled.load();
    out.grid.bpm = gSharedPlayback.gridBpm.load();
    out.grid.t0Seconds = gSharedPlayback.gridT0Seconds.load();
    out.grid.beatsPerBar = gSharedPlayback.gridBeatsPerBar.load();
    out.grid.audioStartSeconds = gSharedPlayback.gridAudioStartSeconds.load();
    out.grid.approxOnsetSeconds = gSharedPlayback.gridApproxOnsetSeconds.load();
    out.grid.kickAttackSeconds = gSharedPlayback.gridKickAttackSeconds.load();
    return out.valid;
}

int WaveformWindow::GetSharedPianoGridMode()
{
    int mode = gSharedPianoGridMode.load();
    if (mode < 0 || mode >= PianoRollRenderer::Grid_Count)
        mode = PianoRollRenderer::Grid_Beat;
    return mode;
}

void WaveformWindow::SetSharedPianoGridMode(int mode)
{
    if (mode < 0 || mode >= PianoRollRenderer::Grid_Count)
        mode = PianoRollRenderer::Grid_Beat;
    gSharedPianoGridMode.store(mode);

    HWND hwnd = gSharedWaveformHwnd.load();
    if (hwnd && IsWindow(hwnd))
        InvalidateRect(hwnd, NULL, FALSE);
}
