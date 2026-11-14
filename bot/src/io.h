#pragma once
// io.h — 엔진 호출은 이 파일에서만. 다른 파일은 SDK 를 모른다.
#include <cstdint>
#include <vector>
#include "world.h"
#include "reference_profile.h"   /* DRF */

namespace nb::io {

extern Static S;

long long fuel();
int round();
int groupCaptureState(std::uint32_t handle);
int groupCaptureTimer(std::uint32_t handle);   // CSJ2: 남은 점령 시간 (실패 -1)   // CNT: 0 없음 · 1 아군 점령 중 · 2 경쟁 · 3 적 점령 중 · 4 점령 가능 (-1 실패)
int turnRound();   // FUELH: 이번 턴 감지 때 읽은 라운드 (엔진 호출 없음)
bool seesTile(Pos p);
bool seesTileQ(Pos p);   /* CSX */   // VB: 엔진 시야판 (이번 턴 처음 물을 때 한 번 만듦)
bool campBlocksMovement(Pos p);
std::uint64_t campPassabilityVersion();
struct NavigationStats {
    int changedTiles = 0, meBuilds = 0, goalBuilds = 0, queries = 0;
    int dynamicPicks = 0, deferred = 0, rescued = 0, goalReason = 0;
};
extern NavigationStats nav;
bool isDraft();
void endTurn();

// 첫 턴: 정적 지도 · 그룹 · 설계 · 핸들. 200k 예산 안 (측정값을 #nb init 줄로 남김)
void initStatic();
// 매 턴: 스냅숏 (엔진 호출 ≤ 12 + 가까운 캠프 수)
void sense(World& w);
extern unsigned g_fs3Gen;   /* FS_COM3: 감지 세대 (sense 마다 +1) */
extern unsigned g_fs2Gen;   /* FS_COM2: 디자인 세대 (loadDesigns 마다 +1) */

// 거리장: 정적 막힘 + 공통 몹 막힘. 움직이는 챔피언은 행동 단계에서 검사한다.
// compute 뒤 get 으로 읽는다 (읽기마다 엔진 호출 ~100). maxDist 0 = 제한 없음
bool prewarmWalls(long long minFuel);         // ZO: 정적 벽 비트 (한 번) — 만들었거나 이미 있으면 true
bool wallsReady();
void ensureBridges();   /* DSH */
void bridgeStep(long long floorFuel);   /* BRG2 */
bool bridgesReady();   /* BRG2 */
bool bridgesJustDone();   /* BRG2 */
bool fieldExport(const std::vector<Pos>& roots, RawVec<std::uint16_t>& out);   // GF: 정적 벽 + 모든 캠프 칸 막힘, 뿌리에서 거리 → 줄 순서 배열 (65535 = 닿지 못함)
bool territoryFieldExport(bool fromEnemy, RawVec<std::uint16_t>& out);   /* TM_CAMP: 스폰에서 보통 걷기 (고정 막힘 + 상대 스폰 막음, 대시 다리 · 유닛 · 캠프 칸 없음) */
int territorySymmetry();   /* TM_CAMP: 검증한 엔진 대칭 (0 가로 · 1 세로 · 2 회전), 아니면 -1 */
struct Field {
    bool ready = false; int round = -1; int tag = 0;   // tag: 임무 식별 (바뀌면 다시)
    bool ranCompute = false;  // 유효 root로 실제 SDK 계산을 호출했는지
    std::uint64_t campVersion = 0;
    Pos origin; int maxDistance = 0;  // 단일 root의 재사용 조건 (0=전체)
    long long lastCost[3] = {0, 0, 0};                 // 진단: 준비 · import · compute 연료
    bool compute(const World& w, const std::vector<Pos>& roots, int maxDist = 0);
    bool recomputeSame(const World& w, const std::vector<Pos>& roots, int maxDist);   /* FS_IO2: 바로 앞 compute 와 같은 입력이면 준비 · import 건너뜀 (아니면 compute) */
    bool from(Pos p, int r, int neededMax = 0) const {
        (void)r;  // 출발점과 장애물이 같으면 다른 라운드에도 재사용한다.
        return ready && origin == p && campVersion == campPassabilityVersion()
            && (maxDistance == 0 || (neededMax > 0 && maxDistance >= neededMax));
    }
    int get(Pos p) const;                              // 65535 = 닿지 못함, -1 = 준비 안 됨
    bool exportValues(RawVec<std::uint16_t>& out) const; // 한 선택에서 많은 칸을 확인할 때 한 번만 복사
    void* impl = nullptr;
    mutable int gmKey[16] = {}; mutable int gmVal[16] = {};   /* FS_IO2: get 칸 기억 (열쇠 = 칸 번호 + 1, 0 = 빔) */
    void gmClear() { for (int& k : gmKey) k = 0; }
};

// 행동 (true = 성공, false = 숨은 장애물 등으로 거절. 예외는 안에서 잡아 false)
bool moveTo(Pos p);
bool lastMoveHidden();   /* BH4_TN3: 마지막 moveTo 가 예외 없이 false (숨은 점유) 였나 */
bool attackChampion(std::uint32_t handle);
bool attackCamp(std::uint32_t handle);
bool castSelf(int slot);
bool castChampion(int slot, std::uint32_t handle);
bool castCamp(int slot, std::uint32_t handle);
bool castGround(int slot, Pos p);
bool submitGems(Pos altar);
bool recall();
extern int g_praD;   /* PRA: 걸어 둔 턴 끝에서 줄어든 남은 채널 (기본 1, 내 몸으로 배움) */
bool praFresh(const Unit& u);   /* PRA: 막 건 리콜 (남은 채널 >= recall_time - g_praD) */
void praMark(Unit& u);   /* PRA: 리콜 중이면 남은 채널 읽기, 막 건 리콜이면 채널 비트 지움 */
bool praArm();   /* PRA: 턴 끝 리콜 걸기 (엔진 상태로 거름) */
bool praCancel();   /* PRA: 걸어 둔 리콜 취소 */
int selfChannelLeft();   /* PRA: 내 남은 채널 (실패 -1) */
void refreshMeState(Me& m);   /* PRA: 취소 뒤 상태 비트 다시 읽기 */
void nearGroups(Pos p, int r, std::vector<int>& out);   /* SNS */
bool cancelSubmit();   /* CHC */
bool buyShard(int itemType, int count);
bool placeWard(Pos p);
bool tryPlaceWard(Pos p);
bool placeLocalVisionWard(Pos from);
bool livePosition(Pos& p);
bool rawStep(Pos me);   /* R1M */
void setPriority(int p);
int priorityNow(std::uint32_t handle);   /* TIE: 이번 라운드 priority 조회 */
void updatePriority(const World& w);   /* PRI: 팀원 bot25 규칙 7 (priority_policy.h) */
int consumableCount(int rawId);            // LK: 들고 있는 소모품 수 (0 순간이동 수정 · 2 제단 잠금)
int consumableCost(int rawId);
int tpChannel();   /* TPS: 엔진 설정 tp_crystal_time */
int recallLen();   /* BH5_IT5: 내 몸 리콜 길이 (home 엠블럼 3, 아니면 설정 recall_time) */
bool buyConsumable(int rawId);
bool useConsumable(int rawId, Pos p);
struct LockSeen { Pos pos; bool ally; int hits; };
void altarLocks(std::vector<LockSeen>& out); // 보이는 제단 잠금
bool attackEnemyLock(Pos altar);   // LK2: 이 제단의 적 잠금을 평타로
int  itemCount(int itemType);
int  itemShardsOf(std::uint32_t handle);   /* TRP: 샤드 환산 (엠블럼 5) */
extern int g_wantGroup;   /* SNRM: 임무 무리 */
extern int g_respEv, g_respEvR;   /* TERR T2: 이번에 새로 본 되살아남 무리 · 라운드 */

// 공유 메모리 8바이트
void readShared(std::uint8_t out[8]);
void writeShared(const std::uint8_t in[8]);

// 드래프트
int submitted(bool ally);                  // FX: 팀이 바친 젬
std::vector<int> draftPicked(bool ally);
int draftTeamSize();   /* TSD */
void draftPick(int champ);
void draftWrite(const std::uint8_t* bytes, int n);
void draftRead(std::uint8_t* out, int n); void opdPrep(); void opdAssign(const std::vector<int>& types, std::uint8_t* out32);   /* OPD */
const reference::Plan& referencePlan();   /* DRF: 팀원 bot25 규칙 1 (맵 특징 → 픽 순서) */
bool championLive(std::uint32_t handle, Unit& out);   /* TWP */
bool carriedGems(std::uint32_t handle, int& gems);   /* TWP */
bool championIdentity(std::uint32_t handle, int& type);   /* TWP */
bool opdCell(Pos& c);   /* BH2_OP4: 드래프트 OPD 계획의 내 종류 첫 무리 중심 (없으면 false) */
bool championAlive(std::uint32_t handle, bool& alive);   /* TWP */
double mapGemShare();   /* DRF2: 캠프 배치의 젬 비중 (젬/재생) / (젬/재생 + 금/재생/30), 못 읽으면 -1 */

const char* lastError();
void debugGroup(int gi, Pos me);   // 진단 (기록 빌드): 그룹의 스폰 칸 시야 · 상태 코드 · 캠프 질문 코드

}  // namespace nb::io
