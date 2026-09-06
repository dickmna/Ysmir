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
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using Snapshot = ysmir::progression::ScalingSnapshot;
    constexpr RE::FormID SlowTime = 0x48AC9;
    struct Mapping { RE::TESShout* privateShout{}; std::array<RE::SpellItem*,3> original{}; };
    struct Origin { RE::FormID shout{}; std::uint32_t tier{}; };
    std::unordered_map<RE::TESShout*,Mapping> mappings;
    std::unordered_map<RE::FormID,Origin> origins;
    std::recursive_mutex mutex;
    std::mutex snapshotMutex;
    std::atomic<std::uint64_t> generation{};
    Snapshot current;
    void SetCurrent(const Snapshot& value) { std::scoped_lock lock(snapshotMutex); current=value; }
    struct EffectBase { RE::Effect* effect{}; RE::Effect::EffectItem item{}; };
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
    std::unordered_map<RE::EffectSetting*,Snapshot> effectSnapshots;
    std::unordered_set<RE::SpellItem*> authorized;
    std::array<RE::SpellItem*,3> slowActual{};
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
            if (auto it=origins.find(spell->GetFormID()); it!=origins.end() && it->second.shout==SlowTime) plan.fixedDuration=true;
            for (auto* effect : spell->effects) {
                if (!effect || !effect->baseEffect) continue;
                plan.effects.push_back({effect,effect->effectItem});
                auto* setting=effect->baseEffect;
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
    void Apply(RE::SpellItem* spell, const Snapshot& s)
    {
        auto it=plans.find(spell);
        if (it==plans.end()) return;
        const auto& plan=it->second;
        std::scoped_lock snapshotLock(snapshotMutex);
        for (const auto& saved : plan.effects) {
            auto* effect=saved.effect;
            auto* setting=effect->baseEffect;
            auto item=saved.item;
            if (setting->HasArchetype(RE::EffectSetting::Archetype::kSlowTime))
                item.magnitude=std::max(.01f, saved.item.magnitude/s.magnitude);
            else
                item.magnitude=ScaleFloat(saved.item.magnitude, setting->HasArchetype(RE::EffectSetting::Archetype::kCloak) ? s.area : s.magnitude);
            if (!plan.fixedDuration) item.duration=ScaleInteger(saved.item.duration,s.duration);
            item.area=ScaleInteger(saved.item.area,s.area);
            effect->effectItem=item;
        }
        for (const auto& saved : plan.settings) {
            saved.effect->data.spellmakingArea=ScaleInteger(saved.area,s.area);
            effectSnapshots.insert_or_assign(saved.effect,s);
        }
        for (const auto& saved : plan.projectiles) {
            auto& data=saved.projectile->data;
            data.speed=ScaleFloat(saved.data.speed,s.distance);
            data.range=ScaleFloat(saved.data.range,s.distance);
            if (!saved.projectile->IsCone()) data.collisionRadius=ScaleFloat(saved.data.collisionRadius,s.collision);
        }
        for (auto* child : plan.spells) spellSnapshots.insert_or_assign(child,s);
        // Private records retain absolute scaling until the next cast/reset. Delayed
        // cloaks consume the same values without guessed restore deadlines. All
        // baselines and flat traversal plans were captured before the first cast.
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
        spellSnapshots.clear(); effectSnapshots.clear(); current={};
    }
    void Resolve()
    {
        std::scoped_lock lock(mutex);
        Restore(); mappings.clear(); origins.clear(); authorized.clear(); plans.clear();
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
                origins.insert_or_assign(privateShout->variations[tier].spell->GetFormID(),Origin{official->GetFormID(),tier});
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
        for (auto* spell : authorized) plans.emplace(spell,BuildPlan(spell));
        for (std::size_t i=0; i<3; ++i) slowActual[i]=data->LookupForm<RE::SpellItem>(ysmir::forms::SlowTimeActualSpells[i],ysmir::forms::PluginName);
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
    void Cooldown(RE::Actor& actor,const Snapshot& s,bool consumed=false)
    {
        if (auto* high=actor.GetHighProcess()) {
            auto base=high->voiceRecoveryTime;
            if (auto* shout=actor.GetCurrentShout(); shout) {
                const auto found=mappings.find(shout);
                const auto tier=Tier(actor);
                const auto selected=found!=mappings.end() ? found->second.privateShout->variations[tier].recoveryTime : shout->variations[tier].recoveryTime;
                // Native storms skip the engine handler. Dragon Aspect has FT's
                // 90-second baseline, while the shared NPC shout remains vanilla.
                if (consumed || selected!=shout->variations[tier].recoveryTime) {
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
            if (ysmir::storm_call::TryConsumeVoiceFire(actor,s)) { Cooldown(actor,s,true); return true; }
            // A disabled/unavailable storm adapter falls back to the untouched
            // official spell, rather than casting the native controller's stub.
            if (shout->GetFormID()==0x7097D) {
                const auto result=original(handler,actor,tag);
                if (result) Cooldown(actor,s);
                return result;
            }
            const auto tier=Tier(actor); auto* spell=found->second.privateShout->variations[tier].spell;
            Apply(spell,s);
            VariationScope scope(shout,tier,spell);
            const auto result=original(handler,actor,tag);
            if (result) { Cooldown(actor,s); ysmir::elemental_fury::OnShout(shout,tier,s); }
            SKSE::log::debug("Shout {:08X} word {} souls={} magnitude={} area={} duration={}",shout->GetFormID(),tier+1,s.souls,s.magnitude,s.area,s.duration);
            return result;
        }
    };
    float PushForce(RE::StaticFunctionTag*,float force,RE::Actor* caster,RE::EffectSetting* effect)
    {
        if (!ysmir::core::IsPlayerActor(caster) || !ysmir::progression::Enabled()) return force;
        std::scoped_lock lock(snapshotMutex);
        auto it=effectSnapshots.find(effect);
        return ScaleFloat(force,it!=effectSnapshots.end() ? it->second.magnitude : current.magnitude);
    }
    void CastSlowTime(RE::StaticFunctionTag*,RE::SpellItem* spell,RE::Actor* target,RE::Actor* caster,RE::EffectSetting* wrapper)
    {
        if (!spell || !target || !ysmir::core::IsPlayerActor(caster) || std::find(slowActual.begin(),slowActual.end(),spell)==slowActual.end()) return;
        Snapshot s;
        { std::scoped_lock lock(snapshotMutex); auto it=effectSnapshots.find(wrapper); s=it!=effectSnapshots.end() ? it->second : current; }
        auto targetHandle=target->GetHandle(); auto casterHandle=caster->GetHandle();
        const auto castGeneration=generation.load();
        // Papyrus can run on a VM worker. Capture now, resolve handles/cast on the
        // main thread. This replaces the old two-second prediction window.
        if (auto* tasks=SKSE::GetTaskInterface()) tasks->AddTask([spell,targetHandle,casterHandle,s,castGeneration] {
            if (generation.load()!=castGeneration) return;
            const auto target=targetHandle.get(); const auto caster=casterHandle.get();
            if (target && caster) ysmir::core::CastFeatureSpell(spell,*caster,target.get(),s);
        });
    }
    std::int32_t SoulCount(RE::StaticFunctionTag*) { std::scoped_lock lock(snapshotMutex); return static_cast<std::int32_t>(std::min(current.souls,static_cast<std::uint32_t>(INT32_MAX))); }
    bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) return false;
        vm->RegisterFunction("GetScaledPushForce","Ysmir",PushForce,true);
        vm->RegisterFunction("CastSlowTime","Ysmir",CastSlowTime);
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
    bool HasPrivatePlayerShoutMapping(const RE::TESShout* shout) { return mappings.contains(const_cast<RE::TESShout*>(shout)); }
    bool IsPlayerVoiceSpellRuntimeAuthorized(const RE::SpellItem* spell) { return authorized.contains(const_cast<RE::SpellItem*>(spell)); }
    bool TryGetPlayerShoutTierForSpell(RE::FormID spell,RE::FormID shout,std::uint32_t& tier)
    {
        auto it=origins.find(spell);
        if (it==origins.end() || it->second.shout!=shout) return false;
        tier=it->second.tier; return true;
    }
    Snapshot CurrentScaling() { std::scoped_lock lock(snapshotMutex); return current; }
    Snapshot ScalingForSpell(RE::SpellItem* spell)
    {
        std::scoped_lock lock(snapshotMutex);
        auto it=spellSnapshots.find(spell); return it!=spellSnapshots.end() ? it->second : Snapshot{};
    }
    Snapshot ScalingForProjectile(RE::Projectile* projectile)
    {
        if (!projectile || !IsPlayerOwnedProjectile(projectile)) return {};
        auto* magic=projectile->GetProjectileRuntimeData().spell;
        return magic ? ScalingForSpell(magic->As<RE::SpellItem>()) : Snapshot{};
    }
    bool CastFeatureSpell(RE::SpellItem* spell,RE::Actor& actor,RE::Actor* target,const Snapshot& s)
    {
        std::scoped_lock lock(mutex);
        auto* caster=actor.GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster || !authorized.contains(spell)) return false;
        Apply(spell,s); caster->CastSpellImmediate(spell,false,target,1.f,false,0.f,&actor); return true;
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
            Resolve(); REL::Relocation<std::uintptr_t> table{RE::VTABLE_VoiceSpellFireHandler[0]};
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
    void FrameUpdate() { progression::UpdateClock(); }
}
