#pragma once

#include "Core/NetPlayClient.h"
#include "Core/NetPlayServer.h"

#include "UICommon/ImGuiMenu/ImGuiFrontend.h"
#include "UICommon/ImGuiMenu/ImGuiNetplay.h"
#include "Common/Flag.h"

namespace UWP {
extern Common::Flag g_shutdown_requested;
extern Common::Flag g_tried_graceful_shutdown;
}

extern std::shared_ptr<NetPlay::NetPlayClient> g_netplay_client;
extern std::shared_ptr<NetPlay::NetPlayServer> g_netplay_server;
extern std::shared_ptr<ImGuiFrontend::ImGuiNetPlay> g_netplay_dialog;
