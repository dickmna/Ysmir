#include "modules.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
    bool enabled{true}, collision{true}, visuals{true}, angleVisuals{true};
    float Tangent(float degrees) { return std::tan(degrees*std::numbers::pi_v<float>/180.f); }
}

namespace ysmir::true_cone
{
    void Initialize()
    {
        enabled=config::Bool("TrueCone","Enabled",true);
        collision=config::Bool("TrueCone","PatchRuntimeCollision",true);
        visuals=config::Bool("TrueCone","PatchVisuals",true);
        angleVisuals=config::Bool("TrueCone","DistanceVisualScale",true);
    }
    bool PatchVisuals() { return enabled && visuals; }
    Geometry Capture(RE::ConeProjectile* projectile)
    {
        const auto& data=projectile->GetConeRuntimeData();
        const auto& base=projectile->GetProjectileBase()->data;
        const float degrees=std::max(0.f,base.coneSpread);
        Geometry g;
        g.radius=std::max(0.f,data.initialCollisionSphereRadius>0 ? data.initialCollisionSphereRadius : base.collisionRadius);
        g.tangent=std::max(0.f,data.coneAngleTangent>0 ? data.coneAngleTangent : Tangent(degrees*.5f));
        // Preserve the runtime's half/full-angle convention and wider authored cones.
        const bool full=degrees<=0 || std::abs(g.tangent-Tangent(degrees))<std::abs(g.tangent-Tangent(degrees*.5f));
        const auto convention=full ? 1.f : .5f;
        const auto original=Tangent(degrees*convention);
        const auto target=Tangent(std::max(15.f,degrees)*convention);
        g.correctedTangent=std::max(g.tangent,original>0 ? g.tangent*target/original : target);
        return g;
    }
    void ApplyCollision(RE::ConeProjectile* projectile,const Geometry& g,const progression::ScalingSnapshot& s)
    {
        if (!enabled || !collision) return;
        auto& data=projectile->GetConeRuntimeData();
        data.initialCollisionSphereRadius=g.radius*s.collision;
        data.coneAngleTangent=g.correctedTangent*s.collision/s.distance;
        if (data.collisionShape)
            data.collisionShape->radius=std::max(data.collisionShape->radius,data.initialCollisionSphereRadius);
    }
    float VisualScale(RE::ConeProjectile* projectile,const Geometry& g,const progression::ScalingSnapshot& s)
    {
        if (!PatchVisuals()) return 1;
        if (!angleVisuals || !collision) return s.vfx;
        // Compare envelopes at equal animation progress, not scaled travel distance.
        const auto u=std::max(0.f,projectile->GetProjectileRuntimeData().distanceMoved)/s.distance;
        const auto original=g.radius+u*g.tangent;
        return s.vfx*(original>0 ? (g.radius+u*g.correctedTangent)/original : 1.f);
    }
}
