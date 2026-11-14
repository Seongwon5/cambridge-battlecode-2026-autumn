#pragma once
// team.h — 8바이트 공유 메모리 규약 (몸 ≤ 6 → 칸 10비트). 칸 = [종류 2][내용 8].
//   종류 0 상태: 구역 7 (8×8 격자) · 적 보임 1          — 2턴에 한 번 이상
//   종류 1 담당: 그룹 6 · 담당 종류 2 (1 농사 · 2 점령 · 3 바치기)
//   종류 2 도움: 그룹 6 · 필요 인원 2                   (2단계)
//   종류 3 참여 불가: 이유 2 (1 회복 · 2 상점 · 3 죽음)
// 만료: 담당 6R · 상태 6R. 자기 칸 번호 = 아군 핸들 정렬 순서.
#include <cstdint>
#include "world.h"
#include "policy.h"

namespace nb::team {

constexpr int MAX = 6, CELL_BITS = 10;
constexpr int K_STATUS = 0, K_CLAIM = 1, K_HELP = 2, K_OUT = 3;
constexpr int CL_FARM = 1, CL_CAPTURE = 2, CL_TRIBUTE = 3;
constexpr int LIFE_CLAIM = 6, LIFE_STATUS = 6;

struct Mate {
    bool known = false;        // 상태를 받은 적 있음 (수명 안)
    int zone = -1, zoneRound = -1000; bool enemySeen = false;
    int claimGroup = -1, claimKind = 0, claimRound = -1000;
    int outReason = 0, outRound = -1000;
    int helpAltar = -1, helpRound = -1000;   // ES: 도움 요청 제단
    int lastReadCell = -1;   /* TWP 규칙 8 */
    int gemsRep = 0, gemsRound = -1000;      // ES2: 보고된 든 젬
    int fightZone = -1, fightRound = -1000;  // PL: 싸움 알림 구역
};
struct View {
    int n = 0, slot = -1;
    Mate mate[MAX];
    std::uint8_t lastWritten[8] = {};
    int terrEv[MAX] = {}; int terrEvN = 0;   /* TERR T2: 이번 읽기에서 받은 되살아남 사건 (무리 번호) */
    Mate prevMate[MAX]; Mate selfSeen;   // TFPC2: 지난번 읽은 값 · 내가 쓴 칸을 남처럼 푼 값
};
extern View T;

int zoneSide();                      // 구역 한 변 (칸)
int zoneOf(Pos p);
Pos zoneCenter(int z);

void init();                         // 칸 번호 (첫 턴)
void read(const World& w);
const Mate& planView(int k, const World& w);   // TFPC2: 지난 라운드 끝 기준 팀원 보기 (모든 몸 같은 값)           // 공유 8바이트 → T.mate (만료 적용)
// 내 칸 쓰기. claimGroup < 0 = 담당 없음. outReason > 0 = 참여 불가
void write(const World& w, int claimGroup, int claimKind, int outReason, int helpAltar = -1, int gems = -1, int fightZone = -1);   // PL: fightZone ≥ 0 = 싸움 알림   // ES2: gems ≥ 0 = 젬 보고
int teamGems(const World& w, int myGems);
bool richGemMode();   /* TWP 규칙 8: Prism 이 있는 팀은 나이 표시 · 넓은 범위 젬 보고 */   // ES2   // ES: helpAltar ≥ 0 = 도움 요청
int claimsOn(int group, int kindMask = 0xF);   // 다른 몸의 유효 담당 수

// 순수 부호 (자체 시험용)
constexpr int encodeStatus(int zone, bool enemySeen) { return (K_STATUS << 8) | (((policy::BUG3_ON ? ((zone & 63) | 64) : (zone & 127))) << 1) | (enemySeen ? 1 : 0); }   // BUG3: 구역 0 도 0 이 아니게 (비트 7 = 늘 1)
constexpr int encodeClaim(int group, int kind) { return (K_CLAIM << 8) | ((group & 63) << 2) | (kind & 3); }
constexpr int encodeHelp(int altar) { return (K_HELP << 8) | ((altar & 63) << 2) | 1; }   // ES
constexpr int encodeGems(int gems) { return (K_HELP << 8) | (((gems / 4 > 63 ? 63 : gems / 4) & 63) << 2) | 2; }   // ES2
constexpr int encodeFight(int zone) { return (K_HELP << 8) | ((zone & 63) << 2) | 3; }   // PL
constexpr int encodeOut(int reason) { return (K_OUT << 8) | (reason & 3); }
constexpr int kindOf(int cell) { return (cell >> 8) & 3; }

}  // namespace nb::team
