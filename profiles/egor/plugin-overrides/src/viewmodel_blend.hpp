#pragma once

#include <cstdint>

// GoldSrc StudioMDL v10 texture flags, see studio.h (Valve Half-Life SDK).
// The supplied frozen knife uses STUDIO_NF_ADDITIVE on its knife skin, while
// the glove/finger/skin textures are opaque.  This classification is not
// guessed from palette colors; GoldSrc indexed textures have no per-pixel
// fractional-alpha channel.
namespace hyprwalk::viewmodel_blend {
    constexpr uint32_t STUDIO_NF_ALPHA = 0x0010;
    constexpr uint32_t STUDIO_NF_ADDITIVE = 0x0020;
    constexpr uint32_t STUDIO_NF_MASKED = 0x0040;

    enum class Mode : uint8_t { Opaque, Masked, Additive };

    constexpr Mode fromStudioFlags(uint32_t flags) {
        if (flags & STUDIO_NF_ADDITIVE) return Mode::Additive;
        if (flags & STUDIO_NF_MASKED) return Mode::Masked;
        return Mode::Opaque;
    }
}
