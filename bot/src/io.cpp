#include <algorithm>
// io.cpp — 엔진 호출 전부. 단가 (탐침 2026-10-19, collision): sdk_champion 5~8 · cooldown 6 · camp_group_state 8 · camp 8 · camp_at 15
// · read_shared 7 · field.compute 2.4k (41²) · field.get 108 · bitboard.is_set 99 · export_linear(bitboard) 405 · champion_design 142
// · vision::champions 14.8k (금지) · vision::camps 1.4k (금지) · move_to 13.
#include <cambc.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "io.h"
#include "policy.h"
#include "draft_features_sdk.h"   /* DRF */

namespace bc = cambc;
namespace d = cambc::detail;

namespace nb::io {

Static S;
static std::string g_err;
const char* lastError() { return g_err.c_str(); }

long long fuel() { return (long long)d::sdk_fuel_remaining(); }
int round() { return (int)bc::round_number(); }
static int s_turnR = 0; int turnRound() { return s_turnR; }   /* FUELH */
static int s_icR = -1; static bool s_icOk = false;   /* FUELX: 아이템 수 턴마다 한 번 */
static std::uint32_t s_actEp = 0;   /* FS_IO1: 내 행동 번호 (행동마다 +1 → 아이템 기억 지움) */
bool isDraft() { return bc::is_draft(); }
void endTurn() { bc::end_turn(); }

static std::uint32_t hOf(const bc::champion& c) { return static_cast<std::uint32_t>(static_cast<bc::handle_champion_t>(c)); }

// ---------------------------------------------------------------- 정적
// bitboard → 칸 비트. export_linear_buffer 는 비트 포장 (W*H/8 바이트). 비트 순서는 첫 호출에서 is_set 과 대조해 정한다.
static int g_bitOrder = 0;   // 0 = LSB 먼저 (collision · lanes 에서 24/24 확인), 1 = MSB 먼저
// 6 마스크를 한 번에: 바이트 고리 한 번 (연산 하나 ~10 연료라 바이트 338 × 마스크 6 을 따로 돌면 30k, 칸마다 돌면 160k)
static std::vector<std::uint8_t> g_staticWallBytes;   // WB: 앞으로 옮김 (초기화에서 채움)
// ZO2: 바이트 v 의 8비트를 8바이트 (0x01 씩) 로 펼친 표 — LSB 순서 / MSB 순서
struct Spread { std::uint64_t lsb[256], msb[256]; };
static constexpr Spread makeSpread() { Spread s{}; for (int v = 0; v < 256; ++v) { std::uint64_t a = 0, b = 0; for (int j = 0; j < 8; ++j) if (v >> j & 1) { a |= 1ull << (8 * j); b |= 1ull << (8 * (7 - j)); } s.lsb[v] = a; s.msb[v] = b; } return s; }
static constexpr Spread kSpread = makeSpread();
static void decodeMasks() {
    const bc::bitboard* boards[6] = {&bc::map::walls(), &bc::map::holes(), &bc::map::bushes(), &bc::map::altars(), &bc::map::spawn_region(bc::team::ally), &bc::map::spawn_region(bc::team::enemy)};
    const std::uint8_t bits[6] = {T_WALL, T_HOLE, T_BUSH, T_ALTAR, T_ALLY_SPAWN, T_ENEMY_SPAWN};
    std::vector<std::uint8_t> by[6];
    for (int k = 0; k < 6; ++k) by[k] = boards[k]->export_linear_buffer();
    {   // WB: 정적 막힘 = 벽 | 구멍 | 제단 | 적 스폰 (T_BLOCKED 와 같은 판)
        const size_t nbytes = bc::bitboard::linear_buffer_size();
        std::vector<std::uint8_t> v(nbytes, 0);
        const size_t m = std::min({nbytes, by[0].size(), by[1].size(), by[3].size(), by[5].size()});
        size_t i = 0;
        for (; i + 8 <= m; i += 8) { std::uint64_t a, b, c2, e, o; std::memcpy(&a, &by[0][i], 8); std::memcpy(&b, &by[1][i], 8); std::memcpy(&c2, &by[3][i], 8); std::memcpy(&e, &by[5][i], 8); o = a | b | c2 | e; std::memcpy(&v[i], &o, 8); }   // 8바이트씩
        for (; i < m; ++i) v[i] = (std::uint8_t)(by[0][i] | by[1][i] | by[3][i] | by[5][i]);
        g_staticWallBytes.swap(v);
    }
    const int n = S.W * S.H, nb = (n + 7) / 8;
    { // 비트 순서 대조 (벽 또는 덤불 마스크에서 몇 칸)
        int agreeL = 0, agreeM = 0, tried = 0; const int k = by[2].size() ? 2 : 0;
        for (int i = 0; i < n && tried < 12; i += std::max(1, n / 12)) {
            const bool real = boards[k]->is_set(bc::vec2(i % S.W, i / S.W));
            const bool lsb = (by[k][(size_t)(i >> 3)] >> (i & 7)) & 1, msb = (by[k][(size_t)(i >> 3)] >> (7 - (i & 7))) & 1;
            agreeL += lsb == real; agreeM += msb == real; ++tried;
        }
        g_bitOrder = agreeL >= agreeM ? 0 : 1;
        if (agreeL != tried && agreeM != tried) std::printf("#nb warn bitorder agree %d/%d of %d\n", agreeL, agreeM, tried);
    }
    {   // ZO4: 8바이트 워드 단위
        const std::uint64_t* tab = g_bitOrder == 0 ? kSpread.lsb : kSpread.msb;
        const size_t nw = ((size_t)nb + 7) / 8;
        for (int k = 0; k < 6; ++k) {
            std::vector<std::uint8_t>& v = by[k];
            if (v.size() < nw * 8) v.resize(nw * 8, 0);
            const std::uint64_t rep8 = 0x0101010101010101ull * bits[k];
            for (size_t wI = 0; wI < nw; ++wI) {
                std::uint64_t word; std::memcpy(&word, &v[wI * 8], 8);
                if (!word) continue;
                for (int bb = 0; bb < 8; ++bb) {
                    const unsigned byteV = (unsigned)((word >> (8 * bb)) & 0xFF); if (!byteV) continue;
                    const size_t b8 = wI * 8 + (size_t)bb; if (b8 >= (size_t)nb) break;
                    const size_t base = b8 * 8;
                    if (k < 3) {
                        const std::uint64_t orv = (tab[byteV] * 0xFFull) & rep8;
                        if (base + 8 <= (size_t)n) { std::uint64_t cur; std::memcpy(&cur, &S.tile[base], 8); cur |= orv; std::memcpy(&S.tile[base], &cur, 8); }
                        else for (int j = 0; j < 8; ++j) if (base + j < (size_t)n) S.tile[base + j] |= (std::uint8_t)(orv >> (8 * j));
                    } else {
                        unsigned vv = byteV;
                        while (vv) {
                            const int j = __builtin_ctz(vv); vv &= vv - 1;
                            const int i = (int)b8 * 8 + (g_bitOrder == 0 ? j : 7 - j); if (i >= n) continue;
                            S.tile[(size_t)i] |= bits[k];
                            if (k == 3) S.altars.push_back({i % S.W, i / S.W});
                            else if (k == 4) S.allySpawn.push_back({i % S.W, i / S.W});
                            else S.enemySpawn.push_back({i % S.W, i / S.W});
                        }
                    }
                }
            }
        }
    }
}

unsigned g_fs2Gen = 1;   /* FS_COM2 */
static void loadDesigns() {
    ++g_fs2Gen;   /* FS_COM2: 디자인이 바뀌면 fnDps 메모를 지움 */
    for (int t = 0; t < MAX_CHAMP_TYPES; ++t) {   // ED: 엔진이 아는 종류까지 (패치: 12 Heart of Ruin)
        d::champion_design cd{};
        if (d::sdk_champion_design(t, &cd) != 0) continue;
        ChampDesign& D = S.design[t];
        D.valid = true; D.type = t; D.attackRange = cd.base_stats.base_attack_range;
        D.hp = cd.base_stats.base_health; D.str = cd.base_stats.base_strength; D.focus = cd.base_stats.base_focus;
        D.hpGrow = cd.base_stats.health_growth; D.strGrow = cd.base_stats.strength_growth; D.focusGrow = cd.base_stats.focus_growth;
        D.passive = cd.definition.passive_type;
        for (int s = 0; s < 2; ++s) {
            const auto& p = cd.definition.abilities[s]; AbilitySpec& a = D.ab[s];
            a.kind = (int)p.kind; a.cooldown = (int)p.cooldown; a.noWalk = (p.attributes & d::ability_attribute_exclusive_with_movement) != 0;
            a.targetsCamps = p.targets & d::ability_target_camps; a.targetsEnemies = p.targets & d::ability_target_enemies;
            a.targetsAllies = p.targets & d::ability_target_allies; a.targetsSelf = p.targets & d::ability_target_self; a.targetsGround = p.targets & d::ability_target_ground;
            switch (a.kind) {
            case rules::AB_DAMAGE: a.range = p.payload.damage.range; a.modifier = p.payload.damage.modifier; break;
            case rules::AB_HEAL: a.range = p.payload.heal.range; a.modifier = p.payload.heal.modifier; break;
            case rules::AB_AOE: a.range = p.payload.aoe_damage.range; a.modifier = p.payload.aoe_damage.modifier; a.radius = p.payload.aoe_damage.radius; a.duration = p.payload.aoe_damage.duration; break;
            case rules::AB_CC: a.range = p.payload.cc.range; a.modifier = p.payload.cc.modifier; a.duration = p.payload.cc.duration; break;
            case rules::AB_SHIELD: a.range = p.payload.shield.range; a.modifier = p.payload.shield.modifier; a.duration = p.payload.shield.duration; break;
            case rules::AB_DASH: a.range = p.payload.dash.range; a.radius = p.payload.dash.radius; a.modifier = p.payload.dash.modifier; a.duration = p.payload.dash.duration; break;   // HD: 착지 피해 · 기절
            default: break;
            }
        }
    }
    for (int t = 0; t < MAX_CAMP_TYPES; ++t) {   // ED: 엔진이 아는 캠프 종류까지 (패치: 골렘)
        d::camp_design cd{};
        if (d::sdk_camp_design(t, &cd) != 0) continue;
        S.camp[t].hp = cd.base_stats.base_health; S.camp[t].str = cd.base_stats.base_strength; S.camp[t].range = cd.base_stats.base_attack_range; S.camp[t].valid = true;
        if (t != rules::C_MONOLITH && S.camp[t].str > 0.f) S.campRangeStrong = std::max(S.campRangeStrong, S.camp[t].range);   // ED: 카이팅 주문 사거리 기준
    }
    { d::game_settings gs{}; if (d::sdk_get_settings(&gs) == 0) { S.killGold = (int)gs.kill_gold; S.lockHits = (int)gs.altar_lock_hits; S.wardCooldownSetting = gs.ward_cooldown; S.subBase = (int)gs.submit_gem_base_duration; S.subMin = (int)gs.submit_gem_minimum_duration; S.subMult = gs.submit_gem_duration_multiplier; } }   // ED: 엔진 설정
}

static bool s_stage1 = false;   // IS
void initStatic() {
    const long long f0 = fuel();
    if (!s_stage1) {   // 첫 턴에는 지도만 읽는다.
    S.W = bc::map::width(); S.H = bc::map::height();
    const size_t nTiles = (size_t)(S.W * S.H);
    S.tile.resize(nTiles); std::memset(S.tile.data(), 0, nTiles);
    S.campBlocked.resize(nTiles); std::memset(S.campBlocked.data(), 0, nTiles);
    S.openCampTiles = 0; S.campVersion = 1;
    S.campGroupAt.resize(nTiles); std::memset(S.campGroupAt.data(), 0xFF, nTiles * sizeof(std::int16_t));   // -1 (GRP: int16)
    S.occStamp.resize(nTiles); std::memset(S.occStamp.data(), 0, nTiles);
    S.altars.reserve(32); S.allySpawn.reserve(64); S.enemySpawn.reserve(64);
    decodeMasks();
    { long long ax = 0, ay = 0, ex = 0, ey = 0;
      for (const auto& p : S.allySpawn) { ax += p.x; ay += p.y; } for (const auto& p : S.enemySpawn) { ex += p.x; ey += p.y; }
      S.allyCenter = S.allySpawn.empty() ? Pos{0, 0} : Pos{(int)(ax / (long long)S.allySpawn.size()), (int)(ay / (long long)S.allySpawn.size())};
      S.enemyCenter = S.enemySpawn.empty() ? Pos{S.W - 1, S.H - 1} : Pos{(int)(ex / (long long)S.enemySpawn.size()), (int)(ey / (long long)S.enemySpawn.size())}; }
    S.mapCenter = {S.W / 2, S.H / 2};
    s_stage1 = true;
    if (!policy::R1M_ON && fuel() < 120000) return;   /* FTM3 · R1M: 첫 턴에 계속 읽음 */
    }
    static bool designsLoaded = false;
    if (!designsLoaded) { loadDesigns(); designsLoaded = true; if (fuel() < 30000) return; }
    const long long f2x = fuel();
    // 그룹: 원시 배치 (한 호출) — [그룹, 금, 젬, 리스폰, 캠프 수, (캠프 핸들, 종류, x, y) × 수]. 래퍼 (camp_groups) 는 객체를 만들어 25k
    static std::vector<std::uint32_t> words;
    static std::uint32_t count = 0;
    static size_t cursor = 0, seenBase = 0;
    static std::uint32_t member = 0, size = 0;
    static Group G;
    static long long cx = 0, cy = 0;
    static bool loaded = false, building = false;
    if (!loaded) {
    words.resize(512);
    if (d::sdk_map_camp_layout(words.data(), (std::uint32_t)words.size(), &count) == 0 && count > words.size()) { words.resize(count); d::sdk_map_camp_layout(words.data(), count, &count); }
    S.campSeenRound.resize(count / 4);
    if (!S.campSeenRound.empty()) std::memset(S.campSeenRound.data(), 0xFF, S.campSeenRound.size() * sizeof(int));
    S.groups.clear(); S.groups.reserve(128);
    loaded = true;
    }
    while (building || cursor + 5 <= count) {
        if (fuel() < (policy::R1M_ON ? policy::R1M_INIT_FLOOR : 18000)) return;   /* R1M: 걸음 몫 남김 */
        if (!building) {
        G = Group{}; G.handle = words[cursor]; G.gold = (int)words[cursor + 1]; G.gems = (int)words[cursor + 2]; G.respawn = (int)words[cursor + 3]; size = words[cursor + 4]; cursor += 5;
        G.campHandle.reserve(size); G.spawn.reserve(size); G.campType.reserve(size); G.campLastHp.assign(size, -1.f); G.seenOffset = seenBase; seenBase += size;   // 할당은 비싸다 (push_back 마다 realloc)
        cx = cy = 0; member = 0; building = true;
        }
        for (; member < size && cursor + 4 <= count; ++member, cursor += 4) {
            if (fuel() < (policy::R1M_ON ? policy::R1M_INIT_FLOOR - 3000 : 15000)) return;   /* R1M */
            const int t = (int)words[cursor + 1]; const Pos p{(int)words[cursor + 2], (int)words[cursor + 3]};
            G.campHandle.push_back(words[cursor]); G.spawn.push_back(p); G.campType.push_back(t);
            if (t >= 0 && t < MAX_CAMP_TYPES && S.camp[t].valid) { G.hpTotal += S.camp[t].hp; G.strMax = std::max(G.strMax, S.camp[t].str); if (S.camp[t].str > 0.f) G.rangeMax = std::max(G.rangeMax, S.camp[t].range); }   // ED
            if (t == rules::C_MONOLITH) G.mono = true;   // ED: 모노리스 그룹은 종류 번호로
            cx += p.x; cy += p.y;
            if (S.inb(p)) { S.tile[(size_t)S.idx(p)] |= T_CAMP; S.campGroupAt[(size_t)S.idx(p)] = (int)S.groups.size(); S.campBlocked[(size_t)S.idx(p)] = 1; }
        }
        const int n = (int)G.spawn.size(); G.center = n ? Pos{(int)(cx / n), (int)(cy / n)} : Pos{0, 0};
        S.groups.push_back(std::move(G));
        building = false;
    }
    const long long f3 = fuel();
    std::printf("#nb initcost masks=%lld designs=%lld groups=%lld\n", 0LL, 0LL, f2x - f3);
    if (fuel() < 30000) return;   // FFR: 그룹 읽기 뒤 챔피언 목록은 다음 턴에 (split: 그룹 71k + 목록 → 첫 턴 넘침)
    for (const auto& c : bc::champions(bc::team::ally)) S.allyHandle.push_back(hOf(c));
    for (const auto& c : bc::champions(bc::team::enemy)) S.enemyHandle.push_back(hOf(c));
    S.nAllies = (int)S.allyHandle.size(); S.nEnemies = (int)S.enemyHandle.size();
    S.myIndex = (int)d::sdk_my_champion();
    S.ready = true;
    std::printf("#nb init W=%d H=%d groups=%zu altars=%zu allies=%d enemies=%d me=%d fuel=%lld\n", S.W, S.H, S.groups.size(), S.altars.size(), S.nAllies, S.nEnemies, S.myIndex, f0 - fuel());
}

// ---------------------------------------------------------------- 턴 스냅숏
static void fillUnit(Unit& u, std::uint32_t h, const d::champion_info& ci, bool withDetail) {
    u.handle = h; u.visible = true; u.type = ci.type; u.pos = {ci.x, ci.y}; u.hp = ci.health; u.maxHp = ci.max_health; u.str = ci.strength; u.focus = ci.focus;
    u.range = ci.attack_range; u.state = (std::uint32_t)ci.state; u.alive = (u.state & rules::ST_ALIVE) != 0; u.acted = (u.state & rules::ST_COMPLETED_TURN) != 0;
    if (withDetail) {
        for (int s = 0; s < 2; ++s) { std::int32_t cd = 0; if (d::sdk_champion_cooldown(h, s, &cd) == 0) u.cooldown[s] = cd; }
        std::uint32_t g = 0; if (d::sdk_champion_gems(h, &g) == 0) u.gems = (int)g;
        std::int32_t p = 0; if (d::sdk_bc_champion_priority(h, 0, &p) == 0) u.prio = p;
        if (policy::BH4_HR8_ON && fuel() > 30000) {   /* BH4_HR8: 기절 남은 라운드 (적용 라운드 연장 포함 → 보수적으로 1, duration>=2 면 2) */
            d::buff_data raw[3]{}; std::uint32_t cnt = 0;
            if (d::sdk_champion_buff_data(h, raw, 3, &cnt) == 0) for (std::uint32_t i = 0; i < cnt && i < 3; ++i)
                if (raw[i].type == static_cast<std::uint32_t>(cambc::buff_types::stun) && raw[i].data.stun.duration > 0) u.stunLeft = raw[i].data.stun.duration >= 2 ? 2 : 1;
        }
    }
}

static bc::bitboard s_vbVis{bc::uninitialized}; static int s_vbRound = -1; static bool s_vbOk = false;   // VB
bool seesTile(Pos p) {
    const int r = round();
    if (s_vbRound != r) { s_vbRound = r; s_vbOk = d::sdk_vision(s_vbVis.ptr()) == 0; }
    if (!s_vbOk || !S.inb(p)) return false;   // 조회 실패는 사망/빈칸의 증거가 아니다
    return s_vbVis.is_set(bc::vec2(p.x, p.y));
}
bool seesTileQ(Pos p) { return s_vbOk && S.inb(p) && s_vbVis.is_set(bc::vec2(p.x, p.y)); }   /* CSX: seesTile 을 이번 라운드에 한 번 부른 뒤 */
static std::vector<std::vector<int>> g_bk; static int g_bkW = 0, g_bkH = 0; constexpr int BKS = 12;   /* SNS: 그룹 중심 격자 */
void nearGroups(Pos p, int r, std::vector<int>& out) {   /* SNS: 중심이 p 에서 cheb r 안인 그룹 (번호 오름차순) */
    out.clear();
    if (g_bk.empty() && !S.groups.empty()) { g_bkW = (S.W + BKS - 1) / BKS; g_bkH = (S.H + BKS - 1) / BKS; g_bk.assign((size_t)(g_bkW * g_bkH), {});
        for (size_t gi = 0; gi < S.groups.size(); ++gi) { const Pos c = S.groups[gi].center; const int bx = std::clamp(c.x / BKS, 0, g_bkW - 1), by = std::clamp(c.y / BKS, 0, g_bkH - 1); g_bk[(size_t)(by * g_bkW + bx)].push_back((int)gi); } }
    if (g_bk.empty()) return;
    const int x0 = std::clamp((p.x - r) / BKS, 0, g_bkW - 1), x1 = std::clamp((p.x + r) / BKS, 0, g_bkW - 1), y0 = std::clamp((p.y - r) / BKS, 0, g_bkH - 1), y1 = std::clamp((p.y + r) / BKS, 0, g_bkH - 1);
    for (int by = y0; by <= y1; ++by) for (int bx = x0; bx <= x1; ++bx) for (int gi : g_bk[(size_t)(by * g_bkW + bx)]) if (cheb(S.groups[(size_t)gi].center, p) <= r) out.push_back(gi);
    std::sort(out.begin(), out.end());
}
int g_praD = 1;   /* PRA */
static int praBase() { return (int)bc::constants::recall_time > 0 ? (int)bc::constants::recall_time : rules::RECALL_TICKS; }   /* PRA */
static inline std::uint32_t itemSdk(int legacy);   /* PRA: 아래 ITN 정의 */
static bool praHome(std::uint32_t h) {   /* PRA: Home 엠블럼 (31 · 33 · 35, 리콜 3) 을 가졌나 — recallLen 과 같은 판정 */
    constexpr std::size_t N = (std::size_t)bc::item_type::enum_type::count + 8; std::uint32_t c[N > 256 ? N : 256] = {};
    if (d::sdk_champion_items(h, c) != 0) return false;
    for (int it : {31, 33, 35}) { const std::uint32_t s = itemSdk(it); if (s < (std::uint32_t)bc::item_type::enum_type::count && c[s] > 0) return true; }
    return false; }
bool praFresh(const Unit& u) {   /* PRA: 막 건 리콜 = 남은 채널 >= 리콜 길이 - g_praD (Home 이 아닐 때만 아이템을 묻지 않음) */
    if (!policy::PRA_ON || !(u.state & rules::ST_RECALLING) || (u.state & (rules::ST_SUBMITTING | rules::ST_TELEPORTING)) || u.chLeft < 0) return false;
    if (u.chLeft >= praBase() - g_praD) return true;
    return u.chLeft >= 3 - g_praD && praHome(u.handle); }
void praMark(Unit& u) {   /* PRA: 리콜 중이면 남은 채널을 읽고, 막 건 리콜이면 채널 비트를 지운다 (팀원 = 빠진 몸 아님 · 적 = 끊을 채널 아님) */
    if (!policy::PRA_ON || !(u.state & rules::ST_RECALLING)) return;
    std::int32_t t = -1; if (d::sdk_champion_channel_time(u.handle, &t) == 0) u.chLeft = t;
    if (praFresh(u)) u.state &= ~rules::ST_RECALLING; }
unsigned g_fs3Gen = 1;   /* FS_COM3 */
void sense(World& w) {
    ++g_fs3Gen;   /* FS_COM3 */
    w.round = round(); s_turnR = w.round;   /* FUELH */
    nav = {};
    w.enemies.clear(); w.allies.clear(); w.camps.clear(); w.occupied.clear(); w.enemyNear8 = false;
    auto& me = bc::self();
    const std::uint32_t meH = hOf(me);
    d::champion_info ci{};
    if (d::sdk_champion(meH, &ci) == 0) {
        Me& m = w.me; m.handle = meH; m.type = ci.type; m.pos = {ci.x, ci.y}; m.hp = ci.health; m.maxHp = ci.max_health; m.str = ci.strength; m.focus = ci.focus; m.range = ci.attack_range;
        m.state = (std::uint32_t)ci.state; m.alive = (m.state & rules::ST_ALIVE) != 0;
        m.canMove = m.state & rules::ST_CAN_MOVE; m.canAttack = m.state & rules::ST_CAN_ATTACK; m.canCast = m.state & rules::ST_CAN_CAST;
        m.channeling = m.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING);
        m.atSpawn = S.inb(m.pos) && (S.tile[(size_t)S.idx(m.pos)] & T_ALLY_SPAWN);
        for (int s = 0; s < 2; ++s) { std::int32_t cd = 0; if (d::sdk_champion_cooldown(meH, s, &cd) == 0) m.cooldown[s] = cd; }
        std::uint32_t g = 0; if (d::sdk_champion_gems(meH, &g) == 0) m.gems = (int)g;
        g = 0; if (d::sdk_champion_gold(meH, &g) == 0) m.gold = (int)g;
        d::ward_resources wr{}; if (d::sdk_champion_wards(meH, &wr) == 0) m.wardCooldown = wr.charges > 0 ? 0 : std::max(1, wr.cooldown);
    } else { w.me.alive = false; }
    for (std::uint32_t h : S.enemyHandle) {
        d::champion_info ei{};
        if (d::sdk_champion(h, &ei) != 0 || ei.health <= 0.f) continue;   // 97 = 시야 밖
        Unit u; fillUnit(u, h, ei, true); praMark(u);   /* PRA */ w.enemies.push_back(u); w.occupied.push_back(u.pos);
        if (cheb(u.pos, w.me.pos) <= rules::VISION) w.enemyNear8 = true;
    }
    for (std::uint32_t h : S.allyHandle) {
        if (h == meH) continue;
        d::champion_info ai{};
        if (d::sdk_champion(h, &ai) != 0 || ai.health <= 0.f) continue;    // 시야 밖 아군은 공유 칸으로 (team)
        Unit u; fillUnit(u, h, ai, false); praMark(u);   /* PRA */ if (policy::AGM_ON) { std::uint32_t g = 0; if (d::sdk_champion_gems(h, &g) == 0) u.gems = (int)g; }   /* AGM */ w.allies.push_back(u); w.occupied.push_back(u.pos);
    }
    // 캠프: 내 10칸 안에 스폰 칸이 있는 그룹만 상태를 묻고, 그 캠프들을 읽는다
    int passChanges = 0;
    static std::vector<int> s_cand; s_cand.clear();
    static std::vector<char> s_resp; static std::vector<int> s_respL;
    const int NR = rules::VISION + 3;
    if (policy::SNS_ON) {   /* SNS: 가까운 그룹 (격자) + 부활 대기 · 만기 표시 그룹만 */
        if (s_resp.size() != S.groups.size()) {
            s_resp.assign(S.groups.size(), 0); s_respL.clear();
            for (size_t gi = 0; gi < S.groups.size(); ++gi) if (S.groups[gi].state == 3 || S.groups[gi].passExpired) { s_resp[gi] = 1; s_respL.push_back((int)gi); }
        }
        nearGroups(w.me.pos, NR, s_cand);
        if (g_wantGroup >= 0 && g_wantGroup < (int)S.groups.size()) s_cand.push_back(g_wantGroup);   /* SNRM */
        for (int gi : s_respL) s_cand.push_back(gi);
        std::sort(s_cand.begin(), s_cand.end()); s_cand.erase(std::unique(s_cand.begin(), s_cand.end()), s_cand.end());
    } else { for (size_t gi = 0; gi < S.groups.size(); ++gi) s_cand.push_back((int)gi); }
    for (size_t ci = 0; ci < s_cand.size(); ++ci) {
        if (fuel() < policy::FGR_SENSE) break;   /* FGR: 감지 캠프 읽기 상한 (못 읽은 무리는 기억 유지) */
        const size_t gi = (size_t)s_cand[ci];
        Group& G = S.groups[gi];
        bool observed = false; bool rfd = false;   /* RFD: 갱신 결과가 바뀔 수 있을 때만 refreshCampGroup */
        if (cheb(G.center, w.me.pos) <= rules::VISION + 3 || (int)gi == g_wantGroup || (G.state == 3 && G.respawnUntil >= 0 && G.respawnUntil <= w.round)) {   /* RSO */
        std::uint32_t st = 0;
        if (d::sdk_camp_group_state(G.handle, &st) == 0 && st >= 1 && st <= 3) {   // 0 = 시야 밖 (기억 유지)
            observed = true;
            if (G.campLastHp.size() != G.campHandle.size()) G.campLastHp.assign(G.campHandle.size(), -1.f);
            if ((int)st != G.state) rfd = true;   /* RFD */
            if ((int)st == 1 && G.state != 1) { for (auto& h : G.campLastHp) h = -1.f; for (size_t k = 0; k < G.spawn.size(); ++k) S.campSeenRound[G.seenOffset + k] = -1; }   // 새 생명 주기
            if ((int)st == 2 || (int)st == 3) { if (G.state != 2 && G.state != 3) ++S.openVersion;   /* PFX */ for (auto& h : G.campLastHp) h = 0.f; }          // 보상 대기 · 리스폰 중: 모두 죽음
            G.state = (int)st; G.stateRound = w.round;
            if (st == 3) { const int ru0 = G.respawnUntil; std::int32_t cd = 0; G.respawnUntil = w.round + (d::sdk_camp_group_cooldown(G.handle, &cd) == 0 ? std::max(0, (int)cd) : 0); if (G.respawnUntil != ru0) rfd = true; if (policy::TERR_ON && ru0 < w.round && cd > 0 && cd + 3 >= G.respawn && gi < 62) { g_respEv = (int)gi; g_respEvR = w.round; } }   /* TERR T2: 갓 시작한 타이머만 알림 */   /* RFD */   // 타이머 조회 실패 시 오늘의 빈 상태만 확정
            else G.respawnUntil = -1;
        }
        static std::vector<int> s_rfgR; if (s_rfgR.size() != S.groups.size()) { s_rfgR.assign(S.groups.size(), 0); for (size_t g2 = 0; g2 < S.groups.size(); ++g2) for (const auto& sp : S.groups[g2].spawn) s_rfgR[g2] = std::max(s_rfgR[g2], cheb(sp, S.groups[g2].center)); }   /* RFG: 무리 반지름 */
        if (!(observed && (G.state == 2 || G.state == 3)) && cheb(G.center, w.me.pos) <= rules::VISION + s_rfgR[gi])   /* DSK: 무리가 죽음 · 대기로 확인되면 캠프별 확인 건너뜀 (캠프 체력은 위에서 모두 0) */ /* RFG: 시야 밖 무리는 캠프 고리를 건너뜀 (모든 캠프가 continue 되므로 같음) */
        for (size_t k = 0; k < G.campHandle.size(); ++k) {
            if (cheb(G.spawn[k], w.me.pos) > rules::VISION) continue;
            if (G.strMax <= 0.f && cheb(G.spawn[k], w.me.pos) > std::max(1, w.me.range) + 3 && ((int)k + w.round) % 3 != 0 && S.campSeenRound[G.seenOffset + k] >= w.round - 3) continue;   /* SNC3: 덩어리만 무리의 먼 캠프는 3R 에 한 번 */
            d::camp_state cs{};
            const int code = d::sdk_camp(G.campHandle[k], &cs);
            if (code != 0 || cs.health <= 0.f) {
                // 조회 실패 대신 SDK가 확인한 개별 사망만 기록한다.
                std::uint32_t state = 99;
                if ((code == 0 && cs.health <= 0.f) ||
                    (seesTile(G.spawn[k]) && d::sdk_camp_state(G.campHandle[k], &state) == 0 && state == 0)) {
                    if (G.campLastHp.size() != G.campHandle.size()) G.campLastHp.assign(G.campHandle.size(), -1.f);
                    if (G.campLastHp[k] != 0.f) { rfd = true; ++S.openVersion; }   /* RFD */   /* PFX */
                    G.campLastHp[k] = 0.f; S.campSeenRound[G.seenOffset + k] = w.round; observed = true;
                }
                continue;
            }
            CampUnit c; c.handle = G.campHandle[k]; c.group = (int)gi; c.type = G.campType[k]; c.pos = {cs.x, cs.y}; c.hp = cs.health; c.maxHp = cs.max_health; c.str = cs.strength;
            if (G.campLastHp.size() != G.campHandle.size()) G.campLastHp.assign(G.campHandle.size(), -1.f);
            if (G.campLastHp[k] >= 0.f && cs.health < G.campLastHp[k] - 0.5f) G.awakeUntil = std::max(G.awakeUntil, w.round + rules::CAMP_SLEEP_ROUNDS);   // 체력이 줄었다 = 누가 치고 있다 → 깼다 (깎인 채 잠든 캠프는 치지 않는다)
            if (c.str > 0.f && G.awakeUntil < w.round && cheb(c.pos, w.me.pos) <= S.campRange(c.type) + 2) { std::uint32_t mst = 99; if (d::sdk_camp_state(c.handle, &mst) == 0 && mst == 1) G.awakeUntil = w.round; }   /* MAW: 엔진이 깸이라 하면 깸 */
            if ((G.campLastHp[k] == 0.f) != (cs.health == 0.f)) rfd = true;   /* RFD */
            G.campLastHp[k] = cs.health;
            S.campSeenRound[G.seenOffset + k] = w.round;
            observed = true;
            w.camps.push_back(c); w.occupied.push_back(c.pos);
        }
        }
        // 먼 그룹도 만기는 확인하되, 멤버 배열은 관측/만기 전환 때만 쓴다.
        if ((observed && rfd) || G.passExpired != Static::respawnExpired(G, w.round))   /* RFD2: 상태 · 캠프 생사 · 만기가 바뀔 때만 */
            passChanges += S.refreshCampGroup(gi, w.round);
        if (policy::SNS_ON && !s_resp.empty()) { const bool q = G.state == 3 || G.passExpired; if (q != (s_resp[gi] != 0)) { s_resp[gi] = q ? 1 : 0; if (q) s_respL.push_back((int)gi); else s_respL.erase(std::find(s_respL.begin(), s_respL.end(), (int)gi)); } }   /* SNS */
    }
    if (passChanges) ++S.campVersion;
    nav.changedTiles = passChanges;
    readShared(w.shared);
    static std::vector<Pos> s_prevOcc;   /* BUG6: 지난 라운드에 찍은 칸을 지워, 128 라운드 뒤 유령 점유를 막음 */
    if (policy::BUG6_ON) { for (const auto& p : s_prevOcc) if (S.inb(p)) S.occStamp[(size_t)S.idx(p)] = 0; }
    for (const auto& p : w.occupied) if (S.inb(p)) S.occStamp[(size_t)S.idx(p)] = (std::uint8_t)(1 + (w.round & 127));
    if (policy::BUG6_ON) s_prevOcc = w.occupied;
}

// ---------------------------------------------------------------- 거리장
int groupCaptureState(std::uint32_t h) { d::camp_group_capture_state v{}; return d::sdk_camp_group_capture_state(h, &v) == 0 ? (int)v.state : -1; }   /* CNT */
int groupCaptureTimer(std::uint32_t h) { d::camp_group_capture_state v{}; return d::sdk_camp_group_capture_state(h, &v) == 0 ? (int)v.timer : -1; }   /* CSJ3 */
NavigationStats nav;
bool campBlocksMovement(Pos p) { return S.campBlocks(p); }
std::uint64_t campPassabilityVersion() { return S.campVersion; }
struct FieldImpl { bc::distance_field f; bc::bitboard walls, roots, visited;
    bool prepOk = false; int prepR = -1; Pos prepMe{}; const Pos* prepRoots = nullptr; std::size_t prepN = 0; std::uint64_t prepVer = 0; bool prepBrg = false, prepFld = false; };   /* FS_IO2: 올린 비트판의 입력 표지 */
static std::vector<std::uint8_t> g_wallBytes, g_rootBytes;   // 비트 포장 (export_linear 과 같은 꼴) — 칸마다 set() 은 엔진 호출 ~100 이라 한 번의 import 로 올린다

static std::vector<int> g_bridgeIdx;   /* DSH */
static inline void bitSet(std::vector<std::uint8_t>& v, int i, bool on);
static int g_brgRow = 0; static bool g_brgDone = false; static bool g_brgFresh = false;   /* BRG2 */
bool bridgesReady() { return g_brgDone; }
bool bridgesJustDone() { const bool f = g_brgFresh; g_brgFresh = false; return f; }
void ensureBridges() { bridgeStep(0); }
void bridgeStep(long long floorFuel) {   /* DSH · BRG2: 구덩이 h 의 반대쪽 이웃 A·B 가 둘 다 설 수 있는 칸이면 h 는 대시 다리 — 줄 단위로 나눠 */
    const size_t n = (size_t)S.W * (size_t)S.H; if (g_brgDone || S.tile.size() != n || S.W <= 0) return;
    if (S.bridge.size() != n) { S.bridge.resize(n); std::memset(S.bridge.data(), 0, n); g_bridgeIdx.clear(); g_brgRow = 0; }
    auto stand = [&](Pos q) { return S.inb(q) && !(S.tile[(size_t)S.idx(q)] & (T_BLOCKED | T_CAMP)); };
    const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (; g_brgRow < S.H; ++g_brgRow) { if (fuel() < floorFuel + 12000) return; const int y = g_brgRow; const std::uint8_t* rowp = S.tile.data() + (size_t)y * (size_t)S.W; for (int x = 0; x < S.W; ++x) { if (!(x & 7) && x + 8 <= S.W) { std::uint64_t w8; std::memcpy(&w8, rowp + x, 8); if (!(w8 & (0x0101010101010101ULL * (std::uint64_t)T_HOLE))) { x += 7; continue; } }   /* FU1: 8칸에 구덩이가 없으면 건너뜀 */
        const std::uint8_t t = rowp[x]; if (!(t & T_HOLE) || (t & (T_WALL | T_ALTAR | T_ENEMY_SPAWN))) continue; const Pos h{x, y};   /* FU1: 칸마다 idx 곱셈 없이 줄 포인터로 */
        bool br = false; const std::uint8_t* tb = S.tile.data(); const int Wd = S.W, Hd = S.H;   /* FU1: 이웃 읽기를 직접 (경계 검사 + 포인터) */
        for (const auto& dd : dirs) { const int ax = x + dd[0], ay = y + dd[1], bx = x - dd[0], by = y - dd[1];
            if (ax < 0 || ay < 0 || ax >= Wd || ay >= Hd || bx < 0 || by < 0 || bx >= Wd || by >= Hd) continue;
            if (!(tb[ay * Wd + ax] & (T_BLOCKED | T_CAMP)) && !(tb[by * Wd + bx] & (T_BLOCKED | T_CAMP))) { br = true; break; } }
        if (br) { S.bridge[(size_t)S.idx(h)] = 1; g_bridgeIdx.push_back(S.idx(h)); } } }
    g_brgDone = true; g_brgFresh = !g_bridgeIdx.empty(); if (g_brgFresh) ++S.openVersion;   /* BRG2: 다리가 있으면 못 감 기억 다시 묻기 */
}
static void dshClear(std::vector<std::uint8_t>& walls) { if (!S.dashNav || !g_brgDone) return; for (int i : g_bridgeIdx) bitSet(walls, i, false); }   /* DSH: 거리장 벽에서 다리 칸 뺌 */
static inline void bitSet(std::vector<std::uint8_t>& v, int i, bool on) {
    const int sh = g_bitOrder == 0 ? (i & 7) : 7 - (i & 7);
    if (on) v[(size_t)(i >> 3)] |= (std::uint8_t)(1u << sh); else v[(size_t)(i >> 3)] &= (std::uint8_t)~(1u << sh);
}

bool wallsReady() { return g_staticWallBytes.size() == bc::bitboard::linear_buffer_size() && g_staticWallBytes.size() > 0; }
bool prewarmWalls(long long minFuel) {   // ZO
    if (wallsReady()) return true;
    if (fuel() < minFuel) return false;
    const size_t nbytes = bc::bitboard::linear_buffer_size();
    std::vector<std::uint8_t> v(nbytes, 0);
    const int n = S.W * S.H;
    /* FU1: 바이트 단위로 조립 (칸마다 읽고 쓰기 하던 것을 줄임 — 결과 바이트 같음) */
    { const std::uint8_t* tp = S.tile.data(); const bool lsb = g_bitOrder == 0; const int nfull = n >> 3;
      for (int b = 0; b < nfull; ++b) { const std::uint8_t* q = tp + (b << 3); std::uint8_t m = 0;
          if (lsb) { m = (std::uint8_t)(((q[0] & T_BLOCKED) ? 1u : 0u) | ((q[1] & T_BLOCKED) ? 2u : 0u) | ((q[2] & T_BLOCKED) ? 4u : 0u) | ((q[3] & T_BLOCKED) ? 8u : 0u) | ((q[4] & T_BLOCKED) ? 16u : 0u) | ((q[5] & T_BLOCKED) ? 32u : 0u) | ((q[6] & T_BLOCKED) ? 64u : 0u) | ((q[7] & T_BLOCKED) ? 128u : 0u)); }
          else { m = (std::uint8_t)(((q[0] & T_BLOCKED) ? 128u : 0u) | ((q[1] & T_BLOCKED) ? 64u : 0u) | ((q[2] & T_BLOCKED) ? 32u : 0u) | ((q[3] & T_BLOCKED) ? 16u : 0u) | ((q[4] & T_BLOCKED) ? 8u : 0u) | ((q[5] & T_BLOCKED) ? 4u : 0u) | ((q[6] & T_BLOCKED) ? 2u : 0u) | ((q[7] & T_BLOCKED) ? 1u : 0u)); }
          v[(size_t)b] = m; }
      for (int i = nfull << 3; i < n; ++i) if (tp[i] & T_BLOCKED) bitSet(v, i, true); }
    g_staticWallBytes.swap(v);
    return true;
}
static std::vector<int> g_campIdx; static std::vector<std::uint8_t> g_dynWall, g_allCampWall; static std::uint64_t g_dynVer = 0;   /* FLD */
static void fldCampIdx() { if (!g_campIdx.empty()) return; for (const auto& G : S.groups) for (const auto& p : G.spawn) if (S.inb(p)) g_campIdx.push_back(S.idx(p)); }
bool fieldExport(const std::vector<Pos>& roots, RawVec<std::uint16_t>& out) {   // GF
    static FieldImpl* G = nullptr; if (!G) G = new FieldImpl();
    const size_t nbytes = bc::bitboard::linear_buffer_size();
    if (g_staticWallBytes.size() != nbytes) prewarmWalls(0);
    if (policy::FLD_ON) {   /* FLD: 정적 벽 + 모든 캠프 칸 = 한 번 */
        if (g_allCampWall.size() != nbytes) { g_allCampWall = g_staticWallBytes;   /* FLDC: 힘 있는 캠프 칸만 벽 */
            for (const auto& Gc : S.groups) for (size_t k = 0; k < Gc.spawn.size(); ++k) { const Pos& q = Gc.spawn[k]; if (!S.inb(q)) continue; const int ty = k < Gc.campType.size() ? Gc.campType[k] : -1; if (S.campStr(ty) > 0.f) bitSet(g_allCampWall, S.idx(q), true); } }
        g_wallBytes = g_allCampWall; g_rootBytes.resize(nbytes); std::memset(g_rootBytes.data(), 0, nbytes);
    } else {
    g_wallBytes = g_staticWallBytes; g_rootBytes.resize(nbytes); std::memset(g_rootBytes.data(), 0, nbytes);   // NI
    for (const auto& Gr : S.groups) for (const auto& p : Gr.spawn) if (S.inb(p)) bitSet(g_wallBytes, S.idx(p), true);
    }
    int nr = 0;
    for (const auto& p : roots) if (S.inb(p)) { bitSet(g_rootBytes, S.idx(p), true); bitSet(g_wallBytes, S.idx(p), false); ++nr; }
    if (nr == 0) return false;
    dshClear(g_wallBytes);   /* DSH */
    G->walls.import_linear_buffer(std::span<const std::uint8_t>(g_wallBytes.data(), g_wallBytes.size()));
    G->roots.import_linear_buffer(std::span<const std::uint8_t>(g_rootBytes.data(), g_rootBytes.size()));
    G->visited.clear();
    G->f.compute(G->walls, G->roots, G->visited);
    out.resize((size_t)S.W * (size_t)S.H);
    G->f.export_linear_buffer(std::span<std::uint16_t>(out.data(), out.size()));
    return true;
}
bool territoryFieldExport(bool fromEnemy, RawVec<std::uint16_t>& out) {   /* TM_CAMP */
    static FieldImpl* G = nullptr; if (!G) G = new FieldImpl();
    if (!wallsReady() && !prewarmWalls(0)) return false;
    const size_t nbytes = bc::bitboard::linear_buffer_size();
    g_wallBytes = g_staticWallBytes;
    g_rootBytes.assign(nbytes, 0);
    if (fromEnemy) {   // 두 팀이 같은 걷기 규칙: 스폰 막힘도 뒤집음 (고정 막힘은 그대로)
        for (Pos p : S.enemySpawn) if (S.inb(p))
            bitSet(g_wallBytes, S.idx(p), (S.tile[(size_t)S.idx(p)] & (T_WALL | T_HOLE | T_ALTAR)) != 0);
        for (Pos p : S.allySpawn) if (S.inb(p)) bitSet(g_wallBytes, S.idx(p), true);
    }
    const auto& roots = fromEnemy ? S.enemySpawn : S.allySpawn;
    int nr = 0;
    for (Pos p : roots) if (S.inb(p) && !(S.tile[(size_t)S.idx(p)] & (T_WALL | T_HOLE | T_ALTAR))) {
        bitSet(g_rootBytes, S.idx(p), true); bitSet(g_wallBytes, S.idx(p), false); ++nr;
    }
    if (nr == 0) return false;
    // dshClear 안 함: 대시가 땅 주인을 바꾸면 안 됨
    G->walls.import_linear_buffer(std::span<const std::uint8_t>(g_wallBytes.data(), g_wallBytes.size()));
    G->roots.import_linear_buffer(std::span<const std::uint8_t>(g_rootBytes.data(), g_rootBytes.size()));
    G->visited.clear();
    G->f.compute(G->walls, G->roots, G->visited);
    out.resize((size_t)S.W * (size_t)S.H);
    G->f.export_linear_buffer(std::span<std::uint16_t>(out.data(), out.size()));
    return true;
}
int territorySymmetry() {   /* TM_CAMP */
    static int cached = -2;
    if (cached != -2) return cached;
    const auto symmetry = bc::map::symmetry();
    const int value = (int)symmetry;
    if (value < 0 || value > 2) return cached = -1;
    const auto blocked = bc::map::walls() | bc::map::holes() | bc::map::altars();   // 엔진 고정 막힘 (타일 배열 안 훑음)
    if (!(blocked == blocked.reflect(symmetry)) ||
        !(bc::map::spawn_region(bc::team::ally).reflect(symmetry) == bc::map::spawn_region(bc::team::enemy)))
        return cached = -1;
    return cached = value;
}
bool Field::compute(const World& w, const std::vector<Pos>& rootsIn, int maxDist) {
    const long long q0 = fuel();
    ready = false; ranCompute = false;
    if (!impl) impl = new FieldImpl();
    FieldImpl& F = *static_cast<FieldImpl*>(impl);
    if (policy::FS_IO2_ON) { F.prepOk = false; gmClear(); }   /* FS_IO2 */
    const size_t nbytes = bc::bitboard::linear_buffer_size();
    if (g_staticWallBytes.size() != nbytes) prewarmWalls(0);   // ZO: 정적 막힘은 한 번만
    if (policy::FLD_ON) {   /* FLD: 정적 벽 + 막힌 캠프 칸 — 막힘 버전이 바뀔 때만 다시 */
        if (g_dynWall.size() != nbytes || S.campChangedOverflow) { fldCampIdx(); g_dynWall = g_staticWallBytes; for (int i : g_campIdx) if (S.campBlocked[(size_t)i] != 0) bitSet(g_dynWall, i, true); g_dynVer = S.campVersion; S.campChangedIdx.clear(); S.campChangedOverflow = false; }   /* CWI: 처음 · 넘침만 전체 */
        else if (g_dynVer != S.campVersion) { for (int i : S.campChangedIdx) { const int sh = g_bitOrder == 0 ? (i & 7) : 7 - (i & 7); const bool st = (g_staticWallBytes[(size_t)(i >> 3)] >> sh & 1) != 0; bitSet(g_dynWall, i, st || S.campBlocked[(size_t)i] != 0); } S.campChangedIdx.clear(); g_dynVer = S.campVersion; }   /* CWI: 바뀐 칸만 */
        g_wallBytes = g_dynWall; g_rootBytes.resize(nbytes); std::memset(g_rootBytes.data(), 0, nbytes);
    } else {
    g_wallBytes = g_staticWallBytes; g_rootBytes.resize(nbytes); std::memset(g_rootBytes.data(), 0, nbytes);   // NI
    // 개별 사망과 재생성 추정을 공유 마스크에서 읽는다.
    for (const auto& G : S.groups) {
        for (const auto& p : G.spawn) if (campBlocksMovement(p)) bitSet(g_wallBytes, S.idx(p), true);
    }
    }
    // 보이는 유닛 칸은 넣지 않는다 (움직이므로 묵은 발자국이 길을 막는다). 칸의 합법성은 combat 이 매 턴 본다
    if (S.inb(w.me.pos)) bitSet(g_wallBytes, S.idx(w.me.pos), false);
    int nr = 0;
    for (const auto& p : rootsIn) if (S.walkableStatic(p) && (!campBlocksMovement(p) || p == w.me.pos)) {
        bitSet(g_rootBytes, S.idx(p), true); bitSet(g_wallBytes, S.idx(p), false); ++nr;
    }
    if (nr == 0) return false;
    const long long q1 = fuel();
    dshClear(g_wallBytes);   /* DSH3: 목표 거리장도 다리 통행 (무리 거리장과 같게) */
    F.walls.import_linear_buffer(std::span<const std::uint8_t>(g_wallBytes.data(), g_wallBytes.size()));
    F.roots.import_linear_buffer(std::span<const std::uint8_t>(g_rootBytes.data(), g_rootBytes.size()));
    if (policy::FS_IO2_ON) { F.prepOk = true; F.prepR = w.round; F.prepMe = w.me.pos; F.prepRoots = rootsIn.data(); F.prepN = rootsIn.size(); F.prepVer = S.campVersion; F.prepBrg = S.dashNav && g_brgDone; F.prepFld = policy::FLD_ON; }   /* FS_IO2 */
    const long long q2 = fuel();
    F.visited.clear();
    ranCompute = true;
    if (maxDist > 0) F.f.compute(F.walls, F.roots, F.visited, (std::uint16_t)maxDist); else F.f.compute(F.walls, F.roots, F.visited);
    const long long q3 = fuel();
    if (io::round() != w.round) return false;
    ready = true; round = w.round; campVersion = campPassabilityVersion();
    origin = rootsIn.size() == 1 ? rootsIn[0] : Pos{}; maxDistance = maxDist;
    lastCost[0] = q0 - q1; lastCost[1] = q1 - q2; lastCost[2] = q2 - q3;
    return true;
}
bool Field::recomputeSame(const World& w, const std::vector<Pos>& rootsIn, int maxDist) {   /* FS_IO2 */
    if (!policy::FS_IO2_ON || !impl) return compute(w, rootsIn, maxDist);
    FieldImpl& F = *static_cast<FieldImpl*>(impl);
    const size_t nbytes = bc::bitboard::linear_buffer_size();
    const bool same = F.prepOk && ranCompute && F.prepR == w.round && io::round() == w.round && F.prepMe == w.me.pos && F.prepRoots == rootsIn.data() && F.prepN == rootsIn.size()
        && F.prepVer == S.campVersion && F.prepBrg == (S.dashNav && g_brgDone) && F.prepFld == policy::FLD_ON
        && g_staticWallBytes.size() == nbytes && (!policy::FLD_ON || (g_dynWall.size() == nbytes && !S.campChangedOverflow && g_dynVer == S.campVersion));
    if (!same) return compute(w, rootsIn, maxDist);
    const long long q0 = fuel();
    ready = false; ranCompute = false; gmClear();
    const long long q1 = fuel(); const long long q2 = q1;   // 준비 · import 없음 (비트판은 바로 앞 compute 가 같은 입력으로 올린 그대로)
    F.visited.clear();
    ranCompute = true;
    if (maxDist > 0) F.f.compute(F.walls, F.roots, F.visited, (std::uint16_t)maxDist); else F.f.compute(F.walls, F.roots, F.visited);
    const long long q3 = fuel();
    if (io::round() != w.round) return false;
    ready = true; round = w.round; campVersion = campPassabilityVersion();
    origin = rootsIn.size() == 1 ? rootsIn[0] : Pos{}; maxDistance = maxDist;
    lastCost[0] = q0 - q1; lastCost[1] = q1 - q2; lastCost[2] = q2 - q3;
    return true;
}
int Field::get(Pos p) const {
    if (!ready || !impl || !S.inb(p)) return -1;
    ++nav.queries;
    if (policy::FS_IO2_ON) {   /* FS_IO2: 같은 칸 다시 묻기 = 앞의 값 (compute 마다 비움) */
        const int i = S.idx(p), s = i & 15;
        if (gmKey[s] == i + 1) return gmVal[s];
        const int v = (int)static_cast<FieldImpl*>(impl)->f.get(bc::vec2(p.x, p.y));
        gmKey[s] = i + 1; gmVal[s] = v; return v;
    }
    return (int)static_cast<FieldImpl*>(impl)->f.get(bc::vec2(p.x, p.y));
}
bool Field::exportValues(RawVec<std::uint16_t>& out) const {
    if (!ready || !impl) return false;
    out.resize((size_t)S.W * S.H);
    static_cast<FieldImpl*>(impl)->f.export_linear_buffer(std::span<std::uint16_t>(out.data(), out.size()));
    return true;
}

// ---------------------------------------------------------------- 행동
template <class Fn> static bool guarded(const char* what, Fn fn) {
    if (policy::FS_IO1_ON) ++s_actEp;   /* FS_IO1 */
    try { return fn(); }
    catch (const std::exception& e) { g_err = std::string(what) + ": " + e.what(); return false; }
    catch (...) { g_err = std::string(what) + ": ?"; return false; }
}
static bool g_mvHid = false; bool lastMoveHidden() { return g_mvHid; }   /* BH4_TN3 */
bool moveTo(Pos p) { g_mvHid = false; return guarded("move", [&] { const bool ok = bc::self().move_absolute(bc::vec2(p.x, p.y)); if (policy::BH4_TN3_ON && !ok) g_mvHid = true; return ok; }); }
bool attackChampion(std::uint32_t h) { return guarded("attack", [&] { return d::sdk_attack((std::uint32_t)d::entity_kind::champion, h) == 0; }); }
bool attackCamp(std::uint32_t h) { return guarded("attackcamp", [&] { return d::sdk_attack((std::uint32_t)d::entity_kind::camp, h) == 0; }); }
static bool castRaw(int slot, std::uint32_t kind, std::uint32_t h, int x, int y) {
    std::int32_t ok = 0; const int code = d::sdk_cast((std::uint32_t)slot, kind, h, x, y, &ok);
    if (code != 0) { g_err = "cast code " + std::to_string(code); return false; }
    return ok != 0;
}
bool castSelf(int slot) { return guarded("castself", [&] { return castRaw(slot, 0, 0, -1, -1); }); }
bool castChampion(int slot, std::uint32_t h) { return guarded("castch", [&] { return castRaw(slot, (std::uint32_t)d::entity_kind::champion, h, -1, -1); }); }
bool castCamp(int slot, std::uint32_t h) { return guarded("castcamp", [&] { return castRaw(slot, (std::uint32_t)d::entity_kind::camp, h, -1, -1); }); }
bool castGround(int slot, Pos p) { return guarded("castground", [&] { return castRaw(slot, 0, 0, p.x, p.y); }); }
bool cancelSubmit() { return guarded("cancelsub", [&] { bc::self().cancel_submit_gems(); return true; }); }   /* CHC */
bool submitGems(Pos a) { return guarded("submit", [&] { bc::self().submit_gems(bc::vec2(a.x, a.y)); return true; }); }
bool recall() { return guarded("recall", [&] { bc::self().recall(); return true; }); }
bool praArm() {   /* PRA: 산 몸 · 스폰 밖 · 채널 아님 · 이 턴에 리콜 시작 안 함 (recall_started 1<<6) 일 때만 */
    const std::uint32_t h = hOf(bc::self()); d::champion_info ci{};
    if (d::sdk_champion(h, &ci) != 0) return false;
    const std::uint32_t st = (std::uint32_t)ci.state;
    if (!(st & rules::ST_ALIVE) || (st & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING | (1u << 6)))) return false;
    const Pos p{ci.x, ci.y}; if (!S.inb(p) || (S.tile[(size_t)S.idx(p)] & T_ALLY_SPAWN)) return false;
    return guarded("praarm", [&] { bc::self().recall(); return true; }); }
bool praCancel() { return guarded("pracancel", [&] { bc::self().cancel_recall(); return true; }); }   /* PRA */
int selfChannelLeft() { std::int32_t t = -1; if (d::sdk_champion_channel_time(hOf(bc::self()), &t) != 0) return -1; return (int)t; }   /* PRA */
void refreshMeState(Me& m) {   /* PRA: 취소 뒤 엔진 상태 비트 (sense 와 같은 식) */
    d::champion_info ci{}; if (d::sdk_champion(m.handle, &ci) != 0) return;
    m.state = (std::uint32_t)ci.state; m.alive = (m.state & rules::ST_ALIVE) != 0;
    m.canMove = m.state & rules::ST_CAN_MOVE; m.canAttack = m.state & rules::ST_CAN_ATTACK; m.canCast = m.state & rules::ST_CAN_CAST;
    m.channeling = m.state & (rules::ST_RECALLING | rules::ST_SUBMITTING | rules::ST_TELEPORTING); }
static const std::uint32_t kItemSdk[42] = {(std::uint32_t)bc::item_type::enum_type::monster_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::monster_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::monster_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::monster_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::monster_shard_of_health, (std::uint32_t)bc::item_type::enum_type::monster_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::power_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::power_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::power_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::power_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::power_shard_of_health, (std::uint32_t)bc::item_type::enum_type::power_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::durability_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::durability_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::durability_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::durability_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::durability_shard_of_health, (std::uint32_t)bc::item_type::enum_type::durability_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::treasure_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::treasure_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::treasure_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::treasure_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::treasure_shard_of_health, (std::uint32_t)bc::item_type::enum_type::treasure_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::jungle_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::jungle_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::jungle_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::jungle_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::jungle_shard_of_health, (std::uint32_t)bc::item_type::enum_type::jungle_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::home_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::home_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::home_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::home_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::home_shard_of_health, (std::uint32_t)bc::item_type::enum_type::home_emblem_of_health, (std::uint32_t)bc::item_type::enum_type::bruiser_shard_of_strength, (std::uint32_t)bc::item_type::enum_type::bruiser_emblem_of_strength, (std::uint32_t)bc::item_type::enum_type::bruiser_shard_of_focus, (std::uint32_t)bc::item_type::enum_type::bruiser_emblem_of_focus, (std::uint32_t)bc::item_type::enum_type::bruiser_shard_of_health, (std::uint32_t)bc::item_type::enum_type::bruiser_emblem_of_health};   /* ITN: 옛 번호 → SDK 이름 */
static inline std::uint32_t itemSdk(int legacy) { if (legacy >= 0 && legacy < 42) return kItemSdk[legacy]; return legacy >= 42 && legacy < (int)bc::item_type::enum_type::count ? (std::uint32_t)legacy : 0xFFFFFFFFu; }   /* ITN2: 새 계열 42~83 = SDK 번호 */   /* ITN */
bool buyShard(int itemType, int count) { s_icR = -1; const std::uint32_t it = itemSdk(itemType); if (it == 0xFFFFFFFFu) return false; return guarded("buy", [&] { return d::sdk_buy_item(it, (std::uint32_t)count) == 0; }); }   /* ITN */
bool placeWard(Pos p) { return guarded("ward", [&] { return bc::self().place_ward(bc::vec2(p.x, p.y)); }); }
bool tryPlaceWard(Pos p){
    std::int32_t placed=0;const int code=d::sdk_place_ward(p.x,p.y,&placed);
    if (policy::FS_IO1_ON) ++s_actEp;   /* FS_IO1 */
    if(code==0&&placed){s_vbRound=-1;return true;}return false;
}
// Local ward coverage: replacing a ward is useful only if personal visibility
// increases inside the current champion's Chebyshev-8 neighbourhood.
bool placeLocalVisionWard(Pos from) {
    struct Choice { Pos p; int score; };
    Choice choices[25]; int n = 0;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const Pos q{from.x + dx, from.y + dy};
        if (S.inb(q) && (S.tile[(size_t)S.idx(q)] & T_BUSH)) choices[n++] = {q, 0};
    }
    if (n == 0) return false;
    // Preserve enough fuel for placement and the remaining end-of-turn work.
    constexpr long long reserve = 5000;
    if (fuel() < reserve + 2500) return false;
    struct Buffers {
        bc::bitboard current{bc::uninitialized}, body{bc::uninitialized};
        bc::bitboard local{bc::uninitialized}, candidate{bc::uninitialized};
    };
    static Buffers b;
    // This must be a fresh read: the champion may have moved since sense().
    if (d::sdk_vision(b.current.ptr()) != 0 ||
        d::sdk_Bitboard_Zero(b.local.ptr()) != 0 ||
        d::sdk_Bitboard_DrawChebyshev(b.local.ptr(), from.x, from.y, (float)rules::VISION) != 0 ||
        d::sdk_Bitboard_Intersect(b.current.ptr(), b.local.ptr(), b.current.ptr()) != 0) return false;
    std::uint32_t before = 0;
    if (d::sdk_Bitboard_NumSetBits(b.current.ptr(), &before) != 0) return false;
    const int x0 = std::max(0, from.x - rules::VISION), x1 = std::min(S.W - 1, from.x + rules::VISION);
    const int y0 = std::max(0, from.y - rules::VISION), y1 = std::min(S.H - 1, from.y + rules::VISION);
    if (before == (std::uint32_t)((x1 - x0 + 1) * (y1 - y0 + 1))) return false;
    // Hypothetical champion vision contains the body only, excluding its old ward.
    const auto self = bc::self();
    self.visible_tiles_from_into(bc::vec2(from.x, from.y), b.body);
    static long long maxCandidateCost = 1024;
    for (int i = 0; i < n; ++i) {
        const long long start = fuel();
        if (start < reserve + maxCandidateCost + 512) return false;
        const Pos q = choices[i].p;
        if (d::sdk_ward_vision(q.x, q.y, b.candidate.ptr()) != 0 ||
            d::sdk_Bitboard_Union(b.body.ptr(), b.candidate.ptr(), b.candidate.ptr()) != 0 ||
            d::sdk_Bitboard_Intersect(b.candidate.ptr(), b.local.ptr(), b.candidate.ptr()) != 0) return false;
        std::uint32_t after = 0;
        if (d::sdk_Bitboard_NumSetBits(b.candidate.ptr(), &after) != 0) return false;
        // |new| - |old| equals newly visible tiles minus lost tiles in this area.
        choices[i].score = (int)after - (int)before;
        maxCandidateCost = std::max(maxCandidateCost, start - fuel());
    }
    std::sort(choices, choices + n, [](const Choice& a, const Choice& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.p.y < b.p.y || (a.p.y == b.p.y && a.p.x < b.p.x);
    });
    for (int i = 0; i < n && choices[i].score > 0; ++i) {
        if (fuel() < reserve) return false;
        if (tryPlaceWard(choices[i].p)) return true;
    }
    return false;
}

bool opdCell(Pos& c) {   /* BH2_OP4: 드래프트 메모리 [16]=0xA7, [17+2t],[18+2t] = 종류 t 의 첫 무리 중심 +1 (mission OPD 와 같은 해석), 한 번만 읽음 */
    static int s_st = -1; static Pos s_c{-1, -1};
    if (s_st < 0) { std::uint8_t m[48] = {}; draftRead(m, 48); int ty = -1;
        if (m[16] != 0xA7) s_st = 0;
        else if (championIdentity(hOf(bc::self()), ty)) { s_st = 0; if (ty >= 0 && ty <= 14 && m[17 + 2 * ty] > 0) { s_c = Pos{(int)m[17 + 2 * ty] - 1, (int)m[18 + 2 * ty] - 1}; s_st = 1; } } }
    c = s_c; return s_st == 1;
}
bool rawStep(Pos me) {   /* R1M: 원자료 [그룹, 금, 젬, 리스폰, 수, (핸들, 종류, x, y) x 수] 에서 가장 가까운 젬 무리 쪽 한 칸 */
    Pos opd{-1, -1}; int opdD = 0;   /* BH2_OP4: OPD 배정 칸이 있으면 가장 가까운 무리 찾기를 건너뜀 */
    if (policy::BH2_OP4_ON && opdCell(opd)) { opdD = std::max(std::abs(opd.x - me.x), std::abs(opd.y - me.y)); if (opdD <= 2) return false; } else opd = Pos{-1, -1};
    static std::vector<std::uint32_t> wv; static std::uint32_t cnt = 0; static bool got = false;
    if (!got && opd.x < 0) { wv.resize(512); if (d::sdk_map_camp_layout(wv.data(), (std::uint32_t)wv.size(), &cnt) == 0 && cnt > wv.size()) { wv.resize(cnt); d::sdk_map_camp_layout(wv.data(), cnt, &cnt); } got = true; }   /* BH2_OP4: OPD 칸이면 원자료 안 읽음 */
    size_t k = 0; int best = 1 << 30; Pos bt{-1, -1};
    if (opd.x >= 0) { bt = opd; best = opdD; k = cnt; }   /* BH2_OP4: 고리 건너뜀 */
    while (k + 5 <= cnt) { const int gems = (int)wv[k + 2]; const std::uint32_t n = wv[k + 4]; k += 5; long long sx = 0, sy = 0; std::uint32_t m = 0;
        for (; m < n && k + 4 <= cnt; ++m, k += 4) { sx += (int)wv[k + 2]; sy += (int)wv[k + 3]; }
        if (gems <= 0 || m == 0) continue; const Pos ce{(int)(sx / m), (int)(sy / m)}; const int dd = std::max(std::abs(ce.x - me.x), std::abs(ce.y - me.y)); if (dd > 2 && dd < best) { best = dd; bt = ce; } }
    if (bt.x < 0) return false;
    Pos cand[8]; int cc[8]; int nc = 0;
    for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) { if (!dx && !dy) continue; const Pos q{me.x + dx, me.y + dy}; const int cq = std::max(std::abs(bt.x - q.x), std::abs(bt.y - q.y)); if (cq >= best) continue; cand[nc] = q; cc[nc] = cq * 4 + std::abs(bt.x - q.x) + std::abs(bt.y - q.y) - cq; ++nc; }
    for (int i = 0; i < nc; ++i) { int b = i; for (int j = i + 1; j < nc; ++j) if (cc[j] < cc[b]) b = j; std::swap(cand[i], cand[b]); std::swap(cc[i], cc[b]); if (moveTo(cand[i])) return true; }
    return false;
}
bool livePosition(Pos& p){d::champion_info ci{};if(d::sdk_champion(hOf(bc::self()),&ci)!=0||ci.health<=0.f)return false;p={ci.x,ci.y};return true;}

int consumableCount(int rawId) { std::uint32_t c[4] = {0, 0, 0, 0}; if (d::sdk_champion_consumables(hOf(bc::self()), c) != 0) return 0; return rawId >= 0 && rawId < 4 ? (int)c[rawId] : 0; }
int tpChannel() { return (int)bc::constants::tp_crystal_time; }   /* TPS */
int recallLen() { const int base = (int)bc::constants::recall_time > 0 ? (int)bc::constants::recall_time : rules::RECALL_TICKS; return (itemCount(31) + itemCount(33) + itemCount(35)) > 0 ? 3 : base; }   /* BH5_IT5: home 엠블럼 = 리콜 3턴 */
int consumableCost(int rawId) { d::consumable_info ci{}; if (d::sdk_consumable_info((std::uint32_t)rawId, &ci) != 0) return 1 << 20; return (int)ci.cost; }
bool buyConsumable(int rawId) { return guarded("buycons", [&] { return d::sdk_buy_consumable((std::uint32_t)rawId) == 0; }); }
bool useConsumable(int rawId, Pos p) { return guarded("usecons", [&] { std::int32_t ok = 0; return d::sdk_use_consumable((std::uint32_t)rawId, p.x, p.y, &ok) == 0 && ok != 0; }); }
void altarLocks(std::vector<LockSeen>& out) {
    out.clear(); d::altar_lock_info buf[32]; std::uint32_t n = 0;
    if (d::sdk_altar_locks(buf, 32, &n) != 0) return;
    const std::uint32_t mine = (std::uint32_t)d::sdk_my_team();
    for (std::uint32_t i = 0; i < n && i < 32; ++i) out.push_back(LockSeen{Pos{buf[i].x, buf[i].y}, buf[i].team == mine, (int)buf[i].hits_remaining});
}
bool attackEnemyLock(Pos altar) {   // LK2
    d::altar_lock_info buf[32]; std::uint32_t n = 0;
    if (d::sdk_altar_locks(buf, 32, &n) != 0) return false;
    const std::uint32_t mine = (std::uint32_t)d::sdk_my_team();
    for (std::uint32_t i = 0; i < n && i < 32; ++i) if (buf[i].team != mine && buf[i].x == altar.x && buf[i].y == altar.y) {
        const std::uint32_t hdl = buf[i].handle;
        return guarded("attacklock", [&] { return d::sdk_attack(6u, hdl) == 0; });
    }
    return false;
}
int priorityNow(std::uint32_t handle) { std::int32_t p = 0; if (d::sdk_bc_champion_priority(handle, 0, &p) == 0) return (int)p; return 0; }   /* TIE */
void setPriority(int p) { guarded("prio", [&] { bc::self().set_priority(p); return true; }); }
int g_wantGroup = -1;   /* SNRM */
int g_respEv = -1, g_respEvR = -1000;   /* TERR T2 */
static int itemShardsOfRaw(std::uint32_t h);   /* FS_IO1 */
int itemShardsOf(std::uint32_t h) {
    if (policy::FS_IO1_ON) {   /* FS_IO1: 같은 라운드 · 같은 행동 번호 · 같은 핸들이면 앞의 값 (그 사이 아이템은 바뀌지 않음) */
        struct E { std::uint32_t h; int v; }; static E s_e[16]; static int s_n = 0, s_r = -1; static std::uint32_t s_ep = 0;
        const int r = round();
        if (r != s_r || s_ep != s_actEp) { s_r = r; s_ep = s_actEp; s_n = 0; }
        for (int i = 0; i < s_n; ++i) if (s_e[i].h == h) return s_e[i].v;
        const int v = itemShardsOfRaw(h);
        if (s_n < 16) { s_e[s_n].h = h; s_e[s_n].v = v; ++s_n; }
        return v;
    }
    std::uint32_t cnt[256] = {}; if (d::sdk_champion_items(h, cnt) != 0) return 0; int t = 0; for (int q = 0; q < (int)bc::item_type::enum_type::count && q < 256; ++q) t += (int)cnt[q] * ((q & 1) ? 5 : 1); return t; }   /* TRP */
static int itemShardsOfRaw(std::uint32_t h) {   /* FS_IO1: 0 채우기는 읽는 칸 (SDK 아이템 수) 만 — 원래 식과 같은 합 순서 */
    constexpr int NC = (int)bc::item_type::enum_type::count < 256 ? (int)bc::item_type::enum_type::count : 256;
    std::uint32_t cnt[256]; for (int q = 0; q < NC; ++q) cnt[q] = 0;
    if (d::sdk_champion_items(h, cnt) != 0) return 0; int t = 0; for (int q = 0; q < NC; ++q) t += (int)cnt[q] * ((q & 1) ? 5 : 1); return t; }
int itemCount(int itemType) {   /* ITN: 옛 번호 → SDK 이름, 배열은 SDK item 수 이상 */
    constexpr std::size_t N = (std::size_t)bc::item_type::enum_type::count + 8;
    static std::uint32_t counts[N > 256 ? N : 256] = {};   /* FUELI: 턴마다 한 번 (사면 다시) */
    if (s_icR != s_turnR) { s_icR = s_turnR; if (policy::FS_IO1_ON) { for (std::size_t q = 0; q < (std::size_t)bc::item_type::enum_type::count; ++q) counts[q] = 0; } else for (auto& c : counts) c = 0;   /* FS_IO1: 읽는 칸만 */ s_icOk = d::sdk_champion_items(hOf(bc::self()), counts) == 0; }
    if (!s_icOk) return 0;
    const std::uint32_t it = itemSdk(itemType); return it < (std::uint32_t)bc::item_type::enum_type::count ? (int)counts[it] : 0;
}

void debugGroup(int gi, Pos me) {
    if (gi < 0 || gi >= (int)S.groups.size()) return;
    const Group& G = S.groups[(size_t)gi];
    bc::bitboard vis{bc::uninitialized}; const int vc = d::sdk_vision(vis.ptr());
    std::uint32_t st = 99; const int sc = d::sdk_camp_group_state(G.handle, &st);
    std::printf("#nbdbg me=%d,%d g=%d state(code %d)=%u vis(code %d):", me.x, me.y, gi, sc, st, vc);
    for (size_t k = 0; k < G.spawn.size(); ++k) {
        const Pos p = G.spawn[k]; bool v = false; if (vc == 0) v = vis.is_set(bc::vec2(p.x, p.y));
        d::camp_state cs{}; const int cc = d::sdk_camp(G.campHandle[k], &cs);
        std::uint32_t h2 = 0; const int ac = d::sdk_camp_at(p.x, p.y, &h2);
        std::printf(" [%d,%d vis=%d camp(code %d) hp=%.0f camp_at(code %d) cheb=%d bush=%d]", p.x, p.y, (int)v, cc, cs.health, ac, cheb(p, me), (int)((S.tile[(size_t)S.idx(p)] & T_BUSH) != 0));
    }
    std::printf("\n");
}

void readShared(std::uint8_t out[8]) { if (d::sdk_read_shared_memory(0, out, 8) != 0) std::memset(out, 0, 8); }
void writeShared(const std::uint8_t in[8]) { d::sdk_write_shared_memory(0, in, 8); }

int submitted(bool ally) { try { return (int)bc::global_submitted_gems(ally ? bc::team::ally : bc::team::enemy); } catch (...) { return 0; } }
std::vector<int> draftPicked(bool ally) { std::vector<int> r; for (auto p : bc::draft::current_draft(ally ? bc::team::ally : bc::team::enemy)) r.push_back((int)p); return r; }
int draftTeamSize() { try { return (int)bc::draft::team_size(); } catch (...) { return 5; } }   /* TSD */
void draftPick(int champ) { bc::draft::pick(static_cast<bc::champion_draft>(champ)); }
void draftWrite(const std::uint8_t* bytes, int n) { d::sdk_bc_submit_draft_memory(bytes, (std::size_t)n); }
void draftRead(std::uint8_t* out, int n) { if (d::sdk_read_draft_memory(0, out, (std::uint32_t)n) != 0) std::memset(out, 0, (size_t)n); }
double mapGemShare() {   /* DRF2 */
    std::vector<std::uint32_t> w(512); std::uint32_t cnt = 0;
    if (d::sdk_map_camp_layout(w.data(), (std::uint32_t)w.size(), &cnt) != 0) return -1.0;
    if (cnt > w.size()) { w.resize(cnt); if (d::sdk_map_camp_layout(w.data(), cnt, &cnt) != 0) return -1.0; }
    double gold = 0.0, gems = 0.0;
    for (std::size_t i = 0; i + 5 <= (std::size_t)cnt;) {
        const double resp = (double)(w[i + 3] > 0 ? w[i + 3] : 1u); const std::uint32_t k = w[i + 4];
        gold += w[i + 1] / resp; gems += w[i + 2] / resp; i += 5 + 4 * (std::size_t)k;
    }
    if (gems + gold <= 0.0) return -1.0;
    return gems / (gems + gold / 30.0);
}

/* TWP: 팀원 bot25 io 도우미 (안개 너머 정체 · 생존 · 든 젬) */
bool championLive(std::uint32_t handle,Unit& out){d::champion_info ci{};if(d::sdk_champion(handle,&ci)!=0||ci.health<=0.f)return false;fillUnit(out,handle,ci,true);return out.alive;}
bool championIdentity(std::uint32_t handle,int&type){d::champion_identity identity{};if(d::sdk_champion_identity(handle,&identity)!=0)return false;type=identity.type;return true;}
bool championAlive(std::uint32_t handle,bool&alive){std::int32_t state=0;if(d::sdk_champion_is_alive(handle,&state)!=0)return false;alive=state!=0;return true;}
bool carriedGems(std::uint32_t handle,int& gems){std::uint32_t n=0;if(d::sdk_champion_gems(handle,&n)!=0)return false;gems=(int)n;return true;}
/* DRF: 팀원 bot25 규칙 1 — 드래프트 첫 턴에 맵 특징 (SDK distance_field) → 픽 계획. 판에서는 메모리 [8..11] 을 읽음 */
const reference::Plan& referencePlan() {
    static reference::Plan result; static bool ready=false;
    if(ready)return result;
    std::uint8_t mem[16]{};
    if(!isDraft()){
        draftRead(mem,16);
        if(mem[8]==0xB6&&mem[9]==1){
            result.profile=mem[10]==255?-1:mem[10];result.flags=mem[11]&3;
            result.picks=std::clamp((int)(mem[11]>>3),1,5);result.exact=(mem[11]&4)!=0;
            for(int k=0;k<5;++k)result.order[k]=k<result.picks?mem[k]:-1;
            ready=true;return result;
        }
    }
    const int W=bc::map::width(),H=bc::map::height(),picks=draftTeamSize();
    std::vector<std::uint32_t> words(512);std::uint32_t count=0;
    if(d::sdk_map_camp_layout(words.data(),(std::uint32_t)words.size(),&count)!=0)return result;
    if(count>words.size()){words.resize(count);if(d::sdk_map_camp_layout(words.data(),count,&count)!=0)return result;}
    words.resize(count);
    const auto f=reference::draftFeatures(W,H,picks,words,
        bc::map::walls().export_linear_buffer(),bc::map::holes().export_linear_buffer(),
        bc::map::altars().export_linear_buffer(),bc::map::spawn_region(bc::team::ally).export_linear_buffer(),
        bc::map::spawn_region(bc::team::enemy).export_linear_buffer(),reference::sdkDraftDistance);
    result=reference::select(f);ready=true;return result;
}

/* OPD: 드래프트 개막 계획 — 첫 픽 차례에 거리 준비 (드래프트에서 되는 SDK 만), 픽마다 몸 종류별 첫 무리 (중심 좌표) */
struct OpdG { int cx, cy, stW, stD, stE = 65535; float V, hp, cf; bool mono = false; };   /* JCO: mono = 모노리스 무리 */   /* OEN: stE = 적 스폰에서 걷는 걸음 */
static std::vector<OpdG> g_opd; static bool g_opdPrep = false, g_opdOk = false;
void opdPrep() {
    if (g_opdPrep) return; g_opdPrep = true;
    loadDesigns();
    const int W = bc::map::width(), H = bc::map::height(), n = W * H; if (n <= 0) return;
    const auto wl = bc::map::walls().export_linear_buffer(), hl = bc::map::holes().export_linear_buffer(), al = bc::map::altars().export_linear_buffer();
    const auto sp = bc::map::spawn_region(bc::team::ally).export_linear_buffer(), es = bc::map::spawn_region(bc::team::enemy).export_linear_buffer();
    std::vector<std::uint32_t> words(512); std::uint32_t count = 0;
    if (d::sdk_map_camp_layout(words.data(), (std::uint32_t)words.size(), &count) != 0) return;
    if (count > words.size()) { words.resize(count); if (d::sdk_map_camp_layout(words.data(), count, &count) != 0) return; }
    std::vector<std::uint8_t> camp((size_t)n, 0), hole((size_t)n, 0), blk((size_t)n, 0);
    for (int i = 0; i < n; ++i) { hole[(size_t)i] = reference::draftBit(hl, i); blk[(size_t)i] = reference::draftBit(wl, i) || reference::draftBit(al, i) || reference::draftBit(es, i); }
    struct GL { std::vector<std::pair<int, int>> cs; std::vector<int> ty; float gold = 0.f, gems = 0.f; };
    std::vector<GL> gl;
    for (std::size_t i = 0; i + 5 <= (std::size_t)count;) {
        GL g; g.gold = (float)words[i + 1]; g.gems = (float)words[i + 2]; const std::uint32_t k = words[i + 4];
        for (std::uint32_t c = 0; c < k && i + 5 + 4 * c + 3 < (std::size_t)count; ++c) {
            const int ty = (int)words[i + 5 + 4 * c + 1], x = (int)words[i + 5 + 4 * c + 2], y = (int)words[i + 5 + 4 * c + 3];
            g.cs.push_back({x, y}); g.ty.push_back(ty); if (x >= 0 && y >= 0 && x < W && y < H) camp[(size_t)(y * W + x)] = 1;
        }
        gl.push_back(g); i += 5 + 4 * (std::size_t)k;
    }
    auto stand = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && !blk[(size_t)(y * W + x)] && !hole[(size_t)(y * W + x)] && !camp[(size_t)(y * W + x)]; };
    std::vector<std::uint8_t> bW((size_t)n), bD((size_t)n);
    const int dirs[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const size_t i = (size_t)(y * W + x); const bool base = blk[i] || camp[i]; bW[i] = base || hole[i]; bool br = false;
        if (hole[i] && !blk[i]) for (const auto& dd : dirs) if (stand(x + dd[0], y + dd[1]) && stand(x - dd[0], y - dd[1])) { br = true; break; }
        bD[i] = base || (hole[i] && !br);
    }
    const auto dW = reference::sdkDraftDistance(W, H, bW, sp), dD = reference::sdkDraftDistance(W, H, bD, sp);
    std::vector<std::uint8_t> bE((size_t)n); for (int i = 0; i < n; ++i) bE[(size_t)i] = reference::draftBit(wl, i) || reference::draftBit(al, i) || camp[(size_t)i] || hole[(size_t)i];   /* OEN: 적 스폰 거리장 (적 스폰 칸은 출발점) */
    const auto dE = reference::sdkDraftDistance(W, H, bE, es);
    for (const auto& g : gl) {
        if (g.cs.empty()) continue; OpdG o{}; long long sx = 0, sy = 0; for (auto& c : g.cs) { sx += c.first; sy += c.second; }
        o.cx = (int)(sx / (long long)g.cs.size()); o.cy = (int)(sy / (long long)g.cs.size()); o.stW = 65535; o.stD = 65535;
        for (auto& c : g.cs) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int x = c.first + dx, y = c.second + dy; if (!stand(x, y)) continue; const size_t i = (size_t)(y * W + x);
            if (dW[i] >= 0) o.stW = std::min(o.stW, dW[i]); if (dD[i] >= 0) o.stD = std::min(o.stD, dD[i]); if (policy::OEN_ON && dE[i] >= 0) o.stE = std::min(o.stE, dE[i]);
        }
        o.V = g.gems + g.gold / 2.5f; std::vector<std::pair<float, float>> cs;
        for (int ty : g.ty) { const float h = S.campHp(ty), s = std::max(0.f, S.campStr(ty)); o.hp += h; cs.push_back({h, s}); if (ty == rules::C_MONOLITH) o.mono = true; }   /* JCO */
        std::sort(cs.begin(), cs.end()); float sA = 0.f; for (auto& c : cs) sA += c.second; for (auto& c : cs) { o.cf += sA * c.first; sA -= c.second; }
        g_opd.push_back(o);
    }
    g_opdOk = !g_opd.empty();
}
static void jcoAssign(const std::vector<int>& types, std::uint8_t* out) {   /* JCO: 개막 연합 배정 — (무리, k) 마다 dps 높은 k 몸이 함께 갈 때 몸당 벌이, 가장 높은 것부터 묶어 배정 */
    struct Bd { int ty; float dps, bud; bool done; }; Bd bd[16]; int nb = 0;
    for (int ty : types) {
        if (ty < 0 || ty > 14 || nb >= 16) continue; const ChampDesign& D = S.designOf(ty); if (!D.valid) continue;
        float dps = std::max(1.f, D.str);   /* opdAssign 과 같은 dps */
        for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s];
            if (a.kind == rules::AB_RESET) dps += std::max(1.f, D.str) / (float)(a.cooldown + 1);
            else if (a.kind == rules::AB_DAMAGE || a.kind == rules::AB_AOE || a.kind == rules::AB_CC || (a.kind == rules::AB_DASH && a.modifier > 0.f)) dps += D.focus * a.modifier / (float)(a.cooldown + 1); }
        bd[nb++] = Bd{ty, dps, std::max(1.f, D.hp * (1.f - policy::RECOVER_HP)), false};
    }
    std::stable_sort(bd, bd + nb, [](const Bd& x, const Bd& y) { return x.dps > y.dps; });   /* 연합은 dps 높은 몸부터 (같으면 픽 순서) */
    std::vector<char> used(g_opd.size(), 0);
    for (int it = 0; it < nb; ++it) {
        int bestG = -1, bestK = 0; float bestR = 0.f;
        for (size_t gi = 0; gi < g_opd.size(); ++gi) {
            const OpdG& o = g_opd[gi]; if (used[gi] || o.mono || o.V <= 0.f || o.hp <= 0.f) continue; const int st = o.stW; if (st >= 65535) continue; if (policy::OEN_ON && o.stE < st) continue;   /* OEN · OPDW 그대로 */
            float dK = 0.f, bK = 0.f; int k = 0;
            for (int i = 0; i < nb; ++i) {
                if (bd[i].done) continue; ++k; dK += bd[i].dps; bK += bd[i].bud;
                const float a = (float)st, T = o.hp / dK, Dm = o.cf / dK, bAvg = bK / (float)k;
                const float Hh = Dm > bK ? std::ceil((Dm - bK) / bAvg) * ((float)(rules::RECALL_TICKS + 1) + a) : 0.f;   /* opdAssign 의 Hh 를 k 몸 예산으로 */
                const float rate = o.V / (float)k / (a + T + (float)rules::CAPTURE_ROUNDS + Hh);   /* 몸당 벌이 */
                if (rate > bestR) { bestR = rate; bestG = (int)gi; bestK = k; }
            }
        }
        if (bestG < 0) break;
        used[(size_t)bestG] = 1; int k = 0;
        for (int i = 0; i < nb && k < bestK; ++i) {
            if (bd[i].done) continue; bd[i].done = true; ++k;
            out[1 + 2 * bd[i].ty] = (std::uint8_t)std::min(255, g_opd[(size_t)bestG].cx + 1); out[2 + 2 * bd[i].ty] = (std::uint8_t)std::min(255, g_opd[(size_t)bestG].cy + 1);
        }
        bool any = false; for (int i = 0; i < nb; ++i) if (!bd[i].done) { any = true; break; } if (!any) break;
    }
    out[0] = 0xA7;
}
void opdAssign(const std::vector<int>& types, std::uint8_t* out) {   // out[0..31]: [0] 0xA7, [1+2t],[2+2t] = 중심 x+1, y+1
    std::memset(out, 0, 32); if (!g_opdOk) return;
    if (policy::JCO_ON) { jcoAssign(types, out); return; }   /* JCO */
    std::vector<float> dSum(g_opd.size(), 0.f); std::vector<int> cnt(g_opd.size(), 0);
    for (int ty : types) {
        if (ty < 0 || ty > 14) continue; const ChampDesign& D = S.designOf(ty); if (!D.valid) continue;
        float dps = std::max(1.f, D.str);
        for (int s = 0; s < 2; ++s) { const AbilitySpec& a = D.ab[s];
            if (a.kind == rules::AB_RESET) dps += std::max(1.f, D.str) / (float)(a.cooldown + 1);
            else if (a.kind == rules::AB_DAMAGE || a.kind == rules::AB_AOE || a.kind == rules::AB_CC || (a.kind == rules::AB_DASH && a.modifier > 0.f)) dps += D.focus * a.modifier / (float)(a.cooldown + 1); }
        bool dash = false; float spd = 1.f;
        for (int s = 0; s < 2; ++s) if (D.ab[s].kind == rules::AB_DASH && D.ab[s].range >= 2.f) { dash = true; spd += D.ab[s].range / (float)(D.ab[s].cooldown + 1); }
        const float bud = std::max(1.f, D.hp * (1.f - policy::RECOVER_HP)); int bestG = -1; float bestR = 0.f;
        for (size_t gi = 0; gi < g_opd.size(); ++gi) {
            const OpdG& o = g_opd[gi]; if (o.V <= 0.f || o.hp <= 0.f) continue; const int st = o.stW; if (st >= 65535) continue; if (policy::OEN_ON && o.stE < st) continue;   /* OEN: 적이 우리보다 먼저 닿는 무리는 개막 배정에서 뺌 */   /* OPDW: 개막은 모두 걷기 길 (같은 길로 함께) */
            (void)dash; const float a = (float)st, dAll = dSum[gi] + dps, T = o.hp / dAll, share = dps / dAll, Dm = o.cf / dAll, budget = bud * (float)(cnt[gi] + 1);
            const float Hh = Dm > budget ? std::ceil((Dm - budget) / bud) * ((float)(rules::RECALL_TICKS + 1) + a) : 0.f;
            const float rate = o.V * share / (a + T + (float)rules::CAPTURE_ROUNDS + Hh);
            if (rate > bestR) { bestR = rate; bestG = (int)gi; }
        }
        if (bestG >= 0) { out[1 + 2 * ty] = (std::uint8_t)std::min(255, g_opd[(size_t)bestG].cx + 1); out[2 + 2 * ty] = (std::uint8_t)std::min(255, g_opd[(size_t)bestG].cy + 1); dSum[(size_t)bestG] += dps; ++cnt[(size_t)bestG]; }
    }
    out[0] = 0xA7;
}

}  // namespace nb::io

#include "priority_policy.h"   /* PRI */
