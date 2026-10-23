#pragma once

#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hyprwalk::mp {
// Main-thread view model only. No sockets, workers, compositor or graphics objects.
enum class OnlineAction { None, ToggleRole, EditAddress, EditPort, Start, Disconnect, Details, WindowCommand, WindowDetails };
enum class OnlineField { Address, Port, ReadOnly };
struct OnlineItem {
    std::string label, hint, page, detail;
    OnlineAction action = OnlineAction::None;
    bool disabled = false, on = false;
    std::string target{}; // local action token; never sent to the peer
};
struct OnlinePage {
    std::string title;
    std::vector<OnlineItem> items;
};
struct OnlineRequest {
    Role role = Role::None;
    Endpoint endpoint;
    bool disconnect = false;
};

class OnlineMenu {
  public:
    Role mode() const noexcept { return m_mode; }
    const std::string& address() const noexcept { return m_address; }
    const std::string& port() const noexcept { return m_port; }
    static bool busy(const Status& status) noexcept;
    static bool onlinePath(std::string_view path) noexcept;
    static std::string validateAddress(std::string_view address);
    static std::string validatePort(std::string_view port, Role role);
    std::string toggleRole(const Status& status);
    std::string setField(OnlineField field, const std::string& value, const Status& status);
    std::optional<OnlineRequest> request(OnlineAction action, const Status& status, std::string& error) const;
    void result(std::string_view result); // Records an immediate local-command error, never invents success.
    OnlinePage page(std::string_view id, const Status& status, std::string_view localName) const;
    std::string details(std::string_view key, const Status& status, std::string_view localName) const;

  private:
    Role m_mode = Role::Host;
    std::string m_address = "127.0.0.1", m_port = "45000", m_error;
};

// The address/port editor is testable independently of the radial renderer.
// Evdev numeric/hex input deliberately uses physical keys, independent of the active
// keyboard layout; it is NOT a general text or clipboard/IME input implementation.
enum class EditorResult { Ignored, Changed, Accept, Cancel };
class OnlineEditor {
  public:
    OnlineEditor(OnlineField field, std::string title, std::string value);
    EditorResult key(std::uint32_t evdev, bool shift = false, bool control = false,
                     bool alt = false, bool meta = false);
    void scroll(int steps) noexcept;
    std::string displayValue() const;
    bool readOnly() const noexcept { return field == OnlineField::ReadOnly; }
    OnlineField field;
    std::string title, value, error;
    std::size_t caret = 0;
    bool selected = true;
    int scrollLine = 0, scrollLimit = 256; // Renderer tightens the limit to measured text height.

  private:
    bool insert(char ch);
    void clearSelection();
};
} // namespace hyprwalk::mp
