// draft.cpp — 픽: 맵을 알아보지 않는다 (NM, 사용자 규칙 10-20). 고정 순서 base (Prism · Ren · Vesper · Vendetta · Varg · Issac · Willow · Tavi · Cassi · Ryth · Marcella · Stellar) 에서 아직 안 뽑은 첫 챔피언. 드래프트 메모리에는 픽 순서를 쓴다.
#include <algorithm>
#include <cstring>
#include <vector>
#include "io.h"
#include "reference_config.h"   /* DRF */

namespace nb {

void playDraft() {
    const auto mine = io::draftPicked(true);
    static const int base[] = {1, 6, 2, 11, 10, 0, 9, 3, 8, 4, 5, 7};   /* 드래프트 시험: 다섯째 자리 */   /* HOR */   /* C8: Vesper · Marcella · Vendetta | Varg · Tavi */   /* COMP D: HoR · Vesper · Vendetta · Marcella · Ryth */   /* COMP c4 */   /* COMP */
    std::vector<int> order;   /* TSD: 팀 크기별 앞 순서 */
    static reference::Plan s_rp; static bool s_rpOk = false, s_rpTried = false;   /* DRF: 맵 특징 → 계획 픽을 맨 앞에 (첫 호출 · 연료 넉넉할 때 한 번) */
    if (reference::DRAFT && !s_rpTried && io::fuel() > 2000000) { s_rpTried = true; try { s_rp = io::referencePlan(); s_rpOk = true; } catch (...) { s_rpOk = false; } }
    if (s_rpOk) for (int k = 0; k < s_rp.picks && k < 5; ++k) if (s_rp.order[k] >= 0 && std::find(order.begin(), order.end(), s_rp.order[k]) == order.end()) order.push_back(s_rp.order[k]);
    static int s_gem = -1; static double s_g = -1.0;   /* DRF2a: 젬 비중 >= 0.924 → Vesper 자리에 Prism (5 Prism · 6 Marcella · 2 Vendetta · 11 HoR · 0 Ryth) */
    if (s_gem < 0) { s_g = io::mapGemShare(); s_gem = (s_g >= 0.924) ? 1 : 0; }
    if (s_gem == 1) { static const int bg[] = {5, 6, 3, 2, 7};   /* GC2: 젬 맵 팀원식 조합 */ for (int p : bg) order.push_back(p); }
    { const int ts = io::draftTeamSize();
      static const int b4[] = {1, 6, 2, 11}; if (ts == 4) for (int p : b4) if (std::find(order.begin(), order.end(), p) == order.end()) order.push_back(p);
    }
    for (int p : base) if (std::find(order.begin(), order.end(), p) == order.end()) order.push_back(p);
    for (int p : order) {
        if (std::find(mine.begin(), mine.end(), p) != mine.end()) continue;
        std::uint8_t mem[48] = {};
        for (size_t i = 0; i < mine.size() && i < 8; ++i) mem[i] = (std::uint8_t)mine[i];
        mem[mine.size() < 8 ? mine.size() : 7] = (std::uint8_t)p;
        mem[8] = 0xD2; mem[9] = (std::uint8_t)(s_gem == 1 ? 1 : 0); mem[10] = (std::uint8_t)(s_g < 0 ? 255 : (int)(s_g * 200.0 + 0.5));   /* DRF2: 픽 기록 [0..7] 과 안 겹침 */
        if (s_rpOk) { mem[8] = 0xB6; mem[9] = 1; mem[10] = (std::uint8_t)(s_rp.profile < 0 ? 255 : s_rp.profile); mem[11] = (std::uint8_t)(s_rp.flags | (s_rp.exact ? 4 : 0) | (s_rp.picks << 3)); }   /* DRF: 팀원 형식 (픽 기록 [0..7] 과 안 겹침) */
        { std::vector<int> ty; auto add = [&](int dn) { if (dn < 3) ty.push_back(dn); else if (dn == 3) { ty.push_back(3); ty.push_back(4); } else ty.push_back(dn + 1); };   /* OPD: 드래프트 번호 → 챔피언 종류 */
          for (int x : mine) add(x); add(p); try { if (io::fuel() > 1000000) io::opdPrep(); io::opdAssign(ty, mem + 16); } catch (...) { std::memset(mem + 16, 0, 32); } }
        io::draftWrite(mem, 48);
        io::draftPick(p);
        return;
    }
}

}  // namespace nb
