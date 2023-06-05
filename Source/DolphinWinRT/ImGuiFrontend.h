#pragma once

#include "Core/TitleDatabase.h"
#include "VideoCommon/AbstractTexture.h"

namespace UICommon
{
class GameFile;
}

class WGDevice;

namespace ImGuiFrontend
{
class UIState;

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

  void LoadGameList();
  void RecurseFolder(std::string path);
  void AddGameFolder(std::string path);

  AbstractTexture* GetOrCreateBackgroundTex(bool list_view);
  AbstractTexture* GetOrCreateMissingTex();
  AbstractTexture* GetHandleForGame(std::shared_ptr<UICommon::GameFile> game);
  std::unique_ptr<AbstractTexture> CreateCoverTexture(std::shared_ptr<UICommon::GameFile> game);
};
}  // namespace ImGuiFrontend
