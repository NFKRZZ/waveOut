#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <string>

namespace PianoSpectrogramUI
{
    inline bool IsBlackKey(int midi)
    {
        switch (((midi % 12) + 12) % 12)
        {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
        }
    }

    inline std::wstring NoteName(int midi)
    {
        static const wchar_t* names[12] = {
            L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
        };
        const int note = ((midi % 12) + 12) % 12;
        const int octave = (midi / 12) - 1;
        wchar_t buf[16];
        swprintf_s(buf, L"%s%d", names[note], octave);
        return std::wstring(buf);
    }

    inline double MidiToFreq(double midi)
    {
        return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    }

    inline double FreqToMidi(double hz)
    {
        if (!(hz > 0.0) || !std::isfinite(hz)) return -1e9;
        return 69.0 + 12.0 * (std::log(hz / 440.0) / std::log(2.0));
    }

    inline uint32_t HeatColor(double db, double dbRange)
    {
        if (!std::isfinite(dbRange) || dbRange < 6.0) dbRange = 6.0;
        const double floorDb = -dbRange;
        double t = (db - floorDb) / dbRange;
        t = std::clamp(t, 0.0, 1.0);
        t = std::pow(t, 0.82);

        auto lerp = [](double a, double b, double x) { return a + (b - a) * x; };
        auto segColor = [&](double tt, double t0, double t1, COLORREF c0, COLORREF c1) -> COLORREF
        {
            double u = (t1 > t0) ? std::clamp((tt - t0) / (t1 - t0), 0.0, 1.0) : 0.0;
            const int r = (int)std::lround(lerp(GetRValue(c0), GetRValue(c1), u));
            const int g = (int)std::lround(lerp(GetGValue(c0), GetGValue(c1), u));
            const int b = (int)std::lround(lerp(GetBValue(c0), GetBValue(c1), u));
            return RGB(r, g, b);
        };

        const COLORREF cA = RGB(5, 8, 18);
        const COLORREF cB = RGB(20, 22, 70);
        const COLORREF cC = RGB(55, 40, 140);
        const COLORREF cD = RGB(185, 18, 34);
        const COLORREF cE = RGB(255, 95, 0);
        const COLORREF cF = RGB(255, 210, 32);
        const COLORREF cG = RGB(255, 248, 220);

        COLORREF c = cA;
        if (t < 0.18) c = segColor(t, 0.00, 0.18, cA, cB);
        else if (t < 0.38) c = segColor(t, 0.18, 0.38, cB, cC);
        else if (t < 0.58) c = segColor(t, 0.38, 0.58, cC, cD);
        else if (t < 0.78) c = segColor(t, 0.58, 0.78, cD, cE);
        else if (t < 0.93) c = segColor(t, 0.78, 0.93, cE, cF);
        else c = segColor(t, 0.93, 1.00, cF, cG);
        return ((uint32_t)GetRValue(c) << 16) | ((uint32_t)GetGValue(c) << 8) | (uint32_t)GetBValue(c);
    }

    inline RECT ComputeMidiSliderTrackRect(const RECT& leftRc)
    {
        RECT r = leftRc;
        r.left = (LONG)(std::max)((int)leftRc.left, (int)leftRc.right - 10);
        r.right = (LONG)(std::max)((int)r.left + 1, (int)leftRc.right - 3);
        r.top += 2;
        r.bottom -= 2;
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
        return r;
    }

    inline RECT ComputeMidiSliderThumbRect(const RECT& trackRc,
        int visibleMin, int visibleMax,
        int hardMin = 12, int hardMax = 135)
    {
        RECT thumb = trackRc;
        if (trackRc.bottom <= trackRc.top)
            return thumb;

        const int fullSpan = (std::max)(1, hardMax - hardMin);
        int visibleSpan = (std::max)(1, visibleMax - visibleMin);
        visibleSpan = (std::min)(visibleSpan, fullSpan);
        const int travelSpan = (std::max)(0, fullSpan - visibleSpan);

        int offset = visibleMin - hardMin;
        offset = (std::max)(0, (std::min)(offset, travelSpan));

        const int trackH = (std::max)(1, (int)(trackRc.bottom - trackRc.top));
        int thumbH = (int)std::lround((double)trackH * ((double)visibleSpan / (double)fullSpan));
        thumbH = (std::max)(16, (std::min)(thumbH, trackH));
        const int travelPx = (std::max)(0, trackH - thumbH);
        const double u = (travelSpan > 0) ? ((double)offset / (double)travelSpan) : 0.0;
        const int thumbTop = trackRc.top + (travelPx > 0 ? (int)std::lround(u * (double)travelPx) : 0);

        thumb.top = thumbTop;
        thumb.bottom = thumbTop + thumbH;
        if (thumb.bottom > trackRc.bottom)
        {
            const int d = thumb.bottom - trackRc.bottom;
            thumb.top -= d;
            thumb.bottom -= d;
        }
        return thumb;
    }

    inline bool ComputeMidiSliderWindowFromY(const RECT& trackRc, int y,
        int currentMin, int currentMax,
        int& outMin, int& outMax,
        int hardMin = 12, int hardMax = 135)
    {
        if (trackRc.bottom <= trackRc.top)
            return false;

        const int fullSpan = (std::max)(1, hardMax - hardMin);
        int visibleSpan = (std::max)(1, currentMax - currentMin);
        visibleSpan = (std::min)(visibleSpan, fullSpan);
        const int travelSpan = (std::max)(0, fullSpan - visibleSpan);
        if (travelSpan <= 0)
            return false;

        const RECT thumbRc = ComputeMidiSliderThumbRect(trackRc, currentMin, currentMax, hardMin, hardMax);
        const int trackH = (std::max)(1, (int)(trackRc.bottom - trackRc.top));
        const int thumbH = (std::max)(1, (int)(thumbRc.bottom - thumbRc.top));
        const int travelPx = (std::max)(0, trackH - thumbH);

        int thumbTop = y - thumbH / 2;
        thumbTop = (std::max)((int)trackRc.top, (std::min)(thumbTop, (int)trackRc.bottom - thumbH));
        const double u = (travelPx > 0) ? ((double)(thumbTop - trackRc.top) / (double)travelPx) : 0.0;
        const int offset = (int)std::lround(u * (double)travelSpan);

        outMin = hardMin + (std::max)(0, (std::min)(offset, travelSpan));
        outMax = outMin + visibleSpan;
        return true;
    }
}

