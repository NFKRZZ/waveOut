#include "ui/ImGuiLayer.h"

#include "FrameStats.h"

bool ImGuiLayer::Initialize()
{
    return true;
}

void ImGuiLayer::Shutdown()
{
}

void ImGuiLayer::BeginFrame()
{
}

void ImGuiLayer::DrawOverlay(const FrameStats& /*stats*/)
{
    // Scaffold only. Hook ImGui SDL3 + bgfx backends here when ready.
}

void ImGuiLayer::EndFrame()
{
}

