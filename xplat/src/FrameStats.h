#pragma once

#include <cstdint>

class FrameStats
{
public:
    void Tick(double deltaSeconds);

    double lastFrameMs() const { return lastFrameMs_; }
    double avgFrameMs() const { return avgFrameMs_; }
    double fps() const { return fps_; }
    std::uint64_t frameIndex() const { return frameIndex_; }

private:
    double lastFrameMs_ = 0.0;
    double avgFrameMs_ = 0.0;
    double fps_ = 0.0;
    bool initialized_ = false;
    std::uint64_t frameIndex_ = 0;
};

