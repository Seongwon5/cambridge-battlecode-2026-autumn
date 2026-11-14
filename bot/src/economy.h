#pragma once
// economy.h — 바치기 시작 · 리콜 · 상점 · 와드. 임무의 보조 결정 (행동 조합과 충돌하지 않게 순서를 정한다).
#include "world.h"
#include "mission.h"
#include "combat.h"

namespace nb::economy {

// 이동 전에 할 일: 제단 곁이면 바치기 시작 (이동 없이) → true 면 이번 턴 끝
bool tryTribute(const World& w, const mission::Mission& M, combat::Plan& p);
// 회복 임무이고 안전하면 리콜 시작 → true 면 이번 턴 끝
bool tryRecall(const World& w, const mission::Mission& M, const combat::Plan& p);
bool recallWanted(const World& w, const mission::Mission& M, const combat::Plan& p);   /* PRA: tryRecall 의 결정만 (리콜 걸기 뺌) */
// 스폰 (또는 Tavi 어디서나) 에서 금을 쓴다. 죽어 있을 때도.
void shop(const World& w);
extern int g_chcStart, g_chcDur;   /* CHC */
float chcDanger(const World& w, int rounds);   /* CHC */
void lockAltar(const World& w);   // LK
int lastSpent();   // SB: 마지막 shop 에서 쓴 금
// 와드: 길에 서 있을 때 충전이 있으면 내 칸 곁에 하나 (돌아다니지 않음)
bool combatWard(const World& w);
void ward(const World& w, const combat::Plan& p);

}  // namespace nb::economy
