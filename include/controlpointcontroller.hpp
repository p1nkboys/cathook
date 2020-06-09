#pragma once

#include <entitycache.hpp>

#define MAX_CONTROL_POINTS 8
#define MAX_PREVIOUS_POINTS 3
struct cp_info
{
    // Index in the ObjectiveResource
    int cp_index{ -1 };
    std::optional<Vector> position;
    // For BLU and RED to show who can and who cannnot cap
    std::array<bool, 2> can_cap{};
    cp_info(){};
};

namespace cpcontroller
{
// Get the closest Control Point we can cap
std::optional<Vector> getClosestControlPoint(Vector source, int team);
} // namespace cpcontroller
