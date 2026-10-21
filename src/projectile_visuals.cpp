#include "core.h"
#include "modules.h"
#include <RE/B/BSVisit.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
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
    struct WaveLayer {
        RE::NiPointer<RE::NiAVObject> object;
        float baseline{1},applied{1};
        void Restore() {
            if (Near(object->local.scale,applied)) object->local.scale=applied=baseline;
        }
        void Apply(float factor) {
            if (!Near(object->local.scale,applied)) baseline=object->local.scale;
            const auto value=baseline*factor;
            if (std::isfinite(value) && value>0) object->local.scale=applied=value;
        }
    };
    bool Tileable(RE::BSShaderMaterial* material);
    struct TileSlot {
        RE::NiPointer<RE::BSGeometry> geometry;
        RE::BSShaderProperty* shader{};
        RE::BSShaderMaterial* material{};
        std::array<RE::NiPoint2,2> baseline{},applied{};
        bool isolated{};
        bool Valid() const {
            return isolated && geometry->GetGeometryRuntimeData().shaderProperty.get()==shader && shader->material==material;
        }
        void Restore() {
            if (!Valid()) return;
            for (std::size_t i=0;i<2;++i) {
                auto& uv=material->texCoordScale[i];
                if (Near(uv.x,applied[i].x)) uv.x=applied[i].x=baseline[i].x;
                if (Near(uv.y,applied[i].y)) uv.y=applied[i].y=baseline[i].y;
            }
        }
        void Apply(float factor,bool debugLogging) {
            auto* current=geometry->GetGeometryRuntimeData().shaderProperty.get();
            if (current!=shader || (current && current->material!=material)) {
                Restore(); shader=current; material=current ? current->material : nullptr; isolated=false;
                // Animated properties normally belong to one geometry. Do not edit a
                // shared property: making only its material unique would affect all owners.
                if (factor>1 && current && current->GetRefCount()==1 && Tileable(material)) {
                    auto* original=material;
                    // SetMaterial creates the copy before releasing the old cache entry.
                    // Leave both acquisition and release to the native resource manager.
                    current->SetMaterial(original,true);
                    material=current->material;
                    isolated=material && material!=original;
                    if (isolated)
                        for (std::size_t i=0;i<2;++i) baseline[i]=applied[i]=material->texCoordScale[i];
                }
                // Defer allocation at birth; retry when there is actual distance growth.
                if (factor<=1) { shader=nullptr; material=nullptr; }
                else if (debugLogging) {
                    const auto* name=geometry->name.c_str();
                    SKSE::log::info("Cone VFX UV {}: isolated={}, property refs={}",
                        name ? name : "<unnamed>",isolated,current ? current->GetRefCount() : 0);
                }
            }
            if (!Valid()) return;
            if (shader->GetRefCount()!=1) { Restore(); return; }
            for (std::size_t i=0;i<2;++i) {
                auto& uv=material->texCoordScale[i];
                if (!Near(uv.x,applied[i].x)) baseline[i].x=uv.x;
                if (!Near(uv.y,applied[i].y)) baseline[i].y=uv.y;
                const RE::NiPoint2 next{baseline[i].x*factor,baseline[i].y*factor};
                if (std::isfinite(next.x) && std::isfinite(next.y)) uv=applied[i]=next;
            }
        }
    };
    struct State {
        RE::ProjectileHandle handle;
        Snapshot snapshot;
        bool isCone{}, storm{}, compensate{},nativeWave{};
        RE::NiPointer<RE::NiAVObject> root;
        float baseline{1}, applied{1};
        RE::NiPoint3 centerAnchor;
        std::vector<RE::NiPointer<RE::NiAVObject>> bounds;
        std::vector<ShaderSlot> shaders;
        std::vector<WaveLayer> waves;
        std::vector<TileSlot> tiles;
    };
    std::unordered_map<std::uint32_t,State> states;
    std::mutex mutex;
    RE::BGSProjectile* stormBase{};
    bool patchBounds{true}, alwaysDraw{true}, density{true},nativeGrowth{true},debug{};
    float boundRadius{18000};
    std::vector<std::string> exclusions;
    double pruneAt{};
    std::string Lower(std::string value) {
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    bool Tileable(RE::BSShaderMaterial* material) {
        if (!material || material->GetType()!=RE::BSShaderMaterial::Type::kEffect) return false;
        const auto* effect=static_cast<RE::BSEffectShaderMaterial*>(material);
        if (effect->effectClampMode!=RE::BSGraphics::TextureAddressMode::kWrapSWrapT) return false;
        const auto* texture=effect->sourceTexturePath.c_str(); if (!texture) return false;
        auto path=Lower(texture);
        const auto slash=path.find_last_of("\\/");
        const auto name=path.substr(slash==std::string::npos ? 0 : slash+1);
        return name=="vaportile01.dds" || name=="cloudtile.dds";
    }
    bool KnownWave(RE::BGSProjectile* base) {
        const auto* model=base->GetModel(); if (!model) return false;
        const auto path=Lower(model); const auto slash=path.find_last_of("\\/");
        const std::string_view name{path.data()+(slash==std::string::npos ? 0 : slash+1)};
        for (const auto stem : {"voicepushprojectile","voiceflamewaveproject","voicefrostwaveproject"}) {
            const std::string_view prefix{stem};
            if (name.starts_with(prefix)) {
                const auto suffix=name.substr(prefix.size());
                return suffix=="01.nif" || suffix=="02.nif" || suffix=="03.nif";
            }
        }
        return false;
    }
    bool WaveNode(RE::NiAVObject* object) {
        if (!object->AsNode()) return false;
        const auto* label=object->name.c_str(); if (!label) return false;
        const std::string_view name{label};
        for (const auto stem : {"RefractMesh","Wisps","Trail"}) {
            const std::string_view prefix{stem};
            if (!name.starts_with(prefix)) continue;
            const auto suffix=name.substr(prefix.size());
            return suffix.size()==2 && std::all_of(suffix.begin(),suffix.end(),[](char c) { return c>='0' && c<='9'; });
        }
        return false;
    }
    bool InWave(const State& state,RE::NiAVObject* object) {
        for (auto* parent=object->parent; parent; parent=parent->parent)
            if (std::any_of(state.waves.begin(),state.waves.end(),[&](const auto& wave) { return wave.object.get()==parent; })) return true;
        return false;
    }
    void RestoreCone(State& state,RE::Projectile* projectile) {
        if (!state.isCone) return;
        for (auto& wave : state.waves) wave.Restore();
        for (auto& tile : state.tiles) tile.Restore();
        if (!state.nativeWave && state.root && Near(state.root->local.scale,state.applied)) {
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
        state.bounds.clear(); state.shaders.clear(); state.waves.clear(); state.tiles.clear(); state.root.reset(root);
        state.baseline=state.applied=root->local.scale;
        const auto center=CenterInParent(projectile,root);
        // Fixed model-space anchor: preserve the original center in all three axes.
        state.centerAnchor=root->local.Invert()*center;
        bool core=false,wisps=false;
        if (state.isCone && nativeGrowth && KnownWave(projectile->GetProjectileBase()))
            RE::BSVisit::TraverseScenegraphObjects(root,[&](RE::NiAVObject* object) {
                if (WaveNode(object) && !InWave(state,object)) {
                    state.waves.push_back({RE::NiPointer<RE::NiAVObject>{object},object->local.scale,object->local.scale});
                    const std::string_view name{object->name.c_str()};
                    core|=name.starts_with("RefractMesh"); wisps|=name.starts_with("Wisps");
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
        state.nativeWave=core && wisps;
        if (!state.nativeWave) state.waves.clear();
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
                if (auto* geometry=object->AsGeometry()) {
                    if (state.nativeWave) {
                        if (InWave(state,geometry)) state.tiles.push_back({RE::NiPointer<RE::BSGeometry>{geometry}});
                    } else state.shaders.push_back({RE::NiPointer<RE::BSGeometry>{geometry},Bind(geometry)});
                }
            return RE::BSVisit::BSVisitControl::kContinue;
        });
        if (debug && state.isCone) {
            const auto* model=projectile->GetProjectileBase()->GetModel();
            SKSE::log::info("Cone VFX {}: native wave={}, layers={}, material candidates={}, distance growth={}",
                model ? model : "<no model>",state.nativeWave,state.waves.size(),state.tiles.size(),state.snapshot.distance);
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
        if (state.nativeWave) {
            // Native controllers retain their time, translation, rotation and alpha.
            // Correct only the outer wave layers, once per branch; the root stays native.
            for (auto& wave : state.waves) wave.Apply(factor);
            for (auto& tile : state.tiles) tile.Apply(factor,debug);
        } else {
            root->local.scale=state.baseline*factor; state.applied=root->local.scale;
            root->local.translate=CenterInParent(projectile,root)-root->local.rotate*(state.centerAnchor*root->local.scale);
        }
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
        nativeGrowth=config::Bool("TrueCone","NativeWaveGrowth",true);
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
