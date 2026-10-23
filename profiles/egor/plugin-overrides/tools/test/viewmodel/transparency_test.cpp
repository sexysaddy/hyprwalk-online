#include "viewmodel_model.hpp"
#include "viewmodel_blend.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using namespace hyprwalk;
using hyprwalk::viewmodel_blend::Mode;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: transparency_test path/to/v_knife.mdl scratch-directory\n";
        return 2;
    }
    static_assert(viewmodel_blend::fromStudioFlags(0x00) == Mode::Opaque);
    static_assert(viewmodel_blend::fromStudioFlags(0x40) == Mode::Masked);
    static_assert(viewmodel_blend::fromStudioFlags(0x20) == Mode::Additive);
    static_assert(viewmodel_blend::fromStudioFlags(0x60) == Mode::Additive);
    CViewmodelModel model;
    std::string error;
    if (!model.start(argv[1], error)) {
        std::cerr << "ERROR loading frozen knife: " << error << '\n';
        return 1;
    }
    if (model.textures().size() != 4) {
        std::cerr << "Expected the four embedded frozen-knife textures\n";
        return 1;
    }
    if (model.textures()[0].blendMode != Mode::Additive) {
        std::cerr << "Knife palette did not retain STUDIO_NF_ADDITIVE\n";
        return 1;
    }
    for (size_t t=1;t<4;++t) {
        if (model.textures()[t].blendMode != Mode::Opaque) {
            std::cerr << "Glove/skin texture mistakenly classified translucent: " << t << '\n';
            return 1;
        }
    }
    size_t additiveParts = 0, opaqueParts = 0;
    for (auto& part : model.parts()) {
        if (part.texture >= model.textures().size() || !part.indexCount) {
            std::cerr << "Bad part in frozen knife mesh\n";
            return 1;
        }
        if (model.textures()[part.texture].blendMode == Mode::Additive) ++additiveParts;
        else ++opaqueParts;
    }
    if (!additiveParts || !opaqueParts) {
        std::cerr << "Mesh is missing knife or glove parts: additive=" << additiveParts << " opaque=" << opaqueParts << '\n';
        return 1;
    }
    if (model.sequences().empty()) {
        std::cerr << "Expected frozen knife skeleton animations\n";
        return 1;
    }
    const auto& knife = model.textures()[0];
    bool foundBlack = false, foundBright = false;
    for (size_t i=0;i<knife.rgba.size();i+=4) {
        const auto r=knife.rgba[i], g=knife.rgba[i+1], b=knife.rgba[i+2];
        if (r == 0 && g == 0 && b == 0) foundBlack=true;
        if (r > 90 || g > 180 || b > 180) foundBright=true;
        if (knife.rgba[i+3] != 255) {
            std::cerr << "Additive mode unexpectedly modifies indexed texture alpha\n";
            return 1;
        }
    }
    if (!foundBlack || !foundBright) {
        std::cerr << "Unexpected frozen knife palette distribution\n";
        return 1;
    }
    // The intended GoldSrc additive equation: final.rgb = clamp(dst.rgb + src.rgb).
    // Unlike the old premultiplied opaque path, black must leave the destination unchanged.
    const float dst[3]{0.23f,0.41f,0.62f};
    const float black[3]{0,0,0};
    for (int c=0;c<3;++c) {
        const float result = std::min(1.f, dst[c] + black[c]);
        if (std::abs(result-dst[c]) > 1.e-6f) return 1;
    }
    // Synthetic GoldSrc masked texture test. Keep the real asset byte-exact;
    // patch only a private scratch copy to exercise the 0x40 palette-index
    // transparency path even though the supplied knife uses additive blending.
    std::ifstream raw(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(raw)), std::istreambuf_iterator<char>());
    const auto int32At = [&](size_t offset) -> int32_t {
        if (offset + 4 > bytes.size()) return -1;
        int32_t value = -1;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    const int32_t textureTable = int32At(184);
    if (textureTable < 0 || static_cast<size_t>(textureTable)+80 > bytes.size()) return 1;
    const int32_t texelOffset = int32At(static_cast<size_t>(textureTable) + 76);
    if (texelOffset < 0 || static_cast<size_t>(texelOffset) >= bytes.size()) return 1;
    const uint32_t maskedFlag = viewmodel_blend::STUDIO_NF_MASKED;
    std::memcpy(bytes.data() + static_cast<size_t>(textureTable) + 64, &maskedFlag, sizeof(maskedFlag));
    bytes[static_cast<size_t>(texelOffset)] = 255u;
    const auto scratch = std::filesystem::path(argv[2]) / "masked-scratch.mdl";
    {
        std::ofstream out(scratch, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return 1;
    }
    CViewmodelModel masked;
    if (!masked.start(scratch.string(), error) || masked.textures().empty()) {
        std::cerr << "Masked scratch variant did not load: " << error << '\n';
        return 1;
    }
    const auto& maskedTex = masked.textures().front();
    if (maskedTex.blendMode != Mode::Masked || maskedTex.rgba[0] != 0 || maskedTex.rgba[1] != 0 ||
        maskedTex.rgba[2] != 0 || maskedTex.rgba[3] != 0) {
        std::cerr << "GoldSrc 0x40 palette index 255 was not converted to transparent RGBA\n";
        return 1;
    }
    // Original framebuffer alpha should be preserved by glBlendFuncSeparate(..., GL_ZERO, GL_ONE).
    std::cout << "PASS: one frozen knife model, " << model.textures().size() << " textures, "
              << additiveParts << " additive parts, " << opaqueParts << " opaque parts, "
              << model.sequences().size() << " animation sequences\n";
    return 0;
}
