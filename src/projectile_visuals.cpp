#include "core.h"
#include "modules.h"
#include <RE/B/BSVisit.h>
#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace
{
    struct State {
        RE::ProjectileHandle handle;
        RE::NiPointer<RE::NiAVObject> root;
        float baseline{1}, applied{1}, multiplier{1};
    };
    struct StormState { RE::ProjectileHandle handle; float vfx{1}; };
    std::unordered_map<std::uint32_t,State> states;
    std::unordered_map<std::uint32_t,StormState> stormStates;
    std::mutex mutex;
    RE::BGSProjectile* stormBase{};
    bool patchBounds{true}, alwaysDraw{true}; float boundRadius{18000};
    using PostLoad=void(*)(RE::Projectile*,RE::NiAVObject*);
    using Update=void(*)(RE::Projectile*);
    std::array<PostLoad,3> postLoad{};
    std::array<Update,3> update{};
    double pruneAt{};

    void Apply(RE::Projectile* projectile,RE::NiAVObject* suppliedRoot)
    {
        if (!projectile || !ysmir::progression::Enabled()) return;
        auto* base=projectile->GetProjectileBase();
        if (!base || base->IsCone()) return; // True Cone exclusively owns cone visuals.
        RE::ProjectileHandle handle{projectile};
        auto id=handle.native_handle();
        if (!id) return;
        auto* root=suppliedRoot ? suppliedRoot : projectile->Get3D();
        if (!root) return;
        const bool storm=base==stormBase;
        // Obtain the dispatch snapshot before taking the visual-state mutex, so
        // the render path never inverts the dispatch -> visual lock order.
        float multiplier=1;
        if (!storm) {
            auto* magic=projectile->GetProjectileRuntimeData().spell;
            auto* spell=magic ? magic->As<RE::SpellItem>() : nullptr;
            if (!ysmir::core::IsPlayerOwnedProjectile(projectile) || !ysmir::core::IsPlayerVoiceSpellRuntimeAuthorized(spell)) return;
            multiplier=ysmir::core::ScalingForSpell(spell).vfx;
        }
        std::scoped_lock lock(mutex);
        if (storm) {
            auto it=stormStates.find(id);
            if (it==stormStates.end() || it->second.handle.get().get()!=projectile) return;
            multiplier=it->second.vfx;
        }
        auto& state=states[id];
        if (state.handle.get().get()!=projectile || state.root.get()!=root)
            state={handle,RE::NiPointer<RE::NiAVObject>{root},root->local.scale,root->local.scale,multiplier};
        else if (std::abs(root->local.scale-state.applied)>.0001f) state.baseline=root->local.scale;
        root->local.scale=state.baseline*state.multiplier;
        state.applied=root->local.scale;
        RE::NiUpdateData data{}; root->UpdateTransformAndBounds(data);
        if (!storm || !patchBounds) return;
        const auto radius=boundRadius*state.multiplier;
        RE::BSVisit::TraverseScenegraphObjects(root,[&](RE::NiAVObject* object) {
            if (object) {
                object->GetFlags().set(RE::NiAVObject::Flag::kFixedBound,RE::NiAVObject::Flag::kForceUpdate);
                if (alwaysDraw) object->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
                object->SetAppCulled(false);
                object->worldBound.center=object->world.translate;
                object->worldBound.radius=std::max(object->worldBound.radius,radius);
            }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
    }
    template<std::size_t I> void Post(RE::Projectile* p,RE::NiAVObject* root) { postLoad[I](p,root); Apply(p,root); }
    template<std::size_t I> void Tick(RE::Projectile* p) { update[I](p); Apply(p,nullptr); }
    template<std::size_t I> void Install(std::uintptr_t address)
    {
        REL::Relocation<std::uintptr_t> table{address};
        postLoad[I]=reinterpret_cast<PostLoad>(table.write_vfunc(0xAA,Post<I>));
        update[I]=reinterpret_cast<Update>(table.write_vfunc(0xAD,Tick<I>));
    }
}

namespace ysmir::projectile_visuals
{
    void Initialize()
    {
        patchBounds=config::Bool("Storm","bPatchProjectileBounds",true);
        alwaysDraw=config::Bool("Storm","bForceProjectileAlwaysDraw",true);
        boundRadius=std::clamp(config::Float("Storm.Visual","fProjectileBoundRadius",18000.f),512.f,1000000.f);
        Install<0>(RE::VTABLE_Projectile[0].address());
        Install<1>(RE::VTABLE_BeamProjectile[0].address());
        Install<2>(RE::VTABLE_MissileProjectile[0].address());
    }
    void SetStormBase(RE::BGSProjectile* base) { stormBase=base; }
    void TrackStorm(const RE::ProjectileHandle& handle,float vfx)
    {
        auto id=handle.native_handle(); if (!id) return;
        { std::scoped_lock lock(mutex); stormStates.insert_or_assign(id,StormState{handle,vfx}); }
        if (auto projectile=handle.get()) Apply(projectile.get(),nullptr);
    }
    void Clear() { std::scoped_lock lock(mutex); states.clear(); stormStates.clear(); }
    void FrameUpdate()
    {
        if (progression::Now()<pruneAt) return;
        pruneAt=progression::Now()+1;
        std::scoped_lock lock(mutex);
        std::erase_if(states,[](const auto& entry) { return !entry.second.handle.get(); });
        std::erase_if(stormStates,[](const auto& entry) { return !entry.second.handle.get(); });
        // Live baselines are never evicted: eviction would re-capture an already
        // scaled root and amplify it again. Expired handles are reclaimed once/sec.
    }
}
