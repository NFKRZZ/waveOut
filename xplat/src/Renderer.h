#pragma once

#include <cstdint>
#include <string_view>

class FrameStats;
class WaveScene;

struct RendererConfig
{
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string_view appName = "waveout_xplat";
    void* nativeWindowHandle = nullptr;
    void* nativeDisplayHandle = nullptr;
    bool vsync = true;
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual bool Initialize(const RendererConfig& cfg) = 0;
    virtual void Shutdown() = 0;
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;
    virtual void Render(const FrameStats& stats, const WaveScene& scene) = 0;
};
