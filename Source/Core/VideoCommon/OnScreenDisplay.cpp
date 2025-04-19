// Copyright 2009 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/OnScreenDisplay.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>

#include <fmt/format.h>
#include <imgui.h>

#include "UICommon/ImGuiMenu/ImGuiFrontend.h"

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/Timer.h"

#include "Core/Config/MainSettings.h"
#include "Core/State.h"
#include <Core/AchievementManager.h>
#include <Core/Config/AchievementSettings.h>

#ifdef WINRT_XBOX
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/windows.graphics.display.core.h>
#include <winrt/windows.gaming.input.h>
#include <windows.applicationmodel.h>
#include <gamingdeviceinformation.h>

#include "DolphinWinRT/Host.h"
#include "DolphinWinRT/UWPUtils.h"

using winrt::Windows::UI::Core::CoreWindow;
using namespace winrt;
#endif

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomTextureData.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/VideoBackendBase.h"
#include "Common/WindowSystemInfo.h"

namespace OSD
{
constexpr float LEFT_MARGIN = 10.0f;         // Pixels to the left of OSD messages.
constexpr float TOP_MARGIN = 10.0f;          // Pixels above the first OSD message.
constexpr float WINDOW_PADDING = 4.0f;       // Pixels between subsequent OSD messages.
constexpr float MESSAGE_FADE_TIME = 1000.f;  // Ms to fade OSD messages at the end of their life.
constexpr float MESSAGE_DROP_TIME = 5000.f;  // Ms to drop OSD messages that has yet to ever render.

static std::atomic<int> s_obscured_pixels_left = 0;
static std::atomic<int> s_obscured_pixels_top = 0;
static ImGuiFrontend::UIState s_setting_state{};
static bool s_show_menu;

struct Message
{
  Message() = default;
  Message(std::string text_, u32 duration_, u32 color_,
          const VideoCommon::CustomTextureData::ArraySlice::Level* icon_ = nullptr)
      : text(std::move(text_)), duration(duration_), color(color_), icon(icon_)
  {
    timer.Start();
  }
  s64 TimeRemaining() const { return duration - timer.ElapsedMs(); }
  std::string text;
  Common::Timer timer;
  u32 duration = 0;
  bool ever_drawn = false;
  bool should_discard = false;
  u32 color = 0;
  const VideoCommon::CustomTextureData::ArraySlice::Level* icon;
  std::unique_ptr<AbstractTexture> texture;
};
static std::multimap<MessageType, Message> s_messages;
static std::mutex s_messages_mutex;

static ImVec4 ARGBToImVec4(const u32 argb)
{
  return ImVec4(static_cast<float>((argb >> 16) & 0xFF) / 255.0f,
                static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                static_cast<float>((argb >> 0) & 0xFF) / 255.0f,
                static_cast<float>((argb >> 24) & 0xFF) / 255.0f);
}

static float DrawMessage(int index, Message& msg, const ImVec2& position, int time_left)
{
  // We have to provide a window name, and these shouldn't be duplicated.
  // So instead, we generate a name based on the number of messages drawn.
  const std::string window_name = fmt::format("osd_{}", index);

  // The size must be reset, otherwise the length of old messages could influence new ones.
  ImGui::SetNextWindowPos(position);
  ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));

  // Gradually fade old messages away (except in their first frame)
  const float fade_time = std::max(std::min(MESSAGE_FADE_TIME, (float)msg.duration), 1.f);
  const float alpha = std::clamp(time_left / fade_time, 0.f, 1.f);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, msg.ever_drawn ? alpha : 1.0);

  float window_height = 0.0f;
  if (ImGui::Begin(window_name.c_str(), nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
                       ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
  {
    if (msg.icon)
    {
      if (!msg.texture)
      {
        const u32 width = msg.icon->width;
        const u32 height = msg.icon->height;
        TextureConfig tex_config(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                                 AbstractTextureType::Texture_2DArray);
        msg.texture = g_gfx->CreateTexture(tex_config);
        if (msg.texture)
        {
          msg.texture->Load(0, width, height, width, msg.icon->data.data(),
                            sizeof(u32) * width * height);
        }
        else
        {
          // don't try again next time
          msg.icon = nullptr;
        }
      }

      if (msg.texture)
      {
        ImGui::Image(*msg.texture.get(), ImVec2(static_cast<float>(msg.icon->width),
                                                static_cast<float>(msg.icon->height)));
        ImGui::SameLine();
      }
    }

    // Use %s in case message contains %.
    if (msg.text.size() > 0)
      ImGui::TextColored(ARGBToImVec4(msg.color), "%s", msg.text.c_str());
    window_height =
        ImGui::GetWindowSize().y + (WINDOW_PADDING * ImGui::GetIO().DisplayFramebufferScale.y);
  }

  ImGui::End();
  ImGui::PopStyleVar();

  msg.ever_drawn = true;

  return window_height;
}

void AddTypedMessage(MessageType type, std::string message, u32 ms, u32 argb,
                     const VideoCommon::CustomTextureData::ArraySlice::Level* icon)
{
  std::lock_guard lock{s_messages_mutex};

  // A message may hold a reference to a texture that can only be destroyed on the video thread, so
  // only mark the old typed message (if any) for removal. It will be discarded on the next call to
  // DrawMessages().
  auto range = s_messages.equal_range(type);
  for (auto it = range.first; it != range.second; ++it)
    it->second.should_discard = true;

  s_messages.emplace(type, Message(std::move(message), ms, argb, std::move(icon)));
}

void AddMessage(std::string message, u32 ms, u32 argb,
                const VideoCommon::CustomTextureData::ArraySlice::Level* icon)
{
  std::lock_guard lock{s_messages_mutex};
  s_messages.emplace(MessageType::Typeless, Message(std::move(message), ms, argb, std::move(icon)));
}

#ifdef USE_RETRO_ACHIEVEMENTS
void DrawAchievementsOverlay()
{
  auto& instance = AchievementManager::GetInstance();
  if (!instance.IsGameLoaded() || !Config::Get(Config::RA_ENABLED))
    return;

  // Position in top-right corner
  const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
  const float margin = 10.0f * scale;
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - margin, margin), ImGuiCond_Always,
                         ImVec2(1.0f, 0.0f));

  if (!ImGui::Begin("##AchievementsOverlay", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                        ImGuiWindowFlags_NoNav))
  {
    ImGui::End();
    return;
  }

  auto* client = instance.GetClient();
  auto* achievement_list = rc_client_create_achievement_list(
      client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
      RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);

  if (achievement_list)
  {
    bool has_progress = false;
    for (u32 bucket_idx = 0; bucket_idx < achievement_list->num_buckets; bucket_idx++)
    {
      auto& bucket = achievement_list->buckets[bucket_idx];
      for (u32 ach_idx = 0; ach_idx < bucket.num_achievements; ach_idx++)
      {
        auto* achievement = bucket.achievements[ach_idx];
        if (!achievement->unlocked && achievement->measured_percent > 0.0f)
        {
          has_progress = true;
          
          // Get badge image
          const auto& badge = instance.GetAchievementBadge(achievement->id, !achievement->unlocked);
          ImGui::PushID(achievement->id);

          // Create badge texture if needed
          if (s_setting_state.achievement_badges.find(achievement->id) == s_setting_state.achievement_badges.end())
          {
            TextureConfig tex_config(badge.width, badge.height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                                   AbstractTextureType::Texture_2D);
            auto texture = g_gfx->CreateTexture(tex_config);
            if (texture)
            {
              texture->Load(0, badge.width, badge.height, badge.width, badge.data.data(),
                          sizeof(u32) * badge.width * badge.height);
              s_setting_state.achievement_badges[achievement->id] = std::move(texture);
            }
          }

          // Draw badge with gray border
          const float badge_size = 32.0f * scale;  // Smaller badges for overlay
          const float border_thickness = 2.0f * scale;
          ImVec2 pos = ImGui::GetCursorScreenPos();
          ImGui::GetWindowDrawList()->AddRect(
              pos,
              ImVec2(pos.x + badge_size + border_thickness * 2,
                     pos.y + badge_size + border_thickness * 2),
              ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f)),
              0.0f, 0, border_thickness);

          ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPos().x + border_thickness,
                                    ImGui::GetCursorPos().y + border_thickness));
          if (auto it = s_setting_state.achievement_badges.find(achievement->id);
              it != s_setting_state.achievement_badges.end())
          {
            ImGui::Image((ImTextureID)(intptr_t)it->second.get(), ImVec2(badge_size, badge_size));
          }

          ImGui::SameLine();
          ImGui::BeginGroup();
          ImGui::TextWrapped("%s", achievement->title);
          ImGui::ProgressBar(achievement->measured_percent / 100.0f,
                           ImVec2(-1, 0), achievement->measured_progress);
          ImGui::EndGroup();

          ImGui::PopID();
          ImGui::Separator();
        }
      }
    }
    
    if (!has_progress)
    {
      ImGui::End();
      rc_client_destroy_achievement_list(achievement_list);
      return;
    }

    rc_client_destroy_achievement_list(achievement_list);
  }

  ImGui::End();
}
#endif

void DrawMessages()
{
  const bool draw_messages = Config::Get(Config::MAIN_OSD_MESSAGES);
  const float current_x =
      LEFT_MARGIN * ImGui::GetIO().DisplayFramebufferScale.x + s_obscured_pixels_left;
  float current_y = TOP_MARGIN * ImGui::GetIO().DisplayFramebufferScale.y + s_obscured_pixels_top;
  int index = 0;

  std::lock_guard lock{s_messages_mutex};

#ifdef USE_RETRO_ACHIEVEMENTS
  DrawAchievementsOverlay();
#endif

  for (auto it = s_messages.begin(); it != s_messages.end();)
  {
    Message& msg = it->second;
    if (msg.should_discard)
    {
      it = s_messages.erase(it);
      continue;
    }

    const s64 time_left = msg.TimeRemaining();

    // Make sure we draw them at least once if they were printed with 0ms,
    // unless enough time has expired, in that case, we drop them
    if (time_left <= 0 && (msg.ever_drawn || -time_left >= MESSAGE_DROP_TIME))
    {
      it = s_messages.erase(it);
      continue;
    }
    else
    {
      ++it;
    }

    if (draw_messages)
      current_y += DrawMessage(index++, msg, ImVec2(current_x, current_y), time_left);
  }
}

void ClearMessages()
{
  std::lock_guard lock{s_messages_mutex};
  s_messages.clear();
}

void DrawInGameMenu()
{
  if (!s_show_menu)
    return;

  float frame_scale = ImGui::GetIO().DisplayFramebufferScale.x;
  ImGui::SetNextWindowSize(ImVec2(540 * frame_scale, 425 * frame_scale));
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2 - (540 / 2) * frame_scale,
                                 ImGui::GetIO().DisplaySize.y / 2 - (425 / 2) * frame_scale));
  if (ImGui::Begin("Pause Menu", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                       ImGuiWindowFlags_NoScrollbar))
  {
    if (ImGui::BeginTabBar("InGameTabs"))
    {
#ifdef WINRT_XBOX
      // todo - make the host handle this!!
      if (ImGui::BeginTabItem("General"))
      {
        if (ImGui::Button("Change Disc"))
        {
          UWP::PickDisc();
        }

        if (ImGui::Button("Exit Game"))
        {
          if (!UWP::g_tried_graceful_shutdown.TestAndClear())
          {
            // First hide the menu and clear any OSD messages
            s_show_menu = false;
            ClearMessages();

            // Wait for any pending GPU operations to complete
            if (g_gfx)
            {
              g_gfx->WaitForGPUIdle();
            }

            // Stop the core first to prevent event processing
            Core::Stop(Core::System::GetInstance());
            Core::Shutdown(Core::System::GetInstance());

            // Request shutdown but DO NOT force a frontend reset immediately
            UWP::g_shutdown_requested.Set();

            // If we want to return to the frontend later, we can set this after proper cleanup
            // UWP::g_needs_frontend_reset.Set();
          }
          else
          {
            // If we've already tried graceful shutdown once, force exit
            if (g_gfx)
            {
              g_gfx->WaitForGPUIdle();
            }
            Core::Stop(Core::System::GetInstance());
            Core::Shutdown(Core::System::GetInstance());
            exit(0);
          }
        }

        ImGui::EndTabItem();
      }
#endif

      if (ImGui::BeginTabItem("Save States"))
      {
        ImGui::TextWrapped("Warning: Savestates can be buggy with Dual Core enabled, do not rely on them "
                    "or you may risk losing progress.");
        for (int i = 0; i < 5; i++)
        {
          if (ImGui::BeginChild(std::format("savestate-{}", i).c_str(), ImVec2(-1, 75 * frame_scale), true))
          {
            ImGui::Text("Port %d - %s", i, State::GetInfoStringOfSlot(i).c_str());

            if (ImGui::Button(std::format("Load State in Port {}", i).c_str()))
            {
              Core::RunOnCPUThread(Core::System::GetInstance(), [i] {
                s_show_menu = false;
                auto& system = Core::System::GetInstance();
                Core::SetState(system, Core::State::Running);
                State::Load(system, i);
              }, false);
            }

            if (ImGui::Button(std::format("Save State in Port {}", i).c_str()))
            {
              Core::RunOnCPUThread(Core::System::GetInstance(), [i] {
                s_show_menu = false;
                auto& system = Core::System::GetInstance();
                Core::SetState(system, Core::State::Running);
                State::Save(system, i);
              }, false);
            }
          }

          ImGui::EndChild();
        }

        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Options"))
      {
        ImGuiFrontend::DrawSettingsMenu(&s_setting_state, frame_scale);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Netplay"))
      {
        if (g_netplay_client != nullptr)
        {
          ImGuiFrontend::DrawLobbyMenu();
        }
        else
        {
          ImGui::Text("You are not currently in any Netplay lobby.");
        }

        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

void SetObscuredPixelsLeft(int width)
{
  s_obscured_pixels_left = width;
}

void SetObscuredPixelsTop(int height)
{
  s_obscured_pixels_top = height;
}

void ToggleShowSettings()
{
  s_show_menu = !s_show_menu;
}
}  // namespace OSD
