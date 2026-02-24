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

#include "DSP.h"   // dsp helpers (FFTW STFT + cache builder)

using namespace WaveformWindow;

namespace
{
    constexpr UINT WM_WAVEFORM_TICK = WM_APP + 17;
}

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
    DWORD renderIntervalMs = 8;
    HANDLE renderTimerQueue = nullptr;
    HANDLE renderTimer = nullptr;

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

    // ---- beat grid overlay ----
    bool gridEnabled = false;
    double gridBpm = 0.0;
    double gridT0Seconds = 0.0;
    int gridBeatsPerBar = 4;
    double gridAudioStartSeconds = 0.0;
    double gridApproxOnsetSeconds = 0.0;
    double gridKickAttackSeconds = 0.0;

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
    int div = 10;
    if (tp && tp->regionDiv > 0) div = tp->regionDiv;
    int waveH = (div > 0) ? (fullH / div) : (fullH / 10);
    waveH = (std::max)(waveH, 40);
    waveH = (std::min)(waveH, fullH);
    waveRc.bottom = waveRc.top + waveH;
    return waveRc;
}

static RECT ComputeWaveInvalidateRect(HWND hwnd, const ThreadParam* tp)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    RECT waveRc = ComputeWaveRect(rc, tp);
    RECT inv = rc;
    inv.top = waveRc.top;
    inv.bottom = (std::min)(static_cast<int>(rc.bottom), static_cast<int>(waveRc.bottom) + 48);
    return inv;
}

static void InvalidateWaveRegion(HWND hwnd, const ThreadParam* tp)
{
    if (!hwnd) return;
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

static double GetCurrentFrameForView(const ThreadParam* tp)
{
    if (!tp) return 0.0;
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
    double visibleFrames = (std::max)(1.0, static_cast<double>(totalFrames) / tp->zoomFactor);
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
    if (!tp || !tp->mciOpened) return;
    wchar_t buf[64] = { 0 };
    std::wstring cmd = L"status " + tp->mciAlias + L" position";
    mciSendStringW(cmd.c_str(), buf, (UINT)std::size(buf), NULL);
    unsigned long posMs = 0;
    if (buf[0] != 0)
        posMs = static_cast<unsigned long>(wcstoul(buf, nullptr, 10));
    tp->pausedSampleIndex = static_cast<size_t>((posMs * tp->sampleRate) / 1000ULL);
}

static void SeekToFrame(ThreadParam* tp, size_t frame, bool resumePlayback)
{
    if (!tp) return;
    const size_t totalFrames = GetTotalFrames(tp);
    if (totalFrames == 0 || tp->sampleRate <= 0) return;

    frame = (std::min)(frame, totalFrames - 1);
    tp->pausedSampleIndex = frame;

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

static void PrepareColorWaveEnvelopes(ThreadParam* tp)
{
    if (!tp)
        return;

    tp->baseMinF.clear(); tp->baseMaxF.clear();
    tp->lowMinF.clear();  tp->lowMaxF.clear();
    tp->midMinF.clear();  tp->midMaxF.clear();
    tp->highMinF.clear(); tp->highMaxF.clear();
    tp->envBlocks = 0;

    std::vector<float> disp = BuildMonoDisplayFrames(tp);
    if (disp.empty())
        return;

    std::vector<float> low = OnePoleLowpass(disp, tp->sampleRate, 200.0);
    std::vector<float> hpLow = OnePoleHighpass(disp, tp->sampleRate, 200.0);
    std::vector<float> mid = OnePoleLowpass(hpLow, tp->sampleRate, 2000.0);
    std::vector<float> high = OnePoleHighpass(disp, tp->sampleRate, 2000.0);

    for (float& v : low)  v = ClampFloat(v, -tp->displayHeadroom, tp->displayHeadroom);
    for (float& v : mid)  v = ClampFloat(v, -tp->displayHeadroom, tp->displayHeadroom);
    for (float& v : high) v = ClampFloat(v, -tp->displayHeadroom, tp->displayHeadroom);

    EnvelopeMinMax(disp, tp->envBlock, tp->baseMinF, tp->baseMaxF);
    EnvelopeMinMax(low,  tp->envBlock, tp->lowMinF,  tp->lowMaxF);
    EnvelopeMinMax(mid,  tp->envBlock, tp->midMinF,  tp->midMaxF);
    EnvelopeMinMax(high, tp->envBlock, tp->highMinF, tp->highMaxF);

    tp->envBlocks = tp->baseMinF.size();
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

    HBRUSH brush = CreateSolidBrush(color);

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
            FillRect(hdc, &rPos, brush);
        }
        if (yNeg != midY)
        {
            RECT rNeg{
                x0,
                (std::min)(midY, yNeg),
                x1 + 1,
                (std::max)(midY, yNeg) + 1
            };
            FillRect(hdc, &rNeg, brush);
        }
    }

    DeleteObject(brush);
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
    PostMessageW(tp->hwnd, WM_WAVEFORM_TICK, 0, 0);
}

static void HandleRenderTick(HWND hwnd, ThreadParam* tp)
{
    if (!tp) return;

    if (tp->playing.load() && tp->samples)
    {
        size_t totalFrames = tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
        auto now = std::chrono::steady_clock::now();
        const double elapsedSec = std::chrono::duration<double>(now - tp->playStartTime).count();
        size_t curFrame = static_cast<size_t>(elapsedSec * tp->sampleRate);
        if (curFrame >= totalFrames)
        {
            tp->playing.store(false);
            tp->pausedSampleIndex = 0;
        }
    }

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

static void DrawTimeMarksOverlay(HDC hdc, const RECT& waveRc, double tLeft, double tRight)
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

        std::wstring label = FormatTimeLabel(t, showMillis);
        TextOutW(hdc, x + 2, (std::max)(waveRc.top + 2, waveRc.bottom - 20), label.c_str(), static_cast<int>(label.size()));
    }

    SelectObject(hdc, oldPen);
    DeleteObject(tickPen);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto tp = reinterpret_cast<ThreadParam*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_CREATE:
    {
        HWND hPlay = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 10, 60, 24, hwnd, (HMENU)1001, GetModuleHandle(NULL), NULL);
        HWND hPause = CreateWindowW(L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            80, 10, 60, 24, hwnd, (HMENU)1002, GetModuleHandle(NULL), NULL);

        HFONT hGuiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        if (hPlay)
        {
            SendMessageW(hPlay, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            InvalidateRect(hPlay, NULL, TRUE);
            UpdateWindow(hPlay);
        }
        if (hPause)
        {
            SendMessageW(hPause, WM_SETFONT, (WPARAM)hGuiFont, TRUE);
            InvalidateRect(hPause, NULL, TRUE);
            UpdateWindow(hPause);
        }

        return 0;
    }

    case WM_SIZE:
    {
        if (tp) tp->cacheDirty = true;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (!tp) break;
        if (id == 1001) // Play
        {
            if (tp->playing.load())
                break;

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
            InvalidateWaveRegion(hwnd, tp);
        }
        else if (id == 1002) // Pause
        {
            if (!tp->mciOpened) break;
            SyncPausedFromMciPosition(tp);

            // `pause` is unreliable for some waveaudio devices; stop + resume-from-position is robust.
            std::wstring cmd = L"stop " + tp->mciAlias;
            mciSendStringW(cmd.c_str(), NULL, 0, NULL);

            tp->playing.store(false);
            InvalidateWaveRegion(hwnd, tp);
        }
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (!tp) break;
        SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        double factor = (delta > 0) ? 1.15 : 1.0 / 1.15;
        tp->zoomFactor = std::clamp(tp->zoomFactor * factor, 1.0, 256.0);
        tp->cacheDirty = true;
        InvalidateWaveRegion(hwnd, tp);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        size_t targetFrame = 0;
        if (TryMapWavePointToFrame(hwnd, tp, pt.x, pt.y, targetFrame))
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
            SeekToFrame(tp, targetFrame, false);
            InvalidateWaveRegion(hwnd, tp);
            return 0;
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc{}; GetClientRect(hwnd, &rc);
        RECT waveRc = ComputeWaveRect(rc, tp);
        if (PtInRect(&waveRc, pt))
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
        if ((wParam & MK_LBUTTON) && tp->dragScrubActive)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            size_t targetFrame = 0;
            if (TryMapWavePointToFrame(hwnd, tp, pt.x, pt.y, targetFrame))
            {
                tp->dragLastX = pt.x;
                SeekToFrame(tp, targetFrame, false);
                InvalidateWaveRegion(hwnd, tp);
            }
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
            double visibleFrames = (totalFrames > 0) ? ((double)totalFrames / tp->zoomFactor) : 1.0;
            double framesPerPx = visibleFrames / (double)waveW;
            tp->panOffsetSamples -= static_cast<long long>(dx * framesPerPx);
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

    case WM_RBUTTONUP:
    {
        if (tp) tp->dragPanActive = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
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
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC wndDC = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT paintRc = ps.rcPaint;
        const int fullW = rc.right - rc.left;
        const int fullH = rc.bottom - rc.top;

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

        HBRUSH bg = CreateSolidBrush(RGB(18, 18, 18));
        FillRect(hdc, &paintRc, bg);
        DeleteObject(bg);

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
            double curFrameD = 0.0;
            if (tp->playing.load())
            {
                auto now = std::chrono::steady_clock::now();
                const double elapsedSec = std::chrono::duration<double>(now - tp->playStartTime).count();
                curFrameD = elapsedSec * tp->sampleRate;
            }
            else
            {
                curFrameD = static_cast<double>(tp->pausedSampleIndex);
            }

            double centerFrame = curFrameD + (double)tp->panOffsetSamples;
            if (centerFrame < 0) centerFrame = 0;
            if (centerFrame >= (double)totalFrames) centerFrame = (double)totalFrames - 1.0;

            double visibleFrames = std::max(1.0, (double)totalFrames / tp->zoomFactor);
            double startFrame = centerFrame - tp->playheadXRatio * visibleFrames;
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
                size_t b0 = static_cast<size_t>(std::floor(startFrame / static_cast<double>(tp->envBlock)));
                size_t b1 = static_cast<size_t>(std::ceil(endFrame / static_cast<double>(tp->envBlock)));
                if (b0 > tp->envBlocks) b0 = tp->envBlocks;
                if (b1 > tp->envBlocks) b1 = tp->envBlocks;

                // Draw in the same stacking order as aubioTest.py: base -> low -> mid -> high.
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(120, 120, 120),
                    tp->baseMinF, tp->baseMaxF, b0, b1, tp->envBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(0, 140, 255),
                    tp->lowMinF, tp->lowMaxF, b0, b1, tp->envBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(255, 170, 0),
                    tp->midMinF, tp->midMaxF, b0, b1, tp->envBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
                DrawEnvelopeLayer(hdc, waveRc, midY, ampScale, RGB(255, 60, 140),
                    tp->highMinF, tp->highMaxF, b0, b1, tp->envBlock, totalFrames, startFrame, visibleFrames, tp->plotYRange);
            }

            const double tLeft = startFrame / static_cast<double>(tp->sampleRate);
            const double tRight = (startFrame + visibleFrames) / static_cast<double>(tp->sampleRate);
            DrawTimeMarksOverlay(hdc, waveRc, tLeft, tRight);
            DrawBeatGridOverlay(hdc, waveRc, tp, startFrame, visibleFrames, totalFrames, tp->sampleRate);

            // draw playhead marker
            int playheadX = waveRc.left + static_cast<int>(tp->playheadXRatio * (double)w);
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

            wchar_t buf1[320];
            swprintf_s(buf1,
                L"t=%s / %s  frame=%zu  zoom=%.2fx  play=%s  src=%s  grid=%s",
                curTime.c_str(),
                totalTime.c_str(),
                static_cast<size_t>((curFrameD < 0.0) ? 0 : static_cast<size_t>(curFrameD)),
                tp->zoomFactor,
                tp->playing.load() ? L"ON" : L"off",
                tp->isStereo ? L"Stereo" : L"Mono",
                tp->gridEnabled ? L"ON" : L"off");

            wchar_t buf2[512];
            swprintf_s(buf2,
                L"BPM=%.3f  T=%.6fs  t0=%.6fs  beats/bar=%d  start=%.3fs  onset=%.3fs  kick=%.6fs  view=[%.3f..%.3f]s",
                tp->gridBpm,
                beatPeriod,
                tp->gridT0Seconds,
                tp->gridBeatsPerBar,
                tp->gridAudioStartSeconds,
                tp->gridApproxOnsetSeconds,
                tp->gridKickAttackSeconds,
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
        }

        if (memDC && memBmp)
        {
            const int bw = (std::max)(0, static_cast<int>(paintRc.right - paintRc.left));
            const int bh = (std::max)(0, static_cast<int>(paintRc.bottom - paintRc.top));
            if (bw > 0 && bh > 0)
            {
                BitBlt(wndDC, paintRc.left, paintRc.top, bw, bh, memDC, paintRc.left, paintRc.top, SRCCOPY);
            }
        }

        if (memDC)
        {
            if (oldBmp) SelectObject(memDC, oldBmp);
            if (memBmp) DeleteObject(memBmp);
            DeleteDC(memDC);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (tp)
        {
            tp->hwnd = nullptr;
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
    if (tp->autoStart && !tp->tempPath.empty())
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

void WaveformWindow::ShowWaveformAsyncRefPlayStereo(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const std::wstring& title)
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

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

void WaveformWindow::ShowWaveformAsyncRefPlayStereoGrid(std::vector<short>* interleavedStereoSamples, int sampleRate, bool startPlaying, const GridOverlayConfig& grid, const std::wstring& title)
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
    tp->gridEnabled = grid.enabled;
    tp->gridBpm = grid.bpm;
    tp->gridT0Seconds = grid.t0Seconds;
    tp->gridBeatsPerBar = grid.beatsPerBar;
    tp->gridAudioStartSeconds = grid.audioStartSeconds;
    tp->gridApproxOnsetSeconds = grid.approxOnsetSeconds;
    tp->gridKickAttackSeconds = grid.kickAttackSeconds;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}
