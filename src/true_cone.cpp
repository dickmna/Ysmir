#include "modules.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace { bool visuals{true}; }

namespace ysmir::true_cone
{
    void Initialize()
    {
        visuals=config::Bool("TrueCone","Enabled",true) && config::Bool("TrueCone","PatchVisuals",true) &&
            config::Bool("DragonSouls","bScaleVFX",true);
    }
    bool PatchVisuals() { return visuals; }
    float VisualScale(RE::ConeProjectile* projectile,const progression::ScalingSnapshot& snapshot)
    {
        if (!visuals) return 1.f;
        const auto& runtime=projectile->GetProjectileRuntimeData();
        const double d=runtime.distanceMoved,range=runtime.range,k=snapshot.distance;
        if (!std::isfinite(d) || d<=0 || !std::isfinite(range) || range<=0 ||
            !std::isfinite(k) || k<1) return 1.f;
        // The instance range already includes distance growth; do not multiply it again.
        // One smooth transition spans the flight, independent of collision geometry.
        const auto u=std::clamp(d/range,0.,1.);
        const auto scale=1.+(k-1.)*(u*u*(3.-2.*u));
        return std::isfinite(scale) && scale<=std::numeric_limits<float>::max() ? static_cast<float>(scale) : 1.f;
    }
}
