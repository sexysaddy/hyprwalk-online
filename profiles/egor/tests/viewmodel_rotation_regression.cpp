#include "viewmodel_model.hpp"
#include "viewmodel_rotation.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace hyprwalk;
namespace fs = std::filesystem;
using viewmodel_rotation::Point;

static void ensure(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static float distance(Point a, Point b) {
    return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);
}
static void checkRotation(float x, float y, float z) {
    const auto m = viewmodel_rotation::matrix(x,y,z);
    const Point p{3.f, -6.f, 8.f}, pivot{1.f, 2.f, 3.f};
    const Point out = viewmodel_rotation::transform(m,p,pivot);
    ensure(std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z),"nonfinite matrix");
    ensure(std::abs(distance(p,pivot)-distance(out,pivot)) < 0.001f,"rotation not rigid");
}
int main(int argc, char** argv) {
    ensure(argc==2,"expected the CS 1.6 models directory");
    const auto identity = viewmodel_rotation::matrix(0.f,0.f,0.f);
    const auto origin = viewmodel_rotation::transform(identity,{2.f,3.f,4.f},{0.f,0.f,0.f});
    ensure(distance(origin,{2.f,3.f,4.f}) < 1e-5f,"identity changes original pose");
    const auto aboutX = viewmodel_rotation::transform(viewmodel_rotation::matrix(90.f,0.f,0.f),{0.f,1.f,0.f},{});
    ensure(distance(aboutX,{0.f,0.f,1.f})<1e-4f,"GoldSrc rotation X basis incorrect");
    const auto aboutY = viewmodel_rotation::transform(viewmodel_rotation::matrix(0.f,90.f,0.f),{1.f,0.f,0.f},{});
    ensure(distance(aboutY,{0.f,0.f,-1.f})<1e-4f,"GoldSrc rotation Y basis incorrect");
    const auto aboutZ = viewmodel_rotation::transform(viewmodel_rotation::matrix(0.f,0.f,90.f),{1.f,0.f,0.f},{});
    ensure(distance(aboutZ,{0.f,1.f,0.f})<1e-4f,"GoldSrc rotation Z basis incorrect");
    for (auto angles : {std::array{0.f,0.f,0.f},std::array{35.f,-25.f,10.f},
                        std::array{180.f,0.f,0.f},std::array{0.f,180.f,0.f},
                        std::array{0.f,0.f,-180.f},std::array{72.f, -93.f, 117.f}})
        checkRotation(angles[0],angles[1],angles[2]);
    std::size_t total=0;
    for (const auto& entry : fs::recursive_directory_iterator(argv[1])) {
        if (!entry.is_regular_file() || entry.path().extension()!=".mdl") continue;
        CViewmodelModel model;
        std::string error;
        ensure(model.start(entry.path().string(),error),"failed to parse model");
        const auto base = model.framing(0.f,0.f,0.f);
        ensure(base.minX==model.projectedMinX() && base.minY==model.projectedMinY() &&
               base.maxX==model.projectedMaxX() && base.maxY==model.projectedMaxY() &&
               base.cameraBack==model.cameraBack(), "default framing differs from working v4");
        for (auto angles : {std::array{35.f,-25.f,10.f},std::array{180.f,0.f,0.f},
                            std::array{0.f,180.f,0.f},std::array{0.f,0.f,90.f}}) {
            const auto frame = model.framing(angles[0],angles[1],angles[2]);
            ensure(std::isfinite(frame.cameraBack) && frame.cameraBack>0.f &&
                   std::isfinite(frame.minX) && std::isfinite(frame.maxX) &&
                   std::isfinite(frame.minY) && std::isfinite(frame.maxY),"nonfinite framing");
            ensure(frame.maxX > frame.minX && frame.maxY > frame.minY,"degenerate framing");
            const auto rot = viewmodel_rotation::matrix(angles[0],angles[1],angles[2]);
            for(float x : {model.minX(),model.maxX()})
                for(float y : {model.minY(),model.maxY()})
                    for(float z : {model.minZ(),model.maxZ()}) {
                        const auto v = viewmodel_rotation::transform(rot,{x,y,z},frame.pivot);
                        ensure(v.x + frame.cameraBack >= 7.99f,"animated bounds cross near plane");
                    }
            // These settings are static across frames, even while the model
            // changes its skinned vertices in reload/shoot/idle sequences.
            for(int step=0;step<4;++step) model.update(0.08f);
            const auto later = model.framing(angles[0],angles[1],angles[2]);
            ensure(later.cameraBack==frame.cameraBack && later.minX==frame.minX &&
                   later.minY==frame.minY && later.maxX==frame.maxX && later.maxY==frame.maxY,
                   "rotated model framing jitters during animation");
        }
        ++total;
    }
    ensure(total==1,"expected only the frozen knife MDL");
    std::cout << "PASS: 1 frozen-knife MDL; rotation basis, rigidity, idle frames, near clip and unchanged zero angles\n";
}
