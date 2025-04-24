// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "ImGuiFrontend.h"

#include "DolphinWinRT/Host.h"
#include "DolphinWinRT/UWPUtils.h"

#include "ImGuiNetplay.h"
#include "ImageLoader.h"
#include "WinRTKeyboard.h"

#include <fmt/format.h>

#include <cctype>
#include <memory>

#include <Windows.h>
#include <imgui.h>

#include "../../../../Externals/tinygltf/tinygltf/stb_image.h"

#ifdef WINRT_XBOX
#include <gamingdeviceinformation.h>
#include <windows.applicationmodel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/windows.gaming.input.h>
#include <winrt/windows.graphics.display.core.h>
#endif

#include <unordered_map>
#include <wil/com.h>

#include "Core/AchievementManager.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/CommonTitles.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core/WiiUtils.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayProto.h"
#include "Core/NetPlayServer.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "Core/TitleDatabase.h"
#include "Core/WiiRoot.h"

#include <rcheevos/include/rc_client_raintegration.h>
#include "rcheevos/include/rc_api_info.h"
#include "rcheevos/include/rc_api_runtime.h"
#include "rcheevos/include/rc_api_user.h"
#include "rcheevos/include/rc_client.h"
#include "rcheevos/include/rc_error.h"
#include "rcheevos/include/rc_hash.h"
#include "rcheevos/include/rc_runtime.h"

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/Image.h"
#include "Common/Timer.h"

#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"
#include "UICommon/UICommon.h"

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/OnScreenUI.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/MixedTriggers.h"
#include "InputCommon/ControllerInterface/DualShockUDPClient/DualShockUDPClient.h"
#include "InputCommon/ControllerInterface/MappingCommon.h"
#include "InputCommon/ControllerInterface/WGInput/WGInput.h"
#include "InputCommon/InputConfig.h"

#include "AudioCommon/AudioCommon.h"
#include "AudioCommon/Enums.h"
#include "AudioCommon/WASAPIStream.h"

#include "ImGuiMappingWindow.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"

#include <thread>

namespace WGI = winrt::Windows::Gaming::Input;
using winrt::Windows::UI::Core::CoreWindow;
using namespace winrt;

// Static handler for config changes (ImGui does not use QObject or signals/slots)
void OnHardcoreChangedStatic()
{
  if (Config::Get(Config::RA_HARDCORE_ENABLED))
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_DEBUGGING, false);
}

namespace ImGuiFrontend
{

constexpr const char* PROFILES_DIR = "Profiles/";
std::vector<std::string> m_wiimote_profiles;
std::string m_selected_wiimote_profile[] = {"", "", "", ""};
std::vector<std::string> m_gc_profiles;
std::string m_selected_gc_profile[] = {"", "", "", ""};
std::vector<std::string> m_paths;
bool m_show_path_warning = false;
float m_frame_scale = 1.0f;
int m_selectedGameIdx;
UIState m_state = UIState();
CarouselCategory m_ccat = CarouselCategory::CAll;
std::map<std::string, FrontendTheme> m_themes;
FrontendTheme* m_selected_theme;

ImGuiFrontend::ImGuiFrontend()
{
  if (!g_video_backend->Initialized())
  {
    WindowSystemInfo wsi;

#ifdef WINRT_XBOX
    // To-Do: Handle other platforms, extract this code so it can be done by the host!

    CoreWindow window = CoreWindow::GetForCurrentThread();
    void* abi = winrt::get_abi(window);

    wsi.type = WindowSystemType::Windows;
    wsi.render_surface = abi;
    wsi.render_width = window.Bounds().Width;
    wsi.render_height = window.Bounds().Height;

    GAMING_DEVICE_MODEL_INFORMATION info = {};
    GetGamingDeviceModelInformation(&info);
    if (info.vendorId == GAMING_DEVICE_VENDOR_ID_MICROSOFT)
    {
      winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation hdi =
          winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
      if (hdi)
      {
        constexpr float frontend_modifier = 1.8f;
        uint32_t width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();

        m_frame_scale = ((float)width / 1920.0f) * frontend_modifier;
        wsi.render_width = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
        wsi.render_height = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
        // Our UI is based on 1080p, and we're adding a modifier to zoom in by 80%
        wsi.render_surface_scale = ((float)wsi.render_width / 1920.0f) * 1.8f;
      }
    }
#endif

    // Manually reactivate the video backend in case a GameINI overrides the video backend setting.
    VideoBackendBase::PopulateBackendInfo(wsi);
    // Issue any API calls which must occur on the main thread for the graphics backend.
    WindowSystemInfo prepared_wsi(wsi);
    g_video_backend->PrepareWindow(prepared_wsi);

    VideoBackendBase::PopulateBackendInfo(wsi);
    if (!g_video_backend->Initialize(wsi))
    {
      PanicAlertFmt("Failed to initialize video backend!");
      return;
    }
  }

  ImGuiIO& io = ImGui::GetIO();
  ciface::WGInput::Init();
  g_controller_interface.RefreshDevices();

  io.AddKeyEvent(ImGuiKey_Backspace, true);  // When key is pressed

  io.AddKeyEvent(ImGuiKey_Backspace, false);  // When key is released

  std::string profiles_path = File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                              Wiimote::GetConfig()->GetProfileDirectoryName();
  for (const auto& filename : Common::DoFileSearch({profiles_path}, {".ini"}))
  {
    std::string basename;
    SplitPath(filename, nullptr, &basename, nullptr);
    if (!basename.empty())
      m_wiimote_profiles.emplace_back(basename);
  }

  m_wiimote_profiles.emplace_back("None");
  m_wiimote_profiles.emplace_back("Wiimote + Nunchuk");
  m_wiimote_profiles.emplace_back("Classic Controller");
  m_wiimote_profiles.emplace_back("Sideways Wiimote");

  profiles_path =
      File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR + Pad::GetConfig()->GetProfileDirectoryName();
  for (const auto& filename : Common::DoFileSearch({profiles_path}, {".ini"}))
  {
    std::string basename;
    SplitPath(filename, nullptr, &basename, nullptr);
    if (!basename.empty())
      m_gc_profiles.emplace_back(basename);
  }

  m_gc_profiles.emplace_back("None");
  m_gc_profiles.emplace_back("Default");

  Wiimote::LoadConfig();
  Pad::LoadConfig();

  PopulateControls();
  LoadGameList();
  LoadThemes();

#ifdef USE_RETRO_ACHIEVEMENTS
  // ImGui doesn't have a window handle, so pass nullptr
  AchievementManager::GetInstance().Init(nullptr);
  // Disable debug mode if hardcore is active
  if (AchievementManager::GetInstance().IsHardcoreModeActive())
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_DEBUGGING, false);
  // Listen for config changes to RA_ENABLED or RA_HARDCORE_ENABLED
  Config::AddConfigChangedCallback([]() { OnHardcoreChangedStatic(); });
  // If hardcore is enabled at startup, ensure we react
  if (Config::Get(Config::RA_HARDCORE_ENABLED))
    OnHardcoreChangedStatic();
#endif  // USE_RETRO_ACHIEVEMENTS

  m_ccat = (CarouselCategory)Config::Get(Config::FRONTEND_LAST_CATEGORY);
  m_last_category = m_ccat;
  FilterGamesForCategory();

  m_selectedGameIdx = Config::Get(Config::FRONTEND_LAST_GAME);

  if (m_selectedGameIdx >= m_displayed_games.size() || m_selectedGameIdx < 0)
    m_selectedGameIdx = 0;
}

void ImGuiFrontend::PopulateControls()
{
  g_controller_interface.RefreshDevices();

  if (!g_controller_interface.HasDefaultDevice())
    return;

  for (auto& device_str : g_controller_interface.GetAllDeviceStrings())
  {
    ciface::Core::DeviceQualifier dq;
    dq.FromString(device_str);

    auto device = g_controller_interface.FindDevice(dq);
    if (device)
    {
      m_controllers.push_back(std::move(device));
    }
  }
}

void ImGuiFrontend::RefreshControls(bool updateGameSelection)
{
  if (m_controllers.empty())
    PopulateControls();

  bool input_handled = false;
  for (auto device : m_controllers)
  {
    if (input_handled || device == nullptr || !device->IsValid())
      return;

    device->UpdateInput();

    // wrap around if exceeding the max games or going below
    if (updateGameSelection)
    {
      auto now = std::chrono::high_resolution_clock::now();
      long timeSinceLastInput =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - m_scroll_last).count();
      if (TryInput("Pad W", device))
      {
        if (!m_direction_pressed)
        {
          m_selectedGameIdx = m_selectedGameIdx <= 0 ?
                                  static_cast<int>(m_displayed_games.size()) - 1 :
                                  m_selectedGameIdx - 1;
          m_direction_pressed = true;
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Pad E", device))
      {
        if (!m_direction_pressed)
        {
          m_selectedGameIdx = m_selectedGameIdx >= static_cast<int>(m_displayed_games.size()) - 1 ?
                                  0 :
                                  m_selectedGameIdx + 1;
          m_direction_pressed = true;
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Left X-", device))
      {
        if (timeSinceLastInput > 200L)
        {
          m_selectedGameIdx = m_selectedGameIdx <= 0 ?
                                  static_cast<int>(m_displayed_games.size()) - 1 :
                                  m_selectedGameIdx - 1;
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Left X+", device))
      {
        if (timeSinceLastInput > 200L)
        {
          m_selectedGameIdx = m_selectedGameIdx >= static_cast<int>(m_displayed_games.size()) - 1 ?
                                  0 :
                                  m_selectedGameIdx + 1;
          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Bumper L", device))
      {
        if (timeSinceLastInput > 250L)
        {
          if (!m_state.showListView && !m_state.showSettingsWindow && g_netplay_dialog == nullptr)
          {
            int idx = -1 + (int)m_ccat;
            if (idx < 1)
              idx = -1 + (int)CCount;

            m_ccat = (CarouselCategory)idx;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (TryInput("Bumper R", device))
      {
        if (timeSinceLastInput > 250L)
        {
          if (!m_state.showListView && !m_state.showSettingsWindow && g_netplay_dialog == nullptr)
          {
            int idx = 1 + (int)m_ccat;
            if (idx >= CCount)
              idx = 1;

            m_ccat = (CarouselCategory)idx;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (m_displayed_games.size() > 10 && TryInput("Trigger L", device))
      {
        if (timeSinceLastInput > 500L)
        {
          int i = m_selectedGameIdx - 10;
          if (i < 0)
          {
            // wrap around, total games + -index
            m_selectedGameIdx = static_cast<int>(m_displayed_games.size()) + i;
          }
          else
          {
            m_selectedGameIdx = i;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else if (m_displayed_games.size() > 10 && TryInput("Trigger R", device))
      {
        if (timeSinceLastInput > 500L)
        {
          int i = m_selectedGameIdx + 10;
          if (i >= m_displayed_games.size())
          {
            // wrap around, i - total games
            m_selectedGameIdx = i - static_cast<int>(m_displayed_games.size());
          }
          else
          {
            m_selectedGameIdx = i;
          }

          m_scroll_last = std::chrono::high_resolution_clock::now();
          break;
        }

        input_handled = true;
      }
      else
      {
        m_direction_pressed = false;
      }
    }
  }
}

FrontendResult ImGuiFrontend::RunUntilSelection()
{
  return RunMainLoop();
}

FrontendResult ImGuiFrontend::RunMainLoop()
{
  FrontendResult selection;

  if (g_netplay_dialog)
  {
    // Reset for if we're exiting a game into netplay again
    g_netplay_dialog->Reset();
  }

  // Main loop
  bool done = false;
  while (!done)
  {
    CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(
        winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);

    if (!m_state.controlsDisabled)
      RefreshControls(!m_state.showSettingsWindow);

    for (auto device : m_controllers)
    {
      if (device && device->IsValid() && !m_state.controlsDisabled)
      {
        if (TryInput("Menu", device))
        {
          if (!m_state.menuPressed && !m_state.showListView && !g_netplay_dialog)
          {
            m_state.showSettingsWindow = !m_state.showSettingsWindow;
            m_state.menuPressed = true;
            LoadGameList();
          }

          break;
        }
        else if (TryInput("Button X", device))
        {
          if (!m_state.menuPressed && !m_state.showSettingsWindow && !g_netplay_dialog)
          {
            m_state.showListView = !m_state.showListView;
            m_state.menuPressed = true;
          }

          break;
        }
        else if (TryInput("View", device))
        {
          if (!m_state.menuPressed && !m_state.showListView && !m_state.showSettingsWindow)
          {
            if (g_netplay_dialog)
            {
              g_netplay_client = nullptr;
              g_netplay_server = nullptr;
              g_netplay_dialog = nullptr;
            }
            else
            {
              g_netplay_dialog = std::make_shared<ImGuiNetPlay>(this, m_games, m_frame_scale);
            }

            m_state.menuPressed = true;
          }

          break;
        }
        else
        {
          m_state.menuPressed = false;
        }
      }
    }

    ImGuiIO& io = ImGui::GetIO();

    {
      std::unique_lock lk(UWP::g_buffer_mutex);

      for (uint32_t c : UWP::g_char_buffer)
      {
        io.AddInputCharacter(c);

        if (c == '\b')
        {
          io.AddKeyEvent(ImGuiKey_Backspace, true);
          io.AddKeyEvent(ImGuiKey_Backspace, false);
        }
      }

      UWP::g_char_buffer.clear();
    }

    // -- Draw Background first
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::Begin("Background", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav))
    {
      ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(m_state.currentBG).get(),
                   ImGui::GetIO().DisplaySize);

      if (m_state.showListView)
      {
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(BG_List_UI).get(),
                     ImGui::GetIO().DisplaySize);
      }
      else if (!m_state.showSettingsWindow && g_netplay_dialog == nullptr)
      {
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Image((ImTextureID)(intptr_t)m_selected_theme->GetBackground(BG_Carousel_UI).get(),
                     ImGui::GetIO().DisplaySize);
      }

      ImGui::End();
    }
    ImGui::PopStyleVar(3);
    // -- Background

    if (g_netplay_dialog != nullptr)
    {
      m_state.currentBG = BG_Netplay;

      auto result = g_netplay_dialog->Draw();
      if (result == BootGame)
      {
        selection.netplay = true;
        break;
      }
      else if (result == ExitNetplay)
      {
        g_netplay_dialog = nullptr;
      }
    }
    else if (m_state.showSettingsWindow)
    {
      m_state.currentBG = BG_Menu;

      ImGui::SetNextWindowSize(ImVec2(700 * m_frame_scale, 425 * m_frame_scale));
      ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (700 / 2) * m_frame_scale,
                                     ImGui::GetIO().DisplaySize.y / 2 - (425 / 2) * m_frame_scale));
      if (ImGui::Begin("Settings", nullptr,
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
      {
        DrawSettingsMenu(&m_state, m_frame_scale);
        ImGui::End();
      }
    }
    else if (m_state.showListView)
    {
      m_state.currentBG = BG_List;

      selection = CreateListPage();
      if (selection.game_result != nullptr || selection.netplay)
      {
        break;
      }
    }
    else
    {
      m_state.currentBG = static_cast<ThemeBG>(m_ccat);

      selection = CreateMainPage();
      if (selection.game_result != nullptr)
      {
        Config::SetBase(Config::FRONTEND_LAST_GAME, (int)m_selectedGameIdx);
        Config::SetBase(Config::FRONTEND_LAST_CATEGORY, (int)m_ccat);
        Config::Save();

        break;
      }
      else if (selection.netplay)
      {
        break;
      }
    }

    // Draw achievements window if enabled and game is loaded
    DrawAchievementsWindow(&m_state);

    g_presenter->Present();

    if (m_state.pending_boot_params)
      return FrontendResult(std::move(m_state.pending_boot_params));
  }

  ImGui::SetNextWindowSize(ImVec2(100 * m_frame_scale, 50 * m_frame_scale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (100 / 2) * m_frame_scale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (50 / 2) * m_frame_scale));
  if (ImGui::Begin("Loading..", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Loading..");
    ImGui::End();
  }

  g_presenter->Present();

  return selection;
}

void CreateGeneralTab(UIState* state)
{
  bool dualCore = Config::Get(Config::MAIN_CPU_THREAD);
  if (ImGui::Checkbox("Dual Core", &dualCore))
  {
    Config::SetBaseOrCurrent(Config::MAIN_CPU_THREAD, dualCore);
    Config::Save();
  }

  bool cheats = Config::Get(Config::MAIN_ENABLE_CHEATS);
  if (ImGui::Checkbox("Enable Cheats", &cheats))
  {
    Config::SetBaseOrCurrent(Config::MAIN_ENABLE_CHEATS, cheats);
    Config::Save();
  }

  bool mismatchedRegion = Config::Get(Config::MAIN_OVERRIDE_REGION_SETTINGS);
  if (ImGui::Checkbox("Allow Mismatched Region Settings", &mismatchedRegion))
  {
    Config::SetBaseOrCurrent(Config::MAIN_OVERRIDE_REGION_SETTINGS, mismatchedRegion);
    Config::Save();
  }

  bool changeDiscs = Config::Get(Config::MAIN_AUTO_DISC_CHANGE);
  if (ImGui::Checkbox("Change Discs Automatically", &changeDiscs))
  {
    Config::SetBaseOrCurrent(Config::MAIN_AUTO_DISC_CHANGE, changeDiscs);
    Config::Save();
  }

  // Create a list of display strings (e.g., 0.1x to 2.0x in 0.1 increments)
  const char* speed_limit_items[] = {"0.1x", "0.2x", "0.3x", "0.4x", "0.5x", "0.6x", "0.7x",
                                     "0.8x", "0.9x", "1.0x", "1.1x", "1.2x", "1.3x", "1.4x",
                                     "1.5x", "1.6x", "1.7x", "1.8x", "1.9x", "2.0x"};

  static int current_speed_index = 10;

  if (ImGui::Combo("Speed Limit", &current_speed_index, speed_limit_items,
                   IM_ARRAYSIZE(speed_limit_items)))
  {
    float new_speed = current_speed_index * 0.1f;
    Config::SetBaseOrCurrent(Config::MAIN_EMULATION_SPEED, new_speed);
    Config::Save();
  }

  const auto fallback = Config::Get(Config::MAIN_FALLBACK_REGION);
  if (ImGui::TreeNode("Fallback Region"))
  {
    if (ImGui::RadioButton("NTSC JP", fallback == DiscIO::Region::NTSC_J))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_J);
      Config::Save();
    }

    if (ImGui::RadioButton("NTSC NA", fallback == DiscIO::Region::NTSC_U))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_U);
      Config::Save();
    }

    if (ImGui::RadioButton("PAL", fallback == DiscIO::Region::PAL))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::PAL);
      Config::Save();
    }

    if (ImGui::RadioButton("Unknown", fallback == DiscIO::Region::Unknown))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::Unknown);
      Config::Save();
    }

    if (ImGui::RadioButton("NTSC Korea", fallback == DiscIO::Region::NTSC_K))
    {
      Config::SetBaseOrCurrent(Config::MAIN_FALLBACK_REGION, DiscIO::Region::NTSC_K);
      Config::Save();
    }

    ImGui::TreePop();
  }
}

void CreateInterfaceTab(UIState* state)
{
  bool showOSD = Config::Get(Config::MAIN_OSD_MESSAGES);
  if (ImGui::Checkbox("Show On-Screen Messages", &showOSD))
  {
    Config::SetBaseOrCurrent(Config::MAIN_OSD_MESSAGES, showOSD);
    Config::Save();
  }

  ImGui::Spacing();

  if (ImGui::BeginCombo("Theme", m_selected_theme->GetName().c_str()))
  {
    for (auto& theme : m_themes)
    {
      if (ImGui::Selectable(theme.first.c_str(),
                            m_selected_theme->GetName() == theme.second.GetName()))
      {
        m_selected_theme = &theme.second;
        Config::SetBaseOrCurrent(Config::FRONTEND_SELECTED_THEME, theme.first);
      }
    }

    ImGui::EndCombo();
  }
}

void CreateGraphicsTab(UIState* state)
{
  if (ImGui::TreeNode("General"))
  {
    // Add backend selection
    std::vector<std::string> backend_names;
    for (auto& backend : VideoBackendBase::GetAvailableBackends())
    {
      backend_names.push_back(backend->GetName());
    }

    std::string current_backend = Config::Get(Config::MAIN_GFX_BACKEND);
    int current_backend_idx = 0;
    for (size_t i = 0; i < backend_names.size(); ++i)
    {
      if (backend_names[i] == current_backend)
      {
        current_backend_idx = static_cast<int>(i);
        break;
      }
    }

    if (ImGui::Combo(
            "Backend", &current_backend_idx,
            [](void* data, int idx, const char** out_text) {
              auto* backends = static_cast<std::vector<std::string>*>(data);
              *out_text = backends->at(idx).c_str();
              return true;
            },
            &backend_names, static_cast<int>(backend_names.size())))
    {
      Config::SetBaseOrCurrent(Config::MAIN_GFX_BACKEND, backend_names[current_backend_idx]);
      Config::Save();
    }

    /* For now we don't need it but you never know so I'll just keep it but commented out for now.
    if (!g_backend_info.Adapters.empty())
    {
      int current_adapter = Config::Get(Config::GFX_ADAPTER);
      if (ImGui::Combo("Adapter", &current_adapter, [](void* data, int idx, const char** out_text) {
        auto* adapters = static_cast<std::vector<std::string>*>(data);
        *out_text = adapters->at(idx).c_str();
        return true;
      }, &g_backend_info.Adapters, static_cast<int>(g_backend_info.Adapters.size())))
      {
        Config::SetBaseOrCurrent(Config::GFX_ADAPTER, current_adapter);
        Config::Save();
      }
    }
    */

    bool vSync = Config::Get(Config::GFX_VSYNC);
    if (ImGui::Checkbox("V-Sync", &vSync))
    {
      Config::SetBaseOrCurrent(Config::GFX_VSYNC, vSync);
      Config::Save();
    }

    const char* aspect_items[] = {"Auto", "Force 16:9", "Force 4:3", "Stretch"};
    int aspect_idx = 0;
    auto aspect = Config::Get(Config::GFX_ASPECT_RATIO);
    switch (aspect)
    {
    case AspectMode::Auto:
      aspect_idx = 0;
      break;
    case AspectMode::ForceWide:
      aspect_idx = 1;
      break;
    case AspectMode::ForceStandard:
      aspect_idx = 2;
      break;
    case AspectMode::Stretch:
      aspect_idx = 3;
      break;
    }

    if (ImGui::Combo("Aspect Ratio", &aspect_idx, aspect_items, IM_ARRAYSIZE(aspect_items)))
    {
      switch (aspect_idx)
      {
      case 0:
        Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::Auto);
        break;
      case 1:
        Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::ForceWide);
        break;
      case 2:
        Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::ForceStandard);
        break;
      case 3:
        Config::SetBaseOrCurrent(Config::GFX_ASPECT_RATIO, AspectMode::Stretch);
        break;
      }
      Config::Save();
    }

    const char* ir_items[] = {"Auto (Multiple of 640x528)",      "Native (640x528)",
                              "2x Native (1280x1056) for 720p",  "3x Native (1920x1584) for 1080p",
                              "4x Native (2560x2112) for 1440p", "5x Native (3200x2640)",
                              "6x Native (3840x3168) for 4K",    "7x Native (4480x3696)",
                              "8x Native (5120x4224) for 5K"};

    int ir_idx = Config::Get(Config::GFX_EFB_SCALE);
    if (ImGui::Combo("Internal Resolution", &ir_idx, ir_items, IM_ARRAYSIZE(ir_items)))
    {
      Config::SetBase(Config::GFX_EFB_SCALE, ir_idx);
      Config::Save();
    }

    const char* shader_items[] = {"Synchronous", "Hybrid Ubershaders", "Exclusive Ubershaders",
                                  "Skip Drawing"};
    int shader_idx = 0;
    auto shader = Config::Get(Config::GFX_SHADER_COMPILATION_MODE);
    switch (shader)
    {
    case ShaderCompilationMode::Synchronous:
      shader_idx = 0;
      break;
    case ShaderCompilationMode::AsynchronousUberShaders:
      shader_idx = 1;
      break;
    case ShaderCompilationMode::SynchronousUberShaders:
      shader_idx = 2;
      break;
    case ShaderCompilationMode::AsynchronousSkipRendering:
      shader_idx = 3;
      break;
    }

    if (ImGui::Combo("Shader Compilation Mode", &shader_idx, shader_items,
                     IM_ARRAYSIZE(shader_items)))
    {
      switch (shader_idx)
      {
      case 0:
        Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE,
                                 ShaderCompilationMode::Synchronous);
        break;
      case 1:
        Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE,
                                 ShaderCompilationMode::AsynchronousUberShaders);
        break;
      case 2:
        Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE,
                                 ShaderCompilationMode::SynchronousUberShaders);
        break;
      case 3:
        Config::SetBaseOrCurrent(Config::GFX_SHADER_COMPILATION_MODE,
                                 ShaderCompilationMode::AsynchronousSkipRendering);
        break;
      }
      Config::Save();
    }

    bool waitForCompile = Config::Get(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING);
    if (ImGui::Checkbox("Compile Shaders Before Starting", &waitForCompile))
    {
      Config::SetBaseOrCurrent(Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING, waitForCompile);
      Config::Save();
    }

    bool shaderCache = Config::Get(Config::GFX_SHADER_CACHE);
    if (ImGui::Checkbox("Shader Cache", &shaderCache))
    {
      Config::SetBaseOrCurrent(Config::GFX_SHADER_CACHE, shaderCache);
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Enhancements"))
  {
    bool perPixelLighting = Config::Get(Config::GFX_ENABLE_PIXEL_LIGHTING);
    if (ImGui::Checkbox("Per-Pixel Lighting", &perPixelLighting))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENABLE_PIXEL_LIGHTING, perPixelLighting);
      Config::Save();
    }

    bool disableFog = Config::Get(Config::GFX_DISABLE_FOG);
    if (ImGui::Checkbox("Disable Fog", &disableFog))
    {
      Config::SetBaseOrCurrent(Config::GFX_DISABLE_FOG, disableFog);
      Config::Save();
    }

    bool disableCopyFilter = Config::Get(Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
    if (ImGui::Checkbox("Disable Copy Filter", &disableCopyFilter))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_DISABLE_COPY_FILTER, disableCopyFilter);
      Config::Save();
    }

    bool forceTrueColor = Config::Get(Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
    if (ImGui::Checkbox("Force True Color", &forceTrueColor))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_FORCE_TRUE_COLOR, forceTrueColor);
      Config::Save();
    }

    bool arbitraryMipmapDetection = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION);
    if (ImGui::Checkbox("Arbitrary Mipmap Detection", &arbitraryMipmapDetection))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION,
                               arbitraryMipmapDetection);
      Config::Save();
    }

    if (arbitraryMipmapDetection)
    {
      float threshold = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD);
      if (ImGui::SliderFloat("Mipmap Detection Threshold", &threshold, 1.0f, 50.0f, "%.1f"))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD,
                                 threshold);
        Config::Save();
      }
      ImGui::TextWrapped("Adjusts the threshold for detecting arbitrary mipmaps. Higher values "
                         "increase detection sensitivity but may cause false positives.");
    }

#ifndef WINRT_XBOX  // I don't have a hdr impl yet for UWP.
    bool hdrOutput = Config::Get(Config::GFX_ENHANCE_HDR_OUTPUT);
    if (ImGui::Checkbox("HDR Output", &hdrOutput))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_HDR_OUTPUT, hdrOutput);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Enables HDR output when supported by the display. Requires a compatible GPU and display.");
#endif
    const char* anisolevel_items[] = {"1x", "2x", "4x", "8x", "16x"};
    auto aniso = Config::Get(Config::GFX_ENHANCE_MAX_ANISOTROPY);
    int aniso_idx = static_cast<int>(aniso);
    if (ImGui::Combo("Anisotropic Filtering", &aniso_idx, anisolevel_items,
                     IM_ARRAYSIZE(anisolevel_items)))
    {
      Config::SetBaseOrCurrent(Config::GFX_ENHANCE_MAX_ANISOTROPY,
                               static_cast<AnisotropicFilteringMode>(aniso_idx));
      Config::Save();
    }

    bool ssaa = Config::Get(Config::GFX_SSAA);
    if (ImGui::RadioButton("MSAA", !ssaa))
    {
      Config::SetBaseOrCurrent(Config::GFX_SSAA, false);
      Config::Save();
    }

    if (ImGui::RadioButton("SSAA", ssaa))
    {
      Config::SetBaseOrCurrent(Config::GFX_SSAA, true);
      Config::Save();
    }

    const char* aalevel_items[] = {"None", "2x", "4x", "8x"};
    auto msaa = Config::Get(Config::GFX_MSAA);
    int msaa_idx = msaa;
    if (ImGui::Combo("MSAA Level", &msaa_idx, aalevel_items, IM_ARRAYSIZE(aalevel_items)))
    {
      Config::SetBaseOrCurrent(Config::GFX_MSAA, msaa_idx);
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Hacks"))
  {
    if (ImGui::TreeNode("Embedded Frame Buffer (EFB)"))
    {
      bool skipEfbCpu = Config::Get(Config::GFX_HACK_EFB_ACCESS_ENABLE);
      if (ImGui::Checkbox("Enable EFB Access from CPU", &skipEfbCpu))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_ACCESS_ENABLE, skipEfbCpu);
        Config::Save();
      }

      bool ignoreFormatChanges = Config::Get(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES);
      if (ImGui::Checkbox("Emulate Format Changes", &ignoreFormatChanges))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES, ignoreFormatChanges);
        Config::Save();
      }

      bool storeEfbCopies = Config::Get(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
      if (ImGui::Checkbox("Store EFB Copies to Texture Only", &storeEfbCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM, storeEfbCopies);
        Config::Save();
      }

      bool deferEfbCopies = Config::Get(Config::GFX_HACK_DEFER_EFB_COPIES);
      if (ImGui::Checkbox("Defer EFB Copies to RAM", &deferEfbCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_DEFER_EFB_COPIES, deferEfbCopies);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Texture Cache"))
    {
      int accuracy = Config::Get(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES);

      int slider_index = 0;
      if (accuracy == 512)
        slider_index = 1;
      else if (accuracy == 128)
        slider_index = 2;

      const char* accuracy_labels[] = {"Safe (0)", "Normal (512)", "Fast (128)"};
      if (ImGui::Combo("Texture Cache Accuracy", &slider_index, accuracy_labels, 3))
      {
        int new_accuracy = 0;
        if (slider_index == 1)
          new_accuracy = 512;
        else if (slider_index == 2)
          new_accuracy = 128;

        Config::SetBaseOrCurrent(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES, new_accuracy);
        Config::Save();
      }

      bool gpuTextureDecoding = Config::Get(Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
      if (ImGui::Checkbox("GPU Texture Decoding", &gpuTextureDecoding))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_GPU_TEXTURE_DECODING, gpuTextureDecoding);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("External Frame Buffer (XFB)"))
    {
      bool storeXfbCopies = Config::Get(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
      if (ImGui::Checkbox("Store XFB Copies to Texture Only", &storeXfbCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM, storeXfbCopies);
        Config::Save();
      }

      bool immediateXfb = Config::Get(Config::GFX_HACK_IMMEDIATE_XFB);
      if (ImGui::Checkbox("Immediately Present XFB", &immediateXfb))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_IMMEDIATE_XFB, immediateXfb);
        Config::Save();
      }

      bool skipDuplicateXfbs = Config::Get(Config::GFX_HACK_SKIP_DUPLICATE_XFBS);
      if (ImGui::Checkbox("Skip Presenting Duplicate Frames", &skipDuplicateXfbs))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_SKIP_DUPLICATE_XFBS, skipDuplicateXfbs);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Other"))
    {
      bool fastDepthCalculation = Config::Get(Config::GFX_FAST_DEPTH_CALC);
      if (ImGui::Checkbox("Fast Depth Calculation", &fastDepthCalculation))
      {
        Config::SetBaseOrCurrent(Config::GFX_FAST_DEPTH_CALC, fastDepthCalculation);
        Config::Save();
      }

      bool disableBoundingBox = Config::Get(Config::GFX_HACK_BBOX_ENABLE);
      if (ImGui::Checkbox("Enable Bounding Box", &disableBoundingBox))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_BBOX_ENABLE, disableBoundingBox);
        Config::Save();
      }

      bool vertexRounding = Config::Get(Config::GFX_HACK_VERTEX_ROUNDING);
      if (ImGui::Checkbox("Vertex Rounding", &vertexRounding))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_VERTEX_ROUNDING, vertexRounding);
        Config::Save();
      }

      bool saveTextureCacheState = Config::Get(Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE);
      if (ImGui::Checkbox("Save Texture Cache to State", &saveTextureCacheState))
      {
        Config::SetBaseOrCurrent(Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE, saveTextureCacheState);
        Config::Save();
      }

      bool viSkip = Config::Get(Config::GFX_HACK_VI_SKIP);
      if (ImGui::Checkbox("VBI Skip", &viSkip))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_VI_SKIP, viSkip);
        Config::Save();
      }

      ImGui::TreePop();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Advanced"))
  {
    if (ImGui::TreeNode("Performance Statistics"))
    {
      bool showFps = Config::Get(Config::GFX_SHOW_FPS);
      if (ImGui::Checkbox("Show FPS", &showFps))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_FPS, showFps);
        Config::Save();
      }

      bool showVps = Config::Get(Config::GFX_SHOW_VPS);
      if (ImGui::Checkbox("Show VPS", &showVps))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_VPS, showVps);
        Config::Save();
      }

      bool showSpeed = Config::Get(Config::GFX_SHOW_SPEED);
      if (ImGui::Checkbox("Show Speed", &showSpeed))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_SPEED, showSpeed);
        Config::Save();
      }

      bool showSpeedColors = Config::Get(Config::GFX_SHOW_SPEED_COLORS);
      if (ImGui::Checkbox("Show Speed Colors", &showSpeedColors))
      {
        Config::SetBaseOrCurrent(Config::GFX_SHOW_SPEED_COLORS, showSpeedColors);
        Config::Save();
      }

      bool overlayStats = Config::Get(Config::GFX_OVERLAY_STATS);
      if (ImGui::Checkbox("Show Rendering Statistics", &overlayStats))
      {
        Config::SetBaseOrCurrent(Config::GFX_OVERLAY_STATS, overlayStats);
        Config::Save();
      }

      bool logRenderTime = Config::Get(Config::GFX_LOG_RENDER_TIME_TO_FILE);
      if (ImGui::Checkbox("Log Render Time to File", &logRenderTime))
      {
        Config::SetBaseOrCurrent(Config::GFX_LOG_RENDER_TIME_TO_FILE, logRenderTime);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Debugging"))
    {
      bool enableWireframe = Config::Get(Config::GFX_ENABLE_WIREFRAME);
      if (ImGui::Checkbox("Enable Wireframe", &enableWireframe))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_WIREFRAME, enableWireframe);
        Config::Save();
      }

      bool showProjStats = Config::Get(Config::GFX_OVERLAY_PROJ_STATS);
      if (ImGui::Checkbox("Show Projection Statistics", &showProjStats))
      {
        Config::SetBaseOrCurrent(Config::GFX_OVERLAY_PROJ_STATS, showProjStats);
        Config::Save();
      }

      bool enableFormatOverlay = Config::Get(Config::GFX_TEXFMT_OVERLAY_ENABLE);
      if (ImGui::Checkbox("Texture Format Overlay", &enableFormatOverlay))
      {
        Config::SetBaseOrCurrent(Config::GFX_TEXFMT_OVERLAY_ENABLE, enableFormatOverlay);
        Config::Save();
      }

      bool enableApiValidation = Config::Get(Config::GFX_ENABLE_VALIDATION_LAYER);
      if (ImGui::Checkbox("Enable API Validation Layers", &enableApiValidation))
      {
        Config::SetBaseOrCurrent(Config::GFX_ENABLE_VALIDATION_LAYER, enableApiValidation);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Utility"))
    {
      bool loadCustomTextures = Config::Get(Config::GFX_HIRES_TEXTURES);
      if (ImGui::Checkbox("Load Custom Textures", &loadCustomTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_HIRES_TEXTURES, loadCustomTextures);
        Config::Save();
      }

      bool prefetchCustomTextures = Config::Get(Config::GFX_CACHE_HIRES_TEXTURES);
      if (ImGui::Checkbox("Prefetch Custom Textures", &prefetchCustomTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_CACHE_HIRES_TEXTURES, prefetchCustomTextures);
        Config::Save();
      }

      bool disableVramCopies = Config::Get(Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
      if (ImGui::Checkbox("Disable EFB VRAM Copies", &disableVramCopies))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_DISABLE_COPY_TO_VRAM, disableVramCopies);
        Config::Save();
      }

      bool enableGraphicsMods = Config::Get(Config::GFX_MODS_ENABLE);
      if (ImGui::Checkbox("Enable Graphics Mods", &enableGraphicsMods))
      {
        Config::SetBaseOrCurrent(Config::GFX_MODS_ENABLE, enableGraphicsMods);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Texture Dumping"))
    {
      bool dumpTextures = Config::Get(Config::GFX_DUMP_TEXTURES);
      if (ImGui::Checkbox("Enable", &dumpTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_TEXTURES, dumpTextures);
        Config::Save();
      }

      bool dumpBaseTextures = Config::Get(Config::GFX_DUMP_BASE_TEXTURES);
      if (ImGui::Checkbox("Dump Base Textures", &dumpBaseTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_BASE_TEXTURES, dumpBaseTextures);
        Config::Save();
      }

      bool dumpMipTextures = Config::Get(Config::GFX_DUMP_MIP_TEXTURES);
      if (ImGui::Checkbox("Dump Mip Maps", &dumpMipTextures))
      {
        Config::SetBaseOrCurrent(Config::GFX_DUMP_MIP_TEXTURES, dumpMipTextures);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Frame Dumping"))
    {
      const char* resolution_items[] = {"Window Resolution",
                                        "Aspect Ratio Corrected Internal Resolution",
                                        "Raw Internal Resolution"};
      auto resolution_type = Config::Get(Config::GFX_FRAME_DUMPS_RESOLUTION_TYPE);
      int resolution_idx = static_cast<int>(resolution_type);
      if (ImGui::Combo("Resolution Type", &resolution_idx, resolution_items,
                       IM_ARRAYSIZE(resolution_items)))
      {
        Config::SetBaseOrCurrent(Config::GFX_FRAME_DUMPS_RESOLUTION_TYPE,
                                 static_cast<FrameDumpResolutionType>(resolution_idx));
        Config::Save();
      }

      int pngCompression = Config::Get(Config::GFX_PNG_COMPRESSION_LEVEL);
      if (ImGui::SliderInt("PNG Compression Level", &pngCompression, 0, 9))
      {
        Config::SetBaseOrCurrent(Config::GFX_PNG_COMPRESSION_LEVEL, pngCompression);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Misc"))
    {
      bool enableProgScan = Config::Get(Config::SYSCONF_PROGRESSIVE_SCAN);
      if (ImGui::Checkbox("Enable Progressive Scan", &enableProgScan))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_PROGRESSIVE_SCAN, enableProgScan);
        Config::Save();
      }

#ifndef WINRT_XBOX  // This currently only works on the Vulkan backend
      bool backendMultithreading = Config::Get(Config::GFX_BACKEND_MULTITHREADING);
      if (ImGui::Checkbox("Backend Multithreading", &backendMultithreading))
      {
        Config::SetBaseOrCurrent(Config::GFX_BACKEND_MULTITHREADING, backendMultithreading);
        Config::Save();
      }
#endif

      bool preferVsForLinePoint = Config::Get(Config::GFX_PREFER_VS_FOR_LINE_POINT_EXPANSION);
      if (ImGui::Checkbox("Prefer VS for Point/Line Expansion", &preferVsForLinePoint))
      {
        Config::SetBaseOrCurrent(Config::GFX_PREFER_VS_FOR_LINE_POINT_EXPANSION,
                                 preferVsForLinePoint);
        Config::Save();
      }

      bool cpuCull = Config::Get(Config::GFX_CPU_CULL);
      if (ImGui::Checkbox("Cull Vertices on the CPU", &cpuCull))
      {
        Config::SetBaseOrCurrent(Config::GFX_CPU_CULL, cpuCull);
        Config::Save();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Experimental"))
    {
      bool deferEfbAccess = Config::Get(Config::GFX_HACK_EFB_DEFER_INVALIDATION);
      if (ImGui::Checkbox("Defer EFB Cache Invalidation", &deferEfbAccess))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_EFB_DEFER_INVALIDATION, deferEfbAccess);
        Config::Save();
      }

      bool manualTextureSampling = Config::Get(Config::GFX_HACK_FAST_TEXTURE_SAMPLING);
      if (ImGui::Checkbox("Fast Texture Sampling", &manualTextureSampling))
      {
        Config::SetBaseOrCurrent(Config::GFX_HACK_FAST_TEXTURE_SAMPLING, manualTextureSampling);
        Config::Save();
      }

      ImGui::TreePop();
    }

    ImGui::TreePop();
  }
}

void CreateControlsTab(UIState* state)
{
  auto devices = g_controller_interface.GetAllDeviceStrings();

  if (ImGui::BeginTabBar("controlsbar"))
  {
    if (ImGui::BeginTabItem("GameCube"))
    {
      // Layout ports with Map button in two columns
      if (ImGui::BeginTable("gc_ports_tbl", 2, ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; i++)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          CreateGCPort(i, devices);
          ImGui::TableNextColumn();
          if (ImGui::Button(std::format("Map##gc{}", i).c_str(), ImVec2(-1, 0)))
          {
            state->showMappingWindow = true;
            state->mappingWindowPort = i;
            state->mappingWindowIsWii = false;
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Wii"))
    {
      // Layout ports with Map button in two columns
      if (ImGui::BeginTable("wii_ports_tbl", 2, ImGuiTableFlags_SizingStretchProp))
      {
        ImGui::TableSetupColumn("Port", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();
        for (int i = 0; i < 4; i++)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          CreateWiiPort(i, devices);
          ImGui::TableNextColumn();
          if (ImGui::Button(std::format("Map##wii{}", i).c_str(), ImVec2(-1, 0)))
          {
            state->showMappingWindow = true;
            state->mappingWindowPort = i;
            state->mappingWindowIsWii = true;
          }
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Alternate Input Sources"))
    {
      bool dsu_enabled = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS_ENABLED);
      if (ImGui::Checkbox("Enable DSU", &dsu_enabled))
      {
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS_ENABLED,
                                 dsu_enabled);
        Config::Save();
        if (dsu_enabled)
          g_controller_interface.RefreshDevices();
      }
      ImGui::Separator();
      std::string servers = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS);
      std::vector<std::string> entries;
      for (size_t p = 0, n; p < servers.size(); p = n + 1)
      {
        n = servers.find(';', p);
        if (n == std::string::npos)
          break;
        auto e = servers.substr(p, n - p);
        if (!e.empty())
          entries.push_back(e);
      }
      static int selected_dsu = -1;
      for (int i = 0; i < (int)entries.size(); ++i)
      {
        if (ImGui::Selectable(entries[i].c_str(), selected_dsu == i))
          selected_dsu = i;
      }
      ImGui::Separator();
      static char dsu_addr[64] = "";
      static int dsu_port = ciface::DualShockUDPClient::DEFAULT_SERVER_PORT;
      ImGui::Text("Address");
      ImGui::SameLine();
      if (ImGui::Button("Edit IP"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGui::InputText("##dsu_addr", dsu_addr, sizeof(dsu_addr));
      ImGui::InputInt("Port", &dsu_port);
      if (ImGui::Button("Add Server"))
      {
        auto entry = fmt::format("DS4:{}:{};", dsu_addr, dsu_port);
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS, servers + entry);
        Config::Save();
        servers = Config::Get(ciface::DualShockUDPClient::Settings::SERVERS);
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove Selected") && selected_dsu >= 0)
      {
        std::string rebuilt;
        for (int i = 0; i < (int)entries.size(); ++i)
        {
          if (i == selected_dsu)
            continue;
          rebuilt += entries[i] + ";";
        }
        Config::SetBaseOrCurrent(ciface::DualShockUDPClient::Settings::SERVERS, rebuilt);
        Config::Save();
        selected_dsu = -1;
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  // Show mapping window
  if (state->showMappingWindow)
    ImGuiMappingWindow::Draw(state->mappingWindowPort, state);
}

void CreateGameCubeTab(UIState* state)
{
  if (ImGui::TreeNode("IPL Settings"))
  {
    const char* language_items[] = {"English", "German", "French", "Spanish", "Italian", "Dutch"};
    auto lang = Config::Get(Config::MAIN_GC_LANGUAGE);

    for (int i = 0; i < 6; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(language_items[i], i == lang))
      {
        Config::SetBaseOrCurrent(Config::MAIN_GC_LANGUAGE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    bool have_menu = false;
    for (const std::string dir : {"USA", "JAP", "EUR"})
    {
      const auto path = "/" + dir + "/" + GC_IPL;
      if (File::Exists(File::GetUserPath(D_GCUSER_IDX) + path) ||
          File::Exists(File::GetSysDirectory() + GC_SYS_DIR + path))
      {
        have_menu = true;
        break;
      }
    }

    bool skip_ipl = Config::Get(Config::MAIN_SKIP_IPL);
    if (ImGui::Checkbox("Skip Main Menu", &skip_ipl))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SKIP_IPL, skip_ipl);
      Config::Save();
    }
    if (!have_menu)
    {
      ImGui::TextWrapped("Put IPL ROMs in User/GC/<region>.");
    }

    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Load GameCube Main Menu"))
  {
    auto has_ipl = [](DiscIO::Region region) {
      std::string region_dir;
      switch (region)
      {
      case DiscIO::Region::NTSC_J:
        region_dir = "JAP";
        break;
      case DiscIO::Region::NTSC_U:
        region_dir = "USA";
        break;
      case DiscIO::Region::PAL:
        region_dir = "EUR";
        break;
      default:
        return false;
      }
      return File::Exists(File::GetUserPath(D_GCUSER_IDX) + "/" + region_dir + "/" + GC_IPL) ||
             File::Exists(File::GetSysDirectory() + GC_SYS_DIR + "/" + region_dir + "/" + GC_IPL);
    };
    auto BootGameCubeIPL = [state](DiscIO::Region region) {
      state->controlsDisabled = true;
      state->showSettingsWindow = false;
      state->pending_boot_params = std::make_unique<BootParameters>(BootParameters::IPL(region));
    };

    bool any = false;
    if (has_ipl(DiscIO::Region::NTSC_J))
    {
      any = true;
      if (ImGui::Button("NTSC-J"))
        BootGameCubeIPL(DiscIO::Region::NTSC_J);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("NTSC-J");
      ImGui::EndDisabled();
    }
    if (has_ipl(DiscIO::Region::NTSC_U))
    {
      any = true;
      if (ImGui::Button("NTSC-U"))
        BootGameCubeIPL(DiscIO::Region::NTSC_U);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("NTSC-U");
      ImGui::EndDisabled();
    }
    if (has_ipl(DiscIO::Region::PAL))
    {
      any = true;
      if (ImGui::Button("PAL"))
        BootGameCubeIPL(DiscIO::Region::PAL);
    }
    else
    {
      ImGui::BeginDisabled();
      ImGui::Button("PAL");
      ImGui::EndDisabled();
    }
    if (!any)
      ImGui::TextWrapped("Put IPL ROMs in User/GC/<region>/IPL.bin");
    ImGui::TreePop();
  }
  auto slot1 = Config::Get(Config::MAIN_SLOT_A);
  if (ImGui::TreeNode("Slot A"))
  {
    if (ImGui::RadioButton("<Nothing>", slot1 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", slot1 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Memory Card", slot1 == ExpansionInterface::EXIDeviceType::MemoryCard))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::MemoryCard);
      Config::Save();
    }

    if (ImGui::RadioButton("GCI Folder",
                           slot1 == ExpansionInterface::EXIDeviceType::MemoryCardFolder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A,
                               ExpansionInterface::EXIDeviceType::MemoryCardFolder);
      Config::Save();
    }

    if (ImGui::RadioButton("USB Gecko", slot1 == ExpansionInterface::EXIDeviceType::Gecko))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::Gecko);
      Config::Save();
    }

    if (ImGui::RadioButton("Advance Game Port", slot1 == ExpansionInterface::EXIDeviceType::AGP))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_A, ExpansionInterface::EXIDeviceType::AGP);
      Config::Save();
    }

    ImGui::TreePop();
  }

  // Todo: This really shouldn't be copy+pasted and could be cleaned up.
  auto slot2 = Config::Get(Config::MAIN_SLOT_B);
  if (ImGui::TreeNode("Slot B"))
  {
    if (ImGui::RadioButton("<Nothing>", slot2 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", slot2 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Memory Card", slot2 == ExpansionInterface::EXIDeviceType::MemoryCard))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::MemoryCard);
      Config::Save();
    }

    if (ImGui::RadioButton("GCI Folder",
                           slot2 == ExpansionInterface::EXIDeviceType::MemoryCardFolder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B,
                               ExpansionInterface::EXIDeviceType::MemoryCardFolder);
      Config::Save();
    }

    if (ImGui::RadioButton("USB Gecko", slot2 == ExpansionInterface::EXIDeviceType::Gecko))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::Gecko);
      Config::Save();
    }

    if (ImGui::RadioButton("Advance Game Port", slot2 == ExpansionInterface::EXIDeviceType::AGP))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SLOT_B, ExpansionInterface::EXIDeviceType::AGP);
      Config::Save();
    }

    ImGui::TreePop();
  }

  auto sp1 = Config::Get(Config::MAIN_SERIAL_PORT_1);
  if (ImGui::TreeNode("SP1"))
  {
    if (ImGui::RadioButton("<Nothing>", sp1 == ExpansionInterface::EXIDeviceType::None))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1, ExpansionInterface::EXIDeviceType::None);
      Config::Save();
    }

    if (ImGui::RadioButton("Dummy", sp1 == ExpansionInterface::EXIDeviceType::Dummy))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::Dummy);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (TAP)",
                           sp1 == ExpansionInterface::EXIDeviceType::Ethernet))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::Ethernet);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (XLink Kai)",
                           sp1 == ExpansionInterface::EXIDeviceType::EthernetXLink))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::EthernetXLink);
      Config::Save();
    }

    if (ImGui::RadioButton("Broadband Adapter (HLE)",
                           sp1 == ExpansionInterface::EXIDeviceType::EthernetBuiltIn))
    {
      Config::SetBaseOrCurrent(Config::MAIN_SERIAL_PORT_1,
                               ExpansionInterface::EXIDeviceType::EthernetBuiltIn);
      Config::Save();
    }

    ImGui::TreePop();
  }
}

void CreateWiiTab(UIState* state)
{
  static WiiUtils::UpdateResult last_res = WiiUtils::UpdateResult::Cancelled;
  if (ImGui::Button("Load Wii System Menu") && !state->controlsDisabled)
  {
    state->controlsDisabled = true;
    state->showSettingsWindow = false;
    state->pending_boot_params =
        std::make_unique<BootParameters>(BootParameters::NANDTitle{Titles::SYSTEM_MENU});
  }
  ImGui::TextWrapped("Boots the Wii System Menu from the NAND.");

  bool pal60 = Config::Get(Config::SYSCONF_PAL60);
  if (ImGui::Checkbox("Enable PAL60", &pal60))
  {
    Config::SetBaseOrCurrent(Config::SYSCONF_PAL60, pal60);
    Config::Save();
  }
  ImGui::TextWrapped("Enables PAL60 mode (480p) instead of standard PAL (576i) for PAL games.");

  bool enable_wiilink = Config::Get(Config::MAIN_WII_WIILINK_ENABLE);
  if (ImGui::Checkbox("Enable WiiConnect24 via WiiLink", &enable_wiilink))
  {
    Config::SetBaseOrCurrent(Config::MAIN_WII_WIILINK_ENABLE, enable_wiilink);
    Config::Save();
  }
  ImGui::TextWrapped("Enables WiiConnect24 features via WiiLink, such as the Wii Mail service.");

  const char* language_items[] = {"Japanese",
                                  "English",
                                  "German",
                                  "French",
                                  "Spanish",
                                  "Italian",
                                  "Dutch",
                                  "Simplified Chinese",
                                  "Traditional Chinese",
                                  "Korean"};
  auto lang = Config::Get(Config::SYSCONF_LANGUAGE);

  if (ImGui::TreeNode("System Language"))
  {
    for (u32 i = 0; i < 10; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(language_items[i], i == lang))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_LANGUAGE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    ImGui::TreePop();
  }

  const char* sound_items[] = {"Mono", "Stereo", "Surround"};
  auto sound = Config::Get(Config::SYSCONF_SOUND_MODE);

  if (ImGui::TreeNode("Sound"))
  {
    for (u32 i = 0; i < 3; i++)
    {
      ImGui::PushID(i);
      if (ImGui::RadioButton(sound_items[i], i == sound))
      {
        Config::SetBaseOrCurrent(Config::SYSCONF_SOUND_MODE, i);
        Config::Save();
      }
      ImGui::PopID();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("SD Card Settings"))
  {
    bool insert_sd_card = Config::Get(Config::MAIN_WII_SD_CARD);
    if (ImGui::Checkbox("Insert SD Card", &insert_sd_card))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD, insert_sd_card);
      Config::Save();
    }
    ImGui::TextWrapped("Supports SD and SDHC. Default size is 128 MB.");

    bool allow_sd_writes = Config::Get(Config::MAIN_ALLOW_SD_WRITES);
    if (ImGui::Checkbox("Allow Writes to SD Card", &allow_sd_writes))
    {
      Config::SetBaseOrCurrent(Config::MAIN_ALLOW_SD_WRITES, allow_sd_writes);
      Config::Save();
    }

    std::string sd_card_path = Config::Get(Config::MAIN_WII_SD_CARD_IMAGE_PATH);
    char sd_card_path_buf[256];
    strncpy(sd_card_path_buf, sd_card_path.c_str(), sizeof(sd_card_path_buf));
    ImGui::InputText("SD Card Path", sd_card_path_buf, sizeof(sd_card_path_buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse##SDCard"))
    {
      state->controlsDisabled = true;
      UWP::OpenDiscPicker();
      state->controlsDisabled = false;
    }

    bool sync_sd_folder = Config::Get(Config::MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC);
    if (ImGui::Checkbox("Automatically Sync with Folder", &sync_sd_folder))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC, sync_sd_folder);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Synchronizes the SD Card with the SD Sync Folder when starting and ending emulation.");

    std::string sd_sync_folder = Config::Get(Config::MAIN_WII_SD_CARD_SYNC_FOLDER_PATH);
    char sd_sync_folder_buf[256];
    strncpy(sd_sync_folder_buf, sd_sync_folder.c_str(), sizeof(sd_sync_folder_buf));
    ImGui::InputText("SD Sync Folder", sd_sync_folder_buf, sizeof(sd_sync_folder_buf),
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("Browse##SDSync"))
    {
      state->controlsDisabled = true;
      UWP::OpenGameFolderPicker([state](std::string path) {
        if (path != "")
        {
          Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_SYNC_FOLDER_PATH, path);
          Config::Save();
        }
        state->controlsDisabled = false;
      });
    }

    const char* sd_size_items[] = {"128 MB", "256 MB", "512 MB", "1 GB", "2 GB",
                                   "4 GB",   "8 GB",   "16 GB",  "32 GB"};
    u64 sd_size = Config::Get(Config::MAIN_WII_SD_CARD_FILESIZE);
    int sd_size_index = 0;
    for (size_t i = 0; i < IM_ARRAYSIZE(sd_size_items); ++i)
    {
      if (sd_size == (128 * 1024 * 1024) * (1ULL << i))
      {
        sd_size_index = static_cast<int>(i);
        break;
      }
    }
    if (ImGui::Combo("SD Card File Size", &sd_size_index, sd_size_items,
                     IM_ARRAYSIZE(sd_size_items)))
    {
      Config::SetBaseOrCurrent(Config::MAIN_WII_SD_CARD_FILESIZE,
                               (128 * 1024 * 1024) * (1ULL << sd_size_index));
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Wii Remote Settings"))
  {
    bool wiimote_motor = Config::Get(Config::SYSCONF_WIIMOTE_MOTOR);
    if (ImGui::Checkbox("Enable Rumble", &wiimote_motor))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_WIIMOTE_MOTOR, wiimote_motor);
      Config::Save();
    }

    const char* sensor_position_items[] = {"Top", "Bottom"};
    int sensor_position = Config::Get(Config::SYSCONF_SENSOR_BAR_POSITION);
    if (ImGui::Combo("Sensor Bar Position", &sensor_position, sensor_position_items,
                     IM_ARRAYSIZE(sensor_position_items)))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SENSOR_BAR_POSITION, sensor_position);
      Config::Save();
    }

    int ir_sensitivity = Config::Get(Config::SYSCONF_SENSOR_BAR_SENSITIVITY);
    if (ImGui::SliderInt("IR Sensitivity", &ir_sensitivity, 1, 5))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SENSOR_BAR_SENSITIVITY, ir_sensitivity);
      Config::Save();
    }

    int speaker_volume = Config::Get(Config::SYSCONF_SPEAKER_VOLUME);
    if (ImGui::SliderInt("Speaker Volume", &speaker_volume, 0, 127))
    {
      Config::SetBaseOrCurrent(Config::SYSCONF_SPEAKER_VOLUME, speaker_volume);
      Config::Save();
    }

    ImGui::TreePop();
  }

  ImGui::Separator();
  static bool show_online_update_modal = false;
  if (ImGui::Button("Online System Update"))
    show_online_update_modal = true;
  if (show_online_update_modal)
  {
    ImGui::OpenPopup("Online System Update");
    show_online_update_modal = false;
  }
  if (ImGui::BeginPopupModal("Online System Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Select Region:");
    static int region_idx = 0;
    const char* region_items[] = {"JPN", "USA", "EUR", "KOR"};
    ImGui::Combo("Region", &region_idx, region_items, IM_ARRAYSIZE(region_items));
    if (ImGui::Button("OK"))
    {
      state->controlsDisabled = true;
      WiiUtils::UpdateResult res = WiiUtils::DoOnlineUpdate(
          [](size_t, size_t, u64) { return true; }, region_items[region_idx]);
      state->controlsDisabled = false;
      ImGui::CloseCurrentPopup();
      // Show result in popup
      last_res = res;
      ImGui::OpenPopup("Update Result");
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Update Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    const char* msg = "";
    switch (last_res)
    {
    case WiiUtils::UpdateResult::Succeeded:
      msg = "Update completed successfully.";
      break;
    case WiiUtils::UpdateResult::AlreadyUpToDate:
      msg = "Already up-to-date.";
      break;
    default:
      msg = "Update failed or cancelled.";
      break;
    }
    ImGui::TextWrapped("%s", msg);
    if (ImGui::Button("OK"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::Separator();
  if (ImGui::Button("Disc-based System Update"))
  {
    state->controlsDisabled = true;
    UWP::OpenDiscPicker();
    state->controlsDisabled = false;
  }
}

void CreateAdvancedTab(UIState* state)
{
  if (ImGui::TreeNode("CPU Options"))
  {
#ifdef WINRT_XBOX
    // Only show available cores for Xbox
    // Enum values: Interpreter=0, JIT64=1, CachedInterpreter=5
    const char* cpu_cores[] = {"Interpreter", "JIT64", "Cached Interpreter"};
    const PowerPC::CPUCore cpu_core_values[] = {PowerPC::CPUCore::Interpreter,
                                                PowerPC::CPUCore::JIT64,
                                                PowerPC::CPUCore::CachedInterpreter};
    const int cpu_cores_count = 3;
#else
    // Enum values: Interpreter=0, JIT64=1, JITARM64=4, CachedInterpreter=5
    const char* cpu_cores[] = {"Interpreter", "JIT64", "JITARM64", "Cached Interpreter"};
    const PowerPC::CPUCore cpu_core_values[] = {PowerPC::CPUCore::Interpreter,
                                                PowerPC::CPUCore::JIT64, PowerPC::CPUCore::JITARM64,
                                                PowerPC::CPUCore::CachedInterpreter};
    const int cpu_cores_count = 4;
#endif
    PowerPC::CPUCore config_core = Config::Get(Config::MAIN_CPU_CORE);
    int combo_index = 0;
    for (int i = 0; i < cpu_cores_count; ++i)
    {
      if (cpu_core_values[i] == config_core)
      {
        combo_index = i;
        break;
      }
    }
    if (ImGui::Combo("CPU Emulation Engine", &combo_index, cpu_cores, cpu_cores_count))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CPU_CORE, cpu_core_values[combo_index]);
      Config::Save();
    }

    ImGui::TreePop();
  }

  bool mmuEnable = Config::Get(Config::MAIN_MMU);
  if (ImGui::Checkbox("Enable MMU", &mmuEnable))
  {
    Config::SetBaseOrCurrent(Config::MAIN_MMU, mmuEnable);
    Config::Save();
  }
  ImGui::TextWrapped("Enables the Memory Management Unit, needed for some games. (ON = Compatible, "
                     "OFF = Fast)\n\nIf unsure, leave this unchecked.");

  bool pauseOnPanic = Config::Get(Config::MAIN_PAUSE_ON_PANIC);
  if (ImGui::Checkbox("Pause on Panic", &pauseOnPanic))
  {
    Config::SetBaseOrCurrent(Config::MAIN_PAUSE_ON_PANIC, pauseOnPanic);
    Config::Save();
  }
  ImGui::TextWrapped("Pauses the emulation if a Read/Write or Unknown Instruction panic "
                     "occurs.\nEnabling will affect performance.\nThe performance impact is the "
                     "same as having Enable MMU on.\n\nIf unsure, leave this unchecked.");

  bool accurateCPUCache = Config::Get(Config::MAIN_ACCURATE_CPU_CACHE);
  if (ImGui::Checkbox("Enable Write-Back Cache (slow)", &accurateCPUCache))
  {
    Config::SetBaseOrCurrent(Config::MAIN_ACCURATE_CPU_CACHE, accurateCPUCache);
    Config::Save();
  }
  ImGui::TextWrapped("Enables emulation of the CPU write-back cache.\nEnabling will have a "
                     "significant impact on performance.\nThis should be left disabled unless "
                     "absolutely needed.\n\nIf unsure, leave this unchecked.");

  bool syncOnSkipIdle = Config::Get(Config::MAIN_SYNC_ON_SKIP_IDLE);
  if (ImGui::Checkbox("Sync on Skip Idle", &syncOnSkipIdle))
  {
    Config::SetBaseOrCurrent(Config::MAIN_SYNC_ON_SKIP_IDLE, syncOnSkipIdle);
    Config::Save();
  }
  ImGui::TextWrapped("Synchronizes the GPU thread with the CPU thread when the CPU is idle.\nThis "
                     "helps prevent desynchronization between the CPU and GPU, which can cause "
                     "visual glitches or crashes.\n\nIf unsure, leave this checked.");

  bool emulateDiscSpeed = !Config::Get(Config::MAIN_FAST_DISC_SPEED);
  if (ImGui::Checkbox("Emulate Disc Speed", &emulateDiscSpeed))
  {
    Config::SetBaseOrCurrent(Config::MAIN_FAST_DISC_SPEED, !emulateDiscSpeed);
    Config::Save();
  }
  ImGui::TextWrapped(
      "When checked, the emulator uses normal disc speed (ON = Stock, OFF = Fast).\nDisabling "
      "emulation accelerates the disc transfer rate, removing loading times but may cause issues "
      "with games that rely on precise timing.\n\nIf unsure, leave this checked.");

  if (ImGui::TreeNode("Clock Override"))
  {
    ImGui::Text("WARNING: Changing this from the default (1.0 = 100%) can and will break\ngames "
                "and cause glitches. Do so at your own risk. \nPlease do not report bugs that "
                "occur with a non-default clock. \nThis is not a magical performance slider!");

    bool overclockEnable = Config::Get(Config::MAIN_OVERCLOCK_ENABLE);
    if (ImGui::Checkbox("Enable Emulated CPU Clock Override", &overclockEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK_ENABLE, overclockEnable);
      Config::Save();
    }

    float clockOverride = Config::Get(Config::MAIN_OVERCLOCK);
    if (ImGui::SliderFloat("Emulated CPU Clock Speed Override", &clockOverride, 0.06f, 4.0f))
    {
      Config::SetBaseOrCurrent(Config::MAIN_OVERCLOCK, clockOverride);
      Config::Save();
    }

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Memory Override"))
  {
    bool ramOverrideEnable = Config::Get(Config::MAIN_RAM_OVERRIDE_ENABLE);
    if (ImGui::Checkbox("Enable Emulated Memory Size Override", &ramOverrideEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_RAM_OVERRIDE_ENABLE, ramOverrideEnable);
      Config::Save();
    }

    if (ramOverrideEnable)
    {
      u32 mem1Size = Config::Get(Config::MAIN_MEM1_SIZE) / 0x100000;  // Convert to MB
      if (ImGui::SliderInt("MEM1 Size (MB)", (int*)&mem1Size, 24, 64))
      {
        Config::SetBaseOrCurrent(Config::MAIN_MEM1_SIZE, mem1Size * 0x100000);
        Config::Save();
      }

      u32 mem2Size = Config::Get(Config::MAIN_MEM2_SIZE) / 0x100000;  // Convert to MB
      if (ImGui::SliderInt("MEM2 Size (MB)", (int*)&mem2Size, 64, 128))
      {
        Config::SetBaseOrCurrent(Config::MAIN_MEM2_SIZE, mem2Size * 0x100000);
        Config::Save();
      }
    }

    ImGui::TextWrapped(
        "Adjusts the amount of RAM in the emulated console.\n\n"
        "WARNING: Enabling this will completely break many games. Only a small number "
        "of games can benefit from this.");

    ImGui::TreePop();
  }

  if (ImGui::TreeNode("Custom RTC Options"))
  {
    bool customRTCEnable = Config::Get(Config::MAIN_CUSTOM_RTC_ENABLE);
    if (ImGui::Checkbox("Enable Custom RTC", &customRTCEnable))
    {
      Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_ENABLE, customRTCEnable);
      Config::Save();
    }

    if (customRTCEnable)
    {
      // Get current RTC value
      u64 customRTC = Config::Get(Config::MAIN_CUSTOM_RTC_VALUE);
      time_t current_time = customRTC;
      struct tm* timeinfo = gmtime(&current_time);

      // Create a date/time picker
      int year = timeinfo->tm_year + 1900;
      int month = timeinfo->tm_mon + 1;
      int day = timeinfo->tm_mday;
      int hour = timeinfo->tm_hour;
      int minute = timeinfo->tm_min;
      int second = timeinfo->tm_sec;

      bool date_changed = false;
      date_changed |= ImGui::InputInt("Year", &year, 1, 100);
      date_changed |= ImGui::InputInt("Month", &month, 1, 12);
      date_changed |= ImGui::InputInt("Day", &day, 1, 31);
      date_changed |= ImGui::InputInt("Hour", &hour, 1, 23);
      date_changed |= ImGui::InputInt("Minute", &minute, 1, 59);
      date_changed |= ImGui::InputInt("Second", &second, 1, 59);

      if (date_changed)
      {
        // Validate and clamp values
        year = std::clamp(year, 2000, 2099);
        month = std::clamp(month, 1, 12);
        day = std::clamp(day, 1, 31);
        hour = std::clamp(hour, 0, 23);
        minute = std::clamp(minute, 0, 59);
        second = std::clamp(second, 0, 59);

        // Convert to time_t
        struct tm new_time = {};
        new_time.tm_year = year - 1900;
        new_time.tm_mon = month - 1;
        new_time.tm_mday = day;
        new_time.tm_hour = hour;
        new_time.tm_min = minute;
        new_time.tm_sec = second;
        new_time.tm_isdst = -1;

        time_t new_time_t = _mkgmtime(&new_time);
        if (new_time_t != -1)
        {
          Config::SetBaseOrCurrent(Config::MAIN_CUSTOM_RTC_VALUE, static_cast<u64>(new_time_t));
          Config::Save();
        }
      }
    }

    ImGui::TextWrapped("This setting allows you to set a custom real time clock (RTC) separate "
                       "from your current system time.\n\nIf unsure, leave this unchecked.");

    ImGui::TreePop();
  }
}

void CreatePathsTab(UIState* state)
{
  ImGui::Text("Game Folders");
  if (ImGui::BeginListBox("##folders"))
  {
    for (auto path : m_paths)
    {
      if (ImGui::Selectable(path.c_str()))
      {
        state->selectedPath = path;
      }
    }
    ImGui::EndListBox();
  }

  if (state->selectedPath == "")
  {
    ImGui::BeginDisabled();
  }

  if (ImGui::Button("Remove Path"))
  {
    if (state->selectedPath != "")
    {
      m_paths.erase(std::remove(m_paths.begin(), m_paths.end(), state->selectedPath),
                    m_paths.end());
      Config::SetIsoPaths(m_paths);
      Config::Save();
    }
  }

  if (state->selectedPath == "")
  {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (ImGui::Button("Add Path"))
  {
    state->controlsDisabled = true;
    UWP::OpenGameFolderPicker([state](std::string path) {
      if (path != "")
      {
        if (!UWP::TestPathPermissions(path))
        {
          m_show_path_warning = true;
        }
        else
        {
          m_paths.emplace_back(path);
          Config::SetIsoPaths(m_paths);
          Config::Save();
        }
      }

      state->controlsDisabled = false;
    });
  }

  ImGui::Spacing();
  ImGui::Separator();

  if (ImGui::Button("Set Dolphin User Folder Location"))
  {
    state->controlsDisabled = true;
    UWP::OpenNewUserPicker([=](std::string path) {
      if (path != "")
      {
        if (!UWP::TestPathPermissions(path))
        {
          m_show_path_warning = true;
        }
      }

      state->controlsDisabled = false;
    });

    // Reset everything and load the new config location.
    UICommon::Shutdown();
    UICommon::SetUserDirectory(UWP::GetUserLocation());
    UICommon::CreateDirectories();
    UICommon::Init();
  }

  if (ImGui::Button("Reset Dolphin User Folder Location"))
  {
    UWP::ResetUserLocation();

    // Reset everything and load the new config location.
    UICommon::Shutdown();
    UICommon::SetUserDirectory(UWP::GetUserLocation());
    UICommon::CreateDirectories();
    UICommon::Init();
  }

  if (m_show_path_warning)
  {
    ImGui::OpenPopup("Warning");
    m_show_path_warning = false;
    ImGui::SetNextWindowSize(ImVec2(950 * m_frame_scale, 80 * m_frame_scale));
  }

  if (ImGui::BeginPopupModal("Warning"))
  {
    ImGui::TextWrapped("The folder path you have selected is not writable! Please check that you "
                       "have the correct permissions for the folder you have selected.");
    ImGui::Separator();
    if (ImGui::Button("OK"))
    {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextWrapped("Note: Please remember to do your USB filesystem setup, or paths to "
                     "your USB will not work properly!");
}

void CreateWiiPort(int index, std::vector<std::string> devices)
{
  if (ImGui::BeginChild(std::format("gc-wii-{}", index).c_str(), ImVec2(-1, 75 * m_frame_scale),
                        true))
  {
    auto controller = Wiimote::GetConfig()->GetController(index);
    auto default_device = controller->GetDefaultDevice().name;

    ImGui::Text("Wiimote Port %d", index + 1);

    if (ImGui::BeginCombo("Device", default_device.c_str()))
    {
      for (auto device : devices)
      {
        if (ImGui::Selectable(device.c_str(), strcmp(default_device.c_str(), device.c_str()) == 0))
        {
          controller->SetDefaultDevice(device);
          controller->UpdateReferences(g_controller_interface);
          Wiimote::GetConfig()->SaveConfig();
        }
      }

      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Profile", m_selected_wiimote_profile[index].c_str()))
    {
      for (auto profile : m_wiimote_profiles)
      {
        if (ImGui::Selectable(profile.c_str(), m_selected_wiimote_profile[index] == profile))
        {
          m_selected_wiimote_profile[index] = profile;

          if (m_selected_wiimote_profile[index] == "None")
          {
            // Loading an empty inifile section clears everything.
            Common::IniFile::Section sec;

            controller->LoadConfig(&sec);
            controller->SetDefaultDevice(default_device);
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::None);
          }
          else if (m_selected_wiimote_profile[index] == "Wiimote + Nunchuk")
          {
            controller->LoadDefaults(g_controller_interface);

            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else if (m_selected_wiimote_profile[index] == "Classic Controller")
          {
            Common::IniFile ini;
            ini.Load(File::GetSysDirectory() + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/Classic.ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else if (m_selected_wiimote_profile[index] == "Sideways Wiimote")
          {
            Common::IniFile ini;
            ini.Load(File::GetSysDirectory() + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/Sideways.ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }
          else
          {
            Common::IniFile ini;
            ini.Load(File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                     Wiimote::GetConfig()->GetProfileDirectoryName() + "/" + profile + ".ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForWiimoteSource(index),
                                     WiimoteSource::Emulated);
          }

          controller->UpdateReferences(g_controller_interface);
          Wiimote::GetConfig()->SaveConfig();
          Config::Save();
        }
      }

      ImGui::EndCombo();
    }
  }

  ImGui::EndChild();
}

void CreateGCPort(int index, std::vector<std::string> devices)
{
  if (ImGui::BeginChild(std::format("gc-port-{}", index).c_str(), ImVec2(-1, 75 * m_frame_scale),
                        true))
  {
    auto controller = Pad::GetConfig()->GetController(index);
    auto default_device = controller->GetDefaultDevice().name;

    ImGui::Text("GameCube Port %d", index + 1);

    if (ImGui::BeginCombo("Device", default_device.c_str()))
    {
      for (auto device : devices)
      {
        if (ImGui::Selectable(device.c_str(), strcmp(default_device.c_str(), device.c_str()) == 0))
        {
          controller->SetDefaultDevice(device);
          controller->UpdateReferences(g_controller_interface);
          Pad::GetConfig()->SaveConfig();
        }
      }

      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Profile", m_selected_gc_profile[index].c_str()))
    {
      for (auto profile : m_gc_profiles)
      {
        if (ImGui::Selectable(profile.c_str(), m_selected_gc_profile[index] == profile))
        {
          m_selected_gc_profile[index] = profile;

          if (m_selected_gc_profile[index] == "None")
          {
            // Loading an empty inifile section clears everything.
            Common::IniFile::Section sec;

            controller->LoadConfig(&sec);
            controller->SetDefaultDevice(default_device);
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_NONE);
          }
          else if (m_selected_gc_profile[index] == "Default")
          {
            controller->LoadDefaults(g_controller_interface);
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_GC_CONTROLLER);
          }
          else
          {
            Common::IniFile ini;
            ini.Load(File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                     Pad::GetConfig()->GetProfileDirectoryName() + "/" + profile + ".ini");

            controller->LoadConfig(ini.GetOrCreateSection("Profile"));
            Config::SetBaseOrCurrent(Config::GetInfoForSIDevice(index),
                                     SerialInterface::SIDevices::SIDEVICE_GC_CONTROLLER);
          }

          controller->UpdateReferences(g_controller_interface);
          Pad::GetConfig()->SaveConfig();
          Config::Save();
        }
      }

      ImGui::EndCombo();
    }
  }

  ImGui::EndChild();
}

FrontendResult ImGuiFrontend::CreateMainPage()
{
  // float selOffset = m_selectedGameIdx >= 5 ? 160.0f * (m_selectedGameIdx - 4) * -1.0f : 0;
  float posX = 30 * m_frame_scale;
  float posY = (345.0f / 2) * m_frame_scale;
  auto extraFlags = m_games.size() < 5 ? ImGuiWindowFlags_None :
                                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav;

  ImGui::SetNextWindowPos(ImVec2(posX, posY));
  if (ImGui::Begin("Dolphin Emulator", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground |
                       extraFlags))
  {
    auto game = CreateGameCarousel();
    ImGui::End();
    if (game != nullptr)
    {
      return FrontendResult(game);
    }
  }

  const u64 current_time_us = Common::Timer::NowUs();
  const u64 time_diff_us = current_time_us - m_imgui_last_frame_time;
  const float time_diff_secs = static_cast<float>(time_diff_us / 1000000.0);
  m_imgui_last_frame_time = current_time_us;

  // Update I/O with window dimensions.
  ImGuiIO& io = ImGui::GetIO();
  io.DeltaTime = time_diff_secs;

  return FrontendResult();  // keep running
}

FrontendResult ImGuiFrontend::CreateListPage()
{
  ImGui::SetNextWindowSize(ImVec2(540 * m_frame_scale, 425 * m_frame_scale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (540 / 2) * m_frame_scale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (425 / 2) * m_frame_scale));

  if (ImGui::Begin("Dolphin Emulator", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings))
  {
    auto game = CreateGameList();
    ImGui::End();
    if (game != nullptr)
    {
      return FrontendResult(game);
    }
  }

  return FrontendResult();
}

std::shared_ptr<UICommon::GameFile> ImGuiFrontend::CreateGameList()
{
  if (ImGui::Button("Search Game"))
  {
    UWP::ShowKeyboard();
    ImGui::SetKeyboardFocusHere();
  }
  ImGui::SameLine();

  ImGui::PushItemWidth(-1);
  ImGui::InputText("##gamesearch", m_list_search_buf, 32);
  ImGui::PopItemWidth();

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::BeginListBox("##Games List", ImVec2(-1, -1)))
  {
    size_t search = strlen(m_list_search_buf);
    std::vector<std::shared_ptr<UICommon::GameFile>> games;
    if (search > 0)
    {
      std::string search_phrase = std::string(m_list_search_buf);
      if (search_phrase != m_prev_list_search)
      {
        m_list_search_results.clear();
        for (auto& game : m_games)
        {
          auto& name = game->GetName(m_title_database);
          auto it = std::search(name.begin(), name.end(), search_phrase.begin(),
                                search_phrase.end(), [](unsigned char ch1, unsigned char ch2) {
                                  return std::toupper(ch1) == std::toupper(ch2);
                                });

          if (it != name.end())
          {
            m_list_search_results.push_back(game);
          }
        }

        m_prev_list_search = m_list_search_buf;
      }

      games = m_list_search_results;
    }
    else
    {
      games = m_games;
    }

    long timeSinceInit = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::high_resolution_clock::now() - m_time_since_init)
                             .count();
    for (auto& game : games)
    {
      if (ImGui::Selectable(
              std::format("{}##{}", game->GetName(m_title_database).c_str(), game->GetFilePath())
                  .c_str()) &&
          timeSinceInit > 1500)
      {
        ImGui::EndListBox();
        return game;
      }
    }

    ImGui::EndListBox();
  }

  return nullptr;
}

std::shared_ptr<UICommon::GameFile> ImGuiFrontend::CreateGameCarousel()
{
  if (m_last_category != m_ccat)
  {
    m_last_category = m_ccat;
    FilterGamesForCategory();

    m_selectedGameIdx = 0;
  }

  long timeSinceInit = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::high_resolution_clock::now() - m_time_since_init)
                           .count();

  // Add a small delay to prevent accidental game starts during controller initialization
  // This is a safety measure to avoid unintended game launches
  if (ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown) && timeSinceInit > 1500)
  {
    if (m_displayed_games.size() != 0)
    {
      return m_displayed_games[m_selectedGameIdx];
    }
  }

  // Display 5 games, 2 games to the left of the selection, 2 games to the right.
  for (int i = m_selectedGameIdx - 2; i < m_selectedGameIdx + 3; i++)
  {
    int idx = i;
    if (m_displayed_games.size() >= 4)
    {
      if (i < 0)
      {
        // wrap around, total games + -index
        idx = static_cast<int>(m_displayed_games.size()) + i;
      }
      else if (i >= m_displayed_games.size())
      {
        // wrap around, i - total games
        idx = i - static_cast<int>(m_displayed_games.size());
      }
    }
    else
    {
      if (i < 0)
      {
        idx = static_cast<int>(m_displayed_games.size()) + i;
      }
      else if (i >= m_displayed_games.size())
      {
        continue;
      }
    }

    if (idx < 0 || idx >= m_displayed_games.size())
      continue;

    ImVec4 border_col;
    float selectedScale = 1.0f;
    if (m_selectedGameIdx == idx)
    {
      border_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      // The background image doesn't fit 2 games very well when scaled up.
      selectedScale = m_displayed_games.size() > 2 ? 1.15 : 1.0f;
    }
    else
    {
      border_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    AbstractTexture* handle = GetHandleForGame(m_displayed_games[idx]);
    ImGui::SameLine();
    ImGui::BeginChild(
        m_displayed_games[idx]->GetFilePath().c_str(),
        ImVec2((160 + 25) * m_frame_scale * selectedScale, 250 * m_frame_scale * selectedScale),
        true,
        ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar);

    if (handle != 0)
    {
      ImGui::Image(
          (ImTextureID)handle,
          ImVec2(160.f * m_frame_scale * selectedScale, 224.f * m_frame_scale * selectedScale),
          ImVec2(0, 0), ImVec2(1, 1), ImVec4(1.0f, 1.0f, 1.0f, 1.0f), border_col);
      ImGui::Text(m_displayed_games[idx]->GetName(m_title_database).c_str());
    }
    else
    {
      ImGui::Text(m_displayed_games[idx]->GetName(m_title_database).c_str());
    }

    ImGui::EndChild();
  }

  return nullptr;
}

AbstractTexture* ImGuiFrontend::GetHandleForGame(std::shared_ptr<UICommon::GameFile> game)
{
  const std::string id = game->GetGameID();
  auto it = m_cover_textures.find(id);
  if (it == m_cover_textures.end())
  {
    if (auto tex = CreateCoverTexture(game))
    {
      m_cover_textures[id] = tex;
      return tex.get();
    }
    // placeholder and async download
    AbstractTexture* missing = GetOrCreateMissingTex();
    m_cover_textures[id] = std::shared_ptr<AbstractTexture>(missing, [](AbstractTexture*) {});
    std::thread([this, game, id]() {
      game->DownloadDefaultCover();
      if (auto real = CreateCoverTexture(game))
        m_cover_textures[id] = real;
    }).detach();
    return missing;
  }
  if (it->second)
    return it->second.get();
  // stale entry: retry
  m_cover_textures.erase(it);
  return GetHandleForGame(game);
}

std::shared_ptr<AbstractTexture> CreateTextureFromPath(std::string path, bool is_theme_asset)
{
  std::string file_data;
  if (!File::ReadFileToString(path, file_data))
    return {};

  std::vector<unsigned char> buffer = {file_data.begin(), file_data.end()};
  if (buffer.empty())
    return {};

  int width, height, channels;
  unsigned char* image_data = stbi_load_from_memory(buffer.data(), static_cast<int>(buffer.size()),
                                                    &width, &height, &channels, 4);
  if (!image_data)
    return {};

  TextureConfig tex_config(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                           AbstractTextureType::Texture_2D);

  std::string texture_name = is_theme_asset ? "theme:" + path : "cover:" + path;
  std::shared_ptr<AbstractTexture> tex = g_gfx->CreateTexture(tex_config, texture_name);
  if (!tex)
  {
    stbi_image_free(image_data);
    PanicAlertFmt("Failed to create ImGui texture");
    return {};
  }

  tex->Load(0, width, height, width, image_data, sizeof(u32) * width * height);
  stbi_image_free(image_data);

  return std::move(tex);
}

std::shared_ptr<AbstractTexture>
ImGuiFrontend::CreateCoverTexture(std::shared_ptr<UICommon::GameFile> game)
{
  const std::string path = File::GetUserPath(D_COVERCACHE_IDX) + game->GetGameTDBID() + ".png";
  if (!File::Exists(path))
    return nullptr;

  // Explicitly mark this as NOT a theme asset (false)
  return std::move(CreateTextureFromPath(path, false));
}

AbstractTexture* ImGuiFrontend::GetOrCreateMissingTex()
{
  if (m_missing_tex != nullptr && m_missing_tex.get() != nullptr)
    return m_missing_tex.get();

  // The missing texture is neither a theme asset nor a cover, so use default false
  auto missing_tex = CreateTextureFromPath("Assets/missing.png");
  m_missing_tex = std::move(missing_tex);

  return m_missing_tex.get();
}

void ImGuiFrontend::LoadGameList()
{
  // Clear the cover textures cache when reloading the game list to prevent memory issue with large
  // game libraries
  m_cover_textures.clear();

  m_paths.clear();
  m_games.clear();
  m_displayed_games.clear();
  m_paths = Config::GetIsoPaths();
  for (auto dir : m_paths)
  {
    RecurseFolderForGames(dir);
  }

#ifdef WINRT_XBOX
  // Load from the default path
  auto localCachePath = winrt::to_string(
      winrt::Windows::Storage::ApplicationData::Current().LocalCacheFolder().Path());
  RecurseFolderForGames(localCachePath);
#endif
}

void ImGuiFrontend::FilterGamesForCategory()
{
  m_displayed_games.clear();

  switch (m_ccat)
  {
  case CAll:
    m_displayed_games.insert(m_displayed_games.begin(), m_games.begin(), m_games.end());
    break;
  case CWii:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() != DiscIO::Platform::WiiDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  case CGC:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() != DiscIO::Platform::GameCubeDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  case COther:
    for (auto& game : m_games)
    {
      if (game->GetPlatform() == DiscIO::Platform::WiiDisc ||
          game->GetPlatform() == DiscIO::Platform::GameCubeDisc)
        continue;

      m_displayed_games.push_back(game);
    }
    break;
  }
}

void ImGuiFrontend::LoadThemes()
{
  std::string selected_theme = Config::Get(Config::FRONTEND_SELECTED_THEME);

  m_themes.clear();
  m_selected_theme = nullptr;

  RecurseForThemes("Sys/FrontendThemes/");
  RecurseForThemes(File::GetUserPath(D_THEMES_IDX));

  if (!m_selected_theme)
  {
    m_selected_theme = &m_themes["Flipper 2.2 - Beached"];
  }
}

void ImGuiFrontend::RecurseForThemes(std::string path)
{
  std::string selected_theme = Config::Get(Config::FRONTEND_SELECTED_THEME);

  for (auto folder : std::filesystem::directory_iterator(path))
  {
    if (!folder.is_directory())
    {
      continue;
    }

    FrontendTheme theme{};
    if (theme.TryLoad(folder.path().string()))
    {
      m_themes.emplace(theme.GetName(), theme);

      if (theme.GetName() == selected_theme)
      {
        m_selected_theme = &m_themes[theme.GetName()];
      }
    }
  }
}

void ImGuiFrontend::RecurseFolderForGames(std::string path)
{
  try
  {
    for (auto file : std::filesystem::directory_iterator(path))
    {
      if (file.is_directory())
      {
        RecurseFolderForGames(file.path().string());
        continue;
      }

      if (!file.is_regular_file())
        continue;

      std::filesystem::path normalised =
          std::filesystem::path(file.path().string()).make_preferred();
      std::string game_path = normalised.string();
      std::replace(game_path.begin(), game_path.end(), '\\', '/');
      auto game = new UICommon::GameFile(game_path);

      if (game && game->IsValid())
        m_games.emplace_back(std::move(game));
    }
  }
  catch (std::exception)
  {
    // This folder can't be opened.
  }
}

void ImGuiFrontend::AddGameFolder(std::string path)
{
  m_paths.push_back(path);
  Config::SetIsoPaths(m_paths);
}

bool ImGuiFrontend::TryInput(std::string expression, std::shared_ptr<ciface::Core::Device> device)
{
  auto* input = device->FindInput(expression);
  if (input == nullptr)
    return false;

  return input->GetState() > 0.5f;
}

void DrawSettingsMenu(UIState* state, float frame_scale)
{
  if (ImGui::BeginListBox("##tabs", ImVec2(100 * frame_scale, -1)))
  {
    if (ImGui::Selectable("General", state->selectedTab == General))
    {
      state->selectedTab = General;
    }
    if (ImGui::Selectable("Interface", state->selectedTab == Interface))
    {
      state->selectedTab = Interface;
    }
    if (ImGui::Selectable("Graphics", state->selectedTab == Graphics))
    {
      state->selectedTab = Graphics;
    }
    if (ImGui::Selectable("Audio", state->selectedTab == Audio))
    {
      state->selectedTab = Audio;
    }
    if (ImGui::Selectable("Controls", state->selectedTab == Controls))
    {
      state->selectedTab = Controls;
    }
    if (ImGui::Selectable("GameCube", state->selectedTab == GC))
    {
      state->selectedTab = GC;
    }
    if (ImGui::Selectable("Wii", state->selectedTab == Wii))
    {
      state->selectedTab = Wii;
    }
    if (ImGui::Selectable("Advanced", state->selectedTab == Advanced))
    {
      state->selectedTab = Advanced;
    }
    if (ImGui::Selectable("Paths", state->selectedTab == Paths))
    {
      state->selectedTab = Paths;
    }
    if (ImGui::Selectable("Achievements", state->selectedTab == Achievements))
    {
      state->selectedTab = Achievements;
    }
    if (ImGui::Selectable("About", state->selectedTab == About))
    {
      state->selectedTab = About;
    }

    ImGui::EndListBox();
  }

  ImGui::SameLine();
  if (ImGui::BeginChild("##tabview", ImVec2(-1, -1), true))
  {
    switch (state->selectedTab)
    {
    case General:
      CreateGeneralTab(state);
      break;
    case Interface:
      CreateInterfaceTab(state);
      break;
    case Graphics:
      CreateGraphicsTab(state);
      break;
    case Audio:
      CreateAudioTab(state);
      break;
    case Controls:
      CreateControlsTab(state);
      break;
    case GC:
      CreateGameCubeTab(state);
      break;
    case Wii:
      CreateWiiTab(state);
      break;
    case Paths:
      CreatePathsTab(state);
      break;
    case Advanced:
      CreateAdvancedTab(state);
      break;
    case About:
      ImGui::TextWrapped(
          "Dolphin Emulator on UWP - Version 1.1.9.0 (Based on Dolphin 2503a-306)\n\n"
          "This is a fork of Dolphin Emulator introducing Xbox support with a big picture "
          "frontend\n\n"
          "Credits:\n\n"
          "Dolphin: for their amazing work on Dolphin Emulator\n"
          "SirMangler: for their amazing work on porting Dolphin for UWP\n"
          "worleydl: For his oct2024-rebase branch (it helped so much)\n"
          "SirManglers Ko-Fi: https://ko-fi.com/sirmangler\n"
          "Sterns Ko-Fi: https://ko-fi.com/stern\n\n"
          "Dolphin Emulator is licensed under GPLv2+ and is not associated with Nintendo.");
      break;
    case Achievements:
      CreateAchievementsTab(state);
      break;
    }

    ImGui::EndChild();
  }
}

std::shared_ptr<AbstractTexture> FrontendTheme::GetBackground(ThemeBG cat)
{
  return m_textures[cat];
}

bool FrontendTheme::TryLoad(std::string path)
{
  const auto SetThemeOrBackup = [=](ThemeBG target, ThemeBG backup, std::string basePath) {
    // Try both PNG and JPEG/JPG formats
    std::string pngPath = basePath + ".png";
    std::string jpgPath = basePath + ".jpg";
    std::string jpegPath = basePath + ".jpeg";

    if (File::Exists(pngPath))
    {
      auto bg = CreateTextureFromPath(pngPath, true);
      m_textures[target] = std::move(bg);
    }
    else if (File::Exists(jpgPath))
    {
      auto bg = CreateTextureFromPath(jpgPath, true);
      m_textures[target] = std::move(bg);
    }
    else if (File::Exists(jpegPath))
    {
      auto bg = CreateTextureFromPath(jpegPath, true);
      m_textures[target] = std::move(bg);
    }

    if (!m_textures[target])
    {
      if (m_textures[backup])
      {
        m_textures[target] = m_textures[backup];
      }
    }
  };

  // Check for required files in both PNG and JPEG formats
  bool hasCarouselBg = File::Exists(path + "\\carousel_background_all.png") ||
                       File::Exists(path + "\\carousel_background_all.jpg") ||
                       File::Exists(path + "\\carousel_background_all.jpeg");

  bool hasMenuBg = File::Exists(path + "\\menu_background.png") ||
                   File::Exists(path + "\\menu_background.jpg") ||
                   File::Exists(path + "\\menu_background.jpeg");

  bool hasListUi = File::Exists(path + "\\list_ui.png") || File::Exists(path + "\\list_ui.jpg") ||
                   File::Exists(path + "\\list_ui.jpeg");

  bool hasCarouselUi = File::Exists(path + "\\carousel_ui.png") ||
                       File::Exists(path + "\\carousel_ui.jpg") ||
                       File::Exists(path + "\\carousel_ui.jpeg");

  if (!hasCarouselBg || !hasMenuBg || !hasListUi || !hasCarouselUi)
  {
    return false;
  }

  // Try loading carousel background in either format
  std::shared_ptr<AbstractTexture> all_bg;
  if (File::Exists(path + "\\carousel_background_all.png"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.png", true);
  else if (File::Exists(path + "\\carousel_background_all.jpg"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.jpg", true);
  else if (File::Exists(path + "\\carousel_background_all.jpeg"))
    all_bg = CreateTextureFromPath(path + "\\carousel_background_all.jpeg", true);

  if (!all_bg)
    return false;

  m_textures[ThemeBG::BG_All] = all_bg;

  // Try loading menu background in either format
  std::shared_ptr<AbstractTexture> menu_bg;
  if (File::Exists(path + "\\menu_background.png"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.png", true);
  else if (File::Exists(path + "\\menu_background.jpg"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.jpg", true);
  else if (File::Exists(path + "\\menu_background.jpeg"))
    menu_bg = CreateTextureFromPath(path + "\\menu_background.jpeg", true);

  if (!menu_bg)
    return false;

  m_textures[ThemeBG::BG_Menu] = menu_bg;

  // Try loading list UI in either format
  std::shared_ptr<AbstractTexture> list_ui_bg;
  if (File::Exists(path + "\\list_ui.png"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.png", true);
  else if (File::Exists(path + "\\list_ui.jpg"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.jpg", true);
  else if (File::Exists(path + "\\list_ui.jpeg"))
    list_ui_bg = CreateTextureFromPath(path + "\\list_ui.jpeg", true);

  if (!list_ui_bg)
    return false;

  m_textures[ThemeBG::BG_List_UI] = list_ui_bg;

  // Try loading carousel UI in either format
  std::shared_ptr<AbstractTexture> carousel_ui_bg;
  if (File::Exists(path + "\\carousel_ui.png"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.png", true);
  else if (File::Exists(path + "\\carousel_ui.jpg"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.jpg", true);
  else if (File::Exists(path + "\\carousel_ui.jpeg"))
    carousel_ui_bg = CreateTextureFromPath(path + "\\carousel_ui.jpeg", true);

  if (!carousel_ui_bg)
    return false;

  m_textures[ThemeBG::BG_Carousel_UI] = carousel_ui_bg;

  // Load optional theme elements with fallbacks
  SetThemeOrBackup(BG_Wii, BG_All, path + "\\carousel_background_wii");
  SetThemeOrBackup(BG_GC, BG_All, path + "\\carousel_background_gamecube");
  SetThemeOrBackup(BG_Other, BG_All, path + "\\carousel_background_other");
  SetThemeOrBackup(BG_List, BG_Menu, path + "\\menu_background_list");
  SetThemeOrBackup(BG_Netplay, BG_Menu, path + "\\menu_background_netplay");

  m_name = std::filesystem::path(path).filename().string();

  return true;
}

void CreateAudioTab(UIState* state)
{
  if (ImGui::CollapsingHeader("DSP Options"))
  {
    const char* dsp_items[] = {"HLE", "LLE Recompiler", "LLE Interpreter"};
    int dsp_idx =
        Config::Get(Config::MAIN_DSP_HLE) ? 0 : (Config::Get(Config::MAIN_DSP_JIT) ? 1 : 2);
    if (ImGui::Combo("DSP Emulation Engine", &dsp_idx, dsp_items, IM_ARRAYSIZE(dsp_items)))
    {
      Config::SetBaseOrCurrent(Config::MAIN_DSP_HLE, dsp_idx == 0);
      Config::SetBaseOrCurrent(Config::MAIN_DSP_JIT, dsp_idx == 1);
      Config::Save();
    }
    ImGui::TextWrapped("Selects how the Digital Signal Processor (DSP) is emulated. Determines how "
                       "the audio is processed and what system features are available.");
  }

  if (ImGui::CollapsingHeader("Volume"))
  {
    int volume = Config::Get(Config::MAIN_AUDIO_MUTED) ? 0 : Config::Get(Config::MAIN_AUDIO_VOLUME);
    if (ImGui::SliderInt("Volume", &volume, 0, 100))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_MUTED, volume == 0);
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_VOLUME, volume);
      Config::Save();
      AudioCommon::UpdateSoundStream(Core::System::GetInstance());
    }
  }

  if (ImGui::CollapsingHeader("Backend Settings"))
  {
    std::vector<std::string> backends = AudioCommon::GetSoundBackends();
    std::string current_backend = Config::Get(Config::MAIN_AUDIO_BACKEND);
    int backend_idx = 0;
    for (size_t i = 0; i < backends.size(); ++i)
    {
      if (backends[i] == current_backend)
      {
        backend_idx = static_cast<int>(i);
        break;
      }
    }

    if (ImGui::Combo(
            "Audio Backend", &backend_idx,
            [](void* data, int idx, const char** out_text) {
              auto* backends = static_cast<std::vector<std::string>*>(data);
              *out_text = backends->at(idx).c_str();
              return true;
            },
            &backends, static_cast<int>(backends.size())))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_BACKEND, backends[backend_idx]);
      Config::Save();
    }

#ifdef _WIN32
    if (backends[backend_idx] == BACKEND_WASAPI)
    {
      std::vector<std::string> devices = WASAPIStream::GetAvailableDevices();
      std::string current_device = Config::Get(Config::MAIN_WASAPI_DEVICE);
      int device_idx = 0;
      for (size_t i = 0; i < devices.size(); ++i)
      {
        if (devices[i] == current_device)
        {
          device_idx = static_cast<int>(i);
          break;
        }
      }

      if (ImGui::Combo(
              "Output Device", &device_idx,
              [](void* data, int idx, const char** out_text) {
                auto* devices = static_cast<std::vector<std::string>*>(data);
                *out_text = devices->at(idx).c_str();
                return true;
              },
              &devices, static_cast<int>(devices.size())))
      {
        Config::SetBaseOrCurrent(Config::MAIN_WASAPI_DEVICE, devices[device_idx]);
        Config::Save();
      }
    }
#endif

    if (AudioCommon::SupportsLatencyControl(backends[backend_idx]))
    {
      int latency = Config::Get(Config::MAIN_AUDIO_LATENCY);
      if (ImGui::SliderInt("Latency", &latency, 0, 200))
      {
        Config::SetBaseOrCurrent(Config::MAIN_AUDIO_LATENCY, latency);
        Config::Save();
      }
      ImGui::TextWrapped(
          "Sets the audio latency in milliseconds. Higher values may reduce audio crackling.");
    }
  }
#ifndef WINRT_XBOX  // TODO: Can't figure out how this one works will look into more if it's
                    // requested or is required
  if (ImGui::CollapsingHeader("Dolby Pro Logic II"))
  {
    bool dpl2 = Config::Get(Config::MAIN_DPL2_DECODER);
    if (ImGui::Checkbox("Enable Dolby Pro Logic II", &dpl2))
    {
      Config::SetBaseOrCurrent(Config::MAIN_DPL2_DECODER, dpl2);
      Config::Save();
    }
    ImGui::TextWrapped(
        "Enables Dolby Pro Logic II emulation using 5.1 surround. Certain backends only.");
    if (dpl2)
    {
      const char* quality_items[] = {"Lowest (Latency ~10 ms)", "Low (Latency ~20 ms)",
                                     "High (Latency ~40 ms)", "Highest (Latency ~80 ms)"};
      int quality_idx = static_cast<int>(Config::Get(Config::MAIN_DPL2_QUALITY));
      if (ImGui::Combo("Decoding Quality", &quality_idx, quality_items,
                       IM_ARRAYSIZE(quality_items)))
      {
        Config::SetBaseOrCurrent(Config::MAIN_DPL2_QUALITY,
                                 static_cast<AudioCommon::DPL2Quality>(quality_idx));
        Config::Save();
      }
      ImGui::TextWrapped("Adjusts the quality setting of the Dolby Pro Logic II decoder. Higher "
                         "presets increases audio latency.");
    }
  }
#endif

  if (ImGui::CollapsingHeader("Audio Playback Settings"))
  {
    int buffer_size = Config::Get(Config::MAIN_AUDIO_BUFFER_SIZE);
    if (ImGui::SliderInt("Audio Buffer Size", &buffer_size, 5, 100))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_BUFFER_SIZE, buffer_size);
      Config::Save();
    }
    ImGui::TextWrapped("Sets the size of the audio buffer in milliseconds. Higher values may "
                       "reduce audio crackling.");

    bool fill_gaps = Config::Get(Config::MAIN_AUDIO_FILL_GAPS);
    if (ImGui::Checkbox("Fill Gaps", &fill_gaps))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_FILL_GAPS, fill_gaps);
      Config::Save();
    }
    ImGui::TextWrapped("Fills gaps in audio playback to prevent audio crackling.");

    bool mute_on_speed_limit = Config::Get(Config::MAIN_AUDIO_MUTE_ON_DISABLED_SPEED_LIMIT);
    if (ImGui::Checkbox("Mute on Speed Limit", &mute_on_speed_limit))
    {
      Config::SetBaseOrCurrent(Config::MAIN_AUDIO_MUTE_ON_DISABLED_SPEED_LIMIT,
                               mute_on_speed_limit);
      Config::Save();
    }
    ImGui::TextWrapped("Mutes audio when emulation speed is above 100%.");
  }
}
void CreateAchievementsTab(UIState* state)
{
#ifdef USE_RETRO_ACHIEVEMENTS
  static bool integration_enabled = Config::Get(Config::RA_ENABLED);
  static bool hardcore_enabled = Config::Get(Config::RA_HARDCORE_ENABLED);
  static bool unofficial_enabled = Config::Get(Config::RA_UNOFFICIAL_ENABLED);
  static bool encore_enabled = Config::Get(Config::RA_ENCORE_ENABLED);
  static bool spectator_enabled = Config::Get(Config::RA_SPECTATOR_ENABLED);
  static bool discord_presence_enabled = Config::Get(Config::RA_DISCORD_PRESENCE_ENABLED);
  static bool progress_enabled = Config::Get(Config::RA_PROGRESS_ENABLED);
  static char username[256] = "";
  static char password[256] = "";
  static bool show_password = false;
  static std::string login_error;

  const bool is_running = Core::GetState(Core::System::GetInstance()) != Core::State::Uninitialized;
  const bool logged_in = !Config::Get(Config::RA_API_TOKEN).empty();

  if (ImGui::Checkbox("Enable RetroAchievements.org Integration", &integration_enabled))
  {
    Config::SetBaseOrCurrent(Config::RA_ENABLED, integration_enabled);
    Config::Save();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Enable integration with RetroAchievements for earning achievements and competing in "
        "leaderboards.\n\n"
        "Must log in with a RetroAchievements account to use. Dolphin does not save your "
        "password locally and uses an API token to maintain login.");
  }

  if (integration_enabled)
  {
    if (!logged_in)
    {
      ImGui::Text("Username");
      ImGui::SameLine();
      if (ImGui::Button("Edit Username"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      if (ImGui::InputText("##username", username, sizeof(username),
                           ImGuiInputTextFlags_EnterReturnsTrue))
      {
        UWP::HideKeyboard();
        ImGui::SetKeyboardFocusHere(-1);
      }

      ImGui::Spacing();

      ImGui::Text("Password");
      ImGui::SameLine();
      if (ImGui::Button("Edit Password"))
      {
        UWP::ShowKeyboard();
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::SameLine();
      ImGuiInputTextFlags pwd_flags =
          ImGuiInputTextFlags_EnterReturnsTrue | (show_password ? 0 : ImGuiInputTextFlags_Password);
      if (ImGui::InputText("##password", password, sizeof(password), pwd_flags))
      {
        UWP::HideKeyboard();
        show_password = !show_password;
        ImGui::SetKeyboardFocusHere(-1);
      }

      ImGui::BeginDisabled(is_running);
      if (ImGui::Button("Log In"))
      {
#ifdef _WIN32
        OutputDebugStringA(
            ("RetroAchievements: Login attempt for user: " + std::string(username) + "\n").c_str());
#endif

        if (std::string(username).empty())
        {
          login_error = "Username cannot be empty";
#ifdef _WIN32
          OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
        }
        else if (std::string(password).empty())
        {
          login_error = "Password cannot be empty";
#ifdef _WIN32
          OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
        }
        else
        {
          Config::SetBaseOrCurrent(Config::RA_USERNAME, username);

          try
          {
            std::string before_token = Config::Get(Config::RA_API_TOKEN);

            AchievementManager::GetInstance().Login(std::string(password));

            std::string after_token = Config::Get(Config::RA_API_TOKEN);

            if (after_token == before_token && after_token.empty())
            {
              login_error = "Authentication failed. Please check your credentials.";
#ifdef _WIN32
              OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
            }
            else
            {
              login_error.clear();
#ifdef _WIN32
              OutputDebugStringA("RetroAchievements: Login successful\n");
#endif
            }
          }
          catch (const std::exception& e)
          {
            login_error = std::string("Exception during login: ") + e.what();
#ifdef _WIN32
            OutputDebugStringA(("RetroAchievements Login Error: " + login_error + "\n").c_str());
#endif
          }
        }
        // Clear password from memory
        memset(password, 0, sizeof(password));
      }
      ImGui::EndDisabled();

      if (!login_error.empty())
      {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
        ImGui::Text("%s", login_error.c_str());
        ImGui::PopStyleColor();
      }
    }
    else
    {
      // Display user profile information
      auto& instance = AchievementManager::GetInstance();
      const auto* user_info = rc_client_get_user_info(instance.GetClient());

      if (user_info)
      {
        ImGui::BeginGroup();

        // User name and points
        ImGui::Text("%s", user_info->display_name);
        ImGui::Text("%u points", user_info->score);

        // User badge/icon
        const AchievementManager::Badge& player_badge = instance.GetPlayerBadge();
        if (!player_badge.data.empty())
        {
          static std::shared_ptr<AbstractTexture> profile_texture;
          static std::vector<u8> last_badge_data;

          if (last_badge_data != player_badge.data)
          {
            last_badge_data = player_badge.data;

            TextureConfig config(player_badge.width, player_badge.height, 1, 1, 1,
                                 AbstractTextureFormat::RGBA8, 0, AbstractTextureType::Texture_2D);

            profile_texture = g_gfx->CreateTexture(config, "RetroAchievements profile picture");
            if (profile_texture)
            {
              profile_texture->Load(0, player_badge.width, player_badge.height, player_badge.width,
                                    player_badge.data.data(),
                                    sizeof(u8) * 4 * player_badge.width * player_badge.height);
            }
          }

          if (profile_texture)
          {
            ImGui::Image((ImTextureID)(intptr_t)profile_texture.get(), ImVec2(64, 64));
          }
          else
          {
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + 64, ImGui::GetCursorScreenPos().y + 64),
                IM_COL32(50, 150, 255, 255));

            ImGui::Dummy(ImVec2(64, 64));
          }
        }

        ImGui::EndGroup();

        ImGui::SameLine(ImGui::GetWindowWidth() - 100);
        if (ImGui::Button("Log Out"))
        {
#ifdef _WIN32
          OutputDebugStringA("RetroAchievements: User logged out\n");
#endif
          AchievementManager::GetInstance().Logout();
          Config::SetBaseOrCurrent(Config::RA_API_TOKEN, "");
          Config::Save();
        }
      }
      else
      {
        if (ImGui::Button("Log Out"))
        {
#ifdef _WIN32
          OutputDebugStringA("RetroAchievements: User logged out\n");
#endif
          AchievementManager::GetInstance().Logout();
          Config::SetBaseOrCurrent(Config::RA_API_TOKEN, "");
          Config::Save();
        }
      }

      if (instance.IsGameLoaded())
      {
        ImGui::Separator();
        rc_client_user_game_summary_t game_summary;
        rc_client_get_user_game_summary(instance.GetClient(), &game_summary);

        ImGui::Text("%s", instance.GetGameDisplayName().data());
        ImGui::Text("Unlocked %u/%u achievements worth %u/%u points",
                    game_summary.num_unlocked_achievements, game_summary.num_core_achievements,
                    game_summary.points_unlocked, game_summary.points_core);

        float progress =
            game_summary.num_core_achievements > 0 ?
                (float)game_summary.num_unlocked_achievements / game_summary.num_core_achievements :
                0.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 0),
                           (std::to_string(game_summary.num_unlocked_achievements) + "/" +
                            std::to_string(game_summary.num_core_achievements))
                               .c_str());

        const auto& rich_presence = instance.GetRichPresence();
        if (rich_presence[0] != '\0')
        {
          ImGui::TextWrapped("%s", rich_presence.data());
        }
      }
    }
    ImGui::Separator();
    ImGui::Text("Function Settings");

    ImGui::BeginDisabled(!logged_in || (is_running && !hardcore_enabled));
    if (ImGui::Checkbox("Enable Hardcore Mode", &hardcore_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_HARDCORE_ENABLED, hardcore_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable Hardcore Mode on RetroAchievements.\n\n"
          "Hardcore Mode is intended to provide an experience as close to gaming on the original "
          "hardware as possible. RetroAchievements rankings are primarily oriented towards "
          "Hardcore "
          "points (Softcore points are tracked but not as heavily emphasized) and leaderboards "
          "require Hardcore Mode to be on.\n\n"
          "To ensure this experience, the following features will be disabled:\n"
          "- Loading states (saving states is allowed)\n"
          "- Emulator speeds below 100% (frame advance disabled, turbo allowed)\n"
          "- Cheats\n"
          "- Memory patches (file patches allowed)\n"
          "- Debug UI\n"
          "- Freelook\n\n"
          "This cannot be turned on while a game is playing.\n"
          "Close your current game before enabling.");
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!logged_in);
    if (ImGui::Checkbox("Enable Unofficial Achievements", &unofficial_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_UNOFFICIAL_ENABLED, unofficial_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking unofficial achievements as well as official achievements.\n\n"
          "Unofficial achievements may be optional or unfinished achievements that have not been "
          "deemed official by RetroAchievements and may be useful for testing or simply for "
          "fun.\n\n"
          "Setting takes effect on next game load.");
    }

    if (ImGui::Checkbox("Enable Encore Mode", &encore_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_ENCORE_ENABLED, encore_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking achievements in Encore Mode.\n\n"
          "Encore Mode re-enables achievements the player has already unlocked on the site so that "
          "the player will be notified if they meet the unlock conditions again, useful for custom "
          "speedrun criteria or simply for fun.\n\n"
          "Setting takes effect on next game load.");
    }

    if (ImGui::Checkbox("Enable Spectator Mode", &spectator_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_SPECTATOR_ENABLED, spectator_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable unlocking achievements in Spectator Mode.\n\n"
          "While in Spectator Mode, achievements and leaderboards will be processed and displayed "
          "on screen, but will not be submitted to the server.\n\n"
          "If this is on at game launch, it will not be turned off until game close, because a "
          "RetroAchievements session will not be created.\n\n"
          "If this is off at game launch, it can be toggled freely while the game is running.");
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::Text("Display Settings");

#ifndef WINRT_XBOX  // We cannot use Discord RPC on Xbox as you cannot launch the Discord Win32 app.
#ifdef USE_DISCORD_PRESENCE
    ImGui::BeginDisabled(!Config::Get(Config::MAIN_USE_DISCORD_PRESENCE));
    if (ImGui::Checkbox("Enable Discord Presence", &discord_presence_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_DISCORD_PRESENCE_ENABLED, discord_presence_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Use RetroAchievements rich presence in your Discord status.\n\n"
                        "Show Current Game on Discord must be enabled.");
    }
    ImGui::EndDisabled();
#endif
#endif

    if (ImGui::Checkbox("Enable Progress Notifications", &progress_enabled))
    {
      Config::SetBaseOrCurrent(Config::RA_PROGRESS_ENABLED, progress_enabled);
      Config::Save();
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "Enable progress notifications on achievements.\n\n"
          "Displays a brief popup message whenever the player makes progress on an achievement "
          "that tracks an accumulated value, such as 60 out of 120 stars.");
    }
  }
#else
  ImGui::TextWrapped("RetroAchievements support is not enabled in this build.");
#endif
}

void DrawAchievementsWindow(UIState* state)
{
#ifdef USE_RETRO_ACHIEVEMENTS
  auto& instance = AchievementManager::GetInstance();
  if (!instance.IsGameLoaded() || !Config::Get(Config::RA_ENABLED))
    return;

  if (!ImGui::Begin("Achievements", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::End();
    return;
  }

  auto* client = instance.GetClient();
  if (!client)
    return;

  rc_client_achievement_list_t* achievement_list = rc_client_create_achievement_list(
      client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE | RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL,
      RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

  if (achievement_list)
  {
    for (u32 bucket_idx = 0; bucket_idx < achievement_list->num_buckets; bucket_idx++)
    {
      auto& bucket = achievement_list->buckets[bucket_idx];
      if (ImGui::CollapsingHeader(bucket.label))
      {
        for (u32 ach_idx = 0; ach_idx < bucket.num_achievements; ach_idx++)
        {
          auto* achievement = bucket.achievements[ach_idx];

          const auto& badge = instance.GetAchievementBadge(achievement->id, !achievement->unlocked);
          ImGui::PushID(achievement->id);

          if (state->achievement_badges.find(achievement->id) == state->achievement_badges.end())
          {
            TextureConfig config(badge.width, badge.height, 1, 1, 1, AbstractTextureFormat::RGBA8,
                                 0, AbstractTextureType::Texture_2D);

            auto tex = g_gfx->CreateTexture(config);
            if (tex)
            {
              tex->Load(0, badge.width, badge.height, badge.width, badge.data.data(),
                        badge.width * badge.height * sizeof(u32));
              state->achievement_badges[achievement->id] = std::move(tex);
            }
          }

          ImVec4 border_color;
          if ((achievement->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE) != 0)
            border_color = ImVec4(1.0f, 0.843f, 0.0f, 1.0f);
          else if ((achievement->unlocked & RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE) != 0)
            border_color = ImVec4(0.0f, 0.478f, 1.0f, 1.0f);
          else
            border_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

          const float badge_size = 64.0f;
          const float border_thickness = 4.0f;
          ImVec2 pos = ImGui::GetCursorScreenPos();
          ImGui::GetWindowDrawList()->AddRectFilled(
              ImVec2(pos.x, pos.y),
              ImVec2(pos.x + badge_size + border_thickness * 2,
                     pos.y + badge_size + border_thickness * 2),
              ImGui::ColorConvertFloat4ToU32(border_color), 0.0f, 0);

          ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + border_thickness,
                                     ImGui::GetCursorPos().y + border_thickness));
          ImGui::Image((ImTextureID)(intptr_t)state->achievement_badges[achievement->id].get(),
                       ImVec2(badge_size, badge_size));

          ImGui::SameLine();
          ImGui::BeginGroup();

          ImGui::TextWrapped("%s", achievement->title);
          ImGui::TextWrapped("%s", achievement->description);
          ImGui::Text("%d points", achievement->points);
          if (achievement->unlocked)
          {
            if (achievement->unlock_time != 0)
            {
              time_t time = achievement->unlock_time;
              struct tm* timeinfo = localtime(&time);
              char buffer[80];
              strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
              ImGui::Text("Unlocked at %s", buffer);
            }
            else
            {
              ImGui::Text("Unlocked");
            }
          }
          else
          {
            ImGui::Text("Locked");
          }

          if (achievement->measured_percent > 0.000)
          {
            ImGui::ProgressBar(achievement->unlocked ? 1.0f :
                                                       achievement->measured_percent / 100.0f,
                               ImVec2(-1, 0), achievement->measured_progress);
          }

          ImGui::EndGroup();
          ImGui::PopID();
          ImGui::Separator();
        }
      }
    }
    rc_client_destroy_achievement_list(achievement_list);
  }

  ImGui::End();
#endif
}

}  // namespace ImGuiFrontend
