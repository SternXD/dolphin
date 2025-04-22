#pragma once

#include "ImGuiFrontend.h"

namespace ImGuiFrontend {

class ImGuiMappingWindow
{
public:
    // Draw the mapping window for given port (0-3). show/hide controlled by state->showMappingWindow
    static void Draw(int port, UIState* state);
    // Begin capturing next input for a control
    // state->capturingRef points to the ControlReference to bind
};

} // namespace ImGuiFrontend
