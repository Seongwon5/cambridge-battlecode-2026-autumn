#pragma once
#include <algorithm>
namespace nb::gem_report {
// Six data bits: preserve four-gem precision below128, then expand the range.
// All decoded values are lower bounds, capped above the winning threshold.
constexpr int pack(int gems){return gems<128?std::clamp(gems/4,0,31):gems<384?32+(gems-128)/16:48+std::min(15,(gems-384)/64);}
constexpr int unpack(int value){value&=63;return value<32?value*4:value<48?128+(value-32)*16:384+(value-48)*64;}
constexpr int epoch(int round){return (round/3)&1;}
constexpr int subtype(int round){return epoch(round)?0:2;}
constexpr int reportRound(int now,int subtype){if(now<2)return -1000;const int latest=now-(now+1)%3;return latest-(epoch(latest)!=(subtype==0)?3:0);}
}
