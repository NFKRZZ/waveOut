#pragma once

class FrameStats;

class ImGuiLayer
{
public:
    bool Initialize();
    void Shutdown();

    void BeginFrame();
    void DrawOverlay(const FrameStats& stats);
    void EndFrame();
};

