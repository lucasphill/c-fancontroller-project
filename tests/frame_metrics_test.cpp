#include "../src/frame_metrics.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
void intervalAndFiltering() {
    fanctl::FrameMetrics values(12,1000);
    require(!values.snapshot(1000).fps,"No presents must not become zero FPS");
    values.present(13,1,100);values.present(13,1,120);
    require(!values.snapshot(120).fps,"Other process was measured");
    values.present(12,1,100);require(!values.snapshot(100).fps,"One present has no interval");
    values.present(12,1,120);values.present(12,1,140);
    const auto sample=values.snapshot(140);
    require(sample.fps && std::abs(*sample.fps-50.0)<0.0001,"Present interval FPS conversion wrong");
    require(sample.frameTimeMs && *sample.frameTimeMs==20.0,"Frame interval units wrong");
    values.present(12,1,120);values.present(12,1,140);
    require(values.snapshot(140).fps==sample.fps,"Duplicate/out-of-order events changed count");
    require(values.snapshot(3140).fps.has_value(),"Inclusive freshness boundary discarded");
    require(!values.snapshot(3141).fps,"Stale frames reused");
}
void chainsAndBounds() {
    fanctl::FrameMetrics values(12,1000);
    for(std::uint64_t t=100;t<=200;t+=10)values.present(12,1,t);
    for(std::uint64_t t=100;t<=200;t+=20)values.present(12,2,t);
    const auto sample=values.snapshot(200);
    require(sample.swapChain==1 && sample.fps==100.0,"Swapchains must not be added together");
    for(std::uint64_t c=3;c<=100;++c)values.present(12,c,200+c);
    require(values.chainCount()==fanctl::FrameMetrics::maximumChains,"Swapchain state unbounded");
    require(!values.snapshot(1).fps && values.chainCount()==0,"Future events became current");
    fanctl::FrameMetrics highRate(12,1000000);
    for(std::uint64_t t=1;t<=10000;++t)highRate.present(12,1,t);
    const auto high=highRate.snapshot(10000);
    require(high.fps && *high.fps==1000000.0,"High-rate cap corrupts intervals");
}
void lossAndRecovery() {
    fanctl::FrameMetrics values(12,1000);
    values.present(12,1,100);values.present(12,1,120);
    values.reportLoss(1,130);
    auto sample=values.snapshot(130);
    require(!sample.fps && sample.recovering && sample.eventsLost==1,"Loss interval exposed FPS");
    values.present(12,1,1140);values.present(12,1,1160);
    values.reportLoss(1,1160); // same cumulative count must not invalidate every future interval
    sample=values.snapshot(1160);
    require(sample.fps==50.0 && !sample.recovering && sample.eventsLost==1,"Loss recovery never resumed");
    values.reportLoss(2,1200);
    require(!values.snapshot(1500).fps,"Second loss did not invalidate interval");
    require(!values.snapshot(2200).fps,"Pre-loss data was resurrected");
    values.present(12,2,2210);values.present(12,2,2250);
    require(values.snapshot(2250).fps==25.0,"Fresh chain after loss failed");
}
void recentWindowAndIdentityReset() {
    fanctl::FrameMetrics values(12,1000);
    values.present(12,1,100);values.present(12,1,200);
    values.present(12,1,1200);values.present(12,1,1220);
    // Only intervals in the trailing second of application presentation history remain.
    require(values.snapshot(1220).fps==50.0,"Old slow intervals contaminated recent window");
    fanctl::FrameMetrics replacement(14,1000);
    replacement.present(12,1,1230);replacement.present(12,1,1250);
    require(!replacement.snapshot(1250).fps,"Old PID data leaked into replacement capture");
    bool rejected=false;
    try{fanctl::FrameMetrics invalid(0,1000);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Zero process identity accepted");
    rejected=false;
    try{fanctl::FrameMetrics invalid(1,std::numeric_limits<std::uint64_t>::max());}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"Clock overflow accepted");
}
}
int main() {
    try{intervalAndFiltering();chainsAndBounds();lossAndRecovery();recentWindowAndIdentityReset();
        std::cout<<"Frame PID filtering, intervals, swapchains, freshness, bounded state and loss recovery passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
