// Right-click menu for the notification-area icon.
#pragma once

#include <windows.h>

namespace cwut {

constexpr UINT kCommandShowPanel = 1001;
constexpr UINT kCommandExit = 1002;

// Caller owns the returned menu and must DestroyMenu it.
HMENU BuildTrayMenu();

}  // namespace cwut
