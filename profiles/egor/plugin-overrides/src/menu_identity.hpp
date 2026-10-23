#pragma once

#include <string_view>

namespace hyprwalk {

    // Stable identity for an active slider while the menu's page is reconstructed every frame.
    // Owner-defined settings often share the action number and default arguments, so the target is part of identity.
    inline bool sameDialIdentity(bool aDial, int aAction, int aArg, int aArg2, std::string_view aTarget,
                                 bool bDial, int bAction, int bArg, int bArg2, std::string_view bTarget) {
        return aDial && bDial && aAction == bAction && aArg == bArg && aArg2 == bArg2 && aTarget == bTarget;
    }

} // namespace hyprwalk
