// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/Version.h"

#include <string>

#include "Common/scmrev.h"

#ifdef WINRT_XBOX
#include <objbase.h>
#include <windows.applicationmodel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/windows.gaming.input.h>
#include <winrt/windows.graphics.display.core.h>
#include <gamingdeviceinformation.h>
using namespace winrt::Windows::ApplicationModel;
#endif

namespace Common
{
#define EMULATOR_NAME "DolphinUWP"

#ifdef _DEBUG
#define BUILD_TYPE_STR "Debug "
#elif defined DEBUGFAST
#define BUILD_TYPE_STR "DebugFast "
#else
#define BUILD_TYPE_STR ""
#endif

#ifdef WINRT_XBOX

std::string GetConsoleModelString()
{
  std::string model;

  // Get the Xbox model information
  GAMING_DEVICE_MODEL_INFORMATION deviceInfo = {};

  HRESULT hr = GetGamingDeviceModelInformation(&deviceInfo);

  if (SUCCEEDED(hr))
  {
    switch (deviceInfo.deviceId)
    {
    case GAMING_DEVICE_DEVICE_ID_XBOX_ONE:
      model = "Xbox One";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_S:
      model = "Xbox One S";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_X:
      model = "Xbox One X";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_X_DEVKIT:
      model = "Xbox One X Developer Kit";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_S:
      model = "Xbox Series S";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X:
      model = "Xbox Series X";
      break;
    case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X_DEVKIT:
      model = "Xbox Series X Developer Kit";
      break;
    default:
      model = "Unknown Xbox model";
      break;
    }
  }
  else
  {
    model = " Error detecting Xbox model";
  }

  return model;
}

void GetConsoleModelString(std::string& out_model)
{
  out_model = GetConsoleModelString();
}

#endif

const std::string& GetScmRevStr()
{
  static const std::string scm_rev_str = EMULATOR_NAME " "
  // Note this macro can be empty if the master branch does not exist.
#if 1 - SCM_COMMITS_AHEAD_MASTER - 1 != 0
                                                       "[" SCM_BRANCH_STR "] "
#endif

#ifdef __INTEL_COMPILER
      BUILD_TYPE_STR SCM_DESC_STR "-ICC";
#else
      BUILD_TYPE_STR SCM_DESC_STR;
#endif
  return scm_rev_str;
}

const std::string& GetScmRevGitStr()
{
  static const std::string scm_rev_git_str = "1ae0b232656ccb47b0843b44fae0e4f47a6a2778";
  return scm_rev_git_str;
}

const std::string& GetScmDescStr()
{
  static const std::string scm_desc_str = SCM_DESC_STR;
  return scm_desc_str;
}

const std::string& GetScmBranchStr()
{
  static const std::string scm_branch_str = SCM_BRANCH_STR;
  return scm_branch_str;
}

const std::string& GetUserAgentStr()
{
  static const std::string user_agent_str = []() {
    std::string version;
#ifdef WINRT_XBOX
    // Read full Package version from AppxManifest
    auto pkg = winrt::Windows::ApplicationModel::Package::Current();
    auto ver = pkg.Id().Version();
    version = std::to_string(ver.Major) + "." + std::to_string(ver.Minor) + "." +
              std::to_string(ver.Build) + "." + std::to_string(ver.Revision);
    // Append console model in parentheses
    const std::string model = GetConsoleModelString();
    return std::string(EMULATOR_NAME) + "/" + version + " (" + model + ")";
#else
    // Fallback for desktop: use SCM description
    version = SCM_DESC_STR;
    return std::string(EMULATOR_NAME) + "/" + SCM_DESC_STR;
#endif
  }();
  return user_agent_str;
}

const std::string& GetScmDistributorStr()
{
  static const std::string scm_distributor_str = SCM_DISTRIBUTOR_STR;
  return scm_distributor_str;
}

const std::string& GetScmUpdateTrackStr()
{
  static const std::string scm_update_track_str = SCM_UPDATE_TRACK_STR;
  return scm_update_track_str;
}

const std::string& GetNetplayDolphinVer()
{
#ifdef _WIN32
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Win";
#elif __APPLE__
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Mac";
#else
  static const std::string netplay_dolphin_ver = SCM_DESC_STR " Lin";
#endif
  return netplay_dolphin_ver;
}

int GetScmCommitsAheadMaster()
{
  // Note this macro can be empty if the master branch does not exist.
  return SCM_COMMITS_AHEAD_MASTER + 0;
}

}  // namespace Common
