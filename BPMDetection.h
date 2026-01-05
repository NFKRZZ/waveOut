#pragma once

#include <vector>

namespace BPMDetection
{

    double getBpmMonoAubio(const std::vector<double>& monoPcm, int sampleRate);

};