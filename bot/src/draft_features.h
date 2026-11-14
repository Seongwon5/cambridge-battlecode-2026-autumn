#pragma once
// Draft-visible geometry only. Eight-neighbour walking excludes walls/holes/altars.
// Camp clearing, champions, dashes and teleport are not simulated by these proxies.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "reference_profile.h"
namespace nb::reference {
using Bytes=std::vector<std::uint8_t>;
inline bool draftBit(const Bytes& b,int i){return i>=0&&(size_t)(i>>3)<b.size()&&((b[(size_t)i>>3]>>(i&7))&1);}
inline std::vector<int> draftDistance(int W,int H,const Bytes& blocked,const Bytes& spawn){
    const int n=W*H;std::vector<int>d(n,-1),q;q.reserve(n);
    for(int i=0;i<n;++i)if(!blocked[i]&&draftBit(spawn,i)){d[i]=0;q.push_back(i);}
    for(size_t k=0;k<q.size();++k){const int i=q[k],x=i%W,y=i/W;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(dx==0&&dy==0)continue;
            const int xx=x+dx,yy=y+dy;if(xx<0||xx>=W||yy<0||yy>=H)continue;const int j=yy*W+xx;
            if(!blocked[j]&&d[j]<0){d[j]=d[i]+1;q.push_back(j);}}
    }return d;
}
using DraftDistanceFunction=std::vector<int>(*)(int,int,const Bytes&,const Bytes&);
inline Features draftFeatures(int W,int H,int picks,const std::vector<std::uint32_t>& words,
    const Bytes& walls,const Bytes& holes,const Bytes& altars,const Bytes& spawnA,const Bytes& spawnB,
    DraftDistanceFunction distance=draftDistance){
    Features f;f.width=W;f.height=H;f.picks=picks;const int n=W*H;if(n<=0)return f;
    Bytes blocked(n,0);int walkable=0;std::vector<int>spawns;for(int i=0;i<n;++i)if(draftBit(spawnA,i))spawns.push_back(i);
    for(int i=0;i<n;++i){blocked[i]=draftBit(walls,i)||draftBit(holes,i)||draftBit(altars,i);walkable+=!blocked[i];}
    const auto da=distance(W,H,blocked,spawnA),db=distance(W,H,blocked,spawnB);
    int separation=n;for(int i=0;i<n;++i)if(draftBit(spawnB,i)&&da[i]>=0)separation=std::min(separation,da[i]);
    f.baseSeparation=(float)separation;
    if(separation<n){const int first=std::max(1,(int)std::ceil(separation*.2)),last=(int)std::floor(separation*.8);
        std::vector<int> layers(separation+1,0);
        for(int i=0;i<n;++i)if(da[i]>=first&&da[i]<=last&&db[i]>=0&&da[i]+db[i]<=separation+2)++layers[da[i]];
        if(first<=last){int m=n;for(int i=first;i<=last;++i)m=std::min(m,layers[i]);f.centralCorridorMin=(float)m;}}
    struct Group{float x=0,y=0,weight=0;};std::vector<Group>groups;
    constexpr float hp[]={1350,2400,120,120,20000,5000};
    constexpr float strength[]={20,60,0,0,200,120};
    float value=0,hpRate=0,arrivalWeight=0,arrival=0,contested=0,detour=0;int campCount=0,neutral=0;
    for(size_t i=0;i+5<=words.size();){const float respawn=(float)std::max(1u,words[i+3]);const auto count=words[i+4];
        if(count>(words.size()-i-5)/4)break;
        const float gold=words[i+1]/respawn,gems=words[i+2]/respawn,weight=gold/30.f+gems;
        f.goldRate+=gold;f.gemRate+=gems;value+=weight;i+=5;
        float x=0,y=0;int a=-1,b=-1,straight=n;
        for(std::uint32_t k=0;k<count;++k,i+=4){const int type=(int)words[i+1],xx=(int)words[i+2],yy=(int)words[i+3];++campCount;x+=xx;y+=yy;
            if(type>=0&&type<6){hpRate+=hp[type]/respawn;f.neutralStrengthRate+=strength[type]/respawn;}
            if(type==0||type==1||type==4||type==5)++neutral;
            if(type==4)f.monolithCount+=1;if(type==5)f.golemCount+=1;
            if(xx<0||xx>=W||yy<0||yy>=H)continue;const int cell=yy*W+xx;for(int spawn:spawns)straight=std::min(straight,std::max(std::abs(xx-spawn%W),std::abs(yy-spawn/W)));
            if(da[cell]>=0&&(a<0||da[cell]<a))a=da[cell];if(db[cell]>=0&&(b<0||db[cell]<b))b=db[cell];}
        if(count)groups.push_back({x/count,y/count,weight});
        if(a>=0&&(b<0||a<=b+8)){arrival+=a*weight;arrivalWeight+=weight;detour+=weight*std::max(0,a-straight)/(float)std::max(1,a);}
        if(a>=0&&b>=0&&std::abs(a-b)<=8)contested+=weight;
    }
    f.gemGoldRatio=f.gemRate/std::max(1e-9f,f.goldRate);
    f.resourceHealthPerValue=hpRate/std::max(1e-9f,value);
    f.ownAccessibleArrivalMean=arrival/std::max(1e-9f,arrivalWeight);
    f.contestedResourceFraction=contested/std::max(1e-9f,value);
    f.neutralCampFraction=(float)neutral/std::max(1,campCount);
    f.campDensity=(float)campCount/std::max(1,walkable);
    int neighbours=0;for(const auto&a:groups)for(const auto&b:groups)if(std::max(std::abs(a.x-b.x),std::abs(a.y-b.y))<=8.f)++neighbours;
    f.resourceNeighbourGroupsMean=groups.empty()?0.f:(float)(neighbours-(int)groups.size())/groups.size();
    f.contactScore=f.contestedResourceFraction/(1.f+f.ownAccessibleArrivalMean/20.f);
    f.farmWalkDetour=detour/std::max(1e-9f,arrivalWeight);
    return f;
}
}
