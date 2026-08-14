#include "menu.h"

namespace cwut {

HMENU BuildTrayMenu() {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return nullptr;
    AppendMenuW(menu, MF_STRING, kCommandShowPanel, L"Show panel");
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");
    SetMenuDefaultItem(menu, kCommandShowPanel, FALSE);
    return menu;
}

}  // namespace cwut
