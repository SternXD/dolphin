#pragma once

#include "Core/TitleDatabase.h"
#include "VideoCommon/AbstractTexture.h"
#include "InputCommon/ControllerInterface/CoreDevice.h"

namespace UICommon
{
class GameFile;
}

class WGDevice;

namespace ImGuiFrontend
{
enum SelectedTab
{
  General,
  Interface,
  Graphics,
  Controls,
  Paths,
  Wii,
  GC,
  Advanced,
  About
};

enum ThemeBG
{
  BG_All = 1,
  BG_Wii,
  BG_GC,
  BG_Other,
  BG_Menu,
  BG_List,
  BG_Netplay,
  BG_List_UI,
  BG_Carousel_UI,
  BG_COUNT
};

enum CarouselCategory
{
  CAll = 1,
  CWii = 2,
  CGC = 3,
  COther = 4,
  CCount
};

class UIState
{
public:
  bool controlsDisabled = false;
  bool showSettingsWindow = false;
  bool showListView = false;
  bool menuPressed = false;
  std::string selectedPath;
  SelectedTab selectedTab = General;
  ThemeBG currentBG = BG_All;
  CarouselCategory carouselCat = CAll;
};

class FrontendResult
{
public:
  std::shared_ptr<UICommon::GameFile> game_result;
  bool netplay;

  FrontendResult() {
    game_result = nullptr;
    netplay = false;
  }

  FrontendResult(std::shared_ptr<UICommon::GameFile> game)
  {
    game_result = game;
    netplay = false;
  }
};

class FrontendTheme
{
public:
  std::shared_ptr<AbstractTexture> GetBackground(ThemeBG cat);
  bool TryLoad(std::string path);
  std::string GetName() { return m_name; }

private:
  std::shared_ptr<AbstractTexture> m_textures[ThemeBG::BG_COUNT];
  std::string m_name;
};

class ImGuiFrontend
{
public:
  ImGuiFrontend();
  FrontendResult RunUntilSelection();

  Core::TitleDatabase m_title_database;
  std::mutex m_frontend_mutex;

private:
  void PopulateControls();
  void RefreshControls(bool updateGameSelection);

  FrontendResult RunMainLoop();
  FrontendResult CreateMainPage();
  FrontendResult CreateListPage();
  std::shared_ptr<UICommon::GameFile> CreateGameCarousel();
  std::shared_ptr<UICommon::GameFile> CreateGameList();

  void LoadGameList();
  void FilterGamesForCategory();
  void LoadThemes();
  void RecurseForThemes(std::string path);
  void RecurseFolderForGames(std::string path);
  void AddGameFolder(std::string path);
  bool TryInput(std::string expression, std::shared_ptr<ciface::Core::Device> device);

  AbstractTexture* GetOrCreateMissingTex();
  AbstractTexture* GetHandleForGame(std::shared_ptr<UICommon::GameFile> game);
  std::shared_ptr<AbstractTexture> CreateCoverTexture(std::shared_ptr<UICommon::GameFile> game);

  std::vector<std::shared_ptr<ciface::Core::Device>> m_controllers;
  std::vector<std::shared_ptr<UICommon::GameFile>> m_games;
  std::unordered_map<std::string, std::shared_ptr<AbstractTexture>> m_cover_textures;
  u64 m_imgui_last_frame_time;

  std::unique_ptr<AbstractTexture> m_background_tex, m_background_list_tex;
  std::shared_ptr<AbstractTexture> m_missing_tex;

  bool m_direction_pressed = false;
  std::chrono::high_resolution_clock::time_point m_scroll_last =
      std::chrono::high_resolution_clock::now();

  std::chrono::high_resolution_clock::time_point m_time_since_init =
      std::chrono::high_resolution_clock::now();
  
  std::vector<std::shared_ptr<UICommon::GameFile>> m_displayed_games;
  CarouselCategory m_last_category = CarouselCategory::CCount; // Default to CCount to trigger an update
  std::string m_prev_list_search;
  std::vector<std::shared_ptr<UICommon::GameFile>> m_list_search_results;
  char m_list_search_buf[32] {};
};

void DrawSettingsMenu(UIState* state, float frame_scale);
void CreateGeneralTab(UIState* state);
void CreateInterfaceTab(UIState* state);
void CreateGraphicsTab(UIState* state);
void CreateControlsTab(UIState* state);
void CreateGameCubeTab(UIState* state);
void CreateWiiTab(UIState* state);
void CreateAdvancedTab(UIState* state);
void CreatePathsTab(UIState* state);
void CreateWiiPort(int index, std::vector<std::string> devices);
void CreateGCPort(int index, std::vector<std::string> devices);

std::shared_ptr<AbstractTexture> CreateTextureFromPath(std::string path);
}  // namespace ImGuiFrontend
