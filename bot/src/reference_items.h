#pragma once
#include <vector>
namespace nb::reference {
struct ItemStep {int shard,emblems;};
inline void itemPlan(int type,int flags,std::vector<ItemStep>&s,int&tail,int heldGems=0){s.clear();const bool monster=flags&1;
    // One early health emblem protects both bodies from Cassi's linked death.
    // The subsequent Focus and sustained Health path follow the reference.
    if(type==3&&!monster){
        s={{40,1},{8,1},{14,1}};
        tail=40;return;
    }
    // Early Capture fits H4's Vesper role; then retain the reference path.
    if(type==1){
        if(monster){s={{48,1}};s.push_back({18,1});s.push_back({0,1});s.push_back({28,1});tail=6;return;}
        s={{48,1},{18,1},{24,1},{54,1},{16,1}};
        tail=36;return;
    }
    // In every observed shop visit, Tribute has priority at 500 carried gems.
    // It interrupts any ordinary stage, including an unfinished first emblem.
    if(type==6){if(heldGems>=500)s.push_back({46,1});s.push_back({16,1});s.push_back({58,1});s.push_back({28,1});tail=10;return;}
    if(type==0 && monster==false) { s.push_back({40,1}); s.push_back({16,1}); tail=6; return; } // Ryth: 00crater
    if(type==2 && monster==false) { s.push_back({22,1}); s.push_back({24,1}); s.push_back({40,1}); s.push_back({16,1}); tail=36; return; } // Vendetta: 00pilgrimage
    if(type==4 && monster==false) { s.push_back({18,1}); s.push_back({6,1}); s.push_back({34,1}); s.push_back({16,1}); tail=6; return; } // Issac: 01yellow_brick_road
    if(type==7 && monster==false) { s.push_back({54,1}); s.push_back({14,1}); s.push_back({28,1}); s.push_back({36,1}); s.push_back({38,1}); tail=40; return; } // Marcella: 01the_farm
    if(type==8 && monster==false) { s.push_back({18,1}); s.push_back({6,1}); s.push_back({34,1}); tail=6; return; } // Willow: 00vault
    if(type==9 && monster==false) { s.push_back({40,1}); s.push_back({16,1}); tail=8; return; } // Ren: 00ribs
    if(type==12 && monster==false) { s.push_back({22,1}); s.push_back({26,1}); tail=40; return; } // Heart of Ruin: 01yellow_brick_road
    if(type==2 && monster==true) { s.push_back({0,1}); s.push_back({28,1}); tail=6; return; } // Vendetta: 00rows
    if(type==3 && monster==true) { s.push_back({2,1}); s.push_back({28,1}); tail=8; return; } // Cassi: 00ambush
    if(type==4 && monster==true) { s.push_back({0,1}); s.push_back({28,1}); tail=6; return; } // Issac: 01big_world
    if(type==7 && monster==true) { s.push_back({54,1}); s.push_back({14,1}); s.push_back({28,1}); s.push_back({36,1}); s.push_back({38,1}); tail=40; return; } // Marcella: 00rows
    if(type==10 && monster==true) { s.push_back({0,1}); s.push_back({28,1}); tail=6; return; } // Varg: 01islands
    if(type==12 && monster==true) { s.push_back({2,1}); s.push_back({28,1}); tail=8; return; } // Heart of Ruin: 00expanse
    // Unobserved heroes keep a simple stat build as a legal fallback.
    s={{type==5?2:0,1},{28,1}};tail=type==5?8:6;
}
}
