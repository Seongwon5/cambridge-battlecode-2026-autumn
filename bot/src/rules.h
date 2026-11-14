#pragma once
// rules.h — 엔진 규칙 (룰북 · SDK 에서 확정된 것). 정책이 아니다. 바꾸려면 룰북을 다시 읽는다.
#include <algorithm>
#include <cstdint>

namespace nb::rules {

constexpr int VISION = 8;              // 챔피언 시야 (체비셰프)
constexpr int MELEE_RANGE = 1;
constexpr int RANGED_RANGE = 3;
constexpr int CAMP_SLEEP_ROUNDS = 2;   // 사거리 안에 챔피언 없이 2라운드면 잠듦
constexpr int CAPTURE_ROUNDS = 5;
constexpr int RESPAWN_TICKS = 60;      // 죽음 → 60 자기 턴끝
constexpr int RECALL_TICKS = 5;
constexpr int SUBMIT_RANGE = 1;
constexpr int TELEPORT_RANGE = 3;     /* TPS: 순간이동 수정 착지 거리 (SDK cambc.h tp_crystal "within range 3", 체비셰프) */        // 제단에서 체비셰프 1 안
constexpr int SHARD_COST = 30;
constexpr int WIN_GEMS = 1000;

// 바치기 길이 = max(1, ceil(젬/10)) 자기 턴끝. 완료는 0 이 된 다음 내 턴 시작.
constexpr int tributeTicks(int gems) { return std::max(1, (gems + 9) / 10); }

// 챔피언 상태 비트 (champion_state)
constexpr std::uint32_t ST_ALIVE = 1u << 0, ST_RECALLING = 1u << 1, ST_SUBMITTING = 1u << 2, ST_CAN_MOVE = 1u << 3,
                        ST_CAN_ATTACK = 1u << 4, ST_COMPLETED_TURN = 1u << 5, ST_CAN_CAST = 1u << 11, ST_TELEPORTING = 1u << 12;

// 능력 종류 (ability_kind)
enum Ability : int { AB_NONE = 0, AB_RESET = 1, AB_DAMAGE = 2, AB_AOE = 3, AB_CC = 4, AB_HEAL = 5, AB_DASH = 6, AB_SHIELD = 7 };

// 몬스터 종류 (camp_type): wolf 0 · elk 1 · gem_cluster 2 · gold_cluster 3 · monolith 4
enum Camp : int { C_WOLF = 0, C_ELK = 1, C_GEM = 2, C_GOLD = 3, C_MONOLITH = 4 };

// 턴 순서 (룰북 turn-order): 두 팀이 번갈아 두고 먼저 두는 팀은 라운드마다 바뀐다. 그래서 적 하나가 내 이번 턴과 다음 턴 사이에 두는 횟수는
//   이번 라운드에 이미 뒀으면 1번, 아직 안 뒀으면 2번 (이번 라운드 뒤쪽 + 다음 라운드 앞쪽).
constexpr int enemyTurnsBeforeMine(bool enemyActedThisRound) { return enemyActedThisRound ? 1 : 2; }


}  // namespace nb::rules
