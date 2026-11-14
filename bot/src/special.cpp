// TWP: 팀원 bot25 special.cpp (규칙 3·4·6). 쌍둥이 (규칙 5) 함수는 뺐다.
#include <algorithm>
#include "special.h"
#include "special_rules.h"
#include "reference_config.h"
#include "io.h"
#include "team.h"
#include "policy.h"
namespace nb::special {
int teamCarried(const World&w){
    // Exact own/visible values plus recent quantised reports across fog.
    // Hidden values cannot be queried directly; the total is an estimate.
    int total=w.me.gems;std::vector<std::uint32_t> hs=io::S.allyHandle;std::sort(hs.begin(),hs.end());
#if TWP_GEMREP
    const int lifetime=team::richGemMode()?3:team::LIFE_CLAIM;
#else
    const int lifetime=team::LIFE_CLAIM;
#endif
    for(size_t k=0;k<hs.size();++k){if(hs[k]==w.me.handle)continue;
        const Unit*seen=nullptr;for(const auto&a:w.allies)if(a.handle==hs[k]&&a.visible){seen=&a;break;}
        if(seen){if(seen->alive){int n=seen->gems;(void)io::carriedGems(hs[k],n);total+=n;}continue;}
        if(k<(size_t)team::T.n){const auto&m=team::T.mate[k];if(w.round-m.gemsRound<=lifetime&&m.gemsRep>0){bool alive=true;if(!io::championAlive(hs[k],alive)||alive)total+=m.gemsRep;}}}
    return total;
}
bool prismCanBank(const World&w){if constexpr(!reference::PRISM_MACRO)return true;return reference::prismCanBank(w.me.type,io::submitted(true),teamCarried(w),w.round,policy::TRIBUTE_FINAL_ROUND);}
static float incoming(const World&w,const Unit&a){
    if(io::fuel()<5000)return 0.f;
    float damage=policy::BH2_SP1_ON?combat::enemyThreatFor(w,a.pos,a.hp,a.type,a.handle):combat::enemyThreatAt(w,a.pos);   /* BH2_SP1 */
    for(const auto&c:w.camps){if(io::fuel()<2500)break;if(c.hp<=0.f||c.str<=0.f||c.group<0||io::S.groups[(size_t)c.group].awakeUntil<w.round)continue;const int d=cheb(c.pos,a.pos);if(d>io::S.campRange(c.type))continue;
        int nearest=cheb(c.pos,w.me.pos);for(const auto&b:w.allies)if(b.handle!=a.handle&&b.visible&&b.alive)nearest=std::min(nearest,cheb(c.pos,b.pos));
        if(d<=nearest)damage+=c.str;
    }return damage;
}
static float score(const World&w,const Unit&a){if(a.maxHp-a.hp<1.f)return 0.f;const auto&D=io::S.designOf(w.me.type);return reference::healPriority(a.hp,a.maxHp,w.me.focus*D.ab[0].modifier,incoming(w,a),a.gems,reference::linked(a.type));}
void planHeal(const World&w,combat::Plan&p){
    if constexpr(!reference::WILLOW_MACRO)return;if(w.me.type!=reference::WILLOW||!w.me.canCast||w.me.cooldown[0]||w.me.channeling)return;
    const auto&D=io::S.designOf(w.me.type);if(!D.valid||D.ab[0].kind!=rules::AB_HEAL)return;
    const Unit*best=nullptr;float bestScore=0.f;bool before=false;
    for(const auto&a:w.allies){if(io::fuel()<6500)break;if(!a.visible||!a.alive)continue;const bool pre=cheb(w.me.pos,a.pos)<=(int)D.ab[0].range,post=cheb(p.cell,a.pos)<=(int)D.ab[0].range;if(!pre&&!post)continue;
        const float value=score(w,a);if(value>bestScore){best=&a;bestScore=value;before=pre;}}
    if(!best)return;p.castSlot=0;p.castKind=1;p.castHandle=best->handle;p.castBeforeMove=before;p.resetAttack=false;
    if(p.atkWhen==4){p.atkKind=0;p.atkWhen=0;}p.resetSlot=-1;
}
bool healNow(const World&w){
    if constexpr(!reference::WILLOW_MACRO)return false;if(w.me.type!=reference::WILLOW||io::fuel()<10000)return false;
    Unit self;if(!io::championLive(w.me.handle,self)||!(self.state&rules::ST_CAN_CAST)||self.cooldown[0]||(self.state&(rules::ST_RECALLING|rules::ST_SUBMITTING|rules::ST_TELEPORTING)))return false;
    World now=w;now.me.pos=self.pos;now.me.hp=self.hp;now.me.focus=self.focus;now.allies.clear();
    for(auto h:io::S.allyHandle){if(io::fuel()<5500)break;if(h==w.me.handle)continue;Unit a;if(io::championLive(h,a))now.allies.push_back(a);}
    const auto&D=io::S.designOf(w.me.type);if(!D.valid||D.ab[0].kind!=rules::AB_HEAL)return false;
    std::vector<std::pair<float,std::uint32_t>> targets;
    for(const auto&a:now.allies){if(io::fuel()<5500)break;if(cheb(self.pos,a.pos)<=(int)D.ab[0].range){const float v=score(now,a);if(v>0.f)targets.push_back({v,a.handle});}}
    std::sort(targets.begin(),targets.end(),[](const auto&a,const auto&b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    for(const auto&t:targets)if(io::castChampion(0,t.second))return true;return false;
}
int willowNextPriority(const World&w){
    if constexpr(!reference::WILLOW_MACRO)return -100;
    if(w.me.type!=reference::WILLOW||io::fuel()<6500)return -100;
    Unit self;if(!io::championLive(w.me.handle,self)||self.cooldown[0]>1||(self.state&(rules::ST_RECALLING|rules::ST_SUBMITTING|rules::ST_TELEPORTING)))return -100;
    const auto&D=io::S.designOf(self.type);if(!D.valid||D.ab[0].kind!=rules::AB_HEAL)return -100;
    World now=w;now.me.pos=self.pos;now.me.hp=self.hp;now.me.maxHp=self.maxHp;now.me.focus=self.focus;int priority=-100;
    for(auto h:io::S.allyHandle){if(io::fuel()<5500)break;if(h==self.handle)continue;Unit a;if(!io::championLive(h,a)||a.maxHp-a.hp<1.f||cheb(a.pos,self.pos)>(int)D.ab[0].range||(io::praMark(a),(a.state&(rules::ST_RECALLING|rules::ST_TELEPORTING))))continue;   /* PRA: 막 건 리콜 팀원은 치유 우선순위 대상 */
        if(io::S.inb(a.pos)&&(io::S.tile[(size_t)io::S.idx(a.pos)]&T_ALLY_SPAWN))continue;
        const float danger=incoming(now,a),amount=self.focus*D.ab[0].modifier;
        if(reference::healPriority(a.hp,a.maxHp,amount,danger,a.gems,reference::linked(a.type))<=0.f)continue;
        const bool urgent=danger>0.f&&a.hp<=danger+a.maxHp*.15f;
        if(urgent)priority=std::max(priority,4);
    }
    return priority;
}
int willowSupportGroup(const World&w){
    if constexpr(!reference::WILLOW_MACRO)return -1;if(w.me.type!=reference::WILLOW||w.me.channeling)return-1;
    std::vector<std::uint32_t>hs=io::S.allyHandle;std::sort(hs.begin(),hs.end());int best=-1;float value=0.f;
    for(const auto&a:w.allies){if(io::fuel()<5500)break;if(!a.visible||!a.alive||a.maxHp-a.hp<1.f||cheb(a.pos,w.me.pos)>10)continue;
        auto it=std::find(hs.begin(),hs.end(),a.handle);if(it==hs.end())continue;const int k=(int)(it-hs.begin());if(k>=team::T.n)continue;
        const auto&m=team::T.mate[k];if(m.claimGroup<0||m.claimKind!=team::CL_FARM||w.round-m.claimRound>team::LIFE_CLAIM)continue;
        if((size_t)m.claimGroup>=io::S.groups.size()||io::S.groups[(size_t)m.claimGroup].state==3)continue;
        const float v=score(w,a)/(float)(2+cheb(a.pos,w.me.pos));if(v<=value)continue;best=m.claimGroup;value=v;
    }return best;
}
}
