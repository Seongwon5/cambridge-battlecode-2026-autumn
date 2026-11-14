"""drive3.py — 드라이버 3: 지침서 v2.2 4단계 판정 (SPRT) 을 큐로 끝없이 돌린다.

파일 (notes/ruler/):
  base.txt     지금 기준 봇 이름 (예: k-w5kp)
  queue_user.txt  사용자가 보낸 규칙 (가장 먼저 판정). 형식은 queue.txt 와 같다.
  queue.txt    후보 한 줄씩:  ID  TYPE  SPEC  [MODE]
                 TYPE = flagoff (SPEC = policy.h 의 bool 깃발 이름, 끄기 시험)
                        patch   (SPEC = 패치 파이썬 경로, 인자로 src 경로를 받음)
                        dir     (SPEC = cambc_ws/bots 아래 완성된 봇 이름)
                 MODE = gain (기본) | noninf
  done.txt     처리한 ID (다시 안 돈다)
  events.log   사건 한 줄씩. 감시 (Monitor) 는 ADOPT · IDLE · ERROR · ALERT · STOPPED 만 알린다.
  rollid.txt   다음 발견 롤 번호 (확인 롤은 1000 + 이 값, 2단계는 2000 + 이 값)
  STOP         있으면 지금 후보를 끝내고 멈춘다.
단계: 1 (dev SPRT gain mu1 6) → 4 (confirm SPRT, mu1 = max(3, 발견 평균/2)) → 2 (상대 3 x 2롤, 후보-기준 차이 z <= -2.5 면 탈락) → 채택.
채택: 봇을 bots/k-<id> 로 남기고 base.txt 갱신, submissions/v<N>_<id>.zip 생성, cambc submit 으로 바로 업로드 (사용자 지시 2026-11-11).
채택 세 번마다 누적 확인: 지금 기준 대 세 번 전 기준 (confirm, gain mu1 3). H0 이면 ALERT 쓰고 멈춤.
"""
import math
import sys, os, re, json, math, time, shutil, pathlib, subprocess, datetime, zipfile
ROOT = pathlib.Path(os.environ.get('CAMBC_ROOT', pathlib.Path(__file__).resolve().parents[1]))   # 저장소 뿌리 (cambc_ws · notes · submissions 가 있는 곳)
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import sprt as S

R = (ROOT / 'notes/ruler')
SUBS = (ROOT / 'submissions')
OPPS = ['c-b53z', 'k-zrt', 'c-i008']
SIG, ALPHA, BETA, MAXR = 6.0, 0.05, 0.10, 16   # SIG = 256판 기준. 롤마다 SIG*sqrt(256/판수) (11-12 01:30: 확인용 64판 롤의 sigma 를 개발용과 같게 쓰던 오류 고침)


def now():
    return datetime.datetime.now().strftime('%m-%d %H:%M')


def ev(kind, msg):
    with open(R / 'events.log', 'a', encoding='utf-8') as f:
        f.write(f'{now()} {kind} {msg}\n')


def state(**kw):
    p = R / 'state.json'
    try:
        s = json.loads(p.read_text(encoding='utf-8')) if p.exists() else {}
    except Exception:
        s = {}
    s.update(kw); s['updated'] = now()
    p.write_text(json.dumps(s, ensure_ascii=False), encoding='utf-8')


def rd(name, default=''):
    p = R / name
    return p.read_text(encoding='utf-8').strip() if p.exists() else default


def next_roll(n):
    v = int(rd('rollid.txt', '1') or 1)
    (R / 'rollid.txt').write_text(str(v + n), encoding='utf-8')
    return v


def run_sprt(cand, base, maps, start, mu1, mode, tag):
    A = math.log((1 - BETA) / ALPHA); Bb = math.log(BETA / (1 - ALPHA))
    m0, m1 = (0.0, mu1) if mode == 'gain' else (-mu1, 0.0)
    ms = S.maps_for(maps); llr = 0.0; xs = []
    maxr = round(MAXR * len(S.maps_for('dev')) / len(ms))   # 11-12: 최대 롤 = 같은 판수 예산 (개발 16롤 x 192판 = 확인 48롤 x 64판)
    ch, bh = S.src_hash(cand), S.src_hash(base)
    import threading
    pre = {}   # 11-12: 다음 롤 사본 미리 빌드 (경기와 겹침)
    def _finish():
        for j, th in list(pre.items()):
            th.join(); S.drop_copies(j, tag)
    for k in range(maxr):
        i = start + k
        st = datetime.datetime.now().isoformat(timespec='minutes')
        state(roll=i, maps=maps, mode=mode, mu1=mu1, rolls_done=k, llr=round(llr, 3), A=round(A, 3), B=round(Bb, 3))
        if i in pre: pre.pop(i).join()
        if k + 1 < maxr:
            th = threading.Thread(target=S.prep_copies, args=(cand, base, i + 1, tag)); th.start(); pre[i + 1] = th
        w, l, n, rc = S.one_roll(cand, base, i, ms, tag)
        if w + l == 0:
            _finish(); return 'ERROR', xs, llr
        x = (w - n / 2) * 256.0 / n; xs.append(x)
        sa = getattr(S.one_roll, 'last_seat', {'A': 0, 'B': 0})   # 자리별 후보 승 (A 자리 판 · B 자리 판 각 n/2)
        if abs(sa['A'] - sa['B']) > 3 * (n / 2 * 0.25) ** .5 * 2 ** .5:
            ev('SEAT', f'{cand} roll {i} 자리 쏠림 A {sa["A"]} / B {sa["B"]} (각 {n // 2}판)')
        sg = SIG * (256.0 / n) ** 0.5
        llr += ((m1 - m0) / sg ** 2) * (x - (m0 + m1) / 2)
        with open(S.LEDGER, 'a', encoding='utf-8') as f:
            f.write(f'{cand},{ch},{base},{bh},{mode},{maps},{i},{w},{l},{n},{x:.2f},{llr:.3f},{st},{datetime.datetime.now().isoformat(timespec="minutes")},1\n')
        state(rolls_done=k + 1, llr=round(llr, 3), last_x=round(x, 1), xs=[round(v, 1) for v in xs])
        if llr >= A:
            _finish(); return 'H1', xs, llr
        if llr <= Bb:
            _finish(); return 'H0', xs, llr
    _finish(); return 'MAX', xs, llr


def build(cid, typ, spec, base):
    name = 'k-q-' + re.sub(r'[^a-z0-9]', '', cid.lower())[:12]
    D = S.B / name
    if typ == 'dir':
        return spec
    if D.exists():
        shutil.rmtree(D, ignore_errors=True)
    D.mkdir(); shutil.copytree(S.B / base / 'src', D / 'src')
    if typ == 'flagoff':
        p = D / 'src' / 'policy.h'; t = p.read_bytes().decode('utf-8')
        pat = re.compile(r'(constexpr bool +' + re.escape(spec) + r' += )true')
        if len(pat.findall(t)) != 1:
            raise RuntimeError(f'flag {spec} count {len(pat.findall(t))}')
        p.write_bytes(pat.sub(r'\1false', t).encode('utf-8'))
    elif typ == 'patch':
        r = subprocess.run([sys.executable, '-X', 'utf8', spec, str(D / 'src')], capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode != 0:
            raise RuntimeError('patch failed: ' + (r.stdout + r.stderr)[-300:])
    else:
        raise RuntimeError('unknown type ' + typ)
    return name


def opp_wins(bot, opp, start, rolls):
    tot = 0
    for k in range(rolls):
        w, l, n, rc = S.one_roll(bot, opp, start + k, S.maps_for('all'), 'o' + re.sub(r'[^a-z0-9]', '', (bot + opp).lower())[-8:])
        tot += (w - n / 2) * 256.0 / n
    return tot


def stage2(cand, base):
    cache = R / f'stage2_{base}.json'
    bc = json.loads(cache.read_text(encoding='utf-8')) if cache.exists() else {}
    worst = 9e9; detail = {}
    for o in OPPS:
        if o not in bc:
            bc[o] = opp_wins(base, o, 2001, 2); cache.write_text(json.dumps(bc), encoding='utf-8')
        cw = opp_wins(cand, o, 2001, 2)
        z = (cw - bc[o]) / math.sqrt(2 * 2 * SIG ** 2)
        detail[o] = round(z, 2); worst = min(worst, z)
    return worst > -2.5, detail


def next_version():
    nums = [int(m.group(1)) for p in SUBS.glob('v*.zip') for m in [re.match(r'v(\d+)_', p.name)] if m]
    return max(nums + [655]) + 1


def adopt(cid, bot, note):
    keep = 'k-' + re.sub(r'[^a-z0-9]', '', cid.lower())[:14]
    if bot != keep:
        if (S.B / keep).exists():
            shutil.rmtree(S.B / keep, ignore_errors=True)
        shutil.copytree(S.B / bot / 'src', S.B / keep / 'src')
    v = next_version(); z = SUBS / f'v{v}_{keep}.zip'
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
        for p in sorted((S.B / keep).rglob('*')):
            if p.is_file() and '.cambc' not in p.parts:
                zf.write(p, p.relative_to(S.B / keep).as_posix())
    hist = rd('adopted.txt')
    with open(R / 'adopted.txt', 'a', encoding='utf-8') as f:
        f.write(f'{now()} v{v} {keep} {note}\n')
    (R / 'base.txt').write_text(keep, encoding='utf-8')
    return keep, v, z


def main():
    ev('START', 'base=' + rd('base.txt'))
    while True:
        if (R / 'STOP').exists():
            ev('STOPPED', 'STOP file'); return
        free = shutil.disk_usage('C:/').free / 1e9
        if free < 15:
            ev('ALERT', f'disk {free:.1f}GB'); return
        base = rd('base.txt')
        done = set(rd('done.txt').split())
        q = [l.split() for name in ('queue_user.txt', 'queue.txt') for l in rd(name).splitlines() if l.strip() and not l.startswith('#')]
        q = [x for x in q if x[0] not in done]
        if not q:
            if not getattr(main, 'idle', False):
                ev('IDLE', 'queue empty'); main.idle = True
            time.sleep(120); continue
        main.idle = False
        cid, typ, spec = q[0][:3]; mode = q[0][3] if len(q[0]) > 3 else 'gain'
        state(cand=cid, typ=typ, spec=spec, cmode=mode, stage='1 (dev)', base=base, xs=[], llr=0)
        with open(R / 'done.txt', 'a', encoding='utf-8') as f:
            f.write(cid + '\n')
        try:
            bot = build(cid, typ, spec, base)
            st = next_roll(MAXR)
            skip = q[0][4] if len(q[0]) > 4 and q[0][4].startswith('s2:') else ''   # 11-13: 's2:<1단계 평균>:<확인 평균>' = 이미 잰 1 · 4단계 결과로 2단계부터 (사용자 승인한 후보만)
            if skip:
                m1, m4 = float(skip.split(':')[1]), float(skip.split(':')[2])
                ev('SKIP14', f'{cid} 1단계 {m1:+.1f} · 확인 {m4:+.1f} (앞서 잰 값) -> 2단계부터')
            else:
                v1, xs, llr = run_sprt(bot, base, 'dev', st, 6.0, mode, 'd' + cid[:6].lower())
                m1 = sum(xs) / len(xs) if xs else 0
                ev('STAGE1', f'{cid} {v1} rolls={len(xs)} mean={m1:+.1f} llr={llr:.2f}')
                if v1 != 'H1':
                    ev('REJECT', f'{cid} stage1 {v1} mean={m1:+.1f}'); cleanup(bot, typ); continue
                mu1 = max(3.0, m1 / 2) if mode == 'gain' else 6.0
                state(stage='4 (confirm)', xs=[], llr=0)
                v4, xs4, llr4 = run_sprt(bot, base, 'confirm', 1000 + 3 * st, mu1, mode, 'c' + cid[:6].lower())
                m4 = sum(xs4) / len(xs4) if xs4 else 0
                ev('STAGE4', f'{cid} {v4} rolls={len(xs4)} mean={m4:+.1f} mu1={mu1:.1f}')
                if v4 == 'MAX' and mode == 'gain' and xs4:   # 11-13 (사용자 승인): 최대 롤을 다 채웠으면 고정 표본 판정 — 평균 >= 2 표준오차면 통과
                    se4 = SIG * math.sqrt(256 / (2 * len(S.maps_for('confirm')))) / math.sqrt(len(xs4)); z4 = m4 / se4
                    ev('STAGE4Z', f'{cid} MAX 고정 표본 z={z4:+.2f} (평균 {m4:+.1f} / 표준오차 {se4:.2f})')
                    if z4 >= 2.0: v4 = 'H1'
                if v4 != 'H1':
                    ev('REJECT', f'{cid} stage4 {v4} (1단계 {m1:+.1f} -> 확인 {m4:+.1f})'); cleanup(bot, typ); continue
            # 11-13 (v664 사례: 기준 봇만 이기는 상성 변화가 채택됨): 기준 봇의 전 채택 버전과도 같은 방식으로 겨뤄 이겨야 한다
            hist0 = rd('adopted.txt').splitlines(); gp = hist0[-2].split()[3] if len(hist0) >= 2 else ''
            if gp and gp != base and (S.W / 'bots' / gp).exists() and not skip:
                state(stage='4b (vs prev)', xs=[], llr=0)
                mu1b = max(3.0, m4 / 2) if mode == 'gain' else 6.0
                vb, xsb, llrb = run_sprt(bot, gp, 'confirm', 2000 + 3 * st, mu1b, mode, 'g' + cid[:6].lower())
                mb = sum(xsb) / len(xsb) if xsb else 0
                if vb == 'MAX' and mode == 'gain' and xsb:
                    seb = SIG * math.sqrt(256 / (2 * len(S.maps_for('confirm')))) / math.sqrt(len(xsb))
                    if mb / seb >= 2.0: vb = 'H1'
                ev('STAGE4B', f'{cid} vs {gp} {vb} rolls={len(xsb)} mean={mb:+.1f}')
                if vb != 'H1':
                    ev('REJECT', f'{cid} stage4b {vb} (기준 {base} 에게 {m4:+.1f} · 전 버전 {gp} 에게 {mb:+.1f})'); cleanup(bot, typ); continue
            state(stage='2 (opponents)')
            ok2, d2 = stage2(bot, base)
            ev('STAGE2', f'{cid} {"pass" if ok2 else "fail"} {d2}')
            if not ok2:
                ev('REJECT', f'{cid} stage2 {d2}'); cleanup(bot, typ); continue
            keep, v, z = adopt(cid, bot, f'{typ}:{spec} s1 {m1:+.1f} s4 {m4:+.1f}')
            ev('ADOPT', f'v{v} {keep} ({typ} {spec}) 1단계 {m1:+.1f}/롤 확인 {m4:+.1f}/롤 · zip {z.name}')
            ok, msg = upload(keep, f'v{v} {cid}: s1 {m1:+.1f} s4 {m4:+.1f}/roll SPRT')
            ev('UPLOAD' if ok else 'ERROR', f'v{v} {keep} {msg}')
            cleanup(bot, typ)
            n_adopt = len(rd('adopted.txt').splitlines())
            if n_adopt % 3 == 0:
                hist = rd('adopted.txt').splitlines()
                prev = hist[-4].split()[3] if len(hist) >= 4 else rd('base0.txt')
                if prev:
                    vc, xc, lc = run_sprt(keep, prev, 'confirm', 3000 + 3 * st, 3.0, 'gain', 'cum')
                    ev('CUMCHECK', f'{keep} vs {prev} {vc} mean={sum(xc) / max(1, len(xc)):+.1f}')
                    if vc == 'H0':
                        ev('ALERT', f'누적 확인 실패: {keep} vs {prev}'); return
        except Exception as e:
            ev('ERROR', f'{cid} {str(e)[:200]}')


def upload(bot, note):
    note = note[:80]
    C = S.CAMBC
    try:
        r = subprocess.run([C, 'submit', f'bots/{bot}', '-n', note], cwd=str(S.W), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=900)
    except Exception as e:
        return False, 'submit exception ' + str(e)[:100]
    for k in range(4):
        time.sleep(30)
        lst = subprocess.run([C, 'submissions', 'list'], cwd=str(S.W), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120).stdout
        if note[:40] in lst:
            line = next((x for x in lst.splitlines() if note[:40] in x), '')
            ver = line.split()[0] if line.split() else ''
            if ver.startswith('v'):
                subprocess.run([C, 'submissions', 'activate', ver], cwd=str(S.W), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
                live = subprocess.run([C, 'submissions', 'list'], cwd=str(S.W), capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120).stdout.splitlines()[:2]
                return True, 'uploaded+activated ' + ver + ' | ' + ' '.join(x.strip() for x in live)
            return True, 'uploaded: ' + line.strip()[:120]
    return False, 'not found in submissions list: rc=%s %s' % (r.returncode, (r.stdout + r.stderr)[-150:])


def cleanup(bot, typ):
    if typ != 'dir' and bot.startswith('k-q-'):
        shutil.rmtree(S.B / bot, ignore_errors=True)


if __name__ == '__main__':
    main()
