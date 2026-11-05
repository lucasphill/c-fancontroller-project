#pragma once
#include "thermal_history.h"
#include <cmath>
namespace fanctl {
// Bound presentation cost without discarding spikes or crossing unobserved spans.
inline std::vector<HistorySample> historyForDisplay(const std::vector<HistorySample>& rows,HistoryTime from,HistoryTime until) {
    if(rows.size()<=4000)return rows;
    const auto bucket=std::max<HistoryTime>(2,(until-from)/1400);
    std::vector<HistorySample> result;std::array<double,6> sums{};HistorySample current;bool started=false;HistoryTime lastEnd=0;int lastInterval=2;
    auto mask=[](const HistorySample& r){int m=0,b=1;for(const auto& f:r.fans)for(const auto& n:{f.temperature,f.boost,f.rpm}){if(n)m|=b;b<<=1;}return m;};
    auto flush=[&]{if(!started)return;int n=0;for(auto& f:current.fans)for(auto* value:{&f.temperature,&f.boost,&f.rpm}){if(*value)*value=static_cast<int>(std::round(sums[n]/current.count));++n;}result.push_back(current);};
    for(const auto& row:rows){const bool same=started&&current.time/bucket==row.time/bucket&&current.configuration==row.configuration&&current.segment==row.segment&&current.gpuSource==row.gpuSource&&mask(current)==mask(row)&&row.time-lastEnd<=std::max(lastInterval,row.interval)*3/2;
        if(!same){flush();current=row;current.end=std::max(row.time,row.end);current.count=0;sums={};started=true;}
        int n=0;for(int f=0;f<2;++f){const auto& v=row.fans[f];auto& target=current.fans[f];for(const auto& value:{v.temperature,v.boost,v.rpm}){if(value)sums[n]+=static_cast<double>(*value)*row.count;++n;}
            auto extrema=[](std::optional<int>& lo,std::optional<int>& hi,std::optional<int> a,std::optional<int> b,std::optional<int> fallback){if(!a)a=fallback;if(!b)b=fallback;if(a)lo=lo?std::min(*lo,*a):a;if(b)hi=hi?std::max(*hi,*b):b;};
            extrema(target.temperatureMin,target.temperatureMax,v.temperatureMin,v.temperatureMax,v.temperature);
            extrema(target.rpmMin,target.rpmMax,v.rpmMin,v.rpmMax,v.rpm);
        }lastEnd=std::max(row.time,row.end);lastInterval=row.interval;current.count+=row.count;current.end=std::max(row.time,row.end);if(same)current.interval=static_cast<int>(std::max<HistoryTime>(bucket,current.interval));
    }flush();return result;
}
} // namespace fanctl
