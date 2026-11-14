#pragma once
#include <algorithm>
// MJG: all values are gems, HP, HP/round, or rounds; s is a force index,
// not an empirically calibrated probability. No mutation, SDK calls or latches.
namespace nb::combat::marginal_join {
struct Input {
    float allyHp, allyDps, enemyHp, enemyDps, depth;
    float allyStake, enemyStake, myHp, myDps, myStake;
    int eta, maxEta;
    float horizon, farmPerRound;
};
struct Result { bool eligible = false; float gain = 0.f, before = 0.f, after = 0.f; };
inline Result evaluate(const Input& x) {
    Result r;
    if (x.eta < 0 || x.eta > x.maxEta || x.allyHp <= 0.f || x.allyDps <= 0.f
        || x.enemyHp <= 0.f || x.enemyDps <= 0.f || x.myHp <= 0.f || x.myDps <= 0.f) return r;
    // Do not count the traveler as present before arrival. Reject expired fights.
    const float a = x.allyHp - x.enemyDps * (float)x.eta;
    const float e = x.enemyHp - x.allyDps * (float)x.eta;
    if (a <= 0.f || e <= 0.f) return r;
    const float enemy = e * x.enemyDps * std::max(1.f, x.depth);
    const float p0 = a * x.allyDps;
    const float p1 = (a + x.myHp) * (x.allyDps + x.myDps);
    r.before = p0 / std::max(1.f, p0 + enemy);
    r.after = p1 / std::max(1.f, p1 + enemy);
    const float duration = std::min(x.horizon, std::max(1.f, e / (x.allyDps + x.myDps)));
    const float opportunity = (2.f * (float)x.eta + duration) * x.farmPerRound;
    r.gain = (r.after - r.before) * (x.enemyStake + x.allyStake)
             - (1.f - r.after) * x.myStake - opportunity;
    r.eligible = true;
    return r;
}
} // namespace nb::combat::marginal_join
