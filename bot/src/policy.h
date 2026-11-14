#pragma once
// policy.h — 정책 상수 (실험으로 바꾸는 것). 한 곳에 모은다. 바꾸면 옛값은 지우고, 비교는 빌드 사이에서 한다.
namespace nb::policy {
inline constexpr bool FWK_ON = true;   // FWK: 걷기만 턴 (농사 · 적 없음 · 6칸 캠프 없음 · 대시 없음) 은 전투 고르기 건너뜀 → OPQ · FBS2 최단 걸음
inline constexpr bool FWK2_ON = true;   // FWK2: 걷기만 턴 건너뜀을 바치기 · 상점 · 회복 (후퇴 아님) 임무로 (최단 걸음 직접)
inline constexpr bool RNGW_ON = true;   // RNGW: 원거리 적도 걷고 쏜다 (닿는 거리 = 사거리 + 차례 수)
inline constexpr bool LVG_ON = true;   // LVG: 팀원이 치던 큰 무리를 놓으면 걷는 나는 다시 고름 (점수식 그대로)
inline constexpr bool STK_ON = false; inline constexpr float STK_P = 0.33f;   // STK: 우리 집 쪽 큰 무리 값 x (1 + 상대가 가져갈 잰 몫 0.33)
inline constexpr bool INV_ON = true;   // INV: 우리 집 쪽 큰 무리에 혼자 든 적 = 무리 값 + 그 적 판돈, 보이면 다시 고름
inline constexpr bool RCV_ON = true;   // RCV: 회복 · 열세 · 사거리 안이면 물러나는 걸음 값 = Σ 적마다 (거리 변화 x 힘 x gph)
inline constexpr bool LONE_ON = true;   // LONE: 혼자 + 보이는 적 2명+ 의 란체스터 힘 >= 내 힘 x R3_RATIO 면 위협 전이라도 R2 진입 금지
inline constexpr bool TFX_TPC = true;   // TFX: 팀 계획 완료율 — 대기 중인 같은 무리를 다시 골라도 새 시작으로 세지 않음
inline constexpr bool TFX_SMITH = true;   // TFX: 팀 계획 받을 피해의 캠프 처치 순서 = 힘/체력 큰 순 (봇 focusCamp 와 같음)
inline constexpr bool TFX_DPS = true;   // TFX: 팀 계획 멤버 = 걸음 순 · 화력 순 두 줄 중 이득 큰 쪽
inline constexpr bool HDF_ON = true;   // HDF: 농사 중에도 피해 대시 칸 후보
inline constexpr bool INR_ON = true;   // INR: 빈 계획 (연료 잘림 · 계산 없음) 인데 사거리 안 적 챔피언 → 체력 가장 낮은 적 평타
inline constexpr bool KST_ON = true;   // KST: 막타 뒤 같은 턴 점령 칸으로 한 걸음
inline constexpr bool HDW_ON = true;   // HDW: 대시 착지가 깨울 잠든 무리 피해를 비용에
inline constexpr bool NOP_ON = false;   // NOP: pickFarm 곁 아군 셈 (OPS · SQE) 끔 — 함께 칠 몸 = 담당 팀원만
inline constexpr bool CSX_ON = true;   // CSX: 전투 앞부분 같은 답 · 적은 계산
inline constexpr bool OPK_ON = true; inline constexpr int OPK_MINSEEN = 3, OPK_ROUNDS = 4;   // OPK: 개막 얇은 (끊긴) 고르기 미룸
inline constexpr bool R1M_ON = true; inline constexpr long long R1M_FLOOR = 70000, R1M_INIT_FLOOR = 25000;   // R1M: 첫 턴부터 움직이기 (초기화 이어 읽기 · 준비 전 한 걸음 · 준비 턴에 남으면 판단)
inline constexpr bool OEN_ON = true;   /* OEN */
inline constexpr bool JCO_ON = true;   /* JCO: 드래프트 개막 배정을 무리 연합으로 (k 몸이 함께 갈 때 몸당 벌이가 가장 높은 무리부터 묶어 배정) */
inline constexpr bool RSW2_ON = false;   /* RSW2 */
inline constexpr bool OSY_ON = true; inline constexpr int OSY_WAIT = 15;   // OSY: 자는 힘 있는 무리에 먼저 닿은 몸은 혼자 감당 못 하면 15R 안에 오는 담당 팀원을 기다린 뒤 침
inline constexpr bool DSH_ON = true;   // DSH: 대시로 구덩이 1칸 건너기 (다리 칸을 길찾기 통행으로, 진전 없으면 대시)
inline constexpr bool FJN_ON = true; inline constexpr int FJN_MODE = 2;   // FJN: 팀원 새 담당 → 걷는 몸 즉시 다시 고르기 (1 값 더 큼 · 2 값 40+ · 3 늘)
inline constexpr bool PFX4_ON = true;   // PFX4: 고르기 상수 (금 나눗수 · 배율 · 설계 화력) 고리 밖으로 (답 같음)
inline constexpr bool PFX3_ON = true;   // PFX3: pnlLoss O(1) (계수 저장)
inline constexpr bool PFX2_ON = true;   // PFX2: 점수 식 연료 — 캠프 한 번 훑기 · 광역 수 저장 · 집 걸음 저장 · 연료 · 라운드 호출 줄이기 (답 같음)
inline constexpr bool PFX_ON = true;   // PFX: 고르기 연료 — 못 감 기억은 캠프 죽음에만 다시 묻기 · 정적 연결 성분 · worthWith 는 1등 후보만 (답 같음)
inline constexpr bool TM42_TFP_ON = true;   // TM42_TFP: 팀원 bot42 처럼 TFP 끔 (개인 캠프 고르기)
inline constexpr bool TFP_ON = !TM42_TFP_ON;   // TFP2: 팀 농사 이익 동기화 (v151: 끔 — 계산 연료가 이동 · 전투를 굶김, 고치는 중)
inline constexpr bool PIT_ON = true; inline constexpr int PIT_MIN_CHEB = 6, PIT_EXTRA = 6; inline constexpr bool PIT_REACH = false;   // PIT: 건너편 적 빼기
inline constexpr bool FVP_ON = true;   /* FVP: Vesper 첫 엠블럼 = Prospector (랭커 Vesper 첫 엠블럼 49% prospector, 우리 88% capture) */
inline constexpr bool PRO_ON = true;   // PRO: Vesper · Marcella 첫 엠블럼 = Prospector
inline constexpr bool AGM_ON = true;   // AGM: 아군 든 젬 읽기 (싸움 판돈)
inline constexpr bool PSR_ON = false; inline constexpr float PSR_AHEAD = 0.3f, PSR_TRAP = 0.5f; inline constexpr float PSR_AHEAD_CAP = 5.f, PSR_TRAP_CAP = 4.f; inline constexpr int PSR_R = 10; inline constexpr float PSR_HIT_MUL = 0.5f;   // PSR2: 칠 수 있는 적이 사거리 안이면 포지션 위험 × 이 값   // PSR: 포지션 위험도 (칸 비용에만)
inline constexpr bool RWA_ON = true;   // RWA: 원거리 걷기 → 리셋 → 평타
inline constexpr bool VYO_ON = true;   /* VYO: 곁 Vesper 에게 보상 (양보 · 줍기 같은 조건) */ inline constexpr bool CPY_ON = true;   // CPY: 점령 양보 = 패시브 덤이 가장 큰 곁 아군 (Vesper 금 · Marcella 젬)
inline constexpr bool MGM_ON = true;   // MGM: Marcella 젬 값 = 젬 + 금 2 (바칠 때)
inline constexpr bool PGBF_ON = false;   // PGBF: PGB 상한 금 값 = V 와 같게 (gvtPick)
inline constexpr bool R3L_ON = true;   // R3L: 후퇴 힘 비교 = 란체스터 (체력 합 × fnDps 합)
inline constexpr bool PEEL_ON = true;   // PEEL: 근접이 앞줄 우리 원거리를 노리는 적 근접 쪽으로
inline constexpr bool CPX_ON = true;   // CPX: ES2 team gems exclude Prism hoard (Prism banks only at the winning move)
inline constexpr bool TSF_ON = true;   // TSF: 젬 들고 제단 곁이면 바침
inline constexpr int TSF_MIN = 10;     // TSF: 이만큼 이상 들었을 때
inline constexpr int TSF_STEPS = 4;    // TSF: 제단까지 걸음
inline constexpr bool FKV_ON = true;   // FKV: 캠프 마지막 일격 값 = 한 방 피해 (체력 0.2 캠프 앞 멈춤 버그)
inline constexpr float GVX_PICK = 2.5f;   // GVX: 무리 고르기 금 1 = 젬 1/2.5 (예전 1/5)
inline constexpr float GVX_VAL = 5.f;     // GVX: 임무 값 · 손익 금 1 = 젬 1/5 (예전 1/10)
inline constexpr bool DSV_ON = false;   // DSV: 적 챔피언 8칸 안이면 피해 없는 대시는 치거나 도망칠 때만 (체력 40% 이상)
inline constexpr bool GDS_ON = true;   // GDS: 싸움 턴 연료 부족으로 choose 가 챔피언 대시 칸을 평가 못 하면 턴 시작에 미리 센 챔피언 대시 (같은 식) 와 계획을 비교
inline constexpr long long GDS_FUEL = 30000;   // GDS: 미리 셀 때 연료 문 (choose 대시 칸 만들기 문과 같은 값)
inline constexpr bool HDS_ON = true;   // HDS: 적 챔피언 8칸 안이면 피해 대시는 챔피언을 맞힐 때만 (체력 40% 이상)
inline constexpr bool FTR_ON = true;   // FTR: 이번 라운드에 끝낼 수 있는 캠프부터
inline constexpr bool KAL_ON = false;   // KAL: 표적 셈의 아군 평타는 아직 안 둔 아군만
inline constexpr bool ZT_ON = true;   // ZT: 내 다음 턴 전에 못 두는 적은 위협 없음
inline constexpr bool ETN2_ON = false;   // ETN2: 정확한 적 차례 수는 닿느냐에만 · 준비된 대시 · 두 차례 기술 사거리
inline constexpr bool TFD_ON = true;   // TFD: 싸움 판단을 싸움 기준 (기준 적) 으로
inline constexpr int TFD_R = 6;      // TFD: 기준 적에서 양쪽을 세는 반경
inline constexpr bool BGF_ON = true; inline constexpr int BGF_MARGIN = 8;   // BGF: 목표 거리장 BFS 제한 (여유 = 둘레 칸 걸음 상한, 대시 2 + 걷기 · 구멍 우회)
inline constexpr long long FGR_MISSION = 45000;  // FGR: GR · PK · FCS 표시 갱신 최소 연료
inline constexpr long long FGR_SENSE = 50000;    // FGR: 감지에서 캠프 무리 읽기를 멈추는 연료
inline constexpr long long FGR_COMBAT = 30000;   // FGR: 전투 계산 최소 연료 (적 · 힘 있는 캠프 곁은 예외)
inline constexpr int MTA_SLACK = 5;   // MTA: 담당 팀원이 내 도착 + 이만큼 안에 오면 '같이 칠 몸'

// --- 가치 단위: 젬. 체력 1점의 젬 값 = (든 젬 + 죽음 시간값 + 회복값) / 최대 체력
constexpr float DEATH_TIME_GEMS = 22.f;   /* DTU: PLV_DEATH 와 같게 */     // 죽어서 잃는 ~100R × 몸당 0.09 젬/R
constexpr float DEATH_ENEMY_GAIN_GEMS = 15.f; // 죽으면 적이 얻는 것 (금 50 · 임무 중단 · 캠프 넘김) 의 젬 환산 — 죽음 비용에 더한다
constexpr int   DEBUG_CELLS_LO = -1, DEBUG_CELLS_HI = -1;   // 기록 빌드에서 칸별 값 (#nbv) 을 찍을 라운드 범위 (-1 = 안 찍음)
constexpr int   PICK_RETRY_ROUNDS = 4;     // 고를 그룹이 없으면 이 간격으로 다시 (거리장 연료)
constexpr int   PICK_MAX_CHEB = 45;        // 조수 고르기 (PNL 이 고른 그룹이 없을 때) 에서 보는 그룹의 최대 거리 (체비셰프)
constexpr int   STALL_ROUNDS = 12;         // 이만큼 목표 걸음이 줄지 않으면 임무 중단 (그 그룹은 AVOID_ROUNDS 동안 피함)
constexpr int   AVOID_ROUNDS = 60;         // 정체 · 위험으로 중단한 그룹을 이만큼 피한다
constexpr float HEAL_COST_GEMS = 6.6f;   /* X9 */      // 체력 한 통을 다시 채우는 시간 (리콜 5 + 스폰 회복 + 걷기 ≈ 30R × 0.09)
constexpr float LOW_HP_FRAC = 0.5f;        // 남는 체력이 이 비율 아래면 죽음 위험 비용이 제곱으로 붙는다
constexpr float GEM_PER_STEP = 0.09f;      // 목표로 한 걸음 가까워지는 값 (몸당 젬/R)
constexpr float LETHAL_MARGIN = 1.0f;      // 받을 피해 ≥ 체력 × 이 값 → 죽을 칸 (−∞)

// --- 임무
constexpr float MS_TMAX = 25.f;      // MS: 모노리스는 이 라운드 안에 잡을 수 있을 때만
constexpr float MS_HPFRAC = 0.6f;    // MS: 받을 피해 합 ≤ 이 비율 × n 명 체력 합
constexpr float MAX_LOSS_HP = 1.8f;  // n 명이면 잡기까지 받을 피해가 체력 × 이 값 안이어야 그 그룹을 맡는다 (= 옛 GO_LOSS_FRAC 0.9 × M_TRIPS 2: 두 번에 나눠 — 중간에 회복 — 끝낸다. 상위 팀 ambush: 엘크 하나에 750→288 맞고 물러났다 돌아옴). 1.8f 는 0.9f × 2.f 와 같은 float
constexpr int   WAIT_RESPAWN_MAX = 10;   // WR10: RSW — 담당 그룹 리스폰이 이보다 오래 남으면 다시 고르기를 묻는다 (v11 의 12 는 강한 상대 −60292)
// 조수 고르기의 그룹당 인원 상한 (캠프 힘으로): 클러스터 1 · 늑대 2 · 엘크 이상 5
constexpr int capFor(float strMax) { return strMax <= 0.f ? 1 : strMax <= 25.f ? 2 : 5; }   // M-d: 엘크 그룹도 5 (상위 팀 stampede 17400 그룹에 5몸)
constexpr int   LETHAL_STREAK_ABANDON = 6; // 모든 칸이 음수인 턴이 연속 이만큼이면 임무 중단

// --- 경제
constexpr int   TRIBUTE_FINAL_ROUND = 1850;   // 이후엔 1젬도 바친다 (욕심쟁이 끔)
constexpr float KB_PRIOR_R = 300.f, KB_PRIOR_DEATH = 1.f, KB_PRIOR_RATE = 0.5f, KB_LINK = 2.f;   // KB: 사전값 (300R · 죽음 1 · 벌이 0.5젬/R), KB_LINK = 짝 몸 (Cassi · Issac) 은 죽을 확률 × 2
constexpr int   FX_LATE = 600;     // FX: 앞선 쪽 바친 젬이 이만큼이면 욕심쟁이 끔 · 팀 기준 줄임
constexpr int   FX_DANGER = 800;   // FX: 상대 바친 젬이 이만큼이면 바로 바침 (이기는 한 수 기준은 rules::WIN_GEMS)
constexpr bool  ES_ON = true;      // ES: 호위 — 제단에 적이 보이면 도움 요청 · 바치기 미룸, 가까운 아군이 도우러 감
constexpr int   ES_R = 6;          // 제단 둘레 (적 · 미룸)
constexpr int   ES_HELP_R = 15;    // 도우러 가는 거리
constexpr float ES_HP = 0.5f;      // 이 체력 비율 이상만 호위
constexpr int   ES_HOLD_MAX = 25;  // 미룸 상한
constexpr int   ES_CLEAR = 3;      // 적이 안 보인 지 이만큼이면 호위 끝
constexpr bool  GR_ON = true;   // GR: 욕심쟁이 — 가까운 잡을 캠프가 있으면 바치기를 미룬다
constexpr float GR_HP = 0.4f;     // 이 체력 비율 이상만 미룸
constexpr int   GR_STEPS = 6;     // 임무 그룹이 이 걸음 안이면 미룸 · HLP 전환 기준
constexpr int   GR_NEAR = 6;      // 임무 없을 때 이 cheb 안 그룹
constexpr bool  NA_ON = false;   // NA: 먼 바치기의 집 리콜 끔 — 가장 가까운 제단으로 걷기 (false = 리콜 켬)
constexpr float RECOVER_HP = 0.35f;        // 이 아래면 회복 임무
constexpr bool  F_CAMP_STAY = true;        // F: 적이 없는 캠프 싸움은 RECOVER_HP 까지 버틴다 (죽음 위험 항 대신 회복 비용)
constexpr int   SHOP_MIN_GOLD = 30;        // 샤드 하나 값 이상이면 스폰에서 산다
constexpr bool  OP_ON = true;      // OP: 목표 없는 초반 라운드에 가까운 그룹 쪽으로 한 걸음
constexpr int   OP_ROUNDS = 12;
constexpr bool  FTM3_ON = true;   // FTM3: 첫 턴 멈춤 고치기 (초기화 턴은 한 걸음만, 고르기는 연료 가득한 다음 턴)
constexpr int   DD_GOLD_STEPS = 15;   // SP2 흉내: 금만 있는 그룹은 이만큼 더 먼 것으로
constexpr int   DD_RESPAWN_SLACK = 7;   // 적이 안 보일 때 리스폰 그룹 도착 여유 R (넘으면 점수 × 0.001)
constexpr bool  PLJ_ON = true;   // PLJ: 싸움 우리 힘 = 표적에게도 닿는 아군만
constexpr int   EC_SUB = 800;   // EC: 바친 젬이 이만큼이면 점령을 양보하지 않는다 (함께 점령 → 젬을 나눠 동시에 바침)
constexpr bool  SP_ON = false; /* SPX: spV 는 어디에도 안 쓰임 — 연료만 */   // SP2: 혼자 잡을 수 있는 그룹은 나눠 갖는다 (낮은 칸 자유 아군의 고르기를 흉내)
constexpr int   SP_STEPS = 10;    // SP2: 흉내로 이미 고른 몸 하나당 걸음 +
constexpr int   SP_AREA = 1000;   // SP5: 몸 하나당 땅이 이만큼 넓은 맵에서만 흩어진다
constexpr bool  CR_ON = true;              // CR: 점령 대기 중 리콜 (보상 받자마자 집)
constexpr int   CR_WAIT = 2;
constexpr int   CR_HOME_MIN = 8;
constexpr int   CR_GOLD_MIN = 150;         // CRG: 금 + 그룹 금이 이 이상일 때만 점령 중 리콜
constexpr bool  CR_SOLO = true;
constexpr long long FS_FUEL = 45000;       // FS: 이만큼 연료가 남으면 내 거리장을 만든 턴에도 목표 거리장을 만든다 (거리장 하나 ~5k)
constexpr long long FG_MAX = 80000;        // FG: 넘친 뒤의 비용 추정 상한
constexpr long long FG_INIT = 20000;       // 목표 칸 계산 비용 첫 추정 (LC: 따뜻한 비용만 잰다 — 처음 세기는 칸 고리 바닥이 막는다)
constexpr long long LC_PREWARM_FLOOR = 45000;   // LC: 턴 끝에 이만큼 남으면 목표 칸 정적 목록 · 거리장을 미리 센다
constexpr bool  GF_ON = true;                 // GF: 그룹 · 집 정적 거리장 (배열)
constexpr bool  GF_PICK = true;               // GF: 고르기에 쓴다
constexpr bool  GF_WALK = true;               // GF: 목표 거리장이 없을 때 걷기에 쓴다
constexpr long long GF_INIT_COST = 30000;     // GF: 거리장 하나 비용 추정 (처음)
constexpr bool  AW_ON = true;   // AW: 제단은 걸음 (정적 거리장) 으로 고른다
constexpr long long GF_COST_CAP = 40000;      // GC: 넘친 뒤 추정 (거리장 하나는 100×100 에서도 ~25k)
constexpr long long OG_FLOOR = 90000;         // OG: 첫 턴 거리장 미리 만들기 바닥
constexpr long long LC_BUILD_RES = 15000;       // LC: 목록 하나 세기 전 (추정 + 이 여유) 가 남아야
constexpr long long LC_COLD_RES = 30000;        // LC: 목표 칸 계산 중 처음 세기는 (추정 + 이 여유) 가 남을 때만 — 아니면 다음 턴
constexpr long long ZO_WALL_FUEL = 55000;   // ZO: 정적 벽 비트를 만들 최소 연료
constexpr long long ZO_PICK_FUEL = 60000;   // ZO: 그룹 고르기 (거리장 + 그룹 고리) 시작 최소 연료
constexpr long long ZO_LOOP_STOP = 30000;   // ZO: 그룹 고리 중 이 아래면 멈춤
constexpr long long ZO_NEED_CAP = 88000;    // ZO3: 요구 연료 상한
constexpr long long ZO_COMBAT_RES = 30000;  // ZO3: 목표 계산 뒤 전투 · 실행 여유
constexpr long long FG_RES = 12000;   // FG: 처음 3R 은 첫 걸음용 정적 그룹 거리장을 이 여유로도 만든다
constexpr long long ZO_TILE_FLOOR = 40000;  // TF: 목표 칸 고리는 이 연료 아래면 멈춤
constexpr bool  RC_HOME_LOOP = true;       // RC: 상점 왕복 · 먼 바치기도 리콜로 (상위 팀 리콜 2862 vs 우리 600)
constexpr int   RC_TRIBUTE_MIN = 12;       // 제단까지 이 걸음 이상이면 리콜 비교
constexpr int   RC_MARGIN = 3;             // 리콜 경로가 이만큼 더 짧아야
constexpr int   RC_SHOP_MIN = 8;           // 스폰까지 이 걸음 이상이면 상점 왕복은 리콜
constexpr float RQ_RECOVER_HP = 0.6f;      // RQ: 할 농사가 없을 때 회복하러 가는 체력
constexpr int   SHOP_TRIP_GOLD_FAR = 150;  // RQ: 멀어도 가는 금 (엠블럼 하나 값일 때만; 옛 90 → 150)

// --- M (랭커 모방 7h, 2026-10-18): 상위 팀 리플레이에서 읽은 O0/O1 규칙. 끄면 기준 B 와 같다
constexpr bool  M_ALIVE_TILES = true;      // 공격 칸은 '산' 캠프 (마지막으로 본 체력 > 0 또는 모름) 둘레만. 죽은 캠프 자리 옆에 서서 d0=0 으로 멈추던 것 (ambush g11: 엘크 1090 남기고 두 몸이 사거리 밖 4칸에서 대기)
constexpr bool  M_FINISH = true;           // 캠프 싸움: 남은 그룹 체력을 지금 인원으로 끝낼 때까지 받을 피해를 셈해 살아남으면 (남는 체력 ≥ SURVIVE_FRAC) 죽음 위험 비용을 빼고 회복 비용만 (상위 팀 종류 5 가 750→288 까지 맞아 주며 16R 에 엘크 처치)
constexpr float SURVIVE_FRAC = 0.2f;
constexpr bool  PK_ON = true;      // PK: 바닥 보상 줍기
constexpr int   PK_MAX_STEPS = 12;
constexpr bool  NOX_ON = true;   // NOX: 임무 밖 힘 없는 덩어리 무리 캠프는 깨어 있을 때만 친다 — 주울 몸 없는 보상 대기를 만들지 않음
constexpr bool  CO_ON = true;      // CO: 점령은 칸 위에 선 몸 하나면 된다 → 아군이 서 있으면 비킨다
constexpr int   BS_GEMS = 200;     // BS: 이만큼 젬이 있는 그룹은 함께 점령해 나눠 받는다 (CO 예외)
constexpr int   OQ_TURNS = 2;      // OQ: 목표 칸이 아군 차지로 이만큼 연속이면 그 칸을 뺀다
constexpr bool  FN_ON = true;      // FN: 깎은 적 마무리 (쫓기)
constexpr float FN_ME_HP = 0.5f;
constexpr float FN_ADV = 1.2f;
constexpr bool  FNK_ON = true;    // FNK: 한 방 적 (체력 <= 내 평타 me.str) 은 FN 문턱 (FN_ME_HP · FN_ADV) 없이 쫓음 — 칸 안전 (죽을 칸 · dmgCost) 은 그대로
constexpr float FN_STEP = 2.0f;    // 쫓는 한 걸음의 진행 값 (젬) — 목표 걸음 값 대신
constexpr bool  LK_ON = true;
constexpr bool  TPS_ON = true;     /* TPS: 순간이동 수정 지름길 */
constexpr float TPS_W = 1.f;       /* TPS: 정도 축 = 착지 위험 무게 (dmgCost 에 곱함; 0 = 위험 무시, 클수록 조심) */
#ifdef TPS_PROBE
#define TPS_LOG(...) ::nb::log::always(__VA_ARGS__)
#else
#define TPS_LOG(...) do { } while (false)
#endif      // LK: 제단 잠금
constexpr bool  LK2_ON = true;     // LK2: 적 잠금은 피하지 않고 평타로 부순다
constexpr int   LK2_BLOCK = 3;     // 칠 수 없을 때만 이만큼 피함
constexpr bool  LK_CANCEL = true;   // LK3: 상대가 바치는 중인 제단만 잠가 채널을 끊는다
constexpr bool  KT_ON = true;      // KT: 탱크 붙은 센 캠프는 피하지 않는다
constexpr bool  CC_ON = true;      // CC: 남는 기술 칸으로 기절
constexpr int   AP2_R = 6;         // AP2: 이 거리 안 적이 없으면 캠프만의 싸움
constexpr bool  SF_ON = false;      // SF: 막힘이 바뀌면 목표 거리장 다시
constexpr int   NE_TURNS = 2;      // NE: 이만큼 연속 못 들어가면 회복

constexpr float AOE_CAMPS_EST = 2.f;       // 범위 주문이 한 번에 맞히는 캠프 수 추정 (샤드 선택)

// --- R (O7): 적 반응. 상위 팀: 체력 < 40% 면 물러남 (57~64%), 수적 열세면 접근 16~20% 뿐, 적과 평균 거리 4~5
constexpr bool  R_RETREAT = true;
constexpr bool  PL_ON = true;      // PL: 싸움 · 농사 손익 (기대 젬) — 싸움이 이득이면 캠프 대신 적, 팀에 알려 합류. 이길 확률 = 란체스터 (PLS)
constexpr int   PL_R = 6;
constexpr bool  VUL_ON = true;     // VUL: 어부지리 (다투는 그룹: 처치 창 · 가로채기 창 · 그 밖엔 사거리 밖 고리에서 기다림)
constexpr bool  FHT_ON = true;     // FHT: 깨어 있는 무리 캠프는 다른 임무 중에도 사거리 안이면 침
constexpr bool  VCR_ON = true;     // VCR: Vesper 몰아주기 (점령 양보 · 아이템 순서 · 금 값)
constexpr int   VCR_TYPE = 1;      // champion_type vesper_vale
constexpr int   VCR_MIN_GOLD = 80;  // VCR3: 이 금 이상 무리만 Vesper 에게 양보
constexpr int   VCR_R = 4;         // VCR: Vesper 가 무리 이 칸 안이면 점령 양보
constexpr float VCR_GOLD = 1.33f;  // VCR: Vesper 패시브 (모든 금 +33%)
constexpr bool  CXE_ROT = true;    // CXE2: 벗어날 걸음 = 동료에게 어그로 넘기는 걸음
constexpr bool  FOC_ON = true;
constexpr bool  RST_ON = true;    // RST: 근접 리셋 조합 — 걷기 전 A, 리셋, 걷고 B (서로 다른 표적)
constexpr bool  RSH_ON = true;
inline constexpr bool RT2_ON = true;   /* RT2: RSH3 기다림 이중 셈 없앰 (기다림은 분모 a 에만) */ constexpr bool  RSH3_ON = true;   // RSH3: 리스폰 그룹 곁 서 있는 R × 이 몸 벌이/R 을 점수 분자에서 뺌 (× 0.001 대신 기회비용)    // RSH: 리스폰 그룹 고르기 점수 = 다시 나올 전체 체력 · 손실로, × 0.001 없음
constexpr bool  TQT_ON = true;    // TQT: 위협이 보여도 '위협 중 죽을 확률' 로 센 바치기 문턱을 넘으면 바치러 감
constexpr float TQT_P = 0.0049f;  // TQT: 위협이 보일 때 R 당 죽을 확률 (래더 v103 40판 측정)
constexpr bool  VSP_ON = true;    // VSP: Varg 튐 평타 — 평타 값에 대상 1칸 둘레 적 · 캠프 값을 더함
constexpr bool  TWS_ON = true;    // TWS: 제단 2칸이면 한 걸음 들어가 같은 턴에 바치기
constexpr bool  DWK_ON = true;    // DWK: 대시 + 걷기 (한 턴 3칸) · 원거리 대시 칸 평타     // FOC: 챔피언 때리기 가치 = 피해 / 남은 체력 (점사)
constexpr bool  FOC2_ON = true;    // FOC2: 목표 값에 이번 R 아군 평타 피해 (처치 마무리)
constexpr bool  FLD_ON = true;     // FLD: 거리장 벽 비트 캐시 (캠프 막힘 버전) — 결과 같음
constexpr bool  SNS_ON = true;     // SNS: 감지 그룹 훑기 = 가까운 그룹 (격자) + 부활 대기 · 만기 표시 그룹만 — 결과 같음
constexpr bool  CHC_ON = true;     // CHC: 바치기 채널 중 닿을 거리 적 힘 합 x 남은 R >= 체력이면 끊음 · 그런 자리에선 시작 안 함
constexpr float PNLD_TOP = 0.75f;   /* HLD: 맨 앞 몸이 다 맞음 (엔진 표적 = 가장 가까운 몸) */
inline constexpr bool HRF_ON = true;   // HRF (I008 사용자 규칙 7): 체력 빠진 몸 — 남은 체력으로 죽지 않고 끝낼 무리는 치고, 원거리는 근거리 팀원이 치는 무리를 뒤에서 지원; 둘 다 없을 때만 회복
inline constexpr bool HLD_ON = true;   // HLD:맡은 무리는 죽지 않는 한 버팀 · 예산 = 체력 − 빠져나갈 몫  // PNLD: 그룹 점수 손실 = 무리 전체 깨어남 · 한 몸이 이 몫을 몰아 맞음 · 넘는 손실은 죽음 비용
constexpr bool  MFB_ON = true;     // MFB: 이동 · 대시 실패 칸을 잠시 막힌 칸으로
constexpr int   MFB_R = 5;
constexpr bool  MRT_ON = true;     // MRT: 숨은 점유로 걷기 실패 → 목표 쪽 옆 칸 한 번 더 (DFB 와 같은 식)
constexpr bool  DFB_ON = true;     // DFB: 대시 실패면 한 칸 걷고 3R 대시 안 고름
constexpr bool  GANK_ON = true;    // GANK: PL 잃을 판돈 x min(1, 적 수 / 우리 수)
constexpr bool  JOIN4_ON = true;   // JOIN4: 팀 싸움 합류 = PL 손익 (알림 구역의 보이는 적, 동료 곁 아군 + 나)
constexpr int   JOIN4_R = 5;
constexpr bool  JOIN5_ON = true;   // JOIN5: 나 없이도 이기거나 비기는 싸움만 합류
constexpr float JOIN_HP = 0.5f;    // JOIN4: 이 체력 비율 이상일 때만 합류
constexpr bool  TH2_ON = true;     // TH2: 다가옴 = 2라운드 추세
constexpr bool  HYS_ON = true;     // HYS: 싸우는 중이면 후퇴 기준 L <= HYS_EXIT
constexpr float HYS_EXIT = 0.667f;   /* X13 */
constexpr bool  CCS_ON = true;     // CCS: 기절 수단이 닿는 칸은 한 턴 더 맞는다고 본다
constexpr long long TGC_SHOP = 5000, TGC_WARD = 8000, TGC_LOCK = 3000, TGC_WRITE = 1500;   // TGC: 턴 끝 관문 = 실측 최대 비용 + 여유
inline constexpr bool CHS_ON = false;   /* CHS */
inline constexpr bool CEF_ON = false;   /* CEF */
inline constexpr bool LHF_ON = true;   /* LHF */
inline constexpr bool BH_F5_ON = true;   /* BH_F5: CO 예외를 CPY 덤 비교와 맞춤 (Prism·Marcella 도) */
inline constexpr bool BH_F3_ON = true;   /* BH_F3: tfpPlan 걸음 표가 덜 찼으면 계획 · solo 캐시 건너뜀 */
inline constexpr bool BH_T2_ON = true;   /* BH_T2: OUT(3) 모드 무관 매 턴 + OUT 아닌 칸 읽으면 outReason 3 지움 */
inline constexpr bool BH2_SP3_ON = true;   // BH2_SP3: 채널 중에도 젬 보고 (gemsRep 만료 막음)
inline constexpr bool BH2_OP4_ON = true;   /* BH2_OP4: 준비 전 걸음 (rawStep · opStep) 도 드래프트 OPD 배정 무리 쪽으로 */
inline constexpr bool BH2_SP1_ON = true;   /* BH2_SP1: Willow 치유 받을 피해를 대상 기준으로 (enemyThreatFor) */
inline constexpr bool BH2_TR5_ON = true;   // BH2 TR5: lockAltar 사거리를 걸은 뒤 지금 칸 (livePosition) 으로 잼
inline constexpr bool BH2_MV5_ON = true;   // BH2 MV5: KT 탱크 표시 바뀜으로 목표 tag 를 올리지 않음 (연료 · gf 대체 경로 · 정체 리셋 낭비 제거; 1순위 바뀜은 FCS)
inline constexpr bool BH3_CE1_ON = false;   // BH3_CE1: MSTEP 평가 안 된 칸도 캠프 피해 (campThreat) 를 더함
inline constexpr bool BH3_CE2_ON = true;   // BH3_CE2: FN 원거리 칸 값도 PL 처럼 사거리 봉투 max(0, 거리-사거리) 로
inline constexpr bool BH3_CE4_ON = true;   /* BH3_CE4: VUL · FN · PEEL · PL 덮어쓰기 뒤 CAE (캠프가 적을 치는 이득) 다시 더함 */
inline constexpr bool BH3_PK1_ON = true;   // BH3_PK1: pfxHomeC 를 집 거리장 판 (g_homeVer) 으로 무효화
inline constexpr bool BH3_PK6_ON = true;   // BH3_PK6: 리스폰 무리 (rsh) 의 mtaSlack · fD 도 다시 나올 전체 캠프로 셈 (hNow · Dm0 와 같은 기준)
inline constexpr bool BH3_PK2_ON = false;   // BH3_PK2: missionWorkTurns(FARM) = 남은 무리 체력 / (내 힘 x 6칸 안 곁 몸 수) — M_FINISH 와 같은 모델
inline constexpr bool BH3_CE3_ON = true;   /* BH3_CE3: CPS 잃는 값 (lose > 0) 이 있으면 MSTEP 도 무리 칸 밖으로 걷지 않음 */
inline constexpr bool BH3_RC1_ON = false;   /* BH3_RC1: 1절 저체력 회복도 후퇴 블록의 남음 예외 (TFV teamStayGain > 0 · FB fbStay) 를 따름 */
inline constexpr bool BH3_TH4_ON = false;   // BH3_TH4: 캠프 예보 fKd 를 CCK (기절 2배) 뒤에 저장 → c.dmg 와 같은 값
inline constexpr bool BH5_IT5_ON = true;   /* BH5_IT5: 리콜 길이 = home 엠블럼이면 3, 아니면 설정 recall_time */
inline constexpr bool BH3_RC2_ON = false;   /* BH3_RC2: 후퇴 아닌 회복이 JNL 로 끝날 때 후퇴 시작 조건 (같은 식) 이 참이면 M_NONE 대신 후퇴 (retreat=true) 로 바꿈 */
inline constexpr bool BH3_TF1_ON = true;   // BH3_TF1: TFP 기다림이 적 점령 (state 2) · 리스폰 중 (state 3 respawnUntil) 도 봄, 목표 유지도 그때 풀림
inline constexpr bool BH3_TF2_ON = true;   // BH3_TF2: TFP 목표 유지로 prev 를 고르면 bestT 도 prev 의 T (g_zkT 창)
inline constexpr bool BH3_TF4_ON = true;   /* BH3_TF4: TFP 멤버에서 다른 살아 있는 무리를 치는 팀원 뺌 (TZD 가 안 옮기는 몸) */
inline constexpr bool BH3_TH5_ON = true;   // BH3_TH5: CXE3 장면은 campThreat 와 CXE3 aggro 가 campHitsMe 하나로 (흡수자 = 보이는 산 몸 전부, 동점은 handle 작은 쪽)
inline constexpr bool BH4_MC6_ON = false;   /* BH4_MC6: 적 Cassi/Issac 처치 값 = 맞은 몸 + 짝 가방 + 짝 죽음 시간, 짝 둘을 한 처치로 셈 */
inline constexpr bool BH4_TN3_ON = true;   // BH4 TN3: 걷기가 숨은 점유로 거절되면 그 턴 옆 칸으로 한 번 더 (MRT)
inline constexpr bool BH4_HR8_ON = false;   // BH4_HR8: 기절 (buff stun) 남은 차례만큼 적 위협 차례 수를 뺌 (막는 차례 1, duration>=2 면 2; Marcella 면역 제외)
inline constexpr bool BH5_CS7_ON = true;   /* BH5_CS7: 리콜 · 리스폰 기다림 비교를 R 로 (걸음 / wkdSpeed) */
inline constexpr bool BH4_VD1_ON = true;   /* BH4_VD1: 첫 평타로 챔피언이 죽으면 리셋 안 씀 */
inline constexpr bool AWJ_ON = true;   // AWJ: 약한 무리 (strMax<=0) 는 먼저 닿는 담당 팀원의 앞선 피해를 hArr 에서 뺌 + 새 담당 바로 알림
inline constexpr bool TM_CAMP_ON = true;   // TM_CAMP: 농사 무리 rate x (땅 계수 + 합류 덤) (팀원 bot34 camp_commitment) — 끄면 땅 걸음장도 안 셈
inline constexpr bool TCX_TERR_ON = false;   /* TCX_TERR: TM_CAMP 땅 계수 끔 (쪼개기 시험) */ inline constexpr float CAMP_ENEMY_PENALTY = TCX_TERR_ON ? 0.f : 0.60f;   // TM_CAMP: 적 쪽 깊이 1 무리 최대 60% 깎음 (팀원 값, 근거 없음)
inline constexpr float CAMP_JOIN_BONUS_PER_ALLY = 0.0f;   // TM_CAMP: 제때 오는 담당 팀원 한 명당 덤 (팀원 값, 근거 없음) — 0 이면 합류 덤 끔
inline constexpr float CAMP_JOIN_BONUS_MAX = 0.20f;   // TM_CAMP: 덤 최대 (팀원 값, 근거 없음)
inline constexpr float CAMP_SHARED_CONFIDENCE = 0.50f;   // TM_CAMP: 안 보이는 팀원 공유 보고 신뢰 (팀원 값, 근거 없음)
inline constexpr bool BG2B_ON = true;   // BG2B: 싸우는 중 (HYS 정의) 라운드는 농사/점령 정체로 세지 않음
inline constexpr bool TF6A_ON = true;   // TF6A: 기절 (HD 착지 · 3c 기절 주문 · CC 남는 칸) 값 = 기절될 적의 다음 차례 위협이 이 칸 비용에서 빠짐 (KFIN · CDM 식)
inline constexpr bool BH3_TD2_ON = true;   // BH3 TD2: fightOdds 에서 팀 싸움 참여 0 (nA==0) 이면 옛 PL_R 국지 셈으로 (0 = 확실히 짐 오판 막음)
inline constexpr bool TM_CAF_ON = true;   /* TM_CAF: 팀원 bot40 — Cassi · Ren 도 샤드를 늘 집중 (Focus) 으로 */
inline constexpr bool FS_COM1_ON = true;   // FS_COM1: 칸 고리의 가림 거리 · 후퇴 걸음 불변식을 턴에 한 번 (결정 같음, 연료만 줄임)
inline constexpr bool FS_COM2_ON = true;   // FS_COM2: fnDps 메모 (같은 입력 비트 → 같은 값, 연료만 줄임)
inline constexpr bool FS_COM3_ON = true;   // FS_COM3: 위협 표를 한 감지 · 한 적 목록에 한 번 (결정 같음, 연료만 줄임)
inline constexpr bool FS_IO1_ON = true;   // FS_IO1: 아이템 조회 기억 (라운드 · 행동 번호 · 핸들) + 0 채우기 84칸만 — 결정 같음
inline constexpr bool FS_IO2_ON = true;   // FS_IO2: BGF 전체 다시 계산 때 같은 비트판 다시 씀 + Field::get 칸 기억 — 결정 같음
inline constexpr bool FS_MIS2_ON = true;   // FS_MIS2: pickFarm 그룹마다 겹친 계산 한 번만 (gsFill 앞당김 · fD 한 번 · PGB 값 다시 씀 · ESA2 팀원 부분 미리) — 답 같음
inline constexpr bool FS_MIS3_ON = true;   // FS_MIS3: threatening() 라운드 메모 (답 같음, 연료만)
inline constexpr bool TBA1_ON = true;   // TBA1: 팀 단위 큰 무리 배정 — 담당된 큰 무리에 필요 인원만큼 가까운 몸이 한 번 옮김 (공유 보기 · 정적 값만, 모든 몸 같은 답)
inline constexpr bool TBA2_ON = true;   // TBA2: 계획 멤버가 곁에 다 올 때까지 무리를 깨우지 않음 (상한 = 멤버 가장 먼 걸음 + OSY_WAIT; 랭커 모여 열기 꼴)
inline constexpr bool TBA_LOG_ON = false;
inline constexpr bool TAC_ON = true;   // TAC: 곁 아군이 위기 (그 칸에서 닿는 적 힘 x 채널 길이 >= 아군 체력, CHC 와 같은 잣대) 면 바치기를 시작·이어가지 않음 — 바치기는 팀원이 여유 있을 때만
inline constexpr bool JHF_ON = true;   // JHF: 곁 싸움에 끼면 이득 (JNL) 이면 새 농사 목표를 고르지 않고 싸움에 남음
inline constexpr bool FBU_ON = true;
inline constexpr float FBU_W = 1.0f;
inline constexpr bool FBT_ON = true;   // FBT: 마무리 남기 (FB) 손실 판돈에 짝 (Cassi · Issac) 의 판돈도 넣음
inline constexpr bool TIE_ON = true;   /* TIE: 캠프 어그로 동거리 = 직전 라운드에 먼저 둔 몸 (priority 높은 쪽 · 같으면 handle 작은 쪽) — 리플레이 5078건 87~96% */
inline constexpr bool TANK_ON = true;   /* TANK: 적 챔피언이 없을 때 곁 몸들을 버틸 체력으로 줄 세워 가장 많은 몸이 먼저 두게 함 = 동거리 캠프 어그로를 받음 (이동 없는 교대) */
inline constexpr bool TNK_STAY_ON = true;   /* TNK: 캠프만 있는 장면에서 캠프가 나를 치지 않으면 (어그로를 받아 줄 아군이 있음) 체력이 낮아도 떠나지 않고 계속 침 */
inline constexpr bool TIE2_ON = true;   /* TIE2: 동거리 어그로 규칙을 일반 장면으로 — 같은 거리의 먼저 두는 아군이 어그로를 받음 (칸 피해 예보 · 넘김 판단) */
inline constexpr bool ATKF_ON = true;   /* ATKF: 원거리는 걷고 나면 그 턴에 못 치므로, 캠프만 있는 장면에서 제자리에서 칠 수 있고 안전하면 (두 턴 예보 피해 < 체력/2) 공격 없는 이동 계획을 고르지 않음 = 딜로스 0 */
inline constexpr bool OPQ_ON = true;   /* OPQ: 위협 없는 농사 이동은 항상 최단 걸음 (계획이 걸음을 안 줄이면 이웃 중 가장 좋은 칸으로) · 목표 칸 모름 + 멈춤은 개막 도우미를 라운드 제한 없이 */
inline constexpr bool RTS_LOG_ON = false;   /* RTS: 후퇴 시작 한 줄 기록 (진단 사본에서만 켬) */
inline constexpr bool KTRC_ON = true; inline constexpr float KTRC_Q = 0.0143f, KTRC_PRIOR_C = 0.067f, KTRC_R0 = 100.f;   // KTRC: 바치기 문턱의 죽을 확률 = 접촉 비율 x 접촉 1턴당 죽음 (측정: 0.0143 · 0.067 · 맵별 상관 r=0.71)   // TBA: 계획 기록 (S0 기록 빌드에서만 켬)
inline constexpr bool FS2_COM1_ON = true;   // FS2_COM1: 3c 기술 고리의 표적 값 · 범위 이웃 · 기절 후보를 (기술, 표적) 마다 한 번 (결정 같음, 연료만)
inline constexpr bool FS2_COM2_ON = true;   // FS2_COM2: 칸마다 kfinRelief 표적당 한 번 · tf6Drop 위협당 한 번 · 가림은 s_fsMC (결정 같음, 연료만)
inline constexpr bool FS2_COM3_ON = true;   // FS2_COM3: 3b 지금 칸 최선 평타를 턴에 한 번 + RST 가 같은 값 다시 씀 (결정 같음, 연료만)
inline constexpr bool FS2_MIS1_ON = true;   // FS2_MIS1: 후퇴 블록 — 쓸 때만 r3Sums/HYS/SOLO, r3Sums 한 번, 싼 거름 먼저, 내 칸 위협 · 합류 한 번 (답 같음, 연료만)
inline constexpr bool FS2_MIS3_ON = true;   // FS2_MIS3: INV 무리 찾기를 x 순 색인으로 (답 같음, 연료만)
inline constexpr bool FS2_MIS4_ON = true;   // FS2_MIS4: pickFarm 고리 불변식 (wkdSpeed · rshRho · SQE · spV) 밖으로 (답 같음, 연료만)
inline constexpr bool RWX_ON = true;   /* RWX: 도착 때 아직 리스폰 중인 무리는 다른 후보가 없을 때만 */
inline constexpr bool LZD_ON = true;   /* LZD: 대시 칸은 걷기 칸 평가 뒤에 만듦 (연료 모자라면 기본 칸 먼저) */
inline constexpr bool CRS_ON = true;   /* CRS: 적 곁 턴은 전투 몫을 먼저 떼고 임무 선택 작업 판단 */
inline constexpr bool R3C_ON = true;   /* R3C: r3Sums 를 감지마다 한 번 (같은 결정) */
inline constexpr bool ZOR_ON = true;   /* ZOR: 조용한 턴 임무 예약 = 잰 전투 + 실행 + 뒤 작업 비용 */
inline constexpr bool CXM_ON = true;   /* CXM: 후퇴 중 캠프 사거리 벗어나기 값을 적이 있어도 */
inline constexpr int FIB_MODE = 1;   /* FIB: 1 = 상위 팀 아이템 순서 그대로 · 2 = 주 스탯 3개 뒤 Bruiser 체력 → Durability 체력 · 0 = 끔 */ inline constexpr bool KPR_ON = false;   /* KPR: 무리 값 × 그 위치에서 지킨 비율 (래더 측정) */ inline constexpr bool DTR_ON = true;   /* DTR: 내 죽음 시간 값 × max(1, 내 벌이 / 사전 벌이) */ inline constexpr bool IDC_ON = true;   /* IDC: 적이 보여도 목표 걸음 값 (mv) 을 깎지 않음 — 위험은 임무 후퇴 · 칸 위협비가 셈 */ inline constexpr bool TWH_ON = true;   /* TWH: 쌍둥이 (Cassi 3 · Issac 4) 체력 아이템 먼저 (Bruiser 체력 → Durability 체력) */ inline constexpr bool BLD2_ON = true;   // BLD2: 근접 아이템 = 랭커 순서 (공격 스탯)
inline constexpr bool IPM_ON = true;   // IPM: Vesper 첫 엠블럼 Monster (0/1) → bruiser 힘 → capture → 나머지 (끄면 capture 먼저)
inline constexpr bool PRA_ON = true;   // PRA: 턴 끝 리콜 걸어 두기 · 다음 턴 v331 리콜 결정으로 두거나 취소 · 막 건 리콜은 채널 아님
inline constexpr bool SITEL_ON = false;   // SITEL: 사건 때만 — 제단 도움 요청에 값으로 합세 (가까운 need 몸), CJN 은 값 비교로
inline constexpr bool SAFEP_ON = true;   // SAFEP: 안전한 칸 (예보 피해 0) 에서는 언제나 적 챔피언에게 기술
constexpr bool  LFR_ON = true;     // LFR: 지금 칸 적 피해 예보 (combat 과 같은 식) >= 체력 x LFR_FRAC 이면 후퇴 시작
constexpr float LFR_FRAC = 0.45f;
constexpr bool  VIS_ON = true;     // VIS: 보이기만 한 적은 농사를 깎지 않음 (위협 적만)
constexpr bool  CXE_ON = true;     // CXE: 캠프 싸움 — 들어오는 피해로 떠날 때, 회복 땐 사거리부터 벗어남. CXE3: 넘길 동료가 있으면 집 대신 가장 가깝지 않은 칸으로 물러나 계속
constexpr bool  SPH_ON = true;     // SPH: 스폰 위에서 국지 열세면 스폰 칸 밖으로 나가지 않음 (스폰은 턴마다 가득 회복)
constexpr bool  UNL_ON = true;     // UNL: 제단이 모두 막히면 여는 모노리스 값 += 팀이 든 젬
constexpr float VUL_AGGRO_PEN = 20.f;   // VUL: 기다림 중 내가 캠프 어그로 대상이 되는 칸 (캠프 피해 계산은 따로 c.dmg 에)
constexpr bool  HD_ON = true;      // HD: HoR 대시 착지 피해를 공격으로
constexpr bool  NF_ON = false;      // NF: 남 좋은 일 안 하기 — 적이 NF_R 안이고 후퇴 잣대 (r3Ratio) 로 열세면 캠프를 치지 않는다
constexpr int   NF_R = 5;          // NF: 적 챔피언이 이 거리 안이면 판단 (평소 사냥은 그대로)
constexpr float PLV_DEATH = 22.0f;   // PLV: 죽음 시간 값 (부활 60R + 복귀 ~20R) × 몸-라운드 0.25 젬
constexpr bool  PLV_OPP = true;   // PLV: 농사 기회비용 (몸 수 차 × 싸움 길이 × 몸-라운드 값)
constexpr float PLV_T = 10.f, PLV_V = 0.25f;
constexpr float PL_MARGIN = 10.0f;
constexpr bool  TH_ON = true;      // TH: 후퇴는 닿을 수 있거나 다가오는 적만 센다
constexpr float RETREAT_HP = 0.4f;         // 적이 보일 때 이 아래면 임무를 내려놓고 물러난다 (스폰 쪽 · 적에서 멀어지는 칸)
constexpr int   RETREAT_ENEMY_R = 7;       // 적을 세는 거리
constexpr int   RETREAT_ALLY_R = 6;        // 아군을 세는 거리
constexpr float RETREAT_RESUME_HP = 0.6f;  // 적이 2R 안 보이고 체력 ≥ 이 비율이면 후퇴를 끝내고 임무로 (회복 끝 기준도 같음)

// --- R2 (O7): 수적 열세 (R3 힘 비교) 이고 지금 칸이 적 사거리 밖이면, 적 피해가 예상되는 칸으로 들어가지 않는다 (임무 진행값이 아무리 커도)
constexpr bool  R2_NO_ENTER = true;

// --- R3 (O7): 열세 = '힘' (체력 × 힘) 비교 — 아이템 · 체력 격차가 큰 적 (2000 체력) 은 여럿으로 센다 (mission::r3Outnumbered · r3Ratio)
constexpr float R3_RATIO = 2.0f;   /* X6 */   // RT1: 적 힘 합 ≥ 우리 힘 합 × 이 값이면 열세

// --- 그 밖 (10-19 이후)
constexpr bool  NFX_ON = true;            // NFX: 걸어서 당장 못 먹을 캠프는 치지 않음
constexpr bool  DPC_ON = true;            // DPC: 쫓기 · 싸움 손익의 적 전력을 적 진영 깊이로 키움
constexpr float DPC_K = 1.0f;
constexpr bool  KFIN_ON = true;           // KFIN: 죽일 적의 위협은 그 칸 비용에서 뺌 (마무리 한 방)
constexpr bool  UNR_ON = true;            // UNR: 걸어서 못 가는 그룹 기억 (고르기 연료)
constexpr bool  GLM_ON = true;            // GLM: 공격 칸에 비어 있는 스폰 칸 포함 · 설 칸 없는 농사 목표 포기 (골렘 둘레 멈춤)
constexpr bool  PSW_ON = false;            // PSW: 팀원 담당이 가장 많은 그룹 (주 무리 목표) 으로 돌아가기
constexpr float PSW_MIN_HP = 500.f;
constexpr int   PSW_MAX = 40;
inline constexpr int PSW_R = 60;   // OPN: PSW 를 개막 60R 에만
constexpr float PSW_LEFT = 0.15f;
constexpr bool  PSW_STRONG = true;        // PSW3: 무리 목표는 힘 있는 캠프가 있는 그룹만
constexpr bool  FCS_ON = true;            // FCS: 그룹 안 화력 집중 (힘/남은 체력 1순위 캠프, 암묵적 합의)
inline constexpr bool ESA2_ON = true; constexpr float ESA_DRISK = 0.00196f;   /* ESA2: 적 진영 혼자 팀원 곁으로 (래더 잰 죽음률 차) */
constexpr bool  RGP_ON = false;            // RGP: 수에 밀린 후퇴 (체력 >= RETREAT_HP) 는 집 대신 RGP_R 안 아군 무리 쪽으로
constexpr int   RGP_R = 12;
constexpr bool  STF_ON = true;            // STF: 함께 치는 아군이 있으면 회복 판단에서 캠프 피해를 나눠 셈 (끝까지 함께)
constexpr bool  HLP_ON = true;            // HLP2: 바치기 미루기 — 팀 젬 기준일 때도 GR, HLP_R 칸 안 팀원 담당 그룹을 내가 합류해 아끼는 팀 몸-R > 내가 쓰는 몸-R 일 때만 함께
constexpr int   HLP_R = 8;
inline constexpr bool KTRQ_ON = true; inline constexpr float KTRQ_PRIOR_RATE = 0.14f, KTRQ_LOSS = 2.f;   // KTRQ: TRQ 혼자 문턱 trqG 의 벌이 = (번 젬 + 0.14 x 300) / (산 R + 300) (래더 153판 실측 0.143 젬/몸R; KB 사전 0.5 는 2.5배 부풀림), 죽을 확률 x 2 (가방이 킬러에게: 우리 -g 적 +g). TQT · ES2 (kbG) 는 그대로
constexpr bool  TRQ_ON = true;            // TRQ: 바치기 · 욕심쟁이 미루기 기준 = g* = 벌이 √(왕복 / 죽을확률) (왕복 = 2 x 제단 걸음 + 엔진 바치기 채널, 상한 벌이 / (2 죽을확률))
constexpr float CB_SOFT = 0.3f;           // CB3: 혼자 끝내지 못할 그룹 점수 배율 (0 = 금지)

// --- 기록
constexpr int   FUEL_LOG_EVERY = 50;

constexpr bool MJG_ON = true;
constexpr int MJG_ARRIVE = 3;
constexpr bool CPS_ON = true;   /* CPS: 보상 대기 무리 칸 = 점령 · 막기 자리 (비키기 벌점 없음 · 떠나면 잃는 값 · 적 점령 칸에 서면 막는 값) */
constexpr bool  RAL_ON = true;   /* RAL: 채널 중 (리콜 · 바치기 · 순간이동) 아군은 싸움 힘 합 (r3Sums · teamFight) 에서 뺀다 — MJG 와 같은 거름 */
// --- 7bug: bugs_v197.md 의 고침 스위치 (끄면 v197 과 같은 동작)
constexpr float FB_W = 3.0f;   /* FB: 위험 무게 (과감함 정도 시험: 0.5 · 1 · 2) */
constexpr bool  BUG1_ON = false;   // BUG1: threatening() 이전 거리는 바로 전 · 전전 라운드 값일 때만
constexpr bool  BUG2_ON = false;   // BUG2: 대시 칸에는 리셋 두 방 값을 넣지 않음
constexpr bool  BUG3_ON = true;   // BUG3: 팀 상태 칸에 늘 1 인 비트 (구역 0 이 빈 칸으로 버려짐)
constexpr bool  BUG4_ON = true;   // BUG4: FN 일반 가지에서 적 스폰 위 적 거름
constexpr bool  BUG5_ON = true;   // BUG5: farmable 인원 상한 = 팀 인원
constexpr bool  BUG6_ON = false;   // BUG6: occStamp 지난 라운드 칸 지우기
constexpr bool  BUG7_ON = true;   // BUG7: UNR 표시에 campVersion 을 남겨 바뀌면 다시 묻기
constexpr bool  BUGX_ON = true;   // BUGX: 대시한 턴에 실드 안 시전 · name() 에 escort
constexpr bool  RALO_ON = false;
constexpr bool  CCD_ON = true;   /* CCD: 점령 임무 중 캠프 피해 — 다른 점령 칸으로 옮기는 값 · 끝내기 전에 죽는 계획이면 사거리 밖으로 */
constexpr float CCD_W = 1.0f;    /* CCD: 위험 무게 (죽음 값에 곱함, 0 = v201 과 같음) */   /* RALO: 팀 칸이 회복 임무 (M_RECOVER) 라고 알린 아군도 뺀다 (K_OUT 쓰는 주기 3R 안) */
inline constexpr bool TERR_ON = true;   /* TERR (territory.md T2+T3): 되살아남 사건 팀 공유 (K_OUT 이유 0 + 무리+1) · pickFarm V x pOwn (늦으면 빼앗김, 래더 210판 terr3b 깊이 띠 표) */
}  // namespace nb::policy

// MJG (2026-10-25): personal marginal support; existing PL decisions preserved.
