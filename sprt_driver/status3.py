"""status3.py — 드라이버 3 상태 화면. 30초마다 다시 그린다. Ctrl+C 로 끈다 (드라이버는 계속 돈다)."""
import os, sys, json, time, shutil, pathlib, subprocess, datetime
ROOT = pathlib.Path(os.environ.get('CAMBC_ROOT', pathlib.Path(__file__).resolve().parents[1]))   # 저장소 뿌리 (cambc_ws · notes · submissions 가 있는 곳)

R = (ROOT / 'notes/ruler')
C = os.environ.get('CAMBC', 'cambc')
W = str(ROOT / 'cambc_ws')
LAD = {'t': 0, 'txt': '(래더 조회 전)'}


def rd(n, d=''):
    p = R / n
    try:
        return p.read_text(encoding='utf-8').strip() if p.exists() else d
    except Exception:
        return d


def ladder():
    if time.time() - LAD['t'] < 600:
        return LAD['txt']
    LAD['t'] = time.time()
    try:
        for page in (1, 2, 3, 4):
            r = subprocess.run([C, 'ladder', '--json', '--page', str(page)], cwd=W, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
            rows = next(v for v in json.loads(r.stdout).values() if isinstance(v, list))
            me = [x for x in rows if x.get('name') == 'SkuBap']
            if me:
                LAD['txt'] = f"래더 {me[0]['rank']}위 · 레이팅 {me[0]['rating']:.0f} · 판 {me[0].get('matches')}"
                break
        lst = subprocess.run([C, 'submissions', 'list'], cwd=W, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60).stdout.splitlines()
        live = next((x for x in lst if x.startswith('Live')), '')
        LAD['txt'] += ' · ' + live.strip()
    except Exception as e:
        LAD['txt'] = '래더 조회 실패: ' + str(e)[:60]
    return LAD['txt']


TMP = (ROOT / 'tmp' / 'sprt_tmp')


def live_matches():
    """지금 돌고 있는 롤 (sprt_tmp 아래 가장 최근 폴더) 의 경기 기록을 읽어 진행 · 승수 · 최근 결과를 보인다."""
    import re
    try:
        ds = [d for d in TMP.iterdir() if d.is_dir() and (d / 'man.txt').exists()]
    except Exception:
        ds = []
    if not ds:
        print('지금 경기: (없음)'); return
    d = max(ds, key=lambda p: p.stat().st_mtime)
    man = (d / 'man.txt').read_text(encoding='utf-8').splitlines()
    names = set()
    for l in man[:2]:
        parts = l.split()
        if len(parts) >= 2:
            names.add(pathlib.Path(parts[0]).name); names.add(pathlib.Path(parts[1]).name)
    log = (d / 'run.log').read_text(encoding='utf-8', errors='replace') if (d / 'run.log').exists() else ''
    res = []
    for line in log.splitlines():
        m = re.search(r'TEAM ([AB]) WIN \(([^)]+)\)\s+(\S+) vs (\S+)\s+(\S+)\s+Round (\d+)', line)
        if m:
            res.append((m.group(2), m.group(3), m.group(4), m.group(5), m.group(6)))
    st_ = {}
    try:
        st_ = json.loads(rd('state.json', '{}'))
    except Exception:
        pass
    left = f"후보({st_.get('cand', '?')})" if not d.name.startswith('o') else '왼쪽 봇'
    lab = lambda n: (left if n.endswith('c') else ('상대' if d.name.startswith('o') else '기준(' + rd('base.txt') + ')'))
    if d.name.startswith('o'):
        print(f'  (2단계: 폴더 이름 o<봇><상대>_<롤>. c = 시험 중인 봇, b = 고정 상대)')
    res = [(lab(a), lab(b), lab(c), m, rr) for a, b, c, m, rr in res]
    tally = {}
    for r in res:
        tally[r[0]] = tally.get(r[0], 0) + 1
    print(f'지금 경기: 롤 폴더 {d.name}   진행 {len(res)}/{len(man)}판   ' + ' · '.join(f'{k} {v}승' for k, v in sorted(tally.items(), key=lambda kv: -kv[1])))
    for r in res[-6:]:
        print(f'    {r[3]:24s} {r[1]} vs {r[2]} → 승 {r[0]} (R{r[4]})')
    c2 = list(R.glob('stage2_*.json'))
    if c2:
        try:
            print('  2단계 기준 봇 결과 (상대별 2롤 합, 256판 환산 승-절반):', json.loads(c2[0].read_text(encoding='utf-8')))
        except Exception:
            pass


def draw():
    os.system('cls' if os.name == 'nt' else 'clear')
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    st = {}
    try:
        st = json.loads(rd('state.json', '{}'))
    except Exception:
        pass
    done = set(rd('done.txt').split())
    qu = [l.split() for l in rd('queue_user.txt').splitlines() if l.strip() and not l.startswith('#')]
    qa = [l.split() for l in rd('queue.txt').splitlines() if l.strip() and not l.startswith('#')]
    qu = [x for x in qu if x[0] not in done]; qa = [x for x in qa if x[0] not in done]
    free = shutil.disk_usage('C:/').free / 1e9
    stop = ' · STOP 파일 있음' if (R / 'STOP').exists() else ''
    print(f'=== 드라이버 3  ({now}, 30초마다 갱신){stop}')
    print(f'기준 봇: {rd("base.txt")}    디스크 여유 {free:.0f}GB    {ladder()}')
    jl = rd('janitor.log').splitlines()
    if jl:
        print(f'청소: {jl[-1]}')
    print('-' * 100)
    if st.get('cand'):
        xs = st.get('xs', [])
        print(f"지금: {st.get('cand')} ({st.get('typ')} {st.get('spec')}, {st.get('cmode')})  단계 {st.get('stage')}  맵 {st.get('maps', '')}")
        print(f"  롤 {st.get('rolls_done', 0)}개 끝 · 다음 롤 #{st.get('roll', '')} · LLR {st.get('llr', 0)} (채택 ≥ {st.get('A', '')} / 기각 ≤ {st.get('B', '')}) · mu1 {st.get('mu1', '')}")
        print(f"  롤별 x (승-절반, 256판 환산): {xs}")
        print(f"  상태 기록 시각 {st.get('updated', '')}")
    else:
        print('지금: (후보 없음)')
    live_matches()
    print('-' * 100)
    print(f'큐: 사용자 {len(qu)}개 · 자동 {len(qa)}개')
    for x in (qu + qa)[:6]:
        print('   ', ' '.join(x), '(사용자)' if x in qu else '')
    print('-' * 100)
    print('최근 사건:')
    for l in rd('events.log').splitlines()[-12:]:
        print('   ', l[:150])
    print('-' * 100)
    print('채택 기록:')
    for l in rd('adopted.txt').splitlines()[-6:]:
        print('   ', l[:150])


if __name__ == '__main__':
    while True:
        try:
            draw()
        except Exception as e:
            print('화면 오류:', e)
        time.sleep(30)
