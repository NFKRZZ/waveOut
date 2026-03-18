#include "scene/WaveScene.h"

#include "AudioFileLoader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    float Clamp11(float v)
    {
        if (v < -1.0f) return -1.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    float Clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    std::uint32_t Bgra(std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a = 255)
    {
        return (static_cast<std::uint32_t>(a) << 24) |
            (static_cast<std::uint32_t>(r) << 16) |
            (static_cast<std::uint32_t>(g) << 8) |
            static_cast<std::uint32_t>(b);
    }

    double OnePoleAlpha(int sampleRate, double cutoffHz)
    {
        if (sampleRate <= 0 || cutoffHz <= 0.0)
            return 1.0;
        const double dt = 1.0 / static_cast<double>(sampleRate);
        const double rc = 1.0 / (2.0 * kPi * cutoffHz);
        return dt / (rc + dt);
    }
}

bool WaveScene::Initialize()
{
    if (initialized_)
        return true;

    songDurationSeconds_ = 180.0;
    minVisibleWindowSeconds_ = 1.0;
    maxVisibleWindowSeconds_ = 30.0;
    visibleWindowSeconds_ = 8.0;
    zoomFactor_ = songDurationSeconds_ / visibleWindowSeconds_;
    playbackSeconds_ = 0.0;
    paused_ = false;

    BuildSongEnvelopes();
    BuildSongSpectrogram();
    UpdateViewport();
    BuildVisibleEnvelopeWindow();
    BuildVisibleGridLines();

    initialized_ = true;
    return true;
}

bool WaveScene::LoadAudioFile(const std::string& path)
{
    audiofile::DecodedPcm16 decoded{};
    std::string error;
    if (!audiofile::AudioFileLoader::LoadPcm16(path, decoded, &error))
        return false;

    if (decoded.sampleRate <= 0 || decoded.channels <= 0 || decoded.samples.empty())
        return false;

    const std::size_t channels = static_cast<std::size_t>((std::max)(1, decoded.channels));
    const std::size_t totalFrames = decoded.samples.size() / channels;
    if (totalFrames == 0)
        return false;

    std::vector<float> mono(totalFrames, 0.0f);
    for (std::size_t i = 0; i < totalFrames; ++i)
    {
        const std::size_t base = i * channels;
        int sum = 0;
        for (std::size_t c = 0; c < channels; ++c)
            sum += static_cast<int>(decoded.samples[base + c]);
        const double avg = static_cast<double>(sum) / static_cast<double>(channels);
        mono[i] = static_cast<float>(avg / 32768.0);
    }

    BuildSongEnvelopesFromMono(mono, decoded.sampleRate);
    BuildSongSpectrogramFromEnvelopes();

    playbackSeconds_ = 0.0;
    paused_ = false;
    playbackRate_ = 1.0;
    UpdateViewport();
    BuildVisibleEnvelopeWindow();
    BuildVisibleGridLines();

    initialized_ = true;
    return true;
}

void WaveScene::NudgePlayback(double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds))
        return;
    if (songDurationSeconds_ <= 0.0)
        return;

    playbackSeconds_ += deltaSeconds;
    while (playbackSeconds_ >= songDurationSeconds_)
        playbackSeconds_ -= songDurationSeconds_;
    while (playbackSeconds_ < 0.0)
        playbackSeconds_ += songDurationSeconds_;

    UpdateViewport();
    BuildVisibleEnvelopeWindow();
    BuildVisibleGridLines();
}

void WaveScene::AdjustZoom(double factor)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    if (songDurationSeconds_ <= 0.0)
        return;

    zoomFactor_ = (std::clamp)(zoomFactor_ * factor, 1.0, 512.0);

    double minWindow = minVisibleWindowSeconds_;
    double maxWindow = maxVisibleWindowSeconds_;
    if (maxWindow < minWindow)
        std::swap(maxWindow, minWindow);

    if (songDurationSeconds_ < minWindow)
        minWindow = songDurationSeconds_;
    if (songDurationSeconds_ < maxWindow)
        maxWindow = songDurationSeconds_;

    const double targetWindow = songDurationSeconds_ / zoomFactor_;
    visibleWindowSeconds_ = (std::clamp)(targetWindow, minWindow, maxWindow);
    zoomFactor_ = songDurationSeconds_ / (std::max)(visibleWindowSeconds_, 1e-6);

    UpdateViewport();
    BuildVisibleEnvelopeWindow();
    BuildVisibleGridLines();
}

void WaveScene::Tick(double deltaSeconds)
{
    if (!initialized_)
        return;

    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0)
        deltaSeconds = 1.0 / 60.0;

    if (!paused_ && songDurationSeconds_ > 0.0)
    {
        playbackSeconds_ += deltaSeconds * playbackRate_;
        while (playbackSeconds_ >= songDurationSeconds_)
            playbackSeconds_ -= songDurationSeconds_;
        while (playbackSeconds_ < 0.0)
            playbackSeconds_ += songDurationSeconds_;
    }

    UpdateViewport();
    BuildVisibleEnvelopeWindow();
    BuildVisibleGridLines();
}

void WaveScene::BuildSongEnvelopes()
{
    songBaseMin_.assign(songBins_, 0.0f);
    songBaseMax_.assign(songBins_, 0.0f);
    songLowMin_.assign(songBins_, 0.0f);
    songLowMax_.assign(songBins_, 0.0f);
    songMidMin_.assign(songBins_, 0.0f);
    songMidMax_.assign(songBins_, 0.0f);
    songHighMin_.assign(songBins_, 0.0f);
    songHighMax_.assign(songBins_, 0.0f);

    for (int i = 0; i < songBins_; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>((std::max)(1, songBins_ - 1));
        const double sec = t * songDurationSeconds_;

        const double phrA = 2.0 * kPi * (0.12 * sec);
        const double phrB = 2.0 * kPi * (0.37 * sec);
        const double phrC = 2.0 * kPi * (0.93 * sec);
        const double phrD = 2.0 * kPi * (1.61 * sec);

        const float modSlow = static_cast<float>(0.55 + 0.45 * std::sin(phrA));
        const float modFast = static_cast<float>(0.55 + 0.45 * std::sin(phrB));

        const float baseA = Clamp11(static_cast<float>(0.65 * modSlow + 0.20 * std::sin(phrC)));
        const float lowA = Clamp11(static_cast<float>(0.75 * modSlow + 0.15 * std::sin(phrD)));
        const float midA = Clamp11(static_cast<float>(0.52 * modFast + 0.28 * std::sin(phrB * 1.7)));
        const float highA = Clamp11(static_cast<float>(0.42 * modFast + 0.36 * std::sin(phrD * 2.1)));

        const float jitter = static_cast<float>(0.06 * std::sin(2.0 * kPi * sec * 7.0));

        songBaseMax_[i] = Clamp11(baseA + jitter);
        songBaseMin_[i] = Clamp11(-baseA + jitter * 0.3f);
        songLowMax_[i] = Clamp11(lowA + jitter * 0.5f);
        songLowMin_[i] = Clamp11(-lowA + jitter * 0.2f);
        songMidMax_[i] = Clamp11(midA + jitter * 0.6f);
        songMidMin_[i] = Clamp11(-midA + jitter * 0.4f);
        songHighMax_[i] = Clamp11(highA + jitter * 0.9f);
        songHighMin_[i] = Clamp11(-highA + jitter * 0.5f);
    }
}

void WaveScene::BuildSongEnvelopesFromMono(const std::vector<float>& mono, int sampleRate)
{
    if (mono.empty() || sampleRate <= 0)
    {
        BuildSongEnvelopes();
        return;
    }

    songDurationSeconds_ = static_cast<double>(mono.size()) / static_cast<double>(sampleRate);
    if (!std::isfinite(songDurationSeconds_) || songDurationSeconds_ <= 0.0)
        songDurationSeconds_ = 1.0;

    songBins_ = (std::clamp)(static_cast<int>(std::llround(songDurationSeconds_ * 220.0)), 8192, 131072);

    songBaseMin_.assign(songBins_, 1.0f);
    songBaseMax_.assign(songBins_, -1.0f);
    songLowMin_.assign(songBins_, 1.0f);
    songLowMax_.assign(songBins_, -1.0f);
    songMidMin_.assign(songBins_, 1.0f);
    songMidMax_.assign(songBins_, -1.0f);
    songHighMin_.assign(songBins_, 1.0f);
    songHighMax_.assign(songBins_, -1.0f);
    std::vector<std::uint32_t> counts(songBins_, 0u);

    const double aLow = OnePoleAlpha(sampleRate, 220.0);
    const double aMid = OnePoleAlpha(sampleRate, 2200.0);
    double lpLow = 0.0;
    double lpMid = 0.0;

    const std::size_t total = mono.size();
    for (std::size_t i = 0; i < total; ++i)
    {
        const double x = static_cast<double>(mono[i]);
        lpLow += aLow * (x - lpLow);
        lpMid += aMid * (x - lpMid);

        const float base = Clamp11(static_cast<float>(x));
        const float low = Clamp11(static_cast<float>(lpLow));
        const float mid = Clamp11(static_cast<float>(lpMid - lpLow));
        const float high = Clamp11(static_cast<float>(x - lpMid));

        const int bin = (std::min)(songBins_ - 1,
            static_cast<int>((static_cast<double>(i) * static_cast<double>(songBins_)) / static_cast<double>(total)));

        songBaseMin_[bin] = (std::min)(songBaseMin_[bin], base);
        songBaseMax_[bin] = (std::max)(songBaseMax_[bin], base);
        songLowMin_[bin] = (std::min)(songLowMin_[bin], low);
        songLowMax_[bin] = (std::max)(songLowMax_[bin], low);
        songMidMin_[bin] = (std::min)(songMidMin_[bin], mid);
        songMidMax_[bin] = (std::max)(songMidMax_[bin], mid);
        songHighMin_[bin] = (std::min)(songHighMin_[bin], high);
        songHighMax_[bin] = (std::max)(songHighMax_[bin], high);
        counts[bin] += 1u;
    }

    for (int i = 0; i < songBins_; ++i)
    {
        if (counts[i] == 0u)
        {
            if (i > 0)
            {
                songBaseMin_[i] = songBaseMin_[i - 1];
                songBaseMax_[i] = songBaseMax_[i - 1];
                songLowMin_[i] = songLowMin_[i - 1];
                songLowMax_[i] = songLowMax_[i - 1];
                songMidMin_[i] = songMidMin_[i - 1];
                songMidMax_[i] = songMidMax_[i - 1];
                songHighMin_[i] = songHighMin_[i - 1];
                songHighMax_[i] = songHighMax_[i - 1];
            }
            else
            {
                songBaseMin_[i] = songBaseMax_[i] = 0.0f;
                songLowMin_[i] = songLowMax_[i] = 0.0f;
                songMidMin_[i] = songMidMax_[i] = 0.0f;
                songHighMin_[i] = songHighMax_[i] = 0.0f;
            }
        }
    }

    auto normalizePair = [](std::vector<float>& mn, std::vector<float>& mx)
    {
        float absMax = 0.0f;
        for (std::size_t i = 0; i < mn.size() && i < mx.size(); ++i)
        {
            absMax = (std::max)(absMax, std::fabs(mn[i]));
            absMax = (std::max)(absMax, std::fabs(mx[i]));
        }
        const float inv = (absMax > 1e-6f) ? (1.0f / absMax) : 1.0f;
        for (std::size_t i = 0; i < mn.size() && i < mx.size(); ++i)
        {
            mn[i] = Clamp11(mn[i] * inv);
            mx[i] = Clamp11(mx[i] * inv);
        }
    };

    normalizePair(songBaseMin_, songBaseMax_);
    normalizePair(songLowMin_, songLowMax_);
    normalizePair(songMidMin_, songMidMax_);
    normalizePair(songHighMin_, songHighMax_);

    maxVisibleWindowSeconds_ = (std::min)(30.0, songDurationSeconds_);
    minVisibleWindowSeconds_ = (std::min)(1.0, maxVisibleWindowSeconds_);
    visibleWindowSeconds_ = (std::clamp)(8.0, minVisibleWindowSeconds_, maxVisibleWindowSeconds_);
    zoomFactor_ = songDurationSeconds_ / (std::max)(visibleWindowSeconds_, 1e-6);
}

void WaveScene::BuildSongSpectrogram()
{
    const int w = (std::max)(1, songSpectrogramWidth_);
    const int h = (std::max)(1, spectrogramHeight_);
    songSpectrogramBgra_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0u);

    for (int y = 0; y < h; ++y)
    {
        const double f = static_cast<double>(y) / static_cast<double>(h - 1);
        for (int x = 0; x < w; ++x)
        {
            const double t = static_cast<double>(x) / static_cast<double>(w - 1);
            const double n =
                0.45 * (0.5 + 0.5 * std::sin(2.0 * kPi * (t * 12.0 + f * 1.5))) +
                0.30 * (0.5 + 0.5 * std::sin(2.0 * kPi * (t * 31.0 - f * 7.3))) +
                0.25 * (0.5 + 0.5 * std::sin(2.0 * kPi * (t * 7.0 + f * 14.0)));

            const double roll = std::pow(1.0 - f, 0.42);
            const double mag = (std::max)(0.0, (std::min)(1.0, n * roll));

            const std::uint8_t r = static_cast<std::uint8_t>((std::min)(255.0, 255.0 * std::pow(mag, 0.70)));
            const std::uint8_t g = static_cast<std::uint8_t>((std::min)(255.0, 255.0 * std::pow(mag, 1.05)));
            const std::uint8_t b = static_cast<std::uint8_t>((std::min)(255.0, 255.0 * std::pow(mag, 1.75)));

            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            songSpectrogramBgra_[idx] = Bgra(b, g, r, 255);
        }
    }

    ++spectrogramRevision_;
}

void WaveScene::BuildSongSpectrogramFromEnvelopes()
{
    const int bins = (std::max)(1, songBins_);
    songSpectrogramWidth_ = (std::clamp)(bins / 2, 1024, 8192);
    const int w = (std::max)(1, songSpectrogramWidth_);
    const int h = (std::max)(1, spectrogramHeight_);

    songSpectrogramBgra_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Bgra(0, 0, 0, 255));

    std::vector<float> lowW(static_cast<std::size_t>(h), 0.0f);
    std::vector<float> midW(static_cast<std::size_t>(h), 0.0f);
    std::vector<float> highW(static_cast<std::size_t>(h), 0.0f);
    std::vector<float> airW(static_cast<std::size_t>(h), 0.0f);

    for (int y = 0; y < h; ++y)
    {
        const float yf = static_cast<float>(y) / static_cast<float>((std::max)(1, h - 1));
        const float top = 1.0f - yf;
        lowW[static_cast<std::size_t>(y)] = std::pow(yf, 2.2f);
        highW[static_cast<std::size_t>(y)] = std::pow(top, 2.2f);
        const float d = (yf - 0.48f) / 0.22f;
        midW[static_cast<std::size_t>(y)] = std::exp(-(d * d));
        airW[static_cast<std::size_t>(y)] = std::pow(top, 4.6f);
    }

    for (int x = 0; x < w; ++x)
    {
        const double songIdx = (static_cast<double>(x) / static_cast<double>((std::max)(1, w - 1))) * static_cast<double>((std::max)(1, bins - 1));
        const float low = 0.5f * (std::fabs(SampleLinear(songLowMin_, songIdx)) + std::fabs(SampleLinear(songLowMax_, songIdx)));
        const float mid = 0.5f * (std::fabs(SampleLinear(songMidMin_, songIdx)) + std::fabs(SampleLinear(songMidMax_, songIdx)));
        const float high = 0.5f * (std::fabs(SampleLinear(songHighMin_, songIdx)) + std::fabs(SampleLinear(songHighMax_, songIdx)));

        const float sparkle = 0.5f + 0.5f * std::sin(static_cast<float>((2.0 * kPi * x) / 57.0));

        for (int y = 0; y < h; ++y)
        {
            const float lw = lowW[static_cast<std::size_t>(y)];
            const float mw = midW[static_cast<std::size_t>(y)];
            const float hw = highW[static_cast<std::size_t>(y)];
            const float aw = airW[static_cast<std::size_t>(y)];

            const float energy = Clamp01(low * lw + mid * mw + high * hw + 0.12f * high * aw * sparkle);
            const float rF = Clamp01(0.04f + energy * (0.32f * lw + 0.82f * mw + 1.25f * hw));
            const float gF = Clamp01(0.03f + energy * (0.18f * lw + 0.76f * mw + 0.52f * hw));
            const float bF = Clamp01(0.02f + energy * (1.08f * lw + 0.40f * mw + 0.18f * hw));

            const std::uint8_t r = static_cast<std::uint8_t>(255.0f * std::pow(rF, 0.85f));
            const std::uint8_t g = static_cast<std::uint8_t>(255.0f * std::pow(gF, 1.00f));
            const std::uint8_t b = static_cast<std::uint8_t>(255.0f * std::pow(bF, 1.20f));

            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
            songSpectrogramBgra_[idx] = Bgra(b, g, r, 255);
        }
    }

    ++spectrogramRevision_;
}

void WaveScene::UpdateViewport()
{
    if (!std::isfinite(songDurationSeconds_) || songDurationSeconds_ <= 0.0)
    {
        viewportLeftSeconds_ = 0.0;
        viewportRightSeconds_ = 1.0;
        spectrogramU0_ = 0.0f;
        spectrogramU1_ = 1.0f;
        return;
    }

    if (!std::isfinite(playbackSeconds_))
        playbackSeconds_ = 0.0;
    while (playbackSeconds_ >= songDurationSeconds_)
        playbackSeconds_ -= songDurationSeconds_;
    while (playbackSeconds_ < 0.0)
        playbackSeconds_ += songDurationSeconds_;

    visibleWindowSeconds_ = (std::clamp)(visibleWindowSeconds_, minVisibleWindowSeconds_, (std::max)(minVisibleWindowSeconds_, maxVisibleWindowSeconds_));

    const double maxLeft = (std::max)(0.0, songDurationSeconds_ - visibleWindowSeconds_);
    double left = playbackSeconds_ - visibleWindowSeconds_ * static_cast<double>(playheadXNorm_);
    left = (std::clamp)(left, 0.0, maxLeft);

    viewportLeftSeconds_ = left;
    viewportRightSeconds_ = left + visibleWindowSeconds_;

    spectrogramU0_ = Clamp01(static_cast<float>(viewportLeftSeconds_ / songDurationSeconds_));
    spectrogramU1_ = Clamp01(static_cast<float>(viewportRightSeconds_ / songDurationSeconds_));
    if (spectrogramU1_ <= spectrogramU0_)
    {
        const float eps = 1.0f / static_cast<float>((std::max)(1, songSpectrogramWidth_));
        spectrogramU1_ = (std::min)(1.0f, spectrogramU0_ + eps);
    }
}

void WaveScene::BuildVisibleEnvelopeWindow()
{
    const int bins = (std::max)(1, visibleBins_);
    baseMin_.assign(bins, 0.0f);
    baseMax_.assign(bins, 0.0f);
    lowMin_.assign(bins, 0.0f);
    lowMax_.assign(bins, 0.0f);
    midMin_.assign(bins, 0.0f);
    midMax_.assign(bins, 0.0f);
    highMin_.assign(bins, 0.0f);
    highMax_.assign(bins, 0.0f);

    const double tLeft = viewportLeftSeconds_;
    const double span = (std::max)(1e-6, visibleWindowSeconds_);

    for (int i = 0; i < bins; ++i)
    {
        const double x = static_cast<double>(i) / static_cast<double>((std::max)(1, bins - 1));
        const double sec = tLeft + x * span;
        const double songNorm = (songDurationSeconds_ > 0.0) ? (sec / songDurationSeconds_) : 0.0;
        const double songIdx = songNorm * static_cast<double>((std::max)(1, songBins_ - 1));

        baseMin_[i] = SampleLinear(songBaseMin_, songIdx);
        baseMax_[i] = SampleLinear(songBaseMax_, songIdx);
        lowMin_[i] = SampleLinear(songLowMin_, songIdx);
        lowMax_[i] = SampleLinear(songLowMax_, songIdx);
        midMin_[i] = SampleLinear(songMidMin_, songIdx);
        midMax_[i] = SampleLinear(songMidMax_, songIdx);
        highMin_[i] = SampleLinear(songHighMin_, songIdx);
        highMax_[i] = SampleLinear(songHighMax_, songIdx);
    }
}

void WaveScene::BuildVisibleGridLines()
{
    beatLinesX_.clear();
    barLinesX_.clear();
    if (!std::isfinite(bpm_) || bpm_ <= 0.0)
        return;

    const double beatT = 60.0 / bpm_;
    const double tLeft = viewportLeftSeconds_;
    const double tRight = viewportRightSeconds_;
    const double span = (std::max)(1e-6, tRight - tLeft);

    const long long k0 = static_cast<long long>(std::floor((tLeft - t0Seconds_) / beatT)) - 2;
    const long long k1 = static_cast<long long>(std::ceil((tRight - t0Seconds_) / beatT)) + 2;
    for (long long k = k0; k <= k1; ++k)
    {
        const double t = t0Seconds_ + static_cast<double>(k) * beatT;
        if (t < tLeft || t > tRight)
            continue;
        const float x = static_cast<float>((t - tLeft) / span);
        if (k % (std::max)(1, beatsPerBar_) == 0)
            barLinesX_.push_back(x);
        else
            beatLinesX_.push_back(x);
    }
}

float WaveScene::SampleLinear(const std::vector<float>& src, double idx) const
{
    if (src.empty())
        return 0.0f;
    if (!std::isfinite(idx))
        idx = 0.0;
    if (idx <= 0.0)
        return src.front();
    const double maxIdx = static_cast<double>(src.size() - 1);
    if (idx >= maxIdx)
        return src.back();

    const std::size_t i0 = static_cast<std::size_t>(std::floor(idx));
    const std::size_t i1 = (std::min)(i0 + 1, src.size() - 1);
    const double t = idx - static_cast<double>(i0);
    return static_cast<float>(src[i0] + (src[i1] - src[i0]) * t);
}