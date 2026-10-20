#include "modules.h"
#include <algorithm>

namespace { bool enabled{true},collision{true},visuals{true}; }

namespace ysmir::true_cone
{
    void Initialize()
    {
        enabled=config::Bool("TrueCone","Enabled",true);
        collision=config::Bool("TrueCone","PatchRuntimeCollision",true);
        visuals=config::Bool("TrueCone","PatchVisuals",true);
    }
    bool PatchVisuals() { return enabled && visuals; }
    Geometry Capture(RE::ConeProjectile* projectile)
    {
        // The shared progression core leaves a cone's base radius unchanged.
        // Reading it also avoids treating a restored runtime radius as a new baseline.
        return {std::max(0.f,projectile->GetProjectileBase()->data.collisionRadius)};
    }
    void ApplyCollision(RE::ConeProjectile* projectile,const Geometry& g,const progression::ScalingSnapshot& s)
    {
        if (!enabled || !collision) return;
        // Preserve the authored cone angle. The native update owns the bhk wrapper,
        // its Havok sphere and game-to-Havok unit conversion.
        projectile->GetConeRuntimeData().initialCollisionSphereRadius=g.radius*s.collision;
    }
}
