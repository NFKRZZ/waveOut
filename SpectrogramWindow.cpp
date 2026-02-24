// SpectrogramWindow.cpp
// NOTE: This is implemented as a real-time spectrum analyzer (FFT magnitude vs frequency)
// synced to the active WaveformWindow playback state.

#include "SpectrogramWindow.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "Winmm.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "DSP.h"

namespace
{
    constexpr UINT WM_SPECTROGRAM_TICK = WM_APP + 49;
    constexpr int kControlStripHeightPx = 40;
    constexpr int kLeftAxisW = 52;
    constexpr int kBottomAxisH = 22;
    constexpr double kDbFloor = -84.0;
    constexpr double kDbCeil = 0.0;
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

    std::size_t totalFrames = 0;
};

struct SpectrumSyncView
{
    bool hasSync = false;
    bool playing = false;
    double currentFrame = 0.0;
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
        v.currentSeconds = (tp->sampleRate > 0) ? (v.currentFrame / (double)tp->sampleRate) : 0.0;
        return v;
    }

    v.hasSync = false;
    v.playing = false;
    v.currentFrame = 0.0;
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

    const long long center = (long long)std::llround(centerFrameD);
    const long long half = nfft / 2;
    double mean = 0.0;
    for (int i = 0; i < nfft; ++i)
    {
        const long long src = center + i - half;
        mean += SampleMonoAtFrame(tp, src);
    }
    mean /= (double)nfft;

    for (int i = 0; i < nfft; ++i)
    {
        const long long src = center + i - half;
        const double s = SampleMonoAtFrame(tp, src) - mean; // remove DC offset before FFT
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
    SetTextColor(hdc, RGB(235, 235, 235));

    std::wstring title = (tp && !tp->title.empty()) ? tp->title : L"Spectrum Analyzer";

    wchar_t hud[512];
    swprintf_s(hud,
        L"sync=%s  play=%s  t=%s / %s  sr=%d  nfft=%d  freq=20Hz..%.0fHz  bpm=%.2f",
        view.hasSync ? L"ON" : L"off",
        view.playing ? L"ON" : L"off",
        FormatTimeLabel(view.currentSeconds, true).c_str(),
        FormatTimeLabel(view.totalSeconds, true).c_str(),
        tp ? tp->sampleRate : 0,
        tp ? tp->nfft : 0,
        tp ? tp->maxFreqHz : 0.0,
        (tp && tp->grid.enabled) ? tp->grid.bpm : 0.0);

    RECT titleRc = controlRc;
    titleRc.left += 8;
    titleRc.right -= 8;
    titleRc.top += 2;
    titleRc.bottom = (LONG)(titleRc.top + 16);
    DrawTextW(hdc, title.c_str(), -1, &titleRc,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

    RECT hudRc = controlRc;
    hudRc.left += 8;
    hudRc.right -= 8;
    hudRc.top += 18;
    hudRc.bottom -= 2;
    SetTextColor(hdc, RGB(200, 200, 200));
    DrawTextW(hdc, hud, -1, &hudRc,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
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
