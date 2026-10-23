#include "viewmodel_model.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static void require(bool ok, const char* why) { if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
static std::int32_t i32(const std::vector<unsigned char>& b, size_t at) {
    require(at + 4 <= b.size(), "mdl int32 offset out of bounds");
    const auto value = (std::uint32_t)b[at] | ((std::uint32_t)b[at+1] << 8) |
        ((std::uint32_t)b[at+2] << 16) | ((std::uint32_t)b[at+3] << 24);
    return static_cast<std::int32_t>(value);
}
static std::int16_t i16(const std::vector<unsigned char>& b, size_t at) {
    require(at + 2 <= b.size(), "mdl int16 offset out of bounds");
    return static_cast<std::int16_t>((std::uint16_t)b[at] | ((std::uint16_t)b[at+1] << 8));
}
int main(int argc, char** argv) {
    require(argc == 2, "usage: viewmodel_uv_regression /path/to/v_knife.mdl");
    std::ifstream file(argv[1], std::ios::binary);
    require(file.good(), "missing MDL");
    const std::vector<unsigned char> data{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto bodyPart = (size_t)i32(data, 208);
    const auto model = (size_t)i32(data, bodyPart + 72);
    const auto mesh = (size_t)i32(data, model + 76);
    const auto commands = (size_t)i32(data, mesh + 4);
    require(std::abs(i16(data,commands)) >= 3, "first triangle command missing");
    const auto skinRef = i32(data, mesh + 8);
    require(skinRef >= 0, "negative skin reference");
    const auto skinTable = (size_t)i32(data, 200);
    const auto texIdx = i16(data, skinTable + (size_t)skinRef*2);
    require(texIdx >= 0, "negative texture index");
    const auto texture = (size_t)i32(data, 184) + (size_t)texIdx * 80;
    const auto width = i32(data, texture + 68), height = i32(data, texture + 72);
    require(width > 0 && height > 0, "invalid GoldSrc texture size");
    const auto s = i16(data, commands + 2 + 4);
    const auto t = i16(data, commands + 2 + 6);
    require(t != height/2, "selected UV is symmetric under flip; cannot detect regression");

    hyprwalk::CViewmodelModel mdl;
    std::string error;
    require(mdl.start(argv[1], error), "GoldSrc loader failed");
    require(mdl.parts().size() > 0 && mdl.parts()[0].firstIndex < mdl.vertices().size(), "first mesh did not emit triangles");
    const auto& v = mdl.vertices()[mdl.parts()[0].firstIndex];
    require(std::abs(v.u-(float)s/width) < 1e-6f, "GoldSrc U mismatch");
    require(std::abs(v.v-(float)t/height) < 1e-6f, "GoldSrc V mismatch (wrong vertical flip)");
    require(std::abs(v.v-(1.f-(float)t/height)) > .01f, "regression case does not distinguish reversed V");
    std::cout << "PASS: GoldSrc StudioMDL triangle-command UVs match actual Valve coordinates\n";
}
