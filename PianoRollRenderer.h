#pragma once

#include <windows.h>
#include <vector>

namespace PianoRollRenderer
{
    enum GridMode
    {
        Grid_None = 0,
        Grid_1_6_Step,
        Grid_1_4_Step,
        Grid_1_3_Step,
        Grid_1_2_Step,
        Grid_Step,
        Grid_1_6_Beat,
        Grid_1_4_Beat,
        Grid_1_3_Beat,
        Grid_1_2_Beat,
        Grid_Beat,
        Grid_Bar,
        Grid_Count
    };

    struct NoteEvent
    {
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        int midiNote = 60;
        int velocity = 100;
        bool selected = false;
    };

    struct ViewState
    {
        double tLeftSeconds = 0.0;
        double tRightSeconds = 0.0;
        double playheadSeconds = 0.0;

        bool showBeatGrid = false;
        double bpm = 0.0;
        double t0Seconds = 0.0;
        int beatsPerBar = 4;
        int gridMode = Grid_Beat;
    };

    struct Config
    {
        int pianoKeyWidthPx = 58;
        int midiMin = 36; // C2
        int midiMax = 84; // C6
    };

    void Draw(HDC hdc, const RECT& rc, const ViewState& view, const Config& cfg = {}, const std::vector<NoteEvent>* notes = nullptr);
}
