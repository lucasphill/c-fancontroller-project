#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <limits>
#include <optional>
#include <stdexcept>

namespace fanctl {
struct FrameStatistics {
    std::optional<double> fps,frameTimeMs;
    std::uint64_t newestQpc=0,swapChain=0,eventsLost=0;
    bool recovering=false;
};

// Application-present intervals only. Different swapchains are never summed.
// QPC timestamps, target identity and loss reports come from the owned capture.
class FrameMetrics {
public:
    FrameMetrics(std::uint32_t processId,std::uint64_t frequency)
        :processId_(processId),frequency_(frequency) {
        if(!processId || !frequency || frequency>std::numeric_limits<std::uint64_t>::max()/3)
            throw std::invalid_argument("Identidade ou relogio de quadros invalido.");
    }
    void present(std::uint32_t processId,std::uint64_t swapChain,std::uint64_t timestamp) {
        if(processId!=processId_ || !timestamp)return;
        auto found=chains_.find(swapChain);
        if(found==chains_.end()) {
            if(chains_.size()>=maximumChains) {
                const auto oldest=std::min_element(chains_.begin(),chains_.end(),[](const auto& a,const auto& b){return a.second.back()<b.second.back();});
                chains_.erase(oldest);
            }
            found=chains_.emplace(swapChain,std::deque<std::uint64_t>{}).first;
        }
        auto& times=found->second;
        if(!times.empty() && timestamp<=times.back())return;
        times.push_back(timestamp);
        while(times.size()>maximumPresents || (times.size()>1 && times.back()-times.front()>frequency_))times.pop_front();
    }
    void reportLoss(std::uint64_t cumulativeLost,std::uint64_t now) {
        if(cumulativeLost>eventsLost_) {chains_.clear();lastLoss_=now;recovering_=true;}
        eventsLost_=cumulativeLost;
    }
    FrameStatistics snapshot(std::uint64_t now) {
        FrameStatistics result;result.eventsLost=eventsLost_;
        if(recovering_ && (now<lastLoss_ || now-lastLoss_<frequency_)) {result.recovering=true;return result;}
        recovering_=false;
        auto selected=chains_.end();
        for(auto it=chains_.begin();it!=chains_.end();) {
            const auto newest=it->second.back();
            if(now<newest || now-newest>frequency_*3) {it=chains_.erase(it);continue;}
            if(selected==chains_.end() || it->second.size()>selected->second.size() ||
                (it->second.size()==selected->second.size() && newest>selected->second.back()))selected=it;
            ++it;
        }
        if(selected==chains_.end() || selected->second.size()<2)return result;
        const auto& times=selected->second;const auto span=times.back()-times.front();
        if(!span)return result;
        result.fps=static_cast<double>(times.size()-1)*static_cast<double>(frequency_)/static_cast<double>(span);
        result.frameTimeMs=1000.0 / *result.fps;
        result.newestQpc=times.back();result.swapChain=selected->first;return result;
    }
    std::size_t chainCount()const{return chains_.size();}
    static constexpr std::size_t maximumChains=32,maximumPresents=4096;
private:
    std::uint32_t processId_;
    std::uint64_t frequency_,eventsLost_=0,lastLoss_=0;
    bool recovering_=false;
    std::map<std::uint64_t,std::deque<std::uint64_t>> chains_;
};
}
