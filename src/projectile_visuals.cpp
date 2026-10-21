#include "core.h"
#include "modules.h"
#include <RE/B/BSVisit.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
    using Snapshot=ysmir::progression::ScalingSnapshot;
    enum class DensityTarget : std::uint8_t { Alpha, EffectColor, Emissive };
    bool Near(float a,float b) { return std::abs(a-b)<=.0001f*std::max({1.f,std::abs(a),std::abs(b)}); }
    float Read(RE::BSShaderProperty* shader,DensityTarget target) {
        if (target==DensityTarget::EffectColor) return static_cast<RE::BSEffectShaderMaterial*>(shader->material)->baseColorScale;
        if (target==DensityTarget::Emissive) return static_cast<RE::BSLightingShaderProperty*>(shader)->emissiveMult;
        return shader->QMaterialAlpha();
    }
    void Write(RE::BSShaderProperty* shader,DensityTarget target,float value) {
        if (target==DensityTarget::EffectColor) static_cast<RE::BSEffectShaderMaterial*>(shader->material)->baseColorScale=value;
        else if (target==DensityTarget::Emissive) static_cast<RE::BSLightingShaderProperty*>(shader)->emissiveMult=value;
        else shader->SetMaterialAlpha(value);
    }
    struct DensityBinding {
        RE::NiPointer<RE::BSShaderProperty> shader;
        RE::BSShaderMaterial* material{};
        DensityTarget target{};
        float baseline{}, applied{};
        bool valid() const { return shader && shader->material==material; }
        ~DensityBinding() {
            if (valid() && Near(Read(shader.get(),target),applied)) Write(shader.get(),target,baseline);
        }
        void Apply(float gain) {
            if (!valid()) return;
            const auto value=Read(shader.get(),target);
            if (!std::isfinite(value)) return;
            if (!Near(value,applied)) baseline=value;
            applied=target==DensityTarget::Alpha ? 1.f-std::pow(1.f-std::clamp(baseline,0.f,1.f),gain) :
                std::min(std::max(0.f,baseline)*gain,std::max(baseline,64.f));
            if (!Near(value,applied)) Write(shader.get(),target,applied);
        }
    };
    struct BindingKey {
        const void* owner{}; DensityTarget target{};
        bool operator==(const BindingKey&) const = default;
    };
    struct BindingHash {
        std::size_t operator()(const BindingKey& k) const { return std::hash<const void*>{}(k.owner)^static_cast<std::size_t>(k.target); }
    };
    // Weak ownership deduplicates shared materials without pinning dead scene data.
    std::unordered_map<BindingKey,std::weak_ptr<DensityBinding>,BindingHash> materials;
    struct ShaderSlot { RE::NiPointer<RE::BSGeometry> geometry; std::shared_ptr<DensityBinding> binding; };
    struct State {
        RE::ProjectileHandle handle;
        Snapshot snapshot;
        bool isCone{}, storm{}, compensate{};
        RE::NiPointer<RE::NiAVObject> root;
        float baseline{1}, applied{1};
        RE::NiPoint3 centerAnchor;
        std::vector<RE::NiPointer<RE::NiAVObject>> bounds;
        std::vector<ShaderSlot> shaders;
    };
    std::unordered_map<std::uint32_t,State> states;
    std::mutex mutex;
    RE::BGSProjectile* stormBase{};
    bool patchBounds{true}, alwaysDraw{true}, density{true},debug{};
    float boundRadius{18000};
    std::vector<std::string> exclusions;
    double pruneAt{};
    std::string Lower(std::string value) {
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    void RestoreCone(State& state,RE::Projectile* projectile) {
        if (!state.isCone) return;
        if (state.root && Near(state.root->local.scale,state.applied)) {
            state.root->local.scale=state.applied=state.baseline;
            if (projectile) {
                const auto center=state.root->parent ? state.root->parent->world.Invert()*projectile->GetPosition() : projectile->GetPosition();
                state.root->local.translate=center-state.root->local.rotate*(state.centerAnchor*state.baseline);
            }
        }
    }
    void RestoreCone(RE::Projectile* projectile) {
        const RE::ProjectileHandle handle{projectile};
        std::scoped_lock lock(mutex);
        if (auto it=states.find(handle.native_handle()); it!=states.end() && it->second.handle.get().get()==projectile)
            RestoreCone(it->second,projectile);
    }
    bool Excluded(RE::BGSProjectile* base) {
        const auto* editor=base->GetFormEditorID(); const auto* model=base->GetModel();
        const auto text=Lower(std::string(editor ? editor : "")+" "+(model ? model : ""));
        return std::any_of(exclusions.begin(),exclusions.end(),[&](const auto& token) { return text.find(token)!=std::string::npos; });
    }
    std::shared_ptr<DensityBinding> Bind(RE::BSGeometry* geometry) {
        const auto& data=geometry->GetGeometryRuntimeData();
        auto* shader=data.shaderProperty.get(); if (!shader || !shader->material) return {};
        bool additive=false;
        if (const auto* alpha=data.alphaProperty.get(); alpha && alpha->GetAlphaBlending())
            additive=alpha->GetDestBlendMode()==RE::NiAlphaProperty::AlphaFunction::kOne && alpha->GetSrcBlendMode()!=RE::NiAlphaProperty::AlphaFunction::kZero;
        else if (shader->effectData)
            additive=shader->effectData->destBlend==RE::NiAlphaProperty::AlphaFunction::kOne && shader->effectData->srcBlend!=RE::NiAlphaProperty::AlphaFunction::kZero;
        const auto target=additive && shader->GetMaterialType()==RE::BSShaderMaterial::Type::kEffect ? DensityTarget::EffectColor :
            additive && netimmerse_cast<RE::BSLightingShaderProperty*>(shader) ? DensityTarget::Emissive : DensityTarget::Alpha;
        const BindingKey key{target==DensityTarget::Emissive ? static_cast<void*>(shader) : shader->material,target};
        auto& weak=materials[key];
        if (auto existing=weak.lock(); existing && existing->valid()) return existing;
        const auto value=Read(shader,target); if (!std::isfinite(value)) return {};
        auto binding=std::make_shared<DensityBinding>();
        binding->shader.reset(shader); binding->material=shader->material; binding->target=target;
        binding->baseline=binding->applied=value; weak=binding;
        return binding;
    }
    RE::NiPoint3 CenterInParent(RE::Projectile* projectile,RE::NiAVObject* root) {
        return root->parent ? root->parent->world.Invert()*projectile->GetPosition() : projectile->GetPosition();
    }
    void BindRoot(State& state,RE::Projectile* projectile,RE::NiAVObject* root) {
        RestoreCone(state,projectile);
        state.bounds.clear(); state.shaders.clear(); state.root.reset(root);
        state.baseline=state.applied=root->local.scale;
        const auto center=CenterInParent(projectile,root);
        // Fixed model-space anchor: preserve the original center in all three axes.
        state.centerAnchor=root->local.Invert()*center;
        RE::BSVisit::TraverseScenegraphObjects(root,[&](RE::NiAVObject* object) {
            if (state.storm && patchBounds) {
                state.bounds.emplace_back(object);
                object->GetFlags().set(RE::NiAVObject::Flag::kFixedBound,RE::NiAVObject::Flag::kForceUpdate);
                if (alwaysDraw) object->GetFlags().set(RE::NiAVObject::Flag::kAlwaysDraw);
                object->SetAppCulled(false);
            }
            // Track every geometry so later material swaps remain observable.
            // Bind() still shares one baseline per material; Apply() skips equal writes.
            if (state.compensate)
                if (auto* geometry=object->AsGeometry())
                    state.shaders.push_back({RE::NiPointer<RE::BSGeometry>{geometry},Bind(geometry)});
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (debug && state.isCone) {
            const auto* model=projectile->GetProjectileBase()->GetModel();
            SKSE::log::info("Cone VFX {}: uniform root scale, density targets={}, distance growth={}",
                model ? model : "<no model>",state.shaders.size(),state.snapshot.distance);
        }
    }
    void Apply(RE::Projectile* projectile,RE::NiAVObject* suppliedRoot)
    {
        if (!projectile || !ysmir::progression::Enabled()) return;
        auto* base=projectile->GetProjectileBase(); if (!base) return;
        const bool storm=base==stormBase;
        auto* magic=projectile->GetProjectileRuntimeData().spell;
        auto* spell=magic ? magic->As<RE::SpellItem>() : nullptr;
        if (!storm && (!ysmir::core::IsPlayerOwnedProjectile(projectile) || !ysmir::core::IsPlayerVoiceSpellRuntimeAuthorized(spell))) return;
        const RE::ProjectileHandle handle{projectile}; const auto id=handle.native_handle();
        if (!id) return; // Initialization can precede handle assignment.
        const auto snapshot=ysmir::core::ScalingForProjectile(projectile);
        std::scoped_lock lock(mutex);
        auto found=states.find(id);
        if (storm && found==states.end()) return;
        auto& state=states[id];
        if (state.handle.get().get()!=projectile) {
            RestoreCone(state,nullptr);
            state={}; state.handle=handle; state.snapshot=snapshot; state.storm=storm;
            state.isCone=base->IsCone(); state.compensate=state.isCone && density && !Excluded(base);
        }
        auto* cone=state.isCone ? static_cast<RE::ConeProjectile*>(projectile) : nullptr;
        if (cone && !ysmir::true_cone::PatchVisuals()) return;
        auto* root=suppliedRoot ? suppliedRoot : projectile->Get3D();
        if (!root || !std::isfinite(root->local.scale) || root->local.scale<=0) return;
        if (state.root.get()!=root) BindRoot(state,projectile,root);
        else if (!Near(root->local.scale,state.applied)) state.baseline=root->local.scale;
        const auto factor=cone ? ysmir::true_cone::VisualScale(cone,state.snapshot) : state.snapshot.vfx;
        const auto densityGain=cone ? ysmir::progression::VisualDensity(factor) : state.snapshot.density;
        // One transform preserves the whole model's proportions around its original anchor.
        root->local.scale=state.baseline*factor; state.applied=root->local.scale;
        root->local.translate=CenterInParent(projectile,root)-root->local.rotate*(state.centerAnchor*root->local.scale);
        RE::NiUpdateData data{}; root->UpdateTransformAndBounds(data);
        for (const auto& object : state.bounds) {
            object->worldBound.center=object->world.translate;
            object->worldBound.radius=std::max(object->worldBound.radius,boundRadius*state.snapshot.vfx);
        }
        for (auto& slot : state.shaders) {
            auto* shader=slot.geometry->GetGeometryRuntimeData().shaderProperty.get();
            if (!slot.binding || !slot.binding->valid() || !shader || shader->material!=slot.binding->material ||
                (slot.binding->target==DensityTarget::Emissive && shader!=slot.binding->shader.get()))
                slot.binding=Bind(slot.geometry.get());
            if (slot.binding) slot.binding->Apply(densityGain);
        }
    }
    template<std::size_t I> struct Hook {
        static inline void(*post)(RE::Projectile*,RE::NiAVObject*);
        static inline void(*update)(RE::Projectile*);
        static void Post(RE::Projectile* p,RE::NiAVObject* root) { post(p,root); Apply(p,root); }
        static void Update(RE::Projectile* p) { update(p); Apply(p,nullptr); }
        static void Install(std::uintptr_t address) {
            REL::Relocation<std::uintptr_t> table{address};
            post=reinterpret_cast<decltype(post)>(table.write_vfunc(0xAA,Post));
            update=reinterpret_cast<decltype(update)>(table.write_vfunc(0xAD,Update));
        }
    };
    struct ConeHook {
        static inline void(*update3D)(RE::ConeProjectile*);
        static inline void(*loaded)(RE::ConeProjectile*);
        static void Update3D(RE::ConeProjectile* p) { RestoreCone(p); update3D(p); Apply(p,nullptr); }
        static void Loaded(RE::ConeProjectile* p) { RestoreCone(p); loaded(p); Apply(p,nullptr); }
    };
}

namespace ysmir::projectile_visuals
{
    void Initialize()
    {
        patchBounds=config::Bool("Storm","bPatchProjectileBounds",true);
        alwaysDraw=config::Bool("Storm","bForceProjectileAlwaysDraw",true);
        boundRadius=std::clamp(config::Float("Storm.Visual","fProjectileBoundRadius",18000.f),512.f,1000000.f);
        density=config::Bool("TrueCone","DensityCompensation",true);
        debug=config::Bool("General","bDebugLogging",false);
        exclusions.clear();
        std::istringstream tokens(config::String("TrueCone","DensityExcludedTokens","Cyclone,BendWill"));
        for (std::string token; std::getline(tokens,token,','); ) {
            const auto first=token.find_first_not_of(" \t"), last=token.find_last_not_of(" \t");
            if (first!=std::string::npos) exclusions.push_back(Lower(token.substr(first,last-first+1)));
        }
        Hook<0>::Install(RE::VTABLE_Projectile[0].address());
        Hook<1>::Install(RE::VTABLE_BeamProjectile[0].address());
        Hook<2>::Install(RE::VTABLE_MissileProjectile[0].address());
        REL::Relocation<std::uintptr_t> table{RE::VTABLE_ConeProjectile[0]};
        ConeHook::update3D=reinterpret_cast<decltype(ConeHook::update3D)>(table.write_vfunc(0xAD,ConeHook::Update3D));
        ConeHook::loaded=reinterpret_cast<decltype(ConeHook::loaded)>(table.write_vfunc(0xC0,ConeHook::Loaded));
    }
    void SetStormBase(RE::BGSProjectile* base) { stormBase=base; }
    void TrackStorm(const RE::ProjectileHandle& handle,const Snapshot& snapshot)
    {
        const auto id=handle.native_handle(); if (!id) return;
        core::TrackProjectile(handle,snapshot);
        { std::scoped_lock lock(mutex); auto& state=states[id]; state.handle=handle; state.snapshot=snapshot; state.storm=true; }
        if (const auto projectile=handle.get()) Apply(projectile.get(),nullptr);
    }
    void Clear() {
        std::scoped_lock lock(mutex);
        for (auto& [id,state] : states) RestoreCone(state,state.handle.get().get());
        states.clear(); materials.clear();
    }
    void FrameUpdate()
    {
        if (progression::Now()<pruneAt) return;
        pruneAt=progression::Now()+1;
        std::scoped_lock lock(mutex);
        std::erase_if(states,[](auto& entry) {
            if (entry.second.handle.get()) return false;
            RestoreCone(entry.second,nullptr); return true;
        });
        std::erase_if(materials,[](const auto& entry) { return entry.second.expired(); });
    }
}
