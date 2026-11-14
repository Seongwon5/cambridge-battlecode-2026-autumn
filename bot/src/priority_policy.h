#pragma once
// PRI (팀원 bot25 규칙 7): 매 턴 끝 sdk_set_priority. Willow 치유 우선순위 줄 (규칙 6 덩어리) 은 뺐다.
#include <algorithm>
#include "special.h"   /* TWP */
// H4b: use the actual end-of-turn state and forecast the next turn's action budget.
namespace nb::io {
namespace priority_detail {
float shield(std::uint32_t handle) {
    d::buff_data buffs[3]{}; std::uint32_t count=0; float value=0.f;
    if (d::sdk_champion_buff_data(handle,buffs,3,&count)==0)
        for (std::uint32_t i=0;i<std::min(count,3u);++i)
            if (buffs[i].type==0) value+=std::max(0.f,buffs[i].data.shield.value);
    return value;
}
float burst(const d::champion_info& info,const int* cooldown,int distance) {
    const auto& design=S.designOf(info.type);
    float best=0.f;
    for (int walk=0;walk<=1;++walk) {
        const bool attackAllowed=walk==0 || info.attack_range<=1;
        const float basic=attackAllowed && distance<=info.attack_range+walk ? info.strength : 0.f;
        float spell=0.f;
        if (design.valid) for (int slot=0;slot<2;++slot) {
            if (cooldown[slot]>1) continue;
            const auto& a=design.ab[slot];
            if (walk && a.noWalk) continue;
            if (a.kind==rules::AB_RESET) {
                if (distance<=info.attack_range+walk) spell=std::max(spell,info.strength);
            } else if (a.kind==rules::AB_DAMAGE || a.kind==rules::AB_CC || a.kind==rules::AB_AOE || a.kind==rules::AB_DASH) {
                const float radius=(a.kind==rules::AB_AOE || a.kind==rules::AB_DASH) ? a.radius : 0.f;
                if (distance<=a.range+radius+walk) spell=std::max(spell,info.focus*a.modifier);
            }
        }
        best=std::max(best,basic+spell);
    }
    return best;
}
void cooldowns(std::uint32_t handle,int (&out)[2]) {
    for (unsigned slot=0;slot<2;++slot) {std::int32_t cd=99;if (d::sdk_champion_cooldown(handle,slot,&cd)==0) out[slot]=cd;else out[slot]=99;}
}
}
void updatePriority(const World& w) {
    (void)w;
    const int currentRound=round();
    const auto handle=static_cast<std::uint32_t>(d::sdk_my_champion());
    d::champion_info info{};
    if (d::sdk_champion(handle,&info)!=0) return;
    int desired=0;
    if ((static_cast<std::uint32_t>(info.state)&rules::ST_ALIVE) && info.max_health>0.f) {
        const float effectiveHp=info.health+priority_detail::shield(handle);
        std::uint32_t gems=0;if (d::sdk_champion_gems(handle,&gems)!=0) return;
        const float fraction=std::clamp(effectiveHp/info.max_health,0.f,1.f)*(gems>0?0.5f:1.f);
        desired=-std::clamp(1+static_cast<int>(4.f*(1.f-fraction)),1,4);
        if (S.ready && fuel()>7000) {
            int ownCd[2];priority_detail::cooldowns(handle,ownCd);
            float outgoing=0.f,incoming=0.f;
            for (auto enemyHandle:S.enemyHandle) {
                if (fuel()<1700) break;
                d::champion_info enemy{};
                if (d::sdk_champion(enemyHandle,&enemy)!=0 || !(static_cast<std::uint32_t>(enemy.state)&rules::ST_ALIVE) || enemy.health<=0.f) continue;
                const int distance=cheb(Pos{info.x,info.y},Pos{enemy.x,enemy.y});if (distance>8) continue;
                int enemyCd[2];priority_detail::cooldowns(enemyHandle,enemyCd);
                if (true) outgoing=std::max(outgoing,priority_detail::burst(info,ownCd,distance)/std::max(1.f,enemy.health+priority_detail::shield(enemyHandle)));
                if (true) incoming+=priority_detail::burst(enemy,enemyCd,distance);
            }
            const float urgency=std::max(outgoing,incoming/std::max(1.f,effectiveHp));
            if (urgency>=0.25f) desired=std::clamp(static_cast<int>(4.f*urgency),1,4);
        }
    }
    if (policy::TANK_ON && S.ready && !w.enemyNear8 && (static_cast<std::uint32_t>(info.state)&rules::ST_ALIVE) && info.max_health>0.f) {   /* TANK: 곁 몸 줄 세우기 */
        std::uint32_t myGems=0;(void)d::sdk_champion_gems(handle,&myGems);
        const float myS=(info.health+priority_detail::shield(handle))*(myGems>0?0.5f:1.f);
        int rank=0,nNear=0;
        for (const auto& a:w.allies) { if (!a.alive||!a.visible||a.handle==handle) continue; if (cheb(a.pos,Pos{info.x,info.y})>6) continue; ++nNear;
            const float aS=a.hp*(a.gems>0?0.5f:1.f); if (aS>myS || (aS==myS && a.handle<handle)) ++rank; }
        if (nNear>0) desired=-std::min(rank,4);
    }
    if(S.ready&&fuel()>6000)desired=std::max(desired,special::willowNextPriority(w));   /* TWP 규칙 6: Willow 급한 치유는 4 */
    static int lastDesired=0;
    if (desired!=lastDesired && round()==currentRound && fuel()>250) {
        const int applied=false?0:desired;
        if (d::sdk_set_priority(applied)==0) lastDesired=desired;
    }
}
}
