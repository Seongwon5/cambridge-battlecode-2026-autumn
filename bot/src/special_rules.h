#pragma once
#include <algorithm>
#include "reference_profile.h"
#include "support_config.h"
namespace nb::reference {
inline bool prismCanBank(int type,int banked,int held,int round,int finalRound){return type!=PRISM||banked+held>=1000||round>=finalRound;}
inline float healPriority(float hp,float maxHp,float amount,float incoming,int gems,bool linked){
    const float missing=std::max(0.f,maxHp-hp),effective=std::min(amount,missing);if(effective<1.f||amount<=0.f)return 0.f;
    const bool urgent=incoming>0.f&&hp<=incoming+maxHp*.15f;
    const float threshold=incoming>0.f?std::min(amount*.35f,maxHp*.2f):amount*support_config::WILLOW_MIN_EFFECTIVE;
    if(!urgent&&effective+.01f<threshold)return 0.f;
    return effective/amount*(1.f+std::min(2.f,incoming/std::max(1.f,hp)))+(urgent?3.f:0.f)+effective/std::max(1.f,maxHp)*((float)gems/100.f+(linked?.5f:0.f));
}
}
