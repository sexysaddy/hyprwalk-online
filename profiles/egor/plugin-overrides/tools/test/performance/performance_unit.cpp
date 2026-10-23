#include "performance.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace hyprwalk::performance;
using namespace std::chrono;
int main() {
    assert(normalizeFpsLimit(61)==60 && normalizeFpsLimit(1)==30 && normalizeFpsLimit(0)==0);
    assert(nextFpsLimit(30)==45 && nextFpsLimit(120)==0 && nextFpsLimit(0)==30);
    assert(normalizeFpsLimit(std::numeric_limits<int>::max())==120);
    int combinations=0;
    std::puts("MODEL ONLY: compositor callbacks for 20 seconds; no real output/GPU/CPU measurement");
    std::puts("Hz,cap,updates_per_second,minimum_interval_ms,maximum_interval_ms");
    for(double hz:{59.94,60.,75.,120.,144.,165.,240.}) for(int cap:FPS_LIMITS) {
        SFrameCadence c;
        int frames=0;
        double smallest=1e9,largest=0,previous=-1;
        for(int tick=0;tick<(int)std::ceil(hz*20);++tick) {
            const double seconds=tick/hz;
            if(seconds>=20) break;
            const auto now=TimePoint{}+duration_cast<nanoseconds>(duration<double>(seconds));
            if(c.due(now,cap)) {
                ++frames; c.advance(now);
                if(previous>=0) {smallest=std::min(smallest,(seconds-previous)*1000); largest=std::max(largest,(seconds-previous)*1000);}
                previous=seconds;
            }
        }
        const double wanted=cap?std::min((double)cap,hz):hz;
        assert(std::abs(frames/20.-wanted)<=.1);
        if(cap && cap<hz) {
            const double slots=hz/cap;
            assert(smallest>=std::max(1.,std::floor(slots))*1000/hz-.001);
            assert(largest<=std::ceil(slots)*1000/hz+.001);
        }
        std::printf("%.2f,%d,%.3f,%.4f,%.4f\n",hz,cap,frames/20.,smallest,largest);
        ++combinations;
    }
    // Regression control: original ceil(1000/60)==17 ms skips alternate ideal 60 Hz callbacks.
    int legacy=0; auto last=TimePoint{};
    for(int tick=0;tick<1200;++tick) {
        const auto now=TimePoint{}+nanoseconds(1000000000LL+std::llround(tick*1e9/60));
        if(last==TimePoint{} || duration_cast<milliseconds>(now-last).count()>=17) {++legacy; last=now;}
    }
    assert(legacy==600);
    SFrameCadence c; const auto start=TimePoint{}+seconds(1);
    assert(c.due(start,60)); c.advance(start);
    assert(!c.due(start+milliseconds(2),60));
    assert(c.due(start+milliseconds(16),60)); c.advance(start+milliseconds(16));
    assert(c.next==start+SFrameCadence::period(60)*2);
    assert(c.due(start+seconds(3),60)); c.advance(start+seconds(3));
    assert(c.next==start+seconds(3)+SFrameCadence::period(60));
    for(int cap:FPS_LIMITS) {assert(c.due(start+seconds(4),cap)); c.advance(start+seconds(4));}
    c.reset(); assert(c.due(start,30)); c.advance(start);
    assert(c.delayMs(start)==33);
    // Model an early millisecond timer with no other events. Deadlines do not drift at 60 FPS.
    c.reset(); auto now=start;
    assert(c.due(now,60)); c.advance(now);
    for(int i=1;i<10000;++i) {
        now+=milliseconds(c.delayMs(now));
        assert(c.due(now,60)); c.advance(now);
        assert(c.next==start+SFrameCadence::period(60)*(i+1));
    }
    std::printf("PASS: %d cadence combinations; baseline 60-to-30 regression reproduced; idle/resume/switch; 10000 phase-preserving timer steps\n",combinations);
}
