#include "BgfxRenderer.h"

#include "FrameStats.h"
#include "scene/WaveScene.h"

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

bgfx::VertexLayout BgfxRenderer::VertexPosColor::layout;
bgfx::VertexLayout BgfxRenderer::VertexPosTexColor::layout;

namespace
{
    const char* RendererFolder(bgfx::RendererType::Enum type)
    {
        switch (type)
        {
        case bgfx::RendererType::Direct3D11: return "dx11";
        case bgfx::RendererType::Direct3D12: return "dx12";
        case bgfx::RendererType::Metal: return "metal";
        case bgfx::RendererType::OpenGL: return "glsl";
        case bgfx::RendererType::OpenGLES: return "essl";
        case bgfx::RendererType::Vulkan: return "spirv";
        default: return "";
        }
    }

    template <typename T>
    bool IsValid(T handle)
    {
        return bgfx::isValid(handle);
    }
}

bool BgfxRenderer::Initialize(const RendererConfig& cfg)
{
    if (initialized_)
        return true;

    width_ = (std::max)(1u, cfg.width);
    height_ = (std::max)(1u, cfg.height);
    resetFlags_ = cfg.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;

    bgfx::PlatformData pd{};
    pd.nwh = cfg.nativeWindowHandle;
    pd.ndt = cfg.nativeDisplayHandle;
    bgfx::setPlatformData(pd);

    bgfx::Init init{};
#if defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11;
#else
    init.type = bgfx::RendererType::Count;
#endif
    init.resolution.width = width_;
    init.resolution.height = height_;
    init.resolution.reset = resetFlags_;

    if (!bgfx::init(init))
        return false;

    VertexPosColor::layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    VertexPosTexColor::layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x111111ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width_), static_cast<std::uint16_t>(height_));

    initialized_ = true;
    return true;
}

void BgfxRenderer::Shutdown()
{
    if (!initialized_)
        return;

    DestroyGpuObjects();
    bgfx::shutdown();
    initialized_ = false;
}

void BgfxRenderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (!initialized_)
        return;

    width_ = (std::max)(1u, width);
    height_ = (std::max)(1u, height);
    bgfx::reset(width_, height_, resetFlags_);
    bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width_), static_cast<std::uint16_t>(height_));
}

void BgfxRenderer::Render(const FrameStats& stats, const WaveScene& scene)
{
    if (!initialized_)
        return;

    bgfx::setViewRect(0, 0, 0, static_cast<std::uint16_t>(width_), static_cast<std::uint16_t>(height_));

    const std::array<float, 16> identity =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    bgfx::setViewTransform(0, identity.data(), identity.data());
    bgfx::touch(0);

    if (!programsReady_)
        programsReady_ = EnsurePrograms();

    const bool textureReady = programsReady_ ? EnsureSpectrogramTexture(scene) : false;
    if (programsReady_ && textureReady)
    {
        DrawSpectrogram(scene);
        DrawWaveLayers(scene);
        DrawGridAndPlayhead(scene);
    }

    DrawFrameHud(stats);
    bgfx::frame();
}

void BgfxRenderer::DestroyGpuObjects()
{
    if (IsValid(spectrogramTexture_))
    {
        bgfx::destroy(spectrogramTexture_);
        spectrogramTexture_ = BGFX_INVALID_HANDLE;
    }
    spectrogramW_ = 0;
    spectrogramH_ = 0;
    spectrogramRevision_ = 0;

    if (IsValid(s_texColor_))
    {
        bgfx::destroy(s_texColor_);
        s_texColor_ = BGFX_INVALID_HANDLE;
    }

    if (IsValid(colorProgram_))
    {
        bgfx::destroy(colorProgram_);
        colorProgram_ = BGFX_INVALID_HANDLE;
    }
    if (IsValid(texturedProgram_))
    {
        bgfx::destroy(texturedProgram_);
        texturedProgram_ = BGFX_INVALID_HANDLE;
    }

    programsReady_ = false;
}

bool BgfxRenderer::EnsurePrograms()
{
    if (IsValid(colorProgram_) && IsValid(texturedProgram_) && IsValid(s_texColor_))
        return true;

    if (!IsValid(s_texColor_))
        s_texColor_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);

    if (!IsValid(colorProgram_))
        colorProgram_ = LoadProgram("vs_color.bin", "fs_color.bin");
    if (!IsValid(texturedProgram_))
        texturedProgram_ = LoadProgram("vs_texture.bin", "fs_texture.bin");

    return IsValid(colorProgram_) && IsValid(texturedProgram_) && IsValid(s_texColor_);
}

bool BgfxRenderer::EnsureSpectrogramTexture(const WaveScene& scene)
{
    const int w = (std::max)(1, scene.spectrogramWidth());
    const int h = (std::max)(1, scene.spectrogramHeight());
    const auto& pixels = scene.spectrogramBgra();
    const std::uint64_t revision = scene.spectrogramRevision();
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * sizeof(std::uint32_t);
    if (pixels.size() * sizeof(std::uint32_t) < bytes)
        return false;

    bool recreated = false;
    if (!IsValid(spectrogramTexture_) || spectrogramW_ != static_cast<std::uint16_t>(w) || spectrogramH_ != static_cast<std::uint16_t>(h))
    {
        if (IsValid(spectrogramTexture_))
            bgfx::destroy(spectrogramTexture_);

        spectrogramTexture_ = bgfx::createTexture2D(
            static_cast<std::uint16_t>(w),
            static_cast<std::uint16_t>(h),
            false,
            1,
            bgfx::TextureFormat::BGRA8,
            BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        spectrogramW_ = static_cast<std::uint16_t>(w);
        spectrogramH_ = static_cast<std::uint16_t>(h);
        recreated = true;
    }

    if (!IsValid(spectrogramTexture_))
        return false;

    if (recreated || spectrogramRevision_ != revision)
    {
        const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(bytes));
        bgfx::updateTexture2D(
            spectrogramTexture_,
            0,
            0,
            0,
            0,
            static_cast<std::uint16_t>(w),
            static_cast<std::uint16_t>(h),
            mem,
            static_cast<std::uint16_t>(w * static_cast<int>(sizeof(std::uint32_t))));
        spectrogramRevision_ = revision;
    }
    return true;
}

void BgfxRenderer::DrawSpectrogram(const WaveScene& scene)
{
    if (!IsValid(texturedProgram_) || !IsValid(spectrogramTexture_) || !IsValid(s_texColor_))
        return;

    if (bgfx::getAvailTransientVertexBuffer(4, VertexPosTexColor::layout) < 4 ||
        bgfx::getAvailTransientIndexBuffer(6) < 6)
    {
        return;
    }

    bgfx::TransientVertexBuffer tvb{};
    bgfx::TransientIndexBuffer tib{};
    bgfx::allocTransientVertexBuffer(&tvb, 4, VertexPosTexColor::layout);
    bgfx::allocTransientIndexBuffer(&tib, 6);

    auto* v = reinterpret_cast<VertexPosTexColor*>(tvb.data);
    constexpr float yTop = 0.10f;
    constexpr float yBottom = -1.0f;
    const float u0 = scene.spectrogramU0();
    const float u1 = scene.spectrogramU1();
    v[0] = { -1.0f, yTop,    0.0f, u0, 0.0f, 0xffffffffu };
    v[1] = {  1.0f, yTop,    0.0f, u1, 0.0f, 0xffffffffu };
    v[2] = {  1.0f, yBottom, 0.0f, u1, 1.0f, 0xffffffffu };
    v[3] = { -1.0f, yBottom, 0.0f, u0, 1.0f, 0xffffffffu };

    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    idx[0] = 0; idx[1] = 1; idx[2] = 2;
    idx[3] = 0; idx[4] = 2; idx[5] = 3;

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, s_texColor_, spectrogramTexture_);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA);
    bgfx::submit(0, texturedProgram_);
}

void BgfxRenderer::DrawWaveLayers(const WaveScene& scene)
{
    if (!IsValid(colorProgram_))
        return;

    constexpr float waveTop = 0.98f;
    constexpr float waveBottom = 0.14f;
    constexpr float waveMid = (waveTop + waveBottom) * 0.5f;
    constexpr float amp = (waveTop - waveBottom) * 0.47f;

    const int bins = (std::max)(1, scene.visibleBins());
    const std::size_t maxRects = static_cast<std::size_t>(bins) * 2u * 4u;
    std::vector<VertexPosColor> vertices;
    std::vector<std::uint16_t> indices;
    vertices.reserve(maxRects * 4u);
    indices.reserve(maxRects * 6u);

    auto clampY = [](float y) -> float
    {
        return (std::max)(waveBottom, (std::min)(waveTop, y));
    };

    auto appendRect = [&](float x0, float x1, float y0, float y1, std::uint32_t c)
    {
        if (y1 <= y0 || x1 <= x0)
            return;
        if (vertices.size() > static_cast<std::size_t>(0xfff0))
            return;

        const std::uint16_t base = static_cast<std::uint16_t>(vertices.size());
        vertices.push_back({ x0, y0, 0.0f, c });
        vertices.push_back({ x1, y0, 0.0f, c });
        vertices.push_back({ x1, y1, 0.0f, c });
        vertices.push_back({ x0, y1, 0.0f, c });
        indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
        indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
    };

    auto appendLayer = [&](const std::vector<float>& minV, const std::vector<float>& maxV, std::uint32_t color)
    {
        const int n = (std::min)(bins, static_cast<int>((std::min)(minV.size(), maxV.size())));
        for (int i = 0; i < n; ++i)
        {
            const float x0 = -1.0f + 2.0f * (static_cast<float>(i) / static_cast<float>(bins));
            const float x1 = -1.0f + 2.0f * (static_cast<float>(i + 1) / static_cast<float>(bins));

            const float yp = clampY(waveMid - (std::max)(0.0f, maxV[i]) * amp);
            const float yn = clampY(waveMid - (std::min)(0.0f, minV[i]) * amp);

            appendRect(x0, x1, (std::min)(yp, waveMid), (std::max)(yp, waveMid), color);
            appendRect(x0, x1, (std::min)(yn, waveMid), (std::max)(yn, waveMid), color);
        }
    };

    appendLayer(scene.baseMin(), scene.baseMax(), Abgr(200, 200, 200, 38));
    appendLayer(scene.lowMin(), scene.lowMax(), Abgr(0, 140, 255, 178));
    appendLayer(scene.midMin(), scene.midMax(), Abgr(255, 170, 0, 178));
    appendLayer(scene.highMin(), scene.highMax(), Abgr(255, 60, 140, 185));

    if (vertices.empty() || indices.empty())
        return;

    const std::uint32_t vcount = static_cast<std::uint32_t>(vertices.size());
    const std::uint32_t icount = static_cast<std::uint32_t>(indices.size());
    if (bgfx::getAvailTransientVertexBuffer(vcount, VertexPosColor::layout) < vcount ||
        bgfx::getAvailTransientIndexBuffer(icount) < icount)
    {
        return;
    }

    bgfx::TransientVertexBuffer tvb{};
    bgfx::TransientIndexBuffer tib{};
    bgfx::allocTransientVertexBuffer(&tvb, vcount, VertexPosColor::layout);
    bgfx::allocTransientIndexBuffer(&tib, icount);

    std::memcpy(tvb.data, vertices.data(), vertices.size() * sizeof(VertexPosColor));
    std::memcpy(tib.data, indices.data(), indices.size() * sizeof(std::uint16_t));

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
    bgfx::submit(0, colorProgram_);
}

void BgfxRenderer::DrawGridAndPlayhead(const WaveScene& scene)
{
    if (!IsValid(colorProgram_))
        return;

    constexpr float waveTop = 0.98f;
    constexpr float waveBottom = 0.14f;

    std::vector<VertexPosColor> lines;
    lines.reserve((scene.beatLinesX().size() + scene.barLinesX().size() + 1u) * 2u);

    auto appendLine = [&](float xNorm, std::uint32_t c)
    {
        const float x = -1.0f + 2.0f * xNorm;
        lines.push_back({ x, waveTop, 0.0f, c });
        lines.push_back({ x, waveBottom, 0.0f, c });
    };

    for (float x : scene.beatLinesX())
        appendLine(x, Abgr(255, 165, 0, 170));
    for (float x : scene.barLinesX())
        appendLine(x, Abgr(255, 64, 64, 220));

    appendLine(scene.playheadXNorm(), Abgr(255, 255, 255, 255));

    const std::uint32_t vcount = static_cast<std::uint32_t>(lines.size());
    if (vcount < 2)
        return;
    if (bgfx::getAvailTransientVertexBuffer(vcount, VertexPosColor::layout) < vcount)
        return;

    bgfx::TransientVertexBuffer tvb{};
    bgfx::allocTransientVertexBuffer(&tvb, vcount, VertexPosColor::layout);
    std::memcpy(tvb.data, lines.data(), lines.size() * sizeof(VertexPosColor));

    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_PT_LINES | BGFX_STATE_MSAA);
    bgfx::submit(0, colorProgram_);
}

void BgfxRenderer::DrawFrameHud(const FrameStats& stats)
{
    bgfx::dbgTextClear();
    bgfx::dbgTextPrintf(0, 0, 0x0f, "waveOut xplat rewrite (GPU path)");
    bgfx::dbgTextPrintf(0, 1, 0x0a, "frame %7.3f ms  avg %7.3f ms  fps %6.1f",
        stats.lastFrameMs(), stats.avgFrameMs(), stats.fps());
    bgfx::dbgTextPrintf(0, 2, 0x07, "shaders: %s",
        programsReady_ ? "loaded" : "missing (compile assets/shaders)");
}

std::uint32_t BgfxRenderer::Abgr(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) const
{
    return (static_cast<std::uint32_t>(a) << 24) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(g) << 8) |
           static_cast<std::uint32_t>(r);
}

bgfx::ShaderHandle BgfxRenderer::LoadShader(std::string_view name) const
{
    const auto type = bgfx::getRendererType();
    const std::string rendererDir = RendererFolder(type);
    if (rendererDir.empty())
        return BGFX_INVALID_HANDLE;

    std::vector<std::filesystem::path> roots;
    roots.emplace_back(std::filesystem::current_path());
#ifdef WAVEOUT_XPLAT_SOURCE_DIR
    roots.emplace_back(std::filesystem::path(WAVEOUT_XPLAT_SOURCE_DIR));
    roots.emplace_back(std::filesystem::path(WAVEOUT_XPLAT_SOURCE_DIR).parent_path());
#endif

    for (const auto& root : roots)
    {
        const std::array<std::filesystem::path, 2> candidates =
        {
            root / "assets" / "shaders" / rendererDir / std::string(name),
            root / "xplat" / "assets" / "shaders" / rendererDir / std::string(name)
        };

        for (const auto& path : candidates)
        {
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs)
                continue;

            std::vector<char> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            if (bytes.empty())
                continue;

            const bgfx::Memory* mem = bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
            return bgfx::createShader(mem);
        }
    }

    return BGFX_INVALID_HANDLE;
}

bgfx::ProgramHandle BgfxRenderer::LoadProgram(std::string_view vsName, std::string_view fsName) const
{
    const bgfx::ShaderHandle vs = LoadShader(vsName);
    const bgfx::ShaderHandle fs = LoadShader(fsName);
    if (!IsValid(vs) || !IsValid(fs))
    {
        if (IsValid(vs)) bgfx::destroy(vs);
        if (IsValid(fs)) bgfx::destroy(fs);
        return BGFX_INVALID_HANDLE;
    }
    return bgfx::createProgram(vs, fs, true);
}
