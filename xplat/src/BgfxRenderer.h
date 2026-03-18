#pragma once

#include "Renderer.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string_view>
#include <vector>

class BgfxRenderer final : public IRenderer
{
public:
    bool Initialize(const RendererConfig& cfg) override;
    void Shutdown() override;
    void Resize(std::uint32_t width, std::uint32_t height) override;
    void Render(const FrameStats& stats, const WaveScene& scene) override;

private:
    struct VertexPosColor
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
        static bgfx::VertexLayout layout;
    };

    struct VertexPosTexColor
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        std::uint32_t abgr = 0xffffffffu;
        static bgfx::VertexLayout layout;
    };

    void DestroyGpuObjects();
    bool EnsurePrograms();
    bool EnsureSpectrogramTexture(const WaveScene& scene);
    void DrawSpectrogram(const WaveScene& scene);
    void DrawWaveLayers(const WaveScene& scene);
    void DrawGridAndPlayhead(const WaveScene& scene);
    void DrawFrameHud(const FrameStats& stats);
    std::uint32_t Abgr(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) const;

    bgfx::ShaderHandle LoadShader(std::string_view name) const;
    bgfx::ProgramHandle LoadProgram(std::string_view vsName, std::string_view fsName) const;

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t resetFlags_ = 0;
    bgfx::ProgramHandle colorProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle texturedProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texColor_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spectrogramTexture_ = BGFX_INVALID_HANDLE;
    std::uint16_t spectrogramW_ = 0;
    std::uint16_t spectrogramH_ = 0;
    std::uint64_t spectrogramRevision_ = 0;
    bool initialized_ = false;
    bool programsReady_ = false;
};
