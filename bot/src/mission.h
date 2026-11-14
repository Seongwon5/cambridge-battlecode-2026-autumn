#pragma once
// mission.h — 임무 관리자: 끝낼 일 하나를 오래 기억한다. 완료 · 중단 조건으로만 바꾼다.
#include "world.h"
#include "io.h"

namespace nb::mission {
float r3Ratio(const World& w);   // ENG3: 하나의 잣대 L = 우리 힘 / 위협 적 힘 (위협 적 없으면 99, 라운드마다 한 번 셈)
float trpPub(const World& w);   // FNR: P(죽음)
bool loneOutnumbered(const World& w);   // LONE: 혼자 · 보이는 적 2+ · 란체스터 열세 (R3_RATIO)
bool r3Outnumbered(const World& w);   // R3: 위협 적이 있고 적 힘 ≥ 우리 힘 × R3_RATIO (후퇴 · R2, 매번 셈)
bool cxeBackOff();   // CXE3: 어그로를 동료에게 넘기고 물러나는 턴
int helpRequest();   // ES: 이번 라운드 내 도움 요청 제단 (없으면 -1)
bool ralOut(const World& w, const Unit& a);   /* RAL: 이 아군이 지금 싸움에서 빠지는 중인가 (채널 · 회복 알림) */
bool threatening(const World& w, const Unit& e);   // TH
bool disturbed(const World& w);   /* MINE: 방해 (적이 내 무리 캠프를 침 · 2R 안 맞음 · 내 칸 또는 목표 쪽 모든 걸음 칸이 사거리 안) */
bool visNear(const World& w);   /* MINE: 기록용 (옛 VIS_NEAR) */
int lastDisturbRound();   /* MINE */

enum Kind : int { M_NONE = 0, M_FARM = 1, M_CAPTURE = 2, M_TRIBUTE = 3, M_RECOVER = 4, M_SHOP = 5, M_ESCORT = 6 };   // ES: 호위

struct Mission {
    Kind kind = M_NONE;
    int group = -1, altar = -1;
    int since = -1;            // 시작 라운드
    int lethalStreak = 0;      // 모든 행동 조합이 음수였던 연속 턴 (combat 이 올림)
    int bestD = 99999, progressRound = -1;   // 목표 걸음이 마지막으로 줄어든 라운드 (정체 판정)
    std::uint64_t goalPassSig = 0;  // 현재 그룹/이웃의 목표 칸 의존성
    bool retreat = false;      // R_RETREAT 로 시작한 회복 임무 (적이 사라지면 일찍 끝낸다)
    int lastEnemyRound = -1000;   // 적이 RETREAT_ENEMY_R 안에 보인 마지막 라운드 (setGoal 이 지우지 않음)       // 임무 그룹의 '산 (또는 모름)' 캠프 수 — 바뀌면 공격 칸을 다시 뿌린다 (M_ALIVE_TILES)
    int lastHitRound = -1000;  // 마지막으로 캠프를 친 라운드 (main 이 갱신) — 목표 칸에서 치지 못하면 정체
    std::vector<Pos> blind;    // 이 그룹의 공격 칸 중 캠프가 보이지 않던 칸 (덤불 · 벽이 시선을 막음) — 목표에서 뺀다
    const char* why = "-";     // 이번 턴 진행이 없었던 이유 (기록)
};

extern Mission M;
extern bool g_zrEn; extern long long g_zrCalm;   /* ZOR */
extern long long g_crsCb;   /* CRS: 적 곁 전투 연료 (잰 값) */
extern io::Field dGoal;        // 임무 목표 거리장 (목표 칸들에서 뿌림)
extern io::Field dMe;          // 내 칸 거리장 (임무 고르기 때만)

void update(World& w);
void kbNoteDead();
void warmMe(const World& w);   /* MFW */   // KB
void prewarmTiles(long long floorFuel);
void prewarmGF(long long floorFuel);
float wkdSpeed(const World& w);   /* BH5_CS7: 칸/라운드 (대시 설계값) */
void prewarmCampTerritory(long long floorFuel);   /* TM_CAMP: 턴 행동 뒤 땅 걸음장 미리 (남는 연료로만) */
int homeSteps(Pos p);                     // RT: 집 거리장 걸음 (없으면 -1)      // GF: 그룹 · 집 정적 거리장 미리   // LC: 목표 칸 정적 목록 미리 (턴 끝 · 죽은 턴)
void noteBody(const World& w);            // LC: 이 몸의 사거리 기억 (죽은 턴의 예열용)             // PN: 남은 연료로 그룹 이웃 목록 미리 (첫 턴)                       // 턴마다: 유효성 → 완료 → 새 임무 → 거리장
std::vector<Pos> goalTiles(const World& w);  // 지금 임무의 목표 칸들 (거리 0)
int stepsToGoal(Pos p); bool bridgeAhead(const World& w, int steps);   /* BRK */
void blockAltar(int a, int until);          // LK: 적 잠금에 막힌 제단
bool altarBlocked(int a, int round);                      // dGoal 로 (준비 안 됐으면 cheb)
float missionGems(const World& w);
float groupHpLeft(int gi);
float goldDiv();
float myDeathTime();   /* DTR: 내 죽음의 시간 값 (젬) */   // VMR: 금 → 젬 나눗수 (GVT)
void tpsPrewarm(long long floorFuel);   /* TPS: 지름길 표 (남는 연료) */
bool tpsPick(const World& w, Pos& b, int& save, float& val);   /* TPS: 지금 칸에서 뛸 착지 칸 (값 > 0 이면 true) */
bool tpsMoveCell(const World& w, Pos& q);   /* TPS: 지름길 출발 칸으로 한 걸음 */
bool tpsAtA(const World& w);   /* TPS: 출발 칸에 섰다 */
void tpsDrop();   /* TPS: 길 버림 */
void tpsUsed(const World& w, Pos b, int save, float val);   /* TPS */
void tpsNoteBuy();   /* TPS */
bool tpsWorthBuying(const World& w);   /* TPS */
int tpsChannel();   /* TPS */

Pos focusCamp(const World& w, int gi);   // FCS: 그룹 안 1순위 캠프 (힘/남은 체력 큰 것)
bool groupUnreachable(int gi);
bool tfpHold(const World& w, int group);
void tbaPrewarm(const World& w, long long floorFuel);   /* TBA v11: 턴 끝 남는 연료로 팀 큰 무리 계획 */   // TFP2: 목표 무리는 모일 때까지 안 침   // NFX: UNR 로 걸어서 못 간다고 확인된 그룹
int   missionWorkTurns(const World& w);      // 목표 칸에 닿은 뒤 끝내기까지 걸리는 턴 (처치 · 점령 5 · 바치기 ceil(젬/10) · 회복 5)
const char* name(Kind k);
int claimKind();                             // team 규약용 담당 종류

void buildHomeEarly();   /* HFI: 첫 턴 집 거리장 */
}  // namespace nb::mission
