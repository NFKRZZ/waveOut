#include "App.h"

#include "BgfxRenderer.h"
#include "Renderer.h"
#include "ui/ImGuiLayer.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{
    struct NativeWindowHandles
    {
        void* nwh = nullptr;
        void* ndt = nullptr;
    };

    NativeWindowHandles ResolveNativeHandles(SDL_Window* window)
    {
        NativeWindowHandles out{};
        if (!window)
            return out;

        const SDL_PropertiesID props = SDL_GetWindowProperties(window);
        if (!props)
            return out;

#if defined(_WIN32)
        out.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
        out.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(__linux__)
#ifdef SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER
        out.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
#endif
#ifdef SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER
        out.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
#endif

        if (!out.nwh)
        {
#ifdef SDL_PROP_WINDOW_X11_WINDOW_NUMBER
            const std::uint64_t x11Window = static_cast<std::uint64_t>(
                SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            if (x11Window != 0)
                out.nwh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(x11Window));
#endif
#ifdef SDL_PROP_WINDOW_X11_DISPLAY_POINTER
            out.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
#endif
        }
#endif
        return out;
    }
}

App::App(std::string audioPath)
    : audioPath_(std::move(audioPath))
{
}
App::~App()
{
    Shutdown();
}

int App::Run()
{
    if (!Initialize())
        return 1;

    bool running = true;
    while (running)
    {
        SDL_Event ev{};
        while (SDL_PollEvent(&ev))
            HandleEvent(ev, running);

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - lastFrameStamp_).count();
        lastFrameStamp_ = now;
        frameStats_.Tick(dt);
        scene_.Tick(dt);

        ui_->BeginFrame();
        ui_->DrawOverlay(frameStats_);
        renderer_->Render(frameStats_, scene_);
        ui_->EndFrame();
        UpdateWindowTitle();
    }

    return 0;
}

bool App::Initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return false;

    window_ = SDL_CreateWindow(
        "waveOut xplat",
        static_cast<int>(width_),
        static_cast<int>(height_),
        SDL_WINDOW_RESIZABLE);
    if (!window_)
        return false;

    int pixelW = 0;
    int pixelH = 0;
    SDL_GetWindowSizeInPixels(window_, &pixelW, &pixelH);
    width_ = static_cast<std::uint32_t>((std::max)(1, pixelW));
    height_ = static_cast<std::uint32_t>((std::max)(1, pixelH));

    const NativeWindowHandles handles = ResolveNativeHandles(window_);
    if (!handles.nwh)
        return false;

    RendererConfig cfg{};
    cfg.width = width_;
    cfg.height = height_;
    cfg.appName = "waveOut xplat";
    cfg.nativeWindowHandle = handles.nwh;
    cfg.nativeDisplayHandle = handles.ndt;
    cfg.vsync = vsync_;

    renderer_ = std::make_unique<BgfxRenderer>();
    if (!renderer_->Initialize(cfg))
        return false;

    bool sceneReady = false;
    if (!audioPath_.empty())
        sceneReady = scene_.LoadAudioFile(audioPath_);
    if (!sceneReady)
        sceneReady = scene_.Initialize();
    if (!sceneReady)
        return false;

    ui_ = std::make_unique<ImGuiLayer>();
    if (!ui_->Initialize())
        return false;

    lastFrameStamp_ = std::chrono::steady_clock::now();
    lastTitleStamp_ = lastFrameStamp_;
    return true;
}

void App::Shutdown()
{
    if (ui_)
    {
        ui_->Shutdown();
        ui_.reset();
    }

    if (renderer_)
    {
        renderer_->Shutdown();
        renderer_.reset();
    }

    if (window_)
    {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_Quit();
}

void App::HandleEvent(const SDL_Event& ev, bool& running)
{
    switch (ev.type)
    {
    case SDL_EVENT_QUIT:
        running = false;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
#ifdef SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
#endif
    {
        const int w = ev.window.data1;
        const int h = ev.window.data2;
        width_ = static_cast<std::uint32_t>((std::max)(1, w));
        height_ = static_cast<std::uint32_t>((std::max)(1, h));
        if (renderer_)
            renderer_->Resize(width_, height_);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        if (ev.key.repeat)
            break;
        switch (ev.key.key)
        {
        case SDLK_SPACE:
            scene_.TogglePaused();
            break;
        case SDLK_LEFT:
            scene_.NudgePlayback(-1.0);
            break;
        case SDLK_RIGHT:
            scene_.NudgePlayback(1.0);
            break;
        case SDLK_HOME:
            scene_.SeekToStart();
            break;
        default:
            break;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        const float y = ev.wheel.y;
        if (y > 0.0f)
            scene_.AdjustZoom(1.15);
        else if (y < 0.0f)
            scene_.AdjustZoom(1.0 / 1.15);
        break;
    }
    default:
        break;
    }
}

void App::UpdateWindowTitle()
{
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - lastTitleStamp_).count();
    if (dt < 0.25)
        return;
    lastTitleStamp_ = now;

    std::ostringstream ss;
    ss << "waveOut xplat  |  frame "
       << std::fixed << std::setprecision(2) << frameStats_.lastFrameMs() << " ms"
       << "  avg " << std::fixed << std::setprecision(2) << frameStats_.avgFrameMs() << " ms"
       << "  fps " << std::fixed << std::setprecision(1) << frameStats_.fps()
       << "  zoom " << std::fixed << std::setprecision(1) << scene_.zoomFactor() << "x"
       << "  " << (scene_.paused() ? "paused" : "playing");
    SDL_SetWindowTitle(window_, ss.str().c_str());
}
