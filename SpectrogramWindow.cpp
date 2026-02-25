// SpectrogramWindow.cpp
// NOTE: This is implemented as a real-time spectrum analyzer (FFT magnitude vs frequency)
// synced to the active WaveformWindow playback state.

#include "SpectrogramWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <timeapi.h>
#pragma comment(lib, "Winmm.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "DSP.h"
#include "PianoSpectrogramUI.h"

namespace
{
    constexpr UINT WM_SPECTROGRAM_TICK = WM_APP + 49;
    constexpr int kControlStripHeightPx = 40;
    constexpr int kLeftAxisW = 52;
    constexpr int kBottomAxisH = 22;
    constexpr int kProcessedToggleW = 146;
    constexpr int kProcessedToggleH = 18;
    constexpr double kDbFloor = -84.0;
    constexpr double kDbCeil = 0.0;

    HFONT GetSpectrogramUiMessageFont()
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

    std::atomic<HWND> gPianoSpecWindowHwnd{ nullptr };
    std::atomic<bool> gPianoSpecWindowOpenOrLaunching{ false };

    enum PianoSpecGridMode
    {
        kPianoSpecGrid_None = 0,
        kPianoSpecGrid_1_6_Step,
        kPianoSpecGrid_1_4_Step,
        kPianoSpecGrid_1_3_Step,
        kPianoSpecGrid_1_2_Step,
        kPianoSpecGrid_Step,
        kPianoSpecGrid_1_6_Beat,
        kPianoSpecGrid_1_4_Beat,
        kPianoSpecGrid_1_3_Beat,
        kPianoSpecGrid_1_2_Beat,
        kPianoSpecGrid_Beat,
        kPianoSpecGrid_Bar,
        kPianoSpecGrid_Count
    };
}

struct SpectrogramThreadParam
{
    std::vector<short>* samples = nullptr; // caller-owned
    int sampleRate = 44100;
    bool isStereo = true;
    std::wstring title;
    WaveformWindow::GridOverlayConfig grid;

    HWND hwnd = nullptr;
    DWORD renderIntervalMs = 16;
    HANDLE renderTimerQueue = nullptr;
    HANDLE renderTimer = nullptr;
    std::atomic<bool> renderTickQueued{ false };

    // 4096 @ 44.1kHz ~= 10.77 Hz/bin and ~92.9 ms window.
    // This is a better compromise for a responsive spectrum analyzer.
    int nfft = 4096;
    double minFreqHz = 20.0;
    double maxFreqHz = 16000.0;
    double spectrumNormPeakEma = 1e-6;

    dsp::FftwR2C fft;
    std::vector<double> window;
    std::vector<double> smoothedDb; // per FFT bin
    std::vector<double> analysisMono; // current FFT input window (mono, normalized)

    std::size_t totalFrames = 0;
    bool useProcessedPlaybackMix = false;
};

struct SpectrumSyncView
{
    bool hasSync = false;
    bool playing = false;
    double currentFrame = 0.0;
    double playbackRate = 1.0;
    double currentSeconds = 0.0;
    double totalSeconds = 0.0;
};

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
    ms = (std::max)(0, (std::min)(999, ms));
    wchar_t buf[32];
    swprintf_s(buf, L"%d:%02d.%03d", mins, secs, ms);
    return std::wstring(buf);
}

static std::size_t GetTotalFrames(const SpectrogramThreadParam* tp)
{
    if (!tp || !tp->samples) return 0;
    return tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
}

static RECT ComputeControlRect(const RECT& clientRc)
{
    RECT r = clientRc;
    r.bottom = (LONG)(std::min)(static_cast<int>(clientRc.bottom), static_cast<int>(clientRc.top) + kControlStripHeightPx);
    return r;
}

static RECT ComputeGraphOuterRect(const RECT& clientRc)
{
    RECT r = clientRc;
    r.top = (LONG)(static_cast<int>(clientRc.top) + kControlStripHeightPx);
    return r;
}

static RECT ComputePlotRect(const RECT& clientRc)
{
    RECT outer = ComputeGraphOuterRect(clientRc);
    RECT plot = outer;
    plot.left += kLeftAxisW;
    plot.bottom -= kBottomAxisH;
    return plot;
}

static RECT ComputeFreqAxisRect(const RECT& clientRc)
{
    RECT outer = ComputeGraphOuterRect(clientRc);
    RECT r = outer;
    r.left += kLeftAxisW;
    r.top = (LONG)(std::max)(static_cast<int>(outer.top), static_cast<int>(outer.bottom) - kBottomAxisH);
    return r;
}

static RECT ComputeDbAxisRect(const RECT& clientRc)
{
    RECT outer = ComputeGraphOuterRect(clientRc);
    RECT r = outer;
    r.right = (LONG)(std::min)(static_cast<int>(outer.right), static_cast<int>(outer.left) + kLeftAxisW);
    r.bottom -= kBottomAxisH;
    return r;
}

static RECT ComputeProcessedToggleRect(const RECT& clientRc)
{
    RECT control = ComputeControlRect(clientRc);
    RECT r{};
    r.right = control.right - 8;
    r.left = (LONG)(std::max)(control.left + 8, r.right - kProcessedToggleW);
    r.top = control.top + 2;
    r.bottom = (LONG)(std::min)(control.bottom - 2, r.top + kProcessedToggleH);
    return r;
}

static void EnsureFftReady(SpectrogramThreadParam* tp)
{
    if (!tp) return;
    if (tp->nfft < 256) tp->nfft = 256;
    if (tp->fft.nfft() != tp->nfft)
    {
        tp->fft.init(tp->nfft);
        tp->window.assign(static_cast<std::size_t>(tp->nfft), 0.0);
        for (int i = 0; i < tp->nfft; ++i)
            tp->window[static_cast<std::size_t>(i)] = dsp::hann(i, tp->nfft);
        tp->smoothedDb.assign(static_cast<std::size_t>(tp->nfft / 2 + 1), kDbFloor);
        tp->analysisMono.assign(static_cast<std::size_t>(tp->nfft), 0.0);
    }
}

static inline double SampleMonoAtFrame(const SpectrogramThreadParam* tp, long long frame)
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

static SpectrumSyncView GetSyncView(const SpectrogramThreadParam* tp)
{
    SpectrumSyncView v{};
    if (!tp) return v;

    v.totalSeconds = (tp->sampleRate > 0 && tp->totalFrames > 0)
        ? (double)tp->totalFrames / (double)tp->sampleRate
        : 0.0;

    WaveformWindow::PlaybackSyncSnapshot snap{};
    if (WaveformWindow::GetPlaybackSyncSnapshot(snap) &&
        snap.valid &&
        snap.sampleRate == tp->sampleRate &&
        snap.totalFrames > 0)
    {
        v.hasSync = true;
        v.playing = snap.playing;
        const double maxFrame = (snap.totalFrames > 0) ? (double)(snap.totalFrames - 1) : 0.0;
        v.currentFrame = std::clamp(snap.currentFrame, 0.0, maxFrame);
        v.playbackRate = (std::isfinite(snap.playbackRate) && snap.playbackRate > 0.0) ? snap.playbackRate : 1.0;
        v.currentSeconds = (tp->sampleRate > 0) ? (v.currentFrame / (double)tp->sampleRate) : 0.0;
        return v;
    }

    v.hasSync = false;
    v.playing = false;
    v.currentFrame = 0.0;
    v.playbackRate = 1.0;
    v.currentSeconds = 0.0;
    return v;
}

static void ComputeSpectrumDbAtFrame(SpectrogramThreadParam* tp, double centerFrameD)
{
    if (!tp) return;
    EnsureFftReady(tp);
    if (tp->fft.nfft() <= 0 || tp->totalFrames == 0 || tp->sampleRate <= 0) return;

    const int nfft = tp->fft.nfft();
    const int bins = nfft / 2 + 1;
    if ((int)tp->smoothedDb.size() != bins)
        tp->smoothedDb.assign((std::size_t)bins, kDbFloor);
    if ((int)tp->analysisMono.size() != nfft)
        tp->analysisMono.assign((std::size_t)nfft, 0.0);

    bool usedProcessed = false;
    if (tp->useProcessedPlaybackMix)
    {
        usedProcessed = WaveformWindow::BuildPlaybackSpectrogramMonoWindow(centerFrameD, nfft, tp->analysisMono);
    }
    if (!usedProcessed)
    {
        const long long center = (long long)std::llround(centerFrameD);
        const long long half = nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const long long src = center + i - half;
            tp->analysisMono[static_cast<std::size_t>(i)] = SampleMonoAtFrame(tp, src);
        }
    }

    double mean = 0.0;
    for (int i = 0; i < nfft; ++i)
    {
        mean += tp->analysisMono[static_cast<std::size_t>(i)];
    }
    mean /= (double)nfft;

    for (int i = 0; i < nfft; ++i)
    {
        const double s = tp->analysisMono[static_cast<std::size_t>(i)] - mean; // remove DC offset before FFT
        tp->fft.in()[i] = s * tp->window[static_cast<std::size_t>(i)];
    }
    tp->fft.execute();

    const fftw_complex* X = tp->fft.out();
    const double nyquist = 0.5 * tp->sampleRate;
    const double fMin = std::max(1.0, tp->minFreqHz);
    const double fMax = std::clamp(tp->maxFreqHz, fMin + 1.0, nyquist);
    const double hzPerBin = (double)tp->sampleRate / (double)nfft;

    double peakDisplayed = 1e-12;
    std::vector<double> mags((std::size_t)bins, 0.0);
    for (int k = 0; k < bins; ++k)
    {
        const double re = X[k][0];
        const double im = X[k][1];
        const double m = std::sqrt(re * re + im * im);
        mags[(std::size_t)k] = m;
        const double f = (double)k * hzPerBin;
        if (f >= fMin && f <= fMax && m > peakDisplayed)
            peakDisplayed = m;
    }

    // Auto-gain reference with smoothing to reduce flicker from frame-to-frame normalization.
    {
        const double prev = (std::max)(tp->spectrumNormPeakEma, 1e-12);
        const double target = (std::max)(peakDisplayed, 1e-12);
        const double a = (target > prev) ? 0.35 : 0.08;
        tp->spectrumNormPeakEma = a * target + (1.0 - a) * prev;
    }
    const double normPeak = (std::max)(tp->spectrumNormPeakEma, 1e-12);

    // Smooth a bit to reduce flicker/jitter but still feel reactive.
    const double attack = 0.45;
    const double release = 0.18;
    for (int k = 0; k < bins; ++k)
    {
        double db = 20.0 * std::log10((mags[(std::size_t)k] / normPeak) + 1e-12);
        db = std::clamp(db, kDbFloor, kDbCeil);

        double prev = tp->smoothedDb[(std::size_t)k];
        const double a = (db > prev) ? attack : release;
        tp->smoothedDb[(std::size_t)k] = a * db + (1.0 - a) * prev;
    }
}

static double FreqToXNormLog(double f, double fMin, double fMax)
{
    f = std::clamp(f, fMin, fMax);
    const double l0 = std::log10(fMin);
    const double l1 = std::log10(fMax);
    if (!(l1 > l0)) return 0.0;
    return std::clamp((std::log10(f) - l0) / (l1 - l0), 0.0, 1.0);
}

static double XNormToFreqLog(double xNorm, double fMin, double fMax)
{
    xNorm = std::clamp(xNorm, 0.0, 1.0);
    const double l0 = std::log10(fMin);
    const double l1 = std::log10(fMax);
    return std::pow(10.0, l0 + xNorm * (l1 - l0));
}

static int DbToY(double db, const RECT& plotRc)
{
    const int h = (std::max)(1, static_cast<int>(plotRc.bottom - plotRc.top));
    db = std::clamp(db, kDbFloor, kDbCeil);
    const double n = (db - kDbFloor) / (kDbCeil - kDbFloor);
    const double y = (double)plotRc.bottom - 1.0 - n * (double)(h - 1);
    return (int)std::lround(y);
}

static void DrawGridAndAxes(HDC hdc, const RECT& clientRc, const RECT& plotRc, int sampleRate, int nfft)
{
    RECT controlRc = ComputeControlRect(clientRc);
    RECT graphOuter = ComputeGraphOuterRect(clientRc);
    RECT dbAxisRc = ComputeDbAxisRect(clientRc);
    RECT freqAxisRc = ComputeFreqAxisRect(clientRc);

    HBRUSH bg = CreateSolidBrush(RGB(18, 18, 18));
    FillRect(hdc, &clientRc, bg);
    DeleteObject(bg);

    HBRUSH controlBg = CreateSolidBrush(RGB(34, 34, 34));
    FillRect(hdc, &controlRc, controlBg);
    DeleteObject(controlBg);

    HBRUSH outerBg = CreateSolidBrush(RGB(14, 14, 14));
    FillRect(hdc, &graphOuter, outerBg);
    DeleteObject(outerBg);

    HBRUSH plotBg = CreateSolidBrush(RGB(10, 10, 10));
    FillRect(hdc, &plotRc, plotBg);
    DeleteObject(plotBg);

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);

    // dB horizontal grid
    HPEN hGridPen = CreatePen(PS_SOLID, 1, RGB(44, 44, 44));
    HPEN hGridStrong = CreatePen(PS_SOLID, 1, RGB(64, 64, 64));
    HGDIOBJ oldPen = SelectObject(hdc, hGridPen);
    SetTextColor(hdc, RGB(180, 180, 180));
    const int dbMarks[] = { -84, -72, -60, -48, -36, -24, -12, 0 };
    for (int db : dbMarks)
    {
        const int y = DbToY((double)db, plotRc);
        SelectObject(hdc, (db == 0 || db == -24 || db == -48 || db == -72) ? hGridStrong : hGridPen);
        MoveToEx(hdc, plotRc.left, y, NULL);
        LineTo(hdc, plotRc.right, y);

        wchar_t lbl[16];
        swprintf_s(lbl, L"%ddB", db);
        TextOutW(hdc, (int)dbAxisRc.left + 4, y - 7, lbl, (int)wcslen(lbl));
    }

    // Frequency vertical grid (log)
    const double nyquist = (sampleRate > 0) ? (0.5 * sampleRate) : 22050.0;
    const double fMin = 20.0;
    const double fMax = std::clamp(16000.0, fMin + 1.0, nyquist);
    const double freqMarks[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 16000 };
    for (double f : freqMarks)
    {
        if (f < fMin || f > fMax) continue;
        const double xn = FreqToXNormLog(f, fMin, fMax);
        int x = plotRc.left + (int)std::lround(xn * (double)(plotRc.right - plotRc.left - 1));
        x = (std::max)(static_cast<int>(plotRc.left), (std::min)(static_cast<int>(plotRc.right - 1), x));

        SelectObject(hdc, (f == 100 || f == 1000 || f == 10000) ? hGridStrong : hGridPen);
        MoveToEx(hdc, x, plotRc.top, NULL);
        LineTo(hdc, x, plotRc.bottom);

        wchar_t flbl[16];
        if (f >= 1000.0)
            swprintf_s(flbl, L"%.0fk", f / 1000.0);
        else
            swprintf_s(flbl, L"%.0f", f);
        TextOutW(hdc, x + 2, (int)freqAxisRc.top + 3, flbl, (int)wcslen(flbl));
    }

    // axis separators
    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    SelectObject(hdc, sepPen);
    MoveToEx(hdc, plotRc.left, plotRc.top, NULL);
    LineTo(hdc, plotRc.left, plotRc.bottom);
    MoveToEx(hdc, plotRc.left, plotRc.bottom, NULL);
    LineTo(hdc, plotRc.right, plotRc.bottom);

    SelectObject(hdc, oldPen);
    DeleteObject(hGridPen);
    DeleteObject(hGridStrong);
    DeleteObject(sepPen);
}

static void DrawSpectrumCurve(HDC hdc, SpectrogramThreadParam* tp, const RECT& plotRc)
{
    if (!tp || tp->sampleRate <= 0 || tp->fft.nfft() <= 0 || tp->smoothedDb.empty())
        return;

    const int w = (std::max)(1, static_cast<int>(plotRc.right - plotRc.left));
    const int h = (std::max)(1, static_cast<int>(plotRc.bottom - plotRc.top));
    if (w < 2 || h < 2) return;

    const int bins = tp->fft.nfft() / 2 + 1;
    if (bins <= 2) return;

    const double nyquist = 0.5 * tp->sampleRate;
    const double hzPerBin = (double)tp->sampleRate / (double)tp->fft.nfft();
    const double fMin = 20.0;
    const double fMax = std::clamp(tp->maxFreqHz, fMin + 1.0, nyquist);

    // Build a fine line from actual FFT bins (log-frequency mapped), compressing bins that land on
    // the same pixel by keeping the highest magnitude (lowest y).
    std::vector<POINT> linePts;
    linePts.reserve((size_t)w + 8);

    int lastX = INT_MIN;
    int bestYForX = plotRc.bottom - 1;
    for (int b = 1; b < bins; ++b)
    {
        const double f = (double)b * hzPerBin;
        if (f < fMin) continue;
        if (f > fMax) break;

        const double xn = FreqToXNormLog(f, fMin, fMax);
        int x = plotRc.left + (int)std::lround(xn * (double)(w - 1));
        x = (std::max)(static_cast<int>(plotRc.left), (std::min)(static_cast<int>(plotRc.right - 1), x));

        const int y = DbToY(tp->smoothedDb[(size_t)b], plotRc);

        if (x != lastX)
        {
            if (lastX != INT_MIN)
                linePts.push_back(POINT{ lastX, bestYForX });
            lastX = x;
            bestYForX = y;
        }
        else
        {
            // lower y = louder; keep the peak bin that maps to this pixel
            bestYForX = (std::min)(bestYForX, y);
        }
    }
    if (lastX != INT_MIN)
        linePts.push_back(POINT{ lastX, bestYForX });

    if (linePts.size() < 2)
        return;

    // Smooth-looking fill under the curve using a polygon instead of per-pixel rectangles.
    std::vector<POINT> polyPts;
    polyPts.reserve(linePts.size() + 2);
    polyPts.insert(polyPts.end(), linePts.begin(), linePts.end());
    polyPts.push_back(POINT{ linePts.back().x, plotRc.bottom - 1 });
    polyPts.push_back(POINT{ linePts.front().x, plotRc.bottom - 1 });

    HBRUSH fillBrush = CreateSolidBrush(RGB(150, 150, 150));
    HPEN fillPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 150));
    HPEN glowPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
    HPEN linePen = CreatePen(PS_SOLID, 1, RGB(235, 235, 235));

    HGDIOBJ oldBrush = SelectObject(hdc, fillBrush);
    HGDIOBJ oldPen = SelectObject(hdc, fillPen);
    Polygon(hdc, polyPts.data(), (int)polyPts.size());

    SelectObject(hdc, glowPen);
    Polyline(hdc, linePts.data(), (int)linePts.size());
    SelectObject(hdc, linePen);
    Polyline(hdc, linePts.data(), (int)linePts.size());

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);

    DeleteObject(fillBrush);
    DeleteObject(fillPen);
    DeleteObject(glowPen);
    DeleteObject(linePen);
}

static void DrawWindow(HDC hdc, const RECT& clientRc, SpectrogramThreadParam* tp)
{
    RECT plotRc = ComputePlotRect(clientRc);
    if (plotRc.right <= plotRc.left || plotRc.bottom <= plotRc.top)
    {
        HBRUSH bg = CreateSolidBrush(RGB(18, 18, 18));
        FillRect(hdc, &clientRc, bg);
        DeleteObject(bg);
        return;
    }

    SpectrumSyncView view = GetSyncView(tp);
    if (tp)
        ComputeSpectrumDbAtFrame(tp, view.currentFrame);

    DrawGridAndAxes(hdc, clientRc, plotRc, tp ? tp->sampleRate : 44100, tp ? tp->nfft : 2048);
    if (tp)
        DrawSpectrumCurve(hdc, tp, plotRc);

    RECT controlRc = ComputeControlRect(clientRc);
    SetBkMode(hdc, TRANSPARENT);
    HFONT oldUiFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetTextColor(hdc, RGB(235, 235, 235));

    std::wstring title = (tp && !tp->title.empty()) ? tp->title : L"Spectrum Analyzer";

    wchar_t hud[512];
    swprintf_s(hud,
        L"sync=%s  play=%s  mode=%s  rate=%.2fx  t=%s / %s  sr=%d  nfft=%d  freq=20Hz..%.0fHz  bpm=%.2f",
        view.hasSync ? L"ON" : L"off",
        view.playing ? L"ON" : L"off",
        (tp && tp->useProcessedPlaybackMix) ? L"PROC" : L"RAW",
        view.playbackRate,
        FormatTimeLabel(view.currentSeconds, true).c_str(),
        FormatTimeLabel(view.totalSeconds, true).c_str(),
        tp ? tp->sampleRate : 0,
        tp ? tp->nfft : 0,
        tp ? tp->maxFreqHz : 0.0,
        (tp && tp->grid.enabled) ? tp->grid.bpm : 0.0);

    const RECT toggleRc = ComputeProcessedToggleRect(clientRc);
    RECT titleRc = controlRc;
    titleRc.left += 8;
    titleRc.right = (LONG)(std::min)(titleRc.right - 8, toggleRc.left - 8);
    titleRc.top += 2;
    titleRc.bottom = (LONG)(titleRc.top + 16);
    DrawTextW(hdc, title.c_str(), -1, &titleRc,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT hudRc = controlRc;
    hudRc.left += 8;
    hudRc.right = (LONG)(std::min)(hudRc.right - 8, toggleRc.left - 8);
    hudRc.top += 18;
    hudRc.bottom -= 2;
    SetTextColor(hdc, RGB(200, 200, 200));
    DrawTextW(hdc, hud, -1, &hudRc,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    if (toggleRc.right > toggleRc.left && toggleRc.bottom > toggleRc.top)
    {
        RECT boxRc = toggleRc;
        boxRc.left += 2;
        boxRc.top += 2;
        boxRc.right = boxRc.left + 12;
        boxRc.bottom = boxRc.top + 12;

        HBRUSH boxBg = CreateSolidBrush(RGB(22, 22, 22));
        FillRect(hdc, &boxRc, boxBg);
        DeleteObject(boxBg);
        HPEN boxPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
        HGDIOBJ oldPen = SelectObject(hdc, boxPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom);

        const bool checked = tp && tp->useProcessedPlaybackMix;
        if (checked)
        {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(80, 210, 220));
            HGDIOBJ prevTickPen = SelectObject(hdc, tickPen);
            MoveToEx(hdc, boxRc.left + 2, boxRc.top + 7, NULL);
            LineTo(hdc, boxRc.left + 5, boxRc.bottom - 3);
            LineTo(hdc, boxRc.right - 2, boxRc.top + 3);
            SelectObject(hdc, prevTickPen);
            DeleteObject(tickPen);
        }

        RECT lblRc = toggleRc;
        lblRc.left = boxRc.right + 6;
        SetTextColor(hdc, checked ? RGB(220, 240, 242) : RGB(180, 180, 180));
        DrawTextW(hdc, L"Processed Mix", -1, &lblRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(boxPen);
    }
    SelectObject(hdc, oldUiFont);
}

static void CALLBACK RenderTimerCallback(PVOID lpParameter, BOOLEAN /*timerOrWaitFired*/)
{
    auto* tp = reinterpret_cast<SpectrogramThreadParam*>(lpParameter);
    if (!tp || !tp->hwnd) return;
    bool expected = false;
    if (!tp->renderTickQueued.compare_exchange_strong(expected, true))
        return;
    PostMessageW(tp->hwnd, WM_SPECTROGRAM_TICK, 0, 0);
}

static void HandleRenderTick(SpectrogramThreadParam* tp)
{
    if (!tp || !tp->hwnd) return;
    tp->renderTickQueued.store(false);
    InvalidateRect(tp->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto* tp = reinterpret_cast<SpectrogramThreadParam*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_CREATE:
        return 0;

    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (!tp) break;
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const RECT toggleRc = ComputeProcessedToggleRect(rc);
        if (PtInRect(&toggleRc, pt))
        {
            tp->useProcessedPlaybackMix = !tp->useProcessedPlaybackMix;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SPECTROGRAM_TICK:
        HandleRenderTick(tp);
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            HandleRenderTick(tp);
            return 0;
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC wndDC = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        const int w = (std::max)(0, static_cast<int>(rc.right - rc.left));
        const int h = (std::max)(0, static_cast<int>(rc.bottom - rc.top));
        HDC hdc = wndDC;
        HDC memDC = NULL;
        HBITMAP memBmp = NULL;
        HBITMAP oldBmp = NULL;
        if (w > 0 && h > 0)
        {
            memDC = CreateCompatibleDC(wndDC);
            if (memDC)
            {
                memBmp = CreateCompatibleBitmap(wndDC, w, h);
                if (memBmp)
                {
                    oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                    hdc = memDC;
                }
            }
        }

        DrawWindow(hdc, rc, tp);

        if (memDC && memBmp)
        {
            const RECT paintRc = ps.rcPaint;
            const int bw = (std::max)(0, static_cast<int>(paintRc.right - paintRc.left));
            const int bh = (std::max)(0, static_cast<int>(paintRc.bottom - paintRc.top));
            if (bw > 0 && bh > 0)
                BitBlt(wndDC, paintRc.left, paintRc.top, bw, bh, memDC, paintRc.left, paintRc.top, SRCCOPY);
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
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
    std::unique_ptr<SpectrogramThreadParam> tp(reinterpret_cast<SpectrogramThreadParam*>(lpParameter));
    timeBeginPeriod(1);

    if (tp)
    {
        tp->totalFrames = GetTotalFrames(tp.get());
        EnsureFftReady(tp.get());
    }

    HINSTANCE hInstance = GetModuleHandleW(NULL);
    const wchar_t CLASS_NAME[] = L"SpectrogramWindowClass";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc))
    {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            timeEndPeriod(1);
            return 1;
        }
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        tp ? tp->title.c_str() : L"Spectrum Analyzer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 260,
        NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        timeEndPeriod(1);
        return 1;
    }

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
            WT_EXECUTEDEFAULT);
    }
    else
    {
        SetTimer(hwnd, 1, tp->renderIntervalMs, NULL);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    timeEndPeriod(1);
    return (DWORD)msg.wParam;
}

void SpectrogramWindow::ShowSpectrogramAsyncRefStereoSynced(
    std::vector<short>* interleavedStereoSamples,
    int sampleRate,
    const WaveformWindow::GridOverlayConfig& grid,
    const std::wstring& title)
{
    SpectrogramThreadParam* tp = new SpectrogramThreadParam();
    tp->samples = interleavedStereoSamples;
    tp->sampleRate = sampleRate;
    tp->isStereo = true;
    tp->grid = grid;
    tp->title = title.empty() ? L"Spectrum Analyzer" : title;

    HANDLE h = CreateThread(nullptr, 0, ThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
}

namespace
{
    constexpr UINT WM_PIANO_SPECTROGRAM_TICK = WM_APP + 50;
    constexpr int kPianoSpecControlStripHeightPx = 56;
    constexpr int kPianoSpecLeftScaleW = 94;
    constexpr int kPianoSpecKeyStripW = 18;
    constexpr int kPianoSpecBottomAxisH = 24;
    constexpr int kPianoSpecCheckboxW = 146;
    constexpr int kPianoSpecButtonW = 126;
    constexpr int kPianoSpecControlH = 22;

    struct PianoSpectrogramThreadParam
    {
        std::vector<short>* samples = nullptr; // caller-owned
        int sampleRate = 44100;
        bool isStereo = true;
        std::wstring title;
        WaveformWindow::GridOverlayConfig grid;

        HWND hwnd = nullptr;
        DWORD renderIntervalMs = 16;
        HANDLE renderTimerQueue = nullptr;
        HANDLE renderTimer = nullptr;
        std::atomic<bool> renderTickQueued{ false };

        bool useProcessedPlaybackMix = true;
        double dbRange = 84.0;
        int nfft = 4096;            // "Max res" control
        int nfftChoiceIndex = 2;
        int gridMode = kPianoSpecGrid_Beat;
        int midiMin = 12;           // C0
        int midiMax = 108;          // C8 default view; slider can move upward
        double historySeconds = 10.0;
        double minFreqHz = 20.0;

        dsp::FftwR2C fft;
        std::vector<double> window;
        std::vector<double> analysisMono; // current FFT input frame
        std::vector<double> rowDbScratch; // current column, per-pixel-row dB
        double fftAmpScale = 1.0;

        std::vector<uint32_t> imageBgra;  // plot image, top-down 32-bit BI_RGB pixels (0x00RRGGBB)
        int imageW = 0;
        int imageH = 0;
        bool historyDirty = true;
        double lastHistoryRightSeconds = std::numeric_limits<double>::quiet_NaN();
        double lastObservedSyncSeconds = std::numeric_limits<double>::quiet_NaN();
        std::size_t totalFrames = 0;

        RECT rcProcessedToggle{};
        RECT rcDbRangeButton{};
        RECT rcMaxResButton{};
        RECT rcGridButton{};
        bool gridMenuOpen = false;
        RECT rcGridMenu{};
        int gridMenuHoverIndex = -1;
        bool midiSliderDragActive = false;
    };

    static const int kPianoSpecNfftChoices[] = { 1024, 2048, 4096, 8192 };
    static const double kPianoSpecDbRanges[] = { 48.0, 60.0, 72.0, 84.0, 96.0, 108.0 };
}

static const wchar_t* PianoSpecGridModeLabel(int mode)
{
    switch (mode)
    {
    case kPianoSpecGrid_None: return L"None";
    case kPianoSpecGrid_1_6_Step: return L"1/6 step";
    case kPianoSpecGrid_1_4_Step: return L"1/4 step";
    case kPianoSpecGrid_1_3_Step: return L"1/3 step";
    case kPianoSpecGrid_1_2_Step: return L"1/2 step";
    case kPianoSpecGrid_Step: return L"Step";
    case kPianoSpecGrid_1_6_Beat: return L"1/6 beat";
    case kPianoSpecGrid_1_4_Beat: return L"1/4 beat";
    case kPianoSpecGrid_1_3_Beat: return L"1/3 beat";
    case kPianoSpecGrid_1_2_Beat: return L"1/2 beat";
    case kPianoSpecGrid_Beat: return L"Beat";
    case kPianoSpecGrid_Bar: return L"Bar";
    default: return L"Beat";
    }
}

static double PianoSpecGridModeBeats(int mode, int beatsPerBar)
{
    switch (mode)
    {
    case kPianoSpecGrid_1_6_Step: return 1.0 / 24.0;
    case kPianoSpecGrid_1_4_Step: return 1.0 / 16.0;
    case kPianoSpecGrid_1_3_Step: return 1.0 / 12.0;
    case kPianoSpecGrid_1_2_Step: return 1.0 / 8.0;
    case kPianoSpecGrid_Step: return 1.0 / 4.0;
    case kPianoSpecGrid_1_6_Beat: return 1.0 / 6.0;
    case kPianoSpecGrid_1_4_Beat: return 1.0 / 4.0;
    case kPianoSpecGrid_1_3_Beat: return 1.0 / 3.0;
    case kPianoSpecGrid_1_2_Beat: return 1.0 / 2.0;
    case kPianoSpecGrid_Beat: return 1.0;
    case kPianoSpecGrid_Bar: return (double)(std::max)(1, beatsPerBar);
    default: return 0.0;
    }
}

static void DrawPianoSpecMusicalGrid(HDC hdc, const RECT& plotRc, const RECT& timeRc,
    const PianoSpectrogramThreadParam* tp, double tLeft, double tRight)
{
    if (!hdc || !tp) return;
    if (!tp->grid.enabled || tp->grid.bpm <= 0.0) return;
    const int sharedGridMode = WaveformWindow::GetSharedPianoGridMode();
    if (sharedGridMode == kPianoSpecGrid_None) return;
    if (!std::isfinite(tLeft) || !std::isfinite(tRight) || !(tRight > tLeft)) return;

    const int plotW = (std::max)(0, (int)(plotRc.right - plotRc.left));
    if (plotW <= 0) return;
    const double visibleSeconds = tRight - tLeft;
    if (!(visibleSeconds > 0.0)) return;

    const double beatSec = 60.0 / tp->grid.bpm;
    if (!std::isfinite(beatSec) || beatSec <= 0.0) return;
    const int beatsPerBar = (std::max)(1, tp->grid.beatsPerBar);
    const double pxPerSec = (double)plotW / visibleSeconds;
    constexpr double kMinSubdivPx = 10.0;
    constexpr double kMinBeatPx = 12.0;
    constexpr double kMinBarPx = 14.0;

    struct Candidate { int mode; double beats; };
    Candidate cands[kPianoSpecGrid_Count - 1]{};
    int candCount = 0;
    for (int m = 1; m < kPianoSpecGrid_Count; ++m)
    {
        const double b = PianoSpecGridModeBeats(m, beatsPerBar);
        if (b > 0.0 && std::isfinite(b))
            cands[candCount++] = Candidate{ m, b };
    }
    std::sort(cands, cands + candCount, [](const Candidate& a, const Candidate& b)
    {
        if (std::fabs(a.beats - b.beats) > 1e-12) return a.beats < b.beats;
        return a.mode < b.mode;
    });

    const double requestedBeats = PianoSpecGridModeBeats(sharedGridMode, beatsPerBar);
    if (!(requestedBeats > 0.0)) return;
    double chosenBeats = cands[candCount - 1].beats;
    int chosenMode = cands[candCount - 1].mode;
    for (int i = 0; i < candCount; ++i)
    {
        if (cands[i].beats + 1e-12 < requestedBeats) continue;
        chosenBeats = cands[i].beats;
        chosenMode = cands[i].mode;
        if (cands[i].beats * beatSec * pxPerSec >= kMinSubdivPx)
            break;
    }

    auto drawLinesAtBeats = [&](double lineBeats, HPEN pen)
    {
        if (!(lineBeats > 0.0) || !std::isfinite(lineBeats)) return;
        const double lineSec = lineBeats * beatSec;
        if (!(lineSec > 0.0)) return;
        long long k0 = (long long)std::floor((tLeft - tp->grid.t0Seconds) / lineSec) - 2;
        long long k1 = (long long)std::ceil((tRight - tp->grid.t0Seconds) / lineSec) + 2;
        HGDIOBJ oldPenLocal = SelectObject(hdc, pen);
        for (long long k = k0; k <= k1; ++k)
        {
            const double tg = tp->grid.t0Seconds + (double)k * lineSec;
            if (tg < tLeft || tg > tRight) continue;
            const double xn = (tg - tLeft) / visibleSeconds;
            int x = plotRc.left + (int)std::lround(xn * (double)(plotW - 1));
            x = (std::max)((int)plotRc.left, (std::min)(x, (int)plotRc.right - 1));
            MoveToEx(hdc, x, plotRc.top, NULL);
            LineTo(hdc, x, plotRc.bottom);
        }
        SelectObject(hdc, oldPenLocal);
    };

    HPEN subdivPen = CreatePen(PS_SOLID, 1, (chosenBeats < 1.0) ? RGB(80, 72, 36) : RGB(110, 95, 30));
    drawLinesAtBeats(chosenBeats, subdivPen);
    DeleteObject(subdivPen);

    const double beatPx = beatSec * pxPerSec;
    if (chosenMode != kPianoSpecGrid_Beat && chosenMode != kPianoSpecGrid_Bar && beatPx >= kMinBeatPx)
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

static std::size_t GetTotalFrames(const PianoSpectrogramThreadParam* tp)
{
    if (!tp || !tp->samples) return 0;
    return tp->isStereo ? (tp->samples->size() / 2) : tp->samples->size();
}

static RECT ComputePianoSpecControlRect(const RECT& clientRc)
{
    RECT r = clientRc;
    r.bottom = (LONG)(std::min)(static_cast<int>(clientRc.bottom), static_cast<int>(clientRc.top) + kPianoSpecControlStripHeightPx);
    return r;
}

static RECT ComputePianoSpecGraphOuterRect(const RECT& clientRc)
{
    RECT r = clientRc;
    r.top = (LONG)(static_cast<int>(clientRc.top) + kPianoSpecControlStripHeightPx);
    return r;
}

static RECT ComputePianoSpecPlotRect(const RECT& clientRc)
{
    RECT outer = ComputePianoSpecGraphOuterRect(clientRc);
    RECT plot = outer;
    plot.left += kPianoSpecLeftScaleW;
    plot.bottom -= kPianoSpecBottomAxisH;
    return plot;
}

static RECT ComputePianoSpecBottomAxisRect(const RECT& clientRc)
{
    RECT outer = ComputePianoSpecGraphOuterRect(clientRc);
    RECT r = outer;
    r.left += kPianoSpecLeftScaleW;
    r.top = (LONG)(std::max)(static_cast<int>(outer.top), static_cast<int>(outer.bottom) - kPianoSpecBottomAxisH);
    return r;
}

static RECT ComputePianoSpecLeftScaleRect(const RECT& clientRc)
{
    RECT outer = ComputePianoSpecGraphOuterRect(clientRc);
    RECT r = outer;
    r.right = (LONG)(std::min)(static_cast<int>(outer.right), static_cast<int>(outer.left) + kPianoSpecLeftScaleW);
    r.bottom -= kPianoSpecBottomAxisH;
    return r;
}

static constexpr int kPianoSpecMidiSliderHardMin = 12;   // C0
static constexpr int kPianoSpecMidiSliderHardMax = 135;  // ~20kHz at 44.1kHz

static RECT ComputePianoSpecMidiSliderTrackRect(const RECT& leftRc)
{
    return PianoSpectrogramUI::ComputeMidiSliderTrackRect(leftRc);
}

static RECT ComputePianoSpecMidiSliderThumbRect(const PianoSpectrogramThreadParam* tp, const RECT& trackRc)
{
    if (!tp) return trackRc;
    return PianoSpectrogramUI::ComputeMidiSliderThumbRect(
        trackRc,
        tp->midiMin,
        tp->midiMax,
        kPianoSpecMidiSliderHardMin,
        kPianoSpecMidiSliderHardMax);
}

static bool SetPianoSpecMidiSliderFromY(PianoSpectrogramThreadParam* tp, const RECT& trackRc, int y)
{
    if (!tp) return false;
    int newMin = tp->midiMin;
    int newMax = tp->midiMax;
    if (!PianoSpectrogramUI::ComputeMidiSliderWindowFromY(
        trackRc, y,
        tp->midiMin, tp->midiMax,
        newMin, newMax,
        kPianoSpecMidiSliderHardMin, kPianoSpecMidiSliderHardMax))
        return false;

    if (newMin == tp->midiMin && newMax == tp->midiMax)
        return false;

    tp->midiMin = newMin;
    tp->midiMax = newMax;
    tp->historyDirty = true;
    return true;
}

static void LayoutPianoSpecControls(PianoSpectrogramThreadParam* tp, const RECT& clientRc)
{
    if (!tp) return;
    RECT ctrl = ComputePianoSpecControlRect(clientRc);
    const int cy = ctrl.top + (std::max)(0, ((int)(ctrl.bottom - ctrl.top) - kPianoSpecControlH) / 2) + 10;
    int x = ctrl.right - 8;

    tp->rcProcessedToggle = RECT{ (LONG)(x - kPianoSpecCheckboxW), (LONG)cy, (LONG)x, (LONG)(cy + kPianoSpecControlH) };
    x -= kPianoSpecCheckboxW + 8;
    tp->rcMaxResButton = RECT{ (LONG)(x - kPianoSpecButtonW), (LONG)cy, (LONG)x, (LONG)(cy + kPianoSpecControlH) };
    x -= kPianoSpecButtonW + 8;
    tp->rcDbRangeButton = RECT{ (LONG)(x - kPianoSpecButtonW), (LONG)cy, (LONG)x, (LONG)(cy + kPianoSpecControlH) };
    x -= kPianoSpecButtonW + 8;
    tp->rcGridButton = RECT{ (LONG)(x - kPianoSpecButtonW), (LONG)cy, (LONG)x, (LONG)(cy + kPianoSpecControlH) };
}

static int PianoSpecGridMenuItemCount()
{
    return 12;
}

static int PianoSpecGridMenuModeAt(int idx)
{
    switch (idx)
    {
    case 0: return kPianoSpecGrid_None;
    case 1: return kPianoSpecGrid_1_6_Step;
    case 2: return kPianoSpecGrid_1_4_Step;
    case 3: return kPianoSpecGrid_1_3_Step;
    case 4: return kPianoSpecGrid_1_2_Step;
    case 5: return kPianoSpecGrid_Step;
    case 6: return kPianoSpecGrid_1_6_Beat;
    case 7: return kPianoSpecGrid_1_4_Beat;
    case 8: return kPianoSpecGrid_1_3_Beat;
    case 9: return kPianoSpecGrid_1_2_Beat;
    case 10: return kPianoSpecGrid_Beat;
    case 11: return kPianoSpecGrid_Bar;
    default: return kPianoSpecGrid_Beat;
    }
}

static bool PianoSpecGridMenuSeparatorBefore(int idx)
{
    return idx == 1 || idx == 6;
}

static RECT PianoSpecGridMenuItemRect(const RECT& menuRc, int idx)
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

static int HitTestPianoSpecGridMenuItem(const PianoSpectrogramThreadParam* tp, POINT pt)
{
    if (!tp || !tp->gridMenuOpen) return -1;
    if (!PtInRect(&tp->rcGridMenu, pt)) return -1;
    for (int i = 0; i < PianoSpecGridMenuItemCount(); ++i)
    {
        RECT ir = PianoSpecGridMenuItemRect(tp->rcGridMenu, i);
        if (PtInRect(&ir, pt))
            return i;
    }
    return -1;
}

static void OpenPianoSpecGridMenu(PianoSpectrogramThreadParam* tp, const RECT& anchorRc, const RECT& clientRc)
{
    if (!tp) return;
    constexpr int kMenuW = 156;
    constexpr int kPad = 4;
    constexpr int kRowH = 20;
    const int menuH = kPad * 2 + kRowH * PianoSpecGridMenuItemCount();

    RECT rc{ anchorRc.left, anchorRc.bottom + 2, anchorRc.left + kMenuW, anchorRc.bottom + 2 + menuH };
    if (rc.right > clientRc.right - 4) OffsetRect(&rc, (clientRc.right - 4) - rc.right, 0);
    if (rc.left < clientRc.left + 4) OffsetRect(&rc, (clientRc.left + 4) - rc.left, 0);
    if (rc.bottom > clientRc.bottom - 4)
    {
        const int aboveTop = anchorRc.top - 2 - menuH;
        if (aboveTop >= clientRc.top + 4)
        {
            rc.top = aboveTop;
            rc.bottom = rc.top + menuH;
        }
    }
    tp->rcGridMenu = rc;
    tp->gridMenuOpen = true;
    tp->gridMenuHoverIndex = -1;
    const int selectedMode = WaveformWindow::GetSharedPianoGridMode();
    for (int i = 0; i < PianoSpecGridMenuItemCount(); ++i)
    {
        if (PianoSpecGridMenuModeAt(i) == selectedMode)
        {
            tp->gridMenuHoverIndex = i;
            break;
        }
    }
}

static void ClosePianoSpecGridMenu(PianoSpectrogramThreadParam* tp)
{
    if (!tp) return;
    tp->gridMenuOpen = false;
    tp->gridMenuHoverIndex = -1;
    SetRectEmpty(&tp->rcGridMenu);
}

static bool HandlePianoSpecGridMenuMouseDown(HWND hwnd, PianoSpectrogramThreadParam* tp, POINT pt)
{
    if (!tp || !hwnd || !tp->gridMenuOpen)
        return false;

    RECT oldRc = tp->rcGridMenu;
    if (!PtInRect(&tp->rcGridMenu, pt))
    {
        ClosePianoSpecGridMenu(tp);
        InvalidateRect(hwnd, &oldRc, FALSE);
        return false;
    }

    const int idx = HitTestPianoSpecGridMenuItem(tp, pt);
    if (idx >= 0)
        WaveformWindow::SetSharedPianoGridMode(PianoSpecGridMenuModeAt(idx));
    ClosePianoSpecGridMenu(tp);
    InvalidateRect(hwnd, &oldRc, FALSE);
    return true;
}

static bool UpdatePianoSpecGridMenuHover(HWND hwnd, PianoSpectrogramThreadParam* tp, POINT pt)
{
    if (!tp || !hwnd || !tp->gridMenuOpen)
        return false;
    const int hover = HitTestPianoSpecGridMenuItem(tp, pt);
    if (hover == tp->gridMenuHoverIndex)
        return true;
    tp->gridMenuHoverIndex = hover;
    InvalidateRect(hwnd, &tp->rcGridMenu, FALSE);
    return true;
}

static void DrawPianoSpecGridMenu(HDC hdc, const PianoSpectrogramThreadParam* tp)
{
    if (!hdc || !tp || !tp->gridMenuOpen) return;
    const RECT& menuRc = tp->rcGridMenu;
    if (menuRc.right <= menuRc.left || menuRc.bottom <= menuRc.top) return;

    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 26));
    FillRect(hdc, &menuRc, bg);
    DeleteObject(bg);
    RECT inner = menuRc;
    InflateRect(&inner, -1, -1);
    HBRUSH innerBg = CreateSolidBrush(RGB(24, 27, 34));
    FillRect(hdc, &inner, innerBg);
    DeleteObject(innerBg);

    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(88, 88, 88));
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, menuRc.left, menuRc.top, menuRc.right, menuRc.bottom);
    SelectObject(hdc, oldBrush);
    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));

    SetBkMode(hdc, TRANSPARENT);

    const int selectedMode = WaveformWindow::GetSharedPianoGridMode();
    for (int i = 0; i < PianoSpecGridMenuItemCount(); ++i)
    {
        RECT ir = PianoSpecGridMenuItemRect(menuRc, i);
        const bool hovered = (i == tp->gridMenuHoverIndex);
        const bool selected = (PianoSpecGridMenuModeAt(i) == selectedMode);

        if (PianoSpecGridMenuSeparatorBefore(i))
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
        DrawTextW(hdc, PianoSpecGridModeLabel(PianoSpecGridMenuModeAt(i)), -1, &tr,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

static bool PianoSpecIsBlackKey(int midi)
{
    return PianoSpectrogramUI::IsBlackKey(midi);
}

static std::wstring PianoSpecNoteName(int midi)
{
    return PianoSpectrogramUI::NoteName(midi);
}

static double PianoSpecMidiToFreq(double midi)
{
    return PianoSpectrogramUI::MidiToFreq(midi);
}

static double PianoSpecFreqToMidi(double hz)
{
    return PianoSpectrogramUI::FreqToMidi(hz);
}

static double PianoSpecChooseTimeTickStep(double visibleSeconds)
{
    static const double steps[] = { 0.05,0.1,0.2,0.5,1,2,5,10,15,30,60,120,300 };
    for (double s : steps)
    {
        if (visibleSeconds / s <= 10.0) return s;
    }
    return 600.0;
}

static void PianoSpecEnsureFftReady(PianoSpectrogramThreadParam* tp)
{
    if (!tp) return;
    if (tp->nfft < 256) tp->nfft = 256;
    if (tp->fft.nfft() == tp->nfft && !tp->window.empty())
        return;

    tp->fft.init(tp->nfft);
    tp->window.assign((std::size_t)tp->nfft, 0.0);
    double winSum = 0.0;
    for (int i = 0; i < tp->nfft; ++i)
    {
        tp->window[(std::size_t)i] = dsp::hann(i, tp->nfft);
        winSum += tp->window[(std::size_t)i];
    }
    tp->analysisMono.assign((std::size_t)tp->nfft, 0.0);
    tp->rowDbScratch.clear();
    tp->fftAmpScale = (winSum > 1e-12) ? (2.0 / winSum) : (1.0 / (double)(std::max)(1, tp->nfft));
    tp->historyDirty = true;
}

static inline double SampleMonoAtFrame(const PianoSpectrogramThreadParam* tp, long long frame)
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

static bool FillAnalysisMonoWindow(PianoSpectrogramThreadParam* tp, double centerFrameD)
{
    if (!tp) return false;
    PianoSpecEnsureFftReady(tp);
    const int nfft = tp->fft.nfft();
    if (nfft <= 0) return false;
    if ((int)tp->analysisMono.size() != nfft)
        tp->analysisMono.assign((std::size_t)nfft, 0.0);

    bool usedProcessed = false;
    if (tp->useProcessedPlaybackMix)
        usedProcessed = WaveformWindow::BuildPlaybackSpectrogramMonoWindow(centerFrameD, nfft, tp->analysisMono);

    if (!usedProcessed)
    {
        const long long center = (long long)std::llround(centerFrameD);
        const long long half = nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const long long src = center + i - half;
            tp->analysisMono[(std::size_t)i] = SampleMonoAtFrame(tp, src);
        }
    }
    return true;
}

static uint32_t PianoSpecHeatColor(double db, double dbRange)
{
    return PianoSpectrogramUI::HeatColor(db, dbRange);
}

static SpectrumSyncView GetSyncView(const PianoSpectrogramThreadParam* tp)
{
    SpectrumSyncView v{};
    if (!tp) return v;

    v.totalSeconds = (tp->sampleRate > 0 && tp->totalFrames > 0)
        ? (double)tp->totalFrames / (double)tp->sampleRate
        : 0.0;

    WaveformWindow::PlaybackSyncSnapshot snap{};
    if (WaveformWindow::GetPlaybackSyncSnapshot(snap) &&
        snap.valid &&
        snap.sampleRate == tp->sampleRate &&
        snap.totalFrames > 0)
    {
        v.hasSync = true;
        v.playing = snap.playing;
        const double maxFrame = (snap.totalFrames > 0) ? (double)(snap.totalFrames - 1) : 0.0;
        v.currentFrame = std::clamp(snap.currentFrame, 0.0, maxFrame);
        v.playbackRate = (std::isfinite(snap.playbackRate) && snap.playbackRate > 0.0) ? snap.playbackRate : 1.0;
        v.currentSeconds = (tp->sampleRate > 0) ? (v.currentFrame / (double)tp->sampleRate) : 0.0;
        return v;
    }

    v.currentFrame = 0.0;
    v.currentSeconds = 0.0;
    v.playbackRate = 1.0;
    v.playing = false;
    v.hasSync = false;
    return v;
}

static void ComputePianoSpectrogramColumn(PianoSpectrogramThreadParam* tp, double centerFrameD, int plotH, std::vector<uint32_t>& outCol)
{
    if (!tp || plotH <= 0 || tp->sampleRate <= 0)
    {
        outCol.assign((std::size_t)(std::max)(0, plotH), PianoSpecHeatColor(-84.0, 84.0));
        return;
    }

    outCol.assign((std::size_t)plotH, PianoSpecHeatColor(-tp->dbRange, tp->dbRange));

    PianoSpecEnsureFftReady(tp);
    if (tp->fft.nfft() <= 0 || !FillAnalysisMonoWindow(tp, centerFrameD))
        return;

    const int nfft = tp->fft.nfft();
    if ((int)tp->rowDbScratch.size() != plotH)
        tp->rowDbScratch.assign((std::size_t)plotH, -1e12);
    else
        std::fill(tp->rowDbScratch.begin(), tp->rowDbScratch.end(), -1e12);

    double mean = 0.0;
    for (int i = 0; i < nfft; ++i)
        mean += tp->analysisMono[(std::size_t)i];
    mean /= (double)nfft;

    for (int i = 0; i < nfft; ++i)
        tp->fft.in()[i] = (tp->analysisMono[(std::size_t)i] - mean) * tp->window[(std::size_t)i];
    tp->fft.execute();

    const fftw_complex* X = tp->fft.out();
    if (!X) return;

    const double nyquist = 0.5 * (double)tp->sampleRate;
    const double fMin = std::max(10.0, tp->minFreqHz);
    const double fMaxByMidi = PianoSpecMidiToFreq((double)tp->midiMax + 0.5);
    const double fMax = std::clamp(fMaxByMidi, fMin + 1.0, nyquist);
    const double hzPerBin = (double)tp->sampleRate / (double)nfft;
    const double midiSpan = (double)(std::max)(1, tp->midiMax - tp->midiMin);

    auto rowForMidi = [&](double midi) -> int
    {
        const double yNorm = ((double)tp->midiMax - midi) / midiSpan;
        int y = (int)std::lround(yNorm * (double)(plotH - 1));
        return (std::max)(0, (std::min)(plotH - 1, y));
    };

    const int kMax = nfft / 2;
    for (int k = 1; k <= kMax; ++k)
    {
        const double f = (double)k * hzPerBin;
        if (f < fMin) continue;
        if (f > fMax) break;

        const double midi = PianoSpecFreqToMidi(f);
        if (!std::isfinite(midi) || midi < (double)tp->midiMin - 0.5 || midi > (double)tp->midiMax + 0.5)
            continue;

        const double re = X[k][0];
        const double im = X[k][1];
        const double mag = std::sqrt(re * re + im * im);
        const double amp = mag * tp->fftAmpScale;
        double db = 20.0 * std::log10(amp + 1e-12);
        if (!std::isfinite(db)) db = -300.0;

        const int y = rowForMidi(midi);
        tp->rowDbScratch[(std::size_t)y] = (std::max)(tp->rowDbScratch[(std::size_t)y], db);
        if (y > 0) tp->rowDbScratch[(std::size_t)(y - 1)] = (std::max)(tp->rowDbScratch[(std::size_t)(y - 1)], db - 2.0);
        if (y + 1 < plotH) tp->rowDbScratch[(std::size_t)(y + 1)] = (std::max)(tp->rowDbScratch[(std::size_t)(y + 1)], db - 2.0);
    }

    for (int y = 1; y + 1 < plotH; ++y)
    {
        const double d0 = tp->rowDbScratch[(std::size_t)(y - 1)] - 3.0;
        const double d1 = tp->rowDbScratch[(std::size_t)y];
        const double d2 = tp->rowDbScratch[(std::size_t)(y + 1)] - 3.0;
        tp->rowDbScratch[(std::size_t)y] = (std::max)(d1, (std::max)(d0, d2));
    }

    for (int y = 0; y < plotH; ++y)
        outCol[(std::size_t)y] = PianoSpecHeatColor(tp->rowDbScratch[(std::size_t)y], tp->dbRange);
}

static void EnsurePianoSpecImage(PianoSpectrogramThreadParam* tp, int w, int h)
{
    if (!tp) return;
    w = (std::max)(0, w);
    h = (std::max)(0, h);
    if (tp->imageW == w && tp->imageH == h && (int)tp->imageBgra.size() == w * h)
        return;

    tp->imageW = w;
    tp->imageH = h;
    tp->imageBgra.assign((std::size_t)w * (std::size_t)h, PianoSpecHeatColor(-tp->dbRange, tp->dbRange));
    tp->historyDirty = true;
    tp->lastHistoryRightSeconds = std::numeric_limits<double>::quiet_NaN();
}

static void ClearPianoSpecImage(PianoSpectrogramThreadParam* tp)
{
    if (!tp || tp->imageW <= 0 || tp->imageH <= 0) return;
    std::fill(tp->imageBgra.begin(), tp->imageBgra.end(), PianoSpecHeatColor(-tp->dbRange, tp->dbRange));
}

static void ShiftPianoSpecImageLeft(PianoSpectrogramThreadParam* tp, int cols)
{
    if (!tp || tp->imageW <= 0 || tp->imageH <= 0 || cols <= 0) return;
    if (cols >= tp->imageW)
    {
        ClearPianoSpecImage(tp);
        return;
    }

    const int w = tp->imageW;
    const int h = tp->imageH;
    const uint32_t bg = PianoSpecHeatColor(-tp->dbRange, tp->dbRange);
    for (int y = 0; y < h; ++y)
    {
        uint32_t* row = tp->imageBgra.data() + (std::size_t)y * (std::size_t)w;
        std::memmove(row, row + cols, (std::size_t)(w - cols) * sizeof(uint32_t));
        std::fill(row + (w - cols), row + w, bg);
    }
}

static void WritePianoSpecColumn(PianoSpectrogramThreadParam* tp, int x, const std::vector<uint32_t>& col)
{
    if (!tp || tp->imageW <= 0 || tp->imageH <= 0) return;
    if (x < 0 || x >= tp->imageW) return;
    if ((int)col.size() < tp->imageH) return;
    for (int y = 0; y < tp->imageH; ++y)
        tp->imageBgra[(std::size_t)y * (std::size_t)tp->imageW + (std::size_t)x] = col[(std::size_t)y];
}

static void RebuildPianoSpecHistory(PianoSpectrogramThreadParam* tp, const SpectrumSyncView& view, int plotW, int plotH)
{
    if (!tp) return;
    EnsurePianoSpecImage(tp, plotW, plotH);
    if (plotW <= 0 || plotH <= 0) return;

    ClearPianoSpecImage(tp);
    if (!view.hasSync || tp->sampleRate <= 0)
    {
        tp->lastHistoryRightSeconds = std::numeric_limits<double>::quiet_NaN();
        tp->lastObservedSyncSeconds = view.currentSeconds;
        tp->historyDirty = false;
        return;
    }

    const double secPerCol = tp->historySeconds / (double)(std::max)(1, plotW);
    const double tRight = view.currentSeconds;
    const double tLeft = tRight - tp->historySeconds;

    std::vector<uint32_t> col;
    col.reserve((std::size_t)plotH);
    for (int x = 0; x < plotW; ++x)
    {
        const double t = tLeft + (static_cast<double>(x) + 0.5) * secPerCol;
        const double centerFrameD = t * (double)tp->sampleRate;
        ComputePianoSpectrogramColumn(tp, centerFrameD, plotH, col);
        WritePianoSpecColumn(tp, x, col);
    }

    tp->lastHistoryRightSeconds = tRight;
    tp->lastObservedSyncSeconds = view.currentSeconds;
    tp->historyDirty = false;
}

static void UpdatePianoSpecHistory(PianoSpectrogramThreadParam* tp, const SpectrumSyncView& view, int plotW, int plotH)
{
    if (!tp) return;
    EnsurePianoSpecImage(tp, plotW, plotH);
    if (plotW <= 0 || plotH <= 0) return;

    const double secPerCol = tp->historySeconds / (double)(std::max)(1, plotW);
    const bool badLast = !std::isfinite(tp->lastHistoryRightSeconds);
    const bool syncJumpPaused = !view.playing && std::isfinite(tp->lastObservedSyncSeconds) &&
        std::fabs(view.currentSeconds - tp->lastObservedSyncSeconds) > (secPerCol * 1.5);

    if (tp->historyDirty || !view.hasSync || badLast || tp->imageW != plotW || tp->imageH != plotH ||
        (std::isfinite(tp->lastHistoryRightSeconds) && view.currentSeconds < tp->lastHistoryRightSeconds - secPerCol * 0.5) ||
        (view.playing && std::isfinite(tp->lastHistoryRightSeconds) && (view.currentSeconds - tp->lastHistoryRightSeconds) > (tp->historySeconds * 0.75)) ||
        syncJumpPaused)
    {
        RebuildPianoSpecHistory(tp, view, plotW, plotH);
        return;
    }

    tp->lastObservedSyncSeconds = view.currentSeconds;
    if (!view.playing)
        return;

    const double delta = view.currentSeconds - tp->lastHistoryRightSeconds;
    int colsToAdvance = (int)std::floor(delta / secPerCol);
    if (colsToAdvance <= 0)
        return;
    if (colsToAdvance >= plotW)
    {
        RebuildPianoSpecHistory(tp, view, plotW, plotH);
        return;
    }

    ShiftPianoSpecImageLeft(tp, colsToAdvance);
    const double oldRight = tp->lastHistoryRightSeconds;
    const double quantizedRight = oldRight + (double)colsToAdvance * secPerCol;
    std::vector<uint32_t> col;
    col.reserve((std::size_t)plotH);
    for (int i = 0; i < colsToAdvance; ++i)
    {
        // Fill columns against the quantized history edge (the actual raster scroll step),
        // not the live playback time, to keep overlays/grid perfectly phase-locked.
        const double t = quantizedRight - (double)(colsToAdvance - i - 1) * secPerCol - 0.5 * secPerCol;
        const double centerFrameD = t * (double)tp->sampleRate;
        ComputePianoSpectrogramColumn(tp, centerFrameD, plotH, col);
        WritePianoSpecColumn(tp, plotW - colsToAdvance + i, col);
    }

    tp->lastHistoryRightSeconds = quantizedRight;
}

static void DrawPianoSpecButton(HDC hdc, const RECT& rc, const std::wstring& text)
{
    if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(88, 88, 88));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 220, 220));
    RECT tr = rc;
    DrawTextW(hdc, text.c_str(), -1, &tr,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

static void DrawPianoSpecCheckbox(HDC hdc, const RECT& rc, bool checked, const wchar_t* label)
{
    if (!hdc || rc.right <= rc.left || rc.bottom <= rc.top) return;
    HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(88, 88, 88));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    RECT cb{ rc.left + 4, rc.top + 4, rc.left + 16, rc.top + 16 };
    HBRUSH cbBg = CreateSolidBrush(RGB(18, 18, 18));
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
    SetTextColor(hdc, checked ? RGB(220, 242, 245) : RGB(190, 190, 190));
    RECT tr = rc;
    tr.left = cb.right + 6;
    DrawTextW(hdc, label ? label : L"", -1, &tr,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawPianoSpecImage(HDC hdc, const RECT& plotRc, const PianoSpectrogramThreadParam* tp)
{
    if (!hdc || !tp) return;
    const int w = (std::max)(0, (int)(plotRc.right - plotRc.left));
    const int h = (std::max)(0, (int)(plotRc.bottom - plotRc.top));
    if (w <= 0 || h <= 0 || tp->imageW != w || tp->imageH != h || tp->imageBgra.empty())
        return;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    StretchDIBits(
        hdc,
        plotRc.left, plotRc.top, w, h,
        0, 0, w, h,
        tp->imageBgra.data(),
        &bmi,
        DIB_RGB_COLORS,
        SRCCOPY);
}

static void DrawPianoSpecWindow(HDC hdc, const RECT& clientRc, PianoSpectrogramThreadParam* tp)
{
    if (!hdc) return;
    if (tp) LayoutPianoSpecControls(tp, clientRc);

    const RECT controlRc = ComputePianoSpecControlRect(clientRc);
    const RECT plotRc = ComputePianoSpecPlotRect(clientRc);
    const RECT leftRc = ComputePianoSpecLeftScaleRect(clientRc);
    const RECT timeRc = ComputePianoSpecBottomAxisRect(clientRc);
    const RECT graphOuter = ComputePianoSpecGraphOuterRect(clientRc);

    HBRUSH bg = CreateSolidBrush(RGB(16, 18, 22));
    FillRect(hdc, &clientRc, bg);
    DeleteObject(bg);

    HBRUSH ctrlBg = CreateSolidBrush(RGB(34, 36, 40));
    FillRect(hdc, &controlRc, ctrlBg);
    DeleteObject(ctrlBg);

    HBRUSH outerBg = CreateSolidBrush(RGB(12, 14, 18));
    FillRect(hdc, &graphOuter, outerBg);
    DeleteObject(outerBg);

    HBRUSH leftBg = CreateSolidBrush(RGB(22, 24, 28));
    FillRect(hdc, &leftRc, leftBg);
    DeleteObject(leftBg);

    HBRUSH plotBg = CreateSolidBrush(RGB(8, 10, 14));
    FillRect(hdc, &plotRc, plotBg);
    DeleteObject(plotBg);

    HBRUSH timeBg = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(hdc, &timeRc, timeBg);
    DeleteObject(timeBg);

    SpectrumSyncView view = GetSyncView(tp);
    if (tp && plotRc.right > plotRc.left && plotRc.bottom > plotRc.top)
    {
        UpdatePianoSpecHistory(tp, view,
            (int)(plotRc.right - plotRc.left),
            (int)(plotRc.bottom - plotRc.top));
        DrawPianoSpecImage(hdc, plotRc, tp);
    }

    if (tp && plotRc.right > plotRc.left && plotRc.bottom > plotRc.top)
    {
        const int plotW = (int)(plotRc.right - plotRc.left);
        const int plotH = (int)(plotRc.bottom - plotRc.top);
        const int midiMin = (std::min)(tp->midiMin, tp->midiMax);
        const int midiMax = (std::max)(tp->midiMin, tp->midiMax);
        const int noteCount = (std::max)(1, midiMax - midiMin + 1);
        const double rowH = (double)plotH / (double)noteCount;
        const double visibleSeconds = (tp->historySeconds > 0.05) ? tp->historySeconds : 10.0;
        // Keep the time/grid overlay locked to the rasterized spectrogram history buffer.
        // The image scrolls in whole-column updates, so using live currentSeconds here causes
        // a visible parallax drift between the heatmap and the timestamps/grid.
        const double axisRightSeconds =
            (std::isfinite(tp->lastHistoryRightSeconds) ? tp->lastHistoryRightSeconds : view.currentSeconds);
        const double tRight = axisRightSeconds;
        const double tLeft = axisRightSeconds - visibleSeconds;

        RECT keyStrip = leftRc;
        keyStrip.right = (LONG)(std::min)((int)leftRc.right, (int)leftRc.left + kPianoSpecKeyStripW);
        RECT labelRc = leftRc;
        labelRc.left = keyStrip.right + 2;
        const RECT midiSliderTrackRc = ComputePianoSpecMidiSliderTrackRect(leftRc);
        if (midiSliderTrackRc.left > labelRc.left)
            labelRc.right = (LONG)((std::max)((int)labelRc.left, (int)midiSliderTrackRc.left - 2));

        SetBkMode(hdc, TRANSPARENT);
        HFONT oldFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(38, 42, 48));
        HPEN hStrong = CreatePen(PS_SOLID, 1, RGB(62, 66, 74));
        HGDIOBJ oldPen = SelectObject(hdc, hPen);

        for (int idx = 0; idx < noteCount; ++idx)
        {
            const int midi = midiMax - idx;
            const int y0 = plotRc.top + (int)std::floor((double)idx * rowH);
            const int y1 = plotRc.top + (int)std::floor((double)(idx + 1) * rowH);
            const RECT rowKey{ keyStrip.left, (LONG)y0, keyStrip.right, (LONG)(std::max)(y0 + 1, y1) };
            HBRUSH rowBr = CreateSolidBrush(PianoSpecIsBlackKey(midi) ? RGB(26, 30, 36) : RGB(36, 40, 46));
            FillRect(hdc, &rowKey, rowBr);
            DeleteObject(rowBr);

            const bool cNote = ((midi % 12 + 12) % 12) == 0;
            SelectObject(hdc, cNote ? hStrong : hPen);
            MoveToEx(hdc, plotRc.left, y0, NULL);
            LineTo(hdc, plotRc.right, y0);
            MoveToEx(hdc, leftRc.left, y0, NULL);
            LineTo(hdc, leftRc.right, y0);

            if (cNote || rowH >= 13.0)
            {
                std::wstring nm = PianoSpecNoteName(midi);
                const double hz = PianoSpecMidiToFreq((double)midi);
                wchar_t lbl[48];
                if (cNote)
                    swprintf_s(lbl, L"%s  %.0fHz", nm.c_str(), hz);
                else
                    swprintf_s(lbl, L"%s", nm.c_str());
                SetTextColor(hdc, cNote ? RGB(230, 232, 236) : RGB(170, 174, 180));
                RECT lr{ labelRc.left + 2, (LONG)y0, leftRc.right - 2, (LONG)(std::max)(y0 + 12, y1) };
                DrawTextW(hdc, lbl, -1, &lr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
        }

        if (midiSliderTrackRc.right > midiSliderTrackRc.left && midiSliderTrackRc.bottom > midiSliderTrackRc.top)
        {
            HBRUSH trackBg = CreateSolidBrush(RGB(18, 21, 27));
            FillRect(hdc, &midiSliderTrackRc, trackBg);
            DeleteObject(trackBg);

            RECT thumbRc = ComputePianoSpecMidiSliderThumbRect(tp, midiSliderTrackRc);
            HBRUSH thumbBg = CreateSolidBrush(tp->midiSliderDragActive ? RGB(120, 170, 210) : RGB(92, 116, 144));
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

        MoveToEx(hdc, leftRc.left, plotRc.bottom - 1, NULL);
        LineTo(hdc, plotRc.right, plotRc.bottom - 1);

        const double tickStep = PianoSpecChooseTimeTickStep(visibleSeconds);
        const double firstTick = std::floor(tLeft / tickStep) * tickStep;
        HPEN timePen = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
        HPEN timeStrong = CreatePen(PS_SOLID, 1, RGB(58, 64, 76));
        for (double t = firstTick; t <= tRight + tickStep * 0.5; t += tickStep)
        {
            if (t < tLeft - 1e-9) continue;
            if (t < 0.0) continue;
            const double xn = (t - tLeft) / visibleSeconds;
            int x = plotRc.left + (int)std::lround(xn * (double)(plotW - 1));
            x = (std::max)((int)plotRc.left, (std::min)(x, (int)plotRc.right - 1));
            const bool strong = (tickStep >= 5.0) || (std::fabs(std::fmod(std::fabs(t), 5.0)) < 1e-6);
            SelectObject(hdc, strong ? timeStrong : timePen);
            MoveToEx(hdc, x, timeRc.top, NULL);
            LineTo(hdc, x, timeRc.top + 7);
        }

        DrawPianoSpecMusicalGrid(hdc, plotRc, timeRc, tp, tLeft, tRight);

        // Bottom time axis keeps timestamp labels, but time marks are tick-only (no full-height grey time grid lines).
        SetTextColor(hdc, RGB(185, 190, 198));
        const bool showMillis = tickStep < 1.0;
        for (double t = firstTick; t <= tRight + tickStep * 0.5; t += tickStep)
        {
            if (t < tLeft - 1e-9) continue;
            if (t < 0.0) continue;
            const double xn = (t - tLeft) / visibleSeconds;
            int x = timeRc.left + (int)std::lround(xn * (double)(((int)timeRc.right - (int)timeRc.left) - 1));
            x = (std::max)((int)timeRc.left, (std::min)(x, (int)timeRc.right - 1));
            std::wstring lbl = FormatTimeLabel(t, showMillis);
            RECT tr{ (LONG)(x + 2), timeRc.top + 8, timeRc.right - 2, timeRc.bottom - 2 };
            DrawTextW(hdc, lbl.c_str(), -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(76, 80, 90));
        SelectObject(hdc, sepPen);
        HGDIOBJ oldBorderBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        MoveToEx(hdc, leftRc.right - 1, leftRc.top, NULL);
        LineTo(hdc, leftRc.right - 1, leftRc.bottom);
        MoveToEx(hdc, plotRc.left, timeRc.top, NULL);
        LineTo(hdc, plotRc.right, timeRc.top);
        Rectangle(hdc, plotRc.left, plotRc.top, plotRc.right, plotRc.bottom);
        Rectangle(hdc, leftRc.left, leftRc.top, leftRc.right, leftRc.bottom);
        Rectangle(hdc, timeRc.left, timeRc.top, timeRc.right, timeRc.bottom);
        SelectObject(hdc, oldBorderBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(sepPen);

        // "Now" marker: draw after borders so it stays visible and aligns with the
        // newest spectral column at the right edge of the scrolling history buffer.
        if (view.hasSync)
        {
            const int nowX = (std::max)((int)plotRc.left + 1, (int)plotRc.right - 2);
            HPEN playPen = CreatePen(PS_SOLID, 2, RGB(248, 248, 248));
            HGDIOBJ prevPlayPen = SelectObject(hdc, playPen);
            MoveToEx(hdc, nowX, plotRc.top + 1, NULL);
            LineTo(hdc, nowX, plotRc.bottom - 1);
            MoveToEx(hdc, nowX, timeRc.top + 1, NULL);
            LineTo(hdc, nowX, timeRc.bottom - 1);
            // Small cap marker at the top helps visibility against bright harmonics.
            MoveToEx(hdc, nowX - 4, plotRc.top + 2, NULL);
            LineTo(hdc, nowX + 4, plotRc.top + 2);
            SelectObject(hdc, prevPlayPen);
            DeleteObject(playPen);
        }

        DeleteObject(timePen);
        DeleteObject(timeStrong);
        SelectObject(hdc, oldFont);
        DeleteObject(hPen);
        DeleteObject(hStrong);
    }

    if (tp)
    {
        SetBkMode(hdc, TRANSPARENT);
        HFONT oldUiFont = (HFONT)SelectObject(hdc, (HFONT)GetStockObject(SYSTEM_FONT));
        SetTextColor(hdc, RGB(235, 235, 235));
        const LONG controlLeftBound = (LONG)std::max((int)controlRc.left + 120,
            (int)((tp->rcGridButton.left > 0) ? tp->rcGridButton.left : tp->rcDbRangeButton.left) - 8);

        RECT titleRc = controlRc;
        titleRc.left += 8;
        titleRc.top += 3;
        titleRc.bottom = titleRc.top + 18;
        titleRc.right = controlLeftBound;
        DrawTextW(hdc, tp->title.empty() ? L"Piano Spectrogram" : tp->title.c_str(), -1, &titleRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        SpectrumSyncView hv = GetSyncView(tp);
        wchar_t hud[512];
        swprintf_s(hud,
            L"sync=%s play=%s mode=%s rate=%.2fx  t=%s / %s  dB=%.0f  max res=%d  hist=%.1fs",
            hv.hasSync ? L"ON" : L"off",
            hv.playing ? L"ON" : L"off",
            tp->useProcessedPlaybackMix ? L"PROC" : L"RAW",
            hv.playbackRate,
            FormatTimeLabel(hv.currentSeconds, true).c_str(),
            FormatTimeLabel(hv.totalSeconds, true).c_str(),
            tp->dbRange,
            tp->nfft,
            tp->historySeconds);
        RECT hudRc = controlRc;
        hudRc.left += 8;
        hudRc.top += 22;
        hudRc.bottom -= 6;
        hudRc.right = controlLeftBound;
        SetTextColor(hdc, RGB(190, 195, 202));
        DrawTextW(hdc, hud, -1, &hudRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        wchar_t gridLabel[96];
        swprintf_s(gridLabel, L"Grid  %s", PianoSpecGridModeLabel(WaveformWindow::GetSharedPianoGridMode()));
        DrawPianoSpecButton(hdc, tp->rcGridButton, gridLabel);

        wchar_t dbLabel[64];
        swprintf_s(dbLabel, L"dB Range  %.0f", tp->dbRange);
        DrawPianoSpecButton(hdc, tp->rcDbRangeButton, dbLabel);

        wchar_t resLabel[64];
        swprintf_s(resLabel, L"Max Res  %d", tp->nfft);
        DrawPianoSpecButton(hdc, tp->rcMaxResButton, resLabel);

        DrawPianoSpecCheckbox(hdc, tp->rcProcessedToggle, tp->useProcessedPlaybackMix, L"Processed Mix");
        DrawPianoSpecGridMenu(hdc, tp);
        SelectObject(hdc, oldUiFont);
    }
}

static void CALLBACK PianoSpecRenderTimerCallback(PVOID lpParameter, BOOLEAN /*timerOrWaitFired*/)
{
    auto* tp = reinterpret_cast<PianoSpectrogramThreadParam*>(lpParameter);
    if (!tp || !tp->hwnd) return;
    bool expected = false;
    if (!tp->renderTickQueued.compare_exchange_strong(expected, true))
        return;
    PostMessageW(tp->hwnd, WM_PIANO_SPECTROGRAM_TICK, 0, 0);
}

static void HandlePianoSpecRenderTick(PianoSpectrogramThreadParam* tp)
{
    if (!tp || !tp->hwnd) return;
    tp->renderTickQueued.store(false);
    InvalidateRect(tp->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK PianoSpecWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    auto* tp = reinterpret_cast<PianoSpectrogramThreadParam*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg)
    {
    case WM_CREATE:
        return 0;

    case WM_SIZE:
        if (tp)
        {
            tp->historyDirty = true;
            tp->imageW = 0;
            tp->imageH = 0;
            tp->imageBgra.clear();
            ClosePianoSpecGridMenu(tp);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
    {
        if (!tp) break;
        RECT rc{}; GetClientRect(hwnd, &rc);
        LayoutPianoSpecControls(tp, rc);
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (HandlePianoSpecGridMenuMouseDown(hwnd, tp, pt))
            return 0;
        {
            const RECT leftRc = ComputePianoSpecLeftScaleRect(rc);
            const RECT sliderTrackRc = ComputePianoSpecMidiSliderTrackRect(leftRc);
            if (sliderTrackRc.right > sliderTrackRc.left && sliderTrackRc.bottom > sliderTrackRc.top &&
                PtInRect(&sliderTrackRc, pt))
            {
                tp->midiSliderDragActive = true;
                SetCapture(hwnd);
                if (SetPianoSpecMidiSliderFromY(tp, sliderTrackRc, pt.y))
                    InvalidateRect(hwnd, NULL, FALSE);
                else
                    InvalidateRect(hwnd, &leftRc, FALSE);
                return 0;
            }
        }
        if (PtInRect(&tp->rcProcessedToggle, pt))
        {
            tp->useProcessedPlaybackMix = !tp->useProcessedPlaybackMix;
            tp->historyDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&tp->rcGridButton, pt))
        {
            if (tp->gridMenuOpen)
            {
                RECT oldRc = tp->rcGridMenu;
                ClosePianoSpecGridMenu(tp);
                InvalidateRect(hwnd, &oldRc, FALSE);
            }
            else
            {
                OpenPianoSpecGridMenu(tp, tp->rcGridButton, rc);
                InvalidateRect(hwnd, &tp->rcGridMenu, FALSE);
            }
            return 0;
        }
        if (PtInRect(&tp->rcDbRangeButton, pt))
        {
            int idx = 0;
            for (int i = 0; i < (int)(sizeof(kPianoSpecDbRanges) / sizeof(kPianoSpecDbRanges[0])); ++i)
            {
                if (std::fabs(tp->dbRange - kPianoSpecDbRanges[i]) < 1e-6) { idx = i; break; }
            }
            idx = (idx + 1) % (int)(sizeof(kPianoSpecDbRanges) / sizeof(kPianoSpecDbRanges[0]));
            tp->dbRange = kPianoSpecDbRanges[idx];
            tp->historyDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&tp->rcMaxResButton, pt))
        {
            tp->nfftChoiceIndex = (tp->nfftChoiceIndex + 1) % (int)(sizeof(kPianoSpecNfftChoices) / sizeof(kPianoSpecNfftChoices[0]));
            tp->nfft = kPianoSpecNfftChoices[tp->nfftChoiceIndex];
            tp->historyDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (tp)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if ((wParam & MK_LBUTTON) && tp->midiSliderDragActive)
            {
                RECT rc{}; GetClientRect(hwnd, &rc);
                const RECT leftRc = ComputePianoSpecLeftScaleRect(rc);
                const RECT sliderTrackRc = ComputePianoSpecMidiSliderTrackRect(leftRc);
                if (SetPianoSpecMidiSliderFromY(tp, sliderTrackRc, pt.y))
                    InvalidateRect(hwnd, NULL, FALSE);
                else
                    InvalidateRect(hwnd, &leftRc, FALSE);
                return 0;
            }
            if (!(wParam & (MK_LBUTTON | MK_RBUTTON)))
            {
                if (UpdatePianoSpecGridMenuHover(hwnd, tp, pt))
                    return 0;
            }
        }
        break;

    case WM_LBUTTONUP:
        if (tp && tp->midiSliderDragActive)
        {
            tp->midiSliderDragActive = false;
            InvalidateRect(hwnd, NULL, FALSE);
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        if (GetCapture() == hwnd) ReleaseCapture();
        break;

    case WM_CAPTURECHANGED:
        if (tp) tp->midiSliderDragActive = false;
        break;

    case WM_KILLFOCUS:
        if (tp && tp->gridMenuOpen)
        {
            RECT oldRc = tp->rcGridMenu;
            ClosePianoSpecGridMenu(tp);
            InvalidateRect(hwnd, &oldRc, FALSE);
        }
        break;

    case WM_MOUSEWHEEL:
        if (tp && (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL))
        {
            SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
            tp->dbRange = std::clamp(tp->dbRange + (delta > 0 ? -6.0 : 6.0), 24.0, 144.0);
            tp->historyDirty = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PIANO_SPECTROGRAM_TICK:
        HandlePianoSpecRenderTick(tp);
        return 0;

    case WM_TIMER:
        if (wParam == 1)
        {
            HandlePianoSpecRenderTick(tp);
            return 0;
        }
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC wndDC = BeginPaint(hwnd, &ps);
        RECT rc{}; GetClientRect(hwnd, &rc);

        const int w = (std::max)(0, (int)(rc.right - rc.left));
        const int h = (std::max)(0, (int)(rc.bottom - rc.top));
        HDC hdc = wndDC;
        HDC memDC = NULL;
        HBITMAP memBmp = NULL;
        HBITMAP oldBmp = NULL;
        if (w > 0 && h > 0)
        {
            memDC = CreateCompatibleDC(wndDC);
            if (memDC)
            {
                memBmp = CreateCompatibleBitmap(wndDC, w, h);
                if (memBmp)
                {
                    oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
                    hdc = memDC;
                }
            }
        }

        DrawPianoSpecWindow(hdc, rc, tp);

        if (memDC && memBmp)
        {
            const RECT paintRc = ps.rcPaint;
            const int bw = (std::max)(0, (int)(paintRc.right - paintRc.left));
            const int bh = (std::max)(0, (int)(paintRc.bottom - paintRc.top));
            if (bw > 0 && bh > 0)
                BitBlt(wndDC, paintRc.left, paintRc.top, bw, bh, memDC, paintRc.left, paintRc.top, SRCCOPY);
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
        }
        if (gPianoSpecWindowHwnd.load() == hwnd)
            gPianoSpecWindowHwnd.store(nullptr);
        gPianoSpecWindowOpenOrLaunching.store(false);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static DWORD WINAPI PianoSpecThreadProc(LPVOID lpParameter)
{
    std::unique_ptr<PianoSpectrogramThreadParam> tp(reinterpret_cast<PianoSpectrogramThreadParam*>(lpParameter));
    timeBeginPeriod(1);

    if (tp)
    {
        tp->totalFrames = GetTotalFrames(tp.get());
        PianoSpecEnsureFftReady(tp.get());
    }

    HINSTANCE hInstance = GetModuleHandleW(NULL);
    const wchar_t CLASS_NAME[] = L"PianoSpectrogramWindowClass";

    WNDCLASSW wc{};
    wc.lpfnWndProc = PianoSpecWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc))
    {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            gPianoSpecWindowOpenOrLaunching.store(false);
            timeEndPeriod(1);
            return 1;
        }
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        tp ? tp->title.c_str() : L"Piano Spectrogram",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1320, 760,
        NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        gPianoSpecWindowOpenOrLaunching.store(false);
        timeEndPeriod(1);
        return 1;
    }

    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(tp.get()));
    tp->hwnd = hwnd;
    gPianoSpecWindowHwnd.store(hwnd);

    tp->renderTimerQueue = CreateTimerQueue();
    if (tp->renderTimerQueue)
    {
        CreateTimerQueueTimer(
            &tp->renderTimer,
            tp->renderTimerQueue,
            PianoSpecRenderTimerCallback,
            tp.get(),
            1,
            tp->renderIntervalMs,
            WT_EXECUTEDEFAULT);
    }
    else
    {
        SetTimer(hwnd, 1, tp->renderIntervalMs, NULL);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    timeEndPeriod(1);
    return (DWORD)msg.wParam;
}

void SpectrogramWindow::ShowPianoSpectrogramAsyncRefStereoSynced(
    std::vector<short>* interleavedStereoSamples,
    int sampleRate,
    const WaveformWindow::GridOverlayConfig& grid,
    const std::wstring& title)
{
    if (HWND existing = gPianoSpecWindowHwnd.load())
    {
        if (IsWindow(existing))
        {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            return;
        }
        gPianoSpecWindowHwnd.store(nullptr);
        gPianoSpecWindowOpenOrLaunching.store(false);
    }

    bool expected = false;
    if (!gPianoSpecWindowOpenOrLaunching.compare_exchange_strong(expected, true))
    {
        if (HWND existing2 = gPianoSpecWindowHwnd.load())
        {
            if (IsWindow(existing2))
            {
                ShowWindow(existing2, SW_RESTORE);
                SetForegroundWindow(existing2);
            }
        }
        return;
    }

    PianoSpectrogramThreadParam* tp = new PianoSpectrogramThreadParam();
    tp->samples = interleavedStereoSamples;
    tp->sampleRate = sampleRate;
    tp->isStereo = true;
    tp->grid = grid;
    tp->title = title.empty() ? L"Piano Spectrogram" : title;
    tp->nfftChoiceIndex = 2;
    tp->nfft = kPianoSpecNfftChoices[tp->nfftChoiceIndex];
    tp->dbRange = 84.0;
    tp->historySeconds = 10.0;
    tp->useProcessedPlaybackMix = true;

    HANDLE h = CreateThread(nullptr, 0, PianoSpecThreadProc, tp, 0, nullptr);
    if (h) CloseHandle(h);
    else
    {
        delete tp;
        gPianoSpecWindowHwnd.store(nullptr);
        gPianoSpecWindowOpenOrLaunching.store(false);
    }
}
