#pragma once
#include "world.h"
#include "combat.h"
namespace nb::special {
int teamCarried(const World&w);
bool prismCanBank(const World&w);
int willowSupportGroup(const World&w);
void planHeal(const World&w,combat::Plan&p);
bool healNow(const World&w);
int willowNextPriority(const World&w);
}
