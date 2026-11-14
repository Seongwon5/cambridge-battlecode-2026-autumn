#include <cstring>
#include <algorithm>
#include "economy.h"
#include "policy.h"
#include "io.h"
#include "reference_config.h"   /* TWP */
#include "reference_items.h"   /* TWP */
#include "log.h"   /* TPS */

namespace nb::economy {

static const Static& S() { return io::S; }

int g_chcStart = -1000, g_chcDur = 0;   /* CHC: 바치기 시작 R · 채널 길이 */
float chcDanger(const World& w, int rounds) {   /* CHC: 닿을 거리 적의 힘 합 x R */
    float s = 0.f; for (const auto& e : w.enemies) if (e.alive && e.visible && cheb(e.pos, w.me.pos) <= e.range + 2) s += e.str;
    return s * (float)std::max(0, rounds);
}
static void lksLock(const World& w, Pos a) {   /* LKS: 바치기 전에 내 잠금 */
    if (!policy::LK_ON || io::consumableCount(2) <= 0) return;
    for (const auto& al : w.allies) if (al.visible && al.alive && (al.state & rules::ST_SUBMITTING) && cheb(al.pos, a) <= rules::SUBMIT_RANGE) return;
    std::vector<io::LockSeen> L; io::altarLocks(L); for (const auto& l : L) if (l.pos == a) return;
    io::useConsumable(2, a);
}
bool tryTribute(const World& w, const mission::Mission& M, combat::Plan& p) {
    if (M.kind != mission::M_TRIBUTE || M.altar < 0 || w.me.gems <= 0 || w.me.channeling) return false;
    const Pos a = S().altars[(size_t)M.altar];
    Pos twsQ{-1, -1};   /* TWS: 2칸이면 한 걸음 들어가 같은 턴에 바침 */
    if (cheb(w.me.pos, a) > rules::SUBMIT_RANGE) {
        if (!policy::TWS_ON || !w.me.canMove || cheb(w.me.pos, a) != rules::SUBMIT_RANGE + 1 || !w.enemies.empty()) return false;   /* TWS2: 보이는 적이 없을 때만 */
        if (p.move && !p.dash && cheb(p.cell, a) <= rules::SUBMIT_RANGE) twsQ = p.cell;
        else for (int dy = -1; dy <= 1 && twsQ.x < 0; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{w.me.pos.x + dx, w.me.pos.y + dy};
            if (!S().inb(q) || !S().walkableStatic(q) || io::campBlocksMovement(q) || S().occupiedNow(q, w.round) || cheb(q, a) > rules::SUBMIT_RANGE) continue; twsQ = q; break; }
        if (twsQ.x < 0) return false;
    }
    // 이번 턴 받을 피해로 죽지 않으면 시작 (피해로는 안 끊김; 스턴 · 죽음만)
    if (p.lethalAll) return false;
    const int chcD = std::max(S().subMin, S().subBase + (int)std::ceil((double)w.me.gems * S().subMult));
    if (policy::CHC_ON && chcDanger(w, chcD) >= w.me.hp) return false;
    if (policy::TAC_ON && (combat::allyCrisis(w, chcD) || combat::joinTargetNow(w))) return false;   /* TAC */   /* CHC: 채널 동안 죽을 자리면 시작 안 함 */
    if (twsQ.x >= 0) { if (!io::moveTo(twsQ)) return false; p.move = false; p.dash = false; p.cell = twsQ; lksLock(w, a); if (!io::submitGems(a)) return true; g_chcStart = w.round; g_chcDur = chcD; return true; }   /* TWS */
    lksLock(w, a);
    if (io::submitGems(a)) { g_chcStart = w.round; g_chcDur = chcD; p.move = false; p.dash = false; p.cell = w.me.pos; return true; }
    if (policy::LK_ON && std::strstr(io::lastError(), "error 125")) {   // LK: 적 잠금
        if (policy::LK2_ON) {   // LK2 · LK3: 잠긴 제단에 바치기 = 잠금 공격 (평타 사거리 안이어야). 사거리 밖이면 한 칸 다가간다
            if (cheb(w.me.pos, a) > std::max(1, w.me.range)) {
                Pos best{-1, -1}; int bd = cheb(w.me.pos, a);
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{w.me.pos.x + dx, w.me.pos.y + dy};
                    if (!S().inb(q) || !S().walkableStatic(q) || io::campBlocksMovement(q) || S().occupiedNow(q, w.round)) continue; const int c = cheb(q, a); if (c < bd) { bd = c; best = q; } }
                if (best.x >= 0) { p.cell = best; p.move = true; p.dash = false; return false; }   // execute 가 걷는다
            }
            mission::blockAltar(M.altar, w.round + policy::LK2_BLOCK);   // 사거리 안인데 못 침 (기절 · 이미 행동) — 잠깐 기다림
        }
    }
    return false;
}

static float g_lastHp = -1.f; static int g_lastHpRound = -1000;
static int s_capSince = -1, s_capGroup = -1;
bool recallWanted(const World& w, const mission::Mission& M, const combat::Plan& p) {   /* PRA: v331 tryRecall 의 결정 그대로 */
    bool capRecall = false;
    if (policy::CR_ON) {   // CR: 점령 칸 위 대기 시간
        const bool onTile = M.kind == mission::M_CAPTURE && M.group >= 0 && mission::stepsToGoal(w.me.pos) == 0;
        if (!onTile) { s_capSince = -1; s_capGroup = -1; }
        else {
            if (s_capGroup != M.group || s_capSince < 0) { s_capGroup = M.group; s_capSince = w.round; }
            if (w.round - s_capSince >= policy::CR_WAIT && cheb(w.me.pos, S().allyCenter) >= policy::CR_HOME_MIN) {
                bool alone = true;
                if (policy::CR_SOLO) for (const auto& a : w.allies) for (const auto& sp : S().groups[(size_t)M.group].spawn) if (a.pos == sp) alone = false;
                capRecall = alone && w.me.gold + S().groups[(size_t)M.group].gold >= policy::CR_GOLD_MIN;   // CRG
            }
        }
    }
    const bool hitLastTurn = g_lastHpRound == w.round - 1 && w.me.hp < g_lastHp - 0.5f;   // 지난 턴 사이에 맞았다 (보이지 않는 가해자 포함)
    g_lastHp = w.me.hp; g_lastHpRound = w.round;
    if (w.me.channeling || w.me.atSpawn) return false;
    bool want = M.kind == mission::M_RECOVER || capRecall;
    if (policy::RC_HOME_LOOP && !want) {   // RC: 집 순환 — 상점 왕복 · 먼 바치기는 걷지 않고 리콜
        const int d0 = mission::stepsToGoal(w.me.pos);
        if (M.kind == mission::M_SHOP && d0 < 999 && d0 >= policy::RC_SHOP_MIN) want = true;
        static int s_rtSince = -1;   // RT: 이 바치기 임무에서 이미 리콜했나
        if (!policy::NA_ON && M.kind == mission::M_TRIBUTE && d0 < 999 && d0 >= policy::RC_TRIBUTE_MIN && s_rtSince != M.since) {   // NA: 끄면 가장 가까운 제단으로 걷기
            int home = 9999; for (const auto& a : S().altars) { const int hs = mission::homeSteps(a); home = std::min(home, hs >= 0 ? hs : cheb(a, S().allyCenter)); }   // RT: 집 거리장 걸음
            if (policy::BH5_CS7_ON ? ((float)home / mission::wkdSpeed(w) + (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + policy::RC_MARGIN < (float)d0 / mission::wkdSpeed(w)) : (home + (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + policy::RC_MARGIN < d0)) {   /* BH5_CS7 */ want = true; s_rtSince = M.since; }
        }
    }
    if (!want && M.kind == mission::M_FARM && M.group >= 0 && M.lastHitRound < w.round - 2) {   /* RCF: 리콜은 탈것 — 집에서 가는 게 더 빠르면 */
        static int s_rfSince = -1;
        const int d0 = mission::stepsToGoal(w.me.pos), hs = mission::homeSteps(S().groups[(size_t)M.group].center);
        if (s_rfSince != M.since && d0 < 999 && hs >= 0 && (policy::BH5_CS7_ON ? ((float)hs / mission::wkdSpeed(w) + (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1 + policy::RC_MARGIN < (float)d0 / mission::wkdSpeed(w)) : (hs + (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1 + policy::RC_MARGIN < d0))) {   /* BH5_CS7 */ want = true; s_rfSince = M.since; }
    }
    if (!want) return false;
    if (w.enemyNear8 || (p.hereDmg >= 0.f ? p.hereDmg : p.dmgTaken) > 0.f || hitLastTurn) return false;   /* RCC: 리콜은 지금 칸에서 */   // 피해로 끊김 → 아무도 칠 수 없는 칸에서만. 맞고 있으면 먼저 걸어 나간다
    return true;
}
bool tryRecall(const World& w, const mission::Mission& M, const combat::Plan& p) { return recallWanted(w, M, p) && io::recall(); }   /* PRA: v331 과 같은 식 */

// 샤드: power 계열 (6샤드 분량). 근접 · 평타 챔피언은 힘, 주문 챔피언 (Cassi · Stellar · Prism · Tavi · Willow · Marcella?) 은 Focus.
// item_type: power_shard_of_strength = 6, power_shard_of_focus = 8, power_shard_of_health = 10
static bool srFocus(int type) {   // SR-M: 샤드 하나가 올리는 턴당 피해 — 힘 성장치 vs 집중 성장치 × 주문 배율 ÷ (쿨다운 + 1)
    const ChampDesign& D = S().designOf(type); if (!D.valid) return false;
    float f = 0.f; for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s]; if (policy::HD_ON && a.kind == rules::AB_DASH && a.modifier > 0.f) { f += a.modifier * (a.radius > 0.f ? policy::AOE_CAMPS_EST : 1.f) / (float)(a.cooldown + 1); continue; }   /* HD: 대시 착지 피해 */ if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_AOE) continue; if (!a.targetsCamps && !(a.kind == rules::AB_AOE && a.targetsGround)) continue; f += a.modifier * (a.kind == rules::AB_AOE ? policy::AOE_CAMPS_EST : 1.f) / (float)(a.cooldown + 1); }
    return D.focusGrow * f > D.strGrow;
}
// S (상위 팀 아이템 순서, 30판): 초반 정글 샤드 5 → 정글 엠블럼 (캠프 피해 −30%), 그 다음 브루저 체력 샤드 5 → 엠블럼 (체력 샤드 하나 = 최대 체력 +hpGrow; 엠블럼 = 체력 5 + 다른 스탯 1 씩), 그 뒤 파워 샤드.
// item_type: jungle_shard str 24 / focus 26, jungle_emblem str 25 / focus 27, bruiser_shard_of_health 40, bruiser_emblem_of_health 41, power str 6 / focus 8
static int g_spent = 0;
static void buyT(int item, int k) { if (k > 0 && io::buyShard(item, k)) g_spent += k * rules::SHARD_COST; }
int lastSpent() { return g_spent; }
static float g_sfFight = 0.f; static int g_sfRound = -1;   // SF3: 최근 300R 중 적이 6칸 안에 있던 비율 (이동 평균)
void shop(const World& w) {
    g_spent = 0;
    if (w.me.alive && w.round != g_sfRound) { g_sfRound = w.round; bool nearE = false; for (const auto& en : w.enemies) if (cheb(en.pos, w.me.pos) <= 6) { nearE = true; break; } g_sfFight += ((nearE ? 1.f : 0.f) - g_sfFight) / 300.f; }   // SF3
    if (policy::LK_ON && (w.me.atSpawn || !w.me.alive) && io::consumableCount(2) == 0) {   // LK: 잠금 하나 (아이템 전에)
        static int s_cost = -1; if (s_cost < 0) s_cost = io::consumableCost(2);
        if (w.me.gold - g_spent >= s_cost && io::buyConsumable(2)) g_spent += s_cost;
    }
    if (policy::TPS_ON && (w.me.atSpawn || !w.me.alive) && mission::tpsWorthBuying(w) && io::consumableCount(0) == 0) {   /* TPS: 쓸 지름길이 있는 맵만 수정 하나 */
        static int s_tc = -1; if (s_tc < 0) s_tc = io::consumableCost(0);
        if (w.me.gold - g_spent >= s_tc && io::buyConsumable(0)) { g_spent += s_tc; mission::tpsNoteBuy(); TPS_LOG("#tps buy %d %d gold=%d", w.round, S().myIndex, w.me.gold); }
    }
    const bool tavi = w.me.type == 11;
    if (!(w.me.atSpawn || !w.me.alive || tavi)) return;
    if (w.me.gold < policy::SHOP_MIN_GOLD) return;
    int n = (w.me.gold - g_spent) / rules::SHARD_COST;   // LK: 잠금 산 뒤 남은 금으로
    if (n <= 0) return;
    if (reference::ITEMS_ALL || (reference::PRISM_ITEMS && w.me.type == reference::PRISM) || (reference::WILLOW_ITEMS && w.me.type == reference::WILLOW)) {   /* TWP 규칙 2: 팀원 아이템 순서 */
        std::vector<reference::ItemStep> steps;int tail=6;
#if TWP_HAS_PLAN
        const int rflags=io::referencePlan().flags;
#else
        const int rflags=0;
#endif
        reference::itemPlan(w.me.type,rflags,steps,tail,w.me.gems);
        // Count completed emblems and held shards; failed buys do not advance.
        for(size_t i=0;i<steps.size();++i){const auto&s=steps[i];int prior=0;for(size_t j=0;j<i;++j)if(steps[j].shard==s.shard)prior+=steps[j].emblems;
            const int owned=io::itemCount(s.shard)+5*io::itemCount(s.shard+1),need=std::max(0,5*(prior+s.emblems)-owned);
            if(need>0){const int k=std::min(n,need),before=g_spent;buyT(s.shard,k);if(g_spent==before)return;n-=k;if(k<need)return;}
        }
        if(n>0)buyT(tail,n);return;
    }
    const bool focus = srFocus(w.me.type) || w.me.type == 7 || (policy::TM_CAF_ON && (w.me.type == reference::CASSI || w.me.type == 9));   /* TM_CAF: 팀원 bot40 */   // SR-M: 룰북 값으로   /* MFO: Marcella 는 집중 (래더 강팀 시전당 피해 118 : 23) */
    {   // S4: v024z 식 아이템 순서
        // 순서: 몬스터 (str 0→1 / focus 2→3) → 브루저 (36→37 / 38→39) → 정글 체력 (28→29) → 브루저 조각
        auto build = [&](int shard, int emblem) { if (n > 0 && io::itemCount(emblem) == 0) { const int need = std::max(0, 5 - io::itemCount(shard)); const int k = std::min(n, need); if (k > 0) { buyT(shard, k); n -= k; } } };
        if (policy::FIB_MODE == 1) {   /* FIB: 상위 팀 엠블럼 순서 (상위 팀 리플레이, 몸 종류별 가장 흔한 순서) — 끝나면 남은 금은 주 스탯 Bruiser */
            int tail = -1;
            switch (w.me.type) {
                case 7: build(56, 57); build(2, 3); build(26, 27); build(40, 41); build(16, 17); tail = 38; break;   /* Marcella: Pro 집중 → Mon 집중 → Jun 집중 → Bru 체력 → Dur 체력 (14/16몸) */
                case 1: build(18, 19); build(48, 49); build(0, 1); build(24, 25); build(16, 17); build(40, 41); tail = 36; break;   /* Vesper: Tre 힘 → Cap 힘 → Mon 힘 → Jun 힘 → Dur 체력 → Bru 체력 */
                case 2: build(58, 59); build(24, 25); build(0, 1); build(64, 65); build(16, 17); tail = 36; break;   /* Vendetta: Pro 체력 → Jun 힘 → Mon 힘 → Hun 체력 → Dur 체력 */
                case 3: build(8, 9); build(14, 15); build(40, 41); tail = 8; break;   /* Cassi: Pow 집중 → Dur 집중 → Bru 체력 */
                case 4: build(58, 59); build(40, 41); build(16, 17); tail = 40; break;   /* Issac: Pro 체력 → Bru 체력 → Dur 체력 */
                case 9: build(58, 59); build(40, 41); build(16, 17); build(8, 9); tail = 8; break;   /* Ren: Pro 체력 → Bru 체력 → Dur 체력 → Pow 집중 */
                case 10: build(0, 1); build(54, 55); build(28, 29); build(16, 17); build(36, 37); tail = 36; break;   /* Varg: Mon 힘 → Pro 힘 → Jun 체력 → Dur 체력 → Bru 힘 */
                case 12: build(58, 59); build(26, 27); build(2, 3); build(64, 65); tail = 38; break;   /* HoR: Pro 체력 → Jun 집중 → Mon 집중 → Hun 체력 */
                default: break;
            }
            if (tail >= 0) { if (n > 0) buyT(tail, n); return; }
        }
        if (policy::VCR_ON && w.me.type == policy::VCR_TYPE) {   /* VCR: 3위 Vesper 순서 */
            if (policy::FVP_ON) build(54, 55);   /* FVP: 덩어리 점령 몫 +25% 를 먼저 (금 눈덩이), 뒤 build(54, 55) 는 이미 있어 건너뜀 */
            if (policy::IPM_ON) { build(0, 1); build(36, 37); build(48, 49); } else { build(48, 49); build(36, 37); } build(40, 41); if (policy::PRO_ON) build(54, 55); else build(18, 19); build(24, 25); build(16, 17); build(0, 1);   /* ITB3: Vesper capture 다음 bruiser 힘 → 체력 */
            if (n > 0) buyT(36, n);
            return;
        }
        if (policy::TWH_ON && (w.me.type == 3 || w.me.type == 4)) { build(40, 41); build(16, 17); }   /* TWH: 랭커 쌍둥이 체력 계열 몫 */
        if (policy::BLD2_ON && w.me.range <= 1) { build(focus ? 56 : 54, focus ? 57 : 55); build(focus ? 26 : 24, focus ? 27 : 25); build(focus ? 2 : 0, focus ? 3 : 1); build(focus ? 38 : 36, focus ? 39 : 37); }   /* BLD2: 랭커 순서 */
        else if (w.me.range <= 1) { build(58, 59);   /* SPRA */   /* PRO: Marcella 첫 엠블럼 prospector 체력 */ build(focus ? 26 : 24, focus ? 27 : 25); build(16, 17); build(focus ? 38 : 36, focus ? 39 : 37); build(focus ? 2 : 0, focus ? 3 : 1); }   /* ITM: 보물 체력 → 정글 주 */   /* SR-R: 맞는 몸은 막는 것부터 */
        else { build(focus ? 56 : 54, focus ? 57 : 55);   /* SPRA */ build(focus ? 26 : 24, focus ? 27 : 25); build(16, 17); build(focus ? 38 : 36, focus ? 39 : 37); build(focus ? 2 : 0, focus ? 3 : 1); }   /* ITM: 보물 주 → 정글 주 */   /* SR-R: 치는 몸은 치는 것부터 */
        bool focusUse = false; { const ChampDesign& D9 = S().designOf(w.me.type); if (D9.valid) for (int s = 0; s < 2; ++s) { const auto& ab = D9.ab[s]; if (ab.modifier > 0.f && (ab.kind == rules::AB_DAMAGE || ab.kind == rules::AB_AOE || ab.kind == rules::AB_CC || ab.kind == rules::AB_SHIELD || ab.kind == rules::AB_HEAL || ab.kind == rules::AB_DASH)) focusUse = true; } }   /* SFX */
        if (n > 0) buyT((focus || (focusUse && g_sfFight >= 0.25f)) ? 38 : 36, n);   // SF3: 교전이 잦은 몸은 집중
    }
}

void lockAltar(const World& w) {   // LK: 잠금 아이템 쓰기
    if (!policy::LK_ON || !w.me.alive || w.me.channeling) return;
    static int s_have = -1, s_haveR = -1000;
    if (w.round - s_haveR >= 10 || s_have < 0) { s_have = io::consumableCount(2); s_haveR = w.round; }
    if (s_have <= 0) return;
    if (!policy::LK_CANCEL) return;
    Pos me = w.me.pos; if (policy::BH2_TR5_ON) { Pos lp; if (io::livePosition(lp)) me = lp; }   /* BH2 TR5: execute 뒤 지금 칸 */
    // LK3: 2칸 안 제단에서 적이 바치는 중이면 잠가서 끊는다 (그 밖엔 잠그지 않음)
    for (int a = 0; a < (int)S().altars.size(); ++a) { const Pos c = S().altars[(size_t)a]; if (cheb(c, me) > 2) continue;
        bool ch = false; for (const auto& e : w.enemies) if ((e.state & rules::ST_SUBMITTING) && cheb(e.pos, c) <= 1) ch = true;
        if (!ch) { for (const auto& e : w.enemies) if (e.alive && e.visible && e.gems > 0 && cheb(e.pos, c) <= 6) ch = true;   /* LKD: 젬 든 적이 다가오는 제단 */
            if (ch) for (const auto& al : w.allies) if (al.visible && al.alive && (al.state & rules::ST_SUBMITTING) && cheb(al.pos, c) <= 1) ch = false; }
        if (!ch) continue;
        std::vector<io::LockSeen> L; io::altarLocks(L); bool locked = false; for (const auto& l : L) if (l.pos == c && l.ally) locked = true;
        if (!locked && io::useConsumable(2, c)) { s_have = 0; return; } }
}

bool combatWard(const World& w){return io::placeLocalVisionWard(w.me.pos);}
void ward(const World& w,const combat::Plan&){
    if(!w.me.alive||w.me.wardCooldown>0||w.me.atSpawn||w.me.channeling)return;
    Pos current;if(io::livePosition(current))io::placeLocalVisionWard(current);
}

}  // namespace nb::economy
