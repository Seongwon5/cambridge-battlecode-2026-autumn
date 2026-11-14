#pragma once
#include <cambc.h>
#include "draft_features.h"
namespace nb::reference {
inline std::vector<int> sdkDraftDistance(int W,int H,const Bytes& blocked,const Bytes& spawn){
    const int n=W*H;Bytes packed((n+7)/8,0),root=spawn;root.resize(packed.size());
    for(int i=0;i<n;++i)if(blocked[i])packed[(size_t)i>>3]|=(std::uint8_t)(1u<<(i&7));
    for(size_t i=0;i<root.size();++i)root[i]&=(std::uint8_t)~packed[i];
    cambc::bitboard walls{cambc::uninitialized},roots{cambc::uninitialized},visited{cambc::uninitialized};
    walls.import_linear_buffer(std::span<const std::uint8_t>(packed.data(),packed.size()));
    roots.import_linear_buffer(std::span<const std::uint8_t>(root.data(),root.size()));visited.clear();
    cambc::distance_field field;field.compute(walls,roots,visited);
    std::vector<std::uint16_t>raw(n);field.export_linear_buffer(std::span<std::uint16_t>(raw.data(),raw.size()));
    std::vector<int>out(n);for(int i=0;i<n;++i)out[i]=raw[i]==65535?-1:raw[i];return out;
}
}
