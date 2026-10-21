#include "modules.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace { bool visuals{true}; float transitionDistance{256.f}; }

namespace ysmir::true_cone
{
    void Initialize()
    {
        visuals=config::Bool("TrueCone","Enabled",true) && config::Bool("TrueCone","PatchVisuals",true) &&
            config::Bool("DragonSouls","bScaleVFX",true);
        transitionDistance=std::max(1.f,config::Float("TrueCone","fVisualTransitionDistance",256.f));
    }
    bool PatchVisuals() { return visuals; }
    float VisualScale(RE::ConeProjectile* projectile,const progression::ScalingSnapshot& snapshot)
    {
        if (!visuals) return 1.f;
        const double d=projectile->GetProjectileRuntimeData().distanceMoved;
        const auto& cone=projectile->GetConeRuntimeData();
        const double r=cone.initialCollisionSphereRadius,t=cone.coneAngleTangent,k=snapshot.distance;
        if (!std::isfinite(d) || d<=0 || !std::isfinite(r) || r<0 ||
            !std::isfinite(t) || t<0 || !std::isfinite(k) || k<1) return 1.f;
        // Read the native geometry; never change radius, tangent or the bhk shape.
        // d/k is the equivalent propagation progress without distance growth.
        const auto originalRadius=r+(d/k)*t;
        const auto ratio=originalRadius>0 ? (r+d*t)/originalRadius : 1.;
        const auto u=std::clamp(d/transitionDistance,0.,1.);
        const auto scale=1.+(ratio-1.)*(u*u*(3.-2.*u));
        return std::isfinite(scale) && scale<=std::numeric_limits<float>::max() ? static_cast<float>(scale) : 1.f;
    }
}
