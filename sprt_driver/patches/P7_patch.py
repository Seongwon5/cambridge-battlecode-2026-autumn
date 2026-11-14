"""P7: 프리즘 5픽 조합의 다섯째 0 을 7 로 (5 · 6 · 3 · 2 · 7). 근거 (래더 11-13): 우리 7 든 조합 8승 2패; 상위 상위 팀 · 상위 팀 · MFF1 · test4 · stellar 가 7 을 씀.
인자: src 경로."""
import sys, pathlib
S = pathlib.Path(sys.argv[1]); CR = chr(13) + chr(10); NL = chr(10)
p = S / 'reference_profile.h'; t = p.read_bytes().decode('utf-8'); crlf = CR in t; t = t.replace(CR, NL)
A = '    for(int i=p.picks;i<5;++i)p.order[i]=-1;return p;'
assert t.count(A) == 1
t = t.replace(A, '    if(prism&&p.picks>=5){for(int&x:p.order)if(x==0)x=7;}   /* P7 */' + NL + A)
if crlf: t = t.replace(NL, CR)
p.write_bytes(t.encode('utf-8'))
print('ok P7')
