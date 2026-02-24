#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "PianoRollRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace PianoRollRenderer
{
    namespace
    {
        static bool IsBlackKey(int midi)
        {
            switch (midi % 12)
            {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
            }
        }

        static std::wstring NoteName(int midi)
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

        static std::wstring FormatTime(double seconds)
        {
            if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
            int total = static_cast<int>(std::floor(seconds));
            int mins = total / 60;
            int secs = total % 60;
            wchar_t buf[32];
            swprintf_s(buf, L"%d:%02d", mins, secs);
            return std::wstring(buf);
        }

        static double ChooseTimeTickStep(double visibleSeconds)
        {
            static const double steps[] = { 0.1,0.2,0.5,1,2,5,10,15,30,60,120,300 };
            for (double s : steps)
            {
                if (visibleSeconds / s <= 10.0) return s;
            }
            return 600.0;
        }
    }

    void Draw(HDC hdc, const RECT& rc, const ViewState& view, const Config& cfg, const std::vector<NoteEvent>* notes)
    {
        const int w = static_cast<int>(rc.right - rc.left);
        const int h = static_cast<int>(rc.bottom - rc.top);
        if (!hdc || w <= 0 || h <= 0) return;
        if (!(view.tRightSeconds > view.tLeftSeconds)) return;

        const int midiMin = (std::min)(cfg.midiMin, cfg.midiMax);
        const int midiMax = (std::max)(cfg.midiMin, cfg.midiMax);
        const int noteCount = midiMax - midiMin + 1;
        if (noteCount <= 0) return;

        const int keyW = (std::max)(40, (std::min)(120, cfg.pianoKeyWidthPx));
        const RECT keyRc{ rc.left, rc.top, (LONG)(std::min)(static_cast<int>(rc.right), static_cast<int>(rc.left) + keyW), rc.bottom };
        // Align piano-roll beat/time grid to the waveform time axis (full width).
        // The piano key strip is drawn as an overlay on top of the left edge.
        const RECT gridRc{ rc.left, rc.top, rc.right, rc.bottom };
        if (gridRc.right <= gridRc.left) return;

        const double visibleSeconds = view.tRightSeconds - view.tLeftSeconds;
        const double rowH = (double)h / (double)noteCount;

        // Backgrounds
        {
            HBRUSH bg = CreateSolidBrush(RGB(10, 10, 10));
            FillRect(hdc, &rc, bg);
            DeleteObject(bg);

            HBRUSH keyBg = CreateSolidBrush(RGB(20, 20, 20));
            FillRect(hdc, &keyRc, keyBg);
            DeleteObject(keyBg);
        }

        SetBkMode(hdc, TRANSPARENT);
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HFONT oldFont = (HFONT)SelectObject(hdc, font);

        // Horizontal note rows + piano key labels
        HPEN rowPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
        HPEN rowStrongPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        HGDIOBJ oldPen = SelectObject(hdc, rowPen);

        for (int idx = 0; idx < noteCount; ++idx)
        {
            const int midi = midiMax - idx;
            const int y0 = rc.top + (int)std::floor(idx * rowH);
            const int y1 = rc.top + (int)std::floor((idx + 1) * rowH);
            RECT rowRect{ rc.left, (LONG)y0, rc.right, (LONG)(std::max)(y0 + 1, y1) };

            if (IsBlackKey(midi))
            {
                HBRUSH br = CreateSolidBrush(RGB(18, 18, 18));
                FillRect(hdc, &rowRect, br);
                DeleteObject(br);
            }
            else
            {
                HBRUSH br = CreateSolidBrush(RGB(24, 24, 24));
                FillRect(hdc, &rowRect, br);
                DeleteObject(br);
            }

            // key strip row fill
            RECT keyRow{ keyRc.left, (LONG)y0, keyRc.right, (LONG)(std::max)(y0 + 1, y1) };
            HBRUSH keyRowBr = CreateSolidBrush(IsBlackKey(midi) ? RGB(28, 28, 28) : RGB(36, 36, 36));
            FillRect(hdc, &keyRow, keyRowBr);
            DeleteObject(keyRowBr);

            const bool cNote = (midi % 12) == 0;
            SelectObject(hdc, cNote ? rowStrongPen : rowPen);
            MoveToEx(hdc, rc.left, y0, NULL);
            LineTo(hdc, rc.right, y0);

            if (cNote || rowH >= 14.0)
            {
                std::wstring nm = NoteName(midi);
                SetTextColor(hdc, cNote ? RGB(230, 230, 230) : RGB(175, 175, 175));
                TextOutW(hdc, static_cast<int>(keyRc.left) + 6, y0 + 2, nm.c_str(), (int)nm.size());
            }
        }
        // bottom line
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right, rc.bottom - 1);

        // Separator between key strip and roll grid
        HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
        SelectObject(hdc, sepPen);
        MoveToEx(hdc, keyRc.right, rc.top, NULL);
        LineTo(hdc, keyRc.right, rc.bottom);

        // Time grid
        const double timeStep = ChooseTimeTickStep(visibleSeconds);
        const double firstTick = std::floor(view.tLeftSeconds / timeStep) * timeStep;
        HPEN timePen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
        HPEN timeStrongPen = CreatePen(PS_SOLID, 1, RGB(85, 85, 85));
        HPEN beatPen = CreatePen(PS_SOLID, 1, RGB(130, 95, 0));
        HPEN barPen = CreatePen(PS_SOLID, 1, RGB(160, 45, 45));

        for (double t = firstTick; t <= view.tRightSeconds + timeStep * 0.5; t += timeStep)
        {
            if (t < view.tLeftSeconds - 1e-9) continue;
            const double xNorm = (t - view.tLeftSeconds) / visibleSeconds;
            int x = gridRc.left + (int)std::lround(xNorm * (double)(gridRc.right - gridRc.left));
            x = (std::max)(static_cast<int>(gridRc.left), (std::min)(x, static_cast<int>(gridRc.right - 1)));
            const bool strong = std::fmod(std::fabs(t), 5.0) < 1e-6 || std::fmod(std::fabs(t), 10.0) < 1e-6;
            SelectObject(hdc, strong ? timeStrongPen : timePen);
            const int tickLen = strong ? 14 : 8;
            MoveToEx(hdc, x, rc.top, NULL);
            LineTo(hdc, x, (std::min)(static_cast<int>(rc.bottom), static_cast<int>(rc.top) + tickLen));

            std::wstring lbl = FormatTime(t);
            SetTextColor(hdc, RGB(170, 170, 170));
            const int tx = (std::max)(x + 2, static_cast<int>(keyRc.right) + 4);
            TextOutW(hdc, tx, static_cast<int>(rc.top) + 2, lbl.c_str(), (int)lbl.size());
        }

        // Beat/bar grid aligned to waveform grid
        if (view.showBeatGrid && view.bpm > 0.0)
        {
            const double T = 60.0 / view.bpm;
            const int beatsPerBar = (std::max)(1, view.beatsPerBar);
            long long k0 = (long long)std::floor((view.tLeftSeconds - view.t0Seconds) / T) - 2;
            long long k1 = (long long)std::ceil((view.tRightSeconds - view.t0Seconds) / T) + 2;
            for (long long k = k0; k <= k1; ++k)
            {
                const double tg = view.t0Seconds + (double)k * T;
                if (tg < view.tLeftSeconds || tg > view.tRightSeconds) continue;
                const double xNorm = (tg - view.tLeftSeconds) / visibleSeconds;
                int x = gridRc.left + (int)std::lround(xNorm * (double)(gridRc.right - gridRc.left));
                x = (std::max)(static_cast<int>(gridRc.left), (std::min)(x, static_cast<int>(gridRc.right - 1)));
                SelectObject(hdc, ((k % beatsPerBar) == 0) ? barPen : beatPen);
                MoveToEx(hdc, x, rc.top, NULL);
                LineTo(hdc, x, rc.bottom);
            }
        }

        // Placeholder note layer (future MIDI notes)
        if (!notes || notes->empty())
        {
            SetTextColor(hdc, RGB(140, 140, 140));
            const wchar_t* msg = L"Piano Roll (notes pending: audio -> MIDI alignment)";
            TextOutW(hdc,
                (std::max)(static_cast<int>(gridRc.left) + 10, static_cast<int>(keyRc.right) + 10),
                static_cast<int>(rc.top) + 22,
                msg,
                (int)wcslen(msg));
        }
        else
        {
            HBRUSH noteBrush = CreateSolidBrush(RGB(50, 180, 255));
            HBRUSH noteSelBrush = CreateSolidBrush(RGB(255, 170, 0));
            for (const auto& n : *notes)
            {
                if (n.endSeconds <= n.startSeconds) continue;
                if (n.endSeconds < view.tLeftSeconds || n.startSeconds > view.tRightSeconds) continue;
                if (n.midiNote < midiMin || n.midiNote > midiMax) continue;

                const double x0n = (n.startSeconds - view.tLeftSeconds) / visibleSeconds;
                const double x1n = (n.endSeconds - view.tLeftSeconds) / visibleSeconds;
                int x0 = gridRc.left + (int)std::floor(x0n * (double)(gridRc.right - gridRc.left));
                int x1 = gridRc.left + (int)std::ceil(x1n * (double)(gridRc.right - gridRc.left));
                x0 = (std::max)(static_cast<int>(gridRc.left), (std::min)(x0, static_cast<int>(gridRc.right - 1)));
                x1 = (std::max)(x0 + 1, (std::min)(x1, static_cast<int>(gridRc.right)));

                const int idx = midiMax - n.midiNote;
                int y0 = rc.top + (int)std::floor(idx * rowH) + 1;
                int y1 = rc.top + (int)std::floor((idx + 1) * rowH) - 1;
                y1 = (std::max)(y0 + 2, y1);
                RECT nr{ (LONG)x0, (LONG)y0, (LONG)x1, (LONG)y1 };
                FillRect(hdc, &nr, n.selected ? noteSelBrush : noteBrush);
            }
            DeleteObject(noteBrush);
            DeleteObject(noteSelBrush);
        }

        // Playhead
        if (view.playheadSeconds >= view.tLeftSeconds && view.playheadSeconds <= view.tRightSeconds)
        {
            const double xNorm = (view.playheadSeconds - view.tLeftSeconds) / visibleSeconds;
            int x = gridRc.left + (int)std::lround(xNorm * (double)(gridRc.right - gridRc.left));
            x = (std::max)(static_cast<int>(gridRc.left), (std::min)(x, static_cast<int>(gridRc.right - 1)));
            HPEN playPen = CreatePen(PS_SOLID, 2, RGB(245, 245, 245));
            SelectObject(hdc, playPen);
            MoveToEx(hdc, x, rc.top, NULL);
            LineTo(hdc, x, rc.bottom);
            DeleteObject(playPen);
        }

        // Border
        HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
        SelectObject(hdc, borderPen);
        MoveToEx(hdc, rc.left, rc.top, NULL);
        LineTo(hdc, rc.right - 1, rc.top);
        LineTo(hdc, rc.right - 1, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.bottom - 1);
        LineTo(hdc, rc.left, rc.top);

        // Cleanup
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldFont);
        DeleteObject(rowPen);
        DeleteObject(rowStrongPen);
        DeleteObject(sepPen);
        DeleteObject(timePen);
        DeleteObject(timeStrongPen);
        DeleteObject(beatPen);
        DeleteObject(barPen);
        DeleteObject(borderPen);
    }
}
