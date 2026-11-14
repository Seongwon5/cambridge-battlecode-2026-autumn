#pragma once
// camp_commitment.h - TM_CAMP (팀원 bot34 camp_commitment.h 를 TM_CAMP_ON 깃발로 옮김)
#include <algorithm>
#include "policy.h"

namespace nb::camp_commitment {
inline float territory(float ownDistance, float enemyDistance) {
    if (!policy::TM_CAMP_ON || ownDistance < 0.f || enemyDistance < 0.f) return 1.f;
    if (ownDistance >= 65535.f) return enemyDistance >= 65535.f ? 1.f : 1.f - policy::CAMP_ENEMY_PENALTY;
    if (enemyDistance >= 65535.f) return 1.f;
    const float depth = std::clamp((ownDistance - enemyDistance) /
        std::max(1.f, ownDistance + enemyDistance), 0.f, 1.f);
    return 1.f - policy::CAMP_ENEMY_PENALTY * depth;
}
// 거리는 '제때 오나' 만 본다. 다른 목표의 가까운 팀원은 덤 0, 제때 오는 먼 팀원은 가까운 팀원과 같은 덤.
inline float readiness(int eta, int myArrival, int uncertainty = 0) {
    if (eta < 0 || eta >= 65535 || myArrival < 0 || myArrival >= 65535) return 0.f;
    const int delay = eta + std::max(0, uncertainty) - myArrival;
    if (delay <= policy::MTA_SLACK) return 1.f;
    return std::clamp((float)(policy::OSY_WAIT - delay) /
        (float)std::max(1, policy::OSY_WAIT - policy::MTA_SLACK), 0.f, 1.f);
}
inline float bonus(float allies) {
    if (!policy::TM_CAMP_ON) return 0.f;
    return std::min(policy::CAMP_JOIN_BONUS_MAX,
        policy::CAMP_JOIN_BONUS_PER_ALLY * std::max(0.f, allies));
}
inline float factor(float territoryFactor, float allies) {
    return policy::TM_CAMP_ON ? territoryFactor + bonus(allies) : 1.f;
}
inline float maxFactor() {
    return policy::TM_CAMP_ON ? 1.f + policy::CAMP_JOIN_BONUS_MAX : 1.f;
}
// 음수 점수는 깎아도 더 좋아지면 안 됨. 덤은 이득 나는 고르기에만.
inline float apply(float rawRate, float territoryFactor, float allies) {
    if (!policy::TM_CAMP_ON || rawRate <= 0.f) return rawRate;
    return rawRate * territoryFactor + rawRate * bonus(allies);
}
}
