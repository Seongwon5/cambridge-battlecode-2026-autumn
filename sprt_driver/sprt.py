"""sprt.py — 지침서 v2.2 4장 '4단계 판정' 의 1·4단계 (롤 단위 짝 SPRT).

쓰는 법:
  python -X utf8 sprt.py CAND BASE [--mode gain|noninf] [--mu1 6] [--sigma 6] [--alpha 0.05] [--beta 0.10]
                         [--maps dev|confirm|all] [--start 1] [--max 16] [--tag 이름]
  - 롤 = 맵 묶음 전체 x 두 자리. 롤 i 마다 두 봇에 같은 재굴림 키 (reroll_iters(i) = 1+(37i mod 211) 번 빈 반복) 를 넣은 사본을 만든다.
  - x = (후보 승 - 판수/2) 를 256판 기준으로 환산한 값. gain: H0 mu=0, H1 mu=mu1. noninf: H0 mu=-mu1, H1 mu=0.
  - 매 롤 결과는 notes/ruler/ledger.csv 에 한 줄 (done=1) 로 쌓인다. 마지막 줄에 판정 JSON 을 출력한다.
얼린 설정 (2026-11-11): sigma 6, mu1 6, alpha 5%, beta 10%, 최대 16롤, 맵 128개 = 이름 정렬 후 4번째마다 confirm (32), 나머지 dev (96).
롤 번호 구간: 발견 (dev) 1~499, 확인 (confirm) 1000~.
"""
import sys, os, re, json, math, shutil, pathlib, subprocess, argparse, datetime, hashlib
ROOT = pathlib.Path(os.environ.get('CAMBC_ROOT', pathlib.Path(__file__).resolve().parents[1]))   # 저장소 뿌리 (cambc_ws · notes · submissions 가 있는 곳)

W = (ROOT / 'cambc_ws')
B = W / 'bots'
POOL = W / 'maps_pool128'
LEDGER = (ROOT / 'notes/ruler/ledger.csv')
TMP = (ROOT / 'tmp' / 'sprt_tmp')
CAMBC = os.environ.get('CAMBC', 'cambc')
KEY = '    const long long fS = io::fuel();\n'


def maps_for(which):
    ms = sorted(p.stem for p in POOL.glob('*.map26a'))
    if which == 'all':
        return ms
    conf = [m for i, m in enumerate(ms) if i % 4 == 3]
    return conf if which == 'confirm' else [m for m in ms if m not in conf]


def src_hash(bot):
    h = hashlib.sha1()
    for p in sorted((B / bot / 'src').rglob('*')):
        if p.is_file():
            h.update(p.relative_to(B / bot).as_posix().encode()); h.update(p.read_bytes())
    return h.hexdigest()[:10]


def reroll_iters(i):
    """재굴림 반복 횟수: 1 + (i*37) mod 211 → 항상 1~211번 (턴당 연료 수백 단위). 연속 211개 롤 번호에서 서로 다름.
    (2026-11-11 고침: 예전 3*i+1 은 확인 롤 1000번대에서 턴당 3000번 넘게 돌아 연료를 태웠다.)"""
    return 1 + (i * 37) % 211


def make_copy(src, dst, i):
    D = B / dst
    if D.exists():
        shutil.rmtree(D, ignore_errors=True)
    shutil.copytree(B / src / 'src', D / 'src')
    m = D / 'src' / 'main.cpp'
    t = m.read_bytes().decode('utf-8').replace('\r\n', '\n')
    assert t.count(KEY) == 1, 'reroll key line not found once in ' + src
    t = t.replace(KEY, f'    {{ volatile int rerollSink=0; for(int i=0;i<{reroll_iters(i)};++i) rerollSink+=i; (void)rerollSink; }}   /* build re-roll */\n' + KEY)
    m.write_bytes(t.encode('utf-8'))


def prep_copies(cand, base, i, tag):
    """11-12: 다음 롤 사본을 미리 만들고 두 사본을 동시에 빌드 (롤마다 빌드 ~30초를 앞 롤 경기와 겹침). 판정 · 키는 그대로."""
    import threading
    cn, bn = f'S{tag}{i}c', f'S{tag}{i}b'
    make_copy(cand, cn, i); make_copy(base, bn, i)
    def b_(n):
        subprocess.run([CAMBC, 'build', '--release', str(B / n)], cwd=str(W), capture_output=True)
    ts = [threading.Thread(target=b_, args=(n,)) for n in (cn, bn)]
    for x in ts: x.start()
    for x in ts: x.join()
    for n in (cn, bn): (B / n / '.prebuilt').write_text('1')


def drop_copies(i, tag):
    for d in (f'S{tag}{i}c', f'S{tag}{i}b'):
        shutil.rmtree(B / d, ignore_errors=True)


def one_roll(cand, base, i, maps, tag):
    cn, bn = f'S{tag}{i}c', f'S{tag}{i}b'
    if not ((B / cn / '.prebuilt').exists() and (B / bn / '.prebuilt').exists()):   # 11-12: 미리 빌드된 사본이면 그대로
        make_copy(cand, cn, i); make_copy(base, bn, i)
    O = TMP / f'{tag}_{i}'
    shutil.rmtree(O, ignore_errors=True); O.mkdir(parents=True)
    rows = []
    for m in maps:
        for s in 'AB':
            x, y = (cn, bn) if s == 'A' else (bn, cn)
            rows.append(f"{(B / x).as_posix()} {(B / y).as_posix()} {(POOL / (m + '.map26a')).as_posix()} {(O / (m + '_' + s + '.repl26a')).as_posix()} --json")
    man = O / 'man.txt'; man.write_text('\n'.join(rows) + '\n')
    log = O / 'run.log'
    with open(log, 'w', encoding='utf-8') as f:
        r = subprocess.run([CAMBC, 'run-batch', '--release', '--log-limit', '0', '--jobs', '10', '--timeout', '1800', str(man)], cwd=str(W), stdout=f, stderr=subprocess.STDOUT)
    txt = log.read_text(encoding='utf-8', errors='replace')
    w = l = 0; seat = {'A': 0, 'B': 0}
    for line in txt.splitlines():
        mm = re.search(r'TEAM ([AB]) WIN \(([^)]+)\)', line)
        if mm:
            if mm.group(2) == cn:
                w += 1
                # 후보가 이긴 판의 자리: 후보가 A 로 놓인 판에서 A 가 이겼으면 'A', B 로 놓인 판에서 B 가 이겼으면 'B'
                seat[mm.group(1)] += 1
            elif mm.group(2) == bn: l += 1
    one_roll.last_seat = seat
    n = len(rows)
    shutil.rmtree(O, ignore_errors=True)
    for d in (cn, bn):
        shutil.rmtree(B / d, ignore_errors=True)
    return w, l, n, r.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cand'); ap.add_argument('base')
    ap.add_argument('--mode', default='gain'); ap.add_argument('--mu1', type=float, default=6.0)
    ap.add_argument('--sigma', type=float, default=6.0)   # 256판 기준 (롤마다 판수로 환산)
    ap.add_argument('--alpha', type=float, default=0.05)
    ap.add_argument('--beta', type=float, default=0.10); ap.add_argument('--maps', default='dev')
    ap.add_argument('--start', type=int, default=1); ap.add_argument('--max', type=int, default=16)
    ap.add_argument('--tag', default=None)
    a = ap.parse_args()
    tag = a.tag or re.sub(r'[^a-z0-9]', '', a.cand.lower())[-6:]
    maps = maps_for(a.maps)
    A = math.log((1 - a.beta) / a.alpha); Bb = math.log(a.beta / (1 - a.alpha))
    if a.mode == 'gain':
        m0, m1 = 0.0, a.mu1
    else:
        m0, m1 = -a.mu1, 0.0
    LEDGER.parent.mkdir(parents=True, exist_ok=True)
    if not LEDGER.exists():
        LEDGER.write_text('candidate,cand_hash,base,base_hash,mode,maps,roll_id,wins,losses,games,x256,llr,started,finished,done\n', encoding='utf-8')
    ch, bh = src_hash(a.cand), src_hash(a.base)
    llr = 0.0; xs = []; verdict = 'max-rolls'
    for k in range(a.max):
        i = a.start + k
        st = datetime.datetime.now().isoformat(timespec='minutes')
        w, l, n, rc = one_roll(a.cand, a.base, i, maps, tag)
        if w + l == 0:
            print(json.dumps({'error': 'no results', 'roll': i, 'rc': rc}), flush=True); verdict = 'error'; break
        x = (w - n / 2) * 256.0 / n
        xs.append(x)
        sg = a.sigma * (256.0 / n) ** 0.5   # 롤 판수에 맞춘 sigma (sigma 는 256판 기준)
        llr += ((m1 - m0) / sg ** 2) * (x - (m0 + m1) / 2)
        fin = datetime.datetime.now().isoformat(timespec='minutes')
        with open(LEDGER, 'a', encoding='utf-8') as f:
            f.write(f'{a.cand},{ch},{a.base},{bh},{a.mode},{a.maps},{i},{w},{l},{n},{x:.2f},{llr:.3f},{st},{fin},1\n')
        print(json.dumps({'roll': i, 'w': w, 'l': l, 'n': n, 'x256': round(x, 1), 'llr': round(llr, 3)}), flush=True)
        if llr >= A: verdict = 'H1'; break
        if llr <= Bb: verdict = 'H0'; break
    mean = sum(xs) / len(xs) if xs else 0.0
    sd = (sum((v - mean) ** 2 for v in xs) / (len(xs) - 1)) ** .5 if len(xs) > 1 else float('nan')
    print(json.dumps({'cand': a.cand, 'base': a.base, 'mode': a.mode, 'maps': a.maps, 'verdict': verdict, 'rolls': len(xs),
                      'mean_x256': round(mean, 2), 'sd_x256': round(sd, 2) if sd == sd else None, 'llr': round(llr, 3), 'A': round(A, 3), 'B': round(Bb, 3)}), flush=True)


if __name__ == '__main__':
    main()
