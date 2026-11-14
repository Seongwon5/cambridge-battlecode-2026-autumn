#include <cstdlib>
// main.cpp — nb: 루프 · 턴 순서 · 실행기 · 기록. 설계: notes/newbot_design.md
#include <cstdio>
#include <exception>
#include "io.h"
#include "world.h"
#include "mission.h"
#include "combat.h"
#include "economy.h"
#include "team.h"
#include "policy.h"
#include "special.h"   /* TWP */
#include "log.h"

namespace nb {

void playDraft();

static log::FuelStat g_fuel;
static World W;

// 실행기: 계획을 합법 순서로. 거절 (숨은 장애물) 은 기록하고 넘어간다.
static void execute(const combat::Plan& p) {
    const Me& me = W.me;
    auto attack = [&]() { if (p.atkKind == 1) return io::attackChampion(p.atkHandle); if (p.atkKind == 2) return io::attackCamp(p.atkHandle); return false; };
    auto cast = [&]() {
        if (p.castSlot < 0) return false;
        switch (p.castKind) { case 0: return io::castSelf(p.castSlot); case 1: return io::castChampion(p.castSlot, p.castHandle); case 2: return io::castCamp(p.castSlot, p.castHandle); default: return io::castGround(p.castSlot, p.castPos); }
    };
    bool casted = false;
    if (p.castSlot >= 0 && p.castBeforeMove) casted = cast();
    bool rstPending = false;   /* RST */
    if (p.atkKind && p.atkWhen == 1) {
        attack();
        if (p.resetAttack && p.resetSlot >= 0) { bool al = true; if (policy::BH4_VD1_ON && !p.atk2Kind && p.atkKind == 1) { bool st = true; al = io::championAlive(p.atkHandle, st) && st; }   /* BH4_VD1 */ if (al && io::castSelf(p.resetSlot)) { if (p.atk2Kind) rstPending = true; else attack(); } }
    }
    if (p.dash && p.dashSlot >= 0) { if (!io::castGround(p.dashSlot, p.dashWalk ? p.dashTo : p.cell)) { NB_LINE("#nb dashfail %s", io::lastError());
            if (policy::MFB_ON) combat::noteMoveFail(p.cell);
            if (policy::DFB_ON) { combat::noteDashFail(); Pos bq = me.pos; int bd = cheb(me.pos, p.cell);   /* DFB: 대시 실패면 그쪽으로 한 칸 */
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{me.pos.x + dx, me.pos.y + dy};
                    if (!io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, W.round)) continue;
                    const int c = cheb(q, p.cell); if (c < bd) { bd = c; bq = q; } }
                if (bq != me.pos) io::moveTo(bq); } } else if (p.dashWalk && !io::moveTo(p.cell)) NB_LINE("#nb dwkfail %d,%d %s", p.cell.x, p.cell.y, io::lastError()); }   /* DWK: 대시 뒤 걷기 */
    else if (p.move && p.cell != me.pos) { if (!io::moveTo(p.cell)) { NB_LINE("#nb movefail %d,%d %s", p.cell.x, p.cell.y, io::lastError()); if (policy::MFB_ON && me.canMove) combat::noteMoveFail(p.cell); if (policy::BH4_TN3_ON && policy::MRT_ON && io::lastMoveHidden()) { Pos bq = me.pos; int bd = cheb(me.pos, p.cell); for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{me.pos.x + dx, me.pos.y + dy}; if (q == p.cell || !io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, W.round)) continue; const int c = cheb(q, p.cell); if (c < bd) { bd = c; bq = q; } } if (bq != me.pos) io::moveTo(bq); }   /* MRT (BH4_TN3): 숨은 점유 → 옆 칸 한 번 더 */ } }
    if (p.atkKind && p.atkWhen == 4) { if (p.resetSlot >= 0 && io::castSelf(p.resetSlot)) attack(); }   /* RWA: 걸은 뒤 리셋 → 평타 */
    if (p.atkKind && p.atkWhen == 2) {
        attack();
        if (p.resetAttack && p.resetSlot >= 0) { bool al = true; if (policy::BH4_VD1_ON && !p.atk2Kind && p.atkKind == 1) { bool st = true; al = io::championAlive(p.atkHandle, st) && st; }   /* BH4_VD1 */ if (al && io::castSelf(p.resetSlot)) attack(); }
    }
    if (rstPending) { if (p.atk2Kind == 1) io::attackChampion(p.atk2Handle); else if (p.atk2Kind == 2) io::attackCamp(p.atk2Handle); }   /* RST: 걸은 뒤 B */
    if (p.castSlot >= 0 && !p.castBeforeMove && !casted) cast();
    if (policy::KST_ON && p.atkKind == 2 && !(p.dash && p.dashSlot >= 0) && !(p.move && p.cell != me.pos) && me.canMove) {   /* KST: 막타 뒤 같은 턴 점령 칸 */
        int gk = -1; for (const auto& c : W.camps) if (c.handle == p.atkHandle) { gk = c.group; break; }
        if (gk >= 0 && (size_t)gk < io::S.groups.size()) {
            bool last = true; for (const auto& c : W.camps) if (c.group == gk && c.handle != p.atkHandle && c.hp > 0.f) { last = false; break; }
            const Group& G = io::S.groups[(size_t)gk]; bool onTile = false; for (const auto& sp : G.spawn) if (sp == me.pos) onTile = true;
            if (last && !onTile && (G.gems > 0 || G.gold > 0)) {
                Pos bq{-1, -1}; for (const auto& sp : G.spawn) { if (cheb(sp, me.pos) != 1 || !io::S.inb(sp) || !io::S.walkableStatic(sp) || io::S.occupiedNow(sp, W.round)) continue; bool campHere = false; for (const auto& c : W.camps) if (c.pos == sp && c.handle != p.atkHandle && c.hp > 0.f) campHere = true; if (!campHere) { bq = sp; break; } }
                if (bq.x >= 0) io::moveTo(bq); } } }
}

// OP: 목표 없는 초반 한 걸음 (가장 가까운 값 있는 그룹 쪽)
static bool opStep(const World& w) {
    if (!policy::OP_ON) return false;
    const Pos me = w.me.pos; int bestG = -1, bestC = 1 << 30;
    if (policy::FTM3_ON && mission::M.kind == mission::M_FARM && mission::M.group >= 0) { bestG = mission::M.group; bestC = cheb(io::S.groups[(size_t)bestG].center, me); }   /* FTM3: 임무 그룹 쪽 */
    bool opdG = false;   /* BH2_OP4: 목표 없고 OPD 배정 칸이 있으면 그 칸에 중심이 cheb<3 인 무리 (mission OPD 와 같은 조건) */
    if (policy::BH2_OP4_ON && bestG < 0 && mission::M.kind == mission::M_NONE) { Pos oc; if (io::opdCell(oc)) { int bd = 3;
        for (int gi = 0; gi < (int)io::S.groups.size(); ++gi) { const Group& G = io::S.groups[(size_t)gi]; const int dd = cheb(G.center, oc); if (dd < bd) { bd = dd; bestG = gi; } }
        if (bestG >= 0 && (io::S.groups[(size_t)bestG].state == 0 || io::S.groups[(size_t)bestG].state == 1)) { opdG = true; bestC = cheb(io::S.groups[(size_t)bestG].center, me); } else bestG = -1; } }
    for (int gi = 0; gi < (int)io::S.groups.size() && !((policy::FTM3_ON || opdG) && bestG >= 0); ++gi) {
        const Group& G = io::S.groups[(size_t)gi];
        if ((G.gems <= 0 && G.gold <= 0) || G.mono) continue;   // ED
        const int c = cheb(G.center, me); if (c < bestC) { bestC = c; bestG = gi; }
    }
    if (bestG < 0 || bestC <= io::S.groups[(size_t)bestG].rangeMax + 1) return false;   // ED
    const Pos t = io::S.groups[(size_t)bestG].center;
    Pos cand[8]; int cc[8]; int n = 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue; const Pos q{me.x + dx, me.y + dy};
        if (!io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, w.round)) continue;
        const int c = cheb(q, t); if (c >= bestC) continue;
        cand[n] = q; cc[n] = c; ++n;
    }
    for (int k = 0; k < n; ++k) {   // 가까운 순 (n ≤ 8, 선택 정렬)
        int b = k; for (int j = k + 1; j < n; ++j) if (cc[j] < cc[b]) b = j;
        std::swap(cand[k], cand[b]); std::swap(cc[k], cc[b]);
        if (io::moveTo(cand[k])) return true;
    }
    return false;
}

static bool g_praArmed = false; static int g_praArmR = -1;   /* PRA: 지난 턴 끝에 내가 리콜을 걸어 두었나 · 건 라운드 */
static bool g_praWant = false;   /* PRA: 이번 턴 흐름을 끝까지 돌았다 → main 루프가 우선순위 뒤 마지막 행동으로 건다 */
static void playTurn() {
    g_praWant = false;   /* PRA */
    const long long f0 = io::fuel();
    const int spanR0 = io::round();   // 넘침 표시: 턴 시작 라운드
    bool r1mSensed = false;   /* R1M */
    if (!io::S.ready) {
        { static bool s_r1raw = false; if (policy::R1M_ON && !s_r1raw) { s_r1raw = true; Pos lp; if (io::livePosition(lp)) (void)io::rawStep(lp); } }   /* R1M: 첫 턴 맨 앞에 원자료 한 칸 (큰 맵은 맵 읽기가 예산을 넘음) */
        io::initStatic();
        if (!io::S.ready) {   /* R1M: 준비 전 턴도 읽은 무리 쪽으로 한 걸음 */
            if (policy::R1M_ON && io::fuel() > 6000) { Pos lp; if (io::livePosition(lp)) { W.me.pos = lp; W.round = io::round(); const bool mv = !io::S.groups.empty() && opStep(W); if (!mv) (void)io::rawStep(lp); } }   /* R1M: 무리 표 없으면 원자료로 */
            return; }
        team::init();
        if (io::fuel() > 10LL * io::S.W * io::S.H + 20000 && io::prewarmWalls(0)) mission::buildHomeEarly();   /* HFI: 첫 턴에 집 거리장 */
        if (policy::FTM3_ON) { if (io::fuel() > 60000) { io::sense(W); mission::warmMe(W); r1mSensed = true; } if (!(policy::R1M_ON && r1mSensed && io::fuel() >= policy::R1M_FLOOR)) { if (policy::R1M_ON && r1mSensed && io::fuel() > 6000 && mission::M.kind == mission::M_NONE) (void)opStep(W); return; } }   /* R1M: 초기화 끝 턴도 한 걸음 */   /* R1M: 남으면 이번 턴 그대로 */   /* MFW */   /* FTM3: 초기화 턴은 스폰에 그대로 (고르기 입력을 v85 와 같게), 고르기는 다음 턴 (연료 가득) */
        if (io::fuel() < 75000) return;
    }
    if (!r1mSensed) io::sense(W);   /* R1M */
    bool praHeld = false;   /* PRA: 걸어 둔 리콜이 아직 이어지면, 이번 턴은 채널 아님 · 움직일 수 있음으로 보고 v331 흐름을 돌린 뒤 둘지 취소할지 정한다 */
    if (policy::PRA_ON && g_praArmed) {
        if (W.me.alive && (W.me.state & rules::ST_RECALLING) && !(W.me.state & (rules::ST_SUBMITTING | rules::ST_TELEPORTING))) {
            praHeld = true;
            if (W.round == g_praArmR + 1) { const int rem = io::selfChannelLeft(); if (rem >= 0) io::g_praD = std::clamp(io::recallLen() - rem, 0, 1); }   /* PRA: 건 턴 끝에서 줄어든 값 배움 */
            W.me.state &= ~rules::ST_RECALLING; W.me.state |= rules::ST_CAN_MOVE | rules::ST_CAN_ATTACK | rules::ST_CAN_CAST;
            W.me.channeling = false; W.me.canMove = W.me.canAttack = W.me.canCast = true;   // 피해 · 스턴이 있었으면 리콜이 이미 끊겼다
        } else g_praArmed = false;   // 끊겼거나 (피해 · 스턴 · 죽음) 집에 닿음
    }
    { static bool s_dn = false; if (policy::DSH_ON && !s_dn && W.me.type >= 0) { s_dn = true; const ChampDesign& D = io::S.designOf(W.me.type);   /* DSH: 내 설계에 대시 (사거리 ≥ 2) 가 있으면 다리 길찾기 */
        if (D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH && D.ab[s].range >= 2.f) { io::S.dashNav = true; io::S.dashSlot = s; io::S.dashRange = (int)D.ab[s].range; } } }
    if (!io::wallsReady()) io::prewarmWalls(policy::ZO_WALL_FUEL);   // ZO
    if (policy::DSH_ON && io::S.dashNav && io::wallsReady() && !io::bridgesReady()) io::bridgeStep(policy::ZO_COMBAT_RES);   /* BRF2: 대시 다리 표를 먼저 */
    const int r = W.round;
    if (!W.me.alive) {
        mission::kbNoteDead();
        if (io::fuel() > policy::TGC_SHOP) economy::shop(W);   /* TGC */
        if (io::fuel() > policy::TGC_WRITE) team::write(W, -1, 0, 3);
        mission::M.kind = mission::M_NONE;
        mission::prewarmGF(policy::LC_PREWARM_FLOOR);
        mission::prewarmTiles(policy::LC_PREWARM_FLOOR);
        if (policy::TPS_ON) mission::tpsPrewarm(policy::LC_PREWARM_FLOOR);   /* TPS */
        g_fuel.add(f0 - io::fuel()); return;
    }
    team::read(W);
    auto pitReach = [](int type, int rng) { int r = std::max(1, rng); const ChampDesign& D = io::S.designOf(type); if (D.valid) for (int k = 0; k < 2; ++k) r = std::max(r, (int)D.ab[k].range + (int)D.ab[k].radius); return r + 1; };   /* PIT3: 걸어 한 칸 + 사거리 */
    if (policy::PIT_ON) {   /* PIT: 건너편 (걸음 하한 ≥ 체비 + 6) 이고 6칸 이상인 적은 이번 턴 판단에서 뺌 */
        const int hm = mission::homeSteps(W.me.pos);
        if (hm >= 0) { size_t k = 0; for (size_t i = 0; i < W.enemies.size(); ++i) { const Unit& e = W.enemies[i]; const int c = cheb(e.pos, W.me.pos); bool drop = false;
            if (c >= policy::PIT_MIN_CHEB && (!policy::PIT_REACH || c > pitReach(W.me.type, W.me.range) && c > pitReach(e.type, e.range))) { const int he = mission::homeSteps(e.pos); if (he >= 0 && std::abs(hm - he) >= c + policy::PIT_EXTRA) drop = true; }
            if (!drop) { if (k != i) W.enemies[k] = W.enemies[i]; ++k; } } W.enemies.resize(k); }
    }
    if (policy::CHC_ON && W.me.channeling && (W.me.state & rules::ST_SUBMITTING)) {   /* CHC: 남은 채널 동안 죽을 피해가 오면 끊고 이번 턴은 평소대로 */
        const int left = std::max(1, nb::economy::g_chcStart + nb::economy::g_chcDur - r);
        if (nb::economy::chcDanger(W, left) >= W.me.hp && nb::io::cancelSubmit()) { W.me.channeling = false; W.me.state &= ~rules::ST_SUBMITTING; io::sense(W);   /* CHS: 끊은 턴은 다시 감지 (움직임 · 공격 가능) */ NB_LINE("#chc %d %d cancel left=%d hp=%.0f", r, io::S.myIndex, left, W.me.hp); }
    }
    if (W.me.channeling) {   // 채널은 그대로 (바치기 · 리콜 · 텔레포트). 임무 관리자는 다음 턴에 완료를 본다
        team::write(W, mission::M.group >= 0 ? mission::M.group : -1, mission::claimKind(), 0, -1, policy::BH2_SP3_ON ? W.me.gems : -1);   /* BH2_SP3 */
        NB_LINE("#nb %d %d %s channel", r, io::S.myIndex, mission::name(mission::M.kind));
        mission::prewarmGF(policy::LC_PREWARM_FLOOR); mission::prewarmTiles(policy::LC_PREWARM_FLOOR);
        g_fuel.add(f0 - io::fuel()); return;
    }
    if (W.me.atSpawn && W.me.gold >= policy::SHOP_MIN_GOLD) { economy::shop(W); W.me.gold -= economy::lastSpent(); }   // SB: 결정 전에 사고 금을 줄인다
    { static bool s_og = true; if (s_og) { s_og = false; mission::prewarmGF(policy::OG_FLOOR); } }   // OG
    const long long fS = io::fuel();
    if (policy::GDS_ON) combat::gdsPrep(W);   /* GDS: 연료 가득할 때 챔피언 대시 미리 셈 */
    mission::noteBody(W);
    mission::g_zrEn = W.enemyNear8;   /* ZOR: 이번 턴 적 곁 여부 */
    mission::update(W);
    if (io::round() != spanR0) return;
    const long long fM = io::fuel();
    combat::Plan p; p.cell = W.me.pos; p.cut = true;   /* FGR: 연료가 모자라면 전투 계산 없이 빈 계획 (그 턴만 서 있음) */
    { bool enNear = false; for (const auto& e : W.enemies) if (e.alive && cheb(e.pos, W.me.pos) <= 8) { enNear = true; break; } for (const auto& c : W.camps) if (!enNear && c.hp > 0.f && c.str > 0.f && cheb(c.pos, W.me.pos) <= 6 && c.group >= 0 && io::S.groups[(size_t)c.group].awakeUntil >= r) { enNear = true; break; }   /* FUELC: 깬 캠프만 */
      bool fwk = false;   /* FWK */
      const bool fwkKind = mission::M.kind == mission::M_FARM || (policy::FWK2_ON && (mission::M.kind == mission::M_TRIBUTE || mission::M.kind == mission::M_SHOP || (mission::M.kind == mission::M_RECOVER && !mission::M.retreat)));   /* FWK2 */
      if (policy::FWK_ON && fwkKind && W.enemies.empty() && !W.enemyNear8 && !enNear) { bool campN = false; for (const auto& c : W.camps) if (c.hp > 0.f && cheb(c.pos, W.me.pos) <= 6) { campN = true; break; }
          bool dashR = false; const ChampDesign& Dk = io::S.designOf(W.me.type); if (W.me.canCast && Dk.valid) for (int s = 0; s < 2; ++s) if (Dk.ab[s].kind == rules::AB_DASH && W.me.cooldown[s] == 0) dashR = true;
          fwk = !campN && !dashR; }
      if (!fwk && (enNear || io::fuel() >= policy::FGR_COMBAT)) p = combat::choose(W, mission::M);
      if (fwk && policy::FWK2_ON && mission::M.kind != mission::M_FARM && W.me.canMove) {   /* FWK2: 농사 아닌 임무는 여기서 최단 걸음 */
          const int q0 = mission::stepsToGoal(W.me.pos);
          if (q0 > 0 && q0 < 999) { Pos bq{-1, -1}; int bd = q0;
              for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{W.me.pos.x + dx, W.me.pos.y + dy};
                  if (!io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, r)) continue;
                  const int dq = mission::stepsToGoal(q); if (dq < bd) { bd = dq; bq = q; } }
              if (bq.x >= 0) { p.cell = bq; p.move = true; p.cut = false; } } } }
    if ((p.cut || (W.enemies.empty() && p.castSlot < 0 && p.value == 0.f && p.cell.x == W.me.pos.x && p.cell.y == W.me.pos.y)) && p.atkKind == 0 && mission::M.kind == mission::M_FARM && mission::M.group >= 0) {   /* CUTA2 */ const CampUnit* cb = nullptr;   /* CUTA: 계산 못 한 턴의 싼 기본 행동 */
      for (const auto& c : W.camps) if (c.group == mission::M.group && c.hp > 0.f && cheb(c.pos, W.me.pos) <= std::max(1, W.me.range) && (!cb || c.hp < cb->hp)) cb = &c;
      if (cb) { p.atkKind = 2; p.atkHandle = cb->handle; p.atkWhen = 1; } }
    if (policy::INR_ON && W.me.canAttack && p.atkKind == 0 && p.castSlot < 0 && !W.enemies.empty()) {   /* INR: 빈 계획인데 지금 칸 사거리 안 적 챔피언 → 이동 전 평타 (원거리는 걷는 턴 제외: 친 뒤 못 걸음) */
      const bool walks = (p.move && p.cell != W.me.pos) || (p.dash && p.dashWalk);
      if (W.me.range <= 1 || !walks) { const Unit* tg = nullptr; const int rg = std::max(1, W.me.range);
        for (const auto& e : W.enemies) { if (!e.alive || !e.visible || cheb(e.pos, W.me.pos) > rg) continue; bool sp = false; for (const auto& s : io::S.enemySpawn) if (s == e.pos) { sp = true; break; } if (sp) continue;
          if (!tg || e.hp < tg->hp || (e.hp == tg->hp && e.handle < tg->handle)) tg = &e; }
        if (tg) { p.atkKind = 1; p.atkHandle = tg->handle; p.atkWhen = 1; NB_LINE("#inr %d %d tgt=%u hp=%.0f cut=%d leth=%d", W.round, io::S.myIndex, tg->handle, tg->hp, (int)p.cut, (int)p.lethalAll); } } }
    if (io::round() != spanR0) return;
    if (policy::GDS_ON) combat::gdsApply(W, mission::M, p);   /* GDS */
    const long long fC = io::fuel();
    if (policy::CRS_ON && W.enemyNear8 && p.nCells > 0) { const long long c = fM - fC; mission::g_crsCb = std::max(c, mission::g_crsCb * 9 / 10); }   /* CRS: 적 곁 전투 연료 */
    const long long noCost[3] = {};
    const auto* goalCost = mission::dGoal.round == r ? mission::dGoal.lastCost : noCost;
    const auto* meCost = mission::dMe.round == r ? mission::dMe.lastCost : noCost;
    NB_LINE("#nbf %d %d sense=%lld mission=%lld combat=%lld cells=%lld dmg=%lld combos=%lld goalfld=%lld,%lld,%lld mefld=%lld,%lld,%lld", r, io::S.myIndex, f0 - fS, fS - fM, fM - fC, p.fuelCells, p.fuelDmg, p.fuelCombos,
              goalCost[0], goalCost[1], goalCost[2], meCost[0], meCost[1], meCost[2]);
    NB_LINE("#nav %d %d me=%d goal=%d query=%d change=%d open=%d version=%llu fieldVersion=%llu dynamic=%d defer=%d rescue=%d reason=%d",
              r, io::S.myIndex, io::nav.meBuilds, io::nav.goalBuilds, io::nav.queries, io::nav.changedTiles, io::S.openCampTiles,
              (unsigned long long)io::S.campVersion, (unsigned long long)mission::dGoal.campVersion, io::nav.dynamicPicks, io::nav.deferred, io::nav.rescued, io::nav.goalReason);
    const char* act = "move";
    if (policy::TPS_ON && W.enemies.empty() && p.atkKind == 0 && p.castSlot < 0) { Pos tq; if (mission::tpsMoveCell(W, tq)) { p.cell = tq; p.move = true; p.dash = false; } }   /* TPS: 지름길 출발 칸으로 */
    Pos tpsB{-1, -1}; int tpsS = 0; float tpsV = 0.f; bool tpsGo = false;   /* TPS */
    if (policy::DSH_ON && io::S.dashNav && p.atkKind == 0 && p.castSlot < 0 && !p.dash && W.enemies.empty() && W.me.cooldown[io::S.dashSlot] == 0
        && (mission::M.kind == mission::M_FARM || mission::M.kind == mission::M_CAPTURE || mission::M.kind == mission::M_TRIBUTE)) {   /* DSH: 걸어서 진전 없으면 다리 너머로 대시 */
        const int d0 = mission::stepsToGoal(W.me.pos);
        if (d0 > 0 && d0 < 999 && (p.cell == W.me.pos || mission::stepsToGoal(p.cell) >= d0)) { Pos best{-1, -1}; int bd = d0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos h{W.me.pos.x + dx, W.me.pos.y + dy};
                if (!io::S.inb(h) || !io::S.bridge.size() || !io::S.bridge[(size_t)io::S.idx(h)]) continue; const Pos L{W.me.pos.x + 2 * dx, W.me.pos.y + 2 * dy};
                if (!io::S.inb(L) || !io::S.walkableStatic(L) || io::campBlocksMovement(L) || io::S.occupiedNow(L, r)) continue;
                const int dl = mission::stepsToGoal(L); if (dl < bd) { bd = dl; best = L; } }
            if (best.x >= 0) { p.dash = true; p.dashSlot = io::S.dashSlot; p.dashWalk = false; p.cell = best; p.move = false; NB_LINE("#dsh %d %d to=%d,%d d0=%d d1=%d", r, io::S.myIndex, best.x, best.y, d0, bd); } } }
    bool praKeep = false;   /* PRA: v331 리콜 결정이 리콜이면 걸어 둔 리콜을 그대로 (한 턴 번 셈), 아니면 첫 행동으로 취소 */
    if (praHeld) {
        if (economy::recallWanted(W, mission::M, p)) { praKeep = true; g_praArmed = false; }
        else if (io::praCancel()) { g_praArmed = false; io::refreshMeState(W.me); }
        else praKeep = true;   // 취소 실패: 이번 턴은 채널, 다음 턴 다시 가름 (g_praArmed 유지)
        if (praKeep) { W.me.channeling = true; W.me.state |= rules::ST_RECALLING; }
    }
    if (io::fuel() > 12000) special::planHeal(W, p);   /* TWP 규칙 6: Willow 치유 대상 (계획에 시전) */
    if (praKeep) act = "recall";   /* PRA */
    else if (economy::tryTribute(W, mission::M, p)) act = "tribute";
    else if (policy::TPS_ON && p.atkKind == 0 && p.castSlot < 0 && !p.lethalAll && mission::tpsPick(W, tpsB, tpsS, tpsV) && economy::chcDanger(W, mission::tpsChannel()) < W.me.hp && io::useConsumable(0, tpsB)) { act = "tp"; tpsGo = true; mission::tpsUsed(W, tpsB, tpsS, tpsV); }   /* TPS: 채널 위험은 CHC 잣대 */
    else if (!praHeld && economy::tryRecall(W, mission::M, p)) act = "recall";   /* PRA: 걸어 둔 턴은 위에서 이미 물음 */
    else {
        if (policy::OPQ_ON && mission::M.kind == mission::M_FARM && W.enemies.empty() && !W.enemyNear8 && p.atkKind == 0 && p.castSlot < 0 && !p.dash && !p.lethalAll && !mission::M.retreat && io::fuel() > 15000 && !(mission::M.group >= 0 && mission::tfpHold(W, mission::M.group))) {   /* OPQ: 위협 없는 이동은 최단 걸음 */
            const int q0 = mission::stepsToGoal(W.me.pos);
            if (q0 >= 2 && q0 < 999) { const int qp = (p.cell == W.me.pos) ? q0 : mission::stepsToGoal(p.cell);
                if (qp >= q0) { Pos bq{-1, -1}; int bd = qp;
                    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{W.me.pos.x + dx, W.me.pos.y + dy};
                        if (!io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, r)) continue;
                        bool hot = false; for (const auto& k : W.camps) if (k.group >= 0 && io::S.groups[(size_t)k.group].awakeUntil >= r && cheb(k.pos, q) <= 4) { hot = true; break; } if (hot) continue;
                        const int dq = mission::stepsToGoal(q); if (dq < bd) { bd = dq; bq = q; } }
                    if (bq.x >= 0) { p.cell = bq; p.move = true; p.dash = false; p.dashWalk = false; } } } }
        execute(p);
        if (policy::OP_ON && ((r <= policy::OP_ROUNDS && mission::M.kind == mission::M_NONE) || (policy::FTM3_ON && (policy::OPQ_ON || r <= policy::OP_ROUNDS) && mission::M.kind == mission::M_FARM && mission::stepsToGoal(W.me.pos) >= 999 && W.enemies.empty())) && p.cell == W.me.pos && !p.atkKind && p.castSlot < 0) opStep(W);   // OP /* FTM3 */ /* OPQ: 목표 칸 모름 + 멈춤은 라운드 제한 없이 */
        if (mission::M.kind == mission::M_FARM && W.enemies.empty() && p.nCells == 0 && p.cell == W.me.pos && !p.atkKind && p.castSlot < 0 && io::fuel() > 15000) {   /* FBS2: 빈 계획 → 거리장 한 걸음 */   /* FBSG: 잰 최대 ~12k + 여유 */
            const int d0 = mission::stepsToGoal(W.me.pos);
            if (d0 > 0 && d0 < 999) { Pos bq{-1, -1}; int bd = d0;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{W.me.pos.x + dx, W.me.pos.y + dy};
                    if (!io::S.inb(q) || !io::S.walkableStatic(q) || io::campBlocksMovement(q) || io::S.occupiedNow(q, r)) continue;
                    bool hot = false; for (const auto& k : W.camps) if (k.group >= 0 && io::S.groups[(size_t)k.group].awakeUntil >= r && cheb(k.pos, q) <= 4) { hot = true; break; } if (hot) continue;
                    const int dq = mission::stepsToGoal(q); if (dq < bd) { bd = dq; bq = q; } }
                if (bq.x >= 0) io::moveTo(bq); } }
        if (p.atkKind == 2 || (p.castSlot >= 0 && p.castKind == 2)) {   // 캠프를 쳤다 → 그 그룹은 깼다 (다음 라운드부터 친다)
            mission::M.lastHitRound = r;
            const std::uint32_t h = p.atkKind == 2 ? p.atkHandle : p.castHandle;
            for (auto& G : io::S.groups) for (auto ch : G.campHandle) if (ch == h) G.awakeUntil = std::max(G.awakeUntil, r + rules::CAMP_SLEEP_ROUNDS + 1);
        }
    }
    if (io::round() == spanR0) special::healNow(W);   /* TWP 규칙 6: 턴 안에 바로 치유 시도 */
    if (policy::TPS_ON && !tpsGo && mission::tpsAtA(W)) mission::tpsDrop();   /* TPS: 출발 칸에서 못 뛰면 길 버림 */
    if (io::fuel() > policy::TGC_SHOP) economy::shop(W);   /* TGC */
    if (io::fuel() > policy::TGC_WARD) economy::ward(W, p);
    if (io::fuel() > policy::TGC_LOCK) economy::lockAltar(W);
    if (io::fuel() > policy::TGC_WRITE) team::write(W, mission::M.group >= 0 ? mission::M.group : -1, mission::claimKind(), mission::M.kind == mission::M_RECOVER ? 1 : 0, mission::helpRequest(), W.me.gems, -1);   /* FZN: 싸움 알림 읽는 곳 없음 → 담당 칸을 밀지 않음 */
    if (policy::PRA_ON && io::round() == spanR0) g_praWant = true;   /* PRA: 걸기는 main 루프에서 updatePriority 뒤 (set_priority 가 리콜 중 막히지 않게 · Willow 우선순위가 자기 리콜을 보지 않게) */
    if (policy::TM_CAMP_ON && io::round() == spanR0) mission::prewarmCampTerritory(policy::ZO_COMBAT_RES);   /* TM_CAMP: 땅 걸음장 (다 될 때까지 계수 1) */
    if (policy::ZOR_ON && !W.enemyNear8 && io::round() == spanR0) { const long long c = fM - io::fuel(); mission::g_zrCalm = std::max(c, mission::g_zrCalm * 9 / 10); }   /* ZOR: 조용한 턴 전투 + 실행 + 뒤 작업 */
    if (io::round() == spanR0 && io::fuel() > policy::LC_PREWARM_FLOOR + 3000) {
        mission::prewarmGF(policy::LC_PREWARM_FLOOR);
        if (io::round() == spanR0) mission::prewarmTiles(policy::LC_PREWARM_FLOOR);
        if (policy::TPS_ON && io::round() == spanR0) mission::tpsPrewarm(policy::LC_PREWARM_FLOOR);   /* TPS */
        mission::tbaPrewarm(W, policy::LC_PREWARM_FLOOR);   /* TBA v11 */
    }
    const long long used = f0 - io::fuel();
    g_fuel.add(used);
    NB_LINE("#nb %d %d %s:%d at=%d,%d gems=%d hp=%.0f act=%s cell=%d,%d atk=%d cast=%d val=%.2f dmg=%.0f cells=%d combos=%d why=%s d0=%d claims=%d slot=%d en=%zu cp=%zu fuel=%lld",
              r, io::S.myIndex, mission::name(mission::M.kind), mission::M.kind == mission::M_TRIBUTE ? mission::M.altar : mission::M.group,
              W.me.pos.x, W.me.pos.y, W.me.gems, W.me.hp, act, p.cell.x, p.cell.y, p.atkKind, p.castSlot, p.value, p.dmgTaken, p.nCells, p.nCombos, mission::M.why,
              mission::stepsToGoal(W.me.pos), mission::M.group >= 0 ? team::claimsOn(mission::M.group) : -1, team::T.slot, W.enemies.size(), W.camps.size(), used);
    if (log::ON && mission::M.group >= 0) { const Group& G = io::S.groups[(size_t)mission::M.group]; NB_LINE("#nbg %d %d g=%d state=%d stateR=%d resp=%d center=%d,%d hp=%.0f str=%.0f awake=%d", r, io::S.myIndex, mission::M.group, G.state, G.stateRound, G.respawnUntil, G.center.x, G.center.y, G.hpTotal, G.strMax, G.awakeUntil >= r); }
    if (r % policy::FUEL_LOG_EVERY == 0 && g_fuel.n > 0 && io::fuel() > 5000) { log::always("#fuel %d %d avg=%lld max=%lld n=%d", r, io::S.myIndex, g_fuel.sum / g_fuel.n, g_fuel.mx, g_fuel.n); g_fuel.reset(); }
}

}  // namespace nb

int main() {
    while (true) {
        const bool draft = nb::io::isDraft();
        const int roundBefore = draft ? -1 : nb::io::round();
        try { if (draft) nb::playDraft(); else nb::playTurn(); }
        catch (const std::exception& e) { std::printf("#nb error %s\n", e.what()); }
        catch (...) { std::printf("#nb error unknown\n"); }
        if (!draft && nb::io::round() != roundBefore) std::printf("#span %d->%d\n", roundBefore, nb::io::round());
        if (!draft && nb::io::round() == roundBefore && nb::io::fuel() > 2000) nb::io::updatePriority(nb::W);   /* PRI: 팀원 bot25 규칙 7 */
        if (nb::policy::PRA_ON && !draft && nb::g_praWant && nb::io::round() == roundBefore && nb::io::fuel() > nb::policy::TGC_WRITE && nb::io::praArm()) { nb::g_praArmed = true; nb::g_praArmR = roundBefore; }   /* PRA: 턴의 정말 마지막 행동으로 리콜 걸어 두기 */
        nb::io::endTurn();
    }
}
