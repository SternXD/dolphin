#include "ImGuiMappingWindow.h"
#include <chrono>
#include <format>
#include <functional>
#include <imgui.h>
#include <string>
#include <vector>
#include "Core/HW/GCPad.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/MappingCommon.h"
#include "InputCommon/InputConfig.h"

// These are the includes for all of the Extensions
#include "Core/HW/WiimoteEmu/Extension/Classic.h"
#include "Core/HW/WiimoteEmu/Extension/DrawsomeTablet.h"
#include "Core/HW/WiimoteEmu/Extension/Drums.h"
#include "Core/HW/WiimoteEmu/Extension/Guitar.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/Extension/Shinkansen.h"
#include "Core/HW/WiimoteEmu/Extension/TaTaCon.h"
#include "Core/HW/WiimoteEmu/Extension/Turntable.h"
#include "Core/HW/WiimoteEmu/Extension/UDrawTablet.h"

namespace ImGuiFrontend
{
static std::vector<std::function<void()>> s_pendingBinds;

void ImGuiMappingWindow::Draw(int port, UIState* state)
{
  ImGuiIO& io = ImGui::GetIO();
  if (state->capturingRef)
  {
    io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
  }
  else
  {
    io.ConfigFlags |= (ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  }

  const char* title =
      state->mappingWindowIsWii ? "Wii Remote Mapping" : "GameCube Controller Mapping";
  ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &state->showMappingWindow))
  {
    ImGui::End();
    return;
  }

  InputConfig* config = state->mappingWindowIsWii ? Wiimote::GetConfig() : Pad::GetConfig();
  ControllerEmu::EmulatedController* controller = config->GetController(port);
  if (state->capturingRef)
  {
    ImGui::Text("Press any button/input to bind '%s'...",
                state->capturingRef->GetExpression().c_str());
    if (!state->mappingInputDetector)
    {
      state->mappingInputDetector = std::make_unique<ciface::Core::InputDetector>();
      const auto& def = controller->GetDefaultDevice();
      std::vector<std::string> devices{def.ToString()};
      state->mappingInputDetector->Start(g_controller_interface, devices);
    }
    state->mappingInputDetector->Update(std::chrono::milliseconds(300),
                                        std::chrono::milliseconds(300), std::chrono::seconds(5));
    if (state->mappingInputDetector->IsComplete())
    {
      auto results = state->mappingInputDetector->TakeResults();
      ciface::MappingCommon::RemoveSpuriousTriggerCombinations(&results);
      if (!results.empty())
      {
        auto expr = ciface::MappingCommon::BuildExpression(results, controller->GetDefaultDevice(),
                                                           ciface::MappingCommon::Quote::On);
        auto* ref = state->capturingRef;
        auto* ctrl = controller;
        s_pendingBinds.emplace_back([ref, ctrl, expr]() {
          ref->SetExpression(expr);
          ctrl->UpdateSingleControlReference(g_controller_interface, ref);
        });
        state->capturingRef = nullptr;
        state->mappingInputDetector.reset();
        ImGui::End();
        return;
      }
      state->mappingInputDetector.reset();
    }
    ImGui::End();
    return;
  }

  if (state->capturingRef)
  {
    ImGui::SetWindowFocus();
    io.WantCaptureKeyboard = true;
    io.WantCaptureMouse = true;
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      ImGui::IsKeyPressed(ImGuiKey_Escape))
    state->showMappingWindow = false;

  if (ImGui::BeginTabBar("MappingTabs"))
  {
    if (state->mappingWindowIsWii)
    {
      if (ImGui::BeginTabItem("General and Options"))
      {
        ImGuiStyle& style = ImGui::GetStyle();
        float footer = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        ImGui::BeginChild("GeneralScroll", ImVec2(0, -footer), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        auto findGroup = [&](const std::string& name) {
          for (auto& gp : controller->groups)
            if (gp->ui_name == name)
              return gp.get();
          return (ControllerEmu::ControlGroup*)nullptr;
        };
        auto renderControls = [&](ControllerEmu::ControlGroup* g) {
          if (!g)
            return;
          if (ImGui::CollapsingHeader(g->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            for (auto& cptr : g->controls)
            {
              auto* ctl = cptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              if (expr.empty())
                expr = "(none)";
              std::string lbl = expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(lbl.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        };
        renderControls(findGroup("Buttons"));
        ImGui::Separator();
        renderControls(findGroup("D-Pad"));
        ImGui::Separator();
        renderControls(findGroup("Hotkeys"));
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Extension##WiiMap", ImGuiTreeNodeFlags_DefaultOpen))
        {
          if (ImGui::Checkbox("MotionPlus##WiiMap", &state->wiimote_motionplus))
          {
            if (auto* attachGroup =
                    static_cast<ControllerEmu::Attachments*>(findGroup("Attachments")))
              attachGroup->GetSelectionSetting().SetValue(state->wiimote_motionplus ? 1 : 0);
            config->SaveConfig();
          }
          static const char* ext_names[] = {
              "None",         "Nunchuk",          "Classic Controller", "Drum Kit",   "Guitar Hero",
              "DJ Turntable", "uDraw GameTablet", "Drawsome Tablet",    "Taiko Drum", "Shinkansen"};
          if (ImGui::Combo("Attached Extension##AttachedExt", &state->wiimote_extension, ext_names,
                           IM_ARRAYSIZE(ext_names)))
          {
            if (auto* attachGroup =
                    static_cast<ControllerEmu::Attachments*>(findGroup("Attachments")))
              attachGroup->SetSelectedAttachment(state->wiimote_extension);
            config->SaveConfig();
          }
          ImGui::Separator();
        }
        if (ImGui::CollapsingHeader("Rumble##WiiMap", ImGuiTreeNodeFlags_DefaultOpen))
        {
          if (auto* rumGroup = findGroup("Rumble"))
          {
            for (auto& cptr : rumGroup->controls)
            {
              auto* ctl = cptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              if (expr.empty())
                expr = "(none)";
              std::string lbl = expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(lbl.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Options##WiiMap", ImGuiTreeNodeFlags_DefaultOpen))
        {
          ImGui::Text("Speaker Pan");
          ImGui::SameLine(150);
          if (ImGui::SliderFloat("Speaker Pan##SpeakerPan", &state->wiimote_speaker_pan, -1.0f,
                                 1.0f))
            config->SaveConfig();
          if (ImGui::SliderFloat("Battery##Battery", &state->wiimote_battery, 0.0f, 1.0f))
            config->SaveConfig();
          ImGui::Checkbox("Upright Wii Remote##Upright", &state->wiimote_upright);
          ImGui::SameLine();
          ImGui::Checkbox("Sideways Wii Remote##Sideways", &state->wiimote_sideways);
          if (auto* optGroup = findGroup("Options"))
          {
            for (auto& cptr : optGroup->controls)
            {
              auto* ctl = cptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              if (expr.empty())
                expr = "(none)";
              std::string lbl = expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(lbl.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Motion Simulation"))
      {
        ImGuiStyle& style = ImGui::GetStyle();
        float footer_height = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        ImGui::BeginChild(ImGui::GetID("MappingScrollAreaScroll"), ImVec2(0, -footer_height), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        for (auto& group_ptr : controller->groups)
        {
          auto* group = group_ptr.get();
          if (group->type == ControllerEmu::GroupType::MixedTriggers ||
              group->type == ControllerEmu::GroupType::Tilt ||
              group->type == ControllerEmu::GroupType::Shake)
          {
            if (ImGui::CollapsingHeader(group->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
              for (auto& ctl_ptr : group->controls)
              {
                auto* ctl = ctl_ptr.get();
                std::string expr = ctl->control_ref->GetExpression();
                ImGui::Text("%s", ctl->ui_name.c_str());
                ImGui::SameLine(150);
                if (expr.empty())
                  expr = "(none)";
                std::string btn_label =
                    expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
                if (ImGui::Button(btn_label.c_str(), ImVec2(180, 0)))
                {
                  state->capturingRef = ctl->control_ref.get();
                  state->mappingInputDetector.reset();
                }
                ImGui::SameLine();
                ImGui::Text("%.2f", ctl->GetState());
              }
              ImGui::Separator();
            }
          }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

      if (state->mappingWindowIsWii &&
          state->wiimote_extension == static_cast<int>(WiimoteEmu::ExtensionNumber::NUNCHUK) &&
          ImGui::BeginTabItem("Extension Motion Simulation"))
      {
        auto findGroup = [&](const std::string& name) {
          for (auto& gp : controller->groups)
            if (gp->ui_name == name)
              return gp.get();
          return (ControllerEmu::ControlGroup*)nullptr;
        };
        auto renderControls = [&](ControllerEmu::ControlGroup* g) {
          if (!g)
            return;
          if (ImGui::CollapsingHeader(g->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            for (auto& cptr : g->controls)
            {
              auto* ctl = cptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              if (expr.empty())
                expr = "(none)";
              std::string lbl = expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(lbl.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        };
        // Scrollable child
        ImGuiStyle& styleExt = ImGui::GetStyle();
        float footerExt = ImGui::GetFrameHeightWithSpacing() + styleExt.ItemSpacing.y;
        ImGui::BeginChild("ExtMotionScroll", ImVec2(0, -footerExt), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        renderControls(findGroup("Shake"));
        renderControls(findGroup("Tilt"));
        renderControls(findGroup("Swing"));
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

#ifndef WINRT_XBOX  // Motion Input tab is only available on Platforms with motion controls
      if (ImGui::BeginTabItem("Motion Input"))
      {
        ImGuiStyle& style = ImGui::GetStyle();
        float footer_height = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        ImGui::BeginChild(ImGui::GetID("MappingScrollAreaScroll"), ImVec2(0, -footer_height), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        for (auto& group_ptr : controller->groups)
        {
          auto* group = group_ptr.get();
          if (group->type == ControllerEmu::GroupType::IMUAccelerometer ||
              group->type == ControllerEmu::GroupType::IMUGyroscope)
          {
            if (ImGui::CollapsingHeader(group->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
              for (auto& ctl_ptr : group->controls)
              {
                auto* ctl = ctl_ptr.get();
                std::string expr = ctl->control_ref->GetExpression();
                ImGui::Text("%s", ctl->ui_name.c_str());
                ImGui::SameLine(150);
                if (expr.empty())
                  expr = "(none)";
                std::string btn_label =
                    expr + std::format("##ctl{0}", reinterpret_cast<intptr_t>(ctl));
                if (ImGui::Button(btn_label.c_str(), ImVec2(180, 0)))
                {
                  state->capturingRef = ctl->control_ref.get();
                  state->mappingInputDetector.reset();
                }
                ImGui::SameLine();
                ImGui::Text("%.2f", ctl->GetState());
              }
              ImGui::Separator();
            }
          }
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

#endif
      if (ImGui::BeginTabItem("Extension"))
      {
        ImGuiStyle& style = ImGui::GetStyle();
        float footer_height = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        ImGui::BeginChild(ImGui::GetID("MappingScrollAreaScroll"), ImVec2(0, -footer_height), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        auto renderGroupUI = [&](ControllerEmu::ControlGroup* group) {
          if (!group)
            return;
          if (ImGui::CollapsingHeader(group->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            for (auto& ctl_ptr : group->controls)
            {
              auto* ctl = ctl_ptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              if (expr.empty())
                expr = "(none)";
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              std::string btn = expr + std::format("##ctl{}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(btn.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        };
        switch (state->wiimote_extension)
        {
        case 1:  // Nunchuk
          renderGroupUI(Wiimote::GetNunchukGroup(port, WiimoteEmu::NunchukGroup::Buttons));
          renderGroupUI(Wiimote::GetNunchukGroup(port, WiimoteEmu::NunchukGroup::Stick));
          renderGroupUI(Wiimote::GetNunchukGroup(port, WiimoteEmu::NunchukGroup::Tilt));
          renderGroupUI(Wiimote::GetNunchukGroup(port, WiimoteEmu::NunchukGroup::Shake));
          renderGroupUI(Wiimote::GetNunchukGroup(port, WiimoteEmu::NunchukGroup::IMUAccelerometer));
          break;
        case 2:  // Classic
          renderGroupUI(Wiimote::GetClassicGroup(port, WiimoteEmu::ClassicGroup::Buttons));
          renderGroupUI(Wiimote::GetClassicGroup(port, WiimoteEmu::ClassicGroup::DPad));
          renderGroupUI(Wiimote::GetClassicGroup(port, WiimoteEmu::ClassicGroup::LeftStick));
          renderGroupUI(Wiimote::GetClassicGroup(port, WiimoteEmu::ClassicGroup::RightStick));
          renderGroupUI(Wiimote::GetClassicGroup(port, WiimoteEmu::ClassicGroup::Triggers));
          break;
        case 3:  // Drum Kit
          renderGroupUI(Wiimote::GetDrumsGroup(port, WiimoteEmu::DrumsGroup::Stick));
          renderGroupUI(Wiimote::GetDrumsGroup(port, WiimoteEmu::DrumsGroup::Pads));
          renderGroupUI(Wiimote::GetDrumsGroup(port, WiimoteEmu::DrumsGroup::Buttons));
          break;
        case 4:  // Guitar Hero
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::Stick));
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::Strum));
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::Frets));
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::Buttons));
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::Whammy));
          renderGroupUI(Wiimote::GetGuitarGroup(port, WiimoteEmu::GuitarGroup::SliderBar));
          break;
        case 5:  // DJ Turntable
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::Stick));
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::Buttons));
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::EffectDial));
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::LeftTable));
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::RightTable));
          renderGroupUI(Wiimote::GetTurntableGroup(port, WiimoteEmu::TurntableGroup::Crossfade));
          break;
        case 6:  // uDraw GameTablet
          renderGroupUI(Wiimote::GetUDrawTabletGroup(port, WiimoteEmu::UDrawTabletGroup::Buttons));
          renderGroupUI(Wiimote::GetUDrawTabletGroup(port, WiimoteEmu::UDrawTabletGroup::Stylus));
          renderGroupUI(Wiimote::GetUDrawTabletGroup(port, WiimoteEmu::UDrawTabletGroup::Touch));
          break;
        case 7:  // Drawsome Tablet
          renderGroupUI(
              Wiimote::GetDrawsomeTabletGroup(port, WiimoteEmu::DrawsomeTabletGroup::Stylus));
          renderGroupUI(
              Wiimote::GetDrawsomeTabletGroup(port, WiimoteEmu::DrawsomeTabletGroup::Touch));
          break;
        case 8:  // Taiko Drum
          renderGroupUI(Wiimote::GetTaTaConGroup(port, WiimoteEmu::TaTaConGroup::Center));
          renderGroupUI(Wiimote::GetTaTaConGroup(port, WiimoteEmu::TaTaConGroup::Rim));
          break;
        case 9:  // Shinkansen
          renderGroupUI(Wiimote::GetShinkansenGroup(port, WiimoteEmu::ShinkansenGroup::Levers));
          renderGroupUI(Wiimote::GetShinkansenGroup(port, WiimoteEmu::ShinkansenGroup::Buttons));
          renderGroupUI(Wiimote::GetShinkansenGroup(port, WiimoteEmu::ShinkansenGroup::Light));
          break;
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    else
    {
      if (ImGui::BeginTabItem("GameCube Controller"))
      {
        ImGuiStyle& style = ImGui::GetStyle();
        float footer_height = ImGui::GetFrameHeightWithSpacing() + style.ItemSpacing.y;
        ImGui::BeginChild(ImGui::GetID("MappingScrollAreaScroll"), ImVec2(0, -footer_height), true,
                          ImGuiWindowFlags_AlwaysUseWindowPadding);
        auto findGroup = [&](const std::string& name) {
          for (auto& gp : controller->groups)
            if (gp->ui_name == name)
              return gp.get();
          return (ControllerEmu::ControlGroup*)nullptr;
        };
        auto renderGroupUI = [&](ControllerEmu::ControlGroup* group) {
          if (!group)
            return;
          if (ImGui::CollapsingHeader(group->ui_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
          {
            for (auto& ctl_ptr : group->controls)
            {
              auto* ctl = ctl_ptr.get();
              std::string expr = ctl->control_ref->GetExpression();
              if (expr.empty())
                expr = "(none)";
              ImGui::Text("%s", ctl->ui_name.c_str());
              ImGui::SameLine(150);
              std::string btn = expr + std::format("##ctl{}", reinterpret_cast<intptr_t>(ctl));
              if (ImGui::Button(btn.c_str(), ImVec2(180, 0)))
              {
                state->capturingRef = ctl->control_ref.get();
                state->mappingInputDetector.reset();
              }
              ImGui::SameLine();
              ImGui::Text("%.2f", ctl->GetState());
            }
            ImGui::Separator();
          }
        };
        renderGroupUI(findGroup("Buttons"));
        renderGroupUI(findGroup("Main Stick"));
        renderGroupUI(findGroup("C-Stick"));
        renderGroupUI(findGroup("Triggers"));
        renderGroupUI(findGroup("D-Pad"));
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }

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
  for (auto& fn : s_pendingBinds)
    fn();
  s_pendingBinds.clear();
}

}  // namespace ImGuiFrontend
