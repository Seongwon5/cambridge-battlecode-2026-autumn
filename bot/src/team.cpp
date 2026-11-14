#include <algorithm>
#include "team.h"
#include "io.h"
#include "reference_config.h"   /* TWP */
#include "gem_report.h"   /* TWP */

namespace nb::team {

View T;
bool richGemMode(){   /* TWP 규칙 8: 팀원은 드래프트 계획에 Prism (드래프트 5). 여기는 우리 몸 중 champion_type 6 (정체 API, 안개 무관) */
    static int mode=-1;if(mode>=0)return mode==1;
    if(io::S.allyHandle.empty())return false;
    mode=0;for(auto h:io::S.allyHandle){int type=-1;if(io::championIdentity(h,type)&&type==reference::PRISM)mode=1;}
    return mode==1;
}

int zoneSide() { const int m = std::max(io::S.W, io::S.H); return std::max(1, (m + 7) / 8); }
int zoneOf(Pos p) { const int s = zoneSide(); const int zx = std::clamp(p.x / s, 0, 7), zy = std::clamp(p.y / s, 0, 7); return zy * 8 + zx; }
Pos zoneCenter(int z) { const int s = zoneSide(); return {std::min(io::S.W - 1, (z % 8) * s + s / 2), std::min(io::S.H - 1, (z / 8) * s + s / 2)}; }

void init() {
    std::vector<std::uint32_t> h = io::S.allyHandle;
    std::sort(h.begin(), h.end());
    T.n = std::min((int)h.size(), MAX); T.slot = -1;
    const std::uint32_t mine = io::S.allyHandle.empty() ? 0 : 0;
    (void)mine;
    // 내 핸들: sense 가 채운 뒤에 알 수 있으므로 read 에서 맞춘다
}
static void ensureSlot(std::uint32_t myHandle) {
    if (T.slot >= 0) return;
    std::vector<std::uint32_t> h = io::S.allyHandle; std::sort(h.begin(), h.end());
    T.n = std::min((int)h.size(), MAX);
    for (int k = 0; k < T.n; ++k) if (h[k] == myHandle) T.slot = k;
}
static inline std::uint64_t wordOf(const std::uint8_t* b) { std::uint64_t w = 0; for (int i = 7; i >= 0; --i) w = (w << 8) | b[i]; return w; }
static inline void bytesOf(std::uint64_t w, std::uint8_t* b) { for (int i = 0; i < 8; ++i) b[i] = (std::uint8_t)(w >> (8 * i)); }

void read(const World& w) {
    ensureSlot(w.me.handle);
    if (T.slot < 0) return;
    const std::uint64_t word = wordOf(w.shared);
    const bool rich=richGemMode();   /* TWP */
    T.terrEvN = 0;   /* TERR T2 */
    for (int k = 0; k < T.n; ++k) T.prevMate[k] = T.mate[k];   /* TFPC2 */
    for (int k = 0; k < T.n; ++k) {
        Mate& m = T.mate[k];
        // 만료
        if (w.round - m.zoneRound > LIFE_STATUS) m.known = false;
        if (w.round - m.claimRound > LIFE_CLAIM) { m.claimGroup = -1; m.claimKind = 0; }
        if (w.round - m.outRound > LIFE_CLAIM) m.outReason = 0;
        if (w.round - m.helpRound > LIFE_CLAIM) m.helpAltar = -1;   // ES
        if (k == T.slot) continue;
        const int cell = (int)((word >> (CELL_BITS * k)) & ((1u << CELL_BITS) - 1));
        if (cell == 0) {m.lastReadCell=0;continue;}   // 빈 칸 (아직 안 씀)
        if (policy::TERR_ON && kindOf(cell) == K_OUT && (cell & 3) == 0 && ((cell >> 2) & 63) != 0) { if (cell != m.lastReadCell && T.terrEvN < MAX) T.terrEv[T.terrEvN++] = ((cell >> 2) & 63) - 1; m.lastReadCell = cell; continue; }   /* TERR T2: 되살아남 사건 칸 (다른 기억은 그대로) */
        if (policy::BH_T2_ON && kindOf(cell) != K_OUT && m.outReason == 3) m.outReason = 0;   /* BH_T2 */
        switch (kindOf(cell)) {
        case K_STATUS: m.zone = (cell >> 1) & (policy::BUG3_ON ? 63 : 127); m.enemySeen = cell & 1; m.zoneRound = w.round; m.known = true; break;
        case K_CLAIM: m.claimGroup = (cell >> 2) & 63; m.claimKind = cell & 3; m.claimRound = w.round; if (m.claimGroup == 63) m.claimGroup = -1; break;
        case K_OUT: m.outReason = cell & 3; m.outRound = w.round; m.claimGroup = -1;if(rich&&m.outReason==3){m.gemsRep=0;m.gemsRound=w.round;} break;
        case K_HELP: if ((cell & 3) == 2 || (rich&&(cell&3)==0)) {
            if(!rich){m.gemsRep=((cell>>2)&63)*4;m.gemsRound=w.round;}
            else if(cell!=m.lastReadCell){m.gemsRep=gem_report::unpack((cell>>2)&63);m.gemsRound=gem_report::reportRound(w.round,cell&3);}
        } else if ((cell & 3) == 3) { m.fightZone = (cell >> 2) & 63; m.fightRound = w.round; } else { m.helpAltar = (cell >> 2) & 63; m.helpRound = w.round; } break;   /* TWP 규칙 8 */   /* PL: 하위 3 = 싸움 알림 */   // ES · ES2
        default: break;
        }
        m.lastReadCell=cell;   /* TWP */
    }
}

void write(const World& w, int claimGroup, int claimKind, int outReason, int helpAltar, int gems, int fightZone) {
    ensureSlot(w.me.handle);
    if (T.slot < 0) return;
    int cell;
    const bool statusTurn = (w.round % 3) == 0, gemsTurn = (w.round % 3) == 2 && gems >= 0;   // ES2: 3R 주기
    if((policy::BH_T2_ON||richGemMode())&&outReason==3)cell=encodeOut(3);   /* BH_T2 */
    else if (policy::AWJ_ON && outReason == 0 && claimGroup >= 0 && claimGroup < 63 && (claimKind == CL_FARM || claimKind == CL_CAPTURE) && (T.selfSeen.claimGroup != claimGroup || T.selfSeen.claimKind != claimKind)) cell = encodeClaim(claimGroup, claimKind);   /* AWJ: 새 담당은 바로 알림 (3R 주기 늦음으로 둘이 같은 무리를 고름) */
    else if (policy::TERR_ON && io::g_respEv >= 0 && io::g_respEv < 62 && io::g_respEvR >= w.round - 1) { cell = (K_OUT << 8) | (((io::g_respEv + 1) & 63) << 2); io::g_respEv = -1; }   /* TERR T2: 사건 한 번 (이유 0 = 기존 해석과 안 부딪힘) */
    else if (gemsTurn) {
        if(richGemMode()){int current=0;(void)io::carriedGems(w.me.handle,current);cell=(K_HELP<<8)|(gem_report::pack(current)<<2)|gem_report::subtype(w.round);}
        else cell=encodeGems(gems);
    }   // ES2
    else if (outReason > 0 && !statusTurn) cell = encodeOut(outReason);
    else if (fightZone >= 0 && !statusTurn) cell = encodeFight(fightZone);   // PL
    else if (helpAltar >= 0 && !statusTurn) cell = encodeHelp(helpAltar);   // ES
    else if (claimGroup >= 0 && claimGroup < 63 && !statusTurn) cell = encodeClaim(claimGroup, claimKind);
    else cell = encodeStatus(zoneOf(w.me.pos), w.enemyNear8);
    // 내 기억도 갱신 (planner 가 자기 칸을 남과 같은 눈으로 보도록)
    Mate& me = T.mate[T.slot]; me.zone = zoneOf(w.me.pos); me.zoneRound = w.round; me.known = true;
    me.claimGroup = claimGroup; me.claimKind = claimKind; me.claimRound = w.round;
    {   /* TFPC2: 내가 쓴 칸을 남처럼 풀어 selfSeen 에 */
        Mate& m = T.selfSeen;
        if (policy::BH_T2_ON && kindOf(cell) != K_OUT && m.outReason == 3) m.outReason = 0;   /* BH_T2 */
        switch (kindOf(cell)) {
        case K_STATUS: m.zone = (cell >> 1) & (policy::BUG3_ON ? 63 : 127); m.enemySeen = cell & 1; m.zoneRound = w.round; m.known = true; break;
        case K_CLAIM: m.claimGroup = (cell >> 2) & 63; m.claimKind = cell & 3; m.claimRound = w.round; if (m.claimGroup == 63) m.claimGroup = -1; break;
        case K_OUT: if (policy::TERR_ON && (cell & 3) == 0 && ((cell >> 2) & 63) != 0) break; m.outReason = cell & 3; m.outRound = w.round; m.claimGroup = -1; break;   /* TERR T2: 사건 칸은 selfSeen 안 바꿈 */
        default: break; }
        if (w.round - m.claimRound > LIFE_CLAIM) { m.claimGroup = -1; m.claimKind = 0; }
        if (w.round - m.outRound > LIFE_CLAIM) m.outReason = 0;
        if (w.round - m.zoneRound > LIFE_STATUS) m.known = false; }
    std::uint8_t b[8]; io::readShared(b);
    std::uint64_t word = wordOf(b);
    const std::uint64_t mask = ((1ull << CELL_BITS) - 1) << (CELL_BITS * T.slot);
    word = (word & ~mask) | ((std::uint64_t)cell << (CELL_BITS * T.slot));
    bytesOf(word, b); io::writeShared(b);
    for (int i = 0; i < 8; ++i) T.lastWritten[i] = b[i];
}

int teamGems(const World& w, int myGems) {   // ES2
    int s = myGems; for (int k = 0; k < T.n; ++k) { if (k == T.slot) continue; const Mate& m = T.mate[k]; if (w.round - m.gemsRound <= LIFE_CLAIM) s += m.gemsRep; }
    return s;
}
int claimsOn(int group, int kindMask) {
    int n = 0;
    for (int k = 0; k < T.n; ++k) { if (k == T.slot) continue; const Mate& m = T.mate[k]; if (m.claimGroup == group && ((kindMask >> m.claimKind) & 1)) ++n; }
    return n;
}

const Mate& planView(int k, const World& w) {   /* TFPC2 */
    if (k == T.slot) return T.selfSeen;
    std::vector<std::uint32_t> hs = io::S.allyHandle; std::sort(hs.begin(), hs.end());
    if (k >= 0 && (size_t)k < hs.size()) for (const auto& a : w.allies) if (a.handle == hs[(size_t)k]) { if (a.acted) return T.prevMate[k]; break; }
    return T.mate[k];
}
}  // namespace nb::team
