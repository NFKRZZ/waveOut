#pragma once

#include <vector>

namespace BPMDetection
{
    struct BeatGridEstimate
    {
        double bpm = 0.0;
        double t0 = 0.0;
        double audioStart = 0.0;
        double approxOnset = 0.0;
        double kickAttack = 0.0;
    };

    double getBpmMonoAubio(const std::vector<double>& monoPcm, int sampleRate);
    BeatGridEstimate estimateBeatGridMonoAubio(const std::vector<double>& monoPcm, int sampleRate);

};
