#pragma once
#include "OnlineMenu.hpp"
#include "WindowSync.hpp"
namespace hyprwalk::mp {
// Command tokens include the current world generation. A stale open page cannot act on a new peer.
OnlinePage windowMenuPage(std::string_view page, const WindowSync&, std::string_view ownerName);
std::string windowMenuAction(std::string_view token, WindowSync&);
std::string windowMenuDetails(std::string_view token, const WindowSync&, std::string_view ownerName);
} // namespace hyprwalk::mp
