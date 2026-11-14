#pragma once
// log.h — 기록. NB_LOG 가 정의된 빌드만 턴마다 "#nb" 한 줄 (왜 진행이 안 됐나). "#fuel" 은 늘 (50R 마다 몸마다 한 줄).
#include <cstdio>
#include <cstdarg>

namespace nb::log {

#ifdef NB_LOG
constexpr bool ON = true;
#else
constexpr bool ON = false;
#endif

inline void line(const char* fmt, ...) {
    if (!ON) return;
    va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap); std::printf("\n");
}
inline void always(const char* fmt, ...) { va_list ap; va_start(ap, fmt); std::vprintf(fmt, ap); va_end(ap); std::printf("\n"); }

struct FuelStat { long long sum = 0, mx = 0; int n = 0; void add(long long v) { sum += v; if (v > mx) mx = v; ++n; } void reset() { sum = mx = 0; n = 0; } };

}  // namespace nb::log

// 비활성 로그는 인수의 거리 조회까지 생략한다.
#define NB_LINE(...) do { if constexpr (::nb::log::ON) ::nb::log::line(__VA_ARGS__); } while (false)
