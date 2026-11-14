"""DPA: 프리즘 조합 (5 Prism · 6 Marcella · 3 쌍둥이 · 2 Vendetta · 0 Ryth) 을 모든 맵에.
근거 (래더 11-13 10:10~15:20, v663 160판): 프리즘 조합을 쓴 판 26승 10패 (72%), 표준 조합 (1 · 2 · 11 …) 46승 79패 (37%).
지금: 젬/금 비 > 0.3337 인 맵만 프리즘 조합 (팀원 결정 나무 reference::select). 바꿈: 항상 프리즘 조합. 인자: src 경로."""
import sys, pathlib
S = pathlib.Path(sys.argv[1]); CR = chr(13) + chr(10); NL = chr(10)
def edit(fn, pairs):
    p = S / fn; t = p.read_bytes().decode('utf-8'); crlf = CR in t; t = t.replace(CR, NL)
    for a, b, n in pairs:
        assert t.count(a) == n, (fn, t.count(a), a[:70]); t = t.replace(a, b)
    if crlf: t = t.replace(NL, CR)
    p.write_bytes(t.encode('utf-8'))
edit('reference_profile.h', [('    const bool prism=(f.gemGoldRatio <= 0.333699612f ? false : true);', '    const bool prism=true;   /* DPA */', 1)])
print('ok DPA')
