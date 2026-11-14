#pragma once
// General role rules fitted from Prism observations. No map IDs or roster rows.
#include <algorithm>
#include <array>
namespace nb::reference {
struct Features { int width=0,height=0,picks=5; float goldRate=0.f,gemRate=0.f;
    float gemGoldRatio=0.f;
    float baseSeparation=0.f;
    float ownAccessibleArrivalMean=0.f;
    float contestedResourceFraction=0.f;
    float resourceHealthPerValue=0.f;
    float neutralStrengthRate=0.f;
    float neutralCampFraction=0.f;
    float campDensity=0.f;
    float resourceNeighbourGroupsMean=0.f;
    float centralCorridorMin=0.f;
    float monolithCount=0.f;
    float golemCount=0.f;
    float contactScore=0.f;
    float farmWalkDetour=0.f;
};
struct Plan { int profile=-1,flags=0,picks=5; std::array<int,5> order{1,2,11,3,6}; bool exact=false; };
inline Plan select(const Features& f) {
    const bool prism=true;   /* DPA */
    const bool monster=(f.baseSeparation <= 61.5f ? false : (f.neutralStrengthRate <= 9.16666667f ? false : (f.farmWalkDetour <= 0.397115398f ? true : false)));
    const bool willow=(f.baseSeparation <= 46.5f ? (f.gemGoldRatio <= 0.169027297f ? false : (f.baseSeparation <= 9.5f ? false : true)) : false);
    const bool ren=(f.baseSeparation <= 91.0f ? false : true);
    const bool heavy3=(f.monolithCount <= 0.5f ? false : true);
    Plan p;p.picks=std::clamp(f.picks,1,5);p.flags=monster?1:0;
    const bool healer=willow&&!monster;
    if(prism){p.order={5,6,3,2,0};
        if(p.picks==4)p.order[3]=monster?9:healer?7:2;
        if(p.picks>=5){if(monster)p.order={5,6,3,9,2};else if(healer)p.order[4]=7;}
    }else if(p.picks<=3){p.order=heavy3?std::array<int,5>{1,11,6,-1,-1}:std::array<int,5>{1,2,11,-1,-1};
    }else if(p.picks==4){p.order={1,2,11,monster?6:3,-1};
    }else{p.order=monster?std::array<int,5>{11,3,1,2,9}:std::array<int,5>{11,3,2,6,1};}
    if(prism&&p.picks>=5){for(int&x:p.order)if(x==0)x=7;}   /* P7 */
    for(int i=p.picks;i<5;++i)p.order[i]=-1;return p;
}
inline constexpr int PRISM=6,WILLOW=8,CASSI=3,ISSAC=4;
inline bool linked(int type){return type==CASSI||type==ISSAC;}
}
