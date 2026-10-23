#include "viewport.hpp"
#include "math3d.hpp"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

int main() {
    using namespace hyprwalk;
    const std::array<std::pair<int,int>,7> outputs={{{1920,1080},{1920,1200},{2520,1080},{1080,1920},{317,197},{1,1},{3,7}}};
    int cases=0;
    for (auto [w,h]:outputs) for (float a:{0.f,16.f/9.f,16.f/10.f,21.f/9.f,9.f/16.f}) {
        const auto c=fitContent(w,h,a);
        assert(c.width>0 && c.height>0 && c.x>=0 && c.y>=0);
        assert(c.x+c.width<=w && c.y+c.height<=h);
        assert(std::abs(c.x-(w-c.x-c.width))<=1 && std::abs(c.y-(h-c.y-c.height))<=1);
        assert(c.width==w || c.height==h);
        if (a==0.f) assert(c.width==w && c.height==h);
        for(float fov:{60.f,90.f,110.f}) {
            const auto proj=M4::perspective(fov*std::numbers::pi_v<float>/180.f,(float)c.width/c.height,.05f,200.f);
            assert(std::abs(proj.m[0]*c.width-proj.m[5]*c.height)<.001f);
            for(float scale:{1.f,.9f,.8f,.75f,.67f,.5f}) {
                const auto [rw,rh]=renderSize(c,scale);
                assert(rw>=1 && rh>=1 && rw<=c.width && rh<=c.height);
                if(scale==1.f) assert(rw==c.width && rh==c.height);
                assert(std::abs(rw-c.width*scale)<=.501f && std::abs(rh-c.height*scale)<=.501f);
                ++cases;
            }
        }
    }
    assert((fitContent(1920,1200,16.f/9.f)==SContentViewport{0,60,1920,1080}));
    assert((fitContent(2520,1080,16.f/9.f)==SContentViewport{300,0,1920,1080}));
    assert((renderSize(fitContent(1920,1080,0),.5f)==std::pair{960,540}));
    assert((fitContent(0,1080,1.f)==SContentViewport{}));
    assert((fitContent(1920,1080,NAN)==fitContent(1920,1080,0)));
    assert(std::abs(contentAspect(16.f/9.f,0,90,100)-1.6f)<.0001f);
    assert(std::abs(contentAspect(16.f/9.f,0,80,80)-16.f/9.f)<.0001f);
    assert(contentAspect(16.f/9.f,3,80,120)==ASPECT_RATIOS[3]);
    assert(aspectRatioIndex(NAN)==0 && aspectRatioIndex(1e30f)==4);
    for(size_t i=0;i<RENDER_SCALES.size();++i) {
        assert(nextRenderScale((float)RENDER_SCALES[i])==RENDER_SCALES[(i+1)%RENDER_SCALES.size()]);
        assert(renderScalePercent((float)RENDER_SCALES[i])==RENDER_SCALES[i]);
    }
    assert(renderScalePercent(NAN)==100 && renderScalePercent(1e30f)==100 && renderScalePercent(-1e30f)==50);
    assert(renderScalePercent(95)==100 && renderScalePercent(68)==67 && renderScalePercent(74)==75);
    assert(renderScaleFactor(NAN)==1.f && renderScaleFactor(50)==.5f && renderScaleFactor(999)==1.f);
    assert(fitContent(1920,1080,std::numeric_limits<float>::max()).height==1);
    assert(fitContent(1920,1080,std::numeric_limits<float>::min()).width==1);
    std::printf("PASS: %d projection/fit/resolution combinations; aspect presets, custom X/Y, odd/tiny dimensions, non-finite inputs\n",cases);
}
