#include "viewmodel_model.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using hyprwalk::CViewmodelModel;
using hyprwalk::SViewmodelVertex;

static void check(bool ok, const std::string& why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
static double delta(const std::vector<SViewmodelVertex>& a, const std::vector<SViewmodelVertex>& b) {
    check(a.size()==b.size(),"vertex size changed across animation");
    double sum=0.;
    for(size_t i=0;i<a.size();++i) {
        double dx=a[i].x-b[i].x,dy=a[i].y-b[i].y,dz=a[i].z-b[i].z;
        check(std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(dz),"nonfinite skinned vertex");
        sum+=dx*dx+dy*dy+dz*dz;
    }
    return std::sqrt(sum/a.size());
}
int main(int argc,char**argv) {
    check(argc==2,"usage: viewmodel_regression /path/to/assets/viewmodels/cs16");
    fs::path root=argv[1];
    check(fs::exists(root/"v_knife.mdl"),"v_knife.mdl missing");
    size_t tested=0, changing=0;
    for (auto& entry:fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()||entry.path().extension()!=".mdl") continue;
        CViewmodelModel m; std::string err;
        check(m.start(entry.path().string(),err),entry.path().string()+" load: "+err);
        check(m.active()&&m.vertices().size()%3==0,"triangles missing: "+entry.path().string());
        check(m.cameraBack()+m.minX() >= 7.99f,"camera crosses an animation pose: "+entry.path().string());
        check(m.projectedMaxX()>m.projectedMinX() && m.projectedMaxY()>m.projectedMinY(),"invalid perspective framing");
        for (auto& v : m.vertices())
            check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"nonfinite default pose");
        // The supplied frozen knife has a *static* 151-frame idle and a
        // genuinely animated draw. Choosing the longest sequence alone would
        // accidentally test that the hands stand still while idle.
        check(m.playSequence("draw"),"frozen knife draw animation is missing");
        auto first=m.vertices();
        for(int step=0;step<10;++step) m.update(0.06f);
        const double change=delta(first,m.vertices());
        if(change>0.001) ++changing;
        check(change<100.,"implausible movement: "+entry.path().string());
        ++tested;
    }
    check(tested==1,"expected only the new frozen knife model");
    check(changing==1,"expected real animation in the frozen knife viewmodel");
    CViewmodelModel knife; std::string err;
    check(knife.start((root/"v_knife.mdl").string(),err),"knife failed: "+err);
    check(knife.sequences().size()>=8,"knife animation sequences missing");
    knife.triggerPrimary();
    check(knife.sequences()[knife.sequence()].label=="midslash1", "knife primary should use visible midslash animation");
    knife.triggerSecondary();
    check(knife.sequences()[knife.sequence()].label=="midslash2", "knife secondary should use visible midslash animation");
    check(knife.playSequence("draw"),"knife draw missing");
    auto start=knife.vertices();
    for(int step=0;step<8;++step) knife.update(0.06f);
    check(delta(start,knife.vertices())>0.3,"knife draw animation does not change visible geometry");
    knife.stop();
    check(!knife.active(),"stop did not clear model");
    std::cout<<"PASS: "<<tested<<" CS1.6 models, "<<changing<<" changing poses, knife deploy/slash, perspective bounds\n";
}
