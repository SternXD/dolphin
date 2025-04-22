#include "ImGuiMappingWindow.h"
#include <InputCommon/ControllerInterface/MappingCommon.h>
#include <imgui.h>
#include <string>
#include "Core/HW/GCPad.h"
#include "Core/HW/Wiimote.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/InputConfig.h"

namespace ImGuiFrontend
{

void ImGuiMappingWindow::Draw(int port, UIState* state)
{
  // Window title based on device
  const char* title =
      state->mappingWindowIsWii ? "Wii Remote Mapping" : "GameCube Controller Mapping";
  ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &state->showMappingWindow))
  {
    ImGui::End();
    return;
  }

  // Close icon in title bar (larger and colored)
  {
    ImGuiStyle& style = ImGui::GetStyle();
    float windowWidth = ImGui::GetWindowWidth();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 btnSize = ImVec2(30, style.FramePadding.y * 2 + ImGui::GetFontSize());
    // Position inside title bar: use FramePadding for Y, extra offset for X
    const float extraOffsetX = style.FramePadding.x + 20.0f;
    ImVec2 buttonPos =
        ImVec2(windowPos.x + windowWidth - btnSize.x - style.FramePadding.x - extraOffsetX,
               windowPos.y + style.FramePadding.y * 2);
    ImGui::SetCursorScreenPos(buttonPos);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(200, 50, 50, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 80, 80, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 50, 50, 255));
    if (ImGui::Button("X##close", btnSize))
      state->showMappingWindow = false;
    ImGui::PopStyleColor(3);
    // Reset cursor to content
    ImGui::SetCursorScreenPos(
        ImVec2(windowPos.x + style.FramePadding.x,
               windowPos.y + style.FramePadding.y + btnSize.y + style.ItemSpacing.y));
  }

  // Close window with Escape
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape))
    state->showMappingWindow = false;

  // Get input config and controller instance
  InputConfig* config = state->mappingWindowIsWii ? Wiimote::GetConfig() : Pad::GetConfig();
  ControllerEmu::EmulatedController* controller = config->GetController(port);

  // Handle input capture for mapping
  if (state->capturingRef)
  {
    ImGui::Text("Press any button/input to bind '%s'...",
                state->capturingRef->GetExpression().c_str());
    // Initialize detector on first frame
    if (!state->mappingInputDetector)
    {
      state->mappingInputDetector = std::make_unique<ciface::Core::InputDetector>();
      // TODO: provide correct DeviceContainer and device qualifier list
      // e.g., state->mappingInputDetector->Start(device_container, {
      // controller->GetDefaultDevice().ToString() });
    }
    // Update detector (pass zero or small timeout)
    // state->mappingInputDetector->Update(std::chrono::milliseconds(10));
    if (state->mappingInputDetector->IsComplete())
    {
      auto results = state->mappingInputDetector->TakeResults();
      // Build expression string
      auto newExpr = ciface::MappingCommon::BuildExpression(results, controller->GetDefaultDevice(),
                                                            ciface::MappingCommon::Quote::On);
      // Apply new binding
      if (auto err = state->capturingRef->SetExpression(newExpr))
      { /* handle parse error if needed */
      }
      // TODO: rebind reference in controller via UpdateSingleControlReference
      state->capturingRef = nullptr;
      state->mappingInputDetector.reset();
    }
    ImGui::Separator();
  }

  // Begin scrollable content region using explicit ImGuiID to avoid empty-label ID collisions
  ImGui::BeginChild(ImGui::GetID("MappingScrollAreaScroll"), ImVec2(0, -80), true,
                    ImGuiWindowFlags_AlwaysUseWindowPadding);

  // Render each control group
  for (auto& group_ptr : controller->groups)
  {
    auto* group = group_ptr.get();
    if (ImGui::CollapsingHeader(group->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
      for (auto& ctl_ptr : group->controls)
      {
        auto* ctl = ctl_ptr.get();
        std::string expr = ctl->control_ref->GetExpression();
        ImGui::Text("%s", ctl->ui_name.c_str());
        ImGui::SameLine(150);
        // Ensure non-empty and unique ID for Button
        if (expr.empty())
          expr = "(none)";
        std::string btn_label = expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
        if (ImGui::Button(btn_label.c_str(), ImVec2(180, 0)))
        {
          // TODO: initiate input capture for ctl->control_ref
        }
        ImGui::SameLine();
        float val = ctl->GetState();
        ImGui::Text("%.2f", val);
      }
      ImGui::Separator();
    }
  }

  // End scrollable region
  ImGui::EndChild();

  // Save or close controls
  if (ImGui::Button("Save", ImVec2(80, 0)))
  {
    config->SaveConfig();
    state->showMappingWindow = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Close", ImVec2(80, 0)))
  {
    state->showMappingWindow = false;
  }

  ImGui::End();
}

}  // namespace ImGuiFrontend
