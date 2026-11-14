"""NDI: 근접 몸이 지금 자리에서 이미 적 챔피언을 칠 수 있으면, 내 다음 차례 전에 나에게 닿는 적 수가 지금 자리보다 늘어나는 칸은 고르지 않는다.
근거 (1등 장부 11-13, notes/damage_model_1113.md 10절): 붙은 거리 (1~2칸) 에서 더 다가감 1등 13% · 우리 18%, 근접 대 원거리 16% · 33%; 들어온 상대의 10R 안 죽음 7.1% (우리에게는 2.1%).
닿는지는 지금 있는 위협 예보 (thDmg > 0) 만 쓴다. 문턱 없음.
인자: src 경로."""
import sys, pathlib
S = pathlib.Path(sys.argv[1]); CR = chr(13) + chr(10); NL = chr(10)
p = S / 'combat.cpp'; t = p.read_bytes().decode('utf-8'); crlf = CR in t; t = t.replace(CR, NL)
A = '    lzdCF0 = io::fuel();   /* LZD2: 조합 시작 연료 */' + NL
assert t.count(A) == 1
t = t.replace(A, A +
  '    auto ndiCnt = [&](Pos q) { int n = 0; for (const auto& t9 : threats) if (thDmg(t9, cheb(t9.pos, q)) > 0.f) ++n; return n; };   /* NDI */' + NL +
  '    bool ndiOn = false; if (!ranged && me.canAttack) for (const auto& e : w.enemies) if (e.alive && e.visible && !onEnemySpawn(e.pos) && cheb(e.pos, me.pos) <= std::max(1, me.range)) { ndiOn = true; break; }' + NL +
  '    const int ndi0 = ndiOn ? ndiCnt(me.pos) : 0;' + NL)
B = '        const Cell& c = cells[ci];' + NL + '        if (ranged && !threats.empty() && c.dash && c.dmg >= rdwWalkMin - 0.5f) {'
assert t.count(B) == 1
t = t.replace(B, '        const Cell& c = cells[ci];' + NL + '        if (ndiOn && c.p != me.pos && ndiCnt(c.p) > ndi0) continue;   /* NDI: 이미 칠 수 있으면 닿는 적이 느는 칸으로 안 들어감 */' + NL + '        if (ranged && !threats.empty() && c.dash && c.dmg >= rdwWalkMin - 0.5f) {')
if crlf: t = t.replace(NL, CR)
p.write_bytes(t.encode('utf-8'))
print('ok NDI')
