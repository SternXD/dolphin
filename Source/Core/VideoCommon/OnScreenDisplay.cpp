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

#include "Core/AchievementManager.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/State.h"
#include "rcheevos/include/rc_api_runtime.h"

#ifdef WINRT_XBOX
#include <gamingdeviceinformation.h>
#include <windows.applicationmodel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/windows.gaming.input.h>
#include <winrt/windows.graphics.display.core.h>

#include "DolphinWinRT/Host.h"
#include "DolphinWinRT/UWPUtils.h"

using winrt::Windows::UI::Core::CoreWindow;
using namespace winrt;
#endif

#include "Common/WindowSystemInfo.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/Assets/CustomTextureData.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

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

void DrawMessages()
{
  const bool draw_messages = Config::Get(Config::MAIN_OSD_MESSAGES);
  const float current_x =
      LEFT_MARGIN * ImGui::GetIO().DisplayFramebufferScale.x + s_obscured_pixels_left;
  float current_y = TOP_MARGIN * ImGui::GetIO().DisplayFramebufferScale.y + s_obscured_pixels_top;
  int index = 0;

  std::lock_guard lock{s_messages_mutex};

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
        ImGui::TextWrapped(
            "Warning: Savestates can be buggy with Dual Core enabled, do not rely on them "
            "or you may risk losing progress.");
        for (int i = 0; i < 5; i++)
        {
          if (ImGui::BeginChild(std::format("savestate-{}", i).c_str(),
                                ImVec2(-1, 75 * frame_scale), true))
          {
            ImGui::Text("Port %d - %s", i, State::GetInfoStringOfSlot(i).c_str());

            if (ImGui::Button(std::format("Load State in Port {}", i).c_str()))
            {
              Core::RunOnCPUThread(
                  Core::System::GetInstance(),
                  [i] {
                    s_show_menu = false;
                    auto& system = Core::System::GetInstance();
                    Core::SetState(system, Core::State::Running);
                    State::Load(system, i);
                  },
                  false);
            }

            if (ImGui::Button(std::format("Save State in Port {}", i).c_str()))
            {
              Core::RunOnCPUThread(
                  Core::System::GetInstance(),
                  [i] {
                    s_show_menu = false;
                    auto& system = Core::System::GetInstance();
                    Core::SetState(system, Core::State::Running);
                    State::Save(system, i);
                  },
                  false);
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

      if (ImGui::BeginTabItem("Achievements"))
      {
        static int view = 0;
        ImGui::RadioButton("Achievement List", &view, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Leaderboards", &view, 1);
        if (view == 0)
        {
          auto& mgr = AchievementManager::GetInstance();
          if (!mgr.IsGameLoaded())
          {
            ImGui::TextUnformatted("No game loaded.");
          }
          else
          {
            auto* client = mgr.GetClient();
            auto* list = rc_client_create_achievement_list(
                client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
                RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
            if (list)
            {
              ImGui::BeginChild("##AchievementsScroll", ImVec2(0, 0), true);
              static std::map<u32, std::unique_ptr<AbstractTexture>> badge_textures;
              for (u32 b = 0; b < list->num_buckets; ++b)
              {
                const auto& bucket = list->buckets[b];
                ImGui::TextUnformatted(bucket.label);
                ImGui::Indent();

                ImGui::TextUnformatted("Unlocked");
                ImGui::Indent();
                for (u32 a = 0; a < bucket.num_achievements; ++a)
                {
                  const auto* ach = bucket.achievements[a];
                  if (ach->unlocked)
                  {
                    const auto& slice = mgr.GetAchievementBadge(ach->id, ach->unlocked == 0);
                    auto it_tex = badge_textures.find(ach->id);
                    if (it_tex == badge_textures.end())
                    {
                      TextureConfig cfg(slice.width, slice.height, 1, 1, 1,
                                        AbstractTextureFormat::RGBA8, 0,
                                        AbstractTextureType::Texture_2DArray);
                      auto tex = g_gfx->CreateTexture(cfg);
                      if (tex)
                        tex->Load(0, slice.width, slice.height, slice.width, slice.data.data(),
                                  slice.width * slice.height * sizeof(u32));
                      badge_textures[ach->id] = std::move(tex);
                      it_tex = badge_textures.find(ach->id);
                    }
                    ImGui::BeginGroup();
                    if (it_tex != badge_textures.end() && it_tex->second)
                      ImGui::Image(*it_tex->second.get(), ImVec2(32, 32));
                    ImGui::SameLine();
                    ImGui::Text("%s (%u pts)", ach->title, ach->points);
                    if (ach->measured_percent > 0.0f)
                      ImGui::ProgressBar(ach->measured_percent * 0.01f, ImVec2(-1, 0),
                                         ach->measured_progress);
                    ImGui::EndGroup();
                    ImGui::Separator();
                  }
                }
                ImGui::Unindent();

                ImGui::TextUnformatted("Locked");
                ImGui::Indent();
                for (u32 a = 0; a < bucket.num_achievements; ++a)
                {
                  const auto* ach = bucket.achievements[a];
                  if (!ach->unlocked)
                  {
                    const auto& slice = mgr.GetAchievementBadge(ach->id, ach->unlocked == 0);
                    auto it_tex = badge_textures.find(ach->id);
                    if (it_tex == badge_textures.end())
                    {
                      TextureConfig cfg(slice.width, slice.height, 1, 1, 1,
                                        AbstractTextureFormat::RGBA8, 0,
                                        AbstractTextureType::Texture_2DArray);
                      auto tex = g_gfx->CreateTexture(cfg);
                      if (tex)
                        tex->Load(0, slice.width, slice.height, slice.width, slice.data.data(),
                                  slice.width * slice.height * sizeof(u32));
                      badge_textures[ach->id] = std::move(tex);
                      it_tex = badge_textures.find(ach->id);
                    }
                    ImGui::BeginGroup();
                    if (it_tex != badge_textures.end() && it_tex->second)
                      ImGui::Image(*it_tex->second.get(), ImVec2(32, 32));
                    ImGui::SameLine();
                    ImGui::Text("%s (%u pts)", ach->title, ach->points);
                    if (ach->measured_percent > 0.0f)
                      ImGui::ProgressBar(ach->measured_percent * 0.01f, ImVec2(-1, 0),
                                         ach->measured_progress);
                    ImGui::EndGroup();
                    ImGui::Separator();
                  }
                }
                ImGui::Unindent();
                ImGui::Unindent();
              }
              ImGui::EndChild();
              rc_client_destroy_achievement_list(list);
            }
          }
        }
        else
        {
          auto& mgr = AchievementManager::GetInstance();
          if (!mgr.IsGameLoaded())
          {
            ImGui::TextUnformatted("No game loaded.");
          }
          else
          {
            auto* client = mgr.GetClient();
            auto* lbs = rc_client_create_leaderboard_list(
                client, RC_CLIENT_LEADERBOARD_LIST_GROUPING_TRACKING);
            if (lbs)
            {
              ImGui::BeginChild("##LeaderboardsScroll", ImVec2(0, 0), true);
              for (u32 b = 0; b < lbs->num_buckets; ++b)
              {
                const auto& bucket = lbs->buckets[b];
                ImGui::TextUnformatted(bucket.label);
                ImGui::Indent();
                for (u32 i = 0; i < bucket.num_leaderboards; ++i)
                {
                  const auto* board = bucket.leaderboards[i];
                  ImGui::BeginGroup();
                  ImGui::Text("%s: %s", board->title,
                              board->tracker_value ? board->tracker_value : "");
                  ImGui::TextWrapped(board->description);
                  ImGui::EndGroup();
                  ImGui::Separator();
                }
                ImGui::Unindent();
              }
              ImGui::EndChild();
              rc_client_destroy_leaderboard_list(lbs);
            }
          }
        }
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
