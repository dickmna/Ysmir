#include "core.h"
#include "modules.h"
#include "RuntimeForms.h"
#include <RE/N/NativeFunction.h>
#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <MinHook.h>

namespace
{
    using Snapshot = ysmir::progression::ScalingSnapshot;
    constexpr RE::FormID SlowTime = 0x48AC9;
    struct Mapping { RE::TESShout* privateShout{}; std::array<RE::SpellItem*,3> original{}; };
    std::unordered_map<RE::TESShout*,Mapping> mappings;
    std::unordered_map<RE::FormID,RE::FormID> origins;
    std::recursive_mutex mutex;
    std::mutex snapshotMutex;
    std::atomic<std::uint64_t> generation{};
    Snapshot current;
    void SetCurrent(const Snapshot& value) { std::scoped_lock lock(snapshotMutex); current=value; }
    enum class MagnitudeRule { Multiply, CloakRadius, SlowTime };
    struct EffectBase { RE::Effect* effect{}; RE::Effect::EffectItem item{}; MagnitudeRule rule{}; };
    struct SettingBase { RE::EffectSetting* effect{}; std::int32_t area{}; };
    struct ProjectileBase { RE::BGSProjectile* projectile{}; RE::BGSProjectileData data{}; };
    struct SpellPlan {
        std::vector<EffectBase> effects;
        std::vector<SettingBase> settings;
        std::vector<ProjectileBase> projectiles;
        std::vector<RE::SpellItem*> spells;
        bool fixedDuration{};
    };
    std::unordered_map<RE::SpellItem*,SpellPlan> plans;
    std::unordered_map<RE::SpellItem*,Snapshot> spellSnapshots;
    struct EffectContext {
        std::uint16_t id{};
        RE::MagicItem* spell{};
        Snapshot snapshot;
        std::atomic<bool> active{true};
        EffectContext(std::uint16_t key,RE::MagicItem* source,const Snapshot& value) : id(key),spell(source),snapshot(value) {}
    };
    struct ProjectileContext { RE::ProjectileHandle handle; Snapshot snapshot; };
    std::unordered_map<RE::ActiveEffect*,std::shared_ptr<EffectContext>> activeEffects;
    std::unordered_map<std::uint32_t,ProjectileContext> projectileContexts;
    thread_local const Snapshot* casting{};
    class CastScope {
        const Snapshot* previous{casting};
    public:
        explicit CastScope(const Snapshot& value) { casting=&value; }
        ~CastScope() { casting=previous; }
    };
    struct DirectedLaunch;
    thread_local DirectedLaunch* directedLaunch{};
    // Let MagicCaster build the spell, power and actor cause. Only relocate its launch.
    struct DirectedLaunch {
        RE::SpellItem* spell;
        const RE::NiPoint3& origin;
        RE::Projectile::ProjectileRot angles;
        RE::Actor& target;
        RE::ProjectileHandle handle;
        DirectedLaunch* previous{directedLaunch};
        DirectedLaunch(RE::SpellItem* s,const RE::NiPoint3& p,RE::Projectile::ProjectileRot a,RE::Actor& t)
            : spell(s),origin(p),angles(a),target(t) { directedLaunch=this; }
        ~DirectedLaunch() { directedLaunch=previous; }
    };
    std::unordered_set<RE::SpellItem*> authorized;
    std::unordered_set<RE::EffectSetting*> scriptContexts;
    std::unordered_map<RE::SpellItem*,std::vector<RE::SpellItem*>> children;

    template<class T> T ScaleInteger(T base, float factor)
    {
        if (base <= 0) return base;
        return static_cast<T>(std::clamp(std::round(static_cast<double>(base)*factor), 1.0, static_cast<double>(std::numeric_limits<T>::max())));
    }
    float ScaleFloat(float base, float factor)
    {
        return static_cast<float>(std::clamp(static_cast<double>(base)*factor,
            -static_cast<double>(std::numeric_limits<float>::max()), static_cast<double>(std::numeric_limits<float>::max())));
    }
    SpellPlan BuildPlan(RE::SpellItem* root)
    {
        SpellPlan plan;
        std::vector<RE::SpellItem*> pending{root};
        std::unordered_set<RE::SpellItem*> spells;
        std::unordered_set<RE::EffectSetting*> settings;
        std::unordered_set<RE::BGSProjectile*> projectiles;
        while (!pending.empty()) {
            auto* spell=pending.back(); pending.pop_back();
            if (!spell || !authorized.contains(spell) || !spells.insert(spell).second) continue;
            plan.spells.push_back(spell);
            if (auto linked=children.find(spell); linked!=children.end())
                pending.insert(pending.end(),linked->second.begin(),linked->second.end());
            if (auto it=origins.find(spell->GetFormID()); it!=origins.end() && it->second==SlowTime) plan.fixedDuration=true;
            for (auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;
                auto* setting=effect->baseEffect;
                const auto rule=setting->HasArchetype(RE::EffectSetting::Archetype::kSlowTime) ? MagnitudeRule::SlowTime :
                    setting->HasArchetype(RE::EffectSetting::Archetype::kCloak) ? MagnitudeRule::CloakRadius : MagnitudeRule::Multiply;
                plan.effects.push_back({effect,effect->effectItem,rule});
                if (setting->HasArchetype(RE::EffectSetting::Archetype::kSlowTime)) plan.fixedDuration=true;
                if (!settings.insert(setting).second) continue;
                plan.settings.push_back({setting,setting->data.spellmakingArea});
                if (auto* association=setting->data.associatedForm) pending.push_back(association->As<RE::SpellItem>());
                if (auto* projectile=setting->data.projectileBase; projectile && projectiles.insert(projectile).second)
                    plan.projectiles.push_back({projectile,projectile->data});
            }
        }
        return plan;
    }
    RE::Effect::EffectItem ScaledItem(const EffectBase& saved,bool fixedDuration,const Snapshot& s)
    {
        auto item=saved.item;
        item.magnitude=saved.rule==MagnitudeRule::SlowTime ? std::max(.01f,saved.item.magnitude/s.magnitude) :
            ScaleFloat(saved.item.magnitude,saved.rule==MagnitudeRule::CloakRadius ? s.area : s.magnitude);
        if (!fixedDuration) item.duration=ScaleInteger(saved.item.duration,s.duration);
        item.area=ScaleInteger(saved.item.area,s.area);
        return item;
    }
    void Apply(RE::SpellItem* spell, const Snapshot& s)
    {
        auto it=plans.find(spell);
        if (it==plans.end()) return;
        const auto& plan=it->second;
        std::scoped_lock snapshotLock(snapshotMutex);
        for (const auto& saved : plan.effects) saved.effect->effectItem=ScaledItem(saved,plan.fixedDuration,s);
        for (const auto& saved : plan.settings) {
            saved.effect->data.spellmakingArea=ScaleInteger(saved.area,s.area);
        }
        for (const auto& saved : plan.projectiles) {
            auto& data=saved.projectile->data;
            data.speed=ScaleFloat(saved.data.speed,s.distance);
            data.range=ScaleFloat(saved.data.range,s.distance);
            if (!saved.projectile->IsCone()) data.collisionRadius=ScaleFloat(saved.data.collisionRadius,s.collision);
        }
        for (auto* child : plan.spells) spellSnapshots.insert_or_assign(child,s);
        // Published values seed deferred effects; existing instances keep their own context.
    }
    // Temporarily restore one instance's numbers while the engine consumes its graph.
    // Save current values, not a guessed "last cast": graphs can share descendants.
    class InstanceScope {
        std::unique_lock<std::recursive_mutex> lock{mutex};
        CastScope context;
        std::vector<EffectBase> effects;
        std::vector<SettingBase> settings;
        std::vector<ProjectileBase> projectiles;
    public:
        InstanceScope(RE::SpellItem* spell,const Snapshot& s) : context(s) {
            auto it=plans.find(spell); if (it==plans.end()) return;
            // The usual path has identical values: no allocation or record writes.
            for (const auto& e : it->second.effects) {
                const auto next=ScaledItem(e,it->second.fixedDuration,s);
                auto& live=e.effect->effectItem;
                if (live.magnitude==next.magnitude && live.area==next.area && live.duration==next.duration) continue;
                effects.push_back({e.effect,live}); live=next;
            }
            for (const auto& e : it->second.settings) {
                const auto next=ScaleInteger(e.area,s.area);
                auto& live=e.effect->data.spellmakingArea;
                if (live==next) continue;
                settings.push_back({e.effect,live}); live=next;
            }
            for (const auto& e : it->second.projectiles) {
                auto& live=e.projectile->data;
                const auto speed=ScaleFloat(e.data.speed,s.distance), range=ScaleFloat(e.data.range,s.distance);
                const auto radius=e.projectile->IsCone() ? live.collisionRadius : ScaleFloat(e.data.collisionRadius,s.collision);
                if (live.speed==speed && live.range==range && live.collisionRadius==radius) continue;
                projectiles.push_back({e.projectile,live}); live.speed=speed; live.range=range; live.collisionRadius=radius;
            }
        }
        ~InstanceScope() {
            for (const auto& e : effects) e.effect->effectItem=e.item;
            for (const auto& e : settings) e.effect->data.spellmakingArea=e.area;
            for (const auto& e : projectiles) e.projectile->data=e.data;
        }
    };
    std::shared_ptr<EffectContext> EffectSnapshot(RE::ActiveEffect* effect,bool requireActive=true) {
        if (!effect) return {};
        std::scoped_lock lock(snapshotMutex);
        auto it=activeEffects.find(effect);
        if (it==activeEffects.end() || it->second->id!=effect->usUniqueID || it->second->spell!=effect->spell) return {};
        if (requireActive && (!it->second->active.load() ||
            effect->flags.any(RE::ActiveEffect::Flag::kDispelled))) return {};
        return it->second;
    }
    void RememberEffect(RE::ActiveEffect* effect) {
        if (!effect || !ysmir::progression::Enabled() || effect->flags.any(RE::ActiveEffect::Flag::kDispelled)) return;
        auto* base=effect->GetBaseObject();
        if (!base || (!base->HasArchetype(RE::EffectSetting::Archetype::kCloak) && !scriptContexts.contains(base))) return;
        {
            std::scoped_lock lock(snapshotMutex);
            const auto found=activeEffects.find(effect);
            // Keep a finished entry until destruction, so a final Update cannot
            // accidentally revive its queued casts.
            if (found!=activeEffects.end() && found->second->id==effect->usUniqueID && found->second->spell==effect->spell) return;
        }
        auto* spell=effect->spell ? effect->spell->As<RE::SpellItem>() : nullptr;
        if (!authorized.contains(spell) || !ysmir::core::IsPlayerActor(effect->GetCasterActor().get())) return;
        const auto snapshot=casting ? *casting : ysmir::core::ScalingForSpell(spell);
        std::scoped_lock lock(snapshotMutex);
        activeEffects.insert_or_assign(effect,std::make_shared<EffectContext>(effect->usUniqueID,effect->spell,snapshot));
    }
    void EndEffect(RE::ActiveEffect* effect,bool remove) {
        std::scoped_lock lock(snapshotMutex);
        const auto found=activeEffects.find(effect);
        if (found==activeEffects.end()) return;
        found->second->active=false;
        if (remove) activeEffects.erase(found);
    }
    template<std::size_t I> struct EffectHook {
        static inline void(*start)(RE::ActiveEffect*);
        static inline void(*finish)(RE::ActiveEffect*);
        static inline void(*update)(RE::ActiveEffect*,float);
        static inline void*(*destroy)(RE::ActiveEffect*,std::uint32_t);
        static void Start(RE::ActiveEffect* effect) {
            RememberEffect(effect);
            start(effect);
        }
        static void Update(RE::ActiveEffect* effect,float delta) {
            // Restored effects may resume without another Start call. Seed them
            // once from the load's spell baseline, then keep that instance fixed.
            RememberEffect(effect); update(effect,delta);
        }
        static void Finish(RE::ActiveEffect* effect) {
            EndEffect(effect,false);
            finish(effect);
        }
        static void* Destroy(RE::ActiveEffect* effect,std::uint32_t flags) {
            EndEffect(effect,true);
            return destroy(effect,flags);
        }
        static void Install(std::uintptr_t address) {
            REL::Relocation<std::uintptr_t> table{address};
            start=reinterpret_cast<decltype(start)>(table.write_vfunc(0x14,Start));
            finish=reinterpret_cast<decltype(finish)>(table.write_vfunc(0x15,Finish));
            destroy=reinterpret_cast<decltype(destroy)>(table.write_vfunc(0x13,Destroy));
            if constexpr (I==0) update=reinterpret_cast<decltype(update)>(table.write_vfunc(4,Update));
        }
    };
    struct CloakHook {
        static inline void(*update)(RE::ActiveEffect*,float);
        static void Update(RE::ActiveEffect* effect,float delta) {
            RememberEffect(effect);
            const auto snapshot=EffectSnapshot(effect);
            if (!snapshot) { update(effect,delta); return; }
            auto* setting=effect->GetBaseObject();
            auto* associated=setting ? setting->data.associatedForm : nullptr;
            auto* child=associated ? associated->As<RE::SpellItem>() : nullptr;
            InstanceScope scope(child,snapshot->snapshot); update(effect,delta);
        }
    };
    template<std::size_t I> struct ProjectileHook {
        static inline bool(*impact)(RE::Projectile*);
        static inline void(*update)(RE::Projectile*,float);
        static inline void(*havok)(RE::Projectile*);
        template<class F> static auto InContext(RE::Projectile* projectile,F&& run) {
            auto* magic=projectile->GetProjectileRuntimeData().spell;
            auto* spell=magic ? magic->As<RE::SpellItem>() : nullptr;
            if (!authorized.contains(spell) || !ysmir::core::IsPlayerOwnedProjectile(projectile)) return run();
            const auto snapshot=ysmir::core::ScalingForProjectile(projectile);
            InstanceScope scope(spell,snapshot); return run();
        }
        static bool Impact(RE::Projectile* p) { return InContext(p,[&] { return impact(p); }); }
        static void Update(RE::Projectile* p,float delta) { InContext(p,[&] { update(p,delta); }); }
        static void Havok(RE::Projectile* p) { InContext(p,[&] { havok(p); }); }
        static void Install(std::uintptr_t address) {
            REL::Relocation<std::uintptr_t> table{address};
            impact=reinterpret_cast<decltype(impact)>(table.write_vfunc(0xAC,Impact));
            update=reinterpret_cast<decltype(update)>(table.write_vfunc(0xAB,Update));
            havok=reinterpret_cast<decltype(havok)>(table.write_vfunc(0x66,Havok));
        }
    };
    struct LaunchHook {
        static inline RE::ProjectileHandle*(*original)(RE::ProjectileHandle*,RE::Projectile::LaunchData&);
        static RE::ProjectileHandle* Launch(RE::ProjectileHandle* out,RE::Projectile::LaunchData& data) {
            auto* spell=data.spell ? data.spell->As<RE::SpellItem>() : nullptr;
            if (!authorized.contains(spell) || !data.shooter ||
                !ysmir::core::IsPlayerActor(data.shooter->As<RE::Actor>())) return original(out,data);
            const auto snapshot=casting ? *casting : ysmir::core::ScalingForSpell(spell);
            CastScope scope(snapshot);
            auto* directed=directedLaunch && directedLaunch->spell==spell ? directedLaunch : nullptr;
            if (directed) {
                data.origin=directed->origin; data.angleX=directed->angles.x; data.angleZ=directed->angles.z;
                data.desiredTarget=&directed->target; data.parentCell=directed->target.GetParentCell();
                data.useOrigin=true; data.alwaysHit=true; data.autoAim=true;
            }
            auto* result=original(out,data);
            if (result) ysmir::core::TrackProjectile(*result,snapshot);
            if (directed && result) directed->handle=*result;
            return result;
        }
    };
    bool InstallInstanceHooks() {
        const auto init=MH_Initialize();
        if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
        auto* address=reinterpret_cast<void*>(REL::RelocationID(42928,44108).address());
        if (MH_CreateHook(address,reinterpret_cast<void*>(LaunchHook::Launch),reinterpret_cast<void**>(&LaunchHook::original))!=MH_OK) return false;
        if (MH_EnableHook(address)!=MH_OK) { MH_RemoveHook(address); return false; }
        EffectHook<0>::Install(RE::VTABLE_ScriptEffect[0].address());
        EffectHook<1>::Install(RE::VTABLE_CloakEffect[0].address());
        REL::Relocation<std::uintptr_t> cloak{RE::VTABLE_CloakEffect[0]};
        CloakHook::update=reinterpret_cast<decltype(CloakHook::update)>(cloak.write_vfunc(4,CloakHook::Update));
        ProjectileHook<0>::Install(RE::VTABLE_Projectile[0].address());
        ProjectileHook<1>::Install(RE::VTABLE_BeamProjectile[0].address());
        ProjectileHook<2>::Install(RE::VTABLE_MissileProjectile[0].address());
        ProjectileHook<3>::Install(RE::VTABLE_ConeProjectile[0].address());
        return true;
    }
    void Restore()
    {
        ++generation;
        for (const auto& [spell,plan] : plans) {
            for (const auto& saved : plan.effects) saved.effect->effectItem=saved.item;
            for (const auto& saved : plan.settings) saved.effect->data.spellmakingArea=saved.area;
            for (const auto& saved : plan.projectiles) saved.projectile->data=saved.data;
        }
        std::scoped_lock snapshotLock(snapshotMutex);
        spellSnapshots.clear(); activeEffects.clear(); projectileContexts.clear(); current={};
    }
    void Resolve()
    {
        std::scoped_lock lock(mutex);
        Restore(); mappings.clear(); origins.clear(); authorized.clear(); plans.clear(); scriptContexts.clear();
        auto* data=RE::TESDataHandler::GetSingleton();
        if (!data) return;
        for (const auto& expected : ysmir::forms::PlayerShouts) {
            auto* official=data->LookupForm<RE::TESShout>(expected.localFormID,expected.masterFile);
            auto* privateShout=data->LookupForm<RE::TESShout>(expected.privateFormID,ysmir::forms::PluginName);
            Mapping entry{privateShout};
            bool valid=official && privateShout && privateShout!=official;
            for (std::uint32_t tier=0; valid && tier<3; ++tier) {
                entry.original[tier]=official->variations[tier].spell;
                auto* expectedSpell=data->LookupForm<RE::SpellItem>(expected.privateSpellIDs[tier],ysmir::forms::PluginName);
                valid=entry.original[tier] && expectedSpell && privateShout->variations[tier].spell==expectedSpell;
            }
            if (!valid) { SKSE::log::error("Private shout {:06X} disagrees with the ESP manifest; disabled",expected.privateFormID); continue; }
            mappings.emplace(official,entry);
            for (std::uint32_t tier=0; tier<3; ++tier)
                origins.insert_or_assign(privateShout->variations[tier].spell->GetFormID(),official->GetFormID());
        }
        for (auto id : ysmir::forms::PlayerSpells) {
            if (auto* spell=data->LookupForm<RE::SpellItem>(id,ysmir::forms::PluginName)) authorized.insert(spell);
            else SKSE::log::error("Private progression spell {:06X} missing",id);
        }
        children.clear();
        for (const auto& edge : ysmir::forms::PlayerSpellEdges) {
            auto* parent=data->LookupForm<RE::SpellItem>(edge.parent,ysmir::forms::PluginName);
            auto* child=data->LookupForm<RE::SpellItem>(edge.child,ysmir::forms::PluginName);
            if (parent && child) children[parent].push_back(child);
        }
        for (auto id : ysmir::forms::ScriptContexts)
            if (auto* effect=data->LookupForm<RE::EffectSetting>(id,ysmir::forms::PluginName)) scriptContexts.insert(effect);
        ysmir::storm_call::Configure();
        for (auto* spell : authorized) plans.emplace(spell,BuildPlan(spell));
        SetCurrent(ysmir::progression::Capture());
        SKSE::log::info("Progression ready: {} of {} private shouts, {} precompiled spell plans",mappings.size(),ysmir::forms::PlayerShouts.size(),plans.size());
    }
    std::uint32_t Tier(RE::Actor& actor)
    {
        if (auto* high=actor.GetHighProcess()) return static_cast<std::uint32_t>(std::clamp(static_cast<int>(high->currentShoutVariation),0,2));
        return static_cast<std::uint32_t>(std::clamp(actor.GetCurrentShoutLevel(),0,2));
    }
    class VariationScope
    {
        RE::TESShout* shout{}; std::uint32_t tier{}; RE::SpellItem* original{};
    public:
        VariationScope(RE::TESShout* s,std::uint32_t t,RE::SpellItem* replacement)
        { if (s && replacement) { shout=s; tier=t; original=s->variations[t].spell; s->variations[t].spell=replacement; } }
        ~VariationScope() { if (shout) shout->variations[tier].spell=original; }
    };
    void Cooldown(RE::Actor& actor,const Snapshot& s)
    {
        if (auto* high=actor.GetHighProcess()) {
            auto base=high->voiceRecoveryTime;
            if (auto* shout=actor.GetCurrentShout(); shout) {
                const auto found=mappings.find(shout);
                const auto tier=Tier(actor);
                const auto selected=found!=mappings.end() ? found->second.privateShout->variations[tier].recoveryTime : shout->variations[tier].recoveryTime;
                // Dragon Aspect has FT's 90-second baseline; NPC records stay vanilla.
                if (selected!=shout->variations[tier].recoveryTime) {
                    const auto* owner=actor.AsActorValueOwner();
                    const auto recovery=owner ? owner->GetActorValue(RE::ActorValue::kShoutRecoveryMult) : 1.f;
                    base=selected*(std::isfinite(recovery) ? std::max(0.f,recovery) : 1.f);
                }
            }
            high->voiceRecoveryTime=std::max(0.f,base*s.cooldown);
        }
    }
    struct VoiceHook
    {
        using Execute=ysmir::core::VoiceExecute;
        static inline Execute original{};
        static bool Hook(RE::VoiceSpellFireHandler* handler,RE::Actor& actor,const RE::BSFixedString& tag)
        {
            std::scoped_lock lock(mutex);
            auto* shout=actor.GetCurrentShout(); auto found=mappings.find(shout);
            if (!ysmir::core::IsPlayerActor(&actor)) {
                VariationScope scope(shout,Tier(actor),found!=mappings.end() ? found->second.original[Tier(actor)] : nullptr);
                return original(handler,actor,tag);
            }
            if (!ysmir::progression::Enabled() || found==mappings.end()) return original(handler,actor,tag);
            const auto s=ysmir::progression::Capture();
            SetCurrent(s);
            if (shout->GetFormID()==0x7097D && !ysmir::storm_call::Available()) {
                const auto result=original(handler,actor,tag);
                if (result) Cooldown(actor,s);
                return result;
            }
            const auto tier=Tier(actor); auto* spell=found->second.privateShout->variations[tier].spell;
            Apply(spell,s);
            if (shout->GetFormID()==0x7097D) ysmir::storm_call::BeforeShout(actor);
            CastScope castScope(s);
            VariationScope scope(shout,tier,spell);
            const auto result=original(handler,actor,tag);
            if (result) { Cooldown(actor,s); ysmir::elemental_fury::OnShout(shout,tier,s); }
            SKSE::log::debug("Shout {:08X} word {} souls={} magnitude={} area={} duration={}",shout->GetFormID(),tier+1,s.souls,s.magnitude,s.area,s.duration);
            return result;
        }
    };
    float PushForce(RE::StaticFunctionTag*,float force,RE::Actor* caster,RE::ActiveEffect* effect)
    {
        if (!ysmir::core::IsPlayerActor(caster) || !ysmir::progression::Enabled()) return force;
        // A zero-duration push effect may finish before its queued Papyrus event
        // reads the immutable scale. This read does not start a new child spell.
        const auto snapshot=EffectSnapshot(effect,false);
        return snapshot ? ScaleFloat(force,snapshot->snapshot.magnitude) : force;
    }
    void CastFromEffect(RE::StaticFunctionTag*,RE::SpellItem* spell,RE::Actor* target,RE::Actor* caster,RE::ActiveEffect* wrapper)
    {
        if (!ysmir::progression::Enabled() || !spell || !target || !ysmir::core::IsPlayerActor(caster) || !authorized.contains(spell)) return;
        const auto snapshot=EffectSnapshot(wrapper); if (!snapshot) return;
        auto* parent=snapshot->spell ? snapshot->spell->As<RE::SpellItem>() : nullptr;
        const auto plan=plans.find(parent);
        if (plan==plans.end() || std::find(plan->second.spells.begin(),plan->second.spells.end(),spell)==plan->second.spells.end()) return;
        const auto targetHandle=target->GetHandle(), casterHandle=caster->GetHandle();
        const auto castGeneration=generation.load();
        const std::weak_ptr<EffectContext> context=snapshot;
        // The VM receives its ActiveMagicEffect instance, then copies its immutable
        // context before queueing engine work. Loading another save invalidates it.
        if (auto* tasks=SKSE::GetTaskInterface()) tasks->AddTask([spell,targetHandle,casterHandle,castGeneration,context] {
            const auto parent=context.lock();
            if (!ysmir::progression::Enabled() || generation.load()!=castGeneration || !parent || !parent->active.load()) return;
            const auto target=targetHandle.get(), caster=casterHandle.get();
            if (target && caster) ysmir::core::CastFeatureSpell(spell,*caster,target.get(),parent->snapshot);
        });
    }
    std::int32_t SoulCount(RE::StaticFunctionTag*) { std::scoped_lock lock(snapshotMutex); return static_cast<std::int32_t>(std::min(current.souls,static_cast<std::uint32_t>(INT32_MAX))); }
    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) return false;
        vm->RegisterFunction("GetScaledPushForce","Ysmir",PushForce,true);
        vm->RegisterFunction("CastFromEffect","Ysmir",CastFromEffect);
        vm->RegisterFunction("GetDragonSoulCount","Ysmir",SoulCount,true);
        return true;
    }
}

namespace ysmir::core
{
    bool IsPlayerActor(const RE::Actor* actor) { return actor && actor==RE::PlayerCharacter::GetSingleton(); }
    bool IsPlayerOwnedProjectile(RE::Projectile* projectile)
    {
        if (!projectile) return false;
        const auto shooter=projectile->GetProjectileRuntimeData().shooter.get();
        return shooter && IsPlayerActor(shooter->As<RE::Actor>());
    }
    // These registries are immutable after DataLoaded installs the voice hook.
    bool IsPlayerVoiceSpellRuntimeAuthorized(const RE::SpellItem* spell) { return authorized.contains(const_cast<RE::SpellItem*>(spell)); }
    Snapshot CurrentScaling() { std::scoped_lock lock(snapshotMutex); return current; }
    Snapshot ScalingForSpell(RE::SpellItem* spell)
    {
        std::scoped_lock lock(snapshotMutex);
        auto it=spellSnapshots.find(spell); return it!=spellSnapshots.end() ? it->second : Snapshot{};
    }
    Snapshot ScalingForProjectile(RE::Projectile* projectile)
    {
        if (!projectile) return {};
        const RE::ProjectileHandle handle{projectile}; const auto id=handle.native_handle();
        {
            std::scoped_lock lock(snapshotMutex);
            auto it=projectileContexts.find(id);
            if (it!=projectileContexts.end() && it->second.handle.get().get()==projectile) return it->second.snapshot;
        }
        if (!IsPlayerOwnedProjectile(projectile)) return {};
        auto* magic=projectile->GetProjectileRuntimeData().spell;
        auto* spell=magic ? magic->As<RE::SpellItem>() : nullptr;
        if (!authorized.contains(spell)) return {};
        const auto snapshot=casting ? *casting : ScalingForSpell(spell);
        TrackProjectile(handle,snapshot);
        return snapshot;
    }
    void TrackProjectile(const RE::ProjectileHandle& handle,const Snapshot& snapshot)
    {
        const auto id=handle.native_handle(); if (!id) return;
        std::scoped_lock lock(snapshotMutex);
        projectileContexts.insert_or_assign(id,ProjectileContext{handle,snapshot});
    }
    bool CastFeatureSpell(RE::SpellItem* spell,RE::Actor& actor,RE::Actor* target,const Snapshot& s)
    {
        if (!progression::Enabled() || !IsPlayerActor(&actor)) return false;
        std::scoped_lock lock(mutex);
        auto* caster=actor.GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster || !authorized.contains(spell)) return false;
        Apply(spell,s); CastScope scope(s); caster->CastSpellImmediate(spell,false,target,1.f,false,0.f,&actor); return true;
    }
    RE::ProjectileHandle LaunchFeatureSpell(RE::SpellItem* spell,RE::Actor& blame,RE::Actor& target,
        const RE::NiPoint3& origin,const RE::NiPoint3& destination,const Snapshot& s)
    {
        if (!progression::Enabled() || !authorized.contains(spell)) return {};
        auto* player=RE::PlayerCharacter::GetSingleton();
        auto* caster=player ? player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
        if (!caster) return {};
        std::scoped_lock lock(mutex);
        Apply(spell,s); CastScope scope(s);
        const auto dx=destination.x-origin.x,dy=destination.y-origin.y,dz=destination.z-origin.z;
        const RE::Projectile::ProjectileRot angles{std::atan2(-dz,std::hypot(dx,dy)),std::atan2(dx,dy)};
        DirectedLaunch directed(spell,origin,angles,target);
        caster->CastSpellImmediate(spell,false,&target,1.f,false,0.f,&blame);
        return directed.handle;
    }
    RE::TESShout* PrivateShout(RE::FormID official)
    {
        auto it=mappings.find(RE::TESForm::LookupByID<RE::TESShout>(official));
        return it!=mappings.end() ? it->second.privateShout : nullptr;
    }
    bool Initialize()
    {
        progression::Initialize(); const auto* papyrus=SKSE::GetPapyrusInterface();
        return papyrus && papyrus->Register(RegisterPapyrus);
    }
    void HandleMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) return;
        if (message->type==SKSE::MessagingInterface::kDataLoaded) {
            Resolve();
            if (!InstallInstanceHooks()) { SKSE::log::critical("Instance hooks unavailable; player dispatch disabled"); return; }
            REL::Relocation<std::uintptr_t> table{RE::VTABLE_VoiceSpellFireHandler[0]};
            VoiceHook::original=reinterpret_cast<VoiceHook::Execute>(table.write_vfunc(1,VoiceHook::Hook));
        }
        if (message->type==SKSE::MessagingInterface::kPreLoadGame) { std::scoped_lock lock(mutex); Restore(); }
        if (message->type==SKSE::MessagingInterface::kPostLoadGame || message->type==SKSE::MessagingInterface::kNewGame) {
            std::scoped_lock lock(mutex);
            Restore(); const auto s=progression::Capture(); SetCurrent(s);
            for (auto* spell : authorized) Apply(spell,s);
        }
        elemental_fury::HandleMessage(message);
    }
    void FrameUpdate() {
        progression::UpdateClock();
        static double pruneAt{};
        if (progression::Now()<pruneAt) return;
        pruneAt=progression::Now()+1;
        std::scoped_lock lock(snapshotMutex);
        std::erase_if(projectileContexts,[](const auto& entry) { return !entry.second.handle.get(); });
    }
}
