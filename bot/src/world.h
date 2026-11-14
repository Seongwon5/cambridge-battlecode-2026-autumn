#pragma once
// world.h — 턴 스냅숏과 정적 지도. 순수 자료 (SDK 타입 없음) → 호스트 자체 시험이 가능하다.
#include <cstdint>
#include <vector>
#include <new>
#include <cstddef>
#include <cstring>
#include "rules.h"

namespace nb {

// IA: 값 초기화 없는 할당자 (resize 가 칸마다 0 을 쓰지 않는다 — 채우기는 memset 으로)
template <class T> struct NoInitAlloc {
    using value_type = T;
    NoInitAlloc() = default;
    template <class U> NoInitAlloc(const NoInitAlloc<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(::operator new(n * sizeof(T))); }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p); }
    template <class U> void construct(U*) noexcept {}   // 기본 생성 = 아무것도 안 함
    template <class U, class... A> void construct(U* p, A&&... a) { ::new ((void*)p) U(static_cast<A&&>(a)...); }
    template <class U> bool operator==(const NoInitAlloc<U>&) const noexcept { return true; }
    template <class U> bool operator!=(const NoInitAlloc<U>&) const noexcept { return false; }
};
template <class T> using RawVec = std::vector<T, NoInitAlloc<T>>;

struct Pos { int x = -1, y = -1; };
constexpr bool operator==(Pos a, Pos b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(Pos a, Pos b) { return !(a == b); }
constexpr int cheb(Pos a, Pos b) { const int dx = a.x > b.x ? a.x - b.x : b.x - a.x, dy = a.y > b.y ? a.y - b.y : b.y - a.y; return dx > dy ? dx : dy; }

// 지형 비트 (칸마다 uint8)
constexpr std::uint8_t T_WALL = 1, T_HOLE = 2, T_BUSH = 4, T_ALTAR = 8, T_ALLY_SPAWN = 16, T_ENEMY_SPAWN = 32, T_CAMP = 64;
constexpr std::uint8_t T_BLOCKED = T_WALL | T_HOLE | T_ALTAR | T_ENEMY_SPAWN;   // 걸을 수 없는 고정 칸 (캠프 칸은 살았을 때만 막힘)

struct AbilitySpec {           // 챔피언 설계의 능력 하나
    int kind = rules::AB_NONE; // rules::Ability
    float range = 0.f, modifier = 0.f, radius = 0.f;
    int duration = 0; int cooldown = 0;   // 설정 쿨다운 (라운드)
    bool noWalk = false;       // "걷기 불가": 걸은 뒤에는 못 씀 (제자리에서만)
    bool targetsEnemies = false, targetsCamps = false, targetsGround = false, targetsSelf = false, targetsAllies = false;
};
struct ChampDesign {           // ED: 최대 MAX_CHAMP_TYPES 종류 (type 번호 = champion_type, 패치의 새 챔피언 12 포함)
    int type = -1, attackRange = 1;
    float hp = 0, str = 0, focus = 0, hpGrow = 0, strGrow = 0, focusGrow = 0;
    int passive = 0;
    AbilitySpec ab[2];
    bool valid = false;
};
struct CampDesign { float hp = 0, str = 0; int range = 3; int gems = 0, gold = 0, respawn = 0; bool valid = false; };
constexpr int MAX_CHAMP_TYPES = 16, MAX_CAMP_TYPES = 8;   // ED: 엔진이 알려 주는 종류 수 상한 (패치: 골렘 · Heart of Ruin)

struct Group {                 // 캠프 그룹 (정적)
    std::uint32_t handle = 0;
    int gems = 0, gold = 0, respawn = 0;
    std::vector<std::uint32_t> campHandle;
    std::vector<Pos> spawn;    // 캠프마다 스폰 칸 (= 살았을 때 위치)
    std::vector<int> campType;
    std::vector<float> campLastHp;   // 캠프마다 마지막으로 본 체력 (줄었으면 누가 치고 있다 = 깼다)
    size_t seenOffset = 0;  // 개별 관측 시각은 전체 몹 배열에 한 번만 할당
    float hpTotal = 0.f, strMax = 0.f;
    int rangeMax = 3;          // ED: 그룹 캠프 사거리 최댓값 (엔진 캠프 설계)
    bool mono = false;         // ED: 모노리스가 있는 그룹 (종류 번호로)
    Pos center;                // 스폰 칸 평균 (가까움 셈용)
    std::vector<int> near;     // 스폰 칸이 서로 캠프 사거리 + 1 안에 있는 다른 그룹 (정적, 처음 물을 때 한 번 채움) — 그 곁에 서면 맞는다
    bool nearReady = false;
    // 기억 (턴마다 갱신)
    int state = 0;             // 0 모름 · 1 살아 있음 · 2 보상 대기 (점령) · 3 리스폰 중
    int stateRound = -1000;    // 그 상태를 본 라운드
    int respawnUntil = -1;     // 리스폰 끝 라운드 (3 일 때)
    int awakeUntil = -1;       // 깬 그룹 (피해를 받음 — 룰: 피해가 깨우고, 사거리 안에 챔피언 없이 2라운드면 잠듦). 잠든 캠프는 치지 않는다
    std::uint64_t passVersion = 0;
    bool passExpired = false;  // 관측값과 별개인 재생성 만기 추정
};

struct Unit {                  // 보이는 챔피언 (적 · 아군) 스냅숏
    std::uint32_t handle = 0;
    int type = -1; Pos pos; float hp = 0, maxHp = 0, str = 0, focus = 0; int range = 1;
    std::uint32_t state = 0;   // champion_state 비트
    int cooldown[2] = {0, 0};
    int gems = 0;
    bool visible = false, alive = true;
    bool acted = false;        // 이번 라운드에 이미 뒀나 (completed_turn)
    int prio = 0;
    int stunLeft = 0;   /* BH4_HR8: 막힌 차례 수 (0 · 1 · 2) */
    int chLeft = -1;   /* PRA: 리콜 중이면 남은 채널 (sdk_champion_channel_time), 아니면 -1 */
};
struct CampUnit {              // 보이는 살아 있는 캠프
    std::uint32_t handle = 0; int group = -1, type = 0; Pos pos; float hp = 0, maxHp = 0, str = 0;
};

struct Me {
    std::uint32_t handle = 0; int type = -1; Pos pos; float hp = 0, maxHp = 0, str = 0, focus = 0; int range = 1;
    std::uint32_t state = 0; int cooldown[2] = {0, 0}; int gems = 0, gold = 0;
    bool alive = true, canMove = false, canAttack = false, canCast = false, channeling = false, atSpawn = false;
    int wardCooldown = 0;
};

struct Static {
    int W = 0, H = 0, nAllies = 0, nEnemies = 0;
    RawVec<std::uint8_t> tile;         // W*H 지형 비트
    RawVec<std::uint8_t> bridge; bool dashNav = false; int dashSlot = -1, dashRange = 0;   /* DSH: 대시 다리 칸 (구덩이 1칸, 양옆 설 수 있음) · 내가 대시로 다님 */
    RawVec<std::uint8_t> campBlocked;  // 몹만의 막힘: 살아 있음/모름=1, 빈 칸=0
    RawVec<int> campSeenRound;         // 몹당 마지막 개별 관측 시각
    int openCampTiles = 0;
    std::uint64_t campVersion = 1;
    std::uint64_t openVersion = 1;   /* PFX: 캠프 죽음 (길이 열릴 수 있음) 때만 오름 */
    std::vector<int> campChangedIdx; bool campChangedOverflow = true;   /* CWI: 막힘이 바뀐 캠프 칸 (거리장 벽 버퍼를 그 칸만 고침) */
    RawVec<std::int16_t> campGroupAt;   /* GRP: int16 */   // W*H: 칸의 캠프 그룹 (-1 없음; 그룹 ≤ 127). 칸 배열은 모두 1바이트 (채우기 = memset 하나; int 배열 채우기는 칸마다 ~5 연료)
    std::vector<Pos> altars, allySpawn, enemySpawn;
    Pos allyCenter, enemyCenter, mapCenter;
    std::vector<Group> groups;
    std::vector<std::uint32_t> allyHandle, enemyHandle;
    ChampDesign design[MAX_CHAMP_TYPES];
    CampDesign camp[MAX_CAMP_TYPES];
    int campRangeStrong = 3;   // ED: 힘 있는 (모노리스 아닌) 캠프 사거리 최댓값 — 카이팅 주문 사거리 기준
    int killGold = 50, lockHits = 3, wardCooldownSetting = 1;
    int subBase = 0, subMin = 1; double subMult = 0.1;   // TRQ: 바치기 채널 = max(subMin, subBase + ceil(젬 x subMult)) (엔진 설정, 룰북 기본값)   // ED: 엔진 설정 (game_settings)
    int campRange(int t) const { return (t >= 0 && t < MAX_CAMP_TYPES && camp[t].valid) ? camp[t].range : 3; }
    float campHp(int t) const { return (t >= 0 && t < MAX_CAMP_TYPES && camp[t].valid) ? camp[t].hp : 0.f; }
    float campStr(int t) const { return (t >= 0 && t < MAX_CAMP_TYPES && camp[t].valid) ? camp[t].str : 0.f; }
    const ChampDesign& designOf(int t) const { return design[(t >= 0 && t < MAX_CHAMP_TYPES) ? t : 0]; }
    int myIndex = -1;                       // sdk_my_champion
    RawVec<std::uint8_t> occStamp;     // W*H: 이 라운드에 보이는 유닛이 선 칸 (= 라운드 & 255 + 1; 0 = 없음). O(1) 점유 검사
    bool ready = false;
    static std::uint8_t stampOf(int round) { return (std::uint8_t)((round & 255) | 0); }
    bool occupiedNow(Pos p, int round) const { return inb(p) && occStamp[(size_t)idx(p)] == (std::uint8_t)(1 + (round & 127)); }
    int idx(Pos p) const { return p.y * W + p.x; }
    bool inb(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < W && p.y < H; }
    bool walkableStatic(Pos p) const { return inb(p) && !(tile[idx(p)] & T_BLOCKED); }
    bool passNav(Pos p) const { return walkableStatic(p) || (dashNav && inb(p) && bridge.size() == tile.size() && bridge[idx(p)] != 0); }   /* DSH: 길찾기용 통행 (다리 칸 포함) */
    bool campBlocks(Pos p) const { return inb(p) && (size_t)idx(p) < campBlocked.size() && campBlocked[(size_t)idx(p)] != 0; }
    bool campAlive(const Group& g, size_t k) const { return k < g.spawn.size() && campBlocks(g.spawn[k]); }
    void initCampPassability() {
        campBlocked.resize((size_t)W * H); std::memset(campBlocked.data(), 0, campBlocked.size());
        openCampTiles = 0; campVersion = 1; openVersion = 1; campChangedIdx.clear(); campChangedOverflow = true;   /* CWI */
        size_t members = 0; for (auto& g : groups) { g.seenOffset = members; members += g.spawn.size(); }
        campSeenRound.resize(members); std::memset(campSeenRound.data(), 0xFF, members * sizeof(int));
        for (auto& g : groups) {
            g.passVersion = 0; g.passExpired = false;
            for (Pos p : g.spawn) if (inb(p)) campBlocked[(size_t)idx(p)] = 1;
        }
    }
    static bool respawnExpired(const Group& g, int r) {
        return g.state == 3 && g.stateRound != r && g.respawnUntil <= r;
    }
    // 관측 후의 최종 상태만 반영한다. 전역 버전은 호출부에서 한 번만 증가.
    int refreshCampGroup(size_t gi, int r) {
        Group& g = groups[gi]; const bool expired = respawnExpired(g, r);
        const bool empty = g.state == 2 || (g.state == 3 && !expired);
        int changes = 0;
        for (size_t k = 0; k < g.spawn.size(); ++k) {
            const Pos p = g.spawn[k]; if (!inb(p)) continue;
            const float hp = k < g.campLastHp.size() ? g.campLastHp[k] : -1.f;
            const size_t member = g.seenOffset + k;
            const int seen = member < campSeenRound.size() ? campSeenRound[member] : -1000;
            const bool newerMember = seen > g.stateRound && seen >= g.respawnUntil;
            const bool blocked = (seen == r && hp > 0.f) || (!empty && ((expired && !newerMember) || hp != 0.f));
            auto& old = campBlocked[(size_t)idx(p)];
            if ((old != 0) != blocked) { old = blocked ? 1 : 0; openCampTiles += blocked ? -1 : 1; ++changes; if (campChangedIdx.size() < 4096) campChangedIdx.push_back(idx(p)); else campChangedOverflow = true; }   /* CWI */
        }
        g.passExpired = expired;
        if (changes) ++g.passVersion;
        return changes;
    }
};

struct World {                 // 한 턴 스냅숏
    int round = 0;
    Me me;
    std::vector<Unit> enemies, allies;      // 보이는 것만 (allies: 시야 밖은 공유 칸으로 — team 이 채움)
    std::vector<CampUnit> camps;            // 보이는 살아 있는 캠프
    std::vector<Pos> occupied;              // 보이는 유닛 칸 (이동 불가)
    std::uint8_t shared[8] = {};
    bool enemyNear8 = false;
};

}  // namespace nb
