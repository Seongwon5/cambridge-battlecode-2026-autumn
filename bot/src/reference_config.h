#pragma once
// TWP (팀원 bot25 규칙 6 Willow · 3·4 Prism · 8 넓은 젬 보고 · 2 아이템): twp_patch.py 스위치 gemrep,prism,willow
#include "reference_profile.h"   // PRISM=6 · WILLOW=8 (champion_type)
#define TWP_GEMREP 1
#define TWP_HAS_PLAN 0
namespace nb::reference {
inline constexpr bool DRAFT=true;   /* DRF */
inline constexpr bool PRISM_MACRO=true,WILLOW_MACRO=true,TWIN_MACRO=false;
inline constexpr bool GEMREP=true,ITEMS_ALL=false;
inline constexpr bool PRISM_ITEMS=PRISM_MACRO,WILLOW_ITEMS=WILLOW_MACRO;   // items 없이도 그 챔피언 아이템은 팀원 순서
inline constexpr float PRISM_GEM_VALUE=2.0f;
inline constexpr bool PRISM_CAPTURE=true;
}
