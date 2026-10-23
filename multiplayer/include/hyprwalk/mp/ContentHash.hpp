#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hyprwalk::mp {
using Digest = std::array<std::uint8_t, 32>;
// SHA-256 content fingerprint only. This is NOT peer authentication or encryption.
// Copyable streaming state; finish() does not mutate it. No filesystem or network access.
class ContentHash {
  public:
    void update(std::span<const std::uint8_t> bytes) noexcept;
    void integer(std::uint64_t value) noexcept; // Canonical u64, network byte order.
    void floating(float value) noexcept;       // Canonical binary32; -0 becomes +0.
    Digest finish() const noexcept;
  private:
    void block(const std::uint8_t* data) noexcept;
    std::array<std::uint32_t, 8> m_state{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                      0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::array<std::uint8_t,64> m_buffer{};
    std::uint64_t m_bytes = 0;
    std::size_t m_used = 0;
};
} // namespace hyprwalk::mp
