#include "menu_identity.hpp"

#include <cassert>
#include <iostream>

using namespace hyprwalk;

int main() {
    constexpr int settingFloatAction = 42; // all Movement and Camera sliders use the same owner action
    assert(sameDialIdentity(true, settingFloatAction, 0, 0, "walk_speed", true, settingFloatAction, 0, 0, "walk_speed"));
    assert(!sameDialIdentity(true, settingFloatAction, 0, 0, "walk_speed", true, settingFloatAction, 0, 0, "gravity"));
    assert(!sameDialIdentity(true, settingFloatAction, 0, 0, "walk_speed", true, settingFloatAction, 0, 1, "walk_speed"));
    assert(!sameDialIdentity(false, settingFloatAction, 0, 0, "walk_speed", true, settingFloatAction, 0, 0, "walk_speed"));
    std::cout << "setting dial identity checks passed\n";
    return 0;
}
