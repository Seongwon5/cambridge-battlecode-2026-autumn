#include <cmath>
#include "io.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <cstdio>
#include "mission.h"
#include "special.h"   /* TWP */
#include "special_rules.h"   /* TWP */
#include "reference_config.h"   /* TWP */
#include "policy.h"
#include "camp_commitment.h"   /* TM_CAMP */
#include "team.h"
#include "log.h"
#include "combat.h"   /* LFR */
namespace nb::combat { int lastChampHit(); }   /* HYS */
#include "combat.h"

namespace nb::mission {
bool g_zrEn = true; long long g_zrCalm = policy::ZO_COMBAT_RES;   /* ZOR */
static inline long long zres() { return (policy::ZOR_ON && !g_zrEn) ? std::min(policy::ZO_COMBAT_RES, g_zrCalm) : policy::ZO_COMBAT_RES; }   /* ZOR */

Mission M;
io::Field dGoal, dMe;
static int g_goalTag = 0;      // 목표가 바뀌면 +1 → 거리장 다시
static int g_lastGoalRound = -1000;

const char* name(Kind k) { static const char* N[] = {"none", "farm", "capture", "tribute", "recover", "shop", "escort"}; return N[(int)k]; }
int claimKind() { return M.kind == M_FARM ? team::CL_FARM : M.kind == M_CAPTURE ? team::CL_CAPTURE : M.kind == M_TRIBUTE ? team::CL_TRIBUTE : 0; }

static const Static& S() { return io::S; }

// 그룹 주변 칸: 사거리 안에서 칠 수 있는 칸 (살아 있는 캠프 스폰 칸 둘레, 사거리 r). 목표 거리 0 칸.
// 시선 근사: from 과 to 사이 (양끝 제외) 의 칸에 벽 · 덤불이 있으면 안 보인다 (덤불은 from 이 덤불 안이면 같은 덤불로 보고 통과). 사거리 ≤ 3 이라 선 위 칸 ≤ 2개
static bool losClear(Pos from, Pos to) {
    const int n = cheb(from, to); if (n <= 1) return true;
    const bool fromBush = S().tile[(size_t)S().idx(from)] & T_BUSH;
    for (int i = 1; i < n; ++i) {
        const Pos q{from.x + ((to.x - from.x) * i * 2 + (to.x > from.x ? n : -n)) / (2 * n), from.y + ((to.y - from.y) * i * 2 + (to.y > from.y ? n : -n)) / (2 * n)};
        if (!S().inb(q)) return false;
        const std::uint8_t t = S().tile[(size_t)S().idx(q)];
        if (t & T_WALL) return false;
        if ((t & T_BUSH) && !fromBush) return false;
    }
    return true;
}
// LC: 스폰 칸 둘레의 정적 공격 칸 후보 (걸을 수 있음 · 캠프 칸 아님 · 시선, 덤불 캠프는 같은 덤불 칸만) — 스폰 · 사거리마다 한 번 (attackTilesOf 의 정적 거름)
static std::unordered_map<std::uint32_t, std::vector<std::uint16_t>> s_lc;
static long long g_lcCold = 0;   // 이번 계산에서 처음 세느라 쓴 연료 (목표 칸 비용 추정에서 뺀다)
static inline std::uint32_t lcKey(Pos sp, int R) { return ((std::uint32_t)S().idx(sp) << 5) | (std::uint32_t)std::min(R, 31); }
static double s_lcRate = 700.0;   // 상자 칸 하나 처음 세기 연료 (감쇠 최댓값으로 잰다)
static long long lcEstimate(int R) { return (long long)((double)((2 * R + 1) * (2 * R + 1)) * s_lcRate) + 3000; }
static const std::vector<std::uint16_t>& lcList(Pos sp, int R) {
    const std::uint32_t key = lcKey(sp, R);
    auto it = s_lc.find(key); if (it != s_lc.end()) return it->second;
    const long long f0 = io::fuel();
    std::vector<std::uint16_t> v;
    const bool inBush = S().tile[(size_t)S().idx(sp)] & T_BUSH;
    for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) {
        const Pos p{sp.x + dx, sp.y + dy};
        if (!S().inb(p) || !S().walkableStatic(p)) continue;
        const size_t pi = (size_t)S().idx(p);
        if (S().tile[pi] & T_CAMP) continue;
        if (inBush && !(S().tile[pi] & T_BUSH)) continue;
        if (!losClear(p, sp)) continue;
        v.push_back((std::uint16_t)pi);
    }
    { const long long c = f0 - io::fuel(); g_lcCold += c; const double box = (double)((2 * R + 1) * (2 * R + 1)); s_lcRate = std::max((double)(c - 3000) / box, s_lcRate * 0.98); if (s_lcRate < 200.0) s_lcRate = 200.0; }
    return s_lc.emplace(key, std::move(v)).first->second;
}
static inline bool lcHas(Pos sp, int R) { return s_lc.count(lcKey(sp, R)) > 0; }
static const std::vector<std::uint16_t>* lcTry(Pos sp, int R) {   // 목표 칸 계산용: 처음 세기에 연료가 모자라면 nullptr
    if (!lcHas(sp, R) && io::fuel() < lcEstimate(R) + policy::LC_COLD_RES) return nullptr;
    return &lcList(sp, R);
}
static inline Pos lcPos(std::uint16_t pi) { return Pos{(int)pi % S().W, (int)pi / S().W}; }
// TH: 이 적이 나를 위협하나 (닿는 거리 안이거나 다가오는 중)
bool threatening(const World& w, const Unit& e) {
    const int dist = cheb(e.pos, w.me.pos);
    if (!policy::TH_ON) return dist <= policy::RETREAT_ENEMY_R;
    if (dist > policy::RETREAT_ENEMY_R) return false;
    struct FsTh { std::uint32_t h; Pos ep, mp; int ty, rg, c0, c1; bool v; }; static FsTh s_fsTh[16]; static int s_fsThN = 0, s_fsThR = -1;   /* FS_MIS3 */
    if (policy::FS_MIS3_ON) { if (s_fsThR != w.round) { s_fsThR = w.round; s_fsThN = 0; }
        for (int i = 0; i < s_fsThN; ++i) { const FsTh& q = s_fsTh[i]; if (q.h == e.handle && q.ep == e.pos && q.mp == w.me.pos && q.ty == e.type && q.rg == e.range && q.c0 == e.cooldown[0] && q.c1 == e.cooldown[1]) return q.v; } }
    static int s_round = -1, s_prevR = -1000, s_prev2R = -1000; static std::vector<std::pair<std::uint32_t, int>> s_prev, s_cur, s_prev2;
    if (s_round != w.round) { s_prev2.swap(s_prev); s_prev.swap(s_cur); s_cur.clear(); s_prev2R = s_prevR; s_prevR = s_round; s_round = w.round; }   /* BUG1: 각 목록의 라운드도 남김 */
    int prevD = -1; if (!policy::BUG1_ON || s_prevR == w.round - 1) for (const auto& q : s_prev) if (q.first == e.handle) prevD = q.second;   /* BUG1 */
    int prev2D = -1; if (!policy::BUG1_ON || s_prev2R == w.round - 2) for (const auto& q : s_prev2) if (q.first == e.handle) prev2D = q.second;   /* TH2 */   /* BUG1 */
    bool have = false; for (const auto& q : s_cur) if (q.first == e.handle) have = true;
    if (!have) s_cur.emplace_back(e.handle, dist);
    // THV ['m-1']: 평타 닿기 · 기술 닿기 (걸음 1) · 대시 (+2)
    int reach = (std::max(1, e.range) + 1); bool dash = false; int spell = 0;
    if (e.type >= 0 && e.type < MAX_CHAMP_TYPES && S().design[e.type].valid) {   // ED
        const ChampDesign& D = S().design[e.type];
        for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s];
            if (a.kind == rules::AB_DASH && (!policy::ETN2_ON || e.cooldown[s] <= 1)) dash = true;   /* ETN2: 2턴 안에 준비된 대시만 */
            else if ((a.kind == rules::AB_DAMAGE || a.kind == rules::AB_CC || a.kind == rules::AB_AOE) && (a.targetsEnemies || a.targetsGround)) spell = std::max(spell, (int)a.range + 1); }
    }
    reach = std::max(reach, spell) + (dash ? 2 : 0) + (-1);
    if (policy::FS_MIS3_ON) {   /* FS_MIS3: 아래 두 줄과 같은 식, 값을 남김 */
        const bool v = (policy::TH2_ON && prev2D >= 0) ? (dist <= reach || dist < prev2D || (prevD >= 0 && dist < prevD && dist <= reach + 2)) : (dist <= reach || (prevD >= 0 && dist < prevD));
        if (s_fsThN < 16) { FsTh& q = s_fsTh[s_fsThN++]; q.h = e.handle; q.ep = e.pos; q.mp = w.me.pos; q.ty = e.type; q.rg = e.range; q.c0 = e.cooldown[0]; q.c1 = e.cooldown[1]; q.v = v; }
        return v; }
    if (policy::TH2_ON && prev2D >= 0) return dist <= reach || dist < prev2D || (prevD >= 0 && dist < prevD && dist <= reach + 2);   /* TH2b: 멀면 2라운드 추세, 가까우면 바로 */
    return dist <= reach || (prevD >= 0 && dist < prevD);
}
// R3: 힘 합 — 적 = 위협 적 Σ 체력 × max(1, 힘), 우리 = 나 + RETREAT_ALLY_R 안 아군 Σ 체력 × max(1, 힘). 돌려주는 값 = 위협 적 수
//     후퇴 (update) · R2 (combat) · r3Ratio 가 이 한 식을 쓴다
static std::vector<std::pair<Unit, int>> s_enMem; static int s_enMemR = -1;   /* ENM: 방금 본 적 기억 (마지막 자리 · 본 라운드) */
static const std::vector<std::pair<Unit, int>>& enMem(const World& w) {
    if (s_enMemR != w.round) { s_enMemR = w.round;
        for (const auto& e : w.enemies) { if (!e.alive) continue; bool f = false; for (auto& m : s_enMem) if (m.first.handle == e.handle) { m.first = e; m.second = w.round; f = true; break; } if (!f) s_enMem.emplace_back(e, w.round); }
        s_enMem.erase(std::remove_if(s_enMem.begin(), s_enMem.end(), [&](const std::pair<Unit, int>& m) { return w.round - m.second > 3; }), s_enMem.end()); }
    return s_enMem; }
static const std::vector<std::uint32_t>& sortedAllyH();   /* RAL: 아래 정의 (FUELH) */
bool ralOut(const World& w, const Unit& a) {   /* RAL: 채널 중 아군 (MJG unavailable 과 같은 비트) · RALO: 회복 임무 알림 (team::write 가 K_OUT 을 3R 마다 씀) */
    if (policy::RAL_ON && (a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING))) return true;
    if (policy::RALO_ON) { const std::vector<std::uint32_t>& hs = sortedAllyH(); int k = -1; for (size_t q = 0; q < hs.size(); ++q) if (hs[q] == a.handle) k = (int)q;   /* RALO */
        if (k >= 0 && k < team::T.n && k != team::T.slot) { const team::Mate& mt = team::T.mate[k]; if (mt.outReason > 0 && w.round - mt.outRound <= 3) return true; } }   /* RALO: 3 = K_OUT 쓰는 주기 (round % 3) */
    return false;
}
static int r3Sums0(const World& w, float& ep, float& op);   /* R3C */
static int r3Sums(const World& w, float& ep, float& op) {   /* R3C: 감지 세대 · World · 라운드가 같으면 저장값 */
    static unsigned s_g = 0; static int s_r = -1, s_n = 0; static const World* s_w = nullptr; static float s_ep = 0.f, s_op = 0.f;
    if (policy::R3C_ON && s_r == w.round && s_g == io::g_fs3Gen && s_w == &w) { ep = s_ep; op = s_op; return s_n; }
    s_n = r3Sums0(w, s_ep, s_op); s_r = w.round; s_g = io::g_fs3Gen; s_w = &w; ep = s_ep; op = s_op; return s_n; }
static int r3Sums0(const World& w, float& ep, float& op) {
    int enN = 0; ep = 0.f; op = w.me.hp * std::max(1.f, w.me.str);
    if (policy::R3L_ON) {   /* R3L: 란체스터 (체력 합 × 피해 합), 싸움 판단과 같은 식 */
        float hE = 0.f, dE = 0.f, hA = w.me.hp, dA = combat::unitDps(w.me.type, w.me.str, w.me.focus);
        for (const auto& e : w.enemies) if (threatening(w, e)) { ++enN; hE += e.hp; dE += combat::unitDps(e.type, e.str, e.focus); }
        if (false) for (const auto& m : enMem(w)) { if (m.second == w.round) continue; bool vis = false;   /* R3M: 기억한 적은 R3 힘 합에서 뺌 (아군과 같은 기준) */ for (const auto& e : w.enemies) if (e.handle == m.first.handle) { vis = true; break; } if (vis || !threatening(w, m.first)) continue; ++enN; hE += m.first.hp; dE += combat::unitDps(m.first.type, m.first.str, m.first.focus); }   /* ENM: 안 보이게 된 적도 3R 은 셈 */
        for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= policy::RETREAT_ALLY_R && !((policy::RAL_ON || policy::RALO_ON) && ralOut(w, a)))   /* RAL */   /* R3VA */ { hA += a.hp; dA += combat::unitDps(a.type, a.str, a.focus); }
        ep = hE * dE; op = hA * dA; return enN; }
    for (const auto& e : w.enemies) if (threatening(w, e)) { ++enN; ep += e.hp * std::max(1.f, e.str); }
    for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= policy::RETREAT_ALLY_R && !((policy::RAL_ON || policy::RALO_ON) && ralOut(w, a)))   /* RAL */   /* R3VA */ op += a.hp * std::max(1.f, a.str);
    return enN;
}
bool loneOutnumbered(const World& w) {   /* LONE: 위협 (닿음 · 다가옴) 이 아닌 보이는 적도 셈 — 혼자일 때만 */
    for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= policy::RETREAT_ALLY_R && !((policy::RAL_ON || policy::RALO_ON) && ralOut(w, a))) return false;
    int n = 0; float hE = 0.f, dE = 0.f;
    for (const auto& e : w.enemies) if (e.alive && e.visible && cheb(e.pos, w.me.pos) <= rules::VISION) { ++n; hE += e.hp; dE += combat::unitDps(e.type, e.str, e.focus); }
    if (n < 2) return false;   /* 수에 밀림 = 적 몸 수 > 우리 몸 수 (나 1) */
    return hE * dE >= w.me.hp * combat::unitDps(w.me.type, w.me.str, w.me.focus) * policy::R3_RATIO;
}
bool r3Outnumbered(const World& w) { float ep = 0.f, op = 0.f; const int enN = r3Sums(w, ep, op); return enN > 0 && ep >= op * policy::R3_RATIO; }   // R3: 적 힘 ≥ 우리 힘 × R3_RATIO
static int s_fs2R3r0 = -1; static float s_fs2R3v = 99.f;   /* FS2_MIS1: r3Ratio 라운드 캐시를 파일 범위로 (update 가 이미 센 합으로 채움) */
static float r3RatioFrom(const World& w, float ep, float op) { if (s_fs2R3r0 == w.round) return s_fs2R3v; s_fs2R3r0 = w.round; s_fs2R3v = ep <= 0.f ? 99.f : op / ep; return s_fs2R3v; }   /* FS2_MIS1: r3Ratio 와 같은 식 */
float r3Ratio(const World& w) {   /* ENG3: 기존 후퇴 잣대를 하나의 잣대로 */
    if (policy::FS2_MIS1_ON) { if (s_fs2R3r0 == w.round) return s_fs2R3v; float ep = 0.f, op = 0.f; r3Sums(w, ep, op); return r3RatioFrom(w, ep, op); }   /* FS2_MIS1 */
    static int r0 = -1; static float v = 99.f; if (r0 == w.round) return v; r0 = w.round;
    float ep = 0.f, op = 0.f; r3Sums(w, ep, op);
    if (ep <= 0.f) { v = 99.f; return v; }
    v = op / ep; return v;
}
static bool anyThreat(const World& w) { for (const auto& e : w.enemies) if (e.alive && threatening(w, e)) return true; return false; }   /* VIS */
#define VIS_NEAR(w) (policy::VIS_ON ? anyThreat(w) : (w).enemyNear8)
bool visNear(const World& w) { return VIS_NEAR(w); }   /* MINE */
static int s_hurtR = -100;   /* HYS → MINE: 파일 범위 (적이 보이는 중 체력이 깎인 라운드) */
static int s_lastDistR = -1000;   /* MINE: 마지막 방해 라운드 */
int lastDisturbRound() { return s_lastDistR; }
bool disturbed(const World& w) {   /* MINE: 방해 정의 하나 (턴마다 한 번 셈) */
    static int s_r = -1; static bool s_v = false; if (s_r == w.round) return s_v; s_r = w.round; s_v = false;
    bool near = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, w.me.pos) <= rules::VISION) { near = true; break; }
    if (!near) return false;
    if (w.round - s_hurtR <= 2) s_v = true;   /* (b) 지난 2R 안에 맞음 */
    else if (combat::enemyThreatAtC(w, w.me.pos) > 0.f) s_v = true;   /* (c) 내 칸이 사거리 안 */
    else if ((M.kind == M_FARM || M.kind == M_CAPTURE) && M.group >= 0 && (size_t)M.group < S().groups.size()) {   /* (a) 적이 내 무리의 캠프를 치는 중 */
        const Group& G = S().groups[(size_t)M.group];
        if (G.awakeUntil >= w.round) for (const auto& e : w.enemies) { if (!e.alive || s_v) continue; for (const auto& k : w.camps) if (k.group == M.group && k.hp > 0.f && cheb(e.pos, k.pos) <= std::max(1, e.range)) { s_v = true; break; } }
    }
    if (!s_v && M.kind != M_NONE) {   /* (c) 목표 쪽 걸음 칸이 모두 사거리 안 */
        const int d0 = stepsToGoal(w.me.pos);
        if (d0 > 0 && d0 < 999) { bool any = false, allThr = true;
            for (int dy = -1; dy <= 1 && allThr; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{w.me.pos.x + dx, w.me.pos.y + dy};
                if (!S().inb(q) || !S().walkableStatic(q) || io::campBlocksMovement(q) || stepsToGoal(q) >= d0) continue; any = true;
                if (combat::enemyThreatAtC(w, q) <= 0.f) { allThr = false; break; } }
            if (any && allThr) s_v = true; }
    }
    if (s_v) s_lastDistR = w.round;
    return s_v;
}
static unsigned tankMask(const World& w, const Group& G) {   // KT: 아군이 1칸 안에 붙은 캠프 스폰 (비트 k)
    if (!policy::KT_ON) return 0u; unsigned m = 0u;
    for (size_t k = 0; k < G.spawn.size() && k < 32; ++k) for (const auto& a : w.allies) if (cheb(a.pos, G.spawn[k]) <= 1) { m |= 1u << k; break; }
    return m;
}
static bool g_tilesPartial = false;   // TF
static Pos focusCampRaw(const World& w, int gi);
Pos focusCamp(const World& w, int gi) {   /* FGR: 같은 라운드 · 같은 무리는 한 번 */
    static std::vector<int> s_r; static std::vector<Pos> s_v;
    if (gi < 0 || (size_t)gi >= S().groups.size()) return focusCampRaw(w, gi);
    if (s_r.size() != S().groups.size()) { s_r.assign(S().groups.size(), -1); s_v.assign(S().groups.size(), Pos{-1, -1}); }
    if (s_r[(size_t)gi] != w.round) {
        const Group& Gq = S().groups[(size_t)gi]; const Pos old = s_v[(size_t)gi];   /* FCQ2: 큰 무리는 4R 마다 (고른 캠프가 살아 있으면) */
        const bool keep = Gq.spawn.size() > 12 && old.x >= 0 && S().campBlocks(old) && w.round - s_r[(size_t)gi] < 4;
        if (!keep) { s_v[(size_t)gi] = focusCampRaw(w, gi); s_r[(size_t)gi] = w.round; } }
    return s_v[(size_t)gi];
}
static int g_homeVer = 0;   /* FCQ: 집 거리장 판 */
static const std::vector<int>& fcqHome(int gi) {   /* FCQ: 무리 캠프마다 집 걸음 (homeSteps) 저장 */
    static std::vector<std::vector<int>> s_h; static std::vector<int> s_v;
    if (s_h.size() != S().groups.size()) { s_h.assign(S().groups.size(), {}); s_v.assign(S().groups.size(), -1); }
    if (s_v[(size_t)gi] != g_homeVer) { const Group& G = S().groups[(size_t)gi]; auto& h = s_h[(size_t)gi]; h.resize(G.spawn.size()); for (size_t k = 0; k < G.spawn.size(); ++k) h[k] = homeSteps(G.spawn[k]); s_v[(size_t)gi] = g_homeVer; }
    return s_h[(size_t)gi];
}
static inline int ftbD2(Pos p) { const int dx = p.x - S().allyCenter.x, dy = p.y - S().allyCenter.y; return dx * dx + dy * dy; }   /* FTB2 */
static Pos focusCampRaw(const World& w, int gi) {   // FCS: 그룹 안 1순위 캠프 (모든 몸이 같은 답)
    if (!policy::FCS_ON || gi < 0 || (size_t)gi >= S().groups.size()) return Pos{-1, -1};
    const Group& G = S().groups[(size_t)gi];
    Pos best{-1, -1}; float bk = -1.f, bh = 0.f; bool bKill = false;   /* FTR */
    int fcnMin = -1, fcnW = 0;   /* FCN5: 우리 쪽에서 닿는 캠프만 — 가장 가까운 캠프 걸음 + 무리 폭 */
    { static std::vector<int> s_gw; if (s_gw.size() != S().groups.size()) s_gw.assign(S().groups.size(), -1);   /* FGR: 무리 폭은 한 번 */
      if (s_gw[(size_t)gi] < 0) { int wv = 0; for (size_t k = 0; k < G.spawn.size(); ++k) for (size_t j = k + 1; j < G.spawn.size(); ++j) wv = std::max(wv, cheb(G.spawn[k], G.spawn[j])); s_gw[(size_t)gi] = wv; }
      fcnW = s_gw[(size_t)gi]; }
    const std::vector<int>& fcqH = fcqHome(gi);   /* FCQ */
    for (size_t k = 0; k < G.spawn.size(); ++k) {
        if (!S().campAlive(G, k)) continue; const int h = fcqH[k]; if (h >= 0 && (fcnMin < 0 || h < fcnMin)) fcnMin = h; }   /* FCQ */
    for (size_t k = 0; k < G.spawn.size(); ++k) {
        if (!S().campAlive(G, k)) continue;
        if (fcnMin >= 0) { const int h = fcqH[k]; if (h >= 0 && h > fcnMin + 2 * fcnW + 2) continue; }   /* FCQ */   /* FCN6: 무리를 돌아가는 걸음보다 더 = 막힌 반대편 */
        const Pos sp = G.spawn[k]; const int t = G.campType[k];
        float hp = S().campHp(t); { const size_t mi = G.seenOffset + k; if (mi < S().campSeenRound.size() && S().campSeenRound[mi] == w.round && k < G.campLastHp.size() && G.campLastHp[k] > 0.f) hp = G.campLastHp[k]; }   /* FGR: 읽은 체력 기록 */
        hp = std::max(1.f, hp);
        bool kill = false;
        if (policy::FTR_ON && S().campStr(t) > 0.f) {   /* FTR: 이번 라운드 (다음 캠프 차례 전) 에 끝낼 수 있나 — 나 + 아직 안 둔 아군 */
            auto reachN = [](int range) { return range <= 1 ? 2 : range; };   // 근접 = 한 걸음 + 평타, 원거리 = 걷으면 못 침
            float can = (w.me.canAttack && cheb(w.me.pos, sp) <= reachN(w.me.range)) ? w.me.str : 0.f;
            for (const auto& a : w.allies) if (a.visible && a.alive && !a.acted && cheb(a.pos, sp) <= reachN(a.range)) can += a.str;
            kill = can >= hp; }
        const float key = kill ? S().campStr(t) : S().campStr(t) / hp;
        bool better = best.x < 0 || (kill && !bKill);
        if (!better && kill == bKill) {
            better = key > bk * 1.0001f + 1e-9f;
            if (!better && std::fabs(key - bk) <= bk * 0.0001f + 1e-9f) better = hp < bh - 0.5f || (std::fabs(hp - bh) <= 0.5f && (ftbD2(sp) < ftbD2(best) || (ftbD2(sp) == ftbD2(best) && (sp.y < best.y || (sp.y == best.y && sp.x < best.x))))); }   /* FTB2: 우리 쪽 먼저 (스폰 중심까지 제곱 거리) */
        if (better) { best = sp; bk = key; bh = hp; bKill = kill; }
    }
    return best;
}
// 한 몸의 캠프 화력 (평타 힘 + 캠프에 쓰는 피해 · 범위 주문의 턴당 몫) — MG · CB · HLP
static float ksDps(int type, float str, float focus) {
    float v = std::max(1.f, str);
    const ChampDesign& D = S().designOf(type);
    if (D.valid) for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s]; if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_AOE) continue; if (!a.targetsCamps && !(a.kind == rules::AB_AOE && a.targetsGround)) continue; v += focus * a.modifier / (float)(a.cooldown + 1); }
    return v;
}
static std::vector<Pos> attackTilesOf(const World& w, const Group& G) {
    std::vector<Pos> out; const int r = std::max(1, w.me.range);
    const bool useAlive = policy::M_ALIVE_TILES && std::any_of(G.spawn.begin(), G.spawn.end(), [](Pos p) { return S().campBlocks(p); });
    const Pos fcsP = focusCamp(w, (int)(&G - &S().groups[0])); bool fcsOnly = fcsP.x >= 0;   /* FCS */
  fcsAgain:
    for (size_t k = 0; k < G.spawn.size(); ++k) {
        if (io::fuel() < policy::ZO_TILE_FLOOR && !out.empty()) break;   // TF
        if (useAlive && !S().campAlive(G, k)) continue;   // 관측과 재생성 추정을 합친 유효 생존 정보
        if (fcsOnly && !(G.spawn[k] == fcsP)) continue;   /* FCS: 1순위 캠프 둘레만 */
        const Pos sp = G.spawn[k];
        const std::vector<std::uint16_t>* LL = lcTry(sp, r);
        if (!LL) { g_tilesPartial = true; break; }   // LC
        for (const std::uint16_t pi16 : *LL) {   // LC: 걸을 수 있음 · 캠프 칸 아님 · 덤불 · 시선은 저장 목록
            const Pos p = lcPos(pi16);
            bool blind = false; for (const auto& b : M.blind) if (b == p) blind = true;   // 거기 서 봤는데 캠프가 안 보였던 칸
            if (blind) continue;
            out.push_back(p);
        }
    }
    if (policy::GLM_ON && out.empty() && !g_tilesPartial) {   /* GLM3: 원래 답 (설 칸) 이 비었을 때만 — 산 캠프 둘레의 비어 있는 (죽은 캠프) 스폰 칸 */
        for (size_t k = 0; k < G.spawn.size(); ++k) {
            if (useAlive && !S().campAlive(G, k)) continue;
            const Pos sp = G.spawn[k];
            for (const Pos q : G.spawn) {
                if (q == sp || cheb(q, sp) > r || S().campBlocks(q) || !S().walkableStatic(q) || !losClear(q, sp)) continue;
                bool blind = false; for (const auto& b : M.blind) if (b == q) blind = true;
                if (blind || std::find(out.begin(), out.end(), q) != out.end()) continue;
                out.push_back(q);
            }
        }
    }
    if (fcsOnly && out.empty() && !g_tilesPartial) { fcsOnly = false; goto fcsAgain; }   /* FCS: 1순위 둘레에 설 칸이 없으면 모든 산 캠프 */
    return out;
}
static std::vector<Pos> captureTilesOf(const Group& G) { return G.spawn; }
// VCR: 이 무리를 Vesper 가 점령하러 올 수 있나 (살아 있는 Vesper 동료가 4칸 안이거나 이 무리 담당)
static float gvtPick();
static float cpyBonus(int type, const Group& G) {
    const float g=gvtPick();
    if(reference::PRISM_MACRO && reference::PRISM_CAPTURE && type==reference::PRISM){
        const float grow=std::max(1.f,S().designOf(type).focusGrow);
        return (float)G.gems*reference::PRISM_GEM_VALUE*rules::SHARD_COST*1.5f/grow/g;
    }
    return type==policy::VCR_TYPE?(float)G.gold*(policy::VCR_GOLD-1.f)/g:type==7?(float)G.gems*2.f/g:0.f;
}   /* CPY · TWP 규칙 4: Prism passive is valued when deciding an already-nearby capture. */
static bool onCapture(const Group& G, Pos p);   /* CPYC */
static bool vcrVesperFor(const World& w, int gi) {
    if (policy::CPY_ON && policy::VCR_ON && gi >= 0) {   /* CPY: 덤이 가장 큰 곁 아군에게 */
        const Group& G = S().groups[(size_t)gi]; const float mine = cpyBonus(w.me.type, G), need = (float)policy::VCR_MIN_GOLD * (policy::VCR_GOLD - 1.f) / gvtPick();
        const std::vector<std::uint32_t>& hsY = sortedAllyH();
        for (const auto& a : w.allies) { if (!a.alive || !a.visible || cheb(a.pos, S().allyCenter) <= 3 || cheb(a.pos, G.center) > policy::VCR_R || ralOut(w, a)) continue;
            int k = -1; for (size_t q = 0; q < hsY.size(); ++q) if (hsY[q] == a.handle) k = (int)q;   /* CPYC: 받을 몸이 이 무리를 맡았나 */
            if (k < 0 || k >= team::T.n) continue;
            if (policy::VYO_ON) { const team::Mate& mt = team::T.mate[k]; if ((mt.claimKind == team::CL_CAPTURE && mt.claimGroup != gi) || a.hp < policy::RECOVER_HP * a.maxHp || (a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING))) continue; }   /* VYO: 다른 무리 점령 중이 아니면 */
            else if (team::T.mate[k].claimGroup != gi || !(team::T.mate[k].claimKind == team::CL_CAPTURE || team::T.mate[k].claimKind == team::CL_FARM)) continue;
            if (cpyBonus(a.type, G) - mine > 0.f) return true; }   /* VCR0: 덤이 더 크면 양보 (여유 없앰) */
        return false; }
    if (!policy::VCR_ON || gi < 0 || w.me.type == policy::VCR_TYPE) return false;
    const Group& G = S().groups[(size_t)gi];
    if (G.gold < policy::VCR_MIN_GOLD) return false;   /* VCR3: 값 큰 무리만 양보 */
    for (const auto& a : w.allies) {
        if (!a.alive || a.type != policy::VCR_TYPE || cheb(a.pos, S().allyCenter) <= 3) continue;
        if (a.visible && cheb(a.pos, G.center) <= policy::VCR_R) return true;   /* VCR2: 곁에 보일 때만 */
    }
    return false;
}
static int alliesNear(const World& w, int r);
static float lossPerBody(const World& w, const Group& G, int gi, int n);
static float goalThreat(int gi);   // MG
static std::vector<Pos> altarTilesOf(int a) {
    std::vector<Pos> out; const Pos c = S().altars[(size_t)a];
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos p{c.x + dx, c.y + dy}; if (S().inb(p) && S().walkableStatic(p)) out.push_back(p); }
    return out;
}

// CQ: 이 그룹 점령 칸 (스폰 칸) 위에 내가 · 아군이 서 있나
static bool onCapture(const Group& G, Pos p) { for (const auto& s : G.spawn) if (s == p) return true; return false; }
static bool allyOnCapture(const World& w, const Group& G) { for (const auto& a : w.allies) { if (!a.alive || !a.visible || (a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING))) continue;   /* COX: 점령할 수 있는 아군만 */
    if (onCapture(G, a.pos)) return true; } return false; }
static bool allyOnCaptureCO(const World& w, const Group& G) { const float mine = policy::CPY_ON ? cpyBonus(w.me.type, G) : 0.f;   /* BH_F5 */
    for (const auto& a : w.allies) { if (!a.alive || !a.visible || (a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING))) continue;
        if (onCapture(G, a.pos) && (!policy::CPY_ON || cpyBonus(a.type, G) >= mine)) return true; } return false; }   /* CO-CPY: 칸 위 아군 모두보다 내 덤이 크면 비키지 않음 */
static std::vector<Pos> s_occEx; static int s_occExTag = -1, s_queueTurns = 0;   // OQ: 이 임무에서 뺀 (아군 차지) 목표 칸
std::vector<Pos> goalTiles(const World& w) {
    switch (M.kind) {
    case M_FARM: if (M.group < 0) return {}; return attackTilesOf(w, S().groups[(size_t)M.group]);
    case M_CAPTURE: return M.group >= 0 ? captureTilesOf(S().groups[(size_t)M.group]) : std::vector<Pos>{};
    case M_TRIBUTE: return M.altar >= 0 ? altarTilesOf(M.altar) : std::vector<Pos>{};
    case M_ESCORT: return M.altar >= 0 ? altarTilesOf(M.altar) : std::vector<Pos>{};   // ES
    case M_RECOVER: case M_SHOP:
        if (policy::RGP_ON && M.kind == M_RECOVER && M.retreat && w.me.hp >= policy::RETREAT_HP * w.me.maxHp) {   /* RGP: 수에 밀린 후퇴는 아군 무리 쪽으로 */
            auto enD = [&](Pos p) { int b = 99; for (const auto& e : w.enemies) if (e.alive) b = std::min(b, cheb(e.pos, p)); return b; };
            const int myE = enD(w.me.pos); int bestN = -1; Pos bp{-1, -1};
            for (const auto& a : w.allies) {
                if (!a.visible || !a.alive || cheb(a.pos, w.me.pos) > policy::RGP_R || a.hp < policy::RETREAT_HP * a.maxHp || enD(a.pos) < myE) continue;
                int nA = 0; for (const auto& b : w.allies) if (b.visible && b.alive && cheb(b.pos, a.pos) <= 4) ++nA;
                if (nA > bestN) { bestN = nA; bp = a.pos; }
            }
            if (bp.x >= 0) { std::vector<Pos> out; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos p{bp.x + dx, bp.y + dy}; if (S().inb(p) && S().walkableStatic(p)) out.push_back(p); } if (!out.empty()) return out; }
        }
        return S().allySpawn;
    default: return {};
    }
}
// GF: 그룹마다 (s_gf[g]) · 집 (s_gfHome) 정적 거리장 배열. 빈 배열 = 아직 없음
static std::vector<RawVec<std::uint16_t>> s_gf; static RawVec<std::uint16_t> s_gfHome;   // NI
static long long s_gfCost = policy::GF_INIT_COST;
static inline bool gfReady(int gi) { return policy::GF_ON && gi >= 0 && (size_t)gi < s_gf.size() && !s_gf[(size_t)gi].empty(); }
static inline int gfAt(const RawVec<std::uint16_t>& f, Pos p) {   // 막힌 칸 (캠프 칸 위에 섰을 때) 은 옆 칸 + 1
    if (!S().inb(p)) return 65535; const int v = (int)f[(size_t)S().idx(p)]; if (v < 65535) return v;
    int b = 65535; for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos q{p.x + dx, p.y + dy}; if ((dx || dy) && S().inb(q)) { const int u = (int)f[(size_t)S().idx(q)]; if (u < 65535) b = std::min(b, u + 1); } }
    return b;
}
// FFR: 뿌리 칸 중복 지우기 — 후보들의 상자 크기 표시 배열로 순서대로 첫 것만 (std::find 는 큰 그룹에서 제곱 — yellow 뿌리 64칸 한 그룹 ~90k;
//      맵 크기 표시 배열은 100×100 맵에서 처음 만들 때 40k, 정렬도 후보 수백 개면 비싸다)
static void rootDedupe(std::vector<Pos>& v) {
    if (v.size() < 2) return; int x0 = v[0].x, y0 = v[0].y, x1 = x0, y1 = y0;
    for (const auto& p : v) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    const int bw = x1 - x0 + 1; std::vector<char> seen((size_t)(bw * (y1 - y0 + 1)), 0); size_t n = 0;
    for (size_t i = 0; i < v.size(); ++i) { const size_t k = (size_t)((v[i].y - y0) * bw + (v[i].x - x0)); if (seen[k]) continue; seen[k] = 1; v[n++] = v[i]; }
    v.resize(n);
}
static bool gfBuild(int gi) {
    if (s_gf.size() != S().groups.size()) s_gf.resize(S().groups.size());
    const Group& G = S().groups[(size_t)gi];
    const long long f0 = io::fuel(); const int r0 = io::round();   // FFR: 뿌리 모으기도 비용에 넣는다
    std::vector<Pos> roots; roots.reserve(G.spawn.size() * 9);
    for (const auto& sp : G.spawn) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const Pos p{sp.x + dx, sp.y + dy}; if (!S().inb(p) || !S().walkableStatic(p) || (S().tile[(size_t)S().idx(p)] & T_CAMP)) continue;
        roots.push_back(p);
    }
    rootDedupe(roots);   // FFR
    RawVec<std::uint16_t> v;
    if (!io::fieldExport(roots, v)) { v.resize((size_t)S().W * (size_t)S().H); std::memset(v.data(), 0xFF, v.size() * 2); }   // NI
    const long long c = f0 - io::fuel();
    s_gfCost = io::round() != r0 ? policy::GF_COST_CAP : std::max(5000LL, std::max(c, s_gfCost * 9 / 10));   // GC
    s_gf[(size_t)gi].swap(v);
    return true;
}
static bool gfBuildHome();
void buildHomeEarly() { if (s_gfHome.empty() && io::wallsReady()) gfBuildHome(); }   /* HFI */
static bool gfBuildHome() {
    const long long f0 = io::fuel(); const int r0 = io::round();
    RawVec<std::uint16_t> v;
    if (!io::fieldExport(S().allySpawn, v)) return false;
    const long long c = f0 - io::fuel();
    s_gfCost = io::round() != r0 ? policy::GF_COST_CAP : std::max(5000LL, std::max(c, s_gfCost * 9 / 10));   // GC
    s_gfHome.swap(v); ++g_homeVer;   /* FCQ */
    return true;
}
int homeSteps(Pos p) { if (s_gfHome.empty()) return -1; const int v = gfAt(s_gfHome, p); return v >= 65535 ? -1 : v; }
static std::vector<RawVec<std::uint16_t>> s_awF;   // AW: 제단마다 정적 거리장 (빈 것 = 아직) — 남는 연료 턴에 미리
static RawVec<std::uint16_t> s_territoryOwn, s_territoryEnemy;   /* TM_CAMP: 우리 스폰 · 적 스폰 땅 걸음장 */
static long long s_territoryCost = policy::GF_INIT_COST;   /* TM_CAMP: 잰 비용 */
static int s_territoryMirror = -2, s_territoryEpoch = 0;   /* TM_CAMP */
static Pos territoryMirror(Pos p) {
    return {s_territoryMirror == 1 ? p.x : S().W - 1 - p.x,
            s_territoryMirror == 0 ? p.y : S().H - 1 - p.y};
}
static bool campTerritoryReady() {
    return !policy::TM_CAMP_ON || S().allySpawn.empty() || S().enemySpawn.empty() ||
        (!s_territoryOwn.empty() && (s_territoryMirror >= 0 || !s_territoryEnemy.empty()));
}
void prewarmCampTerritory(long long floorFuel) {   /* TM_CAMP */
    if (campTerritoryReady() || !io::wallsReady()) return;
    const int startRound = io::round();
    if (io::fuel() < floorFuel + s_territoryCost) return;
    if (s_territoryMirror == -2) s_territoryMirror = io::territorySymmetry();
    for (int side = 0; side < 2; ++side) {
        if (side && s_territoryMirror >= 0) break;
        auto& values = side ? s_territoryEnemy : s_territoryOwn;
        if (!values.empty()) continue;
        const long long f0 = io::fuel();
        if (io::round() != startRound || f0 < floorFuel + s_territoryCost) return;
        if (!io::territoryFieldExport(side != 0, values)) return;
        const long long cost = f0 - io::fuel();
        s_territoryCost = io::round() != startRound ? policy::GF_COST_CAP : std::max(5000LL, std::max(cost, s_territoryCost * 9 / 10));
    }
    if (campTerritoryReady()) ++s_territoryEpoch;
}
static void awBuild(int a) { const std::vector<Pos> roots = altarTilesOf(a); RawVec<std::uint16_t> v; if (roots.empty() || !io::fieldExport(roots, v)) { v.resize((size_t)S().W * (size_t)S().H); std::memset(v.data(), 0xFF, v.size() * 2); } s_awF[(size_t)a].swap(v); }
void compStep(long long floorFuel);   /* PFX */
static bool g_brfRepick = false;   /* BRF2 */
void prewarmGF(long long floorFuel) {
    if (io::fuel() < floorFuel + 3000) return;
    if (!policy::GF_ON || S().groups.empty() || !io::wallsReady()) return;
    if (s_gf.size() != S().groups.size()) s_gf.resize(S().groups.size());
    static std::vector<int> order;
    if (order.size() != S().groups.size()) {   // 우리 스폰에서 가까운 그룹부터
        order.resize(S().groups.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        const Pos from = S().allyCenter;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return cheb(S().groups[(size_t)a].center, from) < cheb(S().groups[(size_t)b].center, from); });
    }
    const int startRound = io::round();
    auto enough = [&]() { const long long f = io::fuel(); return io::round() == startRound && f >= floorFuel + 3000 && f >= s_gfCost + policy::LC_BUILD_RES; };
    if (S().dashNav && !io::bridgesReady()) io::bridgeStep(floorFuel + 8000);   /* BRG2: 다리 표를 남는 연료로 한 조각 */
    if (io::bridgesJustDone()) { for (auto& v : s_gf) v.clear(); s_gfHome.clear(); g_brfRepick = true; }   /* BRG2: 다리 포함으로 다시 만들게 */   /* BRF2: 다리가 생기면 거리가 바뀌니 농사 목표 다시 고름 */
    if (s_gfHome.empty()) { if (!enough()) return; gfBuildHome(); }
    if (policy::AW_ON) { if (s_awF.size() != S().altars.size()) s_awF.resize(S().altars.size()); for (size_t a = 0; a < s_awF.size(); ++a) { if (!s_awF[a].empty()) continue; if (!enough()) return; awBuild((int)a); } }   // AW
    for (int gi : order) { if (io::fuel() < floorFuel + 2000) return; if (gfReady(gi)) continue; if (!enough()) return; gfBuild(gi); }
    compStep(floorFuel);   /* PFX: 거리장이 다 된 뒤에만, 턴당 한 조각 */
}
static std::vector<int> g_altarBlock;   // LK
void blockAltar(int a, int until) { if (a < 0) return; if (g_altarBlock.size() != S().altars.size()) g_altarBlock.assign(S().altars.size(), -1); g_altarBlock[(size_t)a] = until; }
// AMS: 살아 있는 모노리스 칸을 벽으로 보면 시작점에서 제단 곁 칸에 닿을 수 없나.
// 연료: 처음 한 번만 BFS (모노리스 모두 산 경우 + 모노리스 그룹 하나씩 죽은 경우), 그 뒤엔 그룹 상태만 본다.
struct AmsAltar { bool sealedAll = false; std::vector<int> openers; };
static std::vector<AmsAltar> s_ams; static bool s_amsReady = false; static std::vector<int> s_amsMono;
// AMS2: 제단 둘레 R 칸 상자 안에서만 BFS (맵 전체 BFS 는 한 번에도 턴 연료를 넘는다 — 모노리스 맵마다 몸당 1 span).
// 제단 곁 칸 (모노리스 칸 제외) 에서 출발해 상자 가장자리에 닿으면 열림.
static int amsAltarLocal(const std::vector<char>& blk, Pos c) {   // 0 곁 칸 없음 · 1 닿음 · 2 막힘
    const int R = 5; const int side = 2 * R + 1; std::vector<char> seen((size_t)(side * side), 0); std::vector<int> qu; qu.reserve((size_t)(side * side));
    bool open = false;
    auto li = [&](Pos q) { return (q.y - c.y + R) * side + (q.x - c.x + R); };
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (std::abs(dx) + std::abs(dy) != 1) continue; const Pos q{c.x + dx, c.y + dy}; if (!S().passNav(q)) continue; open = true; if (blk[(size_t)S().idx(q)]) continue; seen[(size_t)li(q)] = 1; qu.push_back(li(q)); }
    if (!open) return 0;
    for (size_t h = 0; h < qu.size(); ++h) { const int ly = qu[h] / side, lx = qu[h] % side; if (lx == 0 || ly == 0 || lx == side - 1 || ly == side - 1) return 1;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{c.x + lx - R + dx, c.y + ly - R + dy}; if (!S().passNav(q)) continue; const int l = li(q); if (seen[(size_t)l] || blk[(size_t)S().idx(q)]) continue; seen[(size_t)l] = 1; qu.push_back(l); } }
    return 2;
}
// AMS4: 계산을 라운드마다 BFS 하나씩 나눈다 (한 턴에 여러 BFS 면 턴 연료를 넘는다). 다 끝나기 전 제단은 막힘 아님.
static bool s_amsAnySealed = false; static std::vector<char> s_amsBlk; static std::vector<std::vector<int>> s_amsNb; static int s_amsK = 0, s_amsJ = -1; static bool s_amsDone = false;
static void amsMark(int gi, char v) { const Group& G = S().groups[(size_t)gi]; for (size_t k = 0; k < G.spawn.size(); ++k) if (k < G.campType.size() && G.campType[k] == rules::C_MONOLITH && S().inb(G.spawn[k])) s_amsBlk[(size_t)S().idx(G.spawn[k])] = v; }
void amsInit() {   // 한 번 부를 때마다 BFS 하나 (또는 준비)
    if (s_amsDone) return;
    if (!s_amsReady) {
        s_amsReady = true; s_ams.assign(S().altars.size(), AmsAltar{}); s_amsMono.clear();
        for (int gi = 0; gi < (int)S().groups.size(); ++gi) if (S().groups[(size_t)gi].mono) s_amsMono.push_back(gi);
        if (s_amsMono.empty() || s_amsMono.size() > 8) { s_amsDone = true; return; }
        s_amsNb.assign(S().altars.size(), {}); bool anyNb = false;
        for (size_t k = 0; k < S().altars.size(); ++k) for (int gi : s_amsMono) { const Group& G = S().groups[(size_t)gi]; for (size_t q = 0; q < G.spawn.size(); ++q) if (q < G.campType.size() && G.campType[q] == rules::C_MONOLITH && cheb(G.spawn[q], S().altars[k]) <= 5) { s_amsNb[k].push_back(gi); anyNb = true; break; } }
        if (!anyNb) { s_amsDone = true; return; }   /* AMS5: 제단 곁 모노리스가 없으면 아무것도 안 함 */
        s_amsBlk.assign((size_t)(S().W * S().H), 0); for (int gi : s_amsMono) amsMark(gi, 1);
        s_amsK = 0; s_amsJ = -1; return;
    }
    while (s_amsK < (int)S().altars.size() && s_amsNb[(size_t)s_amsK].empty()) ++s_amsK;
    if (s_amsK >= (int)S().altars.size()) { s_amsDone = true; return; }
    const Pos c = S().altars[(size_t)s_amsK]; auto& nb = s_amsNb[(size_t)s_amsK];
    if (s_amsJ < 0) { if (amsAltarLocal(s_amsBlk, c) == 2) { s_amsJ = 0; return; } ++s_amsK; return; }   // 모두 살아 있으면 막히나
    if (s_amsJ < (int)nb.size()) { const int gi = nb[(size_t)s_amsJ]; amsMark(gi, 0); if (amsAltarLocal(s_amsBlk, c) == 1) s_ams[(size_t)s_amsK].openers.push_back(gi); amsMark(gi, 1); ++s_amsJ; return; }
    s_ams[(size_t)s_amsK].sealedAll = true; s_amsAnySealed = true; ++s_amsK; s_amsJ = -1;   // 여는 그룹을 다 셌을 때 막힘으로 확정
}
static bool altarSealed(int a) {
    if (!s_amsAnySealed) return false;   /* AMS5 */
    // 열린 캠프 칸이 있으면 단일 그룹 제거 사전 계산으로는 봉쇄를 확정할 수 없다.
    // 실제 제단 선택은 같은 출발점의 dMe로 여러 통로의 조합까지 확인한다.
    if (S().openCampTiles > 0) return false;
    if (a < 0 || (size_t)a >= s_ams.size() || !s_ams[(size_t)a].sealedAll) return false;
    auto alive = [](int gi) { const int st = S().groups[(size_t)gi].state; return st == 0 || st == 1; };
    for (int gi : s_ams[(size_t)a].openers) if (!alive(gi)) return false;   // 여는 모노리스 하나가 죽었다
    bool allDead = true; for (int gi : s_amsMono) if (alive(gi)) allDead = false;
    return !allDead;
}
bool altarBlocked(int a, int round) { return a >= 0 && (((size_t)a < g_altarBlock.size() && g_altarBlock[(size_t)a] > round) || altarSealed(a)); }   // AMS
static int awSteps(int a, Pos p) {   // AW: 미리 만든 거리장만 읽는다 (없으면 -1 → 예전 방식)
    if (!policy::AW_ON || a < 0 || (size_t)a >= s_awF.size() || s_awF[(size_t)a].empty()) return -1;
    const int v = gfAt(s_awF[(size_t)a], p); return v >= 65535 ? 99999 : v;
}
enum PickCode : int { PICK_NONE = -1, PICK_DEFERRED = -2 };
static long long s_meCost = 25000;
static RawVec<std::uint16_t> s_meValues;
static int s_meValuesRound = -1;
static std::uint64_t s_meValuesVersion = 0;
static Pos s_meValuesOrigin;
static bool ensureCurrentMe(const World& w) {
    const bool reuse = dMe.from(w.me.pos, w.round);
    if (reuse && s_meValuesRound >= 0 && s_meValuesVersion == S().campVersion && s_meValuesOrigin == w.me.pos) return true;
    // 추가 동적 계산은 FZL의 예비 연료 완화와 별개로 실행 예산을 보존한다.
    if (io::fuel() < (reuse ? 3000 : s_meCost) + zres()) { if (!reuse) s_meCost = std::max(5000LL, s_meCost * 9 / 10); return false; }   /* FPK: 거절마다 감쇠 (넘침 뒤 잠김 방지) */
    const long long f0 = io::fuel(); const int r0 = io::round();
    bool ok = reuse;
    if (!reuse) { ok = dMe.compute(w, {w.me.pos}, 0); if (dMe.ranCompute) ++io::nav.meBuilds; }
    if (ok) ok = dMe.exportValues(s_meValues);
    const long long cost = f0 - io::fuel();
    if (!reuse) s_meCost = io::round() != r0 ? policy::FG_MAX : std::max({5000LL, cost, s_meCost * 9 / 10});
    if (io::round() != r0) { dMe.ready = false; return false; }
    if (ok) { s_meValuesRound = w.round; s_meValuesVersion = S().campVersion; s_meValuesOrigin = w.me.pos; }
    return ok;
}
static int checkedMeDistance(Pos p) {
    if (io::fuel() < policy::ZO_LOOP_STOP + 250) return PICK_DEFERRED;
    ++io::nav.queries;
    return S().inb(p) ? (int)s_meValues[(size_t)S().idx(p)] : 65535;
}
static bool unlOpener(int gi) {   /* UNL: 모든 제단의 설 칸이 살아 있는 캠프로 막혔고, 이 그룹이 그 캠프를 가졌다 */
    if (S().altars.empty() || gi < 0 || (size_t)gi >= S().groups.size()) return false;
    const Group& G = S().groups[(size_t)gi]; if (!(G.state == 0 || G.state == 1)) return false;
    bool opener = false;
    for (int a = 0; a < (int)S().altars.size(); ++a) {
        const std::vector<Pos> tiles = altarTilesOf(a); if (tiles.empty()) return false;
        for (Pos q : tiles) { if (!io::campBlocksMovement(q)) return false;
            for (const auto& sp : G.spawn) if (sp == q) opener = true; }
    }
    return opener;
}
static int awPick(const World& w) {
    int best = -1, bs = 1 << 30; bool dynamic = S().openCampTiles > 0;
    for (int a = 0; a < (int)S().altars.size(); ++a) {
        if (altarBlocked(a, w.round)) continue;
        const int s = awSteps(a, w.me.pos);
        if (s < 0 || s >= 99999) dynamic = true;
        else if (s < bs) { bs = s; best = a; }
    }
    if (!dynamic) return best;
    ++io::nav.dynamicPicks;
    if (!ensureCurrentMe(w)) { ++io::nav.deferred; return PICK_DEFERRED; }
    best = -1; bs = 65535;
    for (int a = 0; a < (int)S().altars.size(); ++a) {
        if (altarBlocked(a, w.round)) continue;
        for (Pos p : altarTilesOf(a)) {
            const int d = checkedMeDistance(p);
            if (d == PICK_DEFERRED) { ++io::nav.deferred; return best >= 0 ? best : PICK_DEFERRED; }
            if (d < bs) { bs = d; best = a; }
        }
    }
    return best;
}
bool bridgeAhead(const World& w, int steps) {   /* BRK: 목표 길 (거리장 내리막) 앞 steps 걸음 안에 대시 다리 칸이 있나 */
    if (!S().dashNav || S().bridge.size() != S().tile.size()) return false;
    Pos p = w.me.pos; int d = stepsToGoal(p); if (d <= 0 || d >= 999) return false;
    for (int i = 0; i < steps && d > 0; ++i) { Pos nb{-1, -1}; int nd = d;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{p.x + dx, p.y + dy}; if (!S().inb(q)) continue; const int v = stepsToGoal(q); if (v < nd) { nd = v; nb = q; } }
        if (nb.x < 0) return false; if (S().bridge[(size_t)S().idx(nb)]) return true; p = nb; d = nd; }
    return false; }
int stepsToGoal(Pos p) {
    if (!dGoal.ready || dGoal.tag != g_goalTag) {   // 임무가 바뀐 뒤 아직 새 거리장이 없으면 모름 (옛 거리장을 읽어 '목표 칸에 있다' 고 오판 → 눈먼 칸 · 정체 연쇄)
        if (policy::GF_WALK && (M.kind == M_FARM || M.kind == M_CAPTURE) && gfReady(M.group)) { const int v = gfAt(s_gf[(size_t)M.group], p); return v >= 65535 ? 999 : v == 0 ? 1 : v; }   // GF: 그룹 거리장으로 (0 은 1 로 — 목표 칸 도착 판정은 목표 거리장만)
        if (policy::GF_WALK && (M.kind == M_SHOP || M.kind == M_RECOVER) && policy::GF_ON && !s_gfHome.empty()) return gfAt(s_gfHome, p);   // GF: 집 거리장 = 목표 칸과 같다
        return 999;
    }
    const int d = dGoal.get(p);
    if (d >= 0) return d;
    return 999;
}
static int g_esReqAltar = -1, g_esReqRound = -1000, g_esHoldSince = -1, g_esSeen = -1000;   // ES
static std::vector<int> g_esCnt, g_esBan;   // ESX: 제단마다 미룬 누적 · 호위 금지 끝 라운드
int helpRequest() { return io::round() - g_esReqRound <= 1 ? g_esReqAltar : -1; }
float goldDiv();
static const std::vector<std::uint32_t>& sortedAllyH() { static std::vector<std::uint32_t> v; if (v.size() != io::S.allyHandle.size()) { v = io::S.allyHandle; std::sort(v.begin(), v.end()); } return v; }   /* FUELH */
static int cpxTeamGems(const World& w, int myGems) {   /* CPX: team::teamGems minus Prism mates' reports */
    static int s_ty[team::MAX]; static bool s_ok = false;
    if (!s_ok) { const std::vector<std::uint32_t>& hs = sortedAllyH(); bool all = !hs.empty();
        for (int k = 0; k < team::MAX; ++k) { s_ty[k] = -1; if ((size_t)k < hs.size()) { int t = -1; if (io::championIdentity(hs[(size_t)k], t)) s_ty[k] = t; else all = false; } }
        s_ok = all; }
    int s = myGems;
    for (int k = 0; k < team::T.n && k < team::MAX; ++k) { if (k == team::T.slot || s_ty[k] == reference::PRISM) continue; const team::Mate& m = team::T.mate[k]; if (w.round - m.gemsRound <= team::LIFE_CLAIM) s += m.gemsRep; }
    return s;
}
struct CampClaimEvidence { int group = -1; Pos pos; float confidence = 0.f; int uncertainty = 0; };   /* TM_CAMP */
struct CampClaimRoster { CampClaimEvidence mate[team::MAX]; };
static bool freshCampClaim(const team::Mate& m, int round) {
    return m.claimGroup >= 0 && (m.claimKind == team::CL_FARM || m.claimKind == team::CL_CAPTURE) &&
        round >= m.claimRound && round - m.claimRound <= team::LIFE_CLAIM &&
        !(m.outReason > 0 && round >= m.outRound && round - m.outRound <= team::LIFE_CLAIM);
}
static CampClaimRoster campClaimRoster(const World& w, bool shared) {   /* TM_CAMP: 담당 팀원 증거 (보이면 실제 자리 · 신뢰 1, 아니면 공유 구역 · 신뢰 0.5 x 나이) */
    CampClaimRoster roster;
    if (!policy::TM_CAMP_ON || policy::CAMP_JOIN_BONUS_PER_ALLY <= 0.f) return roster;
    const auto& handles = sortedAllyH();
    for (int k = 0; k < team::T.n && k < team::MAX && (size_t)k < handles.size(); ++k) {
        if (!shared && handles[(size_t)k] == w.me.handle) continue;
        const auto& m = shared ? team::planView(k, w) : team::T.mate[k];
        if (!freshCampClaim(m, w.round) || (size_t)m.claimGroup >= S().groups.size()) continue;
        auto& evidence = roster.mate[k];
        if (!shared) {
            bool observed = false;
            for (const auto& a : w.allies) if (a.handle == handles[(size_t)k]) {
                observed = true;
                if (a.visible && a.alive && a.maxHp > 0.f && a.hp >= policy::RETREAT_HP * a.maxHp &&
                    !(a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING)) &&
                    S().inb(a.pos) && !(S().tile[(size_t)S().idx(a.pos)] & T_ALLY_SPAWN))
                    evidence = {m.claimGroup, a.pos, 1.f, 0};
                break;
            }
            if (observed) continue;   // 지금 본 '못 옴' 이 옛 공유 보고를 이김
        }
        const int age = w.round - m.zoneRound;
        if (!m.known || m.zone < 0 || m.zone >= 64 || age < 0 || age > team::LIFE_STATUS) continue;
        const Pos p = team::zoneCenter(m.zone);
        if (!S().inb(p) || (S().tile[(size_t)S().idx(p)] & T_ALLY_SPAWN)) continue;
        evidence = {m.claimGroup, p, policy::CAMP_SHARED_CONFIDENCE *
            (1.f - (float)age / (float)(team::LIFE_STATUS + 1)), team::zoneSide() / 2 + age};
    }
    return roster;
}
static float campTerritory(int gi) {   /* TM_CAMP: 땅 계수 (무리 캠프 칸 중 가장 가까운 걸음) */
    if (!policy::TM_CAMP_ON || gi < 0 || (size_t)gi >= S().groups.size()) return 1.f;
    if (!campTerritoryReady()) return 1.f;   // 덜 된 장은 캐시 안 함
    static std::vector<float> cache;
    if (cache.size() != S().groups.size()) cache.assign(S().groups.size(), -1.f);
    float& value = cache[(size_t)gi];
    if (value < 0.f) {
        value = 1.f;
        if (!S().allySpawn.empty() && !S().enemySpawn.empty()) {
            int own = 65535, enemy = 65535;
            for (Pos p : S().groups[(size_t)gi].spawn) if (S().inb(p)) {
                const size_t index = (size_t)S().idx(p);
                own = std::min(own, (int)s_territoryOwn[index]);
                enemy = std::min(enemy, s_territoryMirror >= 0 ?
                    (int)s_territoryOwn[(size_t)S().idx(territoryMirror(p))] : (int)s_territoryEnemy[index]);
            }
            value = camp_commitment::territory((float)own, (float)enemy);
        }
    }
    return value;
}
static float campClaimCredit(const CampClaimEvidence& evidence, int group, int eta, int myArrival) {
    return evidence.group == group ? evidence.confidence *
        camp_commitment::readiness(eta, myArrival, evidence.uncertainty) : 0.f;
}
static float campClaimedAllies(int group, int myArrival, const CampClaimRoster& roster, int excludeSlot) {
    if (!policy::TM_CAMP_ON || !gfReady(group)) return 0.f;
    float count = 0.f;
    for (int k = 0; k < team::T.n && k < team::MAX; ++k) {
        const auto& a = roster.mate[k];
        if (k != excludeSlot && a.group == group && a.confidence > 0.f)
            count += campClaimCredit(a, group, gfAt(s_gf[(size_t)group], a.pos), myArrival);
    }
    return count;
}
static void campPlanJoinCounts(const CampClaimRoster& roster, const int* slots, int n,
    const int* groups, const int* waits, int candidates, const int (*steps)[12], float (*counts)[12]) {
    std::memset(counts, 0, sizeof(float) * team::MAX * 12);
    // 실제 담당 하나에 목적지 하나. 계획의 걸음표를 다시 씀. 제안된 멤버는 담당하기 전에는 덤 없음.
    for (int j = 0; j < n; ++j) {
        const auto& e = roster.mate[slots[j]];
        if (e.group < 0 || e.confidence <= 0.f) continue;
        for (int c = 0; c < candidates; ++c) {
            if (groups[c] != e.group || steps[j][c] >= 65535) continue;
            for (int i = 0; i < n; ++i) if (i != j && steps[i][c] < 65535)
                counts[i][c] += campClaimCredit(e, groups[c], steps[j][c], std::max(steps[i][c], waits[c]));
        }
    }
}
static float gvtPick() {   /* GVT: 금 나눗수 = 남은 판 길이로 (라운드마다 한 번) */
    static int s_r = -1; static float s_v = 2.5f; const int r = io::turnRound();   /* FUELH */ if (r == s_r) return s_v; s_r = r;
    float rem = (float)(2000 - r); for (int t = 0; t < 2; ++t) { const float sub = (float)io::submitted(t == 0); const float rate = sub / (float)std::max(50, r); if (rate > 0.f) rem = std::min(rem, std::max(0.f, 1000.f - sub) / rate); }
    const float f = std::clamp(rem / 1000.f, 0.f, 1.f); s_v = 2.5f + 7.5f * (1.f - f); return s_v;
}
float goldDiv() { return gvtPick(); }
static float rshRho();
float myDeathTime() { return policy::DTR_ON ? policy::DEATH_TIME_GEMS * std::max(1.f, rshRho() / policy::KB_PRIOR_RATE) : policy::DEATH_TIME_GEMS; }   /* DTR */   /* VMR: 금 나눗수 공개 */
static float mgmK(const World& w, float div) {
    if(reference::PRISM_MACRO && w.me.type==reference::PRISM && reference::PRISM_GEM_VALUE>0.f){
        const float focusGrow=std::max(1.f,S().designOf(w.me.type).focusGrow);
        return 1.f+reference::PRISM_GEM_VALUE*rules::SHARD_COST*1.5f/focusGrow/std::max(1.f,div);
    }
    return (policy::MGM_ON && w.me.type == 7) ? 1.f + 2.f / std::max(1.f, div) : 1.f;
}   /* MGM · TWP 규칙 4: Prism 이 든 젬은 집중 샤드 대신 · Marcella 바친 젬은 금 */
float missionGems(const World& w) {
    switch (M.kind) {
    case M_CAPTURE: { if (M.group < 0) return 0.f; const Group& G = S().groups[(size_t)M.group]; return (float)G.gems * mgmK(w, gvtPick()) + (float)G.gold / gvtPick(); }   /* GVF: 고르기 V 와 같은 금 값 */   /* MGM */   /* GVX */
    case M_FARM: {   // 지금 곁에 있는 몸 수로 받을 피해를 셈해 가망만큼 할인 (죽을 싸움이면 0 → 들어가지 않고 짝을 기다린다)
        if (M.group < 0) return 0.f; const Group& G = S().groups[(size_t)M.group];
        const int n = 1 + alliesNear(w, 6);
        const float loss = lossPerBody(w, G, M.group, n);
        const float p = std::clamp(1.f - loss / std::max(1.f, w.me.hp), 0.f, 1.f);
        { const float share = ((float)G.gems * mgmK(w, gvtPick()) + (float)G.gold / gvtPick()) / (float)n; return std::max(0.f, share * p - (policy::DEATH_TIME_GEMS + (float)w.me.gems + (float)S().killGold / gvtPick()) * (1.f - p)); }   /* FRP: 같은 젬 단위 기대값 (0.3 + 0.7p 대신) */   /* GVF */   /* MGM */   /* GVX */
    }
    case M_TRIBUTE: return (float)w.me.gems;                       // 든 젬을 점수로 굳히는 값
    case M_RECOVER: return policy::HEAL_COST_GEMS + ((float)w.me.gems + policy::DEATH_TIME_GEMS + (float)S().killGold / goldDiv()) * (1.f - w.me.hp / std::max(1.f, w.me.maxHp));   /* X19 */
    case M_SHOP: return (float)w.me.gold / gvtPick();   /* X17 */
    default: return 0.f;
    }
}
/* PFX2: 무리 캠프 한 번 훑기 — 고르기 고리에서 무리마다 한 번 채우고 groupHpLeft · pnlLoss 가 같이 씀 */
struct GsSnap { std::uint32_t mask = 0; float hpLeft = 0.f, hsum = 0.f, hw = 0.f; };
static GsSnap g_gs; static int g_gsGi = -1;
static void gsFill(int gi) {
    g_gsGi = gi; const Group& G = S().groups[(size_t)gi]; GsSnap s; const bool lastOk = G.campLastHp.size() == G.campType.size();
    for (size_t k = 0; k < G.campType.size(); ++k) { if (!S().campAlive(G, k)) continue;
        const float h = k < G.campLastHp.size() ? G.campLastHp[k] : -1.f;
        if (lastOk) s.hpLeft += h > 0.f ? h : S().campHp(G.campType[k]);
        if (k < 32) { s.mask |= 1u << k; if (k < G.campLastHp.size()) { s.hsum += h; s.hw += h * (float)(k + 1); } } }
    if (!lastOk) s.hpLeft = G.hpTotal; g_gs = s;
}
float groupHpLeft(int gi) {
    if (gi < 0) return 0.f; const Group& G = S().groups[(size_t)gi];
    if (policy::PFX2_ON && g_gsGi == gi) return g_gs.hpLeft;   /* PFX2 */
    if (G.campLastHp.size() != G.campType.size()) return G.hpTotal;
    float s = 0.f;
    for (size_t k = 0; k < G.campType.size(); ++k) { if (!S().campAlive(G, k)) continue; const float h = G.campLastHp[k]; const int t = G.campType[k]; s += h > 0.f ? h : S().campHp(t); }   // ED
    return s;
}
int missionWorkTurns(const World& w) {
    switch (M.kind) {
    case M_FARM: { if (M.group < 0) return 1;
        if (policy::BH3_PK2_ON) { int n = 1; for (const auto& a : w.allies) if (cheb(a.pos, w.me.pos) <= 6) ++n;   /* BH3_PK2 */
            return std::max(1, (int)std::ceil(groupHpLeft(M.group) / std::max(1.f, w.me.str * (float)n))) + rules::CAPTURE_ROUNDS; }
        const Group& G = S().groups[(size_t)M.group]; return std::max(1, (int)(G.hpTotal / std::max(1.f, w.me.str))) + rules::CAPTURE_ROUNDS; }
    case M_CAPTURE: return rules::CAPTURE_ROUNDS;
    case M_TRIBUTE: return rules::tributeTicks(w.me.gems) + 1;
    case M_RECOVER: return (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1;   /* BH5_IT5 */
    default: return 1;
    }
}

static std::vector<int> g_avoidUntil;   // 그룹마다 피하는 라운드 (정체로 중단한 그룹)
static std::vector<int> g_cnBad;   /* CNC4: 무리마다 내려놓을 때의 엔진 점령 상태 (-1 없음) */
static bool cnBlocked(int gi) { if (gi < 0 || (size_t)gi >= g_cnBad.size() || g_cnBad[(size_t)gi] < 0) return false; const int now = io::groupCaptureState(S().groups[(size_t)gi].handle); if (now == g_cnBad[(size_t)gi]) return true; g_cnBad[(size_t)gi] = -1; return false; }
static void capAvoid(int g, int r) { if (g < 0) return; if (g_avoidUntil.size() != S().groups.size()) g_avoidUntil.assign(S().groups.size(), -1); g_avoidUntil[(size_t)g] = std::max(g_avoidUntil[(size_t)g], r + rules::CAPTURE_ROUNDS + 2); }   // CY

// 목표 그룹 곁의 다른 산 캠프 (3칸 안) 의 힘 합 — 거기 서 있는 동안 턴마다 받는 피해
static float goalThreat(int gi) {
    static int cachedRound = -1;
    static std::uint64_t cachedVersion = 0;
    static std::vector<float> cached;
    const int r = policy::PFX2_ON ? io::turnRound() : io::round();   /* PFX2 */
    if (cachedRound != r || cachedVersion != S().campVersion || cached.size() != S().groups.size()) {
        cached.assign(S().groups.size(), -1.f); cachedRound = r; cachedVersion = S().campVersion;
    }
    if (cached[(size_t)gi] >= 0.f) return cached[(size_t)gi];
    Group& G = io::S.groups[(size_t)gi]; float t = 0.f;
    if (!G.nearReady) {   // 처음 한 번: 다른 그룹 스폰 칸이 캠프 사거리 + 1 안이면 이웃 (첫 턴에 전부 하면 42² 로 20k — 물을 때 하나씩)
        G.nearReady = true;
        static std::vector<int> bx;   // FFR2: 그룹 스폰 상자 (x0 y0 x1 y1) — 상자 거리로 먼저 걸러 같은 답을 싸게 (큰 그룹 둘이면 스폰 쌍 수백~천 번 → 29k, 턴 넘침)
        if (bx.size() != S().groups.size() * 4) { bx.assign(S().groups.size() * 4, 0); for (size_t k = 0; k < S().groups.size(); ++k) { int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20); for (const auto& q : S().groups[k].spawn) { x0 = std::min(x0, q.x); y0 = std::min(y0, q.y); x1 = std::max(x1, q.x); y1 = std::max(y1, q.y); } bx[k * 4] = x0; bx[k * 4 + 1] = y0; bx[k * 4 + 2] = x1; bx[k * 4 + 3] = y1; } }
        auto boxD = [&](Pos p, size_t k) { const int dx = std::max({bx[k * 4] - p.x, p.x - bx[k * 4 + 2], 0}), dy = std::max({bx[k * 4 + 1] - p.y, p.y - bx[k * 4 + 3], 0}); return std::max(dx, dy); };
        const size_t g4 = (size_t)gi * 4;
        for (size_t k = 0; k < S().groups.size(); ++k) {
            if ((int)k == gi) continue; const Group& O = S().groups[k]; if (O.strMax <= 0.f) continue;
            const int rN = std::max(G.rangeMax, O.rangeMax);   // ED
            if (cheb(G.center, O.center) > rN + 6) continue;
            { const int dx = std::max({bx[k * 4] - bx[g4 + 2], bx[g4] - bx[k * 4 + 2], 0}), dy = std::max({bx[k * 4 + 1] - bx[g4 + 3], bx[g4 + 1] - bx[k * 4 + 3], 0}); if (std::max(dx, dy) > rN + 1) continue; }   // FFR2
            bool nearK = false; for (const auto& p : G.spawn) { if (boxD(p, k) > rN + 1) continue; for (const auto& q : O.spawn) if (cheb(p, q) <= rN + 1) { nearK = true; break; } if (nearK) break; }   // FFR2
            if (nearK) G.near.push_back((int)k);
        }
    }
    for (int k : G.near) { const Group& O = S().groups[(size_t)k]; if (O.awakeUntil < r) continue; bool alive = false; for (size_t j = 0; j < O.spawn.size(); ++j) if (S().campAlive(O, j)) { alive = true; break; } if (alive) t += O.strMax; }
    cached[(size_t)gi] = t;
    return t;
}

static void refreshGoalPassability(const World& w) {
    if (!policy::M_ALIVE_TILES || M.kind != M_FARM || M.group < 0) return;
    if (!S().groups[(size_t)M.group].nearReady) (void)goalThreat(M.group);
    const Group& G = S().groups[(size_t)M.group];
    std::uint64_t sig = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) { sig ^= v; sig *= 1099511628211ull; };
    mix((std::uint64_t)M.group); mix(G.passVersion);
    for (int gi : G.near) { mix((std::uint64_t)gi); mix(S().groups[(size_t)gi].passVersion); }
    if (M.goalPassSig != 0 && sig != M.goalPassSig) { ++g_goalTag; M.bestD = 99999; M.progressRound = w.round; io::nav.goalReason |= 4; }
    M.goalPassSig = sig;
}

// n 명이 함께 칠 때 한 몸이 받을 피해 (그룹 자신 + 깬 곁 캠프; 캠프는 한 번에 한 몸을 치니 n 으로 나눔)
static bool mgJungle() { static int r0 = -1; static bool v = false; const int r = io::turnRound();   /* FUELX */ if (r != r0) { r0 = r; v = io::itemCount(25) > 0 || io::itemCount(27) > 0 || io::itemCount(29) > 0; } return v; }   // MG: 정글 엠블럼 (캠프 피해 −30%)
static float mgTeamDps(const World& w, int n) {   // MG: 나 + 가장 센 팀원 n−1 (보이면 실제 값, 모자라면 내 값)
    static int s_r = -1; static float mine = 0.f, o[12]; static int k = 0;   /* FUELX: 라운드마다 한 번 */
    if (s_r != io::turnRound()) { s_r = io::turnRound(); mine = ksDps(w.me.type, w.me.str, w.me.focus); k = 0; for (const auto& a : w.allies) if (a.visible && k < 12) o[k++] = ksDps(a.type, a.str, a.focus);
        std::sort(o, o + k, [](float x, float y) { return x > y; }); }
    float s = mine; for (int i = 0; i < n - 1; ++i) s += i < k ? o[i] : mine;
    return std::max(1.f, s);
}
static float mgLoss(const World& w, const Group& G, int gi, int n) {   // MG: 한 몸이 받을 피해
    const float left = std::max(1.f, groupHpLeft(gi));
    const float T = left / mgTeamDps(w, n);
    float sumStr = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) { if (!S().campAlive(G, k)) continue; sumStr += S().campStr(G.campType[k]); }
    const float red = mgJungle() ? 0.7f : 1.f;
    return (T * sumStr * red + goalThreat(gi) * (T + (float)rules::CAPTURE_ROUNDS)) / (float)n;
}
static float lossPerBody(const World& w, const Group& G, int gi, int n) {
    if (G.mono) return mgLoss(w, G, gi, n);   // MG: 모노리스
    const float ourStr = std::max(1.f, w.me.str) * (float)n;
    const int turns = (int)(G.hpTotal / ourStr) + rules::CAPTURE_ROUNDS;
    return (G.hpTotal / ourStr * G.strMax + goalThreat(gi) * (float)turns) / (float)n;
}
// n 명이면 갈 만한가: 받을 피해가 체력 × MAX_LOSS_HP 안이고 (중간에 돌아와 회복하며 끝낼 수 있음), 값이 남는다
static bool msStrict(const World& w, const Group& G, int gi, int n) {   // MS: 빨리 끝내고 버틸 수 있을 때만
    const float T = std::max(1.f, groupHpLeft(gi)) / mgTeamDps(w, n);
    if (T > policy::MS_TMAX) return false;
    float sumStr = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) { if (!S().campAlive(G, k)) continue; sumStr += S().campStr(G.campType[k]); }
    const float dmg = T * sumStr * (mgJungle() ? 0.7f : 1.f) + goalThreat(gi) * T;
    float hp[12]; int k = 0; for (const auto& a : w.allies) if (a.visible && k < 12) hp[k++] = a.hp;
    std::sort(hp, hp + k, [](float x, float y) { return x > y; });
    float pool = w.me.hp; for (int i = 0; i < n - 1; ++i) pool += i < k ? hp[i] : w.me.hp;
    return dmg <= policy::MS_HPFRAC * pool;
}
static bool worthWith(const World& w, const Group& G, int gi, int n) {
    if (G.mono && !msStrict(w, G, gi, n)) return false;   // MS
    const float loss = lossPerBody(w, G, gi, n);
    if (loss > w.me.hp * policy::MAX_LOSS_HP) return false;
    return ((float)G.gems * mgmK(w, gvtPick()) + (float)G.gold / gvtPick()) / (float)n > combat::dmgCost(   /* GVF */loss, w.me.hp, w.me.maxHp, w.me.gems);   // 보상도 n 으로 나뉨; 비용은 회복 시간 + 바닥 위험
}
// 그룹이 지금 농사 자격이 있나 (내가 아는 한). 0 = 아님, n ≥ 1 = 나를 포함해 n 명이면 감당하고 값이 남는다 (지금 담당 중인 남 others 는 이미 있는 것으로)
// bot (3)의 마무리 판단도 공통 개별 몹 생존 정보를 사용한다.
static bool g_cxeBack = false;   /* CXE3: 이번 턴 어그로를 동료에게 넘기고 물러남 */
bool cxeBackOff() { return g_cxeBack; }
static int cxeState(const World& w) {   /* CXE3: 0 괜찮음 · 1 동료에게 넘김 (물러나 계속) · 2 넘길 동료 없음 (떠남) */
    if (VIS_NEAR(w)) return 0;
    float s = 0.f; int ex = 0; bool canHand = true; const Pos me = w.me.pos;
    for (const auto& k : w.camps) { if (k.str <= 0.f || k.group < 0 || (size_t)k.group >= S().groups.size()) continue; const int rg = S().campRange(k.type); const int dm = cheb(k.pos, me); if (dm > rg) continue;
        const Group& KG = S().groups[(size_t)k.group]; if (!(KG.awakeUntil >= w.round || k.hp < k.maxHp)) continue;
        bool nearest = true; for (const auto& a : w.allies) if (a.visible && a.alive) { const int da = cheb(k.pos, a.pos); if (da < dm) { nearest = false; break; }
            if (policy::TIE2_ON && da == dm) { const int pa = io::priorityNow(a.handle), pm = io::priorityNow(w.me.handle); if (pa > pm || (pa == pm && a.handle < w.me.handle)) { nearest = false; break; } } }   /* TIE2: 동거리면 먼저 두는 아군이 맞음 */
        if (!nearest) continue; s += k.str;
        int allyD = 99; for (const auto& a : w.allies) if (a.visible && a.alive) { const int da = cheb(k.pos, a.pos); if (da <= rg) allyD = std::min(allyD, da); }
        if (allyD >= 99) canHand = false;
        ex = std::max(ex, policy::CXE_ROT && allyD < 99 ? std::max(1, allyD - dm + 1) : rg + 1 - dm); }   /* CXE2: 사거리 안 동료가 있으면 그보다 멀어지면 어그로가 넘어감 (교대) */
    if (!(s > 0.f && w.me.hp <= s * (float)(ex + 1) + 0.1f * w.me.maxHp)) return 0;
    return (canHand && w.me.hp >= policy::RECOVER_HP * w.me.maxHp) ? 1 : 2;
}
static float campStrSum(const Group& G) { float s = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) if (S().campAlive(G, k)) s += S().campStr(G.campType[k]); return s * (mgJungle() ? 0.7f : 1.f); }   /* ROT · 반사실: 산 캠프 힘 합 */
static float hldReserve(const Group& G) {   /* HLD: 빠져나갈 몫 = 산 캠프 힘 합 × (리콜 채널 + 1R) */
    float s = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) if (S().campAlive(G, k)) s += S().campStr(G.campType[k]);
    return s * (mgJungle() ? 0.7f : 1.f) * (float)(io::recallLen() + 1); }
static bool hldStay(const World& w) {   /* HLD: 치던 무리는 죽지 않는 한 버팀 */
    if (!policy::HLD_ON || M.kind != M_FARM || M.group < 0 || (size_t)M.group >= S().groups.size() || VIS_NEAR(w) || w.round - M.lastHitRound > 2) return false;
    const Group& G = S().groups[(size_t)M.group]; if (!(G.state == 0 || G.state == 1)) return false;
    return w.me.hp > hldReserve(G); }
static bool finCanFinish(const World& w) {
    if (M.kind == M_CAPTURE && M.group >= 0 && !VIS_NEAR(w)) return true;
    if (w.round - M.lastEnemyRound <= 10) return false;
    if (M.kind != M_FARM || M.group < 0 || VIS_NEAR(w) || M.lastHitRound < w.round - 2) return false;
    const Group& G = S().groups[(size_t)M.group]; if (G.mono || G.state != 1) return false;
    float sumStr = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) if (S().campAlive(G, k)) sumStr += S().campStr(G.campType[k]);
    sumStr *= mgJungle() ? 0.7f : 1.f;
    int n = 1; for (const auto& a : w.allies) if (a.visible && cheb(a.pos, G.center) <= G.rangeMax + 1) ++n;
    const float ttk = std::max(1.f, groupHpLeft(M.group)) / mgTeamDps(w, n);
    if (sumStr <= 0.f) return true;
    if (policy::STF_ON && n >= 2) { const float ttdT = (w.me.hp - 2.f * sumStr) / (sumStr / (float)n); return ttdT > 0.f && ttk <= ttdT; }   /* STF: 함께 치면 피해가 나뉜다 (2R 몫은 남김) */
    const float ttd = (w.me.hp - sumStr) / sumStr;
    return ttd > 0.f && ttk <= ttd;
}
static int g_csjSince = -1000;   /* CSJ2: 값으로 합류한 점령 (CO 가 되돌리지 않음) */
static int g_lastPickRound = -1000;   // (IDLE) 앞으로 옮김: 임무를 내려놓는 곳에서 리셋
static int farmable(const World& w, int gi, int& waitLeft, int& othersOut) {
    const Group& G = S().groups[(size_t)gi];
    waitLeft = 0; othersOut = 0;
    if (G.gems <= 0 && G.gold <= 0) return 0;
    // MG: Z (모노리스는 치지 않는다) 제거 — 일반 판단으로
    if ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) return 0;
    if (cnBlocked(gi)) return 0;   /* CNC4 */
    if (G.state == 3) waitLeft = std::max(0, G.respawnUntil - w.round);
    if (G.state == 2) return 0;                        // 점령 대기는 CAPTURE 로
    const int others = team::claimsOn(gi, (1 << team::CL_FARM) | (1 << team::CL_CAPTURE));   // 나 뺀 팀원 ≤ team::MAX − 1
    othersOut = others;
    if (io::fuel() < zres() + 8000) return PICK_DEFERRED;   /* PFX2: 한 번 */
    for (int n = 1 + others; n <= (policy::BUG5_ON ? std::max(1, team::T.n) : team::MAX); ++n) {   /* BUG5 */   // DD: 인원 상한 없음 (팀 전원까지 본다)
        if (worthWith(w, G, gi, n)) return n;
    }
    if (log::ON && w.round <= 6 && G.gems > 0) for (int n = 1; n <= team::MAX; ++n) NB_LINE("#nbw %d %d g=%d n=%d loss=%.0f hpLim=%.0f gain=%.1f cost=%.1f str=%.0f", w.round, S().myIndex, gi, n, lossPerBody(w, G, gi, n), w.me.hp * policy::MAX_LOSS_HP, ((float)G.gems + (float)G.gold / 10.f) / (float)n, combat::dmgCost(lossPerBody(w, G, gi, n), w.me.hp, w.me.maxHp, w.me.gems), w.me.str);
    return 0;
}
static int alliesNear(const World& w, int r) { int n = 0; for (const auto& a : w.allies) if (cheb(a.pos, w.me.pos) <= r) ++n; return n; }

extern int g_tfpTarget; extern bool g_tfpMember; extern int g_tfpK;   /* TFP2 */
static void setGoal(Kind k, int group, int altar, const World& w) {
    if (policy::RTS_LOG_ON && k == M_RECOVER && M.kind == M_FARM) { int nA = 0, nE = 0, cR = 0; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= 6) ++nA; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, w.me.pos) <= 8) ++nE;
        for (const auto& c : w.camps) if (c.str > 0.f && c.group == M.group) cR = std::max(cR, S().campRange(c.type));
        float bud = std::max(0.f, w.me.hp - policy::RECOVER_HP * w.me.maxHp); int nb = 1; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= 6) { bud += std::max(0.f, a.hp - policy::RECOVER_HP * a.maxHp); ++nb; }
        const float hl = (M.group >= 0 && (size_t)M.group < S().groups.size()) ? groupHpLeft(M.group) : 0.f, sS = (M.group >= 0 && (size_t)M.group < S().groups.size()) ? campStrSum(S().groups[(size_t)M.group]) : 0.f, dps = mgTeamDps(w, nb);
        log::always("#rec %d %d hp=%.0f/%.0f nE=%d nA=%d myR=%d campR=%d vn=%d g=%d hl=%.0f s=%.1f dps=%.1f bud=%.0f nb=%d", w.round, team::T.slot, w.me.hp, w.me.maxHp, nE, nA, w.me.range, cR, (int)VIS_NEAR(w), M.group, hl, sS, dps, bud, nb); }
    M.kind = k; M.group = group; M.altar = altar; M.since = w.round; M.lethalStreak = 0; M.bestD = 99999; M.progressRound = w.round; M.blind.clear(); M.lastHitRound = -1000; M.goalPassSig = 0; M.retreat = false; ++g_goalTag;
}
static void abandon(const World& w, const char* why) {   // 정체 · 위험으로 중단: 그 그룹은 한동안 피한다
    if (M.group >= 0) { if (g_avoidUntil.size() != S().groups.size()) g_avoidUntil.assign(S().groups.size(), -1); g_avoidUntil[(size_t)M.group] = w.round + policy::AVOID_ROUNDS; }
    setGoal(M_NONE, -1, -1, w); M.why = why;
}

static std::vector<std::vector<Pos>> s_apr; static std::vector<std::uint8_t> s_aprDone;   // APC: 그룹마다 다가설 칸 (한 번)
static const std::vector<Pos>& aprTiles(int gi) {
    if (s_apr.size() != S().groups.size()) { s_apr.assign(S().groups.size(), {}); s_aprDone.assign(S().groups.size(), 0); }
    if (!s_aprDone[(size_t)gi]) { s_aprDone[(size_t)gi] = 1; auto& o = s_apr[(size_t)gi]; const Group& G = S().groups[(size_t)gi];
        for (const auto& sp : G.spawn) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos p{sp.x + dx, sp.y + dy}; if (!S().inb(p) || !S().walkableStatic(p) || (S().tile[(size_t)S().idx(p)] & T_CAMP)) continue; o.push_back(p); } rootDedupe(o); }   // FFR
    return s_apr[(size_t)gi];
}
static std::vector<std::uint8_t> s_unr; static std::vector<std::uint64_t> s_unrV;   // UNR: 걸어서 못 가는 그룹 (확인되면 기억; BUG7: 확인한 campVersion 도 남김)
static Pos s_ucpPos{-1, -1};   /* UCP: 이번 턴 내 칸 */
static bool ucpNoCap(int gi, Pos p) { return gi >= 0 && p.x >= 0 && gfReady(gi) && gfAt(s_gf[(size_t)gi], p) >= 65535; }   /* UCP: 스폰 둘레에 걸어서 못 감 = 점령 못 함 */
bool groupUnreachable(int gi) { return (policy::UNR_ON && gi >= 0 && (size_t)gi < s_unr.size() && s_unr[(size_t)gi]) || ucpNoCap(gi, s_ucpPos); }   /* NFX · UCP */
long long g_crsCb = policy::ZO_COMBAT_RES;   /* CRS */
static long long fuelSpare(const World& w) { return io::fuel() - ((policy::CRS_ON && w.enemyNear8) ? g_crsCb : 0); }   /* CRS: 전투 몫을 뗀 연료 */
static void tryPickFarm(const World& w, int except);   /* MFW3 */
void warmMe(const World& w) { (void)ensureCurrentMe(w); if (M.kind == M_NONE) tryPickFarm(w, -1);   /* MFW3: 첫 고르기를 첫 턴에 */
    for (int gi = 0; gi < (int)S().groups.size(); ++gi) { if (io::fuel() < policy::LC_PREWARM_FLOOR + 20000) break; (void)aprTiles(gi); } }   /* MFW: 첫 턴 남는 연료로 내 칸 거리장 + 무리 다가갈 칸 (r2 탐침: 무리 4개에 17~38k) */
static int tpsVia(const World& w, int gi, int walk);   /* TPS */
/* PFX: 정적 연결 성분 (벽만 · 8방향) 을 남는 연료에 여러 턴 나눠 채움. 성분이 다르면 캠프와 무관하게 영영 못 감 */
static RawVec<std::int16_t> s_comp; static std::vector<int> s_compQ; static size_t s_compQh = 0; static int s_compN = 0, s_compScan = 0; static bool s_compReady = false;
static std::vector<std::int16_t> s_compG;   // 무리마다 뿌리 칸 성분 4개 (-1 빈칸 · -2 넘침 · -3 뿌리 없음 = 모름)
void compStep(long long floorFuel) {
    if (!policy::PFX_ON || s_compReady || !io::wallsReady()) return;
    if (S().dashNav && !io::bridgesReady()) return;   /* DSH · BRG2: 성분은 다리 표가 끝난 뒤 (표는 prewarmGF 가 채움) */
    const int W = S().W, H = S().H; if (W <= 0 || H <= 0) return;
    if (s_comp.size() != (size_t)(W * H)) { s_comp.resize((size_t)(W * H)); std::memset(s_comp.data(), 0xFF, s_comp.size() * 2); s_compQ.clear(); s_compQ.reserve((size_t)(W * H)); s_compQh = 0; s_compN = 0; s_compScan = 0; }
    const long long f0 = io::fuel();
    while (io::fuel() >= floorFuel + 6000 && f0 - io::fuel() < 20000) {   /* CSF ③ */
        if (s_compQh < s_compQ.size()) { const int i = s_compQ[s_compQh++]; const Pos p{i % W, i / W}; const std::int16_t cc = s_comp[(size_t)i];
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{p.x + dx, p.y + dy}; if (!S().passNav(q)) continue; const size_t j = (size_t)S().idx(q); if (s_comp[j] >= 0) continue; s_comp[j] = cc; s_compQ.push_back((int)j); }
            continue; }
        { int sc = 0; while (s_compScan < W * H && (s_comp[(size_t)s_compScan] >= 0 || !S().passNav(Pos{s_compScan % W, s_compScan / W}))) { ++s_compScan; if (++sc >= 256) break; } if (sc >= 256) continue; }   /* CSF ①: 256칸마다 연료 확인 */
        if (s_compScan >= W * H) { s_compReady = true; std::vector<int>().swap(s_compQ); return; }
        s_comp[(size_t)s_compScan] = (std::int16_t)s_compN++; s_compQ.push_back(s_compScan);
    }
}
static const std::vector<Pos>& aprTiles(int gi);
static bool compUnreach(const World& w, int gi) {   // 내 성분에 무리 뿌리 칸이 하나도 없으면 참
    if (!policy::PFX_ON || !s_compReady || !S().inb(w.me.pos)) return false;
    const int mc = s_comp[(size_t)S().idx(w.me.pos)]; if (mc < 0) return false;
    if (s_compG.size() != S().groups.size() * 4) s_compG.assign(S().groups.size() * 4, -1);
    std::int16_t* cg = &s_compG[(size_t)gi * 4];
    if (cg[0] == -1) { int n = 0; for (Pos t : aprTiles(gi)) { if (!S().inb(t)) continue; const std::int16_t v = s_comp[(size_t)S().idx(t)]; if (v < 0) continue; bool dup = false; for (int i = 0; i < n; ++i) if (cg[i] == v) dup = true; if (dup) continue; if (n >= 4) { cg[0] = -2; break; } cg[n++] = v; }
        if (n == 0 && cg[0] == -1) cg[0] = -3; }
    if (cg[0] < -1) return false;
    for (int i = 0; i < 4 && cg[i] >= 0; ++i) if (cg[i] == mc) return false;
    return true;
}
static int approachSteps(const World& w, int gi, bool dynamic) {
    if (compUnreach(w, gi)) return 65535;   /* PFX */
    if (policy::UNR_ON) { if (s_unr.size() != S().groups.size()) { s_unr.assign(S().groups.size(), 0); s_unrV.assign(S().groups.size(), 0); } if (s_unr[(size_t)gi] && (!policy::BUG7_ON || s_unrV[(size_t)gi] == (policy::PFX_ON ? S().openVersion : S().campVersion))) return 65535; }   /* PFX: 죽음에만 다시 묻기 */   /* BUG7: 길이 바뀌면 다시 묻는다 */
    const int cached = policy::GF_PICK && gfReady(gi) ? gfAt(s_gf[(size_t)gi], w.me.pos) : -1;
    if (!dynamic && cached >= 0 && cached < 65535) { if (policy::BUG7_ON && policy::UNR_ON) s_unr[(size_t)gi] = 0; return policy::TPS_ON ? tpsVia(w, gi, cached) : cached; }   /* TPS */   /* BUG7 */
    if (!ensureCurrentMe(w)) return PICK_DEFERRED;
    const auto& tiles = aprTiles(gi);
    Pos first, second; int c1 = 99999, c2 = 99999;
    for (Pos p : tiles) {
        const int c = cheb(p, w.me.pos);
        if (c < c1) { c2 = c1; second = first; c1 = c; first = p; }
        else if (c < c2) { c2 = c; second = p; }
    }
    int best = 65535;
    for (Pos p : {first, second}) if (p.x >= 0) {
        const int d = checkedMeDistance(p); if (d == PICK_DEFERRED) return best < 65535 ? best : PICK_DEFERRED;
        best = std::min(best, d);
    }
    // 두 가까운 칸의 INF는 전체 후보의 도달 불가 증명이 아니다.
    if (best == 65535) for (Pos p : tiles) {
        if (p == first || p == second) continue;
        const int d = checkedMeDistance(p); if (d == PICK_DEFERRED) return PICK_DEFERRED;
        if (d < 65535) { best = d; break; }
    }
    if (cached >= 65535 && best < 65535) ++io::nav.rescued;
    if (policy::UNR_ON && best >= 65535) { s_unr[(size_t)gi] = 1; s_unrV[(size_t)gi] = policy::PFX_ON ? S().openVersion : S().campVersion; } else if (policy::BUG7_ON && policy::UNR_ON) s_unr[(size_t)gi] = 0;   /* UNR */   /* BUG7 */
    return policy::TPS_ON ? tpsVia(w, gi, best) : best;   /* TPS */
}
// PNL: 캠프 손익 (예측 검증된 모델)
static int pfxAoeCnt(const Group& G, int slot, int radius, bool full = false) {   /* BH3_PK6: full = 모든 spawn (리스폰 뒤 전체) */   /* PFX2: 광역 대상 수 — (무리, 산 캠프 마스크) 로 저장 */
    const size_t gi = (size_t)(&G - S().groups.data());
    static std::vector<std::uint32_t> km; static std::vector<int> kc; if (km.size() != S().groups.size() * 2) { km.assign(S().groups.size() * 2, 0xFFFFFFFFu); kc.assign(S().groups.size() * 2, 1); }
    std::uint32_t mask = 0xFFFFFFFFu;
    const bool pk6F = policy::BH3_PK6_ON && full;   /* BH3_PK6 */
    if (policy::PFX2_ON && G.spawn.size() <= 32) { if (pk6F) mask = G.spawn.size() >= 32 ? 0xFFFFFFFFu : ((1u << G.spawn.size()) - 1u); else if (g_gsGi == (int)gi) mask = g_gs.mask; else { mask = 0; for (size_t k = 0; k < G.spawn.size(); ++k) if (S().campAlive(G, k)) mask |= 1u << k; }
        if (km[gi * 2 + (size_t)slot] == mask) return kc[gi * 2 + (size_t)slot]; }
    int best = 0; for (size_t i = 0; i < G.spawn.size(); ++i) { if (!pk6F && !S().campAlive(G, i)) continue; int cnt = 0; for (size_t j = 0; j < G.spawn.size(); ++j) if ((pk6F || S().campAlive(G, j)) && cheb(G.spawn[i], G.spawn[j]) <= radius) ++cnt; best = std::max(best, cnt); }
    const int r = std::max(1, best); if (mask != 0xFFFFFFFFu) { km[gi * 2 + (size_t)slot] = mask; kc[gi * 2 + (size_t)slot] = r; } return r;
}
float wkdSpeed(const World& w) {   /* WKD: 칸/라운드 = 1 + 대시 사거리 / (쿨다운 + 1) (설계값) */
    const ChampDesign& D = S().designOf(w.me.type); float v = 1.f;
    if (D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH) v += D.ab[s].range / (float)(D.ab[s].cooldown + 1);
    return v; }
static float pnlDpsFactor(const World& w, const Group& G, bool full = false) {   /* BH3_PK6: full -> pfxAoeCnt 전체 spawn */   // 몬스터 엠블럼 · 광역 실제 대상 수 (ksDps 대비 배율)
    static int s_monR = -1; static bool s_mon = false; if (s_monR != io::turnRound()) { s_monR = io::turnRound(); s_mon = io::itemCount(1) > 0 || io::itemCount(3) > 0 || io::itemCount(5) > 0; }   /* FUELY */
    const bool mon = s_mon; const float mk = mon ? 1.15f : 1.f;   /* X18 */
    /* PFX4: 라운드 상수 부분 (설계 · 힘 · 집중 · 엠블럼) 한 번 — 무리마다는 광역 대상 수만. 더하는 순서는 원래와 같다 */
    static int s_pR = -1; static float s_str = -1.f, s_foc = -1.f; static int s_ty = -1; static float s_base = 1.f, s_adj0 = 1.f; static int s_na = 0; static float s_vm[2]; static int s_slot[2], s_rad[2];
    if (!policy::PFX4_ON || s_pR != io::turnRound() || s_str != w.me.str || s_foc != w.me.focus || s_ty != w.me.type) { s_pR = io::turnRound(); s_str = w.me.str; s_foc = w.me.focus; s_ty = w.me.type; s_na = 0;
        const ChampDesign& D = S().designOf(w.me.type); s_base = std::max(1.f, w.me.str); s_adj0 = std::max(1.f, w.me.str) * mk;
        if (D.valid) for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s]; if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_AOE) continue; if (!a.targetsCamps && !(a.kind == rules::AB_AOE && a.targetsGround)) continue;
            const float v = w.me.focus * a.modifier / (float)(a.cooldown + 1); s_base += v;
            if (a.kind == rules::AB_AOE && a.radius > 0.f) { s_vm[s_na] = v * mk; s_slot[s_na] = s; s_rad[s_na] = (int)a.radius; ++s_na; } else s_adj0 += v * mk; } }
    if (!policy::PFX4_ON) {   // 원래 식 (검증용)
        const ChampDesign& D = S().designOf(w.me.type); float base = std::max(1.f, w.me.str), adj = std::max(1.f, w.me.str) * mk;
        if (D.valid) for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s]; if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_AOE) continue; if (!a.targetsCamps && !(a.kind == rules::AB_AOE && a.targetsGround)) continue;
            const float v = w.me.focus * a.modifier / (float)(a.cooldown + 1); base += v; int cnt = 1; if (a.kind == rules::AB_AOE && a.radius > 0.f) cnt = pfxAoeCnt(G, s, (int)a.radius, full); adj += v * mk * (float)cnt; }
        return adj / base; }
    float adj = s_adj0; for (int i = 0; i < s_na; ++i) adj += s_vm[i] * (float)pfxAoeCnt(G, s_slot[i], s_rad[i], full);
    return adj / s_base;
}
static float pnlLoss(const Group& G, int n, float dps, bool full = false) {   /* RSH: full = 리스폰 뒤 모든 캠프 (기본 체력) */   // PNLD: 무리 전체가 깨어나 함께 침 — 살아 있는 캠프 모두가 잡힐 때까지 · 한 몸이 몰아 맞음 (PNLD_TOP) · 정글 · 내구
    static int s_durR = -1; static bool s_dur = false; if (s_durR != io::turnRound()) { s_durR = io::turnRound(); s_dur = io::itemCount(13) > 0 || io::itemCount(15) > 0 || io::itemCount(17) > 0; }   /* FUELY */
    const bool dur = s_dur;
    const float red = (mgJungle() ? 0.7f : 1.f) * (dur ? 0.9f : 1.f); float sum = 0.f;
    struct PlC { std::uint32_t mask = 0xFFFFFFFFu; float hsum = -1.f, hw = -1.f; bool full = false; int nc = 0; float hs[32], ss[32]; float coef = 0.f; };   /* PFX3: coef */   /* FUELY: 무리마다 정렬 목록 저장 */
    static std::vector<PlC> s_plc; if (s_plc.size() != S().groups.size()) s_plc.assign(S().groups.size(), PlC{});
    const size_t gix = (size_t)(&G - S().groups.data());
    std::uint32_t mask = 0; float hsum = 0.f, hw = 0.f;
    if (policy::PFX2_ON && !full && g_gsGi == (int)gix) { mask = g_gs.mask; hsum = g_gs.hsum; hw = g_gs.hw; }   /* PFX2: 스냅숏 */
    else for (size_t k = 0; k < G.campType.size() && k < 32; ++k) { if (!full && !S().campAlive(G, k)) continue; mask |= 1u << k; if (!full && k < G.campLastHp.size()) { hsum += G.campLastHp[k]; hw += G.campLastHp[k] * (float)(k + 1); } }
    PlC& pc = s_plc[gix];
    if (pc.mask != mask || pc.hsum != hsum || pc.hw != hw || pc.full != full) { pc.mask = mask; pc.hsum = hsum; pc.hw = hw; pc.full = full; int nc0 = 0;
        for (size_t k = 0; k < G.campType.size() && nc0 < 32; ++k) { if (!full && !S().campAlive(G, k)) continue; const int ty = G.campType[k];
            pc.hs[nc0] = (!full && k < G.campLastHp.size() && G.campLastHp[k] > 0.f) ? G.campLastHp[k] : S().campHp(ty); pc.ss[nc0] = std::max(0.f, S().campStr(ty)); ++nc0; }
        for (int i = 1; i < nc0; ++i) for (int j = i; j > 0 && pc.hs[j] < pc.hs[j - 1]; --j) { std::swap(pc.hs[j], pc.hs[j - 1]); std::swap(pc.ss[j], pc.ss[j - 1]); }
        pc.nc = nc0; { float sA = 0.f; for (int i = 0; i < nc0; ++i) sA += pc.ss[i]; float cf = 0.f; for (int i = 0; i < nc0; ++i) { cf += sA * pc.hs[i]; sA -= pc.ss[i]; } pc.coef = cf; } }   /* PFX3 */
    if (policy::PFX3_ON) sum = pc.coef / std::max(1.f, dps);
    else { const int nc = pc.nc; const float* hs = pc.hs; const float* ss = pc.ss;
    float sAlive = 0.f; for (int i = 0; i < nc; ++i) sAlive += ss[i];
    for (int i = 0; i < nc; ++i) { sum += sAlive * hs[i] / std::max(1.f, dps); sAlive -= ss[i]; } }
    return sum * red * std::max(1.f / (float)std::max(1, n), policy::PNLD_TOP);
}
static int fpkRad(int gi) {   /* FPK: 그룹 중심에서 뿌리 칸 (스폰 칸 + 1) 까지 가장 먼 cheb — 거리장 값 >= cheb(중심, 칸) - 반경 */
    static std::vector<int> r; static int rMax = 0; if (r.size() != S().groups.size()) { r.assign(S().groups.size(), 0); rMax = 0; for (size_t k = 0; k < r.size(); ++k) { int m = 0; for (const auto& s : S().groups[k].spawn) m = std::max(m, cheb(s, S().groups[k].center)); r[k] = m + 1; rMax = std::max(rMax, r[k]); } }
    return gi < 0 ? rMax : r[(size_t)gi];
}
static float rshRho();   /* RSH3: 이 몸의 벌이 / R (아래 KB 장부) */
static int pfxHomeC(int gi) {   /* PFX2: 무리 중심 집 걸음 — 집 거리장이 있으면 한 번만 */
    if (!policy::PFX2_ON) return homeSteps(S().groups[(size_t)gi].center);
    if (policy::BH3_PK1_ON) {   /* BH3_PK1: fcqHome 처럼 집 거리장 판이 바뀌면 다시 셈 */
        static std::vector<int> c, cv; if (c.size() != S().groups.size()) { c.assign(S().groups.size(), -2); cv.assign(S().groups.size(), -1); }
        if (cv[(size_t)gi] != g_homeVer) { const int h = homeSteps(S().groups[(size_t)gi].center); if (s_gfHome.empty()) return h; c[(size_t)gi] = h; cv[(size_t)gi] = g_homeVer; }
        return c[(size_t)gi]; }
    static std::vector<int> c; if (c.size() != S().groups.size()) c.assign(S().groups.size(), -2);
    int& v = c[(size_t)gi]; if (v == -2) { const int h = homeSteps(S().groups[(size_t)gi].center); if (s_gfHome.empty()) return h; v = h; } return v;
}
static float stkVal(int gi, float dv) { const Group& G = S().groups[(size_t)gi]; return (float)G.gems + (float)G.gold / dv; }
static bool stkHomeBig(int gi) {   /* STK · INV: 우리 집 쪽 큰 무리 (값 >= 판 최대 무리 값의 50% = 래더 장부와 같은 정의) */
    static int s_r = -1; static float s_mx = 0.f, s_dv = 1.f;
    if (s_r != io::turnRound()) { s_r = io::turnRound(); s_mx = 0.f; s_dv = gvtPick(); for (size_t g = 0; g < S().groups.size(); ++g) s_mx = std::max(s_mx, stkVal((int)g, s_dv)); }
    const Group& G = S().groups[(size_t)gi];
    if (cheb(G.center, S().allyCenter) >= cheb(G.center, S().enemyCenter)) return false;
    return s_mx > 0.f && stkVal(gi, s_dv) >= 0.5f * s_mx; }
static float stkMul(int gi) { return (policy::STK_ON && stkHomeBig(gi)) ? 1.f + policy::STK_P : 1.f; }
static float invStakeOf(const World& w, const Unit& e) { return (float)e.gems + (float)S().killGold / gvtPick() + policy::DEATH_TIME_GEMS + combat::enemyTwinAdd(w, e); }   /* BH4_MC6 */
static float invStake(const World& w, int gi) {   /* INV: 이 무리 곁에 혼자 든 보이는 적의 판돈 (없으면 0) */
    if (!policy::INV_ON || w.enemies.empty()) return 0.f;
    const Group& G = S().groups[(size_t)gi]; if (!(G.state == 0 || G.state == 1) || !stkHomeBig(gi)) return 0.f;
    float best = 0.f;
    for (const auto& e : w.enemies) { if (!e.alive || cheb(e.pos, G.center) > G.rangeMax + 1) continue;
        bool lone = true; for (const auto& o : w.enemies) if (o.alive && o.handle != e.handle && cheb(o.pos, e.pos) <= rules::VISION) { lone = false; break; }
        if (lone) best = std::max(best, invStakeOf(w, e)); }
    return best; }
static float invMax(const World& w) { float m = 0.f; if (policy::INV_ON) for (const auto& e : w.enemies) if (e.alive) m = std::max(m, invStakeOf(w, e)); return m; }
static int rngRangeOf(const World& w, std::uint32_t h) {   /* RNG: 아군 공격 사거리 (마지막으로 본 값, 라운드마다 한 번 갱신) */
    static std::uint32_t hs[16]; static int rs[16]; static int n = 0, s_r = -1;
    if (s_r != io::turnRound()) { s_r = io::turnRound(); for (const auto& al : w.allies) { if (!al.visible || al.type < 0) continue; int k = 0; for (; k < n; ++k) if (hs[k] == al.handle) break; if (k == n) { if (n >= 16) continue; hs[n++] = al.handle; } rs[k] = al.range; } }
    for (int k = 0; k < n; ++k) if (hs[k] == h) return rs[k];
    return -1; }
static int g_opkSeen = 0; static bool g_opkTrunc = false;   /* OPK */
static float terrPOwn(int gi, float L) {   /* TERR T3: 늦게 닿으면 상대가 먼저 쳐 가져갈 확률을 뺀 몫. 래더 210판 (terr3b.py): 깊이 띠별 상대 먼저 침 몫 h x 상대 첫 타 지연 누적 */
    if (L <= 0.f) return 1.f;
    const Group& G = S().groups[(size_t)gi];
    const float du = std::hypot((float)(G.center.x - S().allyCenter.x), (float)(G.center.y - S().allyCenter.y)), de = std::hypot((float)(G.center.x - S().enemyCenter.x), (float)(G.center.y - S().enemyCenter.y));
    const float f = du / std::max(1.f, du + de); const int b = f < 0.45f ? 0 : (f <= 0.55f ? 1 : 2);
    static const float HB[3] = {0.144f, 0.532f, 0.704f};
    static const float LX[9] = {0.f, 3.f, 6.f, 10.f, 15.f, 20.f, 30.f, 45.f, 60.f};
    static const float CB[3][9] = {{0.08f, 0.27f, 0.41f, 0.51f, 0.63f, 0.74f, 0.84f, 0.92f, 1.f}, {0.08f, 0.25f, 0.36f, 0.47f, 0.57f, 0.65f, 0.79f, 0.92f, 1.f}, {0.05f, 0.21f, 0.31f, 0.40f, 0.50f, 0.59f, 0.72f, 0.88f, 1.f}};
    float c = 1.f;
    for (int i = 1; i < 9; ++i) if (L <= LX[i]) { c = CB[b][i - 1] + (L - LX[i - 1]) / (LX[i] - LX[i - 1]) * (CB[b][i] - CB[b][i - 1]); break; }
    return 1.f - HB[b] * c;
}
static int pickFarm(const World& w, int exceptGroup, int& stepsOut) {
    g_opkSeen = 0; g_opkTrunc = false;   /* OPK */
    const bool dynamic = false;
    const float pfxDiv = gvtPick(), pfxMgm = mgmK(w, pfxDiv), pfxVcr = (policy::VCR_ON && w.me.type == policy::VCR_TYPE) ? policy::VCR_GOLD : 1.f;   /* PFX4: 고르기마다 한 번 */   /* NAVSTAT: 그룹 거리장 우선, 닿지 못할 때만 동적 (approachSteps 안) */
    if (dynamic) ++io::nav.dynamicPicks;
    bool deferred = false;
    int best = -1; int bestSteps = 9999; float pnlBest = -1.f;   /* PNL */
    const CampClaimRoster tmcRoster = policy::TM_CAMP_ON ? campClaimRoster(w, false) : CampClaimRoster{}; int tmcPreferred = -1;   /* TM_CAMP */
    if (policy::PSW_ON && w.round <= policy::PSW_R) {   /* PSW: 주 무리 목표로 돌아가기 — OPN: 개막에만 */
        static std::vector<int> cnt; cnt.assign(S().groups.size(), 0);   /* GRP: 그룹 수만큼 */
        for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k]; if (mt.claimGroup >= 0 && mt.claimGroup < (int)cnt.size() && mt.claimKind == team::CL_FARM) ++cnt[(size_t)mt.claimGroup]; }
        int pg = -1; for (int g = 0; g < (int)S().groups.size(); ++g) {   /* 팀원 담당 (농사) 최다 그룹 */
            if (cnt[g] <= 0) continue; const Group& Gc = S().groups[(size_t)g];
            if (policy::PSW_STRONG && Gc.strMax <= 0.f) continue;   /* PSW3: 힘 없는 덩어리 그룹은 무리 목표 아님 (모여도 안전 이득 없음 · 걷기만 늘어남) */
            if (pg < 0 || cnt[g] > cnt[pg]) pg = g; }
        if (pg >= 0 && pg != exceptGroup) {
            const Group& PG = S().groups[(size_t)pg];
            int pswNeed = 1; { const float fDp = pnlDpsFactor(w, PG), bud = std::max(1.f, w.me.hp - policy::RECOVER_HP * w.me.maxHp);   /* PSWN: osyHold 와 같은 필요 인원 */
              for (; pswNeed <= team::T.n; ++pswNeed) if (pnlLoss(PG, pswNeed, mgTeamDps(w, pswNeed) * fDp) <= bud) break; }
            const bool avoided = (size_t)pg < g_avoidUntil.size() && g_avoidUntil[(size_t)pg] > w.round;
            if (cnt[pg] + 1 <= pswNeed && !avoided && PG.hpTotal >= policy::PSW_MIN_HP && (PG.state == 0 || PG.state == 1) && !groupUnreachable(pg) && !(policy::PSW_STRONG && PG.strMax <= 0.f)) {
                const int d = approachSteps(w, pg, dynamic);
                if (d == PICK_DEFERRED) return PICK_DEFERRED;
                if (d < 9999 && d <= policy::PSW_MAX) {
                    const float hArr = groupHpLeft(pg) - mgTeamDps(w, cnt[pg]) * (float)d;
                    if (hArr > policy::PSW_LEFT * PG.hpTotal) { if (policy::TM_CAMP_ON) tmcPreferred = pg;   /* TM_CAMP: 바로 돌려주지 않고 아래 보정 점수로 비교 (동점이면 이 무리) */
                    else { stepsOut = d; if (log::ON) NB_LINE("#psw %d %d g=%d d=%d n=%d hArr=%.0f", w.round, S().myIndex, pg, d, cnt[pg], hArr); return pg; } }
                }
            }
        }
    }
    static std::vector<int> s_po;   // PO: 가까운 그룹부터 (연료로 끊겨도 먼 그룹만 빠진다)
    static std::vector<int> s_poKey;
    {   /* FPK: 계수 정렬 (거리 0..W+H 정수, 번호 순으로 넣고 꺼냄 → (거리, 번호) 순서 = 원래와 같음). 내 칸이 그대로면 건너뜀 */
      const size_t NG = S().groups.size(); static Pos s_poAt{-9999, -9999}; static int s_poVer = -1;
      if (s_po.size() != NG || !(s_poAt == w.me.pos) || s_poVer != (int)NG) {
        s_po.resize(NG); s_poKey.resize(NG); int kmax = 0;
        for (size_t k = 0; k < NG; ++k) { const int v = cheb(S().groups[k].center, w.me.pos); s_poKey[k] = v; if (v > kmax) kmax = v; }
        static std::vector<int> cnt; cnt.assign((size_t)kmax + 2, 0);
        for (size_t k = 0; k < NG; ++k) ++cnt[(size_t)s_poKey[k] + 1];
        for (size_t v = 1; v < cnt.size(); ++v) cnt[v] += cnt[v - 1];
        for (size_t k = 0; k < NG; ++k) s_po[(size_t)cnt[(size_t)s_poKey[k]]++] = (int)k;
        s_poAt = w.me.pos; s_poVer = (int)NG;
      }
    }
    std::vector<int> spV((policy::FS2_MIS4_ON && !policy::SP_ON) ? (size_t)0 : S().groups.size(), 0);   /* FS2_MIS4: SP 꺼짐이면 빈 벡터 (SP 블록 안에서만 읽힘) */   // SP2: 낮은 칸 자유 아군이 고를 그룹 (흉내)
    if (policy::SP_ON && policy::GF_ON && S().W * S().H >= policy::SP_AREA * std::max(1, team::T.n)) {   // APC: 흉내는 SP2 가 쓰이는 넓은 맵에서만 (islands 0.75만~3.1만 절약)
        {   // 그룹 거리장이 없으면 칸 거리로 (개막 r1~ 은 아직 없다)
            const std::vector<std::uint32_t>& hs = sortedAllyH();   /* FUELH */
            for (const auto& a : w.allies) {
                int k = -1; for (size_t q = 0; q < hs.size(); ++q) if (hs[q] == a.handle) k = (int)q;
                if (k < 0 || k >= team::T.slot || k >= team::T.n) continue;
                const auto& mt = team::T.mate[k]; bool freeA = mt.claimGroup < 0;
                if (!freeA && (size_t)mt.claimGroup < S().groups.size()) { const int st2 = S().groups[(size_t)mt.claimGroup].state; freeA = st2 == 2 || st2 == 3; }
                if (!freeA) continue;
                int bg = -1, be = 1 << 30;
                {   /* FPK: 내 가까운 순 + 하한 생략 · (값, 번호) 사전식 최소 = 원래 결과 */
                const int dMa = cheb(w.me.pos, a.pos), rMax = fpkRad(-1);
                for (int gk = 0; gk < (int)s_po.size(); ++gk) { const int gi = s_po[(size_t)gk]; if (s_poKey[(size_t)gi] - dMa - rMax > be) break;   /* 내 거리 순: 남은 그룹의 하한이 모두 최선보다 큼 */
                    const Group& G = S().groups[(size_t)gi]; if (G.state == 2 || G.mono || (G.gems <= 0 && G.gold <= 0)) continue;   /* SP3 */
                    const int add = spV[(size_t)gi] * policy::SP_STEPS + (G.gems <= 0 ? policy::DD_GOLD_STEPS : 0);
                    const int lb = std::max(0, cheb(G.center, a.pos) - fpkRad(gi)) + add; if (lb > be || (lb == be && gi > bg)) continue;
                    const int sd = gfReady(gi) ? gfAt(s_gf[(size_t)gi], a.pos) : cheb(G.center, a.pos); if (sd >= 65535) continue; const int wl = G.state == 3 ? std::max(0, G.respawnUntil - w.round) : 0; const int e = std::max(sd, wl) + add; if (e < be || (e == be && gi < bg)) { be = e; bg = gi; } }
                }
                if (bg >= 0) ++spV[(size_t)bg];
            }
        }
    }
    float fpkVmax = 0.f;
    { static float s_vmax = -1.f; static bool s_hasMono = false;   /* FPK: 그룹 점수 상한 (한 번) */
        if (s_vmax < 0.f) { s_vmax = 0.f; for (const auto& G : S().groups) { s_vmax = std::max(s_vmax, (float)G.gems * mgmK(w, gvtPick()) + (float)G.gold * std::max(1.f, policy::VCR_GOLD) / gvtPick());   /* MGM */   /* GVX */ if (G.mono) s_hasMono = true; } }
        fpkVmax = s_vmax * (policy::STK_ON ? 1.f + policy::STK_P : 1.f) + invMax(w) + (policy::UNL_ON && s_hasMono ? (float)team::teamGems(w, w.me.gems) : 0.f); }   /* STK · INV: 상한 */
    if (policy::TM_CAMP_ON) fpkVmax *= camp_commitment::maxFactor();   /* TM_CAMP: 상한 x 최대 계수 */
    const float pgbTeam = policy::UNL_ON ? (float)team::teamGems(w, w.me.gems) : 0.f;   /* PGB */
    const float pgbVcr = (policy::VCR_ON && w.me.type == policy::VCR_TYPE) ? policy::VCR_GOLD : 1.f;
    bool pnlrQuiet = true; for (const auto& e : w.enemies) if (cheb(e.pos, w.me.pos) <= 10) { pnlrQuiet = false; break; }   /* PNLR */
    const float fs2Spd = policy::FS2_MIS4_ON ? wkdSpeed(w) : 1.f; float fs2Rho = 0.f; bool fs2RhoOk = false;   /* FS2_MIS4: 고르기 안에서 안 바뀌는 값 */
    struct FsEsa { int cg; Pos c; float pre; }; FsEsa fsEsa[8]; int fsEsaN = -1;   /* FS_MIS2: ESA2 팀원 부분 (team::MAX = 6) */
    long long fgLoopPrev = 0;   /* FUELG */
    for (int gk = 0; gk < (int)S().groups.size(); ++gk) {
        const int gi = s_po[(size_t)gk];
        bool fsPgb = false; float fsPgbS = 1.f, fsPgbI = 0.f;   /* FS_MIS2: PGB 에서 센 stkMul · invStake */
        if (pnlBest > 0.f && fpkVmax / (float)std::max(1, s_poKey[(size_t)gi] - fpkRad(-1) + rules::CAPTURE_ROUNDS) < pnlBest * 0.999f) break;   /* FPK: 남은 그룹 점수 상한 < 최고 */
        if (gi == exceptGroup) continue;
        if (pnlBest > 0.f) {   /* PGB: 이 무리 점수 상한 < 1등 → 계산 없이 건너뜀 */
            const Group& Gb = S().groups[(size_t)gi];
            float Vb = (float)Gb.gems * pfxMgm + (float)Gb.gold * pgbVcr / (policy::PGBF_ON ? pfxDiv : 5.f); if (policy::FS_MIS2_ON) { const float fsS = stkMul(gi), fsI = invStake(w, gi); fsPgb = true; fsPgbS = fsS; fsPgbI = fsI; Vb = Vb * fsS + fsI; } else Vb = Vb * stkMul(gi) + invStake(w, gi);   /* FS_MIS2 */   /* STK · INV: 상한도 같은 식 */   /* PFX4 */   /* MGM */ if (Gb.mono) Vb += pgbTeam;   /* PGBF: V 와 같은 금 값 */
            const int wlb = Gb.state == 3 ? std::max(0, Gb.respawnUntil - w.round) : 0, dlb = std::max(0, s_poKey[(size_t)gi] - fpkRad(gi));
            if (policy::TM_CAMP_ON) Vb *= campTerritory(gi) + policy::CAMP_JOIN_BONUS_MAX;   /* TM_CAMP: 상한도 땅 계수 + 최대 덤 */
            if (Vb / (float)(std::max(dlb, wlb) + rules::CAPTURE_ROUNDS) < pnlBest * 0.999f) continue; }
        { static long long s_gcMax = 0; const long long fNow = io::fuel(); const bool fgHad = fgLoopPrev > 0; if (fgHad) s_gcMax = std::max(s_gcMax * 15 / 16, fgLoopPrev - fNow); fgLoopPrev = fNow;   /* FUELG2 */   /* FUELG: 무리 하나 평가 비용 (잰 최대) */
          if (fgHad && fNow < policy::ZO_LOOP_STOP + s_gcMax) { deferred = true; break; }   /* FUELG2: 첫 평가는 늘 */
          if (fNow < policy::ZO_LOOP_STOP) { deferred = true; break; } }   // ZO   /* PFX2: 같은 읽기 */
        ++g_opkSeen; int waitLeft = 0, others = 0; int fm = 1;   /* OPK */
        if (policy::PFX_ON) { const Group& Gq = S().groups[(size_t)gi];   /* PFX: farmable 의 싼 문만 먼저 — worthWith 는 1등 후보만 */
            if ((Gq.gems <= 0 && Gq.gold <= 0) || ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) || cnBlocked(gi) || Gq.state == 2) fm = 0;
            if (Gq.state == 3) waitLeft = std::max(0, Gq.respawnUntil - w.round); }
        else fm = farmable(w, gi, waitLeft, others);
        if (log::ON && w.round <= 6) { const Group& G = S().groups[(size_t)gi]; NB_LINE("#nbp %d %d g=%d fm=%d state=%d hp=%.0f str=%.0f gems=%d claims=%d threat=%.0f cheb=%d", w.round, S().myIndex, gi, fm, G.state, G.hpTotal, G.strMax, G.gems, others, goalThreat(gi), cheb(G.center, w.me.pos)); }
        if (fm == PICK_DEFERRED) { deferred = true; break; }
        if (!fm) continue;
        const Group& G = S().groups[(size_t)gi];
        // 걸음 = 캠프 스폰 칸 둘레 중 나와 가장 가까운 (cheb) 걸을 수 있는 칸 둘의 거리장 값 (거리장 읽기는 한 번에 ~108 연료라 그룹마다 2번만)
        const int d = approachSteps(w, gi, dynamic);
        if (d == PICK_DEFERRED) { deferred = true; break; }
        if (log::ON && w.round <= 6) NB_LINE("#nbp %d %d g=%d steps=%d", w.round, S().myIndex, gi, d);
        if (d >= 9999) continue;
        {   /* PNL: 라운드당 이득 점수 하나 */
            float V = (float)G.gems * pfxMgm + (float)G.gold * pfxVcr / pfxDiv; V = (policy::FS_MIS2_ON && fsPgb) ? V * fsPgbS + fsPgbI : V * stkMul(gi) + invStake(w, gi);   /* FS_MIS2 */   /* STK · INV */   /* PFX4 */   /* MGM */   /* GVX */ if (V <= 0.f) continue;   /* VCR */
            if (policy::UNL_ON && G.mono && unlOpener(gi)) V += (float)team::teamGems(w, w.me.gems);   /* UNL: 여는 값 */
            int nNear = 0; bool sqeQuiet = true; if (policy::FS2_MIS4_ON) sqeQuiet = pnlrQuiet; else for (const auto& e : w.enemies) if (cheb(e.pos, w.me.pos) <= 10) { sqeQuiet = false; break; }   /* FS2_MIS4: pnlrQuiet 와 같은 식 */   /* SQE: 적이 안 보일 때 늘 */
            if (!policy::NOP_ON && (w.round <= 40 || sqeQuiet)) {   /* OPS: 개막만 */   /* NOP: 끔 */
                for (const auto& al : w.allies) { if (!al.visible || cheb(al.pos, w.me.pos) > 4) continue;
                    ++nNear; } }
            int othersEta = 0, othersOn = 0; int rngOn[8]; int rngOnN = 0; float terrMate = 1e9f;   /* TERR T3: 담당 팀원 중 가장 이른 도착 */   /* RNG */   /* MTO */   /* MTA: 내 도착 + 여유 안에 오는 담당 팀원만 */
            float tmcJoined = 0.f;   /* TM_CAMP: 제때 오는 담당 팀원 (신뢰 x 제때) */
            float awjHead = 0.f; const bool awjWeak = policy::AWJ_ON && G.strMax <= 0.f;   /* AWJ: 약한 무리에서 나보다 먼저 치기 시작하는 담당 팀원의 피해 (ksDps x 앞선 R) */
            const bool rsh = policy::RSH_ON && G.state == 3;   /* RSH: 리스폰 그룹은 다시 나올 전체로 */   /* BH3_PK6: mtaSlack 앞으로 옮김 */
            const bool pk6 = policy::BH3_PK6_ON && rsh;   /* BH3_PK6: mtaSlack · fD 도 hNow · Dm0 와 같은 전체 기준 */
            if (policy::FS_MIS2_ON && policy::PFX2_ON) gsFill(gi);   /* FS_MIS2: 캠프 훑기를 mtaSlack 앞으로 (groupHpLeft · 광역 마스크가 같은 값) */
            const float fsFD = policy::FS_MIS2_ON ? pnlDpsFactor(w, G, pk6) : 0.f;   /* FS_MIS2: fD 한 번 */
            const int mtaSlack = std::max(policy::MTA_SLACK, (int)(std::max(1.f, pk6 ? G.hpTotal : groupHpLeft(gi)) / std::max(1.f, mgTeamDps(w, 1) * (policy::FS_MIS2_ON ? fsFD : pnlDpsFactor(w, G, pk6)))));   /* MTA3: 무리가 죽기 전 도착 */
            { const std::vector<std::uint32_t>& hsM = sortedAllyH();   /* FUELH */
              for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
                if (mt.claimGroup != gi || !(mt.claimKind == team::CL_FARM || mt.claimKind == team::CL_CAPTURE)) continue;
                Pos mp{-1, -1}; const std::uint32_t hk = (size_t)k < hsM.size() ? hsM[(size_t)k] : 0u;
                for (const auto& al : w.allies) if (al.visible && al.alive && al.handle == hk) { mp = al.pos; break; }
                if (mp.x < 0 && mt.zone >= 0) mp = team::zoneCenter(mt.zone);
                int eta = -1; if (mp.x >= 0) { eta = gfReady(gi) ? gfAt(s_gf[(size_t)gi], mp) : cheb(mp, G.center); if (eta >= 65535) eta = -1; } if (policy::TERR_ON && eta >= 0) terrMate = std::min(terrMate, (float)eta);   /* TERR T3 */
                if (awjWeak && eta >= 0) { const int awjM = std::max(eta, waitLeft), awjMe = std::max(d, waitLeft);   /* AWJ: 약한 무리는 OSY 기다림이 없어 먼저 닿은 몸이 바로 침 */
                    if (awjMe > awjM) { float kd = mgTeamDps(w, 1); for (const auto& al : w.allies) if (al.visible && al.alive && al.handle == hk) { kd = ksDps(al.type, al.str, al.focus); break; } awjHead += kd * (float)(awjMe - awjM); } }
                if (policy::TM_CAMP_ON && gfReady(gi)) tmcJoined += campClaimCredit(tmcRoster.mate[k], gi, eta, std::max(d, waitLeft));   /* TM_CAMP */
                if (eta < 0 || eta <= std::max(d, waitLeft) + mtaSlack) { ++othersEta; if (eta >= 0 && eta <= std::max(d, waitLeft) + policy::OSY_WAIT) { ++othersOn; if (rngOnN < 8) rngOn[rngOnN++] = rngRangeOf(w, hk); } } } }   /* RNG */
            const int m = std::max(std::max(othersEta, nNear), 0);   /* TFX-k: K-1 셈 끔 */   /* TFP2: 멤버는 다른 멤버 K−1 이 온다고 셈 */   /* MTA */ /* CB: 함께 치는 (같이 가는) 팀원 수 */ /* MCL: 담당 팀원 · 4칸 안 아군 (보이기만 한 아군 제외) */
            const int mD = std::max(othersOn, nNear);   /* MTO: 시작부터 같이 맞는 몸 = OSY 가 기다려 줄 몸 */
            if (policy::PFX2_ON && !policy::FS_MIS2_ON) gsFill(gi);   /* PFX2: 이 무리 캠프 한 번 훑기 */
            const float fD = policy::FS_MIS2_ON ? fsFD : pnlDpsFactor(w, G, pk6);   /* BH3_PK6 */
            const float myDps = mgTeamDps(w, 1) * fD, allDps = mgTeamDps(w, 1 + m) * fD;
            const float hNow = std::max(1.f, rsh ? G.hpTotal : groupHpLeft(gi));
            float hitDps = 0.f;   /* CB: 이 그룹을 담당하고 곁 (사거리 + 1) 에서 치는 팀원 (그룹 곁에 적 없음) 의 화력 — 곁에 있기만 한 아군은 안 셈 */
            if (G.state == 1) { bool enG = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, G.center) <= G.rangeMax + 4) { enG = true; break; }
                if (!enG) { const std::vector<std::uint32_t>& hsC = sortedAllyH();   /* FUELH */
                    for (const auto& al : w.allies) { if (!al.visible || cheb(al.pos, G.center) > G.rangeMax + 1) continue; int k = -1; for (size_t q = 0; q < hsC.size(); ++q) if (hsC[q] == al.handle) k = (int)q;
                        if (k >= 0 && k < team::T.n && team::T.mate[k].claimGroup == gi && team::T.mate[k].claimKind == team::CL_FARM) hitDps += ksDps(al.type, al.str, al.focus) * fD; } } }
            float hArr = hNow - (awjWeak ? std::max(hitDps * (float)d, awjHead * fD) : hitDps * (float)d);   /* CB: 걸어가는 동안 곁에서 치는 팀원이 깎음 */   /* AWJ: 걸어오는 담당 팀원의 앞선 피해까지 */
            if (hArr <= 0.f) continue;   /* CB: 도착 전에 끝남 */
            const float T = hArr / allDps;
            const float share = m >= 1 ? std::max(std::min(1.f, myDps * T / hNow), (hArr / hNow) / (float)(1 + m)) : std::min(1.f, myDps * T / hNow);   /* TSH2: 바닥 = 남은 일의 균등 몫 */   /* CB: 내가 넣을 피해 몫 */   /* TSH: 팀원이 함께 치는 무리는 팀 값으로 (몫 바닥) */
            const float Dm0 = pnlLoss(G, 1 + mD, mgTeamDps(w, 1 + mD) * fD, rsh);   /* MTO */
            float rngSh = -1.f;   /* RNG: 어그로 = 가장 가까운 몸 -> 사거리 짧은 팀원이 있으면 나는 안 맞음 */
            if (othersOn >= 1 && othersOn == mD) { int rmin = w.me.range, cnt = 1; for (int i = 0; i < rngOnN; ++i) { const int x = rngOn[i] < 0 ? w.me.range : rngOn[i]; if (x < rmin) { rmin = x; cnt = 1; } else if (x == rmin) ++cnt; }
                rngSh = w.me.range > rmin ? 0.f : 1.f / (float)cnt; }
            const float Dm = rngSh < 0.f ? Dm0 : Dm0 * rngSh / std::max(1.f / (float)(1 + mD), policy::PNLD_TOP);   /* RNG */
            const float hldR = false ? hldReserve(G) : policy::RECOVER_HP * w.me.maxHp;   /* HLD */
            const float budget = std::max(1.f, w.me.hp - hldR);
            const bool cbSoloBan = mD == 0 && G.strMax > 0.f && Dm > budget;   /* MTO */   /* CB3: 혼자 시작 금지는 점수 x CB_SOFT */
            const int hs = pfxHomeC(gi); const float home = (hs >= 0 ? (float)hs : (float)cheb(G.center, S().allyCenter)) / (policy::FS2_MIS4_ON ? fs2Spd : wkdSpeed(w));   /* FS2_MIS4 */   /* PFX2 */   /* WKD2: 집 ↔ 무리 걸음도 이동 속도로 */
            float H = Dm > budget ? std::ceil((Dm - budget) / std::max(1.f, false ? w.me.maxHp - hldR : w.me.maxHp * (1.f - policy::RECOVER_HP))) * ((float)(pnlrQuiet ? (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1 : rules::RESPAWN_TICKS) + home) : 0.f;   /* PNLR */   /* PNLD: 넘는 손실 = 죽음 (부활 + 걸음) */
            float rtnT = T;   /* RTN2: 탱커 교대 */
            const int mR = std::max(othersEta, w.round <= 40 ? nNear : 0);   /* MTA */ /* RTN2-1: 담당 팀원 + 개막 곁 아군 */
            if (H > 0.f && mR >= 1 && T > 0.f) {
                struct Bd { float b; bool me; }; Bd bud[8]; int nb = 0; bud[nb++] = {budget, true};
                for (const auto& al : w.allies) { if (nb >= 1 + mR || nb >= 8) break; if (!al.visible || !al.alive) continue; if (cheb(al.pos, w.me.pos) > 4 && cheb(al.pos, G.center) > G.rangeMax + 4) continue; bud[nb++] = {std::max(1.f, al.hp - (false ? hldR : policy::RECOVER_HP * al.maxHp)), false}; }
                while (nb < 1 + mR && nb < 8) bud[nb++] = {budget, false};
                std::sort(bud, bud + nb, [](const Bd& x, const Bd& y) { return x.b > y.b; });
                const float inRate = (Dm / std::max(1.f / (float)(1 + m), policy::PNLD_TOP)) / T;   // 무리가 주는 피해 / R (전체)
                const float tankRate = inRate * policy::PNLD_TOP, sideRate = inRate * (1.f - policy::PNLD_TOP);
                float hpLeft = hArr, tt = 0.f; bool done = false, meLeft = false;
                for (int i = 0; i < nb && tankRate > 0.f; ++i) {
                    const int k = nb - i; const float dpsK = mgTeamDps(w, k) * fD;
                    const float stint = std::max(0.f, bud[i].b - 2.f * tankRate) / tankRate;   /* RTN2-3: 나갈 2턴 */
                    const float need = hpLeft / std::max(1.f, dpsK);
                    const float s = std::min(stint, need);
                    for (int j = i + 1; j < nb; ++j) bud[j].b -= sideRate * s / (float)std::max(1, k - 1);   /* RTN2-2: 곁 몸 25% */
                    hpLeft -= dpsK * s; tt += s;
                    if (hpLeft <= 0.5f) { done = true; break; }
                    if (bud[i].me) meLeft = true; }
                if (done) { H = 0.f; rtnT = tt + (meLeft ? 2.f * home : 0.f); }   /* RTN2-4: 내가 빠지면 회복 왕복 */
            }
            const float a = std::max((float)d / (policy::FS2_MIS4_ON ? fs2Spd : wkdSpeed(w)), (float)waitLeft);   /* FS2_MIS4 */   /* WKD: 대시 챔피언은 걸음보다 빨리 닿음 */
            const float idleR = (rsh && policy::RSH3_ON && !policy::RT2_ON) ? (float)std::max(0, waitLeft - d) : 0.f;   /* RT2 */   /* RSH3: 도착 뒤 리스폰까지 서 있는 R */
            float esa2 = 0.f;   /* ESA2 */
            if (policy::ESA2_ON && policy::FS_MIS2_ON) {   /* FS_MIS2: 팀원별 부분은 gi 와 무관 → 고르기마다 한 번 */
                if (fsEsaN < 0) { fsEsaN = 0; auto deep = [&](Pos c) { const float du = std::hypot((float)(c.x - S().allyCenter.x), (float)(c.y - S().allyCenter.y)), de = std::hypot((float)(c.x - S().enemyCenter.x), (float)(c.y - S().enemyCenter.y)); return du / std::max(1.f, du + de) >= 0.55f; };
                    for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
                        if (mt.claimGroup < 0 || (size_t)mt.claimGroup >= S().groups.size() || w.round - mt.claimRound > team::LIFE_CLAIM || mt.claimKind != team::CL_FARM) continue;
                        const Group& Gm = S().groups[(size_t)mt.claimGroup]; if (!deep(Gm.center)) continue;
                        bool other = false; for (int q = 0; q < team::T.n; ++q) { if (q == k || q == team::T.slot) continue; const team::Mate& m2 = team::T.mate[q]; if (m2.claimGroup >= 0 && (size_t)m2.claimGroup < S().groups.size() && w.round - m2.claimRound <= team::LIFE_CLAIM && cheb(S().groups[(size_t)m2.claimGroup].center, Gm.center) <= 6) { other = true; break; } }
                        if (other) continue;
                        const float mg = (w.round - mt.gemsRound <= team::LIFE_CLAIM) ? (float)mt.gemsRep : 0.f;
                        if (fsEsaN < 8) { fsEsa[fsEsaN].cg = mt.claimGroup; fsEsa[fsEsaN].c = Gm.center; fsEsa[fsEsaN].pre = policy::ESA_DRISK * (mg + policy::DEATH_TIME_GEMS + (float)S().killGold / goldDiv() + 10.f); ++fsEsaN; } } }
                for (int i = 0; i < fsEsaN; ++i) { if (fsEsa[i].cg == gi || cheb(fsEsa[i].c, G.center) > 6) continue; esa2 = std::max(esa2, fsEsa[i].pre * (rtnT + (float)rules::CAPTURE_ROUNDS)); } }
            else if (policy::ESA2_ON) { auto deep = [&](Pos c) { const float du = std::hypot((float)(c.x - S().allyCenter.x), (float)(c.y - S().allyCenter.y)), de = std::hypot((float)(c.x - S().enemyCenter.x), (float)(c.y - S().enemyCenter.y)); return du / std::max(1.f, du + de) >= 0.55f; };
                for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
                    if (mt.claimGroup < 0 || (size_t)mt.claimGroup >= S().groups.size() || mt.claimGroup == gi || w.round - mt.claimRound > team::LIFE_CLAIM || mt.claimKind != team::CL_FARM) continue;
                    const Group& Gm = S().groups[(size_t)mt.claimGroup]; if (cheb(Gm.center, G.center) > 6 || !deep(Gm.center)) continue;
                    bool other = false; for (int q = 0; q < team::T.n; ++q) { if (q == k || q == team::T.slot) continue; const team::Mate& m2 = team::T.mate[q]; if (m2.claimGroup >= 0 && (size_t)m2.claimGroup < S().groups.size() && w.round - m2.claimRound <= team::LIFE_CLAIM && cheb(S().groups[(size_t)m2.claimGroup].center, Gm.center) <= 6) { other = true; break; } }
                    if (other) continue;
                    const float mg = (w.round - mt.gemsRound <= team::LIFE_CLAIM) ? (float)mt.gemsRep : 0.f;
                    esa2 = std::max(esa2, policy::ESA_DRISK * (mg + policy::DEATH_TIME_GEMS + (float)S().killGold / goldDiv() + 10.f) * (rtnT + (float)rules::CAPTURE_ROUNDS)); } }
            if (policy::FS2_MIS4_ON && !fs2RhoOk) { fs2Rho = rshRho(); fs2RhoOk = true; }   /* FS2_MIS4: s_kb* · gvtPick 은 고르기 안에서 안 바뀜 */
            if (policy::TERR_ON) V *= terrPOwn(gi, std::min(a, std::max(terrMate, (float)waitLeft)) - (float)waitLeft);
            if (policy::KPR_ON) {   /* KPR: 빼앗길 위험 — 위치 p 의 지킨 비율 (래더 180판 측정) */
                static const float kP[5] = {0.15f, 0.35f, 0.45f, 0.55f, 0.70f}, kK[5] = {0.95f, 0.89f, 0.70f, 0.61f, 0.48f};
                const float du = (float)cheb(G.center, S().allyCenter), de = (float)cheb(G.center, S().enemyCenter), p = du / std::max(1.f, du + de);
                float keep = kK[0]; if (p >= kP[4]) keep = kK[4]; else for (int i = 0; i < 4; ++i) if (p >= kP[i] && p < kP[i + 1]) { keep = kK[i] + (kK[i + 1] - kK[i]) * (p - kP[i]) / (kP[i + 1] - kP[i]); break; }
                V *= keep; }   /* TERR T3: 팀 첫 도착 - 무리 열림 = 늦음 L, V x (1 - h x CDF(L)) */
            float rate = (V * share - (policy::FS2_MIS4_ON ? fs2Rho : rshRho()) * idleR + esa2) / (a + rtnT + (float)rules::CAPTURE_ROUNDS + H);   /* RTN2: 교대면 길어진 시간 */   /* OPS · CB · RSH3: 서 있는 동안 못 버는 값 */
            if (cbSoloBan && !pnlrQuiet) rate *= policy::CB_SOFT;   /* CB3 · PNLR2 */
            if (policy::RWX_ON && rsh && (float)waitLeft > (float)d / (policy::FS2_MIS4_ON ? fs2Spd : wkdSpeed(w)) + (float)policy::DD_RESPAWN_SLACK) rate *= 0.001f;   /* RWX7: 걸음보다 DD_RESPAWN_SLACK R 넘게 남았을 때만 막음 */   /* RWX: 도착 때 리스폰 중이면 후보 없을 때만 */
            if (G.state == 3 && !rsh) { const int slack = w.enemies.empty() ? policy::DD_RESPAWN_SLACK : 0; if (waitLeft > d + slack) rate *= 0.001f; }   /* RSH: 정직한 점수면 덮어쓰기 없음 */   // 도착 맞춤이 아닌 리스폰 그룹은 후보가 없을 때만
            if (policy::TM_CAMP_ON) { rate = camp_commitment::apply(rate, campTerritory(gi), tmcJoined); if (gi == tmcPreferred && rate == pnlBest) rate = std::nextafter(rate, 3.4e38f); }   /* TM_CAMP: 땅 계수 + 합류 덤 (양수만) · PSW 후보는 동점이면 이김 */
            if (rate > pnlBest) {
                if (policy::PFX_ON) { int wl2 = 0, ot2 = 0; const int fm2 = farmable(w, gi, wl2, ot2);   /* PFX: 1등 후보일 때만 비싼 문 */
                    if (fm2 == PICK_DEFERRED) { deferred = true; break; }
                    if (!fm2) continue; }
                pnlBest = rate; bestSteps = d; best = gi; }
        }
    }
    g_gsGi = -1;   /* PFX2 */
    stepsOut = bestSteps; g_opkTrunc = deferred;   /* OPK */
    return best < 0 && deferred ? PICK_DEFERRED : best;
}
/* HRF (I008 사용자 규칙 7): 체력 빠진 몸 — (a) 남은 체력으로 죽지 않고 혼자 끝낼 무리 (b) 원거리면 근거리 팀원이 치는 무리를 뒤에서 지원. 둘 다 없을 때만 회복 */
static int g_hrfSupG = -1;   // HRF: 원거리 지원으로 붙은 무리 (그 무리에선 어그로 넘김 칸 고르기 = CXE3 항)
static int s_campHurtR = -100;   /* HRF (검토 B): 위협 적 없이 체력이 깎인 라운드 = 캠프 피해 */
static float hrfSoloLoss(const World& w, int gi) {   // 혼자 끝낼 때 받을 피해 = 기존 pnlLoss (P11 한 판단 한 곳, 검토 D) + 곁 캠프 위협 × 끝낼 라운드 (검토 C, mgLoss 와 같은 식)
    const Group& G = S().groups[(size_t)gi];
    const float dps = std::max(1.f, ksDps(w.me.type, w.me.str, w.me.focus));
    float hs = 0.f; for (size_t k = 0; k < G.campType.size(); ++k) { if (!S().campAlive(G, k)) continue;
        const float lh = k < G.campLastHp.size() ? G.campLastHp[k] : -1.f; hs += lh > 0.f ? lh : S().campHp(G.campType[k]); }
    return pnlLoss(G, 1, dps) + goalThreat(gi) * std::ceil(hs / dps);
}
static bool hrfSupport(const World& w, int gi) {   // 원거리 지원: 사거리 짧은 팀원이 깬 무리 곁에서 치고 있음 → 캠프는 가장 가까운 몸만 침 (agent_facts 10-20) → 뒤에 서면 0. 어그로가 넘어와도 빠져나갈 몫 (HLD) 은 남김
    const Group& G = S().groups[(size_t)gi];
    if (w.round - s_campHurtR <= 1) return false;   /* 검토 B: 지난 라운드 캠프에 맞았으면 뒤가 아님 → 지원 안 함 */
    if (G.state != 1 || G.awakeUntil < w.round || w.me.hp <= hldReserve(G)) return false;
    for (const auto& a : w.allies) if (a.visible && a.alive && a.range < w.me.range && cheb(a.pos, G.center) <= G.rangeMax + 1) return true;
    return false;
}
static bool hrfOk(const World& w, int gi, bool& sup) {   // 체력 빠진 몸이 이 무리를 쳐도 되나
    sup = false; const Group& G = S().groups[(size_t)gi];
    if (G.mono || !(G.state == 0 || G.state == 1) || (G.gems <= 0 && G.gold <= 0)) return false;
    if ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) return false;
    if (cnBlocked(gi) || groupUnreachable(gi)) return false;
    if (hrfSoloLoss(w, gi) + hldReserve(G) < w.me.hp) return true;   // (a) 남은 체력으로 죽지 않고 끝냄 + 빠져나갈 몫 (검토 A)
    sup = hrfSupport(w, gi); return sup;            // (b)
}
static int hrfPick(const World& w) {   // 시야 (rules::VISION) 안 무리 중 값 / (걸음 + 1) 가장 큰 것 (CJN · SITEL 과 같은 점수)
    if (!policy::HRF_ON || VIS_NEAR(w) || w.me.atSpawn || w.me.hp >= w.me.maxHp) return -1;
    if (disturbed(w) || w.round - M.lastEnemyRound <= 10) return -1;   /* 검토 C: 회복 끝 (MINE4) · finCanFinish 와 같은 조건 */
    int best = -1; bool bestSup = false; float bv = 0.f; const float dv = goldDiv();
    for (int gi = 0; gi < (int)S().groups.size(); ++gi) { const Group& G = S().groups[(size_t)gi];
        const int c = cheb(G.center, w.me.pos); if (c > rules::VISION) continue;
        bool sup = false; if (!hrfOk(w, gi, sup)) continue;
        const int d = gfReady(gi) ? gfAt(s_gf[(size_t)gi], w.me.pos) : c; if (d >= 65535) continue;
        const float v = stkVal(gi, dv) / (float)(d + 1); if (v > bv) { bv = v; best = gi; bestSup = sup; } }
    g_hrfSupG = bestSup ? best : -1;
    return best;
}
static bool hrfKeep(const World& w) {   // 체력 빠진 채 지금 무리를 계속 쳐도 되나
    if (!policy::HRF_ON || M.kind != M_FARM || M.group < 0 || (size_t)M.group >= S().groups.size() || VIS_NEAR(w) || w.me.atSpawn) return false;
    if (disturbed(w) || w.round - M.lastEnemyRound <= 10) return false;   /* 검토 C */
    bool sup = false; const bool ok = hrfOk(w, M.group, sup);
    if (ok) g_hrfSupG = sup ? M.group : -1;
    return ok;
}
static bool g_opdUsed = false, g_opdDashDefer = false;   /* OPD · OPDW */
static void tryPickFarm(const World& w, int except) {
    { static bool s_opdTried = false; if (!s_opdTried) { s_opdTried = true; std::uint8_t m[48] = {}; io::draftRead(m, 48);   /* OPD: 드래프트 개막 계획 (무리 중심 좌표) */
        if (m[16] == 0xA7 && w.me.type >= 0 && w.me.type <= 14 && m[17 + 2 * w.me.type] > 0) { const Pos c{(int)m[17 + 2 * w.me.type] - 1, (int)m[18 + 2 * w.me.type] - 1}; int g = -1, bd = 3;
            for (int gi = 0; gi < (int)S().groups.size(); ++gi) { const int dd = cheb(S().groups[(size_t)gi].center, c); if (dd < bd) { bd = dd; g = gi; } }
            if (g >= 0 && (S().groups[(size_t)g].state == 0 || S().groups[(size_t)g].state == 1)) { setGoal(M_FARM, g, -1, w); g_lastPickRound = w.round; g_opdUsed = true; M.why = "opd"; if (io::S.dashNav) { io::S.dashNav = false; g_opdDashDefer = true; for (auto& v : s_gf) v.clear(); s_gfHome.clear(); } return; }   /* OPDW: 첫 무리 칠 때까지 다리 길 안 씀 */ } } }
    if (policy::JHF_ON && M.kind == M_NONE && combat::joinTargetNow(w)) { M.why = "join-hold"; return; }   /* JHF */
    if (w.round - g_lastPickRound < policy::PICK_RETRY_ROUNDS) { M.why = "no-farm"; return; }
    static long long s_pickCost = 40000;   // APC: 고르기 연료 (잰 값, 줄 때는 천천히)
    static int s_refuse = 0;   /* NAVFZL: 연속 거절 수 */
    const long long needF = s_refuse >= 2 ? zres() + 8000 /* PGF */ : std::max(policy::ZO_PICK_FUEL, std::min(50000LL, s_pickCost) + zres());
    if (!io::wallsReady() || fuelSpare(w) < needF) { ++s_refuse; M.why = "no-fuel"; return; }
    int steps = 0; const long long pkF0 = io::fuel(); const int pkR0 = io::round(); const int g = pickFarm(w, except, steps);
    { const long long c = pkF0 - io::fuel(); s_pickCost = io::round() != pkR0 ? 60000LL : std::max(c, g == PICK_DEFERRED ? s_pickCost : s_pickCost * 9 / 10); }
    { static int s_opkN = 0;   /* OPK: 개막에 너무 적게 보고 끊긴 답은 미룸 (연속 3번까지) */
      if (policy::OPK_ON && g >= 0 && g_opkTrunc && g_opkSeen < policy::OPK_MINSEEN && w.round <= policy::OPK_ROUNDS && s_opkN < 3) { ++s_opkN; ++s_refuse; M.why = "pick-thin"; return; }
      if (g != PICK_DEFERRED) s_opkN = 0; }
    if (g == PICK_DEFERRED) { ++s_refuse; ++io::nav.deferred; M.why = "pick-deferred"; return; }   /* RQD 되돌림 (래더 v175~177 40% vs v173~174 60%) */
    s_refuse = 0;
    g_lastPickRound = w.round;
    if (g >= 0) {
        setGoal(M_FARM, g, -1, w);
        if (log::ON) NB_LINE("#nbs %d %d pick g=%d steps=%d", w.round, S().myIndex, g, steps);
        return;
    }
    M.why = "no-farm";
    // 혼자 감당할 그룹이 없으면 남이 담당한 그룹에 조수로 붙는다 (약한 몸도 힘을 더하고 보상을 나눈다)
    float tmcBestA = -1.f; const CampClaimRoster tmcRosterA = policy::TM_CAMP_ON ? campClaimRoster(w, false) : CampClaimRoster{};   /* TM_CAMP */
    { int bestG = -1, bestC = 9999;
      for (int gi = 0; gi < (int)S().groups.size(); ++gi) {
          const Group& G = S().groups[(size_t)gi]; if (G.state == 2 || G.state == 3 || gi == except) continue;
          if ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) continue;
          const int others = team::claimsOn(gi, (1 << team::CL_FARM) | (1 << team::CL_CAPTURE)); if (others < 1 || others >= policy::capFor(G.strMax)) continue;
          const int c = cheb(G.center, w.me.pos);
          if (policy::TM_CAMP_ON) {   /* TM_CAMP: 가까운 무리 → 1/(1+거리) x (땅 계수 + 합류 덤) */
              if (c > policy::PICK_MAX_CHEB) continue;
              const int arrival = gfReady(gi) ? gfAt(s_gf[(size_t)gi], w.me.pos) : c;
              const float joined = campClaimedAllies(gi, arrival, tmcRosterA, team::T.slot);
              const float score = camp_commitment::apply(1.f / (float)(1 + c), campTerritory(gi), joined);
              if (score > tmcBestA) { tmcBestA = score; bestG = gi; }
          } else if (c < bestC && c <= policy::PICK_MAX_CHEB) { bestC = c; bestG = gi; }
      }
      if (bestG >= 0) { setGoal(M_FARM, bestG, -1, w); M.why = "assist"; return; } }
    // 할 농사가 없으면 스폰으로: 금이 있으면 사고, 체력이 모자라면 채운다 (들고 있으면 바치기는 5 에서 먼저 잡힘)
    if (combat::joinTargetNow(w)) return;   /* JNL: 곁 아군 싸움에 끼면 이득이면 상점 · 회복으로 떠나지 않음 */
    if (w.me.gold >= policy::SHOP_TRIP_GOLD_FAR && !w.me.atSpawn) setGoal(M_SHOP, -1, -1, w);   // RQ: 엠블럼 값일 때만
    else if (w.me.hp < w.me.maxHp * policy::RQ_RECOVER_HP && !w.me.atSpawn) { const int hg = policy::HRF_ON ? hrfPick(w) : -1;   /* HRF */
        if (hg >= 0) { setGoal(M_FARM, hg, -1, w); M.why = "hrf"; } else setGoal(M_RECOVER, -1, -1, w); }   // RQ: 체력이 낮을 때만
}

static int s_lcMeR = -1;   // LC: 이 몸의 평타 사거리
void noteBody(const World& w) {
    s_lcMeR = std::max(1, w.me.range);
}
void prewarmTiles(long long floorFuel) {
    if (io::fuel() < floorFuel + 3000) return;
    if (s_lcMeR < 0 || S().groups.empty() || io::fuel() < floorFuel) return;
    static int doneMe = -1;
    static size_t doneGroups = 0;
    if (doneMe == s_lcMeR && doneGroups == S().groups.size()) return;
    static std::vector<int> order; static Pos s_from{-999, -999};
    const Pos from = S().allyCenter;
    if (order.size() != S().groups.size() || s_from != from) {   // 우리 스폰에서 가까운 그룹부터 (한 번)
        order.resize(S().groups.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return cheb(S().groups[(size_t)a].center, from) < cheb(S().groups[(size_t)b].center, from); });
        s_from = from;
    }
    for (int gi : order) {
        const Group& G = S().groups[(size_t)gi];
        for (size_t k = 0; k < G.spawn.size(); ++k) {
            if (io::fuel() < floorFuel) return;  // 이미 캐시된 칸을 순회하는 비용도 제한
            const Pos sp = G.spawn[k];
            if (!lcHas(sp, s_lcMeR)) { if (io::fuel() < floorFuel || io::fuel() < lcEstimate(s_lcMeR) + policy::LC_BUILD_RES) return; (void)lcList(sp, s_lcMeR); }
        }
    }
    doneMe = s_lcMeR; doneGroups = S().groups.size();
}
static float s_kbContact = 0.f;   /* KTRC */
static float s_kbAlive = 0.f, s_kbDeaths = 0.f, s_kbEarned = 0.f; static int s_kbLastGems = 0; static bool s_kbWasAlive = true;
static float s_kbGold = 0.f; static int s_kbLastGold = -1;   /* RSH3: 번 금 (늘어난 만큼만) */
static float rshRho() { return (s_kbEarned + policy::KB_PRIOR_RATE * policy::KB_PRIOR_R + s_kbGold / gvtPick()) / (s_kbAlive + policy::KB_PRIOR_R); }   /* VMR2 */   /* RSH3: 젬 + 금/5 (V 와 같은 단위) / 산 R */   // KB: 몸마다 이 판 장부
void kbNoteDead() { if (s_kbWasAlive) s_kbDeaths += 1.f; s_kbWasAlive = false; s_kbLastGems = 0; }   // KB: 죽어 있는 턴 (update 가 안 불림)

// ---- TFP2: 팀 농사 이익 동기화 ----
int g_tfpTarget = -1; bool g_tfpMember = false; int g_tfpK = 0;
static int s_tfpPrev = -1, s_tfpPrevK = 0, s_tfpWaitUntil = -1, s_tfpPlanR = -1000;
static std::vector<int> s_tfpTaken;
static float s_tfpDps[team::MAX], s_tfpHp[team::MAX]; static bool s_tfpStatInit = false;   // 칸 번호별 마지막으로 본 화력 · 최대 체력
static void tfpObserve(const World& w) {
    const std::vector<std::uint32_t>& hs = sortedAllyH();   /* FUELH */
    if (!s_tfpStatInit) { s_tfpStatInit = true; const ChampDesign& D = S().designOf(w.me.type); for (int k = 0; k < team::MAX; ++k) { s_tfpDps[k] = D.valid ? ksDps(w.me.type, D.str, D.focus) : 50.f; s_tfpHp[k] = D.valid ? D.hp : 1000.f; } }
    if (team::T.slot >= 0 && team::T.slot < team::MAX) { s_tfpDps[team::T.slot] = ksDps(w.me.type, w.me.str, w.me.focus); s_tfpHp[team::T.slot] = w.me.maxHp; }
    for (const auto& a : w.allies) { if (!a.visible) continue; for (size_t q = 0; q < hs.size() && q < (size_t)team::MAX; ++q) if (hs[q] == a.handle) { s_tfpDps[q] = ksDps(a.type, a.str, a.focus); s_tfpHp[q] = a.maxHp; } }
}
static std::vector<std::uint16_t> s_tfpZS; static float s_tfpSolo[team::MAX]; static int s_tfpSoloZ[team::MAX], s_tfpSoloR[team::MAX];   // 구역 × 무리 걸음 캐시 · 따로 이득률 캐시
static int g_tpStart = 0, g_tpDone = 0, g_tpCntG = -1, g_tpCntR = -1000;   /* TFPP: 계획 시작 · 완료 수 */
static int g_zkMask = -1, g_zkR = -1000; static float g_zkT = 0.f; static bool g_zkMem = false;   /* TFPZ: 목표 유지 */
static std::vector<int> s_zVal; static int s_zValR = -1000, s_zInit = 0; static int s_zC[12]; static int s_zNC = 0, s_zCR = -1000;   /* TFPZ2 */
static void tfpPlan(const World& w) {
    if (policy::TM_CAMP_ON) { static int tmcEpoch = 0; if (tmcEpoch != s_territoryEpoch) { tmcEpoch = s_territoryEpoch; s_zCR = -1000; s_tfpPlanR = -1000; s_tfpPrev = -1; g_zkMask = -1; } }   /* TM_CAMP: 땅 장이 다 되면 TFP 캐시 지움 */
    if (w.round % 8 == 0 || !s_tfpStatInit) tfpObserve(w);   /* TFPZ2: 8R */
    const size_t NG = S().groups.size(); if (s_tfpTaken.size() != NG) s_tfpTaken.assign(NG, -1000);
    for (int k = 0; k < team::T.n; ++k) { const auto& mt = team::T.mate[k]; if (mt.claimKind == team::CL_CAPTURE && mt.claimGroup >= 0 && (size_t)mt.claimGroup < NG) s_tfpTaken[(size_t)mt.claimGroup] = std::max(s_tfpTaken[(size_t)mt.claimGroup], mt.claimRound); }
    if (M.kind == M_CAPTURE && M.group >= 0) s_tfpTaken[(size_t)M.group] = std::max(s_tfpTaken[(size_t)M.group], w.round);
    if (g_tpCntG >= 0 && (size_t)g_tpCntG < NG && s_tfpTaken[(size_t)g_tpCntG] >= g_tpCntR) { ++g_tpDone; g_tpCntG = -1; }   /* TFPP */
    if ((w.round % 8 != 0 && s_tfpPlanR > w.round - 8) || s_tfpPlanR == w.round || io::fuel() < 40000 || w.enemyNear8) return;   /* TFPQ2: 조용한 턴에만 */
    s_tfpPlanR = w.round;
    int slots[team::MAX], zn[team::MAX]; int n = 0;
    for (int k = 0; k < team::T.n && n < team::MAX; ++k) { const auto& mt = team::planView(k, w); /* TFPC3: 모든 몸 같은 팀원 보기 */ if (!mt.known || mt.zone < 0 || mt.zone >= 64 || mt.outReason > 0) continue; if (policy::BH3_TF4_ON && mt.claimKind == team::CL_FARM && mt.claimGroup >= 0 && (size_t)mt.claimGroup < NG && mt.claimGroup != s_tfpPrev && S().groups[(size_t)mt.claimGroup].state == 1) continue;   /* BH3_TF4: TZD 가 옮기지 않는 몸 (다른 살아 있는 무리 치는 중) 은 멤버 아님 */ slots[n] = k; zn[n] = mt.zone; ++n; }
    g_tfpTarget = -1; g_tfpMember = false; g_tfpK = 0;
    if (n < 2) { s_tfpPrev = -1; return; }
    const float div = gvtPick();
    auto unr = [&](size_t g) { return policy::UNR_ON && g < s_unr.size() && s_unr[g]; };
    int kMask = 0; for (int i = 0; i < n; ++i) kMask |= 1 << slots[i];
    auto tfpWait = [&](size_t g) { const Group& G = S().groups[g]; int c = std::max(0, s_tfpTaken[g] + G.respawn - w.round);   /* BH3_TF1: 적 점령 · 리스폰 중도 기다림 */
        if (policy::BH3_TF1_ON) { if (G.state == 3 && G.respawnUntil > w.round) c = std::max(c, G.respawnUntil - w.round); else if (G.state == 2) c = std::max(c, (int)rules::CAPTURE_ROUNDS + G.respawn); }
        return c; };
    if (s_tfpPrev >= 0 && (size_t)s_tfpPrev < NG && g_zkMask >= 0 && (kMask & g_zkMask) == g_zkMask && s_tfpTaken[(size_t)s_tfpPrev] < g_zkR && (float)(w.round - g_zkR) <= 2.f * g_zkT + 20.f) {   // 목표 유지: 사건이 없으면 계산 없음
        const Group& G0 = S().groups[(size_t)s_tfpPrev];
        if (gfReady(s_tfpPrev) && !unr((size_t)s_tfpPrev) && (float)G0.gems + (float)G0.gold / div > 0.f && (!policy::BH3_TF1_ON || (!(G0.state == 3 && G0.respawnUntil > w.round) && G0.state != 2))) {   /* BH3_TF1 */ g_tfpTarget = s_tfpPrev; g_tfpK = s_tfpPrevK; g_tfpMember = g_zkMem; return; } }
    if (s_tfpZS.size() != 64 * NG) { s_tfpZS.assign(64 * NG, 0xFFFF); for (int k = 0; k < team::MAX; ++k) { s_tfpSoloZ[k] = -1; s_tfpSoloR[k] = -1000; } }
    static std::vector<float> hsS, ssS, gH, gC; static std::vector<int> nc, gHome;
    if (gH.size() != NG) { hsS.assign(NG * 32, 0.f); ssS.assign(NG * 32, 0.f); nc.assign(NG, 0); gH.assign(NG, 0.f); gC.assign(NG, 0.f); gHome.assign(NG, -1); s_zInit = 0; }
    if (s_zInit < (int)NG) {   /* TFPZ2: 기본 표를 라운드마다 8 무리씩 (다 될 때까지 계획 없음) */
        for (int t = 0; t < 8 && s_zInit < (int)NG; ++t, ++s_zInit) { const size_t g = (size_t)s_zInit; const Group& G = S().groups[g]; int c = 0; float* hh = &hsS[g * 32]; float* sss = &ssS[g * 32];
            for (size_t k = 0; k < G.campType.size() && c < 32; ++k) { hh[c] = S().campHp(G.campType[k]); sss[c] = std::max(0.f, S().campStr(G.campType[k])); ++c; }
            for (int i = 1; i < c; ++i) for (int j = i; j > 0 && (policy::TFX_SMITH ? sss[j] * hh[j - 1] > sss[j - 1] * hh[j] : hh[j] < hh[j - 1]); --j) { std::swap(hh[j], hh[j - 1]); std::swap(sss[j], sss[j - 1]); }   /* TFX: 힘/체력 큰 순 */
            nc[g] = c; float sA = 0.f; for (int i = 0; i < c; ++i) sA += sss[i]; float hsum = 0.f, cc = 0.f; for (int i = 0; i < c; ++i) { hsum += hh[i]; cc += sA * hh[i]; sA -= sss[i]; } gH[g] = hsum; gC[g] = cc;
            const int hs = homeSteps(G.center); gHome[g] = hs >= 0 ? hs : -1; }
        s_tfpPlanR = -1000; return; }
    float dAvg = 0.f; for (int i = 0; i < n; ++i) dAvg += s_tfpDps[slots[i]]; dAvg = std::max(1.f, dAvg / (float)n);
    // 후보 12 (점수 큰 순): 점수 = V / (기다림 + 체력 / 평균 화력 + 점령)
    constexpr int CM = 12, TM = 6; int cg[CM]; float cv[CM], cs[CM]; int cw[CM]; int nC = 0;
    if (w.round - s_zValR >= 32) { s_zValR = w.round; s_zVal.clear(); for (size_t g = 0; g < NG; ++g) { const Group& G = S().groups[g]; if (G.gems <= 0 && G.gold <= 0) continue; if (!gfReady((int)g) || unr(g)) continue; s_zVal.push_back((int)g); } s_zCR = -1000; }   /* TFPZ2: 값 있는 무리 (32R) */
    if (w.round - s_zCR >= 16) { s_zCR = w.round; s_zNC = 0;   /* TFPZ2: 후보 12 (16R) */
        for (int g0 : s_zVal) { const size_t g = (size_t)g0; const Group& G = S().groups[g]; const float v = ((float)G.gems + (float)G.gold / div) * stkMul((int)g); if (v <= 0.f) continue;   /* STK */
            const int wait = tfpWait(g); float sc = v / ((float)wait + gH[g] / dAvg + (float)rules::CAPTURE_ROUNDS); if (policy::TM_CAMP_ON) sc *= campTerritory(g0) + policy::CAMP_JOIN_BONUS_MAX;   /* TM_CAMP: 후보 점수 상한 계수 */
            if (nC < CM) { cg[nC] = (int)g; cs[nC] = sc; ++nC; }
            else { int lo = 0; for (int j = 1; j < CM; ++j) if (cs[j] < cs[lo]) lo = j; if (sc > cs[lo]) { cg[lo] = (int)g; cs[lo] = sc; } } }
        for (int c = 0; c < nC; ++c) s_zC[c] = cg[c]; s_zNC = nC; nC = 0; }
    for (int c = 0; c < s_zNC; ++c) { const size_t g = (size_t)s_zC[c]; const Group& G = S().groups[g]; const float v = ((float)G.gems + (float)G.gold / div) * stkMul((int)g); if (v <= 0.f || !gfReady((int)g)) continue;   /* STK */
        cg[nC] = (int)g; cv[nC] = v; cw[nC] = tfpWait(g); cs[nC] = 0.f; ++nC; }
    if (nC == 0) { s_tfpPrev = -1; return; }
    int st[team::MAX][CM]; int fillLeft = 24; bool trunc = false;   /* TFPZ2: 걸음 채우기 최대 24 · BH_F3: 잘림 표시 */
    for (int i = 0; i < n; ++i) for (int c = 0; c < nC; ++c) { const size_t g = (size_t)cg[c]; std::uint16_t& x = s_tfpZS[(size_t)zn[i] * NG + g];
        if (x == 0xFFFF) { if (fillLeft <= 0) { st[i][c] = 99999; trunc = true; continue; } --fillLeft; const int v = gfAt(s_gf[g], team::zoneCenter(zn[i])); x = (std::uint16_t)(v >= 65535 ? 65534 : v); } st[i][c] = x >= 65534 ? 99999 : (int)x; }
    if (policy::BH_F3_ON && trunc) return;   /* BH_F3: 걸음 표가 덜 찼으면 이 턴은 계획 · solo 캐시 안 함 (s_tfpPlanR = 이번 라운드 → 8R 뒤 다음 24칸) */
    float tmcJn[team::MAX][CM], tmcUp[CM], tmcTer[CM], tmcJB[team::MAX][CM], tmcJF[team::MAX][CM];   /* TM_CAMP: 합류 수 · 계수 상한 · 땅 계수 · 덤 · 계수 */
    if (policy::TM_CAMP_ON) {
        const CampClaimRoster tmcR = campClaimRoster(w, true);
        campPlanJoinCounts(tmcR, slots, n, cg, cw, nC, st, tmcJn);
        for (int c = 0; c < nC; ++c) { tmcTer[c] = campTerritory(cg[c]); tmcUp[c] = tmcTer[c];
            for (int i = 0; i < n; ++i) { tmcJB[i][c] = camp_commitment::bonus(tmcJn[i][c]); tmcJF[i][c] = tmcTer[c] + tmcJB[i][c]; tmcUp[c] = std::max(tmcUp[c], tmcJF[i][c]); } }
    }
    auto tmcCandV = [&](int c) { return policy::TM_CAMP_ON ? cv[c] * tmcUp[c] : cv[c]; };   /* TM_CAMP: 팀 후보 고르기 값 */
    auto timeOf = [&](int c, float dSum, float bSum, int K, int gather) {
        if (gather >= 99999 || dSum <= 0.f) return 1e9f; const size_t g = (size_t)cg[c];
        const float kill = gH[g] / dSum, dm = gC[g] / dSum; const float over = std::max(0.f, dm - bSum); const int hs = gHome[g];
        const float rec = over > 0.f ? std::ceil(over / std::max(1.f, bSum / (float)K)) * ((float)(rules::RECALL_TICKS + 1) + (float)(hs >= 0 ? hs : 30)) / (float)K : 0.f;
        return (float)std::max(gather, cw[c]) + kill + (float)rules::CAPTURE_ROUNDS + rec; };
    float solo[team::MAX];
    for (int i = 0; i < n; ++i) { const int k = slots[i];
        if (policy::TM_CAMP_ON) {   /* TM_CAMP: 무리마다 원래 solo rate 를 32R · 구역 캐시로 두고 지금 계수를 그 뒤에 곱함 (팀원 목표가 바뀌면 바로 반영) */
            static std::vector<float> tmcRaw[team::MAX];
            auto& raw = tmcRaw[k];
            if (raw.size() != NG || s_tfpSoloZ[k] != zn[i] || w.round - s_tfpSoloR[k] >= 32) { raw.assign(NG, -1.f); s_tfpSoloZ[k] = zn[i]; s_tfpSoloR[k] = w.round; }
            solo[i] = 0.f; const float dI = s_tfpDps[k], bI = s_tfpHp[k] * (1.f - policy::RECOVER_HP);
            for (int c = 0; c < nC; ++c) { if (st[i][c] >= 99999) continue; float& rt = raw[(size_t)cg[c]];
                if (rt < 0.f) { const float T = timeOf(c, dI, bI, 1, st[i][c]); rt = T < 1e8f ? cv[c] / T : 0.f; }
                const float corr = rt > 0.f ? rt * tmcTer[c] + rt * tmcJB[i][c] : rt; solo[i] = std::max(solo[i], corr); }
            s_tfpSolo[k] = solo[i]; continue; }
        if (s_tfpSoloZ[k] == zn[i] && w.round - s_tfpSoloR[k] < 32) { solo[i] = s_tfpSolo[k]; continue; }
        solo[i] = 0.f; const float dI = s_tfpDps[k], bI = s_tfpHp[k] * (1.f - policy::RECOVER_HP);
        for (int c = 0; c < nC; ++c) { const float T = timeOf(c, dI, bI, 1, st[i][c]); if (T < 1e8f) solo[i] = std::max(solo[i], cv[c] / T); }
        s_tfpSolo[k] = solo[i]; s_tfpSoloZ[k] = zn[i]; s_tfpSoloR[k] = w.round; }
    // 팀 후보: 값 상위 TM
    int tc[TM]; int nT = 0;
    for (int c = 0; c < nC; ++c) { if (nT < TM) { tc[nT++] = c; continue; } int lo = 0; for (int j = 1; j < TM; ++j) if (tmcCandV(tc[j]) < tmcCandV(tc[lo])) lo = j; if (tmcCandV(c) > tmcCandV(tc[lo])) tc[lo] = c; }   /* TM_CAMP: 끄면 cv 그대로 */
    auto order = [&](int c, int* idx) { for (int i = 0; i < n; ++i) { int j = i; while (j > 0 && (st[idx[j - 1]][c] > st[i][c] || (st[idx[j - 1]][c] == st[i][c] && slots[idx[j - 1]] > slots[i]))) { idx[j] = idx[j - 1]; --j; } idx[j] = i; } };   // 끼워 넣기
    const float tpP = (3.f + (float)g_tpDone) / (4.f + (float)g_tpStart);   /* TFPP: 이 판의 계획 완료율 */
    float bestGain = 0.f, bestT = 0.f; int bestC = -1, bestK = 0; float prevGain = -1.f; int prevC = -1; float prevT = 0.f;   /* BH3_TF2 */
    auto dOrd = [&](int* ix) { for (int a1 = 1; a1 < n; ++a1) for (int b1 = a1; b1 > 0 && (s_tfpDps[slots[ix[b1]]] > s_tfpDps[slots[ix[b1 - 1]]] || (s_tfpDps[slots[ix[b1]]] == s_tfpDps[slots[ix[b1 - 1]]] && slots[ix[b1]] < slots[ix[b1 - 1]])); --b1) std::swap(ix[b1], ix[b1 - 1]); };   /* TFX dps: 화력 큰 순 (같으면 칸 번호) */
    int bestOm = 0, prevOm = 0;
    for (int q = 0; q < nT; ++q) for (int om = 0; om < (policy::TFX_DPS ? 2 : 1); ++om) { const int c = tc[q];
        int idx[team::MAX]; order(c, idx); if (om == 1) dOrd(idx);
        float sep = 0.f, dS = 0.f, bS = 0.f; int gmx = 0;
        for (int K = 1; K <= n; ++K) { const int i = idx[K - 1]; gmx = std::max(gmx, st[i][c]); sep += solo[i]; dS += s_tfpDps[slots[i]]; bS += s_tfpHp[slots[i]] * (1.f - policy::RECOVER_HP); if (K < 2) continue;
            const float T = timeOf(c, dS, bS, K, gmx);   /* TFX dps: 고른 몸 중 가장 먼 걸음 (걸음 순이면 st[i][c] 와 같음) */ if (T >= 1e8f) continue;
            float tmcTR = tpP * cv[c] / T; if (policy::TM_CAMP_ON) { float cs2 = 0.f; for (int k2 = 0; k2 < K; ++k2) cs2 += tmcJF[idx[k2]][c]; tmcTR *= cs2 / (float)K; }   /* TM_CAMP: 팀 rate x 멤버 평균 계수 */
            const float gain = tmcTR - sep;   /* TFPP */
            if (cg[c] == s_tfpPrev && K == s_tfpPrevK && (prevC < 0 || gain > prevGain)) { prevGain = gain; prevC = c; prevOm = om; prevT = T; }
            if (gain > bestGain) { bestGain = gain; bestC = c; bestK = K; bestT = T; bestOm = om; } } }
    if (prevGain > 0.f && prevC >= 0 && bestC >= 0 && cg[bestC] != s_tfpPrev && bestGain < prevGain * 1.1f) { bestC = prevC; bestK = s_tfpPrevK; bestGain = prevGain; bestOm = prevOm; if (policy::BH3_TF2_ON) bestT = prevT; }   // 목표 유지 (흔들림 여유 10%)
    if (bestC < 0) { s_tfpPrev = -1; return; }
    int bidx[team::MAX]; order(bestC, bidx); if (bestOm == 1) dOrd(bidx); int bgx = 0; for (int i = 0; i < bestK; ++i) bgx = std::max(bgx, st[bidx[i]][bestC]);   /* TFX dps */
    const int bestG = cg[bestC];
    if (bestG != s_tfpPrev && !(policy::TFX_TPC && bestG == g_tpCntG)) { ++g_tpStart; g_tpCntG = bestG; g_tpCntR = w.round; }   /* TFPP */   /* TFX: 대기 중 같은 무리 다시 고름 = 같은 시작 */
    if (bestG != s_tfpPrev || bestK != s_tfpPrevK) s_tfpWaitUntil = w.round + std::min(200, bgx) + 3;
    s_tfpPrev = bestG; s_tfpPrevK = bestK; g_tfpTarget = bestG; g_tfpK = bestK;
    for (int i = 0; i < bestK; ++i) if (slots[bidx[i]] == team::T.slot) g_tfpMember = true;
    { int mm = 0; for (int i = 0; i < bestK; ++i) mm |= 1 << slots[bidx[i]]; g_zkMask = mm; } g_zkR = w.round; if (bestT > 0.f) g_zkT = bestT; g_zkMem = g_tfpMember;   /* TFPM2 */
    if (log::ON) NB_LINE("#tfp %d %d g=%d K=%d mem=%d n=%d gain=%.3f", w.round, S().myIndex, bestG, bestK, (int)g_tfpMember, n, bestGain);
}
static int s_rtxG = -1;   /* RTX: 후퇴를 일으킨 무리 */
static bool osyHold(const World& w, int group) {   /* OSY: 도착 맞추기 (턴마다 한 번 계산) */
    static int s_r = -1, s_g = -2; static bool s_v = false; static int s_startG = -1, s_startR = -1000, s_startS = -1;
    if (s_r == w.round && s_g == group) return s_v; s_r = w.round; s_g = group; s_v = false;
    if (!policy::OSY_ON || group < 0 || (size_t)group >= S().groups.size() || M.kind != M_FARM || M.group != group || w.enemyNear8) return false;
    const Group& G = S().groups[(size_t)group]; if (G.strMax <= 0.f || G.awakeUntil >= w.round || !(G.state == 0 || G.state == 1)) return false;
    if (cheb(G.center, w.me.pos) > G.rangeMax + 3) return false;   // 무리 곁에서만 (걷는 동안은 상관없음)
    const float budget = std::max(1.f, w.me.hp - (false ? hldReserve(G) : policy::RECOVER_HP * w.me.maxHp)), fD = pnlDpsFactor(w, G);   /* HLD */
    int here = 0; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, G.center) <= G.rangeMax + 2) ++here;
    int need = 1; for (; need <= 5; ++need) if (pnlLoss(G, need, mgTeamDps(w, need) * fD) <= budget) break;
    if (1 + here >= need) return false;
    int coming = 0; const std::vector<std::uint32_t>& hs = sortedAllyH();
    for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
        if (mt.claimGroup != group || !(mt.claimKind == team::CL_FARM || mt.claimKind == team::CL_CAPTURE)) continue;
        const std::uint32_t hk = (size_t)k < hs.size() ? hs[(size_t)k] : 0u; Pos mp{-1, -1};
        for (const auto& al : w.allies) if (al.visible && al.alive && al.handle == hk) { mp = al.pos; break; }
        if (mp.x < 0 || cheb(mp, G.center) <= G.rangeMax + 2) continue;
        int eta = gfReady(group) ? gfAt(s_gf[(size_t)group], mp) : cheb(mp, G.center); if (eta >= 65535) continue;
        if (eta <= policy::OSY_WAIT) ++coming; }
    if (coming == 0) return false;
    if (s_startG != group || s_startS != M.since) { s_startG = group; s_startS = M.since; s_startR = w.round; }   /* OSYT: 임무마다 타이머 */
    if (w.round - s_startR > policy::OSY_WAIT + 4) return false;
    s_v = true; return true;
}
static bool tbaWait(const World& w, int group);   /* TBA2 */
bool tfpHold(const World& w, int group) {
    if (tbaWait(w, group)) return true;   /* TBA2: 계획 멤버 모이기 */
    if (osyHold(w, group)) return true;   /* OSY */
    if (!policy::TFP_ON || group != g_tfpTarget || g_tfpTarget < 0 || !g_tfpMember) return false;
    if (S().groups[(size_t)group].awakeUntil >= w.round || w.round > s_tfpWaitUntil) return false;
    int near = 0; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, w.me.pos) <= 5) ++near;
    return near < g_tfpK - 1;
}
static void snrmSet() { io::g_wantGroup = (M.kind == M_FARM || M.kind == M_CAPTURE) ? M.group : -1; }   /* SNRM */
static float trpP(const World& w) {   /* TRP: P(10R 안 죽음) */
    const Me& me = w.me; float hA = me.hp, dA = me.maxHp / 100.f, hE = 0.f, dE = 0.f, enIt = 0.f, alIt = (float)io::itemShardsOf(me.handle), en1It = 0.f; int nE = 0, nA = 0, dmin = 99;
    for (const auto& a : w.allies) { if (!a.visible || !a.alive) continue; const int dd = cheb(a.pos, me.pos); if (dd > 10) continue; ++nA; const float sh = (float)io::itemShardsOf(a.handle); alIt += sh;
        if (dd <= 8) { const float k = 1.f / (1.f + 2.f * (float)dd); hA += a.hp * k; dA += (a.maxHp / 100.f + sh * 0.5f) * k; } }
    for (const auto& e : w.enemies) { if (!e.alive) continue; const int dd = cheb(e.pos, me.pos); if (dd > 8) continue; ++nE; const float sh = (float)io::itemShardsOf(e.handle); enIt += sh;
        const float k = 1.f / (1.f + 2.f * (float)dd); hE += e.hp * k; dE += (e.maxHp / 100.f + sh * 0.5f) * k; if (dd < dmin) { dmin = dd; en1It = sh; } }
    if (nE == 0) return 0.f;
    const float L = std::log((hA * dA + 1.f) / (hE * dE + 1.f));
    const float z = 0.8692f - 0.86963f * L - 0.60343f * (me.hp / 1000.f) - 1.13114f * (me.hp / std::max(1.f, me.maxHp)) + 0.00362f * enIt + 0.00178f * alIt - 0.12668f * (float)dmin + 0.00364f * en1It - 0.08715f * (float)nE - 0.01571f * (float)nA;
    return 1.f / (1.f + std::exp(-z));
}
float trpPub(const World& w) { return trpP(w); }   /* FNR */
/* TPS: 순간이동 수정 지름길 (notes/audit_1025/top2_rest.md 3절). 값은 같은 단위 젬: 아낀 걸음 x rshRho - 수정 금 / goldDiv - TPS_W x 착지 dmgCost. */
struct TpsCand { Pos a, b; int jb; };
static std::vector<std::vector<TpsCand>> s_tpsC; static std::vector<std::uint8_t> s_tpsDone; static std::vector<int> s_tpsBest;
static std::vector<Pos> s_tpsHA; static std::vector<int> s_tpsHJb, s_tpsHSave;   // 집 거리장으로 본 무리별 가장 좋은 출발 칸 · 그 jb · 집에서 아끼는 걸음
static RawVec<std::uint16_t> s_tpsRow, s_tpsJb; static RawVec<std::uint8_t> s_tpsRowDx, s_tpsJbDy; static std::vector<std::uint16_t> s_tpsLv;
static int s_tpsG = -1, s_tpsY = 0, s_tpsPh = 0; static long long s_tpsStepCost = 0;
int tpsChannel() { static int t = -1; if (t < 0) t = std::max(1, io::tpChannel()); return t; }
static int s_tpsHaveR = -1000, s_tpsHave = 0;
static int tpsHave() { const int r = policy::PFX2_ON ? io::turnRound() : io::round();   /* PFX2 */ if (r != s_tpsHaveR) { s_tpsHaveR = r; s_tpsHave = io::consumableCount(0); } return s_tpsHave; }
void tpsNoteBuy() { s_tpsHave = 1; s_tpsHaveR = io::round(); }
static float tpsCrystalGems() { static int c = -1; if (c < 0) c = io::consumableCost(0); return (float)c / std::max(1.f, goldDiv()); }
static inline bool tpsLand(Pos b) { if (!S().inb(b)) return false; const size_t i = (size_t)S().idx(b); return !(S().tile[i] & (T_BLOCKED | T_CAMP)) && s_gfHome[i] < 65535; }   /* 설 수 있고 집에 걸어 닿는 칸 (돌아올 길) */
static bool tpsStep(int gi) {   // 한 줄씩: 0 줄 최솟값 · 1 칸 jb · 2 후보 뽑기. 다 끝나면 true
    const int W = S().W, H = S().H, R = rules::TELEPORT_RANGE, T = tpsChannel(); const auto& gf = s_gf[(size_t)gi]; const int y = s_tpsY;
    if (s_tpsPh == 0) {
        if ((int)s_tpsLv.size() != W) s_tpsLv.assign((size_t)W, 65535);
        for (int x = 0; x < W; ++x) { const Pos b{x, y}; s_tpsLv[(size_t)x] = tpsLand(b) ? gf[(size_t)S().idx(b)] : (std::uint16_t)65535; }
        for (int x = 0; x < W; ++x) { int m = 65535, md = R; for (int dx = -R; dx <= R; ++dx) { const int xx = x + dx; if (xx < 0 || xx >= W) continue; const int v = s_tpsLv[(size_t)xx]; if (v < m) { m = v; md = dx + R; } }
            const size_t i = (size_t)y * (size_t)W + (size_t)x; s_tpsRow[i] = (std::uint16_t)m; s_tpsRowDx[i] = (std::uint8_t)md; }
        if (++s_tpsY >= H) { s_tpsY = 0; s_tpsPh = 1; } return false;
    }
    if (s_tpsPh == 1) {
        for (int x = 0; x < W; ++x) { int m = 65535, md = R; for (int dy = -R; dy <= R; ++dy) { const int yy = y + dy; if (yy < 0 || yy >= H) continue; const int v = s_tpsRow[(size_t)yy * (size_t)W + (size_t)x]; if (v < m) { m = v; md = dy + R; } }
            const size_t i = (size_t)y * (size_t)W + (size_t)x; s_tpsJb[i] = (std::uint16_t)(m >= 65535 ? 65535 : std::min(65534, m + T)); s_tpsJbDy[i] = (std::uint8_t)md; }
        if (++s_tpsY >= H) { s_tpsY = 0; s_tpsPh = 2; } return false;
    }
    for (int x = 0; x < W; ++x) {
        const size_t i = (size_t)y * (size_t)W + (size_t)x; const int jb = s_tpsJb[i]; if (jb >= 65535) continue;
        const int ga = gf[i]; if (ga >= 65535 || jb >= ga || s_gfHome[i] >= 65535 || (S().tile[i] & T_BLOCKED)) continue;   // 출발 칸: 집에서 걸어 닿음
        bool dom = false;   // 이웃 a' 가 jb(a') <= jb(a) - 1 이면 a 길은 a' 길보다 늘 길거나 같다
        for (int ny = -1; ny <= 1 && !dom; ++ny) for (int nx = -1; nx <= 1; ++nx) { if (!nx && !ny) continue; const Pos q{x + nx, y + ny}; if (!S().inb(q)) continue; const size_t k = (size_t)S().idx(q);
            if ((S().tile[k] & T_BLOCKED) || s_gfHome[k] >= 65535) continue; if ((int)s_tpsJb[k] + 1 <= jb) { dom = true; break; } }
        if (dom) continue;
        const int yy = y + (int)s_tpsJbDy[i] - R; const int xx = x + (int)s_tpsRowDx[(size_t)yy * (size_t)W + (size_t)x] - R;
        s_tpsC[(size_t)gi].push_back(TpsCand{Pos{x, y}, Pos{xx, yy}, jb}); s_tpsBest[(size_t)gi] = std::max(s_tpsBest[(size_t)gi], ga - jb);
    }
    if (++s_tpsY >= H) return true;
    return false;
}
void tpsPrewarm(long long floorFuel) {
    if (!policy::TPS_ON || s_gfHome.empty() || S().groups.empty()) return;
    const size_t n = S().groups.size(), WH = (size_t)S().W * (size_t)S().H;
    if (s_tpsDone.size() != n) { s_tpsDone.assign(n, 0); s_tpsC.assign(n, {}); s_tpsBest.assign(n, 0); s_tpsHA.assign(n, Pos{-1, -1}); s_tpsHJb.assign(n, 65535); s_tpsHSave.assign(n, 0); }
    const int r0 = io::round();
    while (io::round() == r0 && io::fuel() >= floorFuel + 3000 + s_tpsStepCost) {
        if (s_tpsG < 0) {
            int gi = -1; if (M.group >= 0 && (size_t)M.group < n && !s_tpsDone[(size_t)M.group] && gfReady(M.group)) gi = M.group;   // 임무 무리 먼저
            for (size_t g = 0; g < n && gi < 0; ++g) if (!s_tpsDone[g] && gfReady((int)g)) gi = (int)g;
            if (gi < 0) return;
            s_tpsG = gi; s_tpsY = 0; s_tpsPh = 0; s_tpsC[(size_t)gi].clear(); s_tpsBest[(size_t)gi] = 0;
            if (s_tpsRow.size() != WH) { s_tpsRow.resize(WH); s_tpsRowDx.resize(WH); s_tpsJb.resize(WH); s_tpsJbDy.resize(WH); }
        }
        const long long f0 = io::fuel(); const int ra = io::round();
        const bool fin = tpsStep(s_tpsG);
        const long long c = f0 - io::fuel(); s_tpsStepCost = io::round() != ra ? std::max(s_tpsStepCost, c) : std::max(c, s_tpsStepCost * 9 / 10);
        if (fin) { const int g = s_tpsG; s_tpsDone[(size_t)g] = 1; s_tpsG = -1;
            int hv = 65535; for (const auto& c : s_tpsC[(size_t)g]) { const int v = (int)s_gfHome[(size_t)S().idx(c.a)] + c.jb; if (v < hv) { hv = v; s_tpsHA[(size_t)g] = c.a; s_tpsHJb[(size_t)g] = c.jb; } }
            const int hw = gfAt(s_gf[(size_t)g], S().allyCenter); s_tpsHSave[(size_t)g] = (hw < 65535 && hv < 65535) ? std::max(0, hw - hv) : 0;
            TPS_LOG("#tps tab %d %d g=%d n=%d best=%d home=%d", io::round(), S().myIndex, g, (int)s_tpsC[(size_t)g].size(), s_tpsBest[(size_t)g], s_tpsHSave[(size_t)g]); }
    }
}
bool tpsWorthBuying(const World& w) {
    (void)w; if (!policy::TPS_ON) return false; int best = 0; for (size_t g = 0; g < s_tpsDone.size(); ++g) if (s_tpsDone[g]) best = std::max(best, s_tpsHSave[g]);   // 집에서 무리로 가는 길에 쓸 곳
    return best > 0 && (float)best * rshRho() > tpsCrystalGems();
}
static int s_tpsRSince = -2, s_tpsRG = -2; static Pos s_tpsRA{-1, -1}; static RawVec<std::uint16_t> s_tpsAF; static Pos s_tpsAFPos{-1, -1};
static int s_tpsBestDA = 1 << 30, s_tpsDARound = -1;
void tpsDrop() { s_tpsRA = Pos{-1, -1}; }
static void tpsTick(const World& w) {   // 길 고르기 (임무마다 한 번) · 출발 칸 거리장 · 진행
    if (!policy::TPS_ON) return;
    const bool farm = (M.kind == M_FARM || M.kind == M_CAPTURE) && M.group >= 0;
    if (!farm || tpsHave() <= 0) { s_tpsRA = Pos{-1, -1}; s_tpsRSince = -2; return; }
    const int gi = M.group;
    if (s_tpsRSince != M.since || s_tpsRG != gi) {
        s_tpsRA = Pos{-1, -1};
        if (!gfReady(gi) || (size_t)gi >= s_tpsDone.size() || !s_tpsDone[(size_t)gi]) return;   // 표가 아직 — 다음 턴에 다시
        if (s_tpsC[(size_t)gi].empty()) { s_tpsRSince = M.since; s_tpsRG = gi; return; }
        if (!(s_meValuesRound >= 0 && s_meValuesOrigin == w.me.pos)) { if (io::fuel() < policy::FS_FUEL || !ensureCurrentMe(w)) return; }
        s_tpsRSince = M.since; s_tpsRG = gi;
        const int walk = gfAt(s_gf[(size_t)gi], w.me.pos); if (walk >= 65535) return;
        int best = 65535; Pos ba{-1, -1};
        for (const auto& c : s_tpsC[(size_t)gi]) { const int da = (int)s_meValues[(size_t)S().idx(c.a)]; if (da >= 65535) continue; const int v = da + c.jb; if (v < best) { best = v; ba = c.a; } }
        const int save = walk - best; const float val = (float)save * rshRho() - tpsCrystalGems();
        if (ba.x >= 0 && save > 0 && val > 0.f) { s_tpsRA = ba; s_tpsBestDA = 1 << 30; s_tpsDARound = w.round; TPS_LOG("#tps route %d %d g=%d a=%d,%d walk=%d via=%d val=%.1f", w.round, S().myIndex, gi, ba.x, ba.y, walk, best, val); }
    }
    if (s_tpsRA.x < 0) return;
    if (!(s_tpsAFPos == s_tpsRA)) { if (io::fuel() < s_gfCost + zres()) return; if (!io::fieldExport(std::vector<Pos>{s_tpsRA}, s_tpsAF)) { s_tpsRA = Pos{-1, -1}; return; } s_tpsAFPos = s_tpsRA; }
    const int da = (int)s_tpsAF[(size_t)S().idx(w.me.pos)];
    if (da < s_tpsBestDA) { s_tpsBestDA = da; s_tpsDARound = w.round; M.progressRound = w.round; }   // 출발 칸으로 다가가면 진행
    else if (w.round - s_tpsDARound >= policy::STALL_ROUNDS) s_tpsRA = Pos{-1, -1};
}
static int tpsVia(const World& w, int gi, int walk) {   /* 고르기 걸음: 수정을 들었고 값 > 0 이면 집 쪽 출발 칸 길 (내 거리장 · 스폰이면 집 거리장) */
    if (!policy::TPS_ON || gi < 0 || (size_t)gi >= s_tpsDone.size() || !s_tpsDone[(size_t)gi] || s_tpsHA[(size_t)gi].x < 0 || walk >= 65535 || tpsHave() <= 0) return walk;
    const size_t ia = (size_t)S().idx(s_tpsHA[(size_t)gi]); int da;
    if (s_meValuesRound >= 0 && s_meValuesOrigin == w.me.pos) da = (int)s_meValues[ia]; else if (w.me.atSpawn) da = (int)s_gfHome[ia]; else return walk;
    if (da >= 65535) return walk; const int via = da + s_tpsHJb[(size_t)gi];
    return (via < walk && (float)(walk - via) * rshRho() > tpsCrystalGems()) ? via : walk;
}
bool tpsAtA(const World& w) { return s_tpsRA.x >= 0 && w.me.pos == s_tpsRA; }
bool tpsMoveCell(const World& w, Pos& q) {
    if (s_tpsRA.x < 0 || !(s_tpsAFPos == s_tpsRA) || s_tpsAF.empty() || w.me.pos == s_tpsRA) return false;
    int bd = (int)s_tpsAF[(size_t)S().idx(w.me.pos)]; Pos bq{-1, -1};
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos c{w.me.pos.x + dx, w.me.pos.y + dy};
        if (!S().inb(c) || !S().walkableStatic(c) || io::campBlocksMovement(c) || S().occupiedNow(c, w.round)) continue; const int v = (int)s_tpsAF[(size_t)S().idx(c)]; if (v < bd) { bd = v; bq = c; } }
    if (bq.x < 0) return false; q = bq; return true;
}
bool tpsPick(const World& w, Pos& outB, int& outSave, float& outVal) {
    if (!policy::TPS_ON || w.me.channeling || s_gfHome.empty() || tpsHave() <= 0) return false;
    const RawVec<std::uint16_t>* f = nullptr; int tg = -1;
    if ((M.kind == M_FARM || M.kind == M_CAPTURE) && gfReady(M.group)) { f = &s_gf[(size_t)M.group]; tg = M.group; }
    else if (M.kind == M_TRIBUTE && M.altar >= 0 && (size_t)M.altar < s_awF.size() && !s_awF[(size_t)M.altar].empty()) f = &s_awF[(size_t)M.altar];
    if (!f) return false;
    const int here = gfAt(*f, w.me.pos); if (here >= 65535) return false;
    const int T = tpsChannel(), R = rules::TELEPORT_RANGE; int bj = 65535; Pos bb{-1, -1};
    for (int dy = -R; dy <= R; ++dy) for (int dx = -R; dx <= R; ++dx) { const Pos b{w.me.pos.x + dx, w.me.pos.y + dy}; if (!tpsLand(b) || S().occupiedNow(b, w.round)) continue;
        const int v = (int)(*f)[(size_t)S().idx(b)]; if (v < bj) { bj = v; bb = b; } }
    if (bb.x < 0) return false;
    const int save = here - (T + bj); if (save <= 0) return false;
    float dmg = combat::enemyThreatAt(w, bb);   // 착지 위협: 보이는 적 (LFR 잣대) + 목표 아닌 깬 캠프의 한 R 피해
    for (const auto& c : w.camps) if (c.str > 0.f && c.hp > 0.f && c.group >= 0 && c.group != tg && S().groups[(size_t)c.group].awakeUntil >= w.round && cheb(c.pos, bb) <= S().campRange(c.type)) dmg += c.str;
    const float risk = dmg > 0.f ? policy::TPS_W * combat::dmgCost(dmg, w.me.hp, w.me.maxHp, w.me.gems) : 0.f;
    const float val = (float)save * rshRho() - tpsCrystalGems() - risk;
    outB = bb; outSave = save; outVal = val; return val > 0.f;
}
void tpsUsed(const World& w, Pos b, int save, float val) {
    TPS_LOG("#tps use %d %d k=%d g=%d a=%d,%d b=%d,%d save=%d val=%.1f gems=%d hp=%.0f", w.round, S().myIndex, (int)M.kind, M.kind == M_TRIBUTE ? M.altar : M.group, w.me.pos.x, w.me.pos.y, b.x, b.y, save, val, w.me.gems, w.me.hp);
    s_tpsHave = 0; s_tpsHaveR = io::round(); s_tpsRA = Pos{-1, -1};
}
static int g_rsw2G = -1, g_rsw2Since = -1;   /* RSW2b */
/* TBA: 팀 단위 큰 무리 배정 (notes/teamassign_1102/design.md 6절). 입력 = 공유 팀원 보기 · 정적 맵 · 챔피언 종류뿐 → 모든 몸 같은 답 */
struct TbaPlan { int g = -1, mask = 0, K = 0, gather = 0; float gain = 0.f; };
static int s_tbaType[team::MAX]; static bool s_tbaTypeOk = false;
static std::vector<float> s_tbaH, s_tbaC;   // 무리마다 정적 총 체력 · 받을 피해 계수 (TFP gH · gC 와 같은 식, 힘/체력 큰 순으로 처치)
static int g_tbaSince = -1000, g_tbaG = -1;
static std::vector<int> s_tbaLeft;
static int s_tbaClG[team::MAX], s_tbaClR[team::MAX]; static bool s_tbaClInit = false;   // TBA v5: 칸마다 담당 무리 · 처음 본 라운드   // TBA v2: 무리마다 내 TBA 임무가 끝난 라운드 (그 무리 리스폰 시간 동안 다시 안 감)
static bool tbaHold() { return policy::TBA1_ON && M.kind == M_FARM && M.since == g_tbaSince && M.group == g_tbaG; }
static void tbaStatic() {
    if (!s_tbaTypeOk) { const std::vector<std::uint32_t>& hs = sortedAllyH(); bool all = true;
        for (int k = 0; k < team::MAX; ++k) { s_tbaType[k] = -1; if ((size_t)k < hs.size()) { int t = -1; if (io::championIdentity(hs[(size_t)k], t)) s_tbaType[k] = t; else all = false; } }
        s_tbaTypeOk = all; }
    const size_t NG = S().groups.size();
    if (s_tbaH.size() != NG) { s_tbaH.assign(NG, 0.f); s_tbaC.assign(NG, 0.f);
        for (size_t g = 0; g < NG; ++g) { const Group& G = S().groups[g]; std::vector<std::pair<float, float>> c;
            for (int t : G.campType) c.push_back({S().campHp(t), std::max(0.f, S().campStr(t))});
            std::sort(c.begin(), c.end(), [](const std::pair<float, float>& a, const std::pair<float, float>& b) { const float l = a.second * b.first, r = b.second * a.first; return l > r || (l == r && a.first < b.first); });
            float sA = 0.f; for (const auto& x : c) sA += x.second;
            float h = 0.f, cc = 0.f; for (const auto& x : c) { h += x.first; cc += sA * x.first; sA -= x.second; }
            s_tbaH[g] = h; s_tbaC[g] = cc; } }
}
static TbaPlan tbaPlan(const World& w) {
    TbaPlan best; const int n = std::min(team::T.n, team::MAX); if (n < 2 || team::T.slot < 0) return best;
    team::Mate mv[team::MAX];   // TBA v3: planView 와 같은 보기 (지난 라운드 끝) 를 싸게 — 정렬된 핸들은 저장본 (planView 는 부를 때마다 복사 · 정렬)
    { const std::vector<std::uint32_t>& hs = sortedAllyH();
      for (int k = 0; k < n; ++k) { if (k == team::T.slot) { mv[k] = team::T.selfSeen; continue; } mv[k] = team::T.mate[k];
          if ((size_t)k < hs.size()) for (const auto& a : w.allies) if (a.handle == hs[(size_t)k]) { if (a.acted) mv[k] = team::T.prevMate[k]; break; } } }
    const float div = gvtPick(); tbaStatic();
    const int NG = (int)S().groups.size(); float vmax = 0.f;
    for (int g = 0; g < NG; ++g) vmax = std::max(vmax, stkVal(g, div));
    if (vmax <= 0.f) return best;
    auto dpsOf = [&](int k) { const int t = s_tbaType[k]; if (t < 0) return 0.f; const ChampDesign& D = S().designOf(t); return D.valid ? ksDps(t, D.str, D.focus) : 0.f; };
    auto budOf = [&](int k) { const int t = s_tbaType[k]; if (t < 0) return 0.f; const ChampDesign& D = S().designOf(t); return D.valid ? D.hp * (1.f - policy::RECOVER_HP) : 0.f; };
    auto stepsOf = [&](int k, int g) { const team::Mate& m = mv[k]; if (!m.known || m.zone < 0 || m.zone >= 64) return 9999; return cheb(team::zoneCenter(m.zone), S().groups[(size_t)g].center); };
    auto timeOf = [&](int g, float dS, float bS, int K, int gather) {
        if (dS <= 0.f || gather >= 9999 || K < 1) return 1e9f;
        const float kill = s_tbaH[(size_t)g] / dS, over = std::max(0.f, s_tbaC[(size_t)g] / dS - bS), home = (float)cheb(S().groups[(size_t)g].center, S().allyCenter);
        const float rec = over > 0.f ? std::ceil(over / std::max(1.f, bS / (float)K)) * ((float)(rules::RECALL_TICKS + 1) + home) / (float)K : 0.f;
        return (float)gather + kill + (float)rules::CAPTURE_ROUNDS + rec; };
    auto claimOf = [&](int k) { const team::Mate& m = mv[k]; return (m.known && m.claimKind == team::CL_FARM && m.claimGroup >= 0 && m.claimGroup < NG) ? m.claimGroup : -1; };
    if (!s_tbaClInit) { s_tbaClInit = true; for (int k = 0; k < team::MAX; ++k) { s_tbaClG[k] = -1; s_tbaClR[k] = -100000; } }
    auto fresh = [&](int k, int g) { return s_tbaClG[k] == g && w.round - s_tbaClR[k] <= stepsOf(k, g) + policy::OSY_WAIT; };   // TBA v5: 담당자가 아직 도착 전 (새 담당)
    auto soloRate = [&](int k, int g) { if (g < 0) return 0.f; const float T = timeOf(g, dpsOf(k), budOf(k), 1, stepsOf(k, g)); return T < 1e8f ? stkVal(g, div) / T : 0.f; };
    auto eligible = [&](int k) { const team::Mate& m = mv[k]; if (!(m.known && m.outReason == 0 && m.claimKind != team::CL_CAPTURE && m.claimKind != team::CL_TRIBUTE && s_tbaType[k] >= 0)) return false;
        const int cg2 = claimOf(k); return cg2 < 0 || stkVal(cg2, div) < 0.5f * vmax; };   // TBA v2: 큰 무리를 맡은 몸은 다른 큰 무리로 끌지 않음
    for (int c = 0; c < n; ++c) { const int g = claimOf(c); if (g < 0 || stkVal(g, div) < 0.5f * vmax || !fresh(c, g)) continue;
        bool dup = false; for (int c2 = 0; c2 < c; ++c2) if (claimOf(c2) == g) { dup = true; break; }
        if (dup) continue;
        int ord[team::MAX], no = 0; for (int k = 0; k < n; ++k) if (claimOf(k) == g) ord[no++] = k;
        const int nClaim = no; int rest[team::MAX], nr = 0;
        for (int k = 0; k < n; ++k) if (claimOf(k) != g && eligible(k)) rest[nr++] = k;
        std::sort(rest, rest + nr, [&](int a, int b) { const int sa = stepsOf(a, g), sb = stepsOf(b, g); return sa < sb || (sa == sb && a < b); });
        for (int i = 0; i < nr; ++i) ord[no++] = rest[i];
        float dS = 0.f, bS = 0.f, lose = 0.f; int gmx = 0;
        for (int i = 0; i < nClaim; ++i) { dS += dpsOf(ord[i]); bS += budOf(ord[i]); gmx = std::max(gmx, stepsOf(ord[i], g)); }
        const float T0 = timeOf(g, dS, bS, nClaim, gmx), base = T0 < 1e8f ? stkVal(g, div) / T0 : 0.f;
        /* TBA v13: 담당자만으로 버텨도 계획 (빨리 깨는 이득을 셈 — 랭커 모여 열기 꼴) */
        const int gClaim = gmx;
        for (int K = nClaim + 1; K <= no; ++K) { const int k = ord[K - 1];
            if (stepsOf(k, g) >= 9999) break;
            const float dS2 = dS + dpsOf(k);
            if (stepsOf(k, g) > gClaim + policy::OSY_WAIT) break;   /* TBA v8: 담당자와 거의 같이 닿는 몸만 (함께 출발 꼴) */   // TBA v5: 담당자 도착 + 처치 R 안에 못 닿으면 (걸음 순이라 뒤도 못 닿음)
            dS = dS2; bS += budOf(k); gmx = std::max(gmx, stepsOf(k, g)); lose += soloRate(k, claimOf(k));
            if (s_tbaC[(size_t)g] / dS > bS && K < no) continue;   // TBA v13: 버티지 못하는 인원은 건너뜀 (모두 불러도 못 버티면 아래에서 거름)
            if (s_tbaC[(size_t)g] / dS > bS) break;   // 모두 불러도 못 버팀 (보스) = 계획 없음
            const float Tt = timeOf(g, dS, bS, K, gmx); if (Tt >= 1e8f) break;
            const float gain = stkVal(g, div) / Tt - base - lose;
            if (gain > best.gain || (gain == best.gain && gain > 0.f && g < best.g)) { best.g = g; best.K = K; best.gain = gain; best.gather = gmx; best.mask = 0; for (int i = 0; i < K; ++i) best.mask |= 1 << ord[i]; }
            /* TBA v13: 계속 늘려 봄 (이득 가장 큰 K) */
        } }
    return best;
}
static TbaPlan tbaPlanC(const World& w) {   /* TBA v3: 보기 해시로 저장 */
    static unsigned s_h = 1; static TbaPlan s_p; const int n = std::min(team::T.n, team::MAX); if (n < 2 || team::T.slot < 0) return TbaPlan{};
    team::Mate mv[team::MAX]; const std::vector<std::uint32_t>& hs = sortedAllyH();
    for (int k = 0; k < n; ++k) { if (k == team::T.slot) { mv[k] = team::T.selfSeen; continue; } mv[k] = team::T.mate[k];
        if ((size_t)k < hs.size()) for (const auto& a : w.allies) if (a.handle == hs[(size_t)k]) { if (a.acted) mv[k] = team::T.prevMate[k]; break; } }
    if (!s_tbaClInit) { s_tbaClInit = true; for (int k = 0; k < team::MAX; ++k) { s_tbaClG[k] = -1; s_tbaClR[k] = -100000; } }
    unsigned h = 2166136261u; auto mix = [&](int x) { h ^= (unsigned)(x + 7); h *= 16777619u; };
    for (int k = 0; k < n; ++k) { const int cg = (mv[k].known && mv[k].claimKind == team::CL_FARM) ? mv[k].claimGroup : -1;
        if (cg != s_tbaClG[k]) { s_tbaClG[k] = cg; s_tbaClR[k] = w.round; }   // TBA v5: 담당이 바뀐 라운드 (몸마다 자기 기억)
        mix((int)mv[k].known); mix(mv[k].zone); mix(mv[k].claimGroup); mix(mv[k].claimKind); mix(mv[k].outReason);
        const int st = (cg >= 0 && mv[k].zone >= 0 && mv[k].zone < 64 && (size_t)cg < S().groups.size()) ? cheb(team::zoneCenter(mv[k].zone), S().groups[(size_t)cg].center) : 9999;
        mix(cg >= 0 && w.round - s_tbaClR[k] <= st + policy::OSY_WAIT ? 1 : 0); }   // 신선함이 끝나면 다시 셈
    mix((int)(gvtPick() * 16.f)); mix(n);
    if (h != s_h) { s_h = h; s_p = tbaPlan(w); }
    return s_p;
}
static TbaPlan g_tbaCache; static int g_tbaCacheR = -1000;   /* TBA v11: 턴 끝 남는 연료로 계산한 계획 */
void tbaPrewarm(const World& w, long long floorFuel) { if (!(policy::TBA1_ON || policy::TBA2_ON || policy::TBA_LOG_ON) || io::fuel() < floorFuel + 3000) return; g_tbaCache = tbaPlanC(w); g_tbaCacheR = w.round; }
static TbaPlan tbaCached(const World& w) { return (w.round - g_tbaCacheR <= team::LIFE_CLAIM) ? g_tbaCache : TbaPlan{}; }
static bool tbaWait(const World& w, int group) {   /* TBA2: 계획 멤버가 곁에 다 올 때까지 깨우지 않음 */
    if (!policy::TBA2_ON || group < 0 || (size_t)group >= S().groups.size() || M.kind != M_FARM || M.group != group || w.enemyNear8 || team::T.slot < 0) return false;
    const Group& G = S().groups[(size_t)group]; if (G.strMax <= 0.f || G.awakeUntil >= w.round || !(G.state == 0 || G.state == 1)) return false;
    if (cheb(G.center, w.me.pos) > G.rangeMax + 3) return false;
    for (size_t q = 0; q < G.campType.size() && q < G.campLastHp.size(); ++q) if (G.campLastHp[q] > 0.f && G.campLastHp[q] < S().campHp(G.campType[q]) - 0.5f) return false;   // TBA v6: 이미 깎인 무리는 안 기다림 (마무리)
    const TbaPlan P = tbaCached(w); if (P.g != group || !((P.mask >> team::T.slot) & 1)) return false;
    int need = 0; for (int k = 0; k < team::MAX; ++k) if ((P.mask >> k) & 1) ++need;
    int here = 1; unsigned hereMask = 1u << team::T.slot; const std::vector<std::uint32_t>& hs = sortedAllyH();
    for (const auto& a : w.allies) { if (!a.visible || !a.alive || cheb(a.pos, G.center) > G.rangeMax + 2) continue;
        for (size_t q = 0; q < hs.size() && q < (size_t)team::MAX; ++q) if (hs[q] == a.handle && (int)q != team::T.slot && ((P.mask >> q) & 1)) { ++here; hereMask |= 1u << q; break; } }
    if (here >= need) return false;
    bool coming = false;   // TBA v6: 곁에 없는 멤버 중 이 무리를 담당 (공유 칸) 한 몸 = 오고 있음
    for (int k = 0; k < std::min(team::T.n, team::MAX); ++k) { if (!((P.mask >> k) & 1) || ((hereMask >> k) & 1)) continue; const team::Mate& m = team::T.mate[k];
        if (m.known && m.outReason == 0 && m.claimKind == team::CL_FARM && m.claimGroup == group) { coming = true; break; } }
    if (!coming) return false;
    static int s_g = -1, s_since = -1000, s_start = -1000, s_lim = 0;
    if (s_g != group || s_since != M.since) { s_g = group; s_since = M.since; s_start = w.round; s_lim = policy::OSY_WAIT; }   /* TBA v8: 기다림은 OSY_WAIT 안 */
    return w.round - s_start <= s_lim;
}
void update(World& w) {
    if (g_opdDashDefer && (M.lastHitRound >= 0 || M.kind != M_FARM)) { g_opdDashDefer = false; io::S.dashNav = true; for (auto& v : s_gf) v.clear(); s_gfHome.clear(); }   /* OPDW: 첫 무리를 치기 시작하면 (또는 농사가 아니면) 다리 길 켬 */
    if (g_brfRepick) { g_brfRepick = false; if (M.kind == M_FARM && !g_opdUsed) {   /* OPD: 계획은 이미 다리를 셈 */ setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; M.why = "brf2"; } }   /* BRF2: 다리가 생기면 거리가 바뀌니 농사 목표 다시 고름 */
    struct SnrmAtExit { ~SnrmAtExit() { snrmSet(); } } snrmAtExit;   /* SNRM: 이번 턴 결정 뒤 다음 감지에 */
    M.why = "-";
    s_ucpPos = w.me.pos;   /* UCP */
    if (policy::TERR_ON) for (int i = 0; i < team::T.terrEvN; ++i) { const int g = team::T.terrEv[i]; if (g < 0 || (size_t)g >= io::S.groups.size()) continue; Group& G = io::S.groups[(size_t)g];   /* TERR T2: 팀원이 본 되살아남 → 내 기억 (직접 본 것이 우선) */
        if (G.stateRound == w.round || (G.state == 3 && G.respawnUntil >= w.round) || G.respawn <= 0) continue;
        G.state = 3; G.respawnUntil = w.round - 1 + G.respawn; ++io::S.openVersion; }
    const Me& me = w.me;
    if (me.alive) { if (s_kbLastGold >= 0 && me.gold > s_kbLastGold) s_kbGold += (float)(me.gold - s_kbLastGold); s_kbLastGold = me.gold; }   /* RSH3 */
    { static int s_kcLastR = -1; if (me.alive && w.round != s_kcLastR) { s_kcLastR = w.round; if (VIS_NEAR(w)) s_kbContact += 1.f; } }   /* KTRC */
    if (me.alive) { s_kbWasAlive = true; s_kbAlive += 1.f; if (me.gems > s_kbLastGems) s_kbEarned += (float)(me.gems - s_kbLastGems); s_kbLastGems = me.gems; }   // KB
    if (!me.alive) { setGoal(M_NONE, -1, -1, w); M.why = "dead"; return; }
    if (policy::TFP_ON) tfpPlan(w);   /* TFP2 */
    // 0. (R_RETREAT) 적 반응: 수적 열세 (적 ≥ 나+아군+1) 또는 체력 < 40% 에 적이 보이면 임무를 내려놓고 물러난다. 적이 사라지고 체력이 돌아오면 다시 고른다
    float fs2Th = 0.f; bool fs2ThOk = false; auto fs2ThrMe = [&]() -> float { if (!policy::FS2_MIS1_ON) return combat::enemyThreatAt(w, me.pos); if (!fs2ThOk) { fs2Th = combat::enemyThreatAt(w, me.pos); fs2ThOk = true; } return fs2Th; };   /* FS2_MIS1: 내 칸 위협 한 번 (update 는 w 를 안 고침) */
    int fs2Jt = -1; auto fs2JtnNow = [&]() -> bool { if (!policy::FS2_MIS1_ON) return combat::joinTargetNow(w); if (fs2Jt < 0) fs2Jt = combat::joinTargetNow(w) ? 1 : 0; return fs2Jt == 1; };   /* FS2_MIS1: 곁 싸움 합류 한 번 */
    /* RTGATE: 후퇴 시작 · 재확인(JNL)이 같이 쓰는 '남을 이유 없음' 한 곳. 동작은 이전과 같음 */
    auto rtGate = [&]() -> bool { return !(combat::teamStayGain(w) > 0.f) && !(me.hp >= policy::RETREAT_HP * me.maxHp && fs2ThrMe() <= 0.f && fs2JtnNow()) /* JNR */ && trpP(w) > 0.02f && !combat::fbStay(w); };
    bool rc2Pre = false;   /* BH3_RC2: 후퇴 아닌 회복 몸의 후퇴 신호 (열세 · 저체력 · LFR, M_RECOVER 거름 없음) */
    if (policy::R_RETREAT) {
        int enN = 0; for (const auto& e : w.enemies) if (threatening(w, e)) ++enN;
        if (enN > 0) M.lastEnemyRound = w.round;
        const bool fs2Need = !policy::FS2_MIS1_ON || (!me.atSpawn && M.kind != M_RECOVER) || (policy::BH3_RC2_ON && M.kind == M_RECOVER && !M.retreat && enN > 0 && !me.atSpawn);   /* FS2_MIS1: outnumbered 를 읽는 곳 (후퇴 시작 · RC2) 이 쓸 때만 셈 */
        float fs2Ep = 0.f, fs2Op = 0.f; int fs2EnN = 0; if (policy::FS2_MIS1_ON && fs2Need) fs2EnN = r3Sums(w, fs2Ep, fs2Op);   /* FS2_MIS1: r3Sums 한 번 → HYS · SOLO 가 다시 씀 */
        bool outnumbered = policy::FS2_MIS1_ON ? (fs2Need && fs2EnN > 0 && fs2Ep >= fs2Op * policy::R3_RATIO) : r3Outnumbered(w);   /* R3: 힘 비교 */   /* FS2_MIS1: r3Outnumbered 와 같은 식 */
        static float s_hpPrev = -1.f; static int s_hpR = -10;   /* HYS: 싸우는 중 */   /* MINE: s_hurtR 는 파일 범위 */
        if (s_hpR == w.round - 1 && me.hp < s_hpPrev - 0.5f && enN > 0) s_hurtR = w.round; if (s_hpR == w.round - 1 && me.hp < s_hpPrev - 0.5f && enN == 0) s_campHurtR = w.round; s_hpPrev = me.hp; s_hpR = w.round;   /* HRF 검토 B: 기록만 (HRF 꺼짐이면 안 읽음) */
        const bool inFight = w.round - combat::lastChampHit() <= 2 || w.round - s_hurtR <= 2;
        if (policy::HYS_ON && inFight && !outnumbered && fs2Need) outnumbered = (policy::FS2_MIS1_ON ? r3RatioFrom(w, fs2Ep, fs2Op) : r3Ratio(w)) <= policy::HYS_EXIT;   /* FS2_MIS1 */   /* HYSF: 싸우는 중이면 적 1.5배에서 나옴 (조건이 거꾸로라 아무 일 안 하던 것 고침) */
        if (!outnumbered && enN > 0 && fs2Need) {   /* FS2_MIS1 */   /* SOLO: 혼자면 팀 싸움과 같은 젬 손익 (s·적 판돈 − (1−s)·내 판돈 < 0 이면 열세) — 고정 R3 2배 대신 */
            bool alone = true; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, me.pos) <= policy::RETREAT_ALLY_R && !((policy::RAL_ON || policy::RALO_ON) && ralOut(w, a))) { alone = false; break; }
            if (alone) { float ep = 0.f, op = 0.f; if (policy::FS2_MIS1_ON) { ep = fs2Ep; op = fs2Op; } else r3Sums(w, ep, op);   /* FS2_MIS1: 같은 w 의 같은 합 */
                { float hE = 0.f, dE = 0.f, hI = 0.f, dI = 0.f; const float dA = combat::unitDps(me.type, me.str, me.focus);   /* SOLO2: 3턴 안에 닿을 적도 (도착 전 내게 맞을 피해를 뺀 체력) — TSA 와 짝 */
                  for (const auto& e : w.enemies) { if (!e.alive || !e.visible) continue; if (threatening(w, e)) { hE += e.hp; dE += combat::unitDps(e.type, e.str, e.focus); continue; }
                    const int eta = std::max(0, cheb(e.pos, me.pos) - (std::max(1, e.range) + 1)); if (eta > policy::MJG_ARRIVE) continue; const float h = e.hp - dA * (float)eta; if (h > 0.f) { hI += h; dI += combat::unitDps(e.type, e.str, e.focus); } }
                  if (hI > 0.f && hE > 0.f) ep = std::max(ep, (hE + hI) * (dE + dI)); }
                if (ep > 0.f) { const float R = op / (ep * combat::depthK(me.pos)), s = R / (1.f + R), kg = (float)S().killGold / goldDiv() + policy::PLV_DEATH;
                    float stE = 0.f; for (const auto& e : w.enemies) if (threatening(w, e)) stE += (float)e.gems + kg;   /* SOLO3: 판돈은 지금 닿는 적만 (오는 적은 힘에만) */
                    const float stU = (float)me.gems + kg + combat::twinStake(w);   /* TWN */
                    if (s * stE - (1.f - s) * stU < -policy::PLV_T * policy::PLV_V) outnumbered = true; } } }   /* SOLO4: 후퇴도 몸 하나 기회비용 (팀 식과 같은 단위) */   /* HYS: 나오는 기준 한 단계 낮음 */
        const bool lowHpEn = enN > 0 && me.hp < me.maxHp * policy::RETREAT_HP;
        bool lfr = policy::LFR_ON && enN > 0 && !me.atSpawn && M.kind != M_RECOVER && fs2ThrMe() >= me.hp * policy::LFR_FRAC;   /* FS2_MIS1 */
        if (enN > 0 && !me.atSpawn && M.kind != M_RECOVER) { const float kr = combat::fightKillRounds(w); if (kr >= 0.f) { const float th = fs2ThrMe(); lfr = th > 0.f && me.hp / th < kr; } }   /* FS2_MIS1 */   /* LFD: 내가 죽기까지 라운드 < 팀이 표적을 잡기까지 라운드 면 후퇴 (고정 0.45 대신) */
        if (lfr && !(combat::fightOdds(w) < 0.5f)) lfr = false;   /* LFDT: 팀이 이기는 싸움이면 LFR 후퇴 안 함 (LFT2) */   /* LFR */
        int jnaG = -1;   /* JNA: 전투 계산이 밀리면 동료에게 합류 */
        if (enN > 0 && !lowHpEn && !lfr && !me.atSpawn && M.kind != M_RECOVER && me.hp >= policy::RETREAT_HP * me.maxHp && combat::fightOdds(w) < 0.5f) {
            auto enD = [&](Pos q) { int bb = 99; for (const auto& e : w.enemies) if (e.alive) bb = std::min(bb, cheb(e.pos, q)); return bb; };
            const int myE = enD(me.pos); int bestN = -1, bestD = 99;
            const std::vector<std::uint32_t>& hs = sortedAllyH();   /* FUELH */
            for (const auto& a : w.allies) {
                if (!a.visible || !a.alive || cheb(a.pos, me.pos) > policy::RGP_R || a.hp < policy::RETREAT_HP * a.maxHp || enD(a.pos) < myE) continue;
                int k = -1; for (size_t q = 0; q < hs.size(); ++q) if (hs[q] == a.handle) k = (int)q;
                if (k < 0 || k >= team::T.n || k == team::T.slot) continue; const auto& mt = team::T.mate[k];
                const int g = mt.claimGroup; if (g < 0 || g >= (int)S().groups.size() || mt.claimKind != team::CL_FARM) continue;
                const Group& Gj = S().groups[(size_t)g]; if (Gj.state == 2 || groupUnreachable(g) || enD(Gj.center) < myE) continue;
                int nA = 0; for (const auto& o : w.allies) if (o.visible && o.alive && cheb(o.pos, a.pos) <= 4) ++nA;
                const int dA = cheb(a.pos, me.pos);
                if (nA > bestN || (nA == bestN && dA < bestD)) { bestN = nA; bestD = dA; jnaG = g; } }
        }
        if (jnaG >= 0) { if (!(M.kind == M_FARM && M.group == jnaG)) { setGoal(M_FARM, jnaG, -1, w); g_lastPickRound = w.round; } M.why = "join-ally"; }
        else if ((outnumbered || lowHpEn || lfr) && (!policy::FS2_MIS1_ON || (!me.atSpawn && M.kind != M_RECOVER)) /* FS2_MIS1: 싼 거름 먼저 */ && rtGate() && !me.atSpawn && M.kind != M_RECOVER) {   /* FB: 깎인 적 마무리가 기대 젬 + 면 후퇴 시작 안 함 */   /* CRV: 위험 없는 후퇴 빼기 */ if (M.kind == M_FARM && M.group >= 0) s_rtxG = M.group; const int rtsK = (int)M.kind, rtsG = M.group; setGoal(M_RECOVER, -1, -1, w); if (policy::RTS_LOG_ON) { int nA = 0, nE = 0; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, me.pos) <= 6) ++nA; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, me.pos) <= 8) ++nE;
                int nF = 0, nFh = 0; { const std::vector<std::uint32_t>& hsR = sortedAllyH(); for (const auto& a : w.allies) { if (!a.visible || !a.alive || cheb(a.pos, me.pos) > 6) continue; int k = -1; for (size_t q = 0; q < hsR.size(); ++q) if (hsR[q] == a.handle) k = (int)q;
                    if (k >= 0 && k < team::T.n && k != team::T.slot && rtsG >= 0 && team::T.mate[k].claimGroup == rtsG && team::T.mate[k].claimKind == team::CL_FARM) { ++nF; if (a.hp >= 0.5f * a.maxHp) ++nFh; } } }
                log::always("#rts %d %d out=%d low=%d lfr=%d hp=%.0f/%.0f nA=%d nE=%d k=%d g=%d nF=%d nFh=%d", w.round, team::T.slot, (int)outnumbered, (int)lowHpEn, (int)lfr, me.hp, me.maxHp, nA, nE, rtsK, rtsG, nF, nFh); }
            M.retreat = true; M.why = "retreat"; }   /* RTX */   /* RTK 되돌림 (래더 v167 4/15 · v170 21/50 vs v166 13/25) */
        else if (M.kind == M_RECOVER && M.retreat && w.round - M.lastEnemyRound >= 2 && me.hp >= me.maxHp * policy::RETREAT_RESUME_HP) {   /* RTS: 적이 2R 안 보이고 건강하면 */ setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; }
        if (policy::BH3_RC2_ON && M.kind == M_RECOVER && !M.retreat && enN > 0 && !me.atSpawn) {   /* BH3_RC2: 후퇴 블록과 같은 신호를 M.kind 와 상관없이 */
            bool lx = outnumbered || lowHpEn;
            if (!lx) { bool lf = policy::LFR_ON && combat::enemyThreatAt(w, me.pos) >= me.hp * policy::LFR_FRAC;
                const float kr = combat::fightKillRounds(w); if (kr >= 0.f) { const float th = combat::enemyThreatAt(w, me.pos); lf = th > 0.f && me.hp / th < kr; }
                if (lf && !(combat::fightOdds(w) < 0.5f)) lf = false; lx = lf; }
            rc2Pre = lx; }
    }
    if ((M.kind == M_FARM || M.kind == M_NONE) && !M.retreat && !me.atSpawn && me.hp >= 0.5f * me.maxHp && !VIS_NEAR(w) && w.round - M.lastHitRound > 2) {   /* CJN: 다툼 합류 */
        int cjG = -1, cjD = 16; float cjV = 0.f; const float cjDiv = goldDiv();   /* SITEL: 값 / (걸음 + 1) 가장 큰 무리 */
        for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
            if (!mt.enemySeen || w.round - mt.zoneRound > 3) continue; const int g = mt.claimGroup;
            if (g < 0 || g >= (int)S().groups.size() || g == M.group || !(mt.claimKind == team::CL_FARM || mt.claimKind == team::CL_CAPTURE)) continue;
            const Group& G = S().groups[(size_t)g]; if (!(G.state == 1 || G.state == 2)) continue;
            const int d = gfReady(g) ? gfAt(s_gf[(size_t)g], me.pos) : cheb(G.center, me.pos);
            if (policy::SITEL_ON) { if (d < 16) { const float v = stkVal(g, cjDiv) / (float)(d + 1); if (v > cjV) { cjV = v; cjG = g; cjD = d; } } }
            else if (d < cjD) { cjD = d; cjG = g; } }
        if (policy::SITEL_ON && cjG >= 0) { float cur = 0.f; if (M.kind == M_FARM) { const int sg = stepsToGoal(me.pos); cur = missionGems(w) / (float)std::max(1, (sg >= 0 && sg < 999 ? sg : 0) + missionWorkTurns(w)); } if (cjV <= cur) cjG = -1; }   /* SITEL: 지금 일보다 값이 클 때만 */
        if (cjG >= 0) { setGoal(M_FARM, cjG, -1, w); M.why = "cjn"; g_lastPickRound = w.round; }
    }
    // 1. 회복 · 상점 (임무를 덮는 긴급 조건)
    const int cxeS = (policy::CXE_ON && M.kind == M_FARM) ? cxeState(w) : 0;   /* CXE3 */
    bool tnkStay = false;   /* TNK: 어그로를 받아 줄 아군이 있어 캠프가 나를 안 치면 계속 침 (맞게 되어도 두 라운드는 버틸 체력일 때만) */
    if (policy::TNK_STAY_ON && M.kind == M_FARM && !me.atSpawn && !w.enemyNear8 && !VIS_NEAR(w) && (me.hp < me.maxHp * policy::RECOVER_HP || cxeS == 2)) { float hit = 0.f, inR = 0.f; combat::campAggroNow(w, M, hit, inR); tnkStay = hit <= 0.f && me.hp > 2.f * inR; }
    g_cxeBack = cxeS == 1 || tnkStay;
    const bool lowHp = (me.hp < me.maxHp * policy::RECOVER_HP || (policy::CXE_ON && M.kind == M_FARM && cxeS == 2)) && !tnkStay;   /* CXE · CXE3: 넘길 동료 없음 → 떠남 */
    if ((M.kind == M_SHOP || (M.kind == M_RECOVER && !M.retreat)) && !me.atSpawn && me.hp >= me.maxHp * policy::RECOVER_HP && fs2JtnNow()) {   /* FS2_MIS1 */   /* JNL: 가는 길에 곁 싸움에 끼면 이득이면 멈춤 */
        if (policy::BH3_RC2_ON && rc2Pre && M.kind == M_RECOVER && rtGate()) { M.retreat = true; M.why = "retreat"; }   /* BH3_RC2: 후퇴 시작 조건이 참이면 M_NONE 대신 후퇴 → 이후 JNR 이 다룸 */
        else { setGoal(M_NONE, -1, -1, w); M.why = "jnl"; } }
    if (me.atSpawn && me.hp < me.maxHp - 0.5f && M.kind != M_RECOVER && M.kind != M_SHOP && !me.channeling) { setGoal(M_RECOVER, -1, -1, w); M.why = "spf"; }   /* SPF: 스폰에서 덜 찬 채 나가지 않음 */
    if (M.kind == M_RECOVER && M.retreat && !me.atSpawn && me.hp >= policy::RETREAT_HP * me.maxHp && fs2ThrMe() <= 0.f && fs2JtnNow()) {   /* FS2_MIS1 */ setGoal(M_NONE, -1, -1, w); M.why = "jnr"; }   /* JNR: 곁 싸움 합류가 이득이면 후퇴 멈춤 (시작 조건과 같음) */
    if (policy::HRF_ON && M.kind == M_RECOVER && !M.retreat && !me.atSpawn && !VIS_NEAR(w)) { const int hg = hrfPick(w); if (hg >= 0) { setGoal(M_FARM, hg, -1, w); M.why = "hrf"; g_lastPickRound = w.round; } }   /* HRF: 회복 길에 끝낼 · 지원할 무리 */
    if (M.kind == M_RECOVER) {
        if (me.atSpawn ?me.hp >= me.maxHp - 0.5f : (!M.retreat && !disturbed(w) && me.hp >= me.maxHp * policy::RETREAT_RESUME_HP)) {   /* SPF: 스폰에서는 가득 찰 때까지 (R당 ~45% 회복) */ setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; }   /* LSH: 후퇴 복귀 기준과 같게 */   /* MINE4: 방해 없고 체력이 기준이면 집까지 안 감 */
        else { M.why = M.retreat ? "retreat" : "recover"; }
    } else if (lowHp && !me.atSpawn && !hldStay(w) && !finCanFinish(w) && !(policy::BH3_RC1_ON && policy::R_RETREAT && VIS_NEAR(w) && (combat::teamStayGain(w) > 0.f || combat::fbStay(w))) /* BH3_RC1: 후퇴 블록이 남기로 한 장면은 회복으로 덮지 않음 */ && !(M.kind == M_FARM && M.group >= 0 && S().groups[(size_t)M.group].state == 2 && !VIS_NEAR(w)) && !hrfKeep(w)) {   // bot (3)의 저체력 마무리 예외 유지   /* CBR: 막 끝낸 무리는 점령 먼저 */   /* HRF: 지금 무리를 죽지 않고 끝내거나 뒤에서 지원하면 남음 */
        const int hg = policy::HRF_ON ? hrfPick(w) : -1;   /* HRF: 다른 끝낼 · 지원할 무리 */
        if (hg >= 0) { setGoal(M_FARM, hg, -1, w); M.why = "hrf"; g_lastPickRound = w.round; }
        else { if (policy::RTS_LOG_ON && M.kind == M_FARM) { int nA = 0, nF = 0; const std::vector<std::uint32_t>& hsR = sortedAllyH(); for (const auto& a : w.allies) { if (!a.visible || !a.alive || cheb(a.pos, me.pos) > 6) continue; ++nA; int k = -1; for (size_t q = 0; q < hsR.size(); ++q) if (hsR[q] == a.handle) k = (int)q;
                    if (k >= 0 && k < team::T.n && k != team::T.slot && M.group >= 0 && team::T.mate[k].claimGroup == M.group && team::T.mate[k].claimKind == team::CL_FARM) ++nF; }
                int cR = 0; for (const auto& c : w.camps) if (c.str > 0.f && c.group == M.group) cR = std::max(cR, S().campRange(c.type));
                log::always("#lhp %d %d hp=%.0f/%.0f cxe=%d base=%d myR=%d campR=%d nA=%d nF=%d g=%d", w.round, team::T.slot, me.hp, me.maxHp, cxeS, (int)(me.hp < me.maxHp * policy::RECOVER_HP), me.range, cR, nA, nF, M.group); }
            setGoal(M_RECOVER, -1, -1, w); M.why = "recover"; }
    }
    if (policy::HRF_ON) { if (M.kind != M_FARM || M.group != g_hrfSupG) g_hrfSupG = -1;   /* HRF: 지원 무리에선 어그로 넘김 칸 (근거리 뒤) */
        else if (!VIS_NEAR(w) && hrfSupport(w, M.group)) g_cxeBack = true; }
    // 2. 바치기: 완료 (채널 끝) 는 젬 0 으로 안다
    if (M.kind == M_TRIBUTE) {
        if (me.gems == 0 || altarBlocked(M.altar, w.round)) { setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; }   /* RLP */   // LK: 막힌 제단이면 다시 고른다
    }
    if (policy::FCS_ON && fuelSpare(w) >= policy::FGR_MISSION) { static Pos s_fcsLast{-1, -1}; const Pos fp = M.kind == M_FARM ? focusCamp(w, M.group) : Pos{-1, -1}; if (!(fp == s_fcsLast)) { if (s_fcsLast.x >= 0 && fp.x >= 0) ++g_goalTag; s_fcsLast = fp; } }   /* FCS: 1순위가 바뀌면 목표 칸 다시 */
    // 2.5 (ES) 호위: 바치는 몸은 제단에 적이 보이면 도움 요청 · 미룸, 돕는 몸은 요청 제단으로
    if (policy::ES_ON) {
        if (g_esHoldSince >= 0 && w.round - g_esReqRound > 10) g_esHoldSince = -1;
        if (M.kind == M_TRIBUTE && M.altar >= 0 && (size_t)M.altar < S().altars.size() && !me.channeling) {
            const Pos A = S().altars[(size_t)M.altar]; bool en = false;
            for (const auto& e : w.enemies) if (cheb(e.pos, A) <= policy::ES_R) { en = true; break; }
            if (g_esCnt.size() != S().altars.size()) g_esCnt.assign(S().altars.size(), 0);   // ESX
            if (!en) g_esCnt[(size_t)M.altar] = 0;   // ESX: 제단이 비어 보이면 누적 0
            if (en && ++g_esCnt[(size_t)M.altar] > policy::ES_HOLD_MAX) { const int a = M.altar; g_esCnt[(size_t)a] = 0; blockAltar(a, w.round + 100); setGoal(M_NONE, -1, -1, w); M.why = "es-lost"; en = false; }   // ESX: 오래 막힌 제단은 잠시 포기
            if (en) {
                g_esReqAltar = M.altar; g_esReqRound = w.round; if (g_esHoldSince < 0) g_esHoldSince = w.round;
                const int fxU2 = io::submitted(true), fxT2 = io::submitted(false);
                if (w.round - g_esHoldSince <= policy::ES_HOLD_MAX && cheb(me.pos, A) > policy::ES_R && fxT2 < policy::FX_DANGER && fxU2 + team::teamGems(w, me.gems) < rules::WIN_GEMS) { /* FX: 위험 · 이기는 한 수면 미루지 않음 */ const int a = M.altar; blockAltar(a, w.round + 3); setGoal(M_NONE, -1, -1, w); M.why = "es-hold"; }
            }
        }
        if (M.kind == M_ESCORT) {
            bool req = false; for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k]; if (mt.helpAltar == M.altar && w.round - mt.helpRound <= team::LIFE_CLAIM) req = true; }
            if (M.altar >= 0 && (size_t)M.altar < S().altars.size()) { const Pos A = S().altars[(size_t)M.altar]; for (const auto& e : w.enemies) if (cheb(e.pos, A) <= policy::ES_R) { g_esSeen = w.round; break; } }
            static int s_esReqLast = -1000; if (req || w.round == M.since) s_esReqLast = w.round;   // ESX2: 마지막 요청 라운드
            if (w.round - s_esReqLast > 10 && M.altar >= 0) { /* ESX2: 요청이 10R 없으면 (적이 보여도) 끝 */ if (g_esBan.size() != S().altars.size()) g_esBan.assign(S().altars.size(), -1); g_esBan[(size_t)M.altar] = w.round + 100; setGoal(M_NONE, -1, -1, w); M.why = "es-noreq"; g_lastPickRound = -1000; }   // ESX: 호위는 최대 40R
            else if (me.hp < policy::ES_HP * me.maxHp || (!req && w.round - g_esSeen >= policy::ES_CLEAR)) { setGoal(M_NONE, -1, -1, w); M.why = "es-done"; g_lastPickRound = -1000; }
        } else if ((M.kind == M_NONE || M.kind == M_FARM) && me.hp >= policy::ES_HP * me.maxHp) {
            int bestA = -1, bestD = policy::ES_HELP_R + 1;
            for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k];
                if (mt.helpAltar < 0 || w.round - mt.helpRound > team::LIFE_CLAIM || (size_t)mt.helpAltar >= S().altars.size()) continue;
                if ((size_t)mt.helpAltar < g_esBan.size() && g_esBan[(size_t)mt.helpAltar] > w.round) continue;   // ESX
                const int dd = cheb(S().altars[(size_t)mt.helpAltar], me.pos); if (dd < bestD) { bestD = dd; bestA = mt.helpAltar; } }
            if (policy::SITEL_ON && bestA < 0) {   /* SITEL: 반경 밖 — 막힌 젬 값으로, 제단에서 가까운 need 몸만 */
                const size_t NA = S().altars.size(); float Va[16] = {}; int ra = -1; float rv = 0.f;
                for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k];
                    if (mt.helpAltar < 0 || w.round - mt.helpRound > team::LIFE_CLAIM || (size_t)mt.helpAltar >= NA || mt.helpAltar >= 16) continue;
                    if ((size_t)mt.helpAltar < g_esBan.size() && g_esBan[(size_t)mt.helpAltar] > w.round) continue;
                    Va[mt.helpAltar] += (w.round - mt.gemsRound <= team::LIFE_CLAIM) ? (float)mt.gemsRep : 0.f; }
                for (size_t a = 0; a < NA && a < 16; ++a) if (Va[a] > rv) { rv = Va[a]; ra = (int)a; }
                if (ra >= 0) { const Pos A = S().altars[(size_t)ra]; int nE = 0; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, A) <= policy::ES_R) ++nE;
                    const int need = std::max(1, nE) + 1, myD = cheb(A, me.pos); int closer = 0;   // 나보다 제단에 가까운 (요청 안 한 · 빠지지 않은) 팀원 수
                    for (int k = 0; k < team::T.n; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k]; if (!mt.known || mt.outReason > 0 || mt.zone < 0 || mt.zone >= 64) continue;
                        if (mt.helpAltar == ra && w.round - mt.helpRound <= team::LIFE_CLAIM) continue; const int dk = cheb(team::zoneCenter(mt.zone), A); if (dk < myD || (dk == myD && k < team::T.slot)) ++closer; }
                    if (closer < need) { float cur = 0.f; if (M.kind == M_FARM) { const int sg = stepsToGoal(me.pos); cur = missionGems(w) / (float)std::max(1, (sg >= 0 && sg < 999 ? sg : 0) + missionWorkTurns(w)); } if (rv / (float)(myD + 1) > cur) { bestA = ra; M.why = "site-l"; } } } }
            if (bestA >= 0) { const char* wy = M.why; setGoal(M_ESCORT, -1, bestA, w); g_esSeen = w.round; M.why = (policy::SITEL_ON && wy && std::strcmp(wy, "site-l") == 0) ? "site-l" : "escort"; }
        }
    }
    // 3. 점령: 그룹 상태가 2 (보상 대기) 가 아니게 되면 끝
    if (M.kind == M_CAPTURE && me.atSpawn) { setGoal(M_NONE, -1, -1, w); M.why = "cap-home"; g_lastPickRound = -1000; }   // CRG: 점령 중 리콜로 집에 왔으면 점령 끝
    if (M.kind == M_CAPTURE && M.group >= 0) {
        const Group& G = S().groups[(size_t)M.group];
        if (G.state != 2 && G.stateRound >= M.since) { setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; }   /* RLP */
        else if (policy::VCR_ON && G.gems < policy::BS_GEMS && vcrVesperFor(w, M.group)) { capAvoid(M.group, w.round); setGoal(M_NONE, -1, -1, w); M.why = "cap-vesper"; g_lastPickRound = -1000; }   /* VCR: Vesper 에게 */
        else if (policy::CO_ON && !(policy::VCR_ON && me.type == policy::VCR_TYPE) && G.gems < policy::BS_GEMS && io::submitted(true) < policy::EC_SUB && !onCapture(G, me.pos) && (policy::BH_F5_ON ? allyOnCaptureCO(w, G) : allyOnCapture(w, G)) && g_csjSince != M.since) { capAvoid(M.group, w.round); setGoal(M_NONE, -1, -1, w); M.why = "cap-occ"; g_lastPickRound = -1000; }   // CO (BS: 큰 그룹은 함께)
        static int s_c4G = -1, s_c4R = -1000;   /* CNC4: 점령 칸 위 두 라운드 연속 점령 불가 · 경쟁이면 내려놓음 */
        if (M.kind == M_CAPTURE && G.state == 2 && onCapture(G, me.pos)) {
            const int cs4 = io::groupCaptureState(G.handle);
            if (cs4 == 0 || cs4 == 2) {
                if (s_c4G == M.group && s_c4R == w.round - 1) { if (g_cnBad.size() != S().groups.size()) g_cnBad.assign(S().groups.size(), -1); g_cnBad[(size_t)M.group] = cs4; s_c4G = -1; setGoal(M_NONE, -1, -1, w); M.why = "cap-engine"; g_lastPickRound = -1000; }
                else { s_c4G = M.group; s_c4R = w.round; } }
            else s_c4G = -1; }
    }
    // 4. 농사: 완료 → 점령. 중단 조건
    if (M.kind == M_FARM && M.group >= 0) {
        const Group& G = S().groups[(size_t)M.group];
        if (G.state == 2) {
            bool lowerCaptures = false;
            if (policy::CO_ON && !(policy::VCR_ON && me.type == policy::VCR_TYPE) && G.gems < policy::BS_GEMS && io::submitted(true) < policy::EC_SUB && !onCapture(G, me.pos) && (policy::BH_F5_ON ? allyOnCaptureCO(w, G) : allyOnCapture(w, G))) lowerCaptures = true;   // CO (BS: 큰 그룹은 함께)
            if (policy::VCR_ON && G.gems < policy::BS_GEMS && vcrVesperFor(w, M.group)) lowerCaptures = true;   /* VCR: Vesper 에게 */
            if (lowerCaptures) { capAvoid(M.group, w.round); setGoal(M_NONE, -1, -1, w); M.why = "cap-taken"; g_lastPickRound = -1000; }
            else if (ucpNoCap(M.group, me.pos)) { capAvoid(M.group, w.round); setGoal(M_NONE, -1, -1, w); M.why = "cap-unreach"; g_lastPickRound = -1000; }   /* UCP */
            else setGoal(M_CAPTURE, M.group, -1, w);
        }
        else if (G.state == 3) {
            const int left = std::max(0, G.respawnUntil - w.round);
            const int rhwHs = homeSteps(G.center);   /* RHW */
            const bool rsw2Hold = policy::RSW2_ON && g_rsw2G == M.group && g_rsw2Since == M.since;   /* RSW2b: 함께 모이러 온 무리는 집 · 다른 무리로 끌려가지 않음 */
            if (!rsw2Hold && !me.atSpawn && !w.enemyNear8 && rhwHs >= 0 && (policy::BH5_CS7_ON ? left >= (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1 + (int)std::ceil((float)rhwHs / wkdSpeed(w)) : left >= (policy::BH5_IT5_ON ? io::recallLen() : rules::RECALL_TICKS) + 1 + rhwHs) &&   /* BH5_CS7 (BH5_IT5 리콜 길이 위에) */ (me.hp < me.maxHp - 0.5f || me.gold >= rules::SHARD_COST) && !combat::joinTargetNow(w)) {   /* JNL */ setGoal(M_RECOVER, -1, -1, w); M.why = "rhw"; } else {   /* RHW: 기다림 안에 리콜 5+1 + 집→무리 걸음이 들어가면 집에서 회복 · 상점 */
            int altD = 1 << 20; for (int gi2 = 0; gi2 < (int)S().groups.size(); ++gi2) { if (gi2 == M.group) continue; const Group& H = S().groups[(size_t)gi2]; if (H.state == 3 || H.state == 2 || (H.gems <= 0 && H.gold <= 0)) continue; altD = std::min(altD, cheb(H.center, me.pos)); }   // RSW
            static int s_rkG = -1, s_rkUntil = -1, s_rkSince = -1;   /* RSW2: 고르기 점수로 다시 물어 1등이면 기다림 */
            bool rswDrop = !rsw2Hold && (policy::BH5_CS7_ON ? (float)left > (float)altD / wkdSpeed(w) : left > altD);   /* BH5_CS7 */   /* MINE4: 기다림 > 다른 무리 걸음이면 (문턱 없이) */
            if (!rsw2Hold && (policy::BH5_CS7_ON ? (float)left > (float)altD / wkdSpeed(w) : left > altD)) {   /* BH5_CS7 */
                if (s_rkG == M.group && s_rkSince == M.since && w.round < s_rkUntil) rswDrop = false;
                else if (io::fuel() >= 45000) { int st = 0; const int g = pickFarm(w, -1, st);
                    if (g == PICK_DEFERRED) rswDrop = false;
                    else if (g == M.group || g < 0) { rswDrop = false; s_rkG = M.group; s_rkSince = M.since; s_rkUntil = w.round + 8; }
                    else { setGoal(M_FARM, g, -1, w); M.why = "respawn-switch"; g_lastPickRound = w.round; rswDrop = false; } }   /* 1등 그룹으로 바로 */
                else rswDrop = false;   /* 연료가 모자라면 다음 턴에 묻는다 */
            }
            if (rswDrop) { setGoal(M_NONE, -1, -1, w); M.why = "respawn-long"; g_lastPickRound = -1000; }   /* RSW */
            else M.why = "wait-respawn";
            }   /* RHW close */
        } else if (M.lethalStreak >= policy::LETHAL_STREAK_ABANDON) { setGoal(M_NONE, -1, -1, w); M.why = "lethal-all"; g_lastPickRound = -1000; }
    }
    // 5. 바치기 시작 조건 (농사 중이거나 임무 없음). 제단은 cheb 로 셋을 고르고 걸음 (내 칸 거리장) 으로 하나를 정한다 — corners 에서 cheb 로 고른 제단에 50R 걸림
    int fxUs = 0, fxTh = 0; if (me.gems > 0) { fxUs = io::submitted(true); fxTh = io::submitted(false); }   // FX: 막판 융통성
    const int fxTeam = me.gems > 0 ? ((reference::PRISM_MACRO && me.type==reference::PRISM) ? special::teamCarried(w) : team::teamGems(w, me.gems)) : 0, fxLead = std::max(fxUs, fxTh);   /* TWP 규칙 3 */
    const bool referenceBank = !reference::PRISM_MACRO || reference::prismCanBank(me.type,fxUs,fxTeam,w.round,policy::TRIBUTE_FINAL_ROUND);   /* TWP 규칙 3: Prism 은 팀 젬 + 바친 젬 >= 1000 또는 마지막 R 전엔 안 바침 */
    if(!referenceBank && M.kind==M_TRIBUTE){setGoal(M_NONE,-1,-1,w);M.why="prism-hold";g_lastPickRound=-1000;}
    const bool fxWin = me.gems > 0 && fxUs + fxTeam >= rules::WIN_GEMS && M.kind != M_TRIBUTE;
    const bool fxDanger = me.gems > 0 && fxTh >= policy::FX_DANGER;
    const bool fxLate = me.gems > 0 && fxLead >= policy::FX_LATE;
    const float kbP = (s_kbDeaths + policy::KB_PRIOR_DEATH) / (s_kbAlive + policy::KB_PRIOR_R) * ((me.type == 3 || me.type == 4) ? policy::KB_LINK : 1.f);   // KB: 죽을 확률 (짝은 둘이 잃음)
    const float kbF = (s_kbEarned + policy::KB_PRIOR_RATE * policy::KB_PRIOR_R) / (s_kbAlive + policy::KB_PRIOR_R);   // KB: 벌이 / R
    const float kbG = kbF / std::max(1e-4f, kbP);   // KB: 들고 다녀도 되는 젬
    const float fxTg = kbG * (fxLate ? std::max(0.2f, (float)(rules::WIN_GEMS - fxLead) / (float)rules::WIN_GEMS) : 1.f);
    const bool es2Go = me.gems > 0 && ((float)((policy::CPX_ON && me.type != reference::PRISM) ? cpxTeamGems(w, me.gems) : fxTeam) >= fxTg || fxWin || fxDanger);   /* CPX */   // ES2: 팀 젬이 기준 이상이면 함께 간다 (FX: 막판엔 기준을 줄이고 이기는 한 수 · 위험이면 바로)
    float trqThr = -1.f;   /* TQT */
    float trqG = -1.f;   /* TRQ: 경제적 묶음 g* = i √(trip / p), 상한 i / (2p) — 바치기 · 욕심쟁이 미루기 같은 기준 */
    if (policy::TRQ_ON && me.gems > 0) { int st = -1;
        for (int a = 0; a < (int)S().altars.size(); ++a) { if (altarBlocked(a, w.round)) continue; const int s = awSteps(a, me.pos); if (s >= 0 && s < 99999 && (st < 0 || s < st)) st = s; }
        if (st < 0) for (int a = 0; a < (int)S().altars.size(); ++a) { if (altarBlocked(a, w.round)) continue; const int c = cheb(S().altars[(size_t)a], me.pos); if (st < 0 || c < st) st = c; }
        if (st >= 0) { const int sub = std::max(S().subMin, S().subBase + (int)std::ceil((double)me.gems * S().subMult));   /* 룰북: 바치기 채널 */
            const float trip = 2.f * (float)st + (float)sub, pp = std::max(1e-5f, kbP);
            if (policy::KTRQ_ON) { const float iK = (s_kbEarned + policy::KTRQ_PRIOR_RATE * policy::KB_PRIOR_R) / (s_kbAlive + policy::KB_PRIOR_R), ppC = policy::KTRC_ON ? std::max(1e-5f, policy::KTRC_Q * (s_kbContact + policy::KTRC_PRIOR_C * policy::KTRC_R0) / (s_kbAlive + policy::KTRC_R0) * ((me.type == 3 || me.type == 4) ? policy::KB_LINK : 1.f)) : pp, pK = ppC * policy::KTRQ_LOSS;   /* KTRC */ trqG = std::min(iK / (2.f * pK), iK * std::sqrt(trip / pK)); }   /* KTRQ: 실측 벌이 · 처치 가방 이전 */
            else trqG = std::min(kbF / (2.f * pp), kbF * std::sqrt(trip / pp));
            if (policy::TQT_ON) { const float pt = std::max(pp, policy::TQT_P); trqThr = std::min(kbF / (2.f * pt), kbF * std::sqrt(trip / pt)); } } }   /* TQT: 위협 중 문턱 */
    bool grHold = false;   // GR: 욕심쟁이 — 가까운 잡을 캠프가 있으면 바치기를 미룬다
    if (fuelSpare(w) >= policy::FGR_MISSION && policy::GR_ON && (policy::HLP_ON ? !(fxWin || fxDanger) : !es2Go) && !fxLate && me.gems > 0 && (float)me.gems < (trqG >= 0.f ? trqG : kbG) && me.hp >= policy::GR_HP * me.maxHp && w.round < policy::TRIBUTE_FINAL_ROUND && (M.kind == M_FARM || M.kind == M_NONE)) {
        bool thr = false; for (const auto& e : w.enemies) if (threatening(w, e)) { thr = true; break; }
        if (!thr) {
            if (M.kind == M_FARM && M.group >= 0 && !(policy::TFP_ON && tfpHold(w, M.group))) { const Group& G = S().groups[(size_t)M.group]; const int sg = stepsToGoal(me.pos); if ((G.state == 0 || G.state == 1) && sg >= 0 && sg <= policy::GR_STEPS) grHold = true; }   /* TFPW */
            else if (M.kind == M_NONE) for (int gi = 0; gi < (int)S().groups.size() && !grHold; ++gi) {
                if (io::fuel() < zres() + 15000 || io::round() != w.round) break;
                const Group& G = S().groups[(size_t)gi]; if (cheb(G.center, me.pos) > policy::GR_NEAR || G.state == 2) continue;
                int wl = 0, oth = 0; if (farmable(w, gi, wl, oth) > 0 && wl <= 2) grHold = true;
            }
            if (policy::HLP_ON && (M.kind == M_NONE || M.kind == M_FARM)) {   /* HLP2: 내가 합류해 끝내는 시간이 크게 줄 때만 돕기 */
                int hg = -1; float hNet = 0.f;
                for (int gi = 0; gi < (int)S().groups.size(); ++gi) { const Group& G = S().groups[(size_t)gi]; if (cheb(G.center, me.pos) > policy::HLP_R) continue;   /* HLR: 먼 무리를 먼저 거름 (같은 조건, 순서만) */ if (!(G.state == 0 || G.state == 1) || (G.gems <= 0 && G.gold <= 0)) continue;
                    if ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) continue;
                    const int kc = team::claimsOn(gi, (1 << team::CL_FARM) | (1 << team::CL_CAPTURE)); if (kc < 1) continue;
                    const int dd = cheb(G.center, me.pos); if (dd > policy::HLP_R) continue;
                    const float fD = pnlDpsFactor(w, G), mine = ksDps(me.type, me.str, me.focus) * fD, dK1 = mgTeamDps(w, kc + 1) * fD, dK = std::max(1.f, dK1 - mine);
                    const float hL = std::max(1.f, groupHpLeft(gi)), T0 = hL / dK, rem = hL - dK * (float)dd; if (rem <= 0.f) continue;   /* 닿기 전에 끝남 */
                    const float T1 = (float)dd + rem / std::max(1.f, dK1), net = (T0 - T1) - (float)dd;   /* 끝나는 시간이 줄어드는 양 − 내가 걷는 걸음 */
                    if (net > hNet) { hNet = net; hg = gi; } }
                if (hg >= 0) { grHold = true; if (M.kind == M_NONE || (M.kind == M_FARM && M.group != hg && stepsToGoal(me.pos) > policy::GR_STEPS)) { setGoal(M_FARM, hg, -1, w); M.why = "help"; } }
            }
        }
    }
    if (w.round >= 4 && !s_amsDone) amsInit();   /* AMS: precompute */
    if (M.kind == M_TRIBUTE && M.altar >= 0 && altarSealed(M.altar)) { setGoal(M_NONE, -1, -1, w); M.why = "altar-sealed"; g_lastPickRound = -1000; }   /* AMS */
    bool tsfGo = false;   /* TSF: 제단 곁이면 바침 */
    if (policy::TSF_ON && me.gems >= policy::TSF_MIN && !VIS_NEAR(w)) { for (int a = 0; a < (int)S().altars.size(); ++a) { if (altarBlocked(a, w.round)) continue; const int s = awSteps(a, me.pos); if (s >= 0 && s <= policy::TSF_STEPS) { tsfGo = true; break; } } }
    const bool tqtGo = policy::TQT_ON && me.gems > 0 && trqThr >= 0.f && (float)me.gems >= trqThr && VIS_NEAR(w);   /* TQT: 위협 중에도 문턱 넘으면 바치러 */
    const bool tacHold = policy::TAC_ON && me.gems > 0 && !fxWin && !fxDanger && (combat::allyCrisis(w, std::max(S().subMin, S().subBase + (int)std::ceil((double)me.gems * S().subMult))) || combat::joinTargetNow(w));   /* TAC: 곁 아군 위기면 바치러 가지 않음 */
    if (tacHold && M.kind == M_TRIBUTE && !me.channeling) { setGoal(M_NONE, -1, -1, w); M.why = "tac-hold"; g_lastPickRound = -1000; }
    if (referenceBank && (((M.kind == M_NONE || M.kind == M_FARM || (fxWin && M.kind == M_CAPTURE)) && me.gems > 0 && (!VIS_NEAR(w) || fxWin || tqtGo) && (!grHold || tsfGo) && !tacHold) || (fxWin && M.kind != M_TRIBUTE))) {   /* TWP 규칙 3 */   /* TSF */   // FX: 이기는 한 수는 적 · 점령 무시
        int bestA = -1, bestD = 9999;
        for (int a = 0; a < (int)S().altars.size(); ++a) { if (altarBlocked(a, w.round)) continue; const int d = cheb(S().altars[(size_t)a], me.pos); if (d < bestD) { bestD = d; bestA = a; } }
        const bool want = es2Go || tqtGo || tsfGo || (trqG >= 0.f ? (float)me.gems >= trqG : (float)me.gems >= kbG) || w.round >= policy::TRIBUTE_FINAL_ROUND;   /* TQT */   // ES2: 개인 문턱 대신 팀 젬
        if (want && bestA >= 0) {
            const int awA = awPick(w);
            if (awA == PICK_DEFERRED) M.why = "altar-deferred";
            else if (awA >= 0) setGoal(M_TRIBUTE, -1, awA, w);
        }
    }
    static int s_rcpSince = -1; static long long s_rcpCost = 0;   /* RCP2: 집에서 제단 다시 고르기 (awPick 은 캠프 칸이 열리면 내 거리장 + 제단 칸 전부, 잰 값 4~5만) — 이번 머묾에 한 번 골랐고 연료가 잰 비용 + 싸움 몫보다 적으면 건너뜀 */
    if (policy::RC_HOME_LOOP && M.kind == M_TRIBUTE && me.atSpawn && me.gems > 0 && !(s_rcpSince == M.since && io::fuel() < s_rcpCost + zres())) {   // RC: 리콜로 집에 왔으면 제단을 다시 고른다
        int bestA = -1, bestD = 9999;
        for (int a = 0; a < (int)S().altars.size(); ++a) { if (altarBlocked(a, w.round)) continue; const int d = cheb(S().altars[(size_t)a], me.pos); if (d < bestD) { bestD = d; bestA = a; } }
        { const long long qr = io::fuel(); const int awA = awPick(w); s_rcpCost = std::max(s_rcpCost * 7 / 8, qr - io::fuel()); bestA = awA; if (awA == PICK_DEFERRED) M.why = "altar-deferred"; }   /* RCP2: 잰 비용 */
        if (bestA >= 0 && bestA != M.altar) setGoal(M_TRIBUTE, -1, bestA, w);
        if (bestA != PICK_DEFERRED) s_rcpSince = M.since;   /* RCP2 */
    }
    if (fuelSpare(w) >= policy::FGR_MISSION && policy::PK_ON && !M.retreat && (M.kind == M_NONE || M.kind == M_SHOP || (M.kind == M_FARM && (stepsToGoal(me.pos) > 0 || (M.group >= 0 && S().groups[(size_t)M.group].state == 3)) && M.lastHitRound < w.round - 2)) && me.hp >= me.maxHp * policy::RECOVER_HP) {   // PK: 바닥 보상 줍기   /* MINE4: 리스폰 대기 중에도 */
        int bestG = -1, bestD = policy::PK_MAX_STEPS + 1;
        for (int gi = 0; gi < (int)S().groups.size(); ++gi) {
            const Group& G = S().groups[(size_t)gi];
            if (G.state != 2 || G.stateRound != w.round || G.spawn.empty()) continue;
            if (M.kind == M_FARM && gi == M.group) continue;
            if (allyOnCapture(w, G) || team::claimsOn(gi, 1 << team::CL_CAPTURE) > 0) continue;
            if ((size_t)gi < g_avoidUntil.size() && g_avoidUntil[(size_t)gi] > w.round) continue;
            if (cnBlocked(gi)) continue;   /* CNC4 */
            int myC = 99; for (const auto& sp : G.spawn) myC = std::min(myC, cheb(sp, me.pos));
            int enC = 99; for (const auto& e : w.enemies) for (const auto& sp : G.spawn) enC = std::min(enC, cheb(sp, e.pos));
            if ((enC <= 2 || enC <= myC) && (float)G.gems + (float)G.gold / gvtPick() < 40.f) continue;   // RD7: 큰 보상은 적이 곁이어도 꺼리지 않는다
            int dd = myC; if (gfReady(gi)) { const int v = gfAt(s_gf[(size_t)gi], me.pos); if (v >= 65535) continue; /* UCP: 못 가는 보상은 안 주움 */ dd = v + 1; }
            if (dd < bestD) { bestD = dd; bestG = gi; }
        }
        if (bestG >= 0) { setGoal(M_CAPTURE, bestG, -1, w); M.why = "pickup"; }
    }
    if (policy::VYO_ON && policy::CPY_ON && policy::VCR_ON && !M.retreat && (M.kind == M_NONE || M.kind == M_FARM) && me.hp >= me.maxHp * policy::RECOVER_HP && !VIS_NEAR(w) && !me.channeling) {   /* VYO: 곁 보상 대기 무리를 덤 작은 아군이 지키고 있으면 내가 받으러 감 */
        const float myB0 = 0.f; (void)myB0;
        for (int gi = 0; gi < (int)S().groups.size(); ++gi) { const Group& G = S().groups[(size_t)gi];
            if (G.state != 2 || G.spawn.empty() || G.gems >= policy::BS_GEMS || cheb(G.center, me.pos) > policy::VCR_R || onCapture(G, me.pos)) continue;
            if (M.kind == M_FARM && gi == M.group) continue;
            const float mine = cpyBonus(w.me.type, G); if (mine <= 0.f) continue;
            bool lower = false; for (const auto& a : w.allies) if (a.alive && a.visible && onCapture(G, a.pos) && cpyBonus(a.type, G) < mine) { lower = true; break; }
            if (!lower) continue;
            if (ucpNoCap(gi, me.pos)) continue;
            setGoal(M_CAPTURE, gi, -1, w); M.why = "vyo"; g_csjSince = M.since; break; }
    }
    if (fuelSpare(w) >= policy::FGR_MISSION && !M.retreat && (M.kind == M_NONE || M.kind == M_FARM) && me.hp >= me.maxHp * policy::RECOVER_HP && !VIS_NEAR(w)) {   /* CSJ3 */
        auto chanT = [](float x) { return std::max((float)S().subMin, (float)S().subBase + std::ceil(x * (float)S().subMult)); };
        const float hz = (s_kbDeaths + policy::KB_PRIOR_DEATH) / (s_kbAlive + policy::KB_PRIOR_R), rho = rshRho();
        for (int gi = 0; gi < (int)S().groups.size(); ++gi) { const Group& G = S().groups[(size_t)gi];
            if (G.state != 2 || G.stateRound != w.round || G.gems <= 0 || G.spawn.empty() || onCapture(G, me.pos)) continue;
            if (io::groupCaptureState(G.handle) != 1) continue;
            const int tLeft = io::groupCaptureTimer(G.handle); if (tLeft <= 0) continue;
            int k = 0; for (const auto& a : w.allies) if (a.alive && onCapture(G, a.pos)) ++k; if (k <= 0) continue;
            int steps = 99; for (const auto& sp : G.spawn) steps = std::min(steps, cheb(sp, me.pos));
            if (gfReady(gi)) { const int v = gfAt(s_gf[(size_t)gi], me.pos); if (v < 65535) steps = std::max(steps, v + 1); }
            if (steps > tLeft) continue;
            const float S0 = (float)G.gems, a1 = S0 / (float)k, a2 = S0 / (float)(k + 1);
            Pos capT = G.spawn[0]; { int bc = 99; for (const auto& sp : G.spawn) { const int c = cheb(sp, me.pos); if (c < bc) { bc = c; capT = sp; } } }
            float detour = 0.f;
            if (M.kind == M_FARM && M.group >= 0 && gfReady(M.group)) { const int dCG = gfAt(s_gf[(size_t)M.group], capT); int best = 99999;
                for (int a = 0; a < (int)S().altars.size(); ++a) { const int dA = awSteps(a, capT); if (dA < 0 || dA >= 99999) continue; int dAG = 99999; for (Pos q : altarTilesOf(a)) dAG = std::min(dAG, gfAt(s_gf[(size_t)M.group], q)); if (dAG >= 65535) continue; best = std::min(best, dA + dAG); }
                if (best < 99999 && dCG < 65535) detour = std::max(0.f, (float)(best - dCG)); }
            else { int best = 99999; for (int a = 0; a < (int)S().altars.size(); ++a) { const int dA = awSteps(a, capT); if (dA >= 0 && dA < best) best = dA; } if (best < 99999) detour = (float)best; }
            const float focusGain=(reference::PRISM_MACRO&&me.type==reference::PRISM)?a2*(mgmK(w,gvtPick())-1.f):0.f;   /* TWP 규칙 4 */
            const float gain = focusGain + rho * (float)k * (chanT(a1) - chanT(a2)) + hz * ((float)k * chanT(a1) * a1 - (float)(k + 1) * chanT(a2) * a2);
            const float cost = ((float)steps + (float)tLeft + chanT(a2) + detour) * rho;
            if (gain > cost) { setGoal(M_CAPTURE, gi, -1, w); M.why = "csj3"; g_csjSince = M.since; break; } }
    }
    if (policy::CCD_ON && M.kind == M_CAPTURE && M.group >= 0 && combat::g_ccdLeave == w.round - 1 && !me.atSpawn) {   /* CCD: 끝내기 전에 죽는 점령 → 내려놓음 (곧 다시 고르지 않게) */
        capAvoid(M.group, w.round); g_lastPickRound = -1000;
        if (me.hp < me.maxHp * policy::RETREAT_RESUME_HP) { setGoal(M_RECOVER, -1, -1, w); M.why = "ccd-leave"; } else { setGoal(M_NONE, -1, -1, w); M.why = "ccd-leave"; } }
    if ((M.kind == M_FARM || M.kind == M_CAPTURE) && combat::g_noEnter >= policy::NE_TURNS && !me.atSpawn) { combat::g_noEnter = 0; setGoal(M_RECOVER, -1, -1, w); M.why = "no-enter"; }   // NE
    if (!tbaHold() && policy::TFP_ON && !(policy::RSW2_ON && M.kind == M_FARM && g_rsw2G == M.group && g_rsw2Since == M.since && S().groups[(size_t)M.group].state == 3) && g_tfpMember && g_tfpTarget >= 0 && (M.kind == M_NONE || (M.kind == M_FARM && M.group != g_tfpTarget && M.group >= 0 && (S().groups[(size_t)M.group].state == 2 || S().groups[(size_t)M.group].state == 3)))   /* TZD: 하던 일을 마친 멤버만 */ && !me.atSpawn) { setGoal(M_FARM, g_tfpTarget, -1, w); M.why = "tfp"; g_lastPickRound = w.round; }   /* TFP2: 멤버는 목표로 */
    if (!tbaHold() && policy::INV_ON && !(policy::RSW2_ON && M.kind == M_FARM && g_rsw2G == M.group && g_rsw2Since == M.since && S().groups[(size_t)M.group].state == 3) && M.kind == M_FARM && M.group >= 0 && !w.enemies.empty() && w.round - M.lastHitRound > 2 && g_lastPickRound != w.round && fuelSpare(w) >= policy::ZO_PICK_FUEL) {   /* INV: 우리 집 큰 무리에 혼자 든 적을 새로 보면 다시 고름 (점수식 그대로) */
        static int s_invG = -1, s_invR = -1000; int ig = -1;
        if (policy::FS2_MIS3_ON) {   /* FS2_MIS3: 무리 중심 x 순 색인 (판에 한 번) → 산 적 곁 무리만, 번호 순으로 같은 거름 */
            static std::vector<int> s_byX; static std::vector<int> s_xs; static size_t s_byXN = (size_t)-1; static int s_rMax = 0;
            const size_t NG = S().groups.size();
            if (s_byXN != NG) { s_byXN = NG; s_byX.resize(NG); s_rMax = 0; for (size_t q = 0; q < NG; ++q) { s_byX[q] = (int)q; s_rMax = std::max(s_rMax, S().groups[q].rangeMax); }
                std::sort(s_byX.begin(), s_byX.end(), [](int a, int b) { const int xa = S().groups[(size_t)a].center.x, xb = S().groups[(size_t)b].center.x; return xa != xb ? xa < xb : a < b; });
                s_xs.resize(NG); for (size_t q = 0; q < NG; ++q) s_xs[q] = S().groups[(size_t)s_byX[q]].center.x; }
            static std::vector<int> s_cand; s_cand.clear();
            for (const auto& e : w.enemies) { if (!e.alive) continue;
                for (size_t q = (size_t)(std::lower_bound(s_xs.begin(), s_xs.end(), e.pos.x - s_rMax - 1) - s_xs.begin()); q < NG && s_xs[q] <= e.pos.x + s_rMax + 1; ++q) {
                    const int g = s_byX[q]; if (g == M.group) continue; const Group& Gi = S().groups[(size_t)g];
                    if (cheb(e.pos, Gi.center) <= Gi.rangeMax + 1) s_cand.push_back(g); } }
            std::sort(s_cand.begin(), s_cand.end()); s_cand.erase(std::unique(s_cand.begin(), s_cand.end()), s_cand.end());
            for (int g : s_cand) if (invStake(w, g) > 0.f) { ig = g; break; } }
        else
        for (size_t g = 0; g < S().groups.size() && ig < 0; ++g) { if ((int)g == M.group) continue; bool nearE = false; const Group& Gi = S().groups[g];
            for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, Gi.center) <= Gi.rangeMax + 1) { nearE = true; break; }
            if (nearE && invStake(w, (int)g) > 0.f) ig = (int)g; }
        if (ig >= 0 && (ig != s_invG || w.round - s_invR > policy::PICK_RETRY_ROUNDS)) { s_invG = ig; s_invR = w.round; int st = 0; const int ng = pickFarm(w, -1, st);
            if (ng >= 0 && ng != PICK_DEFERRED && ng != M.group) { setGoal(M_FARM, ng, -1, w); M.why = "inv"; g_lastPickRound = w.round; } } }
    if (policy::LVG_ON) {   /* LVG: 팀원이 치던 큰 무리 담당을 놓음 → 치는 중이 아닌 나는 다시 고름 */
        static int s_lvPrev[team::MAX]; static bool s_lvInit = false; if (!s_lvInit) { s_lvInit = true; for (int k = 0; k < team::MAX; ++k) s_lvPrev[k] = -1; }
        int lvG = -1;
        for (int k = 0; k < team::T.n && k < team::MAX; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
            const int g = (mt.known && mt.claimKind == team::CL_FARM && mt.claimGroup >= 0 && (size_t)mt.claimGroup < S().groups.size()) ? mt.claimGroup : -1;
            const int pg = s_lvPrev[k]; s_lvPrev[k] = g;
            if (pg < 0 || pg == g || lvG >= 0) continue;
            const Group& Gp = S().groups[(size_t)pg]; if (Gp.state != 1 || team::claimsOn(pg, 1 << team::CL_FARM) > 0) continue;
            if (mt.enemySeen) continue;   /* LVG2: 적을 보고 놓은 무리 (후퇴) 는 아님 — 캠프 피해로 놓은 것만 */
            { bool enG = false; for (const auto& em : enMem(w)) if (cheb(em.first.pos, Gp.center) <= policy::RETREAT_ENEMY_R + Gp.rangeMax) { enG = true; break; } if (enG) continue; }   /* LVG2: RTX 와 같은 거름 */
            const float dv = gvtPick(); float mx = 0.f; for (size_t q = 0; q < S().groups.size(); ++q) mx = std::max(mx, stkVal((int)q, dv));
            if (mx > 0.f && stkVal(pg, dv) >= 0.5f * mx) lvG = pg; }
        if (lvG >= 0 && !tbaHold() && !(policy::RSW2_ON && M.kind == M_FARM && g_rsw2G == M.group && g_rsw2Since == M.since && S().groups[(size_t)M.group].state == 3) && (M.kind == M_NONE || (M.kind == M_FARM && M.group != lvG && w.round - M.lastHitRound > 2)) && !me.atSpawn && g_lastPickRound != w.round && fuelSpare(w) >= policy::ZO_PICK_FUEL) {
            int st = 0; const int ng = pickFarm(w, -1, st);
            if (ng == lvG) { setGoal(M_FARM, ng, -1, w); M.why = "lvg"; g_lastPickRound = w.round; } } }
    if (policy::FJN_ON) {   /* FJN: 팀원 담당이 새 무리로 바뀌면, 아직 치지 않고 걷는 나는 이번 턴에 다시 고른다 (점수식 그대로 · 기다림 없음) */
        static int s_fjPrev[team::MAX]; static bool s_fjInit = false; if (!s_fjInit) { s_fjInit = true; for (int k = 0; k < team::MAX; ++k) s_fjPrev[k] = -1; }
        int fjG = -1;
        for (int k = 0; k < team::T.n && k < team::MAX; ++k) { if (k == team::T.slot) continue; const team::Mate& mt = team::T.mate[k];
            const int g = (mt.known && mt.claimKind == team::CL_FARM && mt.claimGroup >= 0 && (size_t)mt.claimGroup < S().groups.size()) ? mt.claimGroup : -1;
            if (g != s_fjPrev[k]) { s_fjPrev[k] = g; if (g >= 0 && fjG < 0) { const Group& Gj = S().groups[(size_t)g]; if (Gj.state == 0 || Gj.state == 1) fjG = g; } } }
        if (fjG >= 0 && !tbaHold() && !(policy::RSW2_ON && M.kind == M_FARM && g_rsw2G == M.group && g_rsw2Since == M.since && S().groups[(size_t)M.group].state == 3) && M.kind == M_FARM && M.group >= 0 && M.group != fjG && w.round - M.lastHitRound > 2 && g_lastPickRound != w.round && fuelSpare(w) >= policy::ZO_PICK_FUEL) {
            const float div = gvtPick(); const Group& Gj = S().groups[(size_t)fjG]; const Group& Gm = S().groups[(size_t)M.group];
            const float vJ = (float)Gj.gems + (float)Gj.gold / div, vM = (float)Gm.gems + (float)Gm.gold / div;
            const bool go = policy::FJN_MODE == 3 || (policy::FJN_MODE == 2 && vJ >= 40.f) || (policy::FJN_MODE == 1 && vJ > vM);
            if (go) { int st = 0; const int ng = pickFarm(w, -1, st);
                if (ng >= 0 && ng != PICK_DEFERRED && ng != M.group) { setGoal(M_FARM, ng, -1, w); M.why = "fjn"; g_lastPickRound = w.round; if (log::ON) NB_LINE("#fjn %d %d from=%d to=%d trig=%d", w.round, S().myIndex, Gm.handle ? (int)(&Gm - S().groups.data()) : -1, ng, fjG); } } }
    }
    if (!tbaHold() && policy::RSW2_ON && !me.atSpawn && (M.kind == M_NONE || M.kind == M_FARM) && fuelSpare(w) >= policy::ZO_PICK_FUEL) {   /* RSW2: 큰 무리 리스폰에 충분히 모일 때만 함께 감 — 모든 몸이 같은 팀 보기 (planView 구역) 로 같은 셈 */
        static int s_r2 = -1000;
        const bool busyBig = M.kind == M_FARM && M.group >= 0 && M.lastHitRound >= M.since && S().groups[(size_t)M.group].campHandle.size() >= 5;
        if (!busyBig && w.round - s_r2 >= 2) { s_r2 = w.round;
            const float spd = wkdSpeed(w); int bestG = -1; float bestV = 0.f;
            for (int g = 0; g < (int)S().groups.size(); ++g) { const Group& Gr = S().groups[(size_t)g];
                if (Gr.state != 3 || Gr.campHandle.size() < 5 || groupUnreachable(g) || g == M.group) continue;
                { bool con = cheb(Gr.center, S().enemyCenter) <= cheb(Gr.center, S().allyCenter);   /* RSW2e: 다투는 무리만 (적 스폰 쪽 · 최근 적) */
                  if (!con) for (const auto& em : enMem(w)) if (cheb(em.first.pos, Gr.center) <= policy::RETREAT_ENEMY_R + Gr.rangeMax) { con = true; break; }
                  if (!con) continue; }
                const int left = Gr.respawnUntil - w.round; if (left < 0) continue;
                const int myE = (int)((float)(gfReady(g) ? std::min(gfAt(s_gf[(size_t)g], me.pos), 9999) : cheb(me.pos, Gr.center)) / spd);
                if (myE > left + policy::OSY_WAIT || myE < left - policy::OSY_WAIT) continue;   // 지금 떠나야 리스폰에 맞음 (너무 이르면 하던 일 계속)
                int K = 0;   // 리스폰 시각 + 기다림 안에 닿는 팀 몸 (나 포함, 같은 팀 보기)
                for (int k = 0; k < team::T.n; ++k) { const auto& mt = team::planView(k, w); if (!mt.known || mt.zone < 0 || mt.outReason > 0) continue;
                    const Pos zp = team::zoneCenter(mt.zone); const int e = gfReady(g) ? std::min(gfAt(s_gf[(size_t)g], zp), 9999) : cheb(zp, Gr.center);
                    if ((float)e / spd <= (float)(left + policy::OSY_WAIT)) ++K; }
                const float fD = pnlDpsFactor(w, Gr), budget = std::max(1.f, w.me.hp - policy::RECOVER_HP * w.me.maxHp);
                int need = 1; for (; need <= 5; ++need) if (pnlLoss(Gr, need, mgTeamDps(w, need) * fD, true) <= budget) break;
                if (K < std::max(2, need)) continue;
                const float V = (float)Gr.gems + (float)Gr.gold / gvtPick(); if (V > bestV) { bestV = V; bestG = g; } }
            if (bestG >= 0 && !(M.kind == M_FARM && M.group >= 0 && S().groups[(size_t)M.group].campHandle.size() >= 5 && M.lastHitRound >= M.since)) {
                setGoal(M_FARM, bestG, -1, w); M.why = "rsw2"; g_lastPickRound = w.round; g_rsw2G = bestG; g_rsw2Since = M.since; } } }
    if (policy::TBA1_ON || policy::TBA_LOG_ON) {   /* TBA: 팀 큰 무리 배정 (모든 몸 같은 계획) — 멤버는 한 번 옮김 */
        const TbaPlan P = tbaCached(w); const bool mine = P.g >= 0 && team::T.slot >= 0 && ((P.mask >> team::T.slot) & 1);
        if (policy::TBA_LOG_ON) { static int s_lg = -2, s_lm = -1, s_lr = -1000; if (P.g != s_lg || P.mask != s_lm || w.round - s_lr >= 10) { s_lg = P.g; s_lm = P.mask; s_lr = w.round;
            unsigned vh = 2166136261u; for (int k = 0; k < std::min(team::T.n, team::MAX); ++k) { const team::Mate& m = team::planView(k, w); const int x[5] = {(int)m.known, m.zone, m.claimGroup, m.claimKind, m.outReason}; for (int q = 0; q < 5; ++q) { vh ^= (unsigned)(x[q] + 7); vh *= 16777619u; } }
            unsigned th = 0; for (int k = 0; k < team::MAX; ++k) th = th * 31u + (unsigned)(s_tbaType[k] + 2);
            log::always("#tba %d %d g=%d mask=%d K=%d gain=%.4f mine=%d mg=%d mk=%d vh=%u th=%u", w.round, team::T.slot, P.g, P.mask, P.K, P.gain, (int)mine, M.group, (int)M.kind, vh, th); } }
        if (s_tbaLeft.size() != S().groups.size()) s_tbaLeft.assign(S().groups.size(), -100000);
        { static bool s_hold = false; const bool h = tbaHold(); if (s_hold && !h && g_tbaG >= 0 && (size_t)g_tbaG < s_tbaLeft.size()) s_tbaLeft[(size_t)g_tbaG] = w.round; s_hold = h; }   // TBA v2: 끝난 때 기억
        const bool okG = P.g >= 0 && (S().groups[(size_t)P.g].state == 0 || S().groups[(size_t)P.g].state == 1) && !groupUnreachable(P.g)
            && !((size_t)P.g < g_avoidUntil.size() && g_avoidUntil[(size_t)P.g] > w.round) && w.round - s_tbaLeft[(size_t)P.g] > std::max(S().groups[(size_t)P.g].respawn, (int)rules::CAPTURE_ROUNDS);   // TBA v2: 내 기억으로 열 수 있는 무리만 · 다시 옮김 고리 끊기
        if (policy::TBA1_ON && mine && okG && (M.kind == M_NONE || (M.kind == M_FARM && M.group != P.g)) && !tbaHold()) {
            if (policy::TBA_LOG_ON) log::always("#tbj %d %d g=%d from=%d fk=%d K=%d mask=%d d=%d hp=%.0f cx=%d cy=%d id=%u", w.round, team::T.slot, P.g, M.group, (int)M.kind, P.K, P.mask, cheb(w.me.pos, S().groups[(size_t)P.g].center), w.me.hp, S().groups[(size_t)P.g].center.x, S().groups[(size_t)P.g].center.y, w.me.handle);
            setGoal(M_FARM, P.g, -1, w); M.why = "tba"; g_lastPickRound = w.round; g_tbaSince = M.since; g_tbaG = P.g; }
        if (policy::TBA_LOG_ON) { static bool s_was = false; const bool now = tbaHold(); if (s_was && !now) log::always("#tbe %d %d g=%d why=%s k=%d mg=%d hp=%.0f d=%d", w.round, team::T.slot, g_tbaG, M.why ? M.why : "-", (int)M.kind, M.group, w.me.hp, g_tbaG >= 0 ? cheb(w.me.pos, S().groups[(size_t)g_tbaG].center) : -1); s_was = now; }
    }
    // 6. 임무 없음 → 상점 (스폰에서 금 있음) → 농사 (양보한 그룹은 뺀다)
    /* TWP 규칙 6: Willow 는 농사 중 다친 팀원의 무리로 (willow-support) */
    if constexpr(reference::WILLOW_MACRO){
        if(me.type==reference::WILLOW&&(M.kind==M_NONE||M.kind==M_FARM)&&!me.atSpawn&&io::fuel()>22000){
            const int support=special::willowSupportGroup(w);
            if(support>=0&&!tbaHold()&&(M.kind==M_NONE||M.group!=support&&stepsToGoal(me.pos)>0)){setGoal(M_FARM,support,-1,w);M.why="willow-support";g_lastPickRound=w.round;}
        }
    }
    if (M.kind == M_NONE) {
        if (me.atSpawn && me.gold >= policy::SHOP_MIN_GOLD) { setGoal(M_SHOP, -1, -1, w); }
        else { int rtxEx = -1;   /* RTX: 후퇴한 무리는 그 곁에 최근 3R 적이 있는 동안 뺀다 */
            if (s_rtxG >= 0 && (size_t)s_rtxG < S().groups.size()) { const Group& Gx = S().groups[(size_t)s_rtxG];
                for (const auto& em : enMem(w)) if (cheb(em.first.pos, Gx.center) <= policy::RETREAT_ENEMY_R + Gx.rangeMax) { rtxEx = s_rtxG; break; } }
            if (rtxEx < 0) s_rtxG = -1;   /* RTX: 적이 무리 곁에서 사라지면 풀림 */
            tryPickFarm(w, rtxEx); }
    }
    if (M.kind == M_SHOP) { if (me.gold < policy::SHOP_MIN_GOLD || (me.atSpawn && w.round - M.since > 2)) { setGoal(M_NONE, -1, -1, w); g_lastPickRound = -1000; tryPickFarm(w, -1); } }   // 스폰에 닿아 샀으면 (economy::shop) 끝
    // 목표 그룹/이웃의 멤버 구성 변경은 통로 변경과 구분하여 root 캐시도 무효화.
    refreshGoalPassability(w);
    // 7. 목표 거리장: 목표가 바뀌었거나 (tag) 막힘이 바뀌었을 수 있는 8R 마다 다시. 이번 턴에 내 거리장을 만들었으면 다음 턴에 (한 턴에 거리장 하나)
    if (!policy::BH2_MV5_ON && policy::KT_ON && M.kind == M_FARM && M.group >= 0) {   // KT: 탱크 표시가 바뀌면 목표 칸 다시 (BH2 MV5: 켜지면 끔 — 목표 칸은 탱크 표시를 안 씀)
        static unsigned s_tm = 0u; static int s_tmG = -1; const unsigned tm = tankMask(w, S().groups[(size_t)M.group]);
        if (s_tmG == M.group && tm != s_tm) { ++g_goalTag; M.bestD = 99999; M.progressRound = w.round; }
        s_tm = tm; s_tmG = M.group;
    }
    {   // ZO3: 목표 칸 → 목표 거리장, 단계마다 연료 확인
        static long long s_tilesCost = policy::FG_INIT, s_fieldCost = 25000;
        static std::vector<Pos> s_tiles; static int s_tilesTag = -1, s_tilesRound = -1000;
        auto measure = [](long long f0, int r0, long long& est) { const long long c = f0 - io::fuel(); est = io::round() != r0 ? policy::FG_MAX : std::min(policy::FG_MAX, std::max({5000LL, c, est * 9 / 10})); };   // KC3: 감쇠 최댓값
        const bool wallsMoved = policy::SF_ON && dGoal.ready && dGoal.campVersion != io::campPassabilityVersion();
        bool bgfOut = false;   /* BGF: 제한 거리장이면 내 칸이 한도 - 여유 안에 있어야 함 (밖이면 다시) */
        if (policy::BGF_ON && dGoal.ready && dGoal.tag == g_goalTag && dGoal.maxDistance > 0) { const int dm = dGoal.get(me.pos); bgfOut = dm < 0 || dm >= 65535 || dm > dGoal.maxDistance - policy::BGF_MARGIN; }
        const bool want = M.kind != M_NONE && (dGoal.tag != g_goalTag || w.round - g_lastGoalRound >= 8 || !dGoal.ready || wallsMoved || bgfOut);
        const bool fsOk = dMe.round != w.round || io::fuel() >= policy::FS_FUEL;
        if (want && fsOk && (s_tilesTag != g_goalTag || w.round - s_tilesRound >= 8) && io::fuel() >= std::min(policy::ZO_NEED_CAP, s_tilesCost + zres())) {
            const long long f0 = io::fuel(); const int r0 = io::round();
            g_lcCold = 0; g_tilesPartial = false;
            s_tiles = goalTiles(w); s_tilesTag = g_goalTag; s_tilesRound = w.round;
            if (M.kind == M_FARM && s_occExTag == M.since && !s_occEx.empty()) {   // OQ
                std::vector<Pos> kept; for (const auto& t : s_tiles) if (std::find(s_occEx.begin(), s_occEx.end(), t) == s_occEx.end()) kept.push_back(t);
                if (!kept.empty()) s_tiles.swap(kept);
            }
            if (g_tilesPartial && s_tiles.empty()) s_tilesTag = -1;   // LC: 하나도 못 셌으면 다음 턴에 다시
            if (policy::GLM_ON && !g_tilesPartial && s_tiles.empty() && M.kind == M_FARM && M.group >= 0) { abandon(w, "no-tiles"); g_lastPickRound = -1000; s_tilesTag = -1; }   /* GLM: 설 칸이 없는 목표는 포기 */
            { const long long cold = g_lcCold; const long long fNow = io::fuel(); const long long c = f0 - fNow - cold; s_tilesCost = io::round() != r0 ? policy::FG_MAX : std::min(policy::FG_MAX, std::max({5000LL, c, s_tilesCost * 9 / 10})); }   // LC: 따뜻한 비용만
        }
        static double s_fieldRate = 40.0; const long long fieldPred = std::max(s_fieldCost, (long long)(s_fieldRate * (double)(s_tiles.size() + 150)));   // NI: 뿌리 수 비례 추정
        if (want && fsOk && s_tilesTag == g_goalTag && w.round - s_tilesRound < 8 && io::fuel() >= std::min(policy::ZO_NEED_CAP, fieldPred + zres())) {
            const long long f0 = io::fuel(); const int r0 = io::round();
            const bool newGoal = dGoal.tag != g_goalTag;
            io::nav.goalReason |= (newGoal || !dGoal.ready ? 1 : 0) | (wallsMoved ? 2 : 0) | (w.round - g_lastGoalRound >= 8 ? 8 : 0);
            int bgfB = 0;   /* BGF: BFS 를 목표 직선거리 + 여유 두 배에서 멈춤 (묻는 칸은 내 칸 둘레뿐) */
            if (policy::BGF_ON) { int cmin = 1 << 20; for (const auto& t : s_tiles) cmin = std::min(cmin, cheb(t, me.pos)); if (cmin < (1 << 20)) bgfB = cmin + 2 * policy::BGF_MARGIN; }
            bool computed = dGoal.compute(w, s_tiles, bgfB);
            if (computed && bgfB > 0 && io::round() == r0) { const int dm = dGoal.get(me.pos); if (dm < 0 || dm >= 65535 || dm > bgfB - policy::BGF_MARGIN) { if (io::fuel() >= s_fieldCost + zres()) computed = policy::FS_IO2_ON ? dGoal.recomputeSame(w, s_tiles, 0) : dGoal.compute(w, s_tiles, 0);   /* FS_IO2 */ else computed = false; } }   /* BGF: 내 칸이 한도 근처면 전체로 · GFC: 연료가 될 때만 */
            if (dGoal.ranCompute) ++io::nav.goalBuilds;
            if (computed && io::round() == r0) {
                dGoal.tag = g_goalTag; g_lastGoalRound = w.round;
            } else { dGoal.ready = false; ++io::nav.deferred; }
            if (newGoal) { M.bestD = 99999; M.progressRound = w.round; }
            measure(f0, r0, s_fieldCost);
            { const long long c = f0 - io::fuel(); const double rt = io::round() != r0 ? s_fieldRate * 1.5 : (double)c / (double)(s_tiles.size() + 150); s_fieldRate = std::max(rt, s_fieldRate * 0.98); }   // NI
        }
        const bool stillDirty = !dGoal.ready || dGoal.tag != g_goalTag || dGoal.campVersion != io::campPassabilityVersion();
        if (want && stillDirty && (!fsOk || s_tilesTag != g_goalTag ||
            io::fuel() < std::min(policy::ZO_NEED_CAP, fieldPred + zres()))) {
            ++io::nav.deferred; M.progressRound = w.round;  // 연료 보류를 정체/장기 회피로 바꾸지 않는다.
            { s_fieldCost = std::max(5000LL, s_fieldCost * 9 / 10); s_fieldRate = std::max(10.0, s_fieldRate * 0.9); if (s_tilesTag != g_goalTag || w.round - s_tilesRound >= 8) s_tilesCost = std::max(5000LL, s_tilesCost * 9 / 10); }   /* FPK: 미룸마다 추정 감쇠 (잠김 방지) */
        }
    }
    if (policy::GF_ON && policy::GF_WALK && dGoal.tag != g_goalTag && io::fuel() >= s_gfCost + (w.round <= 3 ? policy::FG_RES : zres())) {   // GF: 그 자리에서 (FG: 처음 3R 은 여유를 줄여 바로 걷는다)
        if ((M.kind == M_FARM || M.kind == M_CAPTURE) && M.group >= 0 && !gfReady(M.group)) gfBuild(M.group);
        else if ((M.kind == M_SHOP || M.kind == M_RECOVER) && s_gfHome.empty()) gfBuildHome();
    }
    if (M.kind == M_FARM && M.group >= 0 && dGoal.ready && dGoal.tag == g_goalTag && stepsToGoal(me.pos) == 1) {   // OQ
        std::vector<Pos> occ; bool freeGoal = false;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{me.pos.x + dx, me.pos.y + dy}; if (!S().inb(q) || dGoal.get(q) != 0) continue;
            bool ally = false; for (const auto& a : w.allies) if (a.pos == q) ally = true;
            if (ally) occ.push_back(q); else freeGoal = true; }
        s_queueTurns = (!occ.empty() && !freeGoal) ? s_queueTurns + 1 : 0;
        if (s_queueTurns >= policy::OQ_TURNS) {
            if (s_occExTag != M.since) { s_occEx.clear(); s_occExTag = M.since; }
            for (const auto& q : occ) if (s_occEx.size() < 24 && std::find(s_occEx.begin(), s_occEx.end(), q) == s_occEx.end()) s_occEx.push_back(q);
            ++g_goalTag; M.bestD = 99999; M.progressRound = w.round; s_queueTurns = 0; M.why = "occ-tile";
        }
    } else s_queueTurns = 0;
    if (policy::TPS_ON) tpsTick(w);   /* TPS */
    if (policy::BG2B_ON && (M.kind == M_FARM || M.kind == M_CAPTURE) && (w.round - combat::lastChampHit() <= 2 || w.round - s_hurtR <= 2)) M.progressRound = w.round;   /* BG2B: 싸우는 중은 정체 아님 (HYS inFight 와 같은 정의) */
    if (policy::TBA2_ON && M.kind == M_FARM && M.group >= 0 && stepsToGoal(me.pos) == 0 && tbaWait(w, M.group)) M.progressRound = w.round;   /* TBA2: 모이기 기다림은 정체 아님 */
    // 8. 정체: 목표 걸음이 STALL_ROUNDS 동안 줄지 않으면 (끝내지 못하는 자리) 중단 — 다음 턴에 새 임무.
    //    목표 칸에 서 있는데 (d = 0) 농사가 진행되지 않으면 (칠 캠프가 안 보임 — 덤불 · 가려짐) 그것도 정체: 2배 기다린 뒤 중단
    if (M.kind == M_FARM || M.kind == M_CAPTURE || M.kind == M_TRIBUTE) {
        const int d = stepsToGoal(me.pos);
        if (d < M.bestD) { M.bestD = d; M.progressRound = w.round; }
        if (d > 0 && w.round - M.progressRound >= policy::STALL_ROUNDS) abandon(w, "stall");
        else if (d == 0 && M.kind == M_FARM) {
            // 공격 칸에 섰는데 이 그룹의 캠프가 하나도 안 보이면 (시선이 막힘) 이 칸은 눈먼 칸 → 빼고 목표를 다시 (ambush: 금 클러스터 3칸 앞에서 200R 서 있었다)
            bool seen = false; for (const auto& c : w.camps) if (c.group == M.group) seen = true;
            const Group& G = S().groups[(size_t)M.group];
            if (!seen && G.state != 2 && G.state != 3 && M.blind.size() < 24) { M.blind.push_back(me.pos); ++g_goalTag; M.why = "blind-tile"; if (log::ON) io::debugGroup(M.group, me.pos); }
            if (G.state != 3 && w.round - std::max(M.progressRound, M.lastHitRound) >= policy::STALL_ROUNDS * 2) abandon(w, "no-target");   /* NTR */
        }
    }
}

}  // namespace nb::mission
