#pragma once

#include "ImGuiNetplay.h"

#include "Common/Flag.h"
#include "Core/NetPlayClient.h"
#include "Core/NetPlayServer.h"

namespace UWP {
extern Common::Flag g_shutdown_requested;
extern Common::Flag g_tried_graceful_shutdown;
}

extern std::shared_ptr<NetPlay::NetPlayClient> g_netplay_client;
extern std::shared_ptr<NetPlay::NetPlayServer> g_netplay_server;
extern std::shared_ptr<ImGuiFrontend::ImGuiNetPlay> g_netplay_dialog;
