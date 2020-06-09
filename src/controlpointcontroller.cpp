#include "common.hpp"
#include "controlpointcontroller.hpp"
namespace cpcontroller
{

std::array<cp_info, MAX_CONTROL_POINTS> controlpoint_data;
CachedEntity *objective_resource = nullptr;
bool is_cp                       = true;

// This function updates the Entity used for the Object resource
void UpdateObjectiveResource()
{
    // Already set and valid
    if (CE_GOOD(objective_resource) && objective_resource->m_iClassID() == CL_CLASS(CTFObjectiveResource))
        return;
    // Find ObjectiveResource
    for (int i = g_IEngine->GetMaxClients() + 1; i < MAX_ENTITIES; i++)
    {
        CachedEntity *ent = ENTITY(i);
        if (CE_BAD(ent) || ent->m_iClassID() != CL_CLASS(CTFObjectiveResource))
        {
            // Not a CP map
            if (CE_GOOD(ent) && ent->m_iClassID() == CL_CLASS(CFuncTrackTrain))
                is_cp = false;
            continue;
        }
        // Found it
        objective_resource = ent;
    }
}

// A Bunch of defines for netvars that don't deserve their own function
#define GET_NUM_CONTROL_POINTS() (CE_INT(objective_resource, netvar.m_iNumControlPoints))
#define GET_OWNING_TEAM(index) ((&CE_INT(objective_resource, netvar.m_iOwningTeam))[index])
#define GET_BASE_CONTROL_POINT_FOR_TEAM(team) ((&CE_INT(objective_resource, netvar.m_iBaseControlPoints))[team])
#define GET_CP_LOCKED(index) ((&CE_VAR(objective_resource, netvar.m_bCPLocked, bool))[index])
#define IN_MINI_ROUND(index) ((&CE_VAR(objective_resource, netvar.m_bInMiniRound, bool))[index])

int GetPreviousPointForPoint(int index, int team, int previndex)
{
    int iIntIndex = previndex + (index * MAX_PREVIOUS_POINTS) + (team * MAX_CONTROL_POINTS * MAX_PREVIOUS_POINTS);
    return (&CE_INT(objective_resource, netvar.m_iPreviousPoints))[iIntIndex];
}

bool GetFarthestOwnedControlPoint(int team)
{
    int iOwnedEnd = GET_BASE_CONTROL_POINT_FOR_TEAM(team);
    if (iOwnedEnd == -1)
        return -1;

    int iNumControlPoints = GET_NUM_CONTROL_POINTS();
    int iWalk             = 1;
    int iEnemyEnd         = iNumControlPoints - 1;
    if (iOwnedEnd != 0)
    {
        iWalk     = -1;
        iEnemyEnd = 0;
    }

    // Walk towards the other side, and find the farthest owned point
    int iFarthestPoint = iOwnedEnd;
    for (int iPoint = iOwnedEnd; iPoint != iEnemyEnd; iPoint += iWalk)
    {
        // If we've hit a point we don't own, we're done
        if (GET_OWNING_TEAM(iPoint) != team)
            break;

        iFarthestPoint = iPoint;
    }

    return iFarthestPoint;
}

// Can we cap this point?
bool isPointUseable(int index, int team)
{
    static auto tf_caplinear = g_ICvar->FindVar("tf_caplinear");
    if (tf_caplinear && !tf_caplinear->GetBool())
        return true;

    // We Own it, can't cap it
    if (GET_OWNING_TEAM(index) == team)
        return false;

    // We are playing a sectioned map, check if the CP is in it
    if (CE_VAR(objective_resource, netvar.m_bPlayingMiniRounds, bool) && !IN_MINI_ROUND(index))
        return false;

    // Any previous points necessary?
    int iPointNeeded = GetPreviousPointForPoint(index, team, 0);

    // Points set to require themselves are always cappable
    if (iPointNeeded == index)
        return true;

    // Is the point locked?
    if (GET_CP_LOCKED(index))
        return false;

    // No required points specified? Require all previous points.
    if (iPointNeeded == -1)
    {
        // No Mini rounds
        if (!CE_VAR(objective_resource, netvar.m_bPlayingMiniRounds, bool))
        {
            // No custom previous point, team must own all previous points
            int iFarthestPoint = GetFarthestOwnedControlPoint(team);
            return (abs(iFarthestPoint - index) <= 1);
        }
        // We got a section map
        else
        {
            // Tf2 itself does not seem to have any more code for this, so here goes
            return true;
        }
    }

    // Loop through each previous point and see if the team owns it
    for (int iPrevPoint = 0; iPrevPoint < MAX_PREVIOUS_POINTS; iPrevPoint++)
    {
        iPointNeeded = GetPreviousPointForPoint(index, team, iPrevPoint);
        if (iPointNeeded != -1)
        {
            // We don't own the needed points
            if (GET_OWNING_TEAM(iPointNeeded) != team)
                return false;
        }
    }
    return true;
}

// Don't constantly update the cap status
static Timer capstatus_update{};
// Update the control points
void UpdateControlPoints()
{
    // No objective ressource, can't run
    if (!objective_resource)
        return;

    int num_cp = CE_INT(objective_resource, netvar.m_iNumControlPoints);
    // No control points
    if (!num_cp)
        return;
    // Clear the invalid controlpoints
    for (int i = num_cp - 1; i < MAX_CONTROL_POINTS; i++)
        controlpoint_data.at(i) = cp_info();

    for (int i = 0; i < num_cp; i++)
    {
        auto &data    = controlpoint_data.at(i);
        data.cp_index = i;

        // Update position (m_vCPPositions[index])
        data.position = (&CE_VAR(objective_resource, netvar.m_vCPPositions, Vector))[i];
    }

    if (capstatus_update.test_and_set(1000))
        for (int i = 0; i < num_cp; i++)
        {
            auto &data = controlpoint_data.at(i);
            // Check accessibility for both teams, requires alot of checks
            data.can_cap.at(0) = isPointUseable(i, TEAM_RED);
            data.can_cap.at(1) = isPointUseable(i, TEAM_BLU);
        }
}

// Get the closest controlpoint to cap
std::optional<Vector> getClosestControlPoint(Vector source, int team)
{
    // No resource for it
    if (CE_BAD(objective_resource))
        return std::nullopt;
    // Not a controll point map
    if (!is_cp)
        return std::nullopt;
    // Map team to 0-1 and check If Valid
    int team_idx = team - TEAM_RED;
    if (team_idx < 0 || team_idx > 1)
        return std::nullopt;

    // No controlpoints
    if (!GET_NUM_CONTROL_POINTS())
        return std::nullopt;

    // Find the best and closest control point
    std::optional<Vector> best_cp;
    float best_distance = FLT_MAX;
    for (auto &cp : controlpoint_data)
    {
        // They can cap
        if (cp.can_cap.at(team_idx))
        {
            // Is it closer?
            if (cp.position && (*cp.position).DistTo(source) < best_distance)
            {
                best_distance = (*cp.position).DistTo(source);
                best_cp       = cp.position;
            }
        }
    }

    return best_cp;
}

void LevelInit()
{
    for (auto &cp : controlpoint_data)
        cp = cp_info();
    objective_resource = nullptr;
    is_cp              = true;
}

static InitRoutine init([]() {
    EC::Register(EC::CreateMove, UpdateObjectiveResource, "cpcontroller_updateent");
    EC::Register(EC::CreateMove, UpdateControlPoints, "cpcontroller_updatecp");
    EC::Register(EC::LevelInit, LevelInit, "levelinit_cocontroller");
});
} // namespace cpcontroller
