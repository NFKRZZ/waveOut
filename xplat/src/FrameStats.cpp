#include "FrameStats.h"

#include <cmath>

void FrameStats::Tick(double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
        return;

    lastFrameMs_ = deltaSeconds * 1000.0;
    const double instantFps = 1.0 / deltaSeconds;

    if (!initialized_)
    {
        avgFrameMs_ = lastFrameMs_;
        fps_ = instantFps;
        initialized_ = true;
    }
    else
    {
        constexpr double kFrameEmaKeep = 0.90;
        constexpr double kFpsEmaKeep = 0.85;
        avgFrameMs_ = (avgFrameMs_ * kFrameEmaKeep) + (lastFrameMs_ * (1.0 - kFrameEmaKeep));
        fps_ = (fps_ * kFpsEmaKeep) + (instantFps * (1.0 - kFpsEmaKeep));
    }

    ++frameIndex_;
}

