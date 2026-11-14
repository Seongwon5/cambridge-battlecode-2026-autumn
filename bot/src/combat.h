#pragma once
// combat.h — 한 턴의 행동 조합 (이동 칸 · 평타 표적 · 주문) 을 같은 단위 (젬) 로 셈해 하나를 고른다.
#include "world.h"
#include "mission.h"

namespace nb::combat {
void noteDashFail();   // DFB
void noteMoveFail(Pos p);   // MFB
int fightCall();   // PL: 이번 라운드 싸움 알림 구역 (없으면 -1)

extern int g_noEnter;
extern int g_mstep;   /* MINE4 */
extern int g_ccdLeave;   /* CCD: 점령을 내려놓고 사거리 밖으로 나간 라운드 */
extern int g_ccdMove;    /* CCD: 같은 무리의 덜 맞는 점령 칸으로 옮긴 라운드 */   // NE: 연속 '못 들어감' 턴 수
struct Plan {
    Pos cell;                      // 이 턴이 끝날 때 서 있을 칸 (= 지금 칸이면 이동 없음)
    bool move = false, dash = false; int dashSlot = -1; bool dashWalk = false; Pos dashTo;
    bool cut = false;   // FUEL: 연료로 계산을 건너뜀   // DWK: 대시 (dashTo) 뒤 걷기 (cell)
    int atkKind = 0; std::uint32_t atkHandle = 0; int atkWhen = 0;   // atkKind 1 챔피언 · 2 캠프; atkWhen 1 이동 전 · 2 이동 후
    bool resetAttack = false; int resetSlot = -1;                     // 평타 → 리셋 → 평타
    int atk2Kind = 0; std::uint32_t atk2Handle = 0;                    // RST: 리셋 뒤 걷고 칠 다른 표적 (0 = 같은 표적)
    int castSlot = -1; int castKind = 0; std::uint32_t castHandle = 0; Pos castPos; bool castBeforeMove = false;   // castKind 0 자기 · 1 챔피언 · 2 캠프 · 3 땅
    float value = 0.f, dmgTaken = 0.f, dmgDealt = 0.f;
    float hereDmg = -1.f;          // RCC: 지금 칸 피해 예보 (계산 없던 턴 -1)
    bool lethalAll = false;        // 모든 칸이 죽을 칸
    int nCells = 0, nCombos = 0;
    long long fuelCells = 0, fuelDmg = 0, fuelCombos = 0;   // 진단: 구간 연료
};

Plan choose(const World& w, mission::Mission& M);
extern bool g_gdsSeen; void gdsPrep(const World& w); void gdsApply(const World& w, const mission::Mission& M, Plan& p);   /* GDS */

// 순수 식 (자체 시험용)
float gemPerHp(int gems, float maxHp);
float dmgCost(float dmg, float hp, float maxHp, int gems);
float fightOdds(const World& w);
float fightKillRounds(const World& w);   /* LFD */
float unitDps(int type, float str, float focus);
void dwlSums(const World& w, float& hA, float& dA, float& hE, float& dE);   /* DWL */
float dwlL(const World& w);   /* RTK */   // R3L: fnDps 공개 (후퇴 힘 비교도 같은 식)   // JNA: 곁 싸움 승산 (PL 과 같은 란체스터), 적 없으면 1
float enemyTwinAdd(const World& w, const Unit& e);   /* BH4_MC6: 적 Cassi/Issac 짝 몫 (짝 gems + 죽음 시간, 짝 안 보이면 죽음 시간만) */
void campAggroNow(const World& w, const mission::Mission& M, float& hit, float& inRange);   /* TNK */
float enemyThreatAt(const World& w, Pos p);
float enemyThreatFor(const World& w, Pos p, float tgtHp, int tgtType, std::uint32_t tgtHandle);   /* BH2_SP1: 대상 기준 (가림 · CCS) */
float enemyThreatAtC(const World& w, Pos p);   /* MINE: 라운드마다 한 번 지은 위협 표의 threatAt (할인 없음) */
bool fbStay(const World& w);
bool allyCrisis(const World& w, int rounds);   /* TAC */
float teamStayGain(const World& w); bool joinTargetNow(const World& w); float depthK(Pos p); float twinStake(const World& w);   /* TFV */   /* FB: 깎인 적 마무리가 기대 젬 + 면 후퇴하지 않음 */   // LFR: 칸 p 의 적 피해 예보 (choose 의 threatAtTgt 와 같은 식, 캠프 뺌)
}  // namespace nb::combat
