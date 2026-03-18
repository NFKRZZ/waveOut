#pragma once

#include "FrameStats.h"
#include "scene/WaveScene.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <memory>
#include <string>

class IRenderer;
class ImGuiLayer;

class App
{
public:
    explicit App(std::string audioPath = {});
    ~App();

    int Run();

private:
    bool Initialize();
    void Shutdown();
    void HandleEvent(const SDL_Event& ev, bool& running);
    void UpdateWindowTitle();

    SDL_Window* window_ = nullptr;
    std::unique_ptr<IRenderer> renderer_;
    std::unique_ptr<ImGuiLayer> ui_;

    std::uint32_t width_ = 1600;
    std::uint32_t height_ = 900;
    bool vsync_ = true;
    std::string audioPath_;

    FrameStats frameStats_;
    WaveScene scene_;
    std::chrono::steady_clock::time_point lastFrameStamp_{};
    std::chrono::steady_clock::time_point lastTitleStamp_{};
};
