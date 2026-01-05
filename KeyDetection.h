#pragma once
#include <array>
#include <vector>
#include <string>
#include <keyfinder/keyfinder.h>
#include "Keys.h"
namespace KeyDetection {
    Key getKey(const std::vector<double>& pcmData, int sampleRate, KeyFinder::KeyFinder& f);

} 