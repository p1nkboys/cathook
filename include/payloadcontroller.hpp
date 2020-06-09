#pragma once

#include <entitycache.hpp>

struct pl_info
{
    CachedEntity *ent;
    std::optional<Vector> position;
    pl_info(){};
};

namespace plcontroller
{
// Get the closest Control Payload
std::optional<Vector> getClosestPayload(Vector source, int team);
} // namespace plcontroller
