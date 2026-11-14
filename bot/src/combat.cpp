#include <cstring>
// combat.cpp — 한 턴의 행동 조합을 같은 단위 (젬) 로 셈한다. 연료를 위해 표적 가치 · 적 위협은 턴에 한 번 미리 셈하고, 칸 고리 안은 좌표 비교만 한다.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "combat.h"
#include "marginal_join.h"
#include "policy.h"
#include "team.h"
#include "log.h"

namespace nb::combat {
int g_noEnter = 0; int g_mstep = 0;   /* MINE4: MSTEP 이 걸음을 바꾼 횟수 */
int g_ccdLeave = -1000, g_ccdMove = -1000;   /* CCD */

static const Static& S() { return io::S; }

float gemPerHp(int gems, float maxHp) { return ((float)gems + policy::DEATH_TIME_GEMS + ((float)S().killGold / mission::goldDiv() + 10.f) + policy::HEAL_COST_GEMS) / std::max(1.f, maxHp); }
// 피해 dmg 의 젬 비용: 회복 시간 (한 통 = HEAL_COST_GEMS) + 죽음 위험 (남는 체력이 LOW_HP_FRAC 아래로 내려갈수록 제곱으로 커짐; 체력이 많을 때의 피해는 싸고, 바닥 근처의 피해는 비싸다)
float dmgCost(float dmg, float hp, float maxHp, int gems) {
    const float mh = std::max(1.f, maxHp);
    const float heal = std::min(dmg, std::max(0.f, hp)) / mh * policy::HEAL_COST_GEMS;   /* OVK: 죽은 뒤 넘치는 피해는 회복 비용이 아님 (못 피할 죽음이면 최대 교환을 고른다) */
    const float after = std::max(0.f, hp - dmg) / mh;
    const float death = (float)gems + mission::myDeathTime() + ((float)S().killGold / mission::goldDiv() + 10.f);   /* DTR */
    float risk = 0.f;
    if (after < policy::LOW_HP_FRAC) { const float x = (policy::LOW_HP_FRAC - after) / policy::LOW_HP_FRAC; risk = death * x * x; }
    return heal + risk;
}
/* LHF: 죽음 위험 = 남는 체력이 이 칸 예보 F 한 번을 못 견디는 몫의 제곱. F=0 이면 위험 0. x = clamp((F - 남는 체력) / F, 0, 1) */
static float dmgCostF(float dmg, float hp, float maxHp, int gems, float F) {
    if (!policy::LHF_ON) return dmgCost(dmg, hp, maxHp, gems);
    const float mh = std::max(1.f, maxHp);
    const float heal = std::min(dmg, std::max(0.f, hp)) / mh * policy::HEAL_COST_GEMS;   /* OVK 그대로 */
    const float afterHp = std::max(0.f, hp - dmg);
    const float death = (float)gems + mission::myDeathTime() + ((float)S().killGold / mission::goldDiv() + 10.f);   /* DTR */
    float risk = 0.f;
    if (F > 0.f) { const float x = std::min(1.f, std::max(0.f, (F - afterHp) / F)); risk = death * x * x; }
    return heal + risk;
}
static int g_stuckTurns = 0; static Pos g_lastPos;

struct Cell { Pos p; bool dash = false; int dashSlot = -1; Pos mid{-1, -1}; bool dw = false; float dmg = 0.f; float prog = 0.f; bool lethal = false; int d1 = 999; bool finishOk = false; float fEd = 0.f, fKd = 0.f; float cae = 0.f; };   /* LHF: 칸 예보 (적 threatAtTgt · 캠프 campThreat) */
static float lhfF(const Cell& c) { return std::max(c.fEd, c.fKd); }   /* LHF: F = max(적 예보, 캠프 예보) */
// 적 위협 (턴에 한 번): 평타 사거리 r1 (칠 수 있는 칸 거리), 두 번 둘 때 r2, 주문 (준비된 것) 의 피해 · 사거리
struct Threat { Pos pos; int r1, r2; float hit; int turns; float spell; int spellReach; int ccReach = -1; float hit2 = 0.f; bool oneAb = true; int kaR = -1; };   /* TRS: hit2 = 리셋 둘째 평타 · oneAb = 내 다음 차례 전 한 차례뿐 (기술 하나) · KAR: kaR = 처치각 거리 */
[[maybe_unused]] static float thDmg(const Threat& t, int d) {   /* COV: 한 적이 내 다음 차례 전에 거리 d 로 주는 피해 = 평타 + max(주문, 리셋 둘째 평타) (두 차례면 둘 다) */
    float h = 0.f; if (d <= t.r1) h = t.hit * (float)t.turns; else if (t.turns == 2 && d <= t.r2) h = t.hit;
    const float sp = (t.spell > 0.f && d <= t.spellReach) ? t.spell : 0.f, rs = (t.hit2 > 0.f && d <= t.r1) ? t.hit2 : 0.f;
    return h + (t.oneAb ? std::max(sp, rs) : sp + rs);
}   /* CCS: 기절 수단 사거리 (없으면 -1) */
// 표적 (턴에 한 번): 평타 한 번의 가치 · 주문 피해 sd 의 가치 함수를 위해 체력 · 가치 보관
struct Target { int kind; std::uint32_t handle; Pos pos; float hp, maxHp, full, hitVal; bool channeling; int group; float ally = 0.f; bool fb = false; };   /* CDS: fb = 싸움 · 열세 중 캠프 (기술 대체 표적만) */   /* FOC2: ally = 아군 평타 피해 (이번 R) */
struct CampSrc { Pos pos; float str; int range = 3; int nearOther = 99; int nearEnemy = 99; int nearAlly = 99; float enVal = 0.f; int nearAll = 99; long long nearAllH = 0x7FFFFFFFFFFFFFFFLL; };   /* BH3_TH5: nearAll = 적 (nearEnemy 와 같은 거름) · 아군 전부 (acted 안 거름) 중 최소 거리, nearAllH = 그 거리 몸 중 가장 작은 handle */   /* CAE: 가장 가까운 적 거리 · 그 적이 맞는 피해의 가치 (젬 / 피해 1) */   /* CAG: 나 말고 가장 가까운 챔피언 거리 */   // ED: 캠프 종류별 사거리

static Pos s_mfP[8]; static int s_mfR[8] = {-100, -100, -100, -100, -100, -100, -100, -100}; static int s_mfK = 0;   /* MFB */
void noteMoveFail(Pos p) { s_mfP[s_mfK] = p; s_mfR[s_mfK] = io::round(); s_mfK = (s_mfK + 1) & 7; }
static bool freeTile(const World& w, Pos p, const mission::Mission&) {
    if (!S().walkableStatic(p)) return false;
    if (policy::MFB_ON) for (int k = 0; k < 8; ++k) if (s_mfP[k] == p && w.round - s_mfR[k] <= policy::MFB_R) return false;   /* MFB: 막 실패한 칸 */
    if (io::campBlocksMovement(p)) return false;
    return !S().occupiedNow(p, w.round);
}
static bool dashPathFree(const World& w, Pos p) {
    const Pos me = w.me.pos; const int dx = p.x - me.x, dy = p.y - me.y;
    const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
    const Pos cand[3] = {{me.x + sx, me.y + sy}, {me.x + sx, me.y}, {me.x, me.y + sy}};
    for (Pos m : cand) {
        if (!S().inb(m) || cheb(m, p) != 1 || cheb(m, me) != 1) continue;
        if (S().tile[(size_t)S().idx(m)] & (T_WALL | T_ALTAR | T_ENEMY_SPAWN)) continue;
        if (!S().occupiedNow(m, w.round) && !io::campBlocksMovement(m)) return true;
    }
    return false;
}

static float fnDps0(int type, float str, float focus);
static float fnDps(int type, float str, float focus) {   /* FS_COM2: 메모 (값은 fnDps0 그대로) */
    if (!policy::FS_COM2_ON) return fnDps0(type, str, focus);
    struct E { unsigned g; int t; std::uint32_t s, f; float v; }; static E s_m[32] = {};
    std::uint32_t sb, fb; std::memcpy(&sb, &str, 4); std::memcpy(&fb, &focus, 4);
    std::uint32_t h = sb ^ (sb >> 15) ^ (fb * 0x9E3779B1u) ^ ((std::uint32_t)type * 0x85EBCA6Bu); h ^= h >> 16;
    E& e = s_m[h & 31u];
    if (e.g == io::g_fs2Gen && e.t == type && e.s == sb && e.f == fb) return e.v;
    const float v = fnDps0(type, str, focus); e.g = io::g_fs2Gen; e.t = type; e.s = sb; e.f = fb; e.v = v; return v;
}
static float fnDps0(int type, float str, float focus) {   // FN: 평타 힘 + 주문 피해 / (쿨다운+1)
    float v = std::max(1.f, str);
    const ChampDesign& D = S().designOf(type);
    if (D.valid) for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s]; if (a.kind == rules::AB_RESET) { v += std::max(1.f, str) / (float)(a.cooldown + 1); continue; }   /* DPR: 리셋 둘째 평타 */ if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_CC && a.kind != rules::AB_AOE && a.kind != rules::AB_HEAL /* FHL: 치유 = 상대 피해를 지움 */ && !(a.kind == rules::AB_DASH && a.modifier > 0.f)) continue; /* HDT */ v += focus * a.modifier / (float)(a.cooldown + 1); }
    return v;
}
static float moveSpeed(int type) { const ChampDesign& D = S().designOf(type); float v = 1.f; if (D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH) v += D.ab[s].range / (float)(D.ab[s].cooldown + 1); return v; }   /* WKD3: 칸/라운드 (대시) */
static float vulCampOn(const World& w, const Unit& e) {   /* VUL: 다음 캠프 단계에 이 적을 칠 깨어 있는 캠프 힘 */
    float s = 0.f;
    for (const auto& k : w.camps) { if (k.str <= 0.f) continue; const int de = cheb(k.pos, e.pos); if (de > S().campRange(k.type)) continue;
        const Group& KG = S().groups[(size_t)k.group]; if (!(KG.awakeUntil >= w.round || k.hp < k.maxHp)) continue;
        bool nearest = cheb(k.pos, w.me.pos) > de;
        if (nearest) for (const auto& a : w.allies) if (a.visible && a.alive && cheb(k.pos, a.pos) <= de) { nearest = false; break; }
        if (nearest) for (const auto& o : w.enemies) if (o.alive && o.handle != e.handle && cheb(k.pos, o.pos) <= de) { nearest = false; break; }
        if (nearest) s += k.str; }
    return s;
}
static int etnTurns(const World& w, const Unit& e) {   /* ETN2: 적이 내 다음 턴 전에 두는 수 = (이번 라운드 아직 안 둠) + (지난 라운드 내 턴에 이미 둠), 0 이면 1, 기록 없으면 -1 */
    static int s_r = -1, s_prevR = -100; static std::vector<std::pair<std::uint32_t, bool>> s_cur, s_prev;
    if (s_r != w.round) { s_prev.swap(s_cur); s_prevR = s_r; s_cur.clear(); s_r = w.round; for (const auto& q : w.enemies) s_cur.emplace_back(q.handle, q.acted); }
    if (s_prevR == w.round - 1) for (const auto& q : s_prev) if (q.first == e.handle) return policy::ZT_ON ? (e.acted ? 0 : 1) + (q.second ? 1 : 0) : std::max(1, (e.acted ? 0 : 1) + (q.second ? 1 : 0));   /* ZT: 0 그대로 */
    return -1;
}
static void buildThreats(const World& w, std::vector<Threat>& out) {
    out.clear();
    for (const auto& e : w.enemies) {
        Threat t; t.pos = e.pos; t.turns = rules::enemyTurnsBeforeMine(e.acted); t.hit = e.str;
        const bool melee = e.range <= 1;
        t.r1 = melee ? rules::MELEE_RANGE + 1 : rules::RANGED_RANGE + (policy::RNGW_ON ? 1 : 0);      // 한 번 둘 때 칠 수 있는 거리
        t.r2 = melee ? rules::MELEE_RANGE + 2 : rules::RANGED_RANGE + (policy::RNGW_ON ? 2 : 1);      // 두 번 둘 때 (두 걸음 · RUXB: 원거리는 걷고 침)
        t.spell = 0.f; t.spellReach = 0;
        const ChampDesign& D = S().designOf(e.type);
        int n = policy::ETN2_ON ? etnTurns(w, e) : -1; if (n < 0) n = t.turns;
        if (policy::BH4_HR8_ON && e.stunLeft > 0 && !(D.valid && D.passive == 8)) n = std::max(0, n - std::min(n, e.stunLeft));   /* BH4_HR8: 묶인 차례는 못 둠 (0 이면 ZT 경로) */
        const bool zt0 = policy::ZT_ON && n == 0; n = std::max(1, n);   /* ZT: 0번 구간 적 (목록에는 둠: 406줄이 적 목록과 같은 번호로 씀 · 여러 턴 셈) */   /* ETN2: 정확한 차례 수 (닿느냐에만) */
        int dr = 0; if (policy::ETN2_ON && D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH && e.cooldown[s] <= n - 1) dr = std::max(dr, (int)D.ab[s].range);
        if (policy::ETN2_ON) { if (n == 1) t.r2 = t.r1; t.r1 += dr; t.r2 += dr; }
        if (D.valid) for (int s = 0; s < 2; ++s) {
            const AbilitySpec& a = D.ab[s];
            const bool dmgDash = a.kind == rules::AB_DASH && a.modifier > 0.f;   /* HDT: 피해 대시 (HoR) */
            if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_CC && a.kind != rules::AB_AOE && !dmgDash) continue;
            if (e.cooldown[s] > ((policy::ETN2_ON ? n : t.turns) == 2 ? 1 : 0)) continue;
            const float dmg = e.focus * a.modifier; const int reach = (int)a.range + (dmgDash ? (int)a.radius : 0) + (a.noWalk ? 0 : 1) + (policy::ETN2_ON && n == 2 ? 1 + (dmgDash ? 0 : dr) : 0);   /* ETN2: 두 차례면 걷고 · 대시하고 시전 */
            if (dmg > t.spell) { t.spell = dmg; t.spellReach = reach; }
            if (policy::CCS_ON && (a.kind == rules::AB_CC || (a.kind == rules::AB_AOE && a.duration > 0) || (dmgDash && a.duration > 0))) t.ccReach = std::max(t.ccReach, reach);   /* CCS */
        }
        { const int nT = policy::ETN2_ON ? n : t.turns; t.oneAb = nT <= 1;   /* TRS: 룰북 — 거의 모든 챔피언이 공격 리셋 (Marcella 는 다음 평타 1R 기절) */
          if (D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_RESET && e.cooldown[s] <= nT - 1) { t.hit2 = e.str; if (policy::CCS_ON && D.ab[s].duration > 0) t.ccReach = std::max(t.ccReach, t.r1); } }
        if (zt0) { t.r1 = t.r2 = -1; t.spell = 0.f; t.spellReach = -1; t.ccReach = -1; }   /* ZT: 이번 턴에는 못 닿음 */
        out.push_back(t);
    }
}
static const std::vector<Threat>& fs3Threats(const World& w) {   /* FS_COM3: buildThreats 캐시 */
    static std::vector<Threat> s_ts; static unsigned s_g = 0; static int s_r = -1; static const Unit* s_d = nullptr; static size_t s_n = (size_t)-1;
    if (s_g != io::g_fs3Gen || s_r != w.round || s_d != w.enemies.data() || s_n != w.enemies.size()) { buildThreats(w, s_ts); s_g = io::g_fs3Gen; s_r = w.round; s_d = w.enemies.data(); s_n = w.enemies.size(); }
    return s_ts;
}
static float tf6Drop(const World& w, const Threat& th, Pos c, int dur) {   /* TF6A: 이 적이 기절로 내 다음 차례 전 차례를 잃으면 칸 c 에서 빠지는 피해 (WKF 가림 그대로, 기절 면역 passive 8 은 0) */
    if (dur <= 0) return 0.f;
    for (const auto& e : w.enemies) if (e.pos == th.pos) { const ChampDesign& ED = S().designOf(e.type); if (ED.valid && ED.passive == 8) return 0.f; break; }
    const int dd = cheb(th.pos, c); bool other = false;
    for (const auto& al : w.allies) if (al.visible && cheb(al.pos, th.pos) < dd && al.hp <= w.me.hp) { other = true; break; }   /* WKF */
    const float k = other ? 0.15f : 1.f;
    Threat t2 = th; t2.turns = std::max(0, th.turns - (dur >= 2 ? 2 : 1)); t2.oneAb = t2.turns <= 1;   /* BH4_HR8 해석: 막는 차례 1, duration>=2 면 2 */
    return std::max(0.f, k * (thDmg(th, dd) - (t2.turns > 0 ? thDmg(t2, dd) : 0.f))); }
static float threatAtTgt(const World& w, const std::vector<Threat>& ts, Pos c) {   /* TGT: 그 적에게 내가 가장 가까운 우리 몸일 때만 */
    float dmg = 0.f;
    for (const auto& t : ts) {
        const int d = cheb(t.pos, c); bool other = false;
        for (const auto& a : w.allies) if (a.visible && cheb(a.pos, t.pos) < d && a.hp <= w.me.hp) { other = true; break; }   /* WKF: 나보다 튼튼한 아군은 나를 가려 주지 않는다 (상대 처치 3R 피해 92% 가 곁 최저 체력 몸) */
        const float k = other ? 0.15f : 1.f;   /* TGX: 표적이 아니어도 ~15% */
        dmg += k * thDmg(t, d);   /* TRS */
    }
    if (policy::CCS_ON && w.me.type != 7) {   /* CCS: 기절 수단이 닿으면 묶인 한 턴 동안 평타가 한 번씩 더 */
        bool cc = false; for (const auto& t : ts) if (t.ccReach >= 0 && cheb(t.pos, c) <= t.ccReach) { cc = true; break; }
        if (cc) for (const auto& t : ts) { const int d = cheb(t.pos, c); if (d <= t.r1) dmg += t.hit; }
    }
    return dmg;
}
static float threatAtTgtMC(const World& w, const std::vector<Threat>& ts, const std::vector<int>& mc, Pos c) {   /* FS_COM1: threatAtTgt 와 같은 값 (mc[i] = 가릴 아군 최소 거리) */
    float dmg = 0.f;
    for (size_t i = 0; i < ts.size(); ++i) { const auto& t = ts[i];
        const int d = cheb(t.pos, c);
        const float k = mc[i] < d ? 0.15f : 1.f;
        dmg += k * thDmg(t, d);
    }
    if (policy::CCS_ON && w.me.type != 7) {
        bool cc = false; for (const auto& t : ts) if (t.ccReach >= 0 && cheb(t.pos, c) <= t.ccReach) { cc = true; break; }
        if (cc) for (const auto& t : ts) { const int d = cheb(t.pos, c); if (d <= t.r1) dmg += t.hit; }
    }
    return dmg;
}
float enemyThreatAt(const World& w, Pos p) {   /* LFR */
    if (w.enemies.empty()) return 0.f;
    if (policy::FS_COM3_ON) return threatAtTgt(w, fs3Threats(w), p);   /* FS_COM3 */
    static std::vector<Threat> s_ts; buildThreats(w, s_ts);
    return threatAtTgt(w, s_ts, p);
}
float enemyThreatFor(const World& w, Pos p, float tgtHp, int tgtType, std::uint32_t tgtHandle) {   /* BH2_SP1: threatAtTgt 를 대상 몸 기준으로 (w.me 도 가림 후보) */
    if (w.enemies.empty()) return 0.f;
    static std::vector<Threat> s_tsB; const std::vector<Threat>& s_ts = policy::FS_COM3_ON ? fs3Threats(w) : (buildThreats(w, s_tsB), s_tsB);   /* FS_COM3 */
    float dmg = 0.f;
    for (const auto& t : s_ts) {
        const int d = cheb(t.pos, p); bool other = false;
        for (const auto& o : w.allies) { if (o.handle == tgtHandle) continue; if (o.visible && cheb(o.pos, t.pos) < d && o.hp <= tgtHp) { other = true; break; } }
        if (!other && w.me.handle != tgtHandle && cheb(w.me.pos, t.pos) < d && w.me.hp <= tgtHp) other = true;
        const float k = other ? 0.15f : 1.f;
        dmg += k * thDmg(t, d);
    }
    if (policy::CCS_ON && tgtType != 7) {
        bool cc = false; for (const auto& t : s_ts) if (t.ccReach >= 0 && cheb(t.pos, p) <= t.ccReach) { cc = true; break; }
        if (cc) for (const auto& t : s_ts) { const int d = cheb(t.pos, p); if (d <= t.r1) dmg += t.hit; }
    }
    return dmg;
}
static float threatAt(const std::vector<Threat>& ts, Pos c);
bool allyCrisis(const World& w, int rounds) {   /* TAC: CHC (채널 동안 죽을 자리) 와 같은 잣대를 보이는 아군에게 */
    if (rounds <= 0) return false;
    for (const auto& a : w.allies) { if (!a.visible || !a.alive || (a.state & (rules::ST_RECALLING | rules::ST_TELEPORTING))) continue;
        float s = 0.f; for (const auto& e : w.enemies) if (e.alive && e.visible && cheb(e.pos, a.pos) <= e.range + 2) s += e.str;
        if (s * (float)rounds >= a.hp) return true; }
    return false; }
float enemyThreatAtC(const World& w, Pos p) {   /* MINE */
    static std::vector<Threat> s_ts; static int s_r = -1; if (s_r != w.round) { s_r = w.round; buildThreats(w, s_ts); }
    return s_ts.empty() ? 0.f : threatAt(s_ts, p);
}
static float threatAt(const std::vector<Threat>& ts, Pos c) {
    float dmg = 0.f;
    for (const auto& t : ts) {
        const int d = cheb(t.pos, c);
        dmg += thDmg(t, d);   /* TRS */
    }
    return dmg;
}
// 캠프 위협: 캠프는 움직이지 않으므로 보이지 않아도 (덤불 속) 스폰 칸에 있다고 본다 — 그룹이 살아 있다고 아는 한. 사거리 3 안이면 라운드마다 힘만큼.
// 잠든 캠프 (피해를 안 받음) 는 치지 않는다 (룰) → 깬 그룹 (awakeUntil) 과 내가 이번 임무로 칠 그룹만.
static inline long long ordKey(int prio, std::uint32_t h) { return ((long long)(-prio) << 32) | (long long)h; }   /* TIE: 작을수록 먼저 둠 = 동거리에서 맞음 */
static void campSources(const World& w, const mission::Mission& Mi, std::vector<CampSrc>& out) {
    out.clear();
    static std::vector<std::pair<std::uint32_t, int>> s_ap; s_ap.clear(); if (policy::TIE_ON && policy::BH3_TH5_ON) for (const auto& a : w.allies) if (a.alive && a.visible) s_ap.push_back({a.handle, io::priorityNow(a.handle)});   /* TIE */
    auto allyKey = [&](const Unit& a) { int p = 0; if (policy::TIE_ON) for (const auto& x : s_ap) if (x.first == a.handle) { p = x.second; break; } return ordKey(p, a.handle); };
    static std::vector<int> s_ng; static int s_rmax = -1; if (s_rmax < 0) { s_rmax = 0; for (const auto& Gx : S().groups) s_rmax = std::max(s_rmax, Gx.rangeMax); }
    if (policy::SNS_ON) io::nearGroups(w.me.pos, s_rmax + 5, s_ng); else { s_ng.clear(); for (size_t gi = 0; gi < S().groups.size(); ++gi) s_ng.push_back((int)gi); }   /* SNS: 곁 그룹만 (번호 순) */
    struct CsxE { Pos p; float ev; std::uint32_t h; }; static std::vector<CsxE> s_xe; static std::vector<Pos> s_xa; s_xe.clear(); s_xa.clear();   /* CSX ③: 적 · 아군 한 번 */
    if (policy::CSX_ON) { for (const auto& e : w.enemies) if (e.alive && e.visible && e.acted) s_xe.push_back({e.pos, ((float)e.gems + (float)S().killGold / mission::goldDiv() + policy::DEATH_TIME_GEMS) / std::max(1.f, e.hp) * 0.6f, e.handle});
        for (const auto& a : w.allies) if (a.alive && a.visible && a.acted) s_xa.push_back(a.pos); (void)io::seesTile(w.me.pos); }   /* CAD: 위치가 굳은 (이미 둔) 몸만 흡수자 */
    for (int gix : s_ng) { const size_t gi = (size_t)gix;
        const Group& G = S().groups[gi];
        if (G.state == 2 || G.state == 3 || G.strMax <= 0.f) continue;
        if (cheb(G.center, w.me.pos) > G.rangeMax + 5) continue;   // ED
        const bool awake = G.awakeUntil >= w.round || (Mi.kind == mission::M_FARM && Mi.group == (int)gi);
        if (!awake) continue;
        for (size_t k = 0; k < G.spawn.size(); ++k) {
            const int t = G.campType[k]; const float str = S().campStr(t);   // ED
            if (str <= 0.f) continue;
            const Pos sp = G.spawn[k];
            // 시야 안 (8칸) 이고 덤불이 아닌데 보이는 산 캠프 목록에 없으면 죽은 것
            if (cheb(sp, w.me.pos) <= rules::VISION && !(S().tile[(size_t)S().idx(sp)] & T_BUSH) && (policy::CSX_ON ? io::seesTileQ(sp) : io::seesTile(sp))) {   // VB: 덤불 너머는 안 보인다   /* CSX */
                const size_t mi = G.seenOffset + k; const bool seen = mi < S().campSeenRound.size() && S().campSeenRound[mi] == w.round && k < G.campLastHp.size() && G.campLastHp[k] > 0.f;   /* CSQ */
                if (!seen) continue;
            }
            { int no = 99, ne = 99, na = 99; float ev = 0.f;
              if (policy::CSX_ON) { for (const auto& xe : s_xe) { const int de = cheb(xe.p, sp); if (de < ne) { ne = de; ev = xe.ev; } } for (const Pos& xa : s_xa) na = std::min(na, cheb(xa, sp)); }   /* CSX ③ */
              else {
              for (const auto& e : w.enemies) if (e.alive && e.visible) { const int de = cheb(e.pos, sp); if (de < ne) { ne = de; ev = ((float)e.gems + (float)S().killGold / mission::goldDiv() + policy::DEATH_TIME_GEMS) / std::max(1.f, e.hp) * 0.6f; } }   /* CAE */
              for (const auto& a : w.allies) if (a.alive && a.visible) na = std::min(na, cheb(a.pos, sp)); }
              no = std::min(ne, na);   /* CAG */
              out.push_back({sp, str, S().campRange(t), no, ne, na, ev});   // ED
              if (policy::BH3_TH5_ON) { int nA = 99; long long nH = 0x7FFFFFFFFFFFFFFFLL;   /* BH3_TH5 */
                  auto upd = [&](int d, long long h) { if (d < nA || (d == nA && h < nH)) { nA = d; nH = h; } };
                  if (policy::CSX_ON) { for (const auto& xe : s_xe) upd(cheb(xe.p, sp), xe.h); } else { for (const auto& e : w.enemies) if (e.alive && e.visible) upd(cheb(e.pos, sp), e.handle); }
                  for (const auto& a : w.allies) if (a.alive && a.visible) upd(cheb(a.pos, sp), allyKey(a));   /* TIE */
                  out.back().nearAll = nA; out.back().nearAllH = nH; } }
        }
    }
}
static bool g_th5Scene = false; static long long g_th5Me = 0;   /* BH3_TH5: CXE3 장면 (M_FARM · campOnly · cxeBackOff) 이번 턴 */
static bool campHitsMe(const CampSrc& k, Pos q) { const int dq = cheb(k.pos, q); if (dq > k.range) return false;   /* BH3_TH5: 캠프가 q 에 선 나를 치나 */
    if (!(policy::BH3_TH5_ON && g_th5Scene)) return policy::TIE2_ON ? (dq < k.nearAlly && dq <= k.nearEnemy) : dq <= k.nearOther;   /* 옛: 동점 = 나, 흡수자 = 이미 둔 몸 (CSX) — TIE2: 이미 둔 아군은 먼저 둔 몸이라 동거리면 그쪽이 맞음 */
    if (dq != k.nearAll) return dq < k.nearAll; return g_th5Me < k.nearAllH; }   /* 흡수자 = 보이는 산 몸 전부, 동점 = handle 작은 쪽 (엔진) */
void campAggroNow(const World& w, const mission::Mission& Mi, float& hit, float& inRange) {   /* TNK: 지금 내 칸에서 캠프가 나를 칠 힘 합 (동거리 priority 규칙 포함) · 사거리 안 힘 합 */
    static std::vector<CampSrc> srcs; campSources(w, Mi, srcs);
    const bool sv = g_th5Scene; const long long sm = g_th5Me;
    g_th5Scene = policy::BH3_TH5_ON; g_th5Me = ordKey(policy::TIE_ON ? io::priorityNow(w.me.handle) : 0, w.me.handle);
    hit = 0.f; inRange = 0.f;
    for (const auto& k : srcs) { if (cheb(k.pos, w.me.pos) <= k.range) inRange += k.str; if (campHitsMe(k, w.me.pos)) hit += k.str; }
    g_th5Scene = sv; g_th5Me = sm;
}
static float campThreat(const std::vector<CampSrc>& srcs, Pos c) { float d = 0.f; for (const auto& k : srcs) { if (campHitsMe(k, c)) d += k.str; } return d; }   /* CAG: 가장 가까운 몸만 맞음 */   // ED: 캠프 종류별 사거리

// 표적 가치 (젬): 챔피언 = 죽이면 든 젬 + 처치 금 + 상대 시간 손실, 아니면 비율 할인. 캠프 = 피해 비율 × 그룹 가치 (젬 + 금/10) / 함께 치는 몸
static float champValue(const Target& t, float dmg) { if (policy::FOC2_ON && t.ally > 0.f && t.ally < t.hp) { if (dmg + t.ally >= t.hp) return t.full; return dmg / std::max(1.f, t.hp - t.ally) * t.full * 0.6f; }   /* FOC2 */
    if (dmg >= t.hp) return t.full; return dmg / std::max(1.f, policy::FOC_ON ? t.hp : t.maxHp) * t.full * 0.6f; }   /* FOC: 남은 체력 대비 */
static float campValue(const Target& t, float dmg) { return (policy::FKV_ON && dmg >= t.hp ? dmg : std::min(dmg, t.hp)) / std::max(1.f, t.maxHp) * t.full; }   /* FKV: 마지막 일격은 한 방 피해만큼 */
static float targetValue(const Target& t, float dmg) { return t.kind == 1 ? champValue(t, dmg) : campValue(t, dmg); }

static int s_dashFailR = -100; void noteDashFail() { s_dashFailR = io::round(); }   /* DFB */
static const Unit* g_plEnemy = nullptr; static int g_plCall = -1, g_plCallRound = -1;   // PL
static std::uint32_t g_chsH = 0; static int g_chsR = -10;   /* CHS: 지난 턴 싸움 표적 */
int fightCall() { return io::round() == g_plCallRound ? g_plCall : -1; }
static int s_hysHit = -100; int lastChampHit() { return s_hysHit; }   /* HYS */
static bool g_nfHold = false;   // NF: 이번 턴 캠프를 치지 않는다 (국지 열세)
static int g_vulHoldG = -1, g_vulMode = 0;   // VUL: 0 없음 · 1 기다림 · 2 처치 창 · 3 가로채기 창
struct VulCamp { Pos pos; int R, dE, dA, eIdx; bool awake; }; static VulCamp g_vulCamps[16]; static int g_vulN = 0;   // VUL3: 그룹 캠프마다 가장 가까운 적 · 아군 거리 (한 번만)
static float dpcK(Pos p) {   /* DPC: 적 진영 깊이 배율 */
    if (!policy::DPC_ON) return 1.f;
    const float dU = std::hypot((float)(p.x - S().allyCenter.x), (float)(p.y - S().allyCenter.y)), dE = std::hypot((float)(p.x - S().enemyCenter.x), (float)(p.y - S().enemyCenter.y));
    const float s = dU / std::max(1.f, dU + dE);
    return s <= 0.5f ? 1.f : 1.f + policy::DPC_K * (s - 0.5f) * 2.f;
}
static bool onEnemySpawn(Pos p) { for (const auto& s : S().enemySpawn) if (s == p) return true; return false; }
float enemyTwinAdd(const World& w, const Unit& e) {   /* BH4_MC6: 짝 하나를 죽이면 둘 다 죽고 두 가방 모두 킬러에게 (룰북) */
    if (!policy::BH4_MC6_ON || (e.type != 3 && e.type != 4)) return 0.f;
    float v = policy::DEATH_TIME_GEMS;
    for (const auto& o : w.enemies) if (o.alive && o.handle != e.handle && o.type == 7 - e.type) { v += (float)o.gems; break; }
    return v; }
static float enemyStakeIn(const World& w, const Unit& e, Pos c, int R, float kv) {   /* BH4_MC6: 반경 판돈 — 짝 둘이 다 반경 안이면 한 처치 (번호 큰 몸은 처치 금 몫 뺌), 짝이 밖이면 첫 몸에 짝 몫 */
    if (!policy::BH4_MC6_ON || (e.type != 3 && e.type != 4)) return (float)e.gems + kv;
    for (const auto& o : w.enemies) if (o.alive && o.handle != e.handle && o.type == 7 - e.type) {
        if (!onEnemySpawn(o.pos) && cheb(o.pos, c) <= R) return (float)e.gems + (o.handle < e.handle ? policy::PLV_DEATH : kv);
        return (float)e.gems + kv + (float)o.gems + policy::PLV_DEATH; }
    return (float)e.gems + kv + policy::PLV_DEATH; }
static float fbLerp(const float* X, const float* Y, int n, float x) { if (x <= X[0]) return Y[0]; for (int i = 0; i + 1 < n; ++i) if (x <= X[i + 1]) return Y[i] + (Y[i + 1] - Y[i]) * (x - X[i]) / (X[i + 1] - X[i]); return Y[n - 1]; }
static float fbBurst(const World& w, const Unit& e) { float b = fnDps(w.me.type, w.me.str, w.me.focus); for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, e.pos) <= std::max(1, a.range) + 1) b += fnDps(a.type, a.str, a.focus);   /* FBR: 이번 턴에 칠 수 있는 아군 */ return std::max(1.f, b); }
static float fbDelta(const World& w, const Unit& e) {   /* FB: 남아서 마무리할 때 기대 젬 − 물러날 때 (finish_boundary.md 4절, 래더 우리 표, 칸 사이 직선) */
    static const float TX[6] = {0.25f, 0.75f, 1.25f, 1.75f, 2.5f, 3.5f}, PK[6] = {0.58f, 0.51f, 0.35f, 0.30f, 0.20f, 0.08f};
    static const float RX[6] = {0.125f, 0.375f, 0.625f, 0.875f, 1.25f, 1.75f}, PD[6] = {0.f, 0.03f, 0.08f, 0.15f, 0.34f, 0.71f};
    const float T = e.hp / fbBurst(w, e);
    const float Rt = enemyThreatAt(w, w.me.pos) / std::max(1.f, w.me.hp);
    const float pk = fbLerp(TX, PK, 6, T), pd = fbLerp(RX, PD, 6, Rt);
    if (policy::FBU_ON) { const float kv = (float)S().killGold / mission::goldDiv() + policy::PLV_DEATH;   /* FBU */
        return (pk - 0.05f) * enemyStakeIn(w, e, e.pos, policy::TFD_R, kv) - policy::FBU_W * (pd - 0.02f) * ((float)w.me.gems + kv + (policy::FBT_ON ? twinStake(w) : 0.f)); }
    return (pk - 0.05f) * (2.f * (float)e.gems + 9.f) - policy::FB_W * (pd - 0.02f) * (2.f * ((float)w.me.gems + (policy::FBT_ON ? twinStake(w) : 0.f)) + 7.5f); }   /* FBT */
static bool fbTarget(const World& w, const Unit& e) { return e.alive && e.hp < 0.4f * e.maxHp && cheb(e.pos, w.me.pos) <= 5 && !onEnemySpawn(e.pos); }   /* FB: 깎인 적 (40% 아래, FN 거리) */
bool fbStay(const World& w) {   /* FB: 깎인 적 하나라도 기대 젬 + 면 남음 */
    static int s_r = -1; static bool s_v = false; if (s_r == w.round) return s_v; s_r = w.round; s_v = false;
    for (const auto& e : w.enemies) if (fbTarget(w, e) && fbDelta(w, e) > 0.f) { s_v = true; break; }
    return s_v; }
struct TFight { const Unit* tg = nullptr; float s = 1.f, E = 0.f; int nA = 0, nE = 0; float dA = 0.f; };   /* LFD: 팀 피해 합 */   /* TFD */
static TFight teamFight(const World& w) {   /* TFD: 싸움을 기준으로 한 팀 판단 (내 위치는 합에 안 들어감) */
    static int s_r = -1; static TFight s_t; if (s_r == w.round) return s_t;   // 한 몸의 한 턴 안에서는 한 번
    const Me& me = w.me; TFight T;
    const Unit* e0 = nullptr;   /* TFA2: 기준 = 내 PL_R+4 안 적 중 (체력, 번호) 최저 — 몸마다 같은 기준 */
    for (const auto& e : w.enemies) { if (!e.alive || onEnemySpawn(e.pos) || cheb(e.pos, me.pos) > policy::PL_R + 4) continue; if (!e0 || e.hp < e0->hp - 0.5f || (std::fabs(e.hp - e0->hp) <= 0.5f && e.handle < e0->handle)) e0 = &e; }
    if (e0) {
        const Unit* tg = e0;
        for (const auto& e : w.enemies) { if (!e.alive || onEnemySpawn(e.pos) || cheb(e.pos, e0->pos) > 5) continue; if (e.hp < tg->hp - 0.5f || (std::fabs(e.hp - tg->hp) <= 0.5f && e.handle < tg->handle)) tg = &e; }
        T.tg = tg;
        float hE = 0.f, dE = 0.f, stE = 0.f, hA = 0.f, dA = 0.f, stU = 0.f;
        const float kv = (float)S().killGold / mission::goldDiv() + policy::PLV_DEATH;
        for (const auto& e : w.enemies) { if (!e.alive || cheb(e.pos, tg->pos) > policy::TFD_R) continue; hE += e.hp; dE += fnDps(e.type, e.str, e.focus); ++T.nE; if (!onEnemySpawn(e.pos)) stE += enemyStakeIn(w, e, tg->pos, policy::TFD_R, kv); }   /* SPW: 스폰 위 적 = 힘은 셈, 판돈 뺌 */
        if (cheb(me.pos, tg->pos) <= policy::TFD_R) { hA += me.hp; dA += fnDps(me.type, me.str, me.focus); stU += (float)me.gems + kv; ++T.nA; }
        for (const auto& a : w.allies) { if (!a.visible || !a.alive || cheb(a.pos, tg->pos) > policy::TFD_R || ((policy::RAL_ON || policy::RALO_ON) && mission::ralOut(w, a)) || a.hp < policy::RETREAT_HP * a.maxHp) continue;   /* RAL */   /* TLH: 물러날 몸은 싸움 힘 아님 */
            if (a.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING)) continue;   /* PLT */
            const int etaA = (int)std::ceil((float)std::max(0, cheb(a.pos, tg->pos) - (std::max(1, a.range) + 1)) / moveSpeed(a.type));   /* WKD3 */ if (etaA > policy::MJG_ARRIVE) continue; const float hEff = a.hp - dE * (float)etaA; if (hEff <= 0.f) continue;   /* PLT: 도착 전 맞을 피해를 뺀 체력 (TSA 와 같은 꼴) */
            hA += hEff; dA += fnDps(a.type, a.str, a.focus); stU += (float)a.gems + kv; ++T.nA; }
        T.dA = dA;   /* LFD */
        const float Rl = (hA * dA) / std::max(1.f, hE * dE * dpcK(tg->pos)); T.s = T.nA > 0 ? Rl / (1.f + Rl) : 0.f;
        if (policy::GANK_ON) stU *= std::min(1.f, (float)T.nE / (float)std::max(1, T.nA));
        T.E = T.s * stE - (1.f - T.s) * stU - (policy::PLV_OPP ? (float)(T.nA - T.nE) * policy::PLV_T * policy::PLV_V : 0.f);
    }
    s_r = w.round; s_t = T; return T;
}
float unitDps(int type, float str, float focus) { return fnDps(type, str, focus); }   /* R3L */
void dwlSums(const World& w, float& hA, float& dA, float& hE, float& dE) {   /* DWL: 거리 가중 1/(1+2d) 란체스터 (반경 10, 보이는 몸, 나 포함) */
    hA = w.me.hp; dA = unitDps(w.me.type, w.me.str, w.me.focus); hE = 0.f; dE = 0.f;
    for (const auto& a : w.allies) { if (!a.visible || !a.alive) continue; const int dd = cheb(a.pos, w.me.pos); if (dd > 10) continue; const float k = 1.f / (1.f + 2.f * (float)dd); hA += a.hp * k; dA += unitDps(a.type, a.str, a.focus) * k; }
    for (const auto& e : w.enemies) { if (!e.alive) continue; const int dd = cheb(e.pos, w.me.pos); if (dd > 10) continue; const float k = 1.f / (1.f + 2.f * (float)dd); hE += e.hp * k; dE += unitDps(e.type, e.str, e.focus) * k; }
}
float dwlL(const World& w) { float a1, a2, e1, e2; dwlSums(w, a1, a2, e1, e2); return std::log((a1 * a2 + 1.f) / (e1 * e2 + 1.f)); }   /* RTK: 새 예측 로그 비 */
float fightKillRounds(const World& w) {   /* LFD: 팀 싸움 표적을 잡기까지 라운드 (표적 없음 = -1) */
    const TFight T = teamFight(w); if (!T.tg || T.dA <= 0.f) return -1.f; return T.tg->hp / T.dA;
}
float fightOdds(const World& w) {   /* JNA: PL 과 같은 셈 */
    { const TFight T = teamFight(w); if (policy::TFD_ON) { if (policy::BH3_TD2_ON) { if (!T.tg) return 1.f; if (T.nA > 0) return T.s; } else return T.tg ? T.s : 1.f; } }   /* BH3 TD2: 우리 참여 0 이면 아래 내 곁 국지 셈 */   /* TFD: 팀 승산 */
    const Me& me = w.me; float hE = 0.f, dE = 0.f; const Unit* tg = nullptr;
    for (const auto& e : w.enemies) { if (!e.alive || cheb(e.pos, me.pos) > policy::PL_R || onEnemySpawn(e.pos)) continue; hE += e.hp; dE += fnDps(e.type, e.str, e.focus); if (!tg || e.hp < tg->hp) tg = &e; }
    if (!tg) return 1.f;
    float hA = me.hp, dA = fnDps(me.type, me.str, me.focus);
    for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, me.pos) <= policy::PL_R && cheb(a.pos, tg->pos) <= policy::PL_R) { hA += a.hp; dA += fnDps(a.type, a.str, a.focus); }
    const float Rl = (hA * dA) / std::max(1.f, hE * dE * dpcK(tg->pos)); return Rl / (1.f + Rl);
}   // SPF2
static void buildTargets(const World& w, const mission::Mission& Mi, std::vector<Target>& out) {
    out.clear();
    const int shareN = 1 + (int)w.allies.size();
    for (const auto& e : w.enemies) {
        Target t; t.kind = 1; t.handle = e.handle; t.pos = e.pos; t.hp = e.hp; t.maxHp = e.maxHp; t.group = -1;
        t.full = (float)e.gems + ((float)S().killGold / mission::goldDiv()) + policy::DEATH_TIME_GEMS + enemyTwinAdd(w, e);   /* BH4_MC6 */
        t.channeling = (e.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING)) != 0;
        if (policy::FOC2_ON) for (const auto& a : w.allies) if (a.visible && a.alive && !a.acted && cheb(a.pos, e.pos) <= (a.range > 1 ? a.range : 2)) t.ally += a.str;   /* KAT: 이번 R 아직 안 둔 아군이 닿는 평타 (근접은 걷고 침) — 이미 둔 아군 피해는 체력에 이미 빠짐 */   /* FOC2 · KAL: 아직 안 둔 아군만 */
        t.hitVal = champValue(t, w.me.str); out.push_back(t);
    }
    const Pos fcsP = (policy::FCS_ON && Mi.kind == mission::M_FARM) ? mission::focusCamp(w, Mi.group) : Pos{-1, -1};   /* FCS */
    const bool fcsIn = fcsP.x >= 0 && cheb(fcsP, w.me.pos) <= std::max(1, w.me.range);
    static std::vector<int> s_xgS; static std::vector<std::uint8_t> s_xgU, s_xgH; static int s_xgStamp = 0;   /* CSX ①: 무리마다 한 번 */
    if (s_xgS.size() != S().groups.size()) { s_xgS.assign(S().groups.size(), 0); s_xgU.assign(S().groups.size(), 0); s_xgH.assign(S().groups.size(), 0); }
    ++s_xgStamp;
    for (const auto& k : w.camps) {
        const bool cdFb = g_plEnemy != nullptr || g_nfHold;   /* CDS: PL · NF 중 캠프는 평타 · 대시 표적이 아니고, 닿는 챔피언이 없을 때 기술 표적만 */
        if (g_vulHoldG >= 0 && k.group == g_vulHoldG) continue;   /* VUL: 기다림 — 다투는 그룹 캠프는 치지 않음 */
        const Group& G = S().groups[(size_t)k.group];
        // 내 임무 그룹이 아니면 치지 않는다 — 그룹에 힘 있는 캠프가 하나라도 있으면 (클러스터만 쳐도 엘크가 깬다: Cassi 가 두 엘크 사이에서 죽음). 순수 클러스터 그룹만 예외
        if (!(Mi.kind == mission::M_FARM && k.group == Mi.group) && (G.strMax > 0.f || policy::NOX_ON) && !(policy::FHT_ON && G.awakeUntil >= w.round && Mi.kind != mission::M_RECOVER)) continue;   /* FHT: 깨어 있는 무리는 지나가며 침 */
        if (policy::CSX_ON) { const size_t xg = (size_t)k.group; if (s_xgS[xg] != s_xgStamp) { s_xgS[xg] = s_xgStamp; s_xgU[xg] = (policy::NFX_ON && mission::groupUnreachable(k.group)) ? 1 : 0; s_xgH[xg] = (!s_xgU[xg] && mission::tfpHold(w, k.group)) ? 1 : 0; }
            if (s_xgU[xg] || s_xgH[xg]) continue; } else {   /* CSX ① */
        if (policy::NFX_ON && mission::groupUnreachable(k.group)) continue;   /* NFX: 걸어서 못 가는 그룹은 치지 않음 */
        if (mission::tfpHold(w, k.group)) continue;   /* TFP2: 모이기 전 */ }
        if (fcsIn && k.group == Mi.group && !(k.pos == fcsP)) continue;   /* FCS: 1순위가 사거리 안이면 같은 그룹 다른 캠프는 안 침 */
        Target t; t.kind = 2; t.handle = k.handle; t.pos = k.pos; t.hp = k.hp; t.maxHp = std::max(1.f, G.hpTotal); t.group = k.group; t.channeling = false;
        t.full = ((float)G.gems + (float)G.gold / mission::goldDiv()) / (float)std::max(1, shareN);
        t.fb = cdFb; t.hitVal = campValue(t, w.me.str); out.push_back(t);
    }
    if (policy::VSP_ON && S().designOf(w.me.type).valid && S().designOf(w.me.type).passive == 4 && io::fuel() > 30000) {   /* VSP: 튐 평타 (splash_attack) */
        const float sv = w.me.str;
        static std::vector<int> s_xcS, s_xcI; static int s_xcStamp = 0; if (policy::CSX_ON) { if (s_xcS.size() != (size_t)(S().W * S().H)) { s_xcS.assign((size_t)(S().W * S().H), 0); s_xcI.assign((size_t)(S().W * S().H), -1); } ++s_xcStamp;   /* CSX ② */
            for (size_t ki = 0; ki < w.camps.size(); ++ki) { const Pos q = w.camps[ki].pos; if (!S().inb(q)) continue; const size_t qi = (size_t)S().idx(q); if (s_xcS[qi] != s_xcStamp) { s_xcS[qi] = s_xcStamp; s_xcI[qi] = (int)ki; } } }
        for (auto& tg : out) { float add = 0.f;
            for (const auto& e : w.enemies) { if (e.handle == tg.handle || cheb(e.pos, tg.pos) > 1) continue;
                Target u; u.kind = 1; u.handle = e.handle; u.pos = e.pos; u.hp = e.hp; u.maxHp = e.maxHp; u.group = -1; u.channeling = false;
                u.full = (float)e.gems + ((float)S().killGold / mission::goldDiv()) + policy::DEATH_TIME_GEMS + enemyTwinAdd(w, e); add += champValue(u, sv); }   /* BH4_MC6 */
            int xn = 0; int xi[9];   /* CSX ②: 둘레 3x3 캠프 번호 (오름차순) */
            if (policy::CSX_ON) { for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos q{tg.pos.x + dx, tg.pos.y + dy}; if (!S().inb(q)) continue; const size_t qi = (size_t)S().idx(q); if (s_xcS[qi] == s_xcStamp) xi[xn++] = s_xcI[qi]; }
                for (int a2 = 1; a2 < xn; ++a2) for (int b2 = a2; b2 > 0 && xi[b2] < xi[b2 - 1]; --b2) std::swap(xi[b2], xi[b2 - 1]); }
            for (int xk = 0; xk < (policy::CSX_ON ? xn : (int)w.camps.size()); ++xk) { const auto& k = w.camps[(size_t)(policy::CSX_ON ? xi[xk] : xk)]; if (k.handle == tg.handle || cheb(k.pos, tg.pos) > 1 || k.group < 0) continue;
                const Group& KG = S().groups[(size_t)k.group]; if (!(k.group == tg.group || KG.awakeUntil >= w.round)) continue;
                Target u; u.kind = 2; u.handle = k.handle; u.pos = k.pos; u.hp = k.hp; u.maxHp = std::max(1.f, KG.hpTotal); u.group = k.group; u.channeling = false;
                u.full = ((float)KG.gems + (float)KG.gold / mission::goldDiv()) / (float)std::max(1, shareN); add += campValue(u, sv); }
            tg.hitVal += add; }
    }
}


// MJG: voluntary support of a visible ally in actual contact. No remote orders,
// no changes to mission/channel priorities or the final lethal/R2 safety filters.
static bool twinOf(const World& w, const Unit& a) { return (w.me.type == 3 || w.me.type == 4) && a.type == 7 - w.me.type; }   /* TWN: Cassi · Issac 는 함께 죽음 (룰북) */
float twinStake(const World& w) {   /* TWN: 내가 죽으면 짝도 죽음 → 짝 젬 + 죽음 시간 */
    if (w.me.type != 3 && w.me.type != 4) return 0.f;
    for (const auto& a : w.allies) if (a.visible && a.alive && twinOf(w, a)) return (float)a.gems + policy::PLV_DEATH;
    return policy::PLV_DEATH; }
static const Unit* marginalJoinTarget(const World& w) {
    const auto& me = w.me;
    if (!policy::MJG_ON || w.allies.empty()) return nullptr;   /* TFV: 체력 문턱 없음 (셈 안의 승산 · 판돈으로) */
    const float myDps = fnDps(me.type, me.str, me.focus);
    const float stake = (float)S().killGold / mission::goldDiv() + policy::PLV_DEATH;
    const Unit* best = nullptr; float bestGain = 0.f; int bestEta = 999;
    const auto unavailable = rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING;
    for (const auto& target : w.enemies) {
        if (!target.alive || onEnemySpawn(target.pos)) continue;
        const int eta = (int)std::ceil((float)std::max(0, cheb(me.pos, target.pos) - std::max(1, me.range)) / moveSpeed(me.type));   /* WKD3 */
        if (eta > (int)policy::PLV_T) continue;   /* MJE: 도착 한도 = 팀 식 지평 (만료 · 걷기 기회비용은 식이 거름) */
        bool contact = false;
        float hA = 0.f, dA = 0.f, stA = 0.f;
        for (const auto& a : w.allies) {
            if (!a.visible || !a.alive || a.hp <= 0.f || (a.state & unavailable)) continue;
            // Only currently close combatants; don't instantly add distant allies.
            const int d = cheb(a.pos, target.pos);
            if (d > std::max(std::max(1, a.range), std::max(1, target.range)) + 1) continue;
            contact = true; hA += a.hp; dA += fnDps(a.type, a.str, a.focus);
            stA += (float)a.gems + stake + (twinOf(w, a) ? (float)me.gems + policy::PLV_DEATH : 0.f);   /* TWN: 짝이 죽으면 나도 */
        }
        if (!contact) continue;
        float hE = 0.f, dE = 0.f, stE = 0.f;
        for (const auto& e : w.enemies) {
            if (!e.alive || onEnemySpawn(e.pos) || cheb(e.pos, target.pos) > policy::PL_R) continue;
            hE += e.hp; dE += fnDps(e.type, e.str, e.focus); stE += enemyStakeIn(w, e, target.pos, policy::PL_R, stake);   /* BH4_MC6 */
        }
        const auto r = marginal_join::evaluate({hA, dA, hE, dE, dpcK(target.pos), stA, stE,
            me.hp, myDps, (float)me.gems + stake + twinStake(w), eta, (int)policy::PLV_T,
            policy::PLV_T, policy::PLV_V});
        float gainX = r.eligible ? r.gain : -1e9f;   /* FTJ */
        if (target.hp < 0.4f * target.maxHp) { static const float TX[6] = {0.25f, 0.75f, 1.25f, 1.75f, 2.5f, 3.5f}, PK[6] = {0.58f, 0.51f, 0.35f, 0.30f, 0.20f, 0.08f};
            float bA = 0.f; for (const auto& a : w.allies) if (a.visible && a.alive && !(a.state & unavailable) && cheb(a.pos, target.pos) <= std::max(1, a.range) + 1) bA += fnDps(a.type, a.str, a.focus);
            const float pk0 = bA > 0.f ? fbLerp(TX, PK, 6, target.hp / bA) : 0.f, pk1 = fbLerp(TX, PK, 6, target.hp / std::max(1.f, bA + myDps));
            gainX = std::max(gainX, (pk1 - pk0) * ((float)target.gems + stake) - (2.f * (float)eta + 1.f) * policy::PLV_V); }   /* FTJ: 저체력 적 마무리 합류 값 */
        if (gainX <= 0.f) continue;
        const float rgain = gainX;
        if (!best || rgain > bestGain || (rgain == bestGain &&
            (eta < bestEta || (eta == bestEta && target.handle < best->handle)))) {
            best = &target; bestGain = rgain; bestEta = eta;
        }
    }
    return best;
}

float depthK(Pos p) { return dpcK(p); }   /* SDP: 팀 식과 같은 깊이 보정을 밖에서도 */
bool joinTargetNow(const World& w) { return policy::MJG_ON && marginalJoinTarget(w) != nullptr; }   /* JNL: 곁 싸움에 끼면 이득인 적이 있나 */
float teamStayGain(const World& w) {   /* TFV: 이미 싸움터에 있는 나의 한계 값 (marginal_join, 도착 0) — 접촉 아군이 없으면 -1e9 */
    static int s_r = -1; static float s_v = -1e9f; if (s_r == w.round) return s_v; s_r = w.round; s_v = -1e9f;
    const auto& me = w.me; const float myDps = fnDps(me.type, me.str, me.focus); const float stake = (float)S().killGold / mission::goldDiv() + policy::PLV_DEATH;
    const auto unavailable = rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING;
    for (const auto& target : w.enemies) {
        if (!target.alive || onEnemySpawn(target.pos) || cheb(me.pos, target.pos) > std::max(1, me.range) + 1) continue;   // 내가 지금 닿는 적만 (도착 0)
        float hA = 0.f, dA = 0.f, stA = 0.f; bool contact = false;
        struct Arr { float hp, dps; int eta; }; Arr arr[8]; int nArr = 0;   /* TSA: 오는 아군 */
        for (const auto& a : w.allies) { if (!a.visible || !a.alive || a.hp <= 0.f || (a.state & unavailable)) continue;
            const int reachA = std::max(std::max(1, a.range), std::max(1, target.range)) + 1, dT = cheb(a.pos, target.pos);
            if (dT > reachA) { { const int etaT = (int)std::ceil((float)(dT - reachA) / moveSpeed(a.type)); if (etaT <= policy::MJG_ARRIVE && nArr < 8) arr[nArr++] = {a.hp, fnDps(a.type, a.str, a.focus), etaT}; }   /* WKD3 */ continue; }
            contact = true; hA += a.hp; dA += fnDps(a.type, a.str, a.focus); stA += (float)a.gems + stake + (twinOf(w, a) ? (float)me.gems + policy::PLV_DEATH : 0.f); }   /* TWN */
        if (!contact) continue;
        float hE = 0.f, dE = 0.f, stE = 0.f;
        for (const auto& e : w.enemies) { if (!e.alive || onEnemySpawn(e.pos) || cheb(e.pos, target.pos) > policy::PL_R) continue; hE += e.hp; dE += fnDps(e.type, e.str, e.focus); stE += enemyStakeIn(w, e, target.pos, policy::PL_R, stake); }   /* BH4_MC6 */
        for (int q = 0; q < nArr; ++q) { const float h = arr[q].hp - dE * (float)arr[q].eta; if (h > 0.f) { hA += h; dA += arr[q].dps; } }   /* TSA: 도착 전 맞을 피해를 뺀 체력 */
        const auto r = marginal_join::evaluate({hA, dA, hE, dE, dpcK(target.pos), stA, stE, me.hp, myDps, (float)me.gems + stake + twinStake(w), 0, policy::MJG_ARRIVE, policy::PLV_T, policy::PLV_V});
        if (r.eligible) s_v = std::max(s_v, r.gain); }
    return s_v; }
static long long g_lzdComb = 1500;   /* LZD2: 칸 하나 조합 비용 (잰 값; 시작값은 기록 판 평균 6.4k / 9.5칸 근처) */
Plan choose(const World& w, mission::Mission& Mi) {
    const long long f0_ = io::fuel();
    Plan best; const Me& me = w.me; best.cell = me.pos;
    if (io::fuel() < 18000) { best.lethalAll = true; best.dmgTaken = me.hp; return best; }
    const float gph = gemPerHp(me.gems, me.maxHp);
    const ChampDesign& D = S().designOf(me.type);
    const bool ranged = me.range > 1;
    static std::vector<Threat> threats; static std::vector<CampSrc> srcs; static std::vector<Target> targets;
    g_plEnemy = nullptr;
    const bool plMis = Mi.kind == mission::M_FARM || Mi.kind == mission::M_CAPTURE || Mi.kind == mission::M_NONE || Mi.kind == mission::M_ESCORT;
    if (policy::PL_ON && plMis) {   // PL: 싸움 기대 손익 (젬) = s × 적 판돈 − (1 − s) × 우리 판돈
        if (policy::TFD_ON) { const TFight T = teamFight(w);   /* TFD: 팀이 싸우면 나도 (기준 적 = 같은 표적) */
            const bool jnaNow = std::strcmp(mission::M.why, "join-ally") == 0;   /* JNP: 밀려 합류를 고른 턴엔 걸지 않음 */
            if (T.tg && !jnaNow && cheb(me.pos, T.tg->pos) <= policy::TFD_R && T.E > policy::PL_MARGIN) { g_plEnemy = T.tg; g_plCall = team::zoneOf(T.tg->pos); g_plCallRound = w.round; }
        } else {
        float hEs = 0.f, dEs = 0.f, hAs = me.hp, dAs = fnDps(me.type, me.str, me.focus);   /* PLS */
        float stE = 0.f, stU = (float)me.gems + ((float)S().killGold / mission::goldDiv()) + policy::PLV_DEATH; int plvA = 1, plvE = 0;   /* PLV */
        for (const auto& e : w.enemies) { if (cheb(e.pos, me.pos) > policy::PL_R || onEnemySpawn(e.pos)) continue;   /* SPF2 */ hEs += e.hp; dEs += fnDps(e.type, e.str, e.focus); stE += (float)e.gems + ((float)S().killGold / mission::goldDiv()) + policy::PLV_DEATH; ++plvE; if (!g_plEnemy || e.hp < g_plEnemy->hp) g_plEnemy = &e; }
        if (g_plEnemy) {
            for (const auto& a : w.allies) if (cheb(a.pos, me.pos) <= policy::PL_R && (!policy::PLJ_ON || cheb(a.pos, g_plEnemy->pos) <= policy::PL_R)) { hAs += a.hp; dAs += fnDps(a.type, a.str, a.focus);   /* PLJ */ stU += (float)a.gems + ((float)S().killGold / mission::goldDiv()) + policy::PLV_DEATH; ++plvA; }
            const float Rl = (hAs * dAs) / std::max(1.f, hEs * dEs * dpcK(g_plEnemy->pos)), s = Rl / (1.f + Rl);   /* PLS: 이길 확률 = 란체스터 (DPC 깊이) */
            if (policy::GANK_ON) stU *= std::min(1.f, (float)plvE / (float)std::max(1, plvA));   /* GANK: 잃을 수 있는 몸은 적 수만큼 */
            const float E = s * stE - (1.f - s) * stU - (policy::PLV_OPP ? (float)(plvA - plvE) * policy::PLV_T * policy::PLV_V : 0.f);   // PLV: 농사 기회비용
            if (E > policy::PL_MARGIN) { g_plCall = team::zoneOf(g_plEnemy->pos); g_plCallRound = w.round; } else g_plEnemy = nullptr;
        }
        }
        if (!g_plEnemy && policy::CHS_ON && g_chsH && w.round - g_chsR <= 1) {   /* CHS: 지난 턴 싸움 표적이 체력 절반 아래로 도망 — 다음 턴 칠 수 있는 거리면 이어서 쫓음 */
            int reach = std::max(1, me.range) + 1; const ChampDesign& Dc = S().designOf(me.type);
            if (Dc.valid) for (int s2 = 0; s2 < 2; ++s2) if (Dc.ab[s2].kind == rules::AB_DASH && me.cooldown[s2] == 0) reach = std::max(reach, std::max(1, me.range) + (int)Dc.ab[s2].range);
            for (const auto& e : w.enemies) if (e.handle == g_chsH && e.alive && e.visible && !onEnemySpawn(e.pos) && e.hp < policy::LOW_HP_FRAC * e.maxHp && cheb(e.pos, me.pos) <= reach) { g_plEnemy = &e; break; } }
        if (!g_plEnemy && policy::MJG_ON) g_plEnemy = marginalJoinTarget(w);
        if (!policy::MJG_ON && !g_plEnemy && policy::JOIN4_ON && me.hp >= policy::JOIN_HP * me.maxHp && mission::r3Ratio(w) * policy::R3_RATIO > 1.f) {   /* JOIN4: 합류도 같은 손익 */
            const float kg = (float)S().killGold / mission::goldDiv();
            for (int k = 0; k < team::T.n && !g_plEnemy; ++k) { if (k == team::T.slot) continue; const auto& mt = team::T.mate[k]; if (mt.fightZone < 0 || w.round - mt.fightRound > 2) continue;
                const Pos zc = team::zoneCenter(mt.fightZone); if (cheb(zc, me.pos) > policy::JOIN4_R + 4) continue;
                const Unit* an = nullptr; for (const auto& a : w.allies) if (a.visible && a.alive && (!an || cheb(a.pos, zc) < cheb(an->pos, zc))) an = &a;
                if (!an || cheb(an->pos, zc) > 6) continue;
                float hE = 0.f, dE = 0.f, stE = 0.f, hA = me.hp, dA = fnDps(me.type, me.str, me.focus), stU = (float)me.gems + kg + policy::PLV_DEATH; int nE = 0, nA = 1; const Unit* tg = nullptr;
                for (const auto& e : w.enemies) { if (!e.alive || onEnemySpawn(e.pos) || cheb(e.pos, an->pos) > policy::PL_R || cheb(e.pos, me.pos) > policy::JOIN4_R) continue;
                    hE += e.hp; dE += fnDps(e.type, e.str, e.focus); stE += (float)e.gems + kg + policy::PLV_DEATH; ++nE; if (!tg || e.hp < tg->hp) tg = &e; }
                if (!tg) continue;
                for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, an->pos) <= policy::PL_R) { hA += a.hp; dA += fnDps(a.type, a.str, a.focus); stU += (float)a.gems + kg + policy::PLV_DEATH; ++nA; }
                const float hA0 = hA - me.hp, dA0 = dA - fnDps(me.type, me.str, me.focus);
                if (policy::JOIN5_ON && hA0 * dA0 < hE * dE * dpcK(tg->pos)) continue;   /* JOIN5: 나 없이도 이기거나 비기는 싸움만 */
                const float Rl = (hA * dA) / std::max(1.f, hE * dE * dpcK(tg->pos)), s = Rl / (1.f + Rl);
                const float E = s * stE - (1.f - s) * stU - (policy::PLV_OPP ? (float)(nA - nE) * policy::PLV_T * policy::PLV_V : 0.f);
                if (E > policy::PL_MARGIN) g_plEnemy = tg;
            }
        }
    }
    if (policy::CHS_ON && g_plEnemy) { g_chsH = g_plEnemy->handle; g_chsR = w.round; }   /* CHS */
    g_vulHoldG = -1; g_vulMode = 0; g_vulN = 0;
    if (policy::VUL_ON && Mi.kind == mission::M_FARM && Mi.group >= 0 && !w.enemies.empty() && io::fuel() > 30000) {   /* VUL3: 싸게 — 그룹 캠프마다 가장 가까운 적 · 아군 거리를 한 번만 */
        const int G0 = Mi.group; const Group& VG = S().groups[(size_t)G0];
        if (VG.state == 1 && VG.strMax > 0.f) {
            const bool gAwake = VG.awakeUntil >= w.round;
            for (const auto& k : w.camps) { if (k.group != G0 || k.str <= 0.f || g_vulN >= 16) continue;
                VulCamp vc; vc.pos = k.pos; vc.R = S().campRange(k.type); vc.awake = gAwake || k.hp < k.maxHp; vc.dE = 99; vc.dA = 99; vc.eIdx = -1;
                for (int ei = 0; ei < (int)w.enemies.size(); ++ei) { const auto& e = w.enemies[(size_t)ei]; if (!e.alive) continue; const int d = cheb(k.pos, e.pos); if (d < vc.dE) { vc.dE = d; vc.eIdx = ei; } else if (d == vc.dE) vc.eIdx = -2; }
                for (const auto& a : w.allies) if (a.visible && a.alive) vc.dA = std::min(vc.dA, cheb(k.pos, a.pos));
                g_vulCamps[g_vulN++] = vc; }
            bool contested = false, meAg = false; const int dMeAll = 0; (void)dMeAll;
            for (int q = 0; q < g_vulN; ++q) { const VulCamp& vc = g_vulCamps[q]; const int dm = cheb(vc.pos, me.pos);
                if (vc.awake && vc.eIdx >= 0 && vc.dE <= vc.R && vc.dE < std::min(dm, vc.dA)) contested = true;   /* 적이 이 깨어 있는 캠프의 어그로 대상 = 실제로 맞는 중 */
                if (vc.awake && dm <= vc.R && dm <= vc.dE && dm <= vc.dA) meAg = true; }   /* 내가 어그로 대상 (동점은 나) */
            bool yieldAg = false;
            if (!contested && meAg) for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, me.pos) <= std::max(1, e.range) + 2) { yieldAg = true; break; }
            if (contested) {
                const Unit* kill = nullptr;
                for (const auto& e : w.enemies) { if (!e.alive || cheb(e.pos, me.pos) > policy::PL_R) continue;
                    float burst = fnDps(me.type, me.str, me.focus), op = me.hp * burst, ep = 0.f;
                    for (const auto& a : w.allies) { if (!a.visible || !a.alive) continue; const float d = fnDps(a.type, a.str, a.focus); if (cheb(a.pos, e.pos) <= std::max(1, a.range) + 1) burst += d; if (cheb(a.pos, e.pos) <= 4) op += a.hp * d; }
                    if (e.hp > 2.f * burst + 2.f * 400.f) continue;   /* 싼 거름: 캠프 피해 최대치로도 안 되면 건너뜀 */
                    for (const auto& o : w.enemies) if (o.alive && cheb(o.pos, e.pos) <= 4) ep += o.hp * fnDps(o.type, o.str, o.focus);
                    if (op < ep) continue;
                    if (e.hp - 2.f * vulCampOn(w, e) <= 2.f * burst && (!kill || e.hp < kill->hp)) kill = &e; }
                if (kill) { g_vulMode = 2; g_plEnemy = kill; g_plCall = team::zoneOf(kill->pos); g_plCallRound = w.round; }
                else { float dG = fnDps(me.type, me.str, me.focus); for (const auto& a : w.allies) if (a.visible && a.alive && cheb(a.pos, VG.center) <= VG.rangeMax + 2) dG += fnDps(a.type, a.str, a.focus);
                    if (mission::groupHpLeft(G0) <= 2.f * dG) g_vulMode = 3; else { g_vulMode = 1; g_vulHoldG = G0; } }
            } else if (false && yieldAg) { g_vulMode = 1; g_vulHoldG = G0; }   /* YAG0 */
        }
    }
    { bool nfN = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, w.me.pos) <= policy::NF_R) { nfN = true; break; } g_nfHold = policy::NF_ON && nfN && mission::r3Ratio(w) * policy::R3_RATIO <= 1.f; }   /* NF · ENG3: 적 NF_R 안 + 후퇴와 같은 잣대로 열세면 캠프를 치지 않음 */
    if (policy::FS_COM3_ON) threats = fs3Threats(w); else buildThreats(w, threats); campSources(w, Mi, srcs);   /* FS_COM3 */ buildTargets(w, Mi, targets); g_th5Scene = false;   /* BH3_TH5 */
    if (io::fuel() < 16000) { best.lethalAll = true; best.dmgTaken = me.hp; return best; }
    const bool fightOn = !threats.empty() || !srcs.empty();

    // 1. 칸 집합
    static std::vector<Cell> cells; cells.clear();
    cells.push_back({me.pos});
    { const int gi = S().campGroupAt[(size_t)S().idx(me.pos)]; if (gi >= 0 && !(Mi.kind == mission::M_CAPTURE && Mi.group == gi) && !(policy::CPS_ON && S().groups[(size_t)gi].state == 2)) cells[0].prog = -5.f; }   /* CPS: 보상 대기 칸은 비키지 않음 */   // 스폰 칸 비키기
    if (me.canMove) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        if (!dx && !dy) continue; const Pos p{me.pos.x + dx, me.pos.y + dy};
        if (S().inb(p) && freeTile(w, p, Mi)) cells.push_back({p});
    }
    int dashSlot = -1;
    if (me.canCast && D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH && me.cooldown[s] == 0) dashSlot = s;
    if (policy::DFB_ON && w.round - s_dashFailR <= 3) dashSlot = -1;   /* DFB: 막 실패한 대시는 잠시 안 고름 */
    if (dashSlot >= 0 && !fightOn && mission::bridgeAhead(w, D.ab[dashSlot].cooldown - 2)) dashSlot = -1;   /* BRK: 앞에 건널 다리가 있으면 대시를 아껴 둠 */
    bool hdfCamp = false;   /* HDF: 피해 대시면 3칸 안 칠 캠프 */
    if (policy::HDF_ON && dashSlot >= 0 && policy::HD_ON && D.ab[dashSlot].modifier > 0.f) for (const auto& k : w.camps) { if (k.hp <= 0.f || k.group < 0 || cheb(k.pos, me.pos) > 3) continue;
        if (k.group == Mi.group || S().groups[(size_t)k.group].awakeUntil >= w.round) { hdfCamp = true; break; } }
    const bool lzdWant = dashSlot >= 0 && io::fuel() > 30000 && (fightOn || mission::stepsToGoal(me.pos) >= 4 || hdfCamp);   /* LZD: 만들지는 여기서 정함 */
    const bool lzdWantW = policy::DWK_ON && dashSlot >= 0 && me.canMove && io::fuel() > 45000 && !fightOn && mission::stepsToGoal(me.pos) >= 4;
    const bool lzdLazy = policy::LZD_ON && !(policy::CEF_ON && policy::R_RETREAT && Mi.kind == mission::M_RECOVER && !threats.empty());
    auto lzdBuild = [&]() {
    if (lzdWant) for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        { const int dd = std::max(std::abs(dx), std::abs(dy)); if (dd == 0 || (dd == 1 && !(policy::HD_ON && D.ab[dashSlot].modifier > 0.f)) || dd > 2) continue; } const Pos p{me.pos.x + dx, me.pos.y + dy};   // HD: 피해 대시는 1칸 착지도
        if (!S().inb(p) || !freeTile(w, p, Mi) || !io::seesTile(p)) continue;   /* DNV: 안 보이는 칸으로 대시 안 함 */
        // 대시 길: 출발 · 도착에 붙은 중간 칸 하나가 벽 · 제단 · 적 스폰 · 유닛 · 산 캠프가 아니어야 (구멍은 넘음)
        if (me.range > 1) { bool nearE = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, p) < me.range) { nearE = true; break; } if (nearE) continue; }   /* DKR: 원거리는 카이팅 사거리 밖에 착지 */
        if (dashPathFree(w, p)) cells.push_back({p, true, dashSlot});
    }
    if (lzdWantW) {   /* DWK2: 이동할 때만 */   /* DWK: 대시 뒤 걷기 한 칸 (지금 칸에서 3칸) */
        const size_t nBase = cells.size(); std::uint32_t seen[2] = {0u, 0u};
        for (size_t ci = 0; ci < nBase; ++ci) { const Cell c0 = cells[ci]; if (!c0.dash || cheb(c0.p, me.pos) != 2) continue;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { const Pos q{c0.p.x + dx, c0.p.y + dy};
                const int ox = q.x - me.pos.x, oy = q.y - me.pos.y; if (std::max(std::abs(ox), std::abs(oy)) != 3) continue;
                const int bit = (oy + 3) * 7 + (ox + 3); if (bit < 0 || bit >= 49) continue; if (seen[bit >> 5] & (1u << (bit & 31))) continue;
                if (!S().inb(q) || !freeTile(w, q, Mi) || !io::seesTile(q)) continue;   /* DNV */
                if (me.range > 1) { bool nearE = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, q) < me.range) { nearE = true; break; } if (nearE) continue; }   /* DKR 와 같음 */
                seen[bit >> 5] |= (1u << (bit & 31)); Cell cw{q, true, dashSlot}; cw.mid = c0.p; cw.dw = true; cells.push_back(cw); } }
    }
    };   /* LZD */
    if (!lzdLazy) lzdBuild();
    const long long fA = io::fuel();

    // 2. 칸마다 받을 피해 · 진행. 한 걸음의 값 = 임무 가치 / (남은 걸음 + 끝내는 턴)
    const int d0 = mission::stepsToGoal(me.pos);
    float mv = mission::missionGems(w);
    const int work = mission::missionWorkTurns(w);
    if (!policy::IDC_ON && !threats.empty() && (Mi.kind == mission::M_FARM || Mi.kind == mission::M_CAPTURE)) {   // 적이 보이면 임무를 끝낼 가망이 준다   /* IDC: 끔 — 같은 위험을 임무 · 칸 층이 이미 셈 */
        float dps = 0.f; for (size_t ti = 0; ti < threats.size() && ti < w.enemies.size(); ++ti) if (cheb(threats[ti].pos, me.pos) <= rules::VISION && (!policy::VIS_ON || mission::threatening(w, w.enemies[ti]))) dps += threats[ti].hit;   /* VIS */
        mv *= std::clamp(1.f - dps * (float)work / std::max(1.f, me.hp), 0.f, 1.f);
    }
    const float stepW = (Mi.kind != mission::M_NONE && d0 < 999 && mv > 0.f) ? std::max(policy::GEM_PER_STEP, mv / (float)(d0 + work + 1)) : policy::GEM_PER_STEP;
    if (g_lastPos == me.pos && d0 > 0 && Mi.kind != mission::M_NONE) ++g_stuckTurns; else g_stuckTurns = 0;
    g_lastPos = me.pos;
    best.lethalAll = fightOn;
    bool noEnter = false;   // R2
    const bool rcvOn = policy::RCV_ON && policy::R_RETREAT && Mi.kind == mission::M_RECOVER && !threats.empty() && threatAt(threats, me.pos) > 0.f && mission::r3Outnumbered(w);   /* RCV */
    if (policy::R2_NO_ENTER && !threats.empty()) noEnter = mission::r3Outnumbered(w) && threatAt(threats, me.pos) <= 0.f;
    if (policy::LONE_ON && !noEnter && !threats.empty() && threatAt(threats, me.pos) <= 0.f && mission::loneOutnumbered(w)) noEnter = true;   /* LONE */   /* R3: 후퇴와 같은 힘 비교 */
    // M_FINISH: 캠프 싸움 (적 없음) 은 끝낼 때까지의 피해로 죽음 위험을 본다 — 지금 인원 (6칸 안 아군 + 나) 으로 남은 그룹 체력을 깎는 턴 수
    float finishTurns = 0.f;
    bool campOnly = true; for (const auto& e : w.enemies) if (cheb(e.pos, me.pos) <= policy::AP2_R && (!policy::VIS_ON || mission::threatening(w, e))) campOnly = false;   // AP2 /* VIS */
    g_th5Scene = policy::BH3_TH5_ON && Mi.kind == mission::M_FARM && campOnly && mission::cxeBackOff(); g_th5Me = ordKey(policy::TIE_ON ? io::priorityNow(w.me.handle) : 0, w.me.handle);   /* TIE */   /* BH3_TH5 */
    if (policy::M_FINISH && campOnly && Mi.kind == mission::M_FARM && Mi.group >= 0) {
        int n = 1; for (const auto& a : w.allies) if (cheb(a.pos, me.pos) <= 6) ++n;
        finishTurns = mission::groupHpLeft(Mi.group) / std::max(1.f, me.str * (float)n) + 1.f;
    }
    bool cefNoEscape = false;   /* CEF: 후퇴 중 갈 수 있는 칸 어디서나 보이는 적이 다음 턴에 닿으면 (사거리 + 걸음 1) 멀어지는 값 없음 → 받아치기 */
    if (policy::CEF_ON && policy::R_RETREAT && Mi.kind == mission::M_RECOVER && !threats.empty()) { cefNoEscape = true;
        for (const auto& c : cells) { bool safe = true; for (const auto& e : w.enemies) { if (!e.alive || !e.visible) continue; if (cheb(e.pos, c.p) <= std::max(1, e.range) + 1) { safe = false; break; } } if (safe) { cefNoEscape = false; break; } } }
    static std::vector<int> s_fsMC; static std::vector<char> s_fsRS, s_fsRV; static std::vector<int> s_fsRD; int s_fsDMe = 99; float s_fsHS = 0.f; int s_fsNT = 0;   /* FS_COM1 */
    if (policy::FS_COM1_ON && fightOn) { s_fsMC.assign(threats.size(), 1 << 30);
        for (size_t i = 0; i < threats.size(); ++i) { int m = 1 << 30; for (const auto& a : w.allies) if (a.visible && a.hp <= w.me.hp) { const int d = cheb(a.pos, threats[i].pos); if (d < m) m = d; } s_fsMC[i] = m; } }
    if (policy::FS_COM1_ON && policy::R_RETREAT && Mi.kind == mission::M_RECOVER && !threats.empty() && !cefNoEscape) {
        s_fsRS.assign(threats.size(), 0); s_fsRV.assign(threats.size(), 0); s_fsRD.assign(threats.size(), 0);
        for (size_t ti = 0; ti < threats.size(); ++ti) { const auto& t = threats[ti]; if (ti < w.enemies.size() && fbTarget(w, w.enemies[ti]) && w.enemies[ti].hp <= fbBurst(w, w.enemies[ti])) { s_fsRS[ti] = 1; continue; }
            const int dm = cheb(t.pos, me.pos); s_fsRD[ti] = dm; s_fsDMe = std::min(s_fsDMe, dm); if (dm <= rules::VISION) { s_fsHS += t.hit; ++s_fsNT; s_fsRV[ti] = 1; } }
    }
    size_t cellsDone = 0;
    long long cleMax = 0, cleT = io::fuel();   /* CLE: 칸 하나 최대 비용 */
    bool lzdDone = !lzdLazy;   /* LZD */
    size_t lzdNStep = (size_t)-1; long long lzdRes = 0;   /* LZD2: 걷기 칸 수 · 남길 몫 */
lzdLoop:
    for (size_t lzdI = cellsDone; lzdI < cells.size(); ++lzdI) { Cell& c = cells[lzdI];
        { const long long fn = io::fuel(); if (fn < cleT) cleMax = std::max(cleMax, cleT - fn); cleT = fn; }
        if (io::fuel() < 16000 + cleMax) break;
        if (lzdI >= lzdNStep && io::fuel() < lzdRes + cleMax) break;   /* LZD2: 대시 칸은 걷기 칸 조합 몫을 남기고 */
        ++cellsDone;
        if (fightOn) { const float ed = policy::FS_COM1_ON ? threatAtTgtMC(w, threats, s_fsMC, c.p) : threatAtTgt(w, threats, c.p); float kd = campThreat(srcs, c.p); c.fEd = ed; if (!policy::BH3_TH4_ON) c.fKd = kd;   /* LHF */
            if (policy::CCS_ON && me.type != 7 && kd > 0.f) for (const auto& t : threats) if (t.ccReach >= 0 && cheb(t.pos, c.p) <= t.ccReach) { kd *= 2.f; break; }   /* CCK: 묶인 라운드에 캠프도 한 번 더 */
            if (policy::BH3_TH4_ON) c.fKd = kd;   /* BH3_TH4: CCK 뒤 값 */
            c.dmg = ed + kd; }   /* TGT */
        { float cg = 0.f; for (const auto& k : srcs) { if (k.nearEnemy > k.range || k.nearEnemy >= k.nearAlly) continue; if (cheb(k.pos, c.p) <= k.nearEnemy) continue; cg += k.str * k.enVal; } if (policy::BH3_CE4_ON) c.cae = cg; c.prog += cg; }   /* CAE: 캠프가 상대를 침 */
        c.finishOk = (finishTurns > 0.f && c.dmg > 0.f && (me.hp - c.dmg * finishTurns) >= me.maxHp * policy::SURVIVE_FRAC)
                     || (policy::F_CAMP_STAY && campOnly && c.dmg > 0.f && (me.hp - c.dmg) >= me.maxHp * policy::RECOVER_HP);   // F (06:10): 캠프만 있는 싸움은 쫓기지 않으니 회복 문턱까지는 피해를 회복 비용으로만 (상위 팀 는 체력 40% 아래서도 캠프를 친다; 우리 엘크 49R vs v024z 28R, 카이팅 자리 제자리 13.9%)
        c.lethal = c.dmg >= me.hp * policy::LETHAL_MARGIN;
        if (!c.lethal) best.lethalAll = false;
        c.d1 = mission::stepsToGoal(c.p);
        const float penalty = c.prog;
        c.prog = penalty + ((d0 < 999 && c.d1 < 999) ? (float)(d0 - c.d1) * stepW : 0.f);
        if (c.d1 >= 65535 && d0 < 65535) c.prog = -1.f;
        if (noEnter && c.p != me.pos && threatAt(threats, c.p) > 0.f) c.prog -= 1e5f;   // R2: 열세인데 사거리 밖 → 들어가지 않는다
        if (policy::R_RETREAT && Mi.kind == mission::M_RECOVER && !threats.empty() && !cefNoEscape) {   // 물러날 때: 적에서 한 걸음 멀어짐 = 한 번 맞지 않음 (적 평균 힘 × 체력 1점의 젬 값)
            if (policy::FS_COM1_ON) { int dC = 99; float rcvS = 0.f;   /* FS_COM1: 칸에 기대는 것만 */
                for (size_t ti = 0; ti < threats.size(); ++ti) { if (s_fsRS[ti]) continue; const auto& t = threats[ti]; const int dc = cheb(t.pos, c.p); dC = std::min(dC, dc); if (s_fsRV[ti]) rcvS += (float)(dc - s_fsRD[ti]) * t.hit; }
                if (s_fsNT > 0) c.prog += rcvOn ? rcvS * gph : (float)(dC - s_fsDMe) * (s_fsHS / (float)s_fsNT) * gph;
            } else {
            int dMe = 99, dC = 99; float hitSum = 0.f; int nT = 0; float rcvS = 0.f;   /* RCV */
            for (size_t ti = 0; ti < threats.size(); ++ti) { const auto& t = threats[ti]; if (ti < w.enemies.size() && fbTarget(w, w.enemies[ti]) && w.enemies[ti].hp <= fbBurst(w, w.enemies[ti])) continue;   /* FB: 곧 죽을 적 (T<=1) 은 후퇴 걸음 값에서 뺌 */
                dMe = std::min(dMe, cheb(t.pos, me.pos)); dC = std::min(dC, cheb(t.pos, c.p)); if (cheb(t.pos, me.pos) <= rules::VISION) { hitSum += t.hit; ++nT; rcvS += (float)(cheb(t.pos, c.p) - cheb(t.pos, me.pos)) * t.hit; } }
            if (nT > 0) c.prog += rcvOn ? rcvS * gph : (float)(dC - dMe) * (hitSum / (float)nT) * gph; }   /* RCV: 열세 · 사거리 안이면 적마다 */
        }
        if (Mi.kind == mission::M_FARM && campOnly && mission::cxeBackOff() && !srcs.empty()) {   /* CXE3: 캠프가 가장 가까운 몸만 침 → 내가 가장 가깝지 않은 칸으로 */
            auto aggro = [&](Pos q) { float s = 0.f; for (const auto& k : srcs) { if (policy::BH3_TH5_ON && g_th5Scene) { if (campHitsMe(k, q)) s += k.str; continue; }   /* BH3_TH5: campThreat 와 같은 판정 */
                const int dq = cheb(k.pos, q); if (dq > k.range) continue; bool mine = true; for (const auto& a : w.allies) if (a.visible && a.alive && cheb(k.pos, a.pos) <= dq) { mine = false; break; } if (mine) s += k.str; } return s; };
            c.prog += (aggro(me.pos) - aggro(c.p)) * gph * 2.f;
        }
        if (policy::CXE_ON && Mi.kind == mission::M_RECOVER && (campOnly || policy::CXM_ON) && !srcs.empty()) {   /* CXM: 적이 곁에 있어도 */   /* CXE: 캠프 사거리부터 벗어남 */
            auto exitSteps = [&](Pos q) { int e = 0; for (const auto& k : srcs) { const int dd = cheb(k.pos, q); if (dd <= k.range) e = std::max(e, k.range + 1 - dd); } return e; };
            const float inc = campThreat(srcs, me.pos);
            if (inc > 0.f) c.prog += (float)(exitSteps(me.pos) - exitSteps(c.p)) * inc * gph;
        }
        if (g_stuckTurns >= 2 && c.p != me.pos && c.d1 < 999) { if (c.d1 == d0) c.prog += stepW * 0.5f; else if (g_stuckTurns >= 4 && c.d1 == d0 + 1) c.prog += stepW * 1.25f; }
        if (log::ON && w.round >= policy::DEBUG_CELLS_LO && w.round <= policy::DEBUG_CELLS_HI) NB_LINE("#nbv %d %d cell=%d,%d d1=%d dmg=%.0f prog=%.2f stepW=%.2f mv=%.1f", w.round, S().myIndex, c.p.x, c.p.y, c.d1, c.dmg, c.prog, stepW, mv);
    }
    if (!lzdDone) { lzdDone = true; lzdNStep = cellsDone; lzdRes = 12000 + g_lzdComb * (long long)lzdNStep; if (cellsDone == cells.size() && io::fuel() > lzdRes) { const size_t n0 = cells.size(); lzdBuild(); if (cells.size() > n0) { cleT = io::fuel(); goto lzdLoop; } } }   /* LZD: 걷기 칸을 다 봤으면 대시 칸 */
    bool ce4Ovr = false;   /* BH3_CE4: 이번 턴 VUL · FN · PEEL · PL 이 c.prog 를 덮어씀 */
    if (g_vulMode == 1 && g_vulN > 0) {   /* VUL 기다림 · 넘기기: 그룹 캠프 사거리 바로 밖 고리 (사거리 + 1) 로, 내가 어그로 대상인 칸은 깎음 */
        auto ringD = [&](Pos q, bool& aggro) { int m = 99; aggro = false;
            for (int t2 = 0; t2 < g_vulN; ++t2) { const VulCamp& vc = g_vulCamps[t2]; const int dq = cheb(q, vc.pos); m = std::min(m, dq - vc.R);
                if (dq <= vc.R && dq <= vc.dE && dq <= vc.dA) aggro = true; }
            return m >= 99 ? 0 : std::abs(m - 1); };
        bool ag0 = false; const int r0 = ringD(me.pos, ag0); ce4Ovr = true;
        for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f) continue; bool ag = false; const int rc = ringD(c.p, ag);
            c.prog = (float)(r0 - rc) * policy::FN_STEP - (ag ? policy::VUL_AGGRO_PEN : 0.f); }
    }
    if (io::fuel() > 18000 && policy::FN_ON && Mi.kind != mission::M_RECOVER && Mi.kind != mission::M_TRIBUTE) {   /* FB: 쫓기 문턱 (체력 50% · 힘 비 · 2배 화력) 대신 기대 젬 */   /* FNK: 내 체력 문턱은 아래 적마다 */
        const Unit* tgt = nullptr;
        for (const auto& e : w.enemies) {
            const bool fnk = policy::FNK_ON && e.alive && e.hp <= me.str && !onEnemySpawn(e.pos);   /* FNK: 내 평타 한 방으로 끝나는 적 */
            if (!fnk) { const float fbOpp = (2.f * (float)std::max(0, cheb(e.pos, me.pos) - std::max(1, me.range)) + 1.f) * policy::PLV_V;   /* FBR3: 쫓는 동안 못 하는 농사 */
                if (fbTarget(w, e) && fbDelta(w, e) > fbOpp) { if (!tgt || e.hp < tgt->hp) tgt = &e; } continue; }   /* FB: 한 방 적이 아니면 기대 젬 하나로 */
            if ((!fnk && e.hp >= 0.5f * e.maxHp) || cheb(e.pos, me.pos) > 5 || (policy::BUG4_ON && onEnemySpawn(e.pos))) continue;   /* BUG4 */
            float burst = fnDps(me.type, me.str, me.focus);
            for (const auto& a : w.allies) if (cheb(a.pos, e.pos) <= 3) burst += fnDps(a.type, a.str, a.focus);
            if (!fnk && mission::r3Ratio(w) < policy::FN_ADV) continue;   /* ENG3: 후퇴와 같은 잣대 */   /* FNK: 한 방 적은 힘 비교 없이 (칸 안전이 거름) */
            if (e.hp > burst * 2.f) continue;   /* DPC */
            if (!tgt || e.hp < tgt->hp) tgt = &e;
        }
        if (tgt) { ce4Ovr = true;
            const int dm = cheb(me.pos, tgt->pos);
            const float kapStep = me.range > 1 ? policy::FN_STEP : std::max(policy::FN_STEP, ((float)tgt->gems + (float)S().killGold / mission::goldDiv() + policy::DEATH_TIME_GEMS) / (float)(std::max(0, dm - 1) + 1));   /* KAPM: 근접만 쫓는 걸음 = 처치 판돈 / (닿기까지 걸음 + 1) */
            for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f) continue; if (policy::BH3_CE2_ON && me.range > 1) { const int reach = std::max(1, me.range); const int d0e = std::max(0, dm - reach); const int d1e = std::max(0, cheb(c.p, tgt->pos) - reach); c.prog = (float)(d0e - d1e) * policy::FN_STEP; continue; }   /* BH3_CE2: 원거리는 사거리 봉투로 (PL RNG 와 같게) */ c.prog = (float)(dm - cheb(c.p, tgt->pos)) * kapStep; }   /* KAPM */
        }
    }
    if (policy::PEEL_ON && !g_plEnemy && me.range <= 1 && me.hp >= 0.5f * me.maxHp && io::fuel() > 18000 && Mi.kind != mission::M_RECOVER && Mi.kind != mission::M_TRIBUTE && (mission::r3Ratio(w) >= policy::FN_ADV || mission::trpPub(w) <= 0.02f)) {   /* PLR: 위험 없는 막기 */   /* PEEL: 앞줄에 선 우리 원거리를 노리는 적 근접 막기 */
        const Unit* pt = nullptr; int pd = 99;
        for (const auto& a : w.allies) { if (!a.visible || !a.alive || a.range <= 1 || cheb(a.pos, me.pos) > 5) continue;
            for (const auto& e : w.enemies) { if (!e.alive || e.range > 1) continue; const int de = cheb(a.pos, e.pos); if (de > 3) continue;
                bool front = true; if (cheb(me.pos, e.pos) < de) front = false; for (const auto& b : w.allies) if (b.visible && b.alive && b.handle != a.handle && cheb(b.pos, e.pos) < de) { front = false; break; }
                if (!front) continue; const int dm = cheb(me.pos, e.pos); if (dm <= 1 || dm > 5) continue; if (dm < pd) { pd = dm; pt = &e; } } }
        if (pt) { ce4Ovr = true; for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f) continue; c.prog = (float)(pd - cheb(c.p, pt->pos)) * policy::FN_STEP; } }
    }
    if (g_plEnemy) { ce4Ovr = true; const int reach=std::max(1,me.range); const int d0e=std::max(0,cheb(me.pos,g_plEnemy->pos)-reach); const float kapPl=me.range>1?policy::FN_STEP:std::max(policy::FN_STEP,((float)g_plEnemy->gems+(float)S().killGold/mission::goldDiv()+policy::DEATH_TIME_GEMS)/(float)(d0e+1));   /* KAPM */ for(size_t ci=0;ci<cellsDone&&ci<cells.size();++ci) { Cell& c=cells[ci]; if(c.prog<=-1e4f)continue; const int d1e=std::max(0,cheb(c.p,g_plEnemy->pos)-reach); c.prog=(float)(d0e-d1e)*kapPl; } } // RNG: approach attack envelope, not enemy tile
    if (policy::BH3_CE4_ON && ce4Ovr) for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog > -1e4f) c.prog += c.cae; }   /* BH3_CE4: 덮어쓴 턴만 CAE 다시 (안 덮은 턴은 이미 들어 있음) */
    auto cpsGroup = [&](Pos q) -> int { const int g = S().campGroupAt[(size_t)S().idx(q)]; return (g >= 0 && S().groups[(size_t)g].state == 2) ? g : -1; };   /* BH3_CE3: 함수 범위 (MSTEP 도 씀) */
    int g0 = -1; float lose = 0.f;   /* BH3_CE3: CPS 의 지금 무리 · 떠나면 잃는 값 (CPS 블록이 안 돌면 0) */
    if (policy::CPS_ON && Mi.kind != mission::M_RECOVER && Mi.kind != mission::M_TRIBUTE && Mi.kind != mission::M_SHOP && io::fuel() > 18000) {   /* CPS: 보상 대기 칸을 떠나면 잃는 값 · 적 점령 칸에 서면 막는 값 (젬) */
        auto cpsVal = [&](int g) -> float { const Group& G = S().groups[(size_t)g]; return (float)G.gems + (float)G.gold / mission::goldDiv(); };
        auto cpsMate = [&](int g) -> bool { for (const auto& a : w.allies) if (a.visible && a.alive && S().campGroupAt[(size_t)S().idx(a.pos)] == g) return true; return false; };
        g0 = cpsGroup(me.pos); lose = 0.f;
        if (g0 >= 0 && !cpsMate(g0)) {   /* CPS: 칸 위 유일한 아군일 때만 (다른 아군이 서 있으면 떠나도 그대로) */
            const int cs0 = io::groupCaptureState(S().groups[(size_t)g0].handle);
            if (cs0 == 2) lose = cpsVal(g0);   /* CPS: 경쟁 중에 떠나면 보상이 상대에게 */
            else if (cs0 == 1 && (g_plEnemy || !(Mi.kind == mission::M_CAPTURE && Mi.group == g0))) { const int tl = io::groupCaptureTimer(S().groups[(size_t)g0].handle); if (tl > 0) lose = cpsVal(g0) * (float)(rules::CAPTURE_ROUNDS - tl + 1) / (float)(rules::CAPTURE_ROUNDS + 1); }   /* CPS: 쌓은 점령 진행 몫 */
        }
        int gE = -1, csE = -1;
        for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f) continue;
            const int gc = cpsGroup(c.p);
            if (lose > 0.f && gc != g0) c.prog -= lose;
            if (gc >= 0 && gc != g0) { if (gc != gE) { gE = gc; csE = io::groupCaptureState(S().groups[(size_t)gc].handle); } if (csE == 3) c.prog += cpsVal(gc); }   /* CPS: 적 점령 중인 칸에 서면 막음 */
        }
    }
    bool ccdLeave = false; float ccdHere = 0.f;
    if (policy::CCD_ON && campOnly && Mi.kind == mission::M_CAPTURE && Mi.group >= 0 && !srcs.empty() && d0 < 999 && io::fuel() > 18000) {   /* CCD: 점령 임무 중 캠프 피해 */
        const Group& CG = S().groups[(size_t)Mi.group];
        const bool onT = S().campGroupAt[(size_t)S().idx(me.pos)] == Mi.group;
        const int cs = io::groupCaptureState(CG.handle), tl = io::groupCaptureTimer(CG.handle);
        const int rTile = (onT && cs == 1 && tl > 0) ? tl : rules::CAPTURE_ROUNDS;   /* 무리 칸 사이를 걸어 옮겨도 점령은 이어진다 (엔진 시험 engine_capture_channel 2절) */
        bool mate = false; for (const auto& a : w.allies) if (a.visible && a.alive && S().campGroupAt[(size_t)S().idx(a.pos)] == Mi.group) { mate = true; break; }
        const float keepV = (onT && cs == 1 && tl > 0 && !mate) ? ((float)CG.gems + (float)CG.gold / mission::goldDiv()) * (float)(rules::CAPTURE_ROUNDS - tl + 1) / (float)(rules::CAPTURE_ROUNDS + 1) : 0.f;   /* CPS 와 같은 식: 쌓은 진행 */
        const float deathV = (float)me.gems + mission::myDeathTime() + ((float)S().killGold / mission::goldDiv() + 10.f);   /* DTR */   /* dmgCost 의 죽음 값 */
        ccdHere = campThreat(srcs, me.pos);
        bool anyLive = false;
        for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f || c.d1 >= 999) continue;
            const float cd = campThreat(srcs, c.p);
            const bool keepT = onT && !c.dash && S().campGroupAt[(size_t)S().idx(c.p)] == Mi.group;   /* 점령이 이어지는 칸 (지금 칸 포함) */
            const int Rc = keepT ? rTile : c.d1 + rules::CAPTURE_ROUNDS;
            const bool dies = cd * (float)Rc >= me.hp;   /* 2: 끝내기 전에 죽는 계획 */
            if (cd > 0.f && (onT || dies)) c.finishOk = false;   /* 1: 점령 칸 위에는 끝낼 싸움이 없다 — 캠프 피해는 dmgCost 그대로 (덜 맞는 점령 칸이 이김) */
            if (onT && !keepT && ccdHere > 0.f) c.prog -= keepV;   /* 1: 무리 칸 밖 = 쌓은 진행을 잃음 */
            if (dies) c.prog -= policy::CCD_W * deathV; else anyLive = true;
        }
        const float keepIfStay = 0.f;   /* 2: 끝내기 전에 죽으면 보상을 못 받는다 (보상은 끝날 때 칸 위 아군만) → 남아서 지키는 값은 0 */
        if (!anyLive && ccdHere > 0.f && policy::CCD_W * deathV > keepIfStay) {
            ccdLeave = true;
            auto exitSteps = [&](Pos q) { int e = 0; for (const auto& k : srcs) { const int dd = cheb(k.pos, q); if (dd <= k.range && dd <= k.nearOther) e = std::max(e, k.range + 1 - dd); } return e; };
            const int e0 = exitSteps(me.pos);
            for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.prog <= -1e4f) continue; c.prog = (float)(e0 - exitSteps(c.p)) * ccdHere * gph; }   /* CXE 와 같은 식: 사거리 밖으로 */
        }
    }
    if (policy::SPH_ON && me.atSpawn && !w.enemies.empty()) {   /* SPH: 스폰 지키기 */
        if (mission::r3Ratio(w) < 1.f) {   /* ENG3: 후퇴와 같은 잣대 */ for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { Cell& c = cells[ci]; if (c.p == me.pos) continue; if (!(S().tile[(size_t)S().idx(c.p)] & T_ALLY_SPAWN)) c.prog -= 1e5f; } }
    }
    const long long fB = io::fuel();
    if (campOnly && (Mi.kind == mission::M_FARM || Mi.kind == mission::M_CAPTURE) && d0 > 0 && d0 < 999) {   // NE
        bool closer = false, blocked = true;
        for (size_t ci = 0; ci < cellsDone && ci < cells.size(); ++ci) { const Cell& c = cells[ci]; if (c.p == me.pos || c.d1 >= d0) continue; closer = true; if (c.dmg <= 0.f || c.finishOk) blocked = false; }
        g_noEnter = (closer && blocked) ? g_noEnter + 1 : 0;
    } else g_noEnter = 0;

    int lzdCk = 0; long long lzdCF0 = 0;   /* LZD2 */
    // 3. 조합: 칸마다 최선 평타 (+리셋) · 주문 하나. 값만 비교하고 계획은 마지막에 하나만 만든다
    int resetSlot = -1; if (me.canCast && D.valid) for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_RESET && me.cooldown[s] == 0) resetSlot = s;
    const bool resetStun = resetSlot >= 0 && (D.ab[resetSlot].duration > 0 || me.type == 7);   /* CDM: 룰북 — Marcella 리셋 = 다음 평타 1R 기절 */
    struct Pick { int cell = 0; int tgt = -1; int tgt2 = -1; int when = 0; bool reset = false; int spell = -1; int spellTgt = -1; float v = -1e9f; float prog = 0.f; };
    Pick bp; int nCombos = 0;
    Pick bpA; auto abPick = [&](const Pick& q) { if (q.v > bpA.v + 1e-3f || (q.v > bpA.v - 1e-3f && q.prog > bpA.prog + 1e-4f)) bpA = q; };
    Pick bpS;   /* SAFEP: 안전한 칸에서 적 챔피언에게 기술을 쓰는 조합 중 최선 */   /* CDA: 기술 · 리셋 · 피해 대시를 쓰는 조합 중 최선 */
    auto better = [&](float v, float prog) { return v > bp.v + 1e-3f || (v > bp.v - 1e-3f && prog > bp.prog + 1e-4f); };
    float rdwWalkMin = 1e30f; if (ranged) for (int ci = 0; ci < (int)cells.size() && ci < (int)cellsDone; ++ci) if (!cells[ci].dash && cells[ci].prog > -1e4f && !cells[ci].lethal) rdwWalkMin = std::min(rdwWalkMin, cells[ci].dmg);   /* RDW (C3): 걷기 · 제자리 칸 중 가장 적게 맞는 피해 */
    struct Fs2Nb { float v; bool fb; }; struct Fs2Cc { int tj; Pos p; bool sk; };   /* FS2_COM1: 범위 이웃 (값, fb) · 기절 후보 (번호, 칸, 평타로 죽음) */
    static std::vector<Fs2Nb> s_f2Nb; static std::vector<Fs2Cc> s_f2Cc; static std::vector<int> s_f2St[2], s_f2NbO[2], s_f2NbN[2], s_f2CcO[2], s_f2CcN[2]; static std::vector<float> s_f2V[2], s_f2R[2];
    if (policy::FS2_COM1_ON) { s_f2Nb.clear(); s_f2Cc.clear(); const size_t nT2 = targets.size();
        for (int s = 0; s < 2; ++s) { s_f2St[s].assign(nT2, 0); s_f2NbO[s].resize(nT2); s_f2NbN[s].resize(nT2); s_f2CcO[s].resize(nT2); s_f2CcN[s].resize(nT2); s_f2V[s].resize(nT2); s_f2R[s].resize(nT2); } }
    static std::vector<unsigned> s_f2kS, s_f2dS; static std::vector<float> s_f2kV, s_f2dV; unsigned f2G = 0;   /* FS2_COM2: 칸 도장 (칸마다 +1) */
    if (policy::FS2_COM2_ON) { s_f2kS.assign(targets.size(), 0u); s_f2kV.resize(targets.size()); s_f2dS.assign(threats.size() * 2, 0u); s_f2dV.resize(threats.size() * 2); }
    int f2Ht = -1; float f2Hv = 0.f;   /* FS2_COM3: 지금 칸에서 칠 최선 표적 (3b 와 같은 거름 fb · 사거리 · 엄격한 >, 칸과 무관) */
    if (policy::FS2_COM3_ON && me.canAttack) for (int ti = 0; ti < (int)targets.size(); ++ti) { const Target& t = targets[ti]; if (t.fb) continue; if (cheb(me.pos, t.pos) <= me.range && t.hitVal > f2Hv) { f2Hv = t.hitVal; f2Ht = ti; } }
    bool psrOn = false; float psrUx = 0.f, psrUy = 0.f, psrUsF = -1e9f, psrEnF = 1e9f;   /* PSR: 턴마다 한 번 — 축 · 우리 전선 · 적 전선 */
    if (policy::PSR_ON) { const float ax = (float)(S().enemyCenter.x - S().allyCenter.x), ay = (float)(S().enemyCenter.y - S().allyCenter.y), al = std::sqrt(ax * ax + ay * ay);
        if (al > 0.5f) { psrUx = ax / al; psrUy = ay / al;
            auto prg = [&](Pos q) { return (float)(q.x - S().allyCenter.x) * psrUx + (float)(q.y - S().allyCenter.y) * psrUy; };
            for (const auto& e : w.enemies) { if (!e.alive || !e.visible || onEnemySpawn(e.pos) || cheb(e.pos, me.pos) > policy::PSR_R) continue; psrOn = true; psrEnF = std::min(psrEnF, prg(e.pos)); }
            if (psrOn) for (const auto& a : w.allies) { if (!a.alive || !a.visible) continue; psrUsF = std::max(psrUsF, prg(a.pos)); } } }
    lzdCF0 = io::fuel();   /* LZD2: 조합 시작 연료 */
    for (int ci = 0; ci < (int)cells.size() && ci < (int)cellsDone; ++ci) {
        if (io::fuel() < 12000) break;
        ++lzdCk;   /* LZD2 */
        const Cell& c = cells[ci];
        if (ranged && !threats.empty() && c.dash && c.dmg >= rdwWalkMin - 0.5f) {   /* RDW: 원거리 대시는 위협이 있을 때 피해를 실제로 줄이거나 대시로만 칠 표적이 있을 때만 (동점이면 걷기 — 대시는 다음 싸움 · 진짜 탈출에) */
            bool only = false; for (const auto& u : targets) if (!u.fb && cheb(u.pos, c.p) <= me.range && cheb(u.pos, me.pos) > me.range) { only = true; break; }
            if (!only) continue; }
        const bool moving = c.p != me.pos;
        float psrC = 0.f;   /* PSR: 포지션 위험도 — 우리 전선보다 앞섬 · 적 전선 안에 갇힘 (칸 비용에만) */
        if (policy::PSR_ON && psrOn) { const float pr = (float)(c.p.x - S().allyCenter.x) * psrUx + (float)(c.p.y - S().allyCenter.y) * psrUy;
            if (psrUsF > -1e8f) psrC += policy::PSR_AHEAD * std::min(policy::PSR_AHEAD_CAP, std::max(0.f, pr - psrUsF));
            psrC += policy::PSR_TRAP * std::min(policy::PSR_TRAP_CAP, std::max(0.f, pr - psrEnF));
            if (psrC > 0.f) { const int rg = std::max(1, me.range); for (const auto& e : w.enemies) if (e.alive && e.visible && !onEnemySpawn(e.pos) && cheb(e.pos, c.p) <= rg) { psrC *= policy::PSR_HIT_MUL; break; } } }   /* PSR2 */
        const float cost = c.finishOk ? 0.f : dmgCostF(c.dmg, me.hp, me.maxHp, me.gems, lhfF(c)) + psrC;   /* LHF */   /* PSR */   // AP: 살아남는 캠프 싸움은 거름만   // 끝까지 살아남는 싸움은 회복 비용만
        float base = -cost + c.prog - (c.lethal ? 1e6f : 0.f);
        if (policy::FS2_COM2_ON) ++f2G;   /* FS2_COM2: 새 칸 */
        auto f2Drop = [&](const Threat& th, int dur) -> float {   /* FS2_COM2: tf6Drop 을 (칸, 위협, 등급) 마다 한 번 (dur > 0 만 옴) */
            if (!policy::FS2_COM2_ON) return tf6Drop(w, th, c.p, dur);
            const size_t k = (size_t)(&th - threats.data()) * 2 + (dur >= 2 ? 1 : 0);
            if (s_f2dS[k] == f2G) return s_f2dV[k];
            const float v = tf6Drop(w, th, c.p, dur); s_f2dS[k] = f2G; s_f2dV[k] = v; return v; };
        auto tf6Relief = [&](const Pos* ps, int n, int dur) -> float {   /* TF6A: 기절될 적들 (ps) 의 위협을 함께 뺀 칸 비용 차 (KFIN 과 같은 식) */
            if (!policy::TF6A_ON || n <= 0 || dur <= 0 || !fightOn || c.finishOk) return 0.f;
            float tE = 0.f;
            for (const auto& th : threats) { bool hit = false; for (int i = 0; i < n; ++i) if (ps[i] == th.pos) { hit = true; break; } if (hit) tE += f2Drop(th, dur); }
            if (tE <= 0.f) return 0.f;
            const float d2 = std::max(0.f, c.dmg - tE);
            float rel = dmgCostF(c.dmg, me.hp, me.maxHp, me.gems, lhfF(c)) - dmgCostF(d2, me.hp, me.maxHp, me.gems, std::max(std::max(0.f, c.fEd - tE), c.fKd));   /* LHF */
            if (c.lethal && d2 < me.hp * policy::LETHAL_MARGIN) rel += 1e6f;
            return std::max(0.f, rel); };
        [[maybe_unused]] bool cdDashHit = false;   /* CD: 피해 대시가 무엇이든 맞힘 */
        if (policy::HD_ON && c.dash && c.dashSlot >= 0 && D.ab[c.dashSlot].modifier > 0.f) {   // HD: 착지 반경 안 적 · 캠프 피해
            const AbilitySpec& da = D.ab[c.dashSlot]; const float sd = me.focus * da.modifier; const int rad = std::max(1, (int)da.radius);
            const Pos land = c.dw ? c.mid : c.p;   /* DWK: 착지 칸 */
            bool champHit = false;   /* HDS */
            bool mineHit = false;   /* HCD */
            for (const auto& u : targets) { if (io::fuel() < 12000) goto combosDone; if (!u.fb && cheb(u.pos, land) <= rad) { base += targetValue(u, sd); if (u.kind == 1) champHit = true; if (u.kind == 2 && u.group == Mi.group) mineHit = true; cdDashHit = true; } }
            if (champHit) g_gdsSeen = true;   /* GDS: 챔피언 맞히는 대시 칸을 choose 가 평가함 */
            if (policy::TF6A_ON && da.duration > 0 && champHit) { Pos tp[16]; int tn = 0;   /* TF6A: 착지 기절 = 반경 안 적 챔피언 모두의 다음 차례 위협 빼기 */
                for (const auto& u : targets) if (u.kind == 1 && !u.fb && cheb(u.pos, land) <= rad && tn < 16) tp[tn++] = u.pos;
                base += tf6Relief(tp, tn, da.duration); }
            if (Mi.kind == mission::M_FARM && Mi.group >= 0 && threats.empty() && !cdDashHit) { bool atCamp = false;   /* HCD: 농사 중 맡은 무리 바로 앞이면 그 캠프를 맞히는 피해 대시만 */
                for (const auto& k : w.camps) if (k.hp > 0.f && k.group == Mi.group && cheb(k.pos, me.pos) <= std::max(1, me.range) + 1) { atCamp = true; break; }
                if (atCamp) continue; }
            if (policy::HDW_ON) { static std::vector<int> s_wg; s_wg.clear();   /* HDW: 착지 광역이 깨울 잠든 무리 */
                for (const auto& k : w.camps) { if (k.hp <= 0.f || k.group < 0 || k.group == Mi.group || cheb(k.pos, land) > rad) continue; const Group& KG = S().groups[(size_t)k.group];
                    if (KG.awakeUntil >= w.round || KG.strMax <= 0.f) continue; if (std::find(s_wg.begin(), s_wg.end(), k.group) == s_wg.end()) s_wg.push_back(k.group); }
                if (!s_wg.empty()) { float extra = 0.f;
                    for (const auto& k : w.camps) if (k.hp > 0.f && std::find(s_wg.begin(), s_wg.end(), k.group) != s_wg.end() && cheb(k.pos, land) <= S().campRange(k.type)) extra += S().campStr(k.type);
                    if (extra > 0.f) base -= dmgCostF(c.dmg + extra, me.hp, me.maxHp, me.gems, std::max(c.fEd, c.fKd + extra)) - dmgCostF(c.dmg, me.hp, me.maxHp, me.gems, lhfF(c)); } }   /* LHF */
            if (policy::HDS_ON && !champHit && !cdDashHit && me.hp >= 0.4f * me.maxHp) {   /* CDH: 무엇이든 맞히는 피해 대시는 씀 */ bool near8 = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, me.pos) <= rules::VISION) { near8 = true; break; } if (near8) continue; }   /* HDS: 적이 가까우면 피해 대시는 챔피언을 맞힐 때만 */
        }
        if (policy::DSV_ON && c.dash && c.dashSlot >= 0 && D.ab[c.dashSlot].modifier <= 0.f && me.range <= 1 && me.hp >= 0.4f * me.maxHp) {   /* DSV2: 근접만 · DSV: 적이 가까우면 대시는 치거나 도망칠 때만 */
            bool near8 = false; for (const auto& e : w.enemies) if (e.alive && cheb(e.pos, me.pos) <= rules::VISION) { near8 = true; break; }
            if (near8) { bool atk = false; const Pos land = c.dw ? c.p : c.p; for (const auto& u : targets) if (u.kind == 1 && cheb(u.pos, land) <= std::max(1, me.range)) { atk = true; break; }
                const float hereD = cells[0].dmg; const bool esc = hereD > 0.f && c.dmg < 0.5f * hereD;
                if (!atk && !esc) continue; } }
        ++nCombos; if (better(base, c.prog)) { bp = Pick{}; bp.cell = ci; bp.v = base; bp.prog = c.prog; }
        if (cdDashHit) { Pick q; q.cell = ci; q.v = base; q.prog = c.prog; abPick(q); }   /* CDA */
        if (targets.empty()) continue;
        // 3b. 평타: 근접은 이동 전 (내 칸) 또는 이동 후 (c). 원거리는 이동 없을 때만
        int bt = -1, bw = 0; float bv = 0.f;
        int f2Tt = -1; float f2Tv = 0.f;   /* FS2_COM3: 도착 칸에서 칠 최선 표적 */
        if (policy::FS2_COM3_ON && me.canAttack) {
            const bool walked = policy::DWK_ON ? ((moving && !c.dash) || c.dw) : moving;   /* 3b 원거리와 같은 식 */
            const bool hereOk = !ranged || !walked, thereOk = !ranged || (policy::DWK_ON && c.dash && !c.dw);
            if (thereOk) { for (int ti = 0; ti < (int)targets.size(); ++ti) { if ((ti & 3) == 0 && io::fuel() < 12000) goto combosDone; const Target& t = targets[ti]; if (t.fb) continue; if (cheb(c.p, t.pos) <= me.range && t.hitVal > f2Tv) { f2Tv = t.hitVal; f2Tt = ti; } } }
            else if (io::fuel() < 12000) goto combosDone;   /* 원래 고리의 ti=0 연료 검사 */
            const bool hOk = hereOk && f2Ht >= 0, tOk = f2Tt >= 0;   /* 원래 순서 (ti 지금 칸, ti 도착 칸, ...) 에서 최댓값을 가진 첫 원소 */
            if (hOk && (!tOk || f2Hv > f2Tv || (!(f2Tv > f2Hv) && f2Ht <= f2Tt))) { bv = f2Hv; bt = f2Ht; bw = 1; }
            else if (tOk) { bv = f2Tv; bt = f2Tt; bw = 2; } }
        if (!policy::FS2_COM3_ON && me.canAttack) for (int ti = 0; ti < (int)targets.size(); ++ti) {
            if ((ti & 3) == 0 && io::fuel() < 12000) goto combosDone;
            const Target& t = targets[ti]; if (t.fb) continue;   /* CDS */
            if (ranged) { const bool walked = policy::DWK_ON ? ((moving && !c.dash) || c.dw) : moving;   /* DWK: 대시는 걷기가 아님 */
                if (!walked && cheb(me.pos, t.pos) <= me.range && t.hitVal > bv) { bv = t.hitVal; bt = ti; bw = 1; }
                if (policy::DWK_ON && c.dash && !c.dw && cheb(c.p, t.pos) <= me.range && t.hitVal > bv) { bv = t.hitVal; bt = ti; bw = 2; }
                continue; }
            if (cheb(me.pos, t.pos) <= me.range && t.hitVal > bv) { bv = t.hitVal; bt = ti; bw = 1; }
            if (cheb(c.p, t.pos) <= me.range && t.hitVal > bv) { bv = t.hitVal; bt = ti; bw = 2; }
        }
        auto kfinRelief0 = [&](const Target& t) -> float {   /* KFIN: 이번 턴에 죽일 적의 위협을 이 칸 비용에서 뺀 이득 */
            if (!policy::KFIN_ON || t.kind != 1 || !fightOn) return 0.f;
            float tE = 0.f;
            for (const auto& th : threats) { if (th.pos != t.pos) continue; const int dd = cheb(th.pos, c.p); bool other = false;
                if (policy::FS2_COM2_ON && policy::FS_COM1_ON) other = s_fsMC[(size_t)(&th - threats.data())] < dd; else   /* FS2_COM2: 1차 가림 표 (visible && hp <= me.hp 최소 거리) */
                for (const auto& al : w.allies) if (al.visible && cheb(al.pos, th.pos) < dd && al.hp <= me.hp) { other = true; break; }   /* WKF */
                const float k = other ? 0.15f : 1.f;
                tE += k * thDmg(th, dd); break; }   /* TRS */
            if (tE <= 0.f) return 0.f;
            const float d2 = std::max(0.f, c.dmg - tE);
            float rel = c.finishOk ? 0.f : dmgCostF(c.dmg, me.hp, me.maxHp, me.gems, lhfF(c)) - dmgCostF(d2, me.hp, me.maxHp, me.gems, std::max(std::max(0.f, c.fEd - tE), c.fKd));   /* LHF */
            if (c.lethal && d2 < me.hp * policy::LETHAL_MARGIN) rel += 1e6f;
            return std::max(0.f, rel);
        };
        auto kfinRelief = [&](const Target& t) -> float {   /* FS2_COM2: 같은 칸 · 같은 표적이면 한 번만 셈 */
            if (!policy::FS2_COM2_ON) return kfinRelief0(t);
            const size_t k = (size_t)(&t - targets.data());
            if (s_f2kS[k] == f2G) return s_f2kV[k];
            const float v = kfinRelief0(t); s_f2kS[k] = f2G; s_f2kV[k] = v; return v; };
        if (bt >= 0) {   // (T 수정) 평타 표적이 없어도 주문 (3c) 은 본다 — 전에는 여기서 continue 해 사거리 4 주문이 평타 사거리 밖에서 전혀 안 나갔다
            const float kr1 = (targets[bt].kind == 1 && me.str >= targets[bt].hp) ? kfinRelief(targets[bt]) : 0.f;   /* KFIN */
            ++nCombos; if (better(base + bv + kr1, c.prog)) { bp = Pick{}; bp.cell = ci; bp.tgt = bt; bp.when = bw; bp.v = base + bv + kr1; bp.prog = c.prog; }
            if (cdDashHit) { Pick q; q.cell = ci; q.tgt = bt; q.when = bw; q.v = base + bv + kr1; q.prog = c.prog; abPick(q); }   /* CDA */
            if (resetSlot >= 0 && !(policy::BUG2_ON && c.dash) && !(policy::BH4_VD1_ON && !c.dash && targets[bt].kind == 1 && me.str >= targets[bt].hp)) { const float kr2 =   /* BUG2: 대시 칸은 리셋을 안 씀 (두 방 값 금지) */ (targets[bt].kind == 1 && 2.f * me.str >= targets[bt].hp) ? kfinRelief(targets[bt]) : 0.f;   /* KFIN */
                const Target& rvT = targets[bt]; const float rvSpl = bv - targetValue(rvT, me.str);   /* RV2: VSP 튐 몫 */
                const float bv2 = c.dash ? bv * 2.f : targetValue(rvT, 2.f * me.str) + 2.f * rvSpl;   /* RV2: 같은 표적 두 방 = 2배 피해 한 번 (대시 칸은 그대로) */
                const float cdmK = (resetStun && !c.dash && targets[bt].kind == 1 && kr2 <= 0.f) ? kfinRelief(targets[bt]) : 0.f;   /* CDM: 기절 = 그 적의 다음 차례 위협이 이 칸에서 빠짐 (마무리와 같은 식) */
                ++nCombos; const float v2 = base + bv2 + kr2 + cdmK; if (better(v2, c.prog)) { bp = Pick{}; bp.cell = ci; bp.tgt = bt; bp.when = bw; bp.reset = !c.dash; bp.v = v2;   /* RXA2 */ bp.prog = c.prog; }
                if (!c.dash || cdDashHit) { Pick q; q.cell = ci; q.tgt = bt; q.when = bw; q.reset = !c.dash; q.v = v2; q.prog = c.prog; abPick(q); } }   /* CDA */
        }
        if (policy::RST_ON && resetSlot >= 0 && !ranged && moving && !c.dash && me.canAttack) {   /* RST: A (지금 칸) → 리셋 → 걷고 B (도착 칸) */
            int ta = -1, tb = -1; float va = 0.f, vb = 0.f;
            if (policy::FS2_COM3_ON) { ta = f2Ht; va = f2Hv; tb = f2Tt; vb = f2Tv; } else   /* FS2_COM3: 3b 와 같은 거름 · 같은 순서의 두 최선 (근접 · 걷기 칸이라 도착 칸 최선이 셈해져 있음) */
            for (int ti = 0; ti < (int)targets.size(); ++ti) { const Target& tt = targets[ti]; if (tt.fb) continue;   /* CDS */
                if (cheb(me.pos, tt.pos) <= me.range && tt.hitVal > va) { va = tt.hitVal; ta = ti; }
                if (cheb(c.p, tt.pos) <= me.range && tt.hitVal > vb) { vb = tt.hitVal; tb = ti; } }
            if (ta >= 0 && tb >= 0 && ta != tb) { ++nCombos; const float v5 = base + va + vb;
                if (better(v5, c.prog)) { bp = Pick{}; bp.cell = ci; bp.tgt = ta; bp.tgt2 = tb; bp.when = 1; bp.reset = true; bp.v = v5; bp.prog = c.prog; } { Pick q; q.cell = ci; q.tgt = ta; q.tgt2 = tb; q.when = 1; q.reset = true; q.v = v5; q.prog = c.prog; abPick(q); } } }   /* CDA */
        if (policy::RWA_ON && ranged && resetSlot >= 0 && moving && !c.dash && !c.dw && me.canAttack) {   /* RWA: 걷기 → 리셋 → 평타 */
            int tw = -1; float vw = 0.f;
            for (int ti = 0; ti < (int)targets.size(); ++ti) { const Target& tt = targets[ti]; if (tt.fb) continue; if (cheb(c.p, tt.pos) <= me.range && tt.hitVal > vw) { vw = tt.hitVal; tw = ti; } }
            if (tw >= 0) { const float krw = (targets[tw].kind == 1 && me.str >= targets[tw].hp) ? kfinRelief(targets[tw]) : 0.f; ++nCombos; const float vrw = base + vw + krw;
                if (better(vrw, c.prog)) { bp = Pick{}; bp.cell = ci; bp.tgt = tw; bp.when = 4; bp.v = vrw; bp.prog = c.prog; } { Pick q; q.cell = ci; q.tgt = tw; q.when = 4; q.v = vrw; q.prog = c.prog; abPick(q); } } }   /* CDA */
        // 3c. 피해 · 스턴 · 범위 주문 (리셋 대신): 사거리 안 최선 표적. 걷기 불가 주문은 이동 없을 때만
        if (me.canCast && D.valid && !c.dash) for (int s = 0; s < 2; ++s) {
            const AbilitySpec& a = D.ab[s];
            if (me.cooldown[s] != 0) continue;
            if (a.kind != rules::AB_DAMAGE && a.kind != rules::AB_CC && a.kind != rules::AB_AOE) continue;
            if (a.noWalk && moving) continue;
            const Pos from = a.noWalk ? me.pos : c.p;
            const float sd = me.focus * a.modifier;
            bool cdsChamp = false; for (const auto& u : targets) if (u.kind == 1 && a.targetsEnemies && cheb(from, u.pos) <= (int)a.range) { cdsChamp = true; break; }   /* CDS: 챔피언 먼저, 없으면 캠프 */
            int st = -1; float sv = 0.f;
            for (int ti = 0; ti < (int)targets.size(); ++ti) {
                if (io::fuel() < 12000) goto combosDone;
                const Target& t = targets[ti];
                if (t.kind == 1 && !a.targetsEnemies) continue; if (t.kind == 2 && !a.targetsCamps) continue;
                if (cheb(from, t.pos) > (int)a.range) continue; if (t.fb && cdsChamp) continue;
                if (policy::FS2_COM1_ON) {   /* FS2_COM1: 아래 원래 길과 같은 값 · 같은 더하기 순서 */
                    const size_t ui = (size_t)ti; int& fl = s_f2St[s][ui];
                    if (!(fl & 1)) { fl |= 1; s_f2V[s][ui] = targetValue(t, sd);
                        s_f2NbO[s][ui] = (int)s_f2Nb.size();
                        if (a.kind == rules::AB_AOE) { const int rad = std::max(1, (int)a.radius);
                            for (int tj = 0; tj < (int)targets.size(); ++tj) { if ((tj & 3) == 0 && io::fuel() < 12000) goto combosDone; if (tj == ti) continue; const Target& u = targets[tj]; if (u.kind == 1 && !a.targetsEnemies) continue; if (u.kind == 2 && !a.targetsCamps) continue; if (cheb(u.pos, t.pos) <= rad) s_f2Nb.push_back({targetValue(u, sd), u.fb}); } }
                        s_f2NbN[s][ui] = (int)s_f2Nb.size() - s_f2NbO[s][ui];
                        s_f2CcO[s][ui] = (int)s_f2Cc.size();
                        if (policy::TF6A_ON && a.duration > 0 && a.targetsEnemies && (a.kind == rules::AB_CC || a.kind == rules::AB_AOE)) { const int rr = a.kind == rules::AB_AOE ? std::max(1, (int)a.radius) : 0;
                            for (int tj = 0; tj < (int)targets.size(); ++tj) { const Target& u = targets[tj]; if (u.kind != 1 || cheb(u.pos, t.pos) > rr) continue; if (tj == ti && sd >= u.hp) continue; s_f2Cc.push_back({tj, u.pos, me.str >= u.hp}); } }
                        s_f2CcN[s][ui] = (int)s_f2Cc.size() - s_f2CcO[s][ui]; }
                    float v;
                    if (ti == bt && t.kind == 1) { if (!(fl & 2)) { fl |= 2; s_f2R[s][ui] = targetValue(t, me.str + sd) - targetValue(t, me.str); } v = s_f2R[s][ui]; } else v = s_f2V[s][ui];   /* RV2 */
                    if (a.kind == rules::AB_AOE) for (int q = s_f2NbO[s][ui], qe = q + s_f2NbN[s][ui]; q < qe; ++q) { const Fs2Nb& nb = s_f2Nb[(size_t)q]; if (nb.fb && cdsChamp) continue; v += nb.v; }
                    if (a.kind == rules::AB_CC && t.channeling) v += (float)t.full * 0.5f;
                    if (policy::TF6A_ON && a.duration > 0 && a.targetsEnemies && (a.kind == rules::AB_CC || a.kind == rules::AB_AOE)) { Pos tp[16]; int tn = 0;
                        for (int q = s_f2CcO[s][ui], qe = q + s_f2CcN[s][ui]; q < qe && tn < 16; ++q) { const Fs2Cc& e = s_f2Cc[(size_t)q]; if (e.tj == bt && e.sk) continue; tp[tn++] = e.p; }
                        v += tf6Relief(tp, tn, a.duration); }
                    if (v > sv) { sv = v; st = ti; }
                    continue; }
                float v = (ti == bt && t.kind == 1) ? targetValue(t, me.str + sd) - targetValue(t, me.str) : targetValue(t, sd);   /* RV2: 평타 표적에 겹치는 주문은 더한 몫만 */
                if (a.kind == rules::AB_AOE) {   // 범위: 중심 둘레 (반경) 안의 다른 표적도 맞는다 (상위 팀 stampede: 6캠프 동시 80~140)
                    const int rad = std::max(1, (int)a.radius);
                    for (int tj = 0; tj < (int)targets.size(); ++tj) { if ((tj & 3) == 0 && io::fuel() < 12000) goto combosDone; if (tj == ti) continue; const Target& u = targets[tj]; if (u.kind == 1 && !a.targetsEnemies) continue; if (u.kind == 2 && !a.targetsCamps) continue; if (u.fb && cdsChamp) continue; if (cheb(u.pos, t.pos) <= rad) v += targetValue(u, sd); }
                }
                if (a.kind == rules::AB_CC && t.channeling) v += (float)t.full * 0.5f;   // 채널 (리콜 · 바치기 · 텔레포트) 을 끊는 값
                if (policy::TF6A_ON && a.duration > 0 && a.targetsEnemies && (a.kind == rules::AB_CC || a.kind == rules::AB_AOE)) { Pos tp[16]; int tn = 0;   /* TF6A: 기절 주문 = 맞을 적 챔피언 모두의 다음 차례 위협 빼기 */
                    const int rr = a.kind == rules::AB_AOE ? std::max(1, (int)a.radius) : 0;
                    for (int tj = 0; tj < (int)targets.size() && tn < 16; ++tj) { const Target& u = targets[tj]; if (u.kind != 1 || cheb(u.pos, t.pos) > rr) continue;
                        if ((tj == ti && sd >= u.hp) || (tj == bt && me.str >= u.hp)) continue;   /* 죽는 적은 KFIN 이 셈 */
                        tp[tn++] = u.pos; }
                    v += tf6Relief(tp, tn, a.duration); }
                if (v > sv) { sv = v; st = ti; }
            }
            if (st < 0) continue;
            float krs = 0.f; if (targets[st].kind == 1 && sd >= targets[st].hp) krs = kfinRelief(targets[st]);   /* KFIN: 기술 마무리 */
            else if (bt >= 0 && targets[bt].kind == 1 && me.str >= targets[bt].hp) krs = kfinRelief(targets[bt]);
            ++nCombos; const float v3 = base + bv + sv + krs;
            if (better(v3, c.prog)) { bp = Pick{}; bp.cell = ci; bp.tgt = bt; bp.when = bw; bp.spell = s; bp.spellTgt = st; bp.v = v3; bp.prog = c.prog; }
            { Pick q; q.cell = ci; q.tgt = bt; q.when = bw; q.spell = s; q.spellTgt = st; q.v = v3; q.prog = c.prog; abPick(q); }   /* CDA */
            if (policy::SAFEP_ON && targets[(size_t)st].kind == 1 && !c.lethal && c.dmg <= 0.f && (v3 > bpS.v + 1e-3f || (v3 > bpS.v - 1e-3f && c.prog > bpS.prog + 1e-4f))) { bpS = Pick{}; bpS.cell = ci; bpS.tgt = bt; bpS.when = bw; bpS.spell = s; bpS.spellTgt = st; bpS.v = v3; bpS.prog = c.prog; }   /* SAFEP */
        }
    }
combosDone:
    if (lzdCk > 0 && lzdCF0 > 0) { const long long per = (lzdCF0 - io::fuel()) / lzdCk; g_lzdComb = std::max(per, g_lzdComb * 9 / 10); }   /* LZD2 */
    if (cellsDone == 0) { best.lethalAll = true; best.dmgTaken = me.hp; return best; }
    if (bpA.v > -1e4f && bpA.cell == bp.cell && bpA.v > bp.v - 1e-3f) bp = bpA;
    if (policy::SAFEP_ON && bpS.v > -1e4f) {   /* SAFEP: 고른 계획이 적 챔피언을 안 맞히면 안전한 칸의 기술 조합으로 */
        const bool hitsChamp = (bp.spell >= 0 && bp.spellTgt >= 0 && (size_t)bp.spellTgt < targets.size() && targets[(size_t)bp.spellTgt].kind == 1)
                            || (bp.tgt >= 0 && (size_t)bp.tgt < targets.size() && targets[(size_t)bp.tgt].kind == 1) || (bp.tgt2 >= 0 && (size_t)bp.tgt2 < targets.size() && targets[(size_t)bp.tgt2].kind == 1);
        if (!hitsChamp) bp = bpS; }   /* CDA2: 같은 칸 · 같은 값이면 기술 (다른 칸으로 바꾼 259번 / 8판이 모두 값을 잃음, 1856 젬) */   /* CDA: 준비된 기술은 닿는 표적이 있으면 그 턴에 쓴다 (죽을 칸 · 막힌 칸 제외, 후퇴 중엔 물러나는 걸음을 잃지 않을 때만) */
    if ((Mi.kind == mission::M_FARM || Mi.kind == mission::M_CAPTURE || Mi.kind == mission::M_TRIBUTE || Mi.kind == mission::M_RECOVER || Mi.kind == mission::M_SHOP) && d0 > 0 && d0 < 999 && bp.cell == 0 && bp.tgt < 0 && bp.spell < 0 && g_vulMode == 0 && !ccdLeave && !mission::cxeBackOff() && !(policy::SPH_ON && me.atSpawn && !w.enemies.empty())) {   /* MINE4 MSTEP (회복 · 후퇴 임무도: 제자리의 64% 가 집으로 가다 선 턴) */
        const bool rec = Mi.kind == mission::M_RECOVER || Mi.kind == mission::M_SHOP; const float hereDmg = cells[0].dmg;   /* 후퇴 중에는 지금 칸보다 더 맞는 칸으로는 안 감 */
        int bi = -1; float bdm = 0.f; for (int ci = 1; ci < (int)cells.size(); ++ci) { Cell& c = cells[ci]; if (c.dash) continue;
            if (policy::BH3_CE3_ON && lose > 0.f && cpsGroup(c.p) != g0) continue;   /* BH3_CE3: 쌓은 점령 몫을 잃는 칸 (무리 칸 밖) 으로는 MSTEP 이 걷지 않음 */
            if ((size_t)ci >= cellsDone) { if (c.d1 >= 999) c.d1 = mission::stepsToGoal(c.p); if (c.d1 >= d0) continue; const float thE = threats.empty() ? 0.f : threatAt(threats, c.p); const float thK = policy::BH3_CE1_ON ? campThreat(srcs, c.p) : 0.f; const float th = thE + thK;   /* 평가 안 된 칸: 위협 표로만 */   /* BH3_CE1: 캠프 피해도 */
                if (th >= me.hp * policy::LETHAL_MARGIN || (noEnter && th > 0.f) || (true && th * 2.f >= me.hp)) continue; c.dmg = th; c.fEd = policy::BH3_CE1_ON ? thE : th; c.fKd = thK; c.lethal = false; c.prog = (float)(d0 - c.d1) * stepW; }
            else if (c.lethal || c.prog <= -1e4f || c.d1 >= d0 || (true && c.dmg * 2.f >= me.hp)) continue;   /* MINE7: 두 턴 (이번 · 다음) 예보 피해를 견디는 칸만 */
            if (rec && c.dmg > hereDmg + 1e-3f) continue;
            if (bi < 0 || c.dmg < bdm - 1e-3f || (c.dmg < bdm + 1e-3f && c.d1 < cells[(size_t)bi].d1)) { bi = ci; bdm = c.dmg; } }
        if (bi < 0 && g_stuckTurns >= 1) for (int ci = 1; ci < (int)cells.size() && ci < (int)cellsDone; ++ci) { const Cell& c = cells[ci];   /* 목표 쪽 칸이 모두 막힘 (캠프 칸 · 아군) 이고 지난 턴도 섰으면 옆 칸 (d1 == d0) 으로 돌아감 */
            if (c.dash || c.lethal || c.prog <= -1e4f || c.d1 != d0 || c.dmg > hereDmg + 1e-3f || (true && c.dmg * 2.f >= me.hp)) continue;
            if (policy::BH3_CE3_ON && lose > 0.f && cpsGroup(c.p) != g0) continue;   /* BH3_CE3: stuck 옆 칸도 같은 거름 */
            if (bi < 0 || c.dmg < bdm - 1e-3f) { bi = ci; bdm = c.dmg; } }
        if (bi >= 0) { bp = Pick{}; bp.cell = bi; bp.v = -dmgCostF(cells[(size_t)bi].dmg, me.hp, me.maxHp, me.gems, lhfF(cells[(size_t)bi])) + cells[(size_t)bi].prog;   /* LHF */ bp.prog = cells[(size_t)bi].prog; ++g_mstep; } }
    if (policy::ATKF_ON && ranged && Mi.kind == mission::M_FARM && campOnly && !mission::cxeBackOff() && !g_nfHold && g_vulMode == 0 && !noEnter && me.canAttack && !cells.empty() && bp.cell != 0 && bp.tgt < 0 && bp.spell < 0) {   /* ATKF: 딜로스 0 — 제자리에서 칠 수 있고 안전하면 공격 */
        const Cell& c0 = cells[0]; int bt0 = -1; float bv0 = 0.f;
        for (int ti = 0; ti < (int)targets.size(); ++ti) { const Target& t = targets[(size_t)ti]; if (t.kind == 2 && !t.fb && cheb(t.pos, me.pos) <= me.range && t.hitVal > bv0) { bv0 = t.hitVal; bt0 = ti; } }
        if (bt0 >= 0 && !c0.lethal && c0.dmg * 2.f < me.hp) { const float v0 = (c0.finishOk ? 0.f : -dmgCostF(c0.dmg, me.hp, me.maxHp, me.gems, lhfF(c0))) + c0.prog + bv0; Pick q; q.cell = 0; q.tgt = bt0; q.when = 1; q.v = v0; q.prog = c0.prog; bp = q; } }
    // 완전히 평가한 조합 중 최선을 실행한다. 중간에 끊긴 범위 주문은 선택하지 않는다.
    const Cell& bc = cells[(size_t)bp.cell];
    best.cell = bc.p; best.move = bc.p != me.pos && !bc.dash; best.dash = bc.dash; best.dashSlot = bc.dashSlot; best.dashWalk = bc.dw; best.dashTo = bc.dw ? bc.mid : bc.p;   /* DWK */
    best.value = bp.v; best.dmgTaken = bc.dmg; best.dmgDealt = bp.prog; best.hereDmg = cells[0].dmg;   /* RCC */
    if (bp.tgt >= 0) { const Target& t = targets[(size_t)bp.tgt]; best.atkKind = t.kind; best.atkHandle = t.handle; best.atkWhen = bp.when; best.resetAttack = bp.reset; best.resetSlot = resetSlot;
        if (bp.tgt2 >= 0) { const Target& t2 = targets[(size_t)bp.tgt2]; best.atk2Kind = t2.kind; best.atk2Handle = t2.handle; } }   /* RST */
    if (bp.spell >= 0) {
        const Target& t = targets[(size_t)bp.spellTgt]; const AbilitySpec& a = D.ab[bp.spell];
        best.castSlot = bp.spell; best.castBeforeMove = a.noWalk;
        if (a.kind == rules::AB_AOE || a.targetsGround) { best.castKind = 3; best.castPos = t.pos; } else { best.castKind = t.kind; best.castHandle = t.handle; }
    }
    if (policy::CC_ON && best.castSlot < 0 && !best.dash && best.atkWhen != 4 && me.canCast && D.valid) {   /* RWA: 리셋을 쓴 턴 */   // CC
        for (int s = 0; s < 2 && best.castSlot < 0; ++s) {
            const AbilitySpec& a = D.ab[s];
            if (a.kind != rules::AB_CC || me.cooldown[s] != 0) continue;
            if (a.noWalk && best.move) continue;
            const Pos from = a.noWalk ? me.pos : best.cell; const int R = (int)a.range;
            if (a.targetsEnemies) {
                const Unit* te = nullptr; int bd = 99;
                for (const auto& e : w.enemies) { const int dd = cheb(e.pos, from); if (dd <= R && dd < bd) { bd = dd; te = &e; } }
                if (policy::TF6A_ON && te && a.duration > 0 && !threats.empty()) {   /* TF6A: 고른 칸에서 기절로 빠지는 위협이 가장 큰 적 (같으면 가장 가까운 적 그대로) */
                    auto dropOf = [&](const Unit& e) { for (const auto& th : threats) if (th.pos == e.pos) return tf6Drop(w, th, best.cell, a.duration); return 0.f; };
                    float bE = dropOf(*te);
                    for (const auto& e : w.enemies) { if (cheb(e.pos, from) > R) continue; const float rE = dropOf(e); if (rE > bE + 1e-3f) { bE = rE; te = &e; } } }
                if (te) { best.castSlot = s; best.castKind = 1; best.castHandle = te->handle; best.castBeforeMove = a.noWalk; break; }
            }
            if (a.targetsCamps) {
                const CampUnit* tc = nullptr; float bs = 0.f;
                for (const auto& k : w.camps) {
                    if (k.str <= 0.f || cheb(k.pos, from) > R || k.group < 0) continue;
                    const Group& G = S().groups[(size_t)k.group];
                    const bool hitNow = best.atkKind == 2 && best.atkHandle == k.handle;
                    if (!(G.awakeUntil >= w.round || hitNow)) continue;
                    const int kr = S().campRange(k.type);   // ED
                    bool body = cheb(k.pos, best.cell) <= kr; for (const auto& al : w.allies) if (cheb(k.pos, al.pos) <= kr) body = true;
                    if (!body) continue;
                    if (k.str > bs) { bs = k.str; tc = &k; }
                }
                if (tc) { best.castSlot = s; best.castKind = 2; best.castHandle = tc->handle; best.castBeforeMove = a.noWalk; }
            }
        }
    }
    if (ccdLeave) g_ccdLeave = w.round;   /* CCD */
    else if (policy::CCD_ON && ccdHere > 0.f && Mi.kind == mission::M_CAPTURE && !bc.dash && bc.p != me.pos && S().campGroupAt[(size_t)S().idx(bc.p)] == Mi.group && S().campGroupAt[(size_t)S().idx(me.pos)] == Mi.group && bc.dmg < cells[0].dmg) g_ccdMove = w.round;
#ifdef CCD_PROBE
    if (ccdLeave) log::always("#ccdx leave %d %d hp=%.0f here=%.0f", w.round, S().myIndex, me.hp, ccdHere);
    else if (g_ccdMove == w.round) log::always("#ccdx move %d %d hp=%.0f here=%.0f to=%.0f", w.round, S().myIndex, me.hp, cells[0].dmg, bc.dmg);
#endif
    best.nCells = (int)cells.size(); best.nCombos = nCombos;
    { const long long fC = io::fuel(); best.fuelCells = f0_ - fA; best.fuelDmg = fA - fB; best.fuelCombos = fB - fC; }
    // 5. 스스로 실드 (받을 피해가 있고 준비됐으면)
    if (me.canCast && D.valid && best.castSlot < 0 && !best.resetAttack && best.atkWhen != 4 && best.dmgTaken > 0.f && !(policy::BUGX_ON && best.dash))   /* RWA */ for (int s = 0; s < 2; ++s)
        if (D.ab[s].kind == rules::AB_SHIELD && me.cooldown[s] == 0) { best.castSlot = s; best.castKind = 0; }
    if (best.atkKind == 1 || best.castKind == 1) s_hysHit = w.round;   /* HYS */
    if (best.lethalAll) ++Mi.lethalStreak; else Mi.lethalStreak = 0;
    return best;
}

/* GDS (렌즈 G · 처치): 싸움 턴은 mission::update 와 걷기 칸이 연료를 먼저 써서 choose 의 대시 칸 (fuel > 30000 문 · 칸 목록 끝) 이 평가되지 않는다
   (notes/gen_1102b/dashuse/measure.md: 연료 9만+ 턴 대시 몫 0.10, 여유 턴 0.34). 래더 240판: 대시 준비 · 적 챔피언 3칸 안 R 에서 챔피언 대시 우리 13% : 랭커 33%,
   40% 미만 적이면 26% : 61%, HoR 처치 판당 0.63 : 3.05. 그래서 턴 시작 (연료 가득) 에 choose 의 대시 칸과 같은 식으로 최선의 챔피언 대시 하나를 미리 센다:
   값 = 반경 안 챔피언 champValue(sd) + 착지 뒤 평타 몫, 비용 = 착지 칸 예보 (처치 KFIN · 기절 TF6A 로 빠지는 위협 뺌) 의 dmgCostF. 계획 교체는 gdsApply 에서만. */
bool g_gdsSeen = false;
namespace { struct GdsPick { bool ok = false, lethal = false; int round = -1, slot = -1; Pos land{-1, -1}; std::uint32_t mainH = 0; float v = 0.f, cost = 0.f, dL = 0.f; }; GdsPick s_gds; std::vector<Threat> s_gdsTs; }
void gdsPrep(const World& w) {
    g_gdsSeen = false; s_gds = GdsPick{}; s_gds.round = w.round;
    const Me& me = w.me;
    if (!policy::GDS_ON || !me.alive || me.channeling || !me.canCast || w.enemies.empty() || io::fuel() < policy::GDS_FUEL) return;
    const ChampDesign& D = S().designOf(me.type); if (!D.valid) return;
    int slot = -1; for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH && D.ab[s].modifier > 0.f && me.cooldown[s] == 0) slot = s;
    if (slot < 0 || (policy::DFB_ON && w.round - s_dashFailR <= 3)) return;   /* DFB 와 같음 */
    const AbilitySpec& da = D.ab[slot]; const int rad = std::max(1, (int)da.radius), rng = std::max(1, (int)da.range);
    const float sd = me.focus * da.modifier, str = me.canAttack ? me.str : 0.f;
    int ce[8]; int nc = 0;
    for (int i = 0; i < (int)w.enemies.size() && nc < 8; ++i) { const Unit& e = w.enemies[(size_t)i]; if (e.alive && !onEnemySpawn(e.pos) && cheb(e.pos, me.pos) <= rng + rad) ce[nc++] = i; }
    if (nc == 0) return;
    const std::vector<Threat>& ts = policy::FS_COM3_ON ? fs3Threats(w) : (buildThreats(w, s_gdsTs), s_gdsTs);
    if (ts.size() != w.enemies.size()) return;
    const float kg = (float)S().killGold / mission::goldDiv();
    Target tg[8];
    for (int j = 0; j < nc; ++j) { const Unit& e = w.enemies[(size_t)ce[j]]; Target& t = tg[j]; t.kind = 1; t.handle = e.handle; t.pos = e.pos; t.hp = e.hp; t.maxHp = e.maxHp; t.group = -1; t.channeling = false; t.hitVal = 0.f; t.ally = 0.f; t.fb = false;
        t.full = (float)e.gems + kg + policy::DEATH_TIME_GEMS + enemyTwinAdd(w, e);   /* buildTargets 와 같음 */
        if (policy::FOC2_ON) for (const auto& a : w.allies) if (a.visible && a.alive && !a.acted && cheb(a.pos, e.pos) <= (a.range > 1 ? a.range : 2)) t.ally += a.str; }
    const CampUnit* kc[16]; int nk = 0;
    for (const auto& k : w.camps) if (k.hp > 0.f && k.str > 0.f && nk < 16 && cheb(k.pos, me.pos) <= rng + S().campRange(k.type)) kc[nk++] = &k;
    for (int dy = -rng; dy <= rng; ++dy) for (int dx = -rng; dx <= rng; ++dx) {
        if (!dx && !dy) continue; const Pos L{me.pos.x + dx, me.pos.y + dy};
        int in[8]; int ni = 0; for (int j = 0; j < nc; ++j) if (cheb(tg[j].pos, L) <= rad) in[ni++] = j;
        if (ni == 0) continue;
        if (!S().inb(L) || onEnemySpawn(L) || !freeTile(w, L, mission::M) || !io::seesTile(L) || !dashPathFree(w, L)) continue;
        bool campHot = false;   /* 캠프 피해는 여기서 안 셈 → 깬 캠프 사거리 · 착지로 깨울 캠프가 걸린 칸은 choose 에 맡김 */
        for (int q = 0; q < nk; ++q) { const CampUnit& k = *kc[q]; const int dk = cheb(k.pos, L); if (dk > S().campRange(k.type)) continue;
            if (dk <= rad || k.hp < k.maxHp || (k.group >= 0 && S().groups[(size_t)k.group].awakeUntil >= w.round)) { campHot = true; break; } }
        if (campHot) continue;
        float V = 0.f, mAdd = 0.f; int mj = -1;
        for (int a = 0; a < ni; ++a) { const Target& t = tg[in[a]]; const float v1 = champValue(t, sd); V += v1;
            if (str > 0.f && t.hp > sd) { const float add = champValue(t, sd + str) - v1; if (add > mAdd) { mAdd = add; mj = in[a]; } } }   /* 착지 뒤 평타 (choose 3b 의 대시 칸 평타) */
        V += mAdd;
        float dmg = threatAtTgt(w, ts, L); bool kill = false;
        for (int a = 0; a < ni; ++a) { const int j = in[a]; const Threat& th = ts[(size_t)ce[j]]; const int d = cheb(th.pos, L);
            if (tg[j].hp <= sd || (j == mj && tg[j].hp <= sd + str)) { kill = true; float k = 1.f; for (const auto& al : w.allies) if (al.visible && cheb(al.pos, th.pos) < d && al.hp <= me.hp) { k = 0.15f; break; } dmg -= k * thDmg(th, d); }   /* KFIN */
            else if (policy::TF6A_ON && da.duration > 0) dmg -= tf6Drop(w, th, L, (int)da.duration); }   /* TF6A */
        dmg = std::max(0.f, dmg);
        if (dmg >= me.hp * policy::LETHAL_MARGIN) continue;
        const float cost = dmgCostF(dmg, me.hp, me.maxHp, me.gems, dmg);   /* LHF: F = 적 예보 */
        const float sc = V - cost, sb = s_gds.v - s_gds.cost;
        if (!s_gds.ok || sc > sb + 1e-3f || (sc > sb - 1e-3f && dmg < s_gds.dL)) { s_gds.ok = true; s_gds.slot = slot; s_gds.land = L; s_gds.v = V; s_gds.cost = cost; s_gds.dL = dmg; s_gds.lethal = kill; s_gds.mainH = mj >= 0 ? tg[mj].handle : 0u; }
    }
}
void gdsApply(const World& w, const mission::Mission& Mi, Plan& p) {   /* GDS: choose 가 챔피언 대시 칸을 평가 못 한 턴에만 */
    if (!policy::GDS_ON || !s_gds.ok || s_gds.round != w.round || g_gdsSeen || p.dash) return;
    const bool backOff = Mi.kind == mission::M_RECOVER || Mi.kind == mission::M_TRIBUTE || Mi.kind == mission::M_SHOP;
    if (backOff && !s_gds.lethal) return;   /* 물러남 · 바치기 · 상점 길의 걸음 값 (prog) 은 여기서 안 셈 → 처치 대시만 */
    if (!s_gds.lethal && policy::R2_NO_ENTER && p.hereDmg <= 0.f && s_gds.dL > 0.f && mission::r3Outnumbered(w)) return;   /* R2: 열세인데 아직 사거리 밖이면 들어가지 않음 */
    if (s_gds.v - s_gds.cost <= p.value - p.dmgDealt) return;   /* 같은 단위: 계획의 (값 - 진행) 과 비교 (계산 못 한 턴은 0) */
    p.dash = true; p.dashSlot = s_gds.slot; p.dashWalk = false; p.cell = s_gds.land; p.dashTo = s_gds.land; p.move = false;
    p.atkKind = 0; p.atkHandle = 0; p.atkWhen = 0; p.resetAttack = false; p.atk2Kind = 0; p.atk2Handle = 0;
    if (s_gds.mainH != 0u && w.me.canAttack) { p.atkKind = 1; p.atkHandle = s_gds.mainH; p.atkWhen = 2; }
    p.castSlot = -1; p.castKind = 0; p.castBeforeMove = false;   /* BUGX: 대시한 턴에 다른 시전 없음 */
    p.value = s_gds.v - s_gds.cost; p.dmgTaken = s_gds.dL; p.dmgDealt = 0.f; p.lethalAll = false; p.cut = false;
    NB_LINE("#gds %d %d land=%d,%d v=%.1f cost=%.1f dL=%.0f lethal=%d", w.round, S().myIndex, s_gds.land.x, s_gds.land.y, s_gds.v, s_gds.cost, s_gds.dL, (int)s_gds.lethal);
}

}  // namespace nb::combat
