#pragma once

#include <string>
#include <vector>

#include "WaveFormWindow.h"

namespace SpectrogramWindow
{
    // Opens a separate spectrum-analyzer window (FFT magnitude vs frequency) that follows
    // the active waveform window's playback position / play-pause via
    // WaveformWindow::GetPlaybackSyncSnapshot.
    // The audio data pointer must remain valid while the spectrogram window is open.
    void ShowSpectrogramAsyncRefStereoSynced(
        std::vector<short>* interleavedStereoSamples,
        int sampleRate,
        const WaveformWindow::GridOverlayConfig& grid,
        const std::wstring& title = L"Spectrogram");

    // Opens a scrolling piano-roll-scaled spectrogram (Wave Candy style) synced to the
    // active waveform playback state. Includes a processed/raw toggle and simple UI
    // controls for dB range + FFT resolution.
    void ShowPianoSpectrogramAsyncRefStereoSynced(
        std::vector<short>* interleavedStereoSamples,
        int sampleRate,
        const WaveformWindow::GridOverlayConfig& grid,
        const std::wstring& title = L"Piano Spectrogram");
}
