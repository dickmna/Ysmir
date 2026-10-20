#include "core.h"
#include "modules.h"
#include "RuntimeForms.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <vector>

namespace
{
    using Snapshot=ysmir::progression::ScalingSnapshot;
    constexpr std::uint32_t SerializationID=0x524D5359, RecordID=0x4D525453;
    struct Settings {
        bool enabled{true}, pauseIndoors{true}, blameCaster{};
        float radius{12800}, initialMin{3}, initialMax{6}, staggerMin{.12f}, staggerMax{.35f}, retry{.25f};
        float height{3500}, targetHeight{96}, imodDistance{3072};
        std::array<float,3> damage{40,60,80}, duration{60,120,180};
        std::array<float,3> minimum{3,.75f,.25f}, maximum{6,6.75f,5.25f}, spread{100,300,600};
    } settings;
    struct Saved {
        std::uint32_t tier{};
        float remaining{}, cycle{};
        std::array<float,2> pending{};
        std::uint32_t count{};
        Snapshot scaling;
    };
    // Read-only migration for 0.5/0.6's fixed STRM v3 record.
    struct Legacy {
        std::uint32_t caster{}, word{};
        float remaining{}, cycle{};
        std::array<float,2> pending{};
        std::uint32_t count{};
        std::array<float,6> scales{};
    };
    static_assert(sizeof(Legacy)==52 && sizeof(Saved)==60);
    std::array<RE::SpellItem*,3> roots{}, bolts{}, weather{};
    std::array<RE::EffectSetting*,3> timers{};
    RE::BGSListForm* exceptions{};
    RE::TESImageSpaceModifier* imod{};
    RE::BGSSoundDescriptorForm* sound{};
    bool resolved{}, loading{};
    Saved state;
    std::optional<Saved> resume;
    RE::ActiveEffect* identity{}; // Compared only; rediscovered before access.
    std::uint16_t identityID{};
    double lastTick{};
    std::mt19937 rng{std::random_device{}()};

    float Random(float low,float high) { return low>=high ? low : std::uniform_real_distribution<float>(low,high)(rng); }
    float Setting(const char* section,const char* key,float value,float maximum=1000000.f)
    { return std::clamp(ysmir::config::Float(section,key,value),0.f,maximum); }
    template<class T> T* Lookup(std::uint32_t id) {
        auto* data=RE::TESDataHandler::GetSingleton();
        return data ? data->LookupForm<T>(id,ysmir::forms::PluginName) : nullptr;
    }
    bool IsLive(RE::ActiveEffect* effect) {
        return effect && !effect->flags.any(RE::ActiveEffect::Flag::kDispelled) && effect->duration>effect->elapsedSeconds;
    }
    RE::ActiveEffect* Timer(RE::Actor& player,std::uint32_t& tier) {
        if (auto* effects=player.GetActiveEffectList()) for (auto* effect : *effects)
            if (IsLive(effect) && ysmir::core::IsPlayerActor(effect->GetCasterActor().get()))
                for (std::uint32_t i=0;i<3;++i)
                    if (timers[i] && effect->GetBaseObject()==timers[i] && effect->spell==roots[i]) { tier=i; return effect; }
        return nullptr;
    }
    std::vector<RE::ActiveEffect*> WeatherEffects(RE::Actor& player) {
        std::vector<RE::ActiveEffect*> result;
        if (auto* effects=player.GetActiveEffectList())
            for (auto* effect : *effects)
                if (IsLive(effect) && effect->spell && std::find(weather.begin(),weather.end(),effect->spell)!=weather.end() &&
                    ysmir::core::IsPlayerActor(effect->GetCasterActor().get())) result.push_back(effect);
        return result;
    }
    void EndWeather(RE::Actor& player) {
        for (auto* effect : WeatherEffects(player)) effect->Dispel(true);
    }
    void MaintainWeather(RE::Actor& player,float remaining) {
        const auto active=WeatherEffects(player);
        if (!active.empty()) {
            // Extend the same weather effect on recast, avoiding Finish/Start races.
            for (auto* effect : active) effect->duration=effect->elapsedSeconds+remaining;
            return;
        }
        auto* sky=RE::Sky::GetSingleton(); auto* cell=player.GetParentCell();
        if (!sky || sky->mode!=RE::Sky::Mode::kFull || !cell || cell->IsInteriorCell() ||
            (exceptions && exceptions->HasForm(sky->currentWeather))) return;
        auto scale=state.scaling; scale.duration=remaining/settings.duration[state.tier];
        if (ysmir::core::CastFeatureSpell(weather[state.tier],player,&player,scale))
            for (auto* effect : WeatherEffects(player)) effect->duration=effect->elapsedSeconds+remaining;
        // magicSetWeatherScript owns SetActive/ReleaseOverride and its exception list.
    }
    bool ValidTarget(RE::Actor* target,RE::Actor& player,float radius) {
        return target && target!=&player && !target->IsDead() && target->Is3DLoaded() &&
            target->GetParentCell() && target->IsHostileToActor(&player) &&
            target->GetPosition().GetDistance(player.GetPosition())<=radius;
    }
    void Feedback(RE::Actor& player,RE::Actor& target) {
        if (imod) RE::ImageSpaceModifierInstanceForm::Trigger(imod,
            std::clamp(1.f-target.GetPosition().GetDistance(player.GetPosition())/settings.imodDistance,.25f,1.f),nullptr);
        if (sound) if (auto* audio=RE::BSAudioManager::GetSingleton()) {
            RE::BSSoundHandle handle;
            if (audio->GetSoundHandle(handle,static_cast<RE::BSISoundDescriptor*>(sound))) {
                handle.SetPosition(target.GetPosition()); handle.Play();
            }
        }
    }
    std::size_t StrikePass(RE::Actor& player) {
        auto* world=RE::TES::GetSingleton(); if (!world) return 0;
        const auto radius=static_cast<float>(std::min(static_cast<double>(settings.radius)*state.scaling.area,
            static_cast<double>(std::numeric_limits<float>::max())));
        std::vector<RE::ActorHandle> targets;
        world->ForEachReferenceInRange(&player,radius,[&](RE::TESObjectREFR* ref) {
            auto* actor=ref ? ref->As<RE::Actor>() : nullptr;
            if (ValidTarget(actor,player,radius)) targets.push_back(actor->GetHandle());
            return RE::BSContainer::ForEachResult::kContinue;
        });
        std::size_t strikes{};
        for (const auto& handle : targets) if (auto target=handle.get(); ValidTarget(target.get(),player,radius)) {
            auto origin=target->GetPosition();
            origin.x+=Random(-settings.spread[state.tier],settings.spread[state.tier]);
            origin.y+=Random(-settings.spread[state.tier],settings.spread[state.tier]);
            origin.z+=settings.height;
            auto destination=target->GetPosition(); destination.z+=settings.targetHeight;
            auto* blame=settings.blameCaster ? &player : target.get();
            auto projectile=ysmir::core::LaunchFeatureSpell(bolts[state.tier],*blame,*target,origin,destination,state.scaling);
            if (projectile) { ysmir::projectile_visuals::TrackStorm(projectile,state.scaling); Feedback(player,*target); ++strikes; }
        }
        return strikes;
    }
    // Each global pass reacquires ALL eligible hostiles, including new arrivals.
    void Advance(RE::Actor& player,float delta) {
        state.cycle-=delta;
        for (std::uint32_t i=0;i<state.count;++i) state.pending[i]-=delta;
        // One overdue pass per frame: a hitch cannot collapse all three strikes.
        if (state.count && state.pending[0]<=0) {
            const auto overdue=state.pending[0];
            StrikePass(player);
            for (std::uint32_t i=1;i<state.count;++i) state.pending[i-1]=state.pending[i]-overdue;
            --state.count; state.cycle-=overdue;
            return;
        }
        if (state.count || state.cycle>0) return;
        const auto targets=StrikePass(player);
        state.count=targets ? state.tier : 0;
        float stagger{};
        for (std::uint32_t i=0;i<state.count;++i) {
            stagger+=Random(settings.staggerMin,settings.staggerMax);
            state.pending[i]=stagger;
        }
        state.cycle=stagger+(targets ? Random(settings.minimum[state.tier],settings.maximum[state.tier]) : settings.retry);
    }
    bool ValidSaved(const Saved& saved) {
        const auto& s=saved.scaling;
        return saved.tier<3 && saved.count<=saved.tier && saved.remaining>0 && std::isfinite(saved.remaining) &&
            std::isfinite(saved.cycle) && std::isfinite(saved.pending[0]) && std::isfinite(saved.pending[1]) &&
            std::all_of(std::begin(saved.pending),std::end(saved.pending),[](float x){return x>=0;}) &&
            std::isfinite(s.distance) && s.distance>=1 && std::isfinite(s.magnitude) && s.magnitude>=1 &&
            std::isfinite(s.duration) && s.duration>=1 && std::isfinite(s.area) && s.area>=1 &&
            std::isfinite(s.collision) && s.collision>=1 && std::isfinite(s.vfx) && s.vfx>=1 &&
            std::isfinite(s.density) && s.density>=1;
    }
    void Save(SKSE::SerializationInterface* io) {
        if (!identity || !ysmir::storm_call::Available()) return;
        auto* player=RE::PlayerCharacter::GetSingleton(); std::uint32_t tier{};
        auto* effect=player ? Timer(*player,tier) : nullptr;
        if (!effect || effect!=identity || effect->usUniqueID!=identityID) return;
        auto saved=state; saved.remaining=effect->duration-effect->elapsedSeconds; saved.cycle=std::max(0.f,saved.cycle);
        for (auto& due : saved.pending) due=std::max(0.f,due);
        if (ValidSaved(saved)) io->WriteRecord(RecordID,4,saved);
    }
    void Load(SKSE::SerializationInterface* io) {
        resume.reset(); std::uint32_t type{},version{},length{};
        while (io->GetNextRecordInfo(type,version,length)) {
            Saved saved{}; bool read=false;
            if (type==RecordID && version==4 && length==sizeof(Saved))
                read=io->ReadRecordData(&saved,length)==length;
            else if (type==RecordID && version==3 && length==sizeof(Legacy)) {
                Legacy old{}; RE::FormID caster{};
                if (io->ReadRecordData(&old,length)==length && old.word>=1 && old.word<=3 &&
                    io->ResolveFormID(old.caster,caster) && caster==0x14) {
                    saved.tier=old.word-1; saved.remaining=old.remaining; saved.cycle=old.cycle;
                    saved.count=old.count;
                    // v3 left expired times in unused slots after consuming a pass.
                    for (std::uint32_t i=0;i<std::min(saved.count,2u);++i) saved.pending[i]=std::max(0.f,old.pending[i]);
                    auto& s=saved.scaling;
                    s.distance=old.scales[0]; s.magnitude=old.scales[1]; s.duration=old.scales[2];
                    s.area=old.scales[3]; s.collision=old.scales[4]; s.vfx=old.scales[5];
                    s.density=1+(std::clamp(ysmir::config::Float("DragonSouls","fTrueConeDensityAt4x",2.2f),1.f,1000.f)-1)*(s.vfx-1)/3;
                    read=true;
                }
            } else {
                if (length>65536) return;
                std::array<std::byte,1024> bytes{};
                while (length) { const auto n=std::min(length,static_cast<std::uint32_t>(bytes.size())); if(io->ReadRecordData(bytes.data(),n)!=n) return; length-=n; }
            }
            if (read && ValidSaved(saved)) resume=saved;
        }
    }
    void Reset() { identity=nullptr; identityID=0; state={}; resume.reset(); lastTick=ysmir::progression::Now(); }
}

namespace ysmir::storm_call
{
    bool Available() { return settings.enabled && resolved && progression::Enabled(); }
    bool Initialize() {
        settings.enabled=config::Bool("Storm","bEnable",true);
        settings.pauseIndoors=config::Bool("Storm","bPauseInInteriors",true);
        settings.blameCaster=config::Bool("Storm","bBlameCaster",false);
        settings.radius=Setting("Storm.Gameplay","fSearchRadius",12800);
        settings.initialMin=Setting("Storm.Timing","fInitialDelayMin",3,3600);
        settings.initialMax=std::max(settings.initialMin,Setting("Storm.Timing","fInitialDelayMax",6,3600));
        settings.staggerMin=std::max(.001f,Setting("Storm.Timing","fPassStaggerMin",.12f,60));
        settings.staggerMax=std::max(settings.staggerMin,Setting("Storm.Timing","fPassStaggerMax",.35f,60));
        settings.retry=std::max(.01f,Setting("Storm.Timing","fNoTargetRetry",.25f,60));
        settings.height=Setting("Storm.Visual","fStrikeHeight",3500);
        settings.targetHeight=Setting("Storm.Visual","fTargetHeight",96);
        settings.imodDistance=std::max(1.f,Setting("Storm.Visual","fImodFadeDistance",3072));
        for (std::size_t i=0;i<3;++i) {
            const auto word=std::to_string(i+1);
            settings.damage[i]=Setting("Storm.Gameplay",("fDamageWord"+word).c_str(),settings.damage[i]);
            settings.duration[i]=std::max(1.f,Setting("Storm.Gameplay",("fDurationWord"+word).c_str(),settings.duration[i]));
            settings.minimum[i]=Setting("Storm.Timing",("fRecastDelayMinWord"+word).c_str(),settings.minimum[i],3600);
            settings.maximum[i]=std::max(settings.minimum[i],Setting("Storm.Timing",("fRecastDelayMaxWord"+word).c_str(),settings.maximum[i],3600));
            settings.spread[i]=Setting("Storm.Visual",("fSpreadWord"+word).c_str(),settings.spread[i]);
        }
        auto* io=SKSE::GetSerializationInterface(); if (!io) return false;
        io->SetUniqueID(SerializationID); io->SetSaveCallback(Save); io->SetLoadCallback(Load);
        io->SetRevertCallback([](SKSE::SerializationInterface*){Reset();});
        return true;
    }
    void Configure() {
        auto* shout=core::PrivateShout(0x7097D); resolved=shout;
        for (std::size_t i=0;i<3;++i) {
            roots[i]=shout ? shout->variations[i].spell : nullptr;
            timers[i]=Lookup<RE::EffectSetting>(forms::StormDuration[i]);
            bolts[i]=Lookup<RE::SpellItem>(forms::StormBolts[i]);
            weather[i]=Lookup<RE::SpellItem>(forms::StormWeather[i]);
            resolved=resolved && roots[i] && timers[i] && bolts[i] && weather[i];
            if (!resolved) continue;
            for (auto* item : roots[i]->effects) if(item && item->baseEffect==timers[i]) item->effectItem.duration=static_cast<std::uint32_t>(settings.duration[i]);
            for (auto* item : weather[i]->effects) if(item) item->effectItem.duration=static_cast<std::uint32_t>(settings.duration[i]);
            for (auto* item : bolts[i]->effects) if(item) item->effectItem.magnitude=settings.damage[i];
        }
        auto* data=RE::TESDataHandler::GetSingleton(); if (!data) { resolved=false; return; }
        exceptions=data->LookupForm<RE::BGSListForm>(0x998,"Update.esm");
        imod=data->LookupForm<RE::TESImageSpaceModifier>(0xA9D93,"Skyrim.esm");
        sound=data->LookupForm<RE::BGSSoundDescriptorForm>(0xEB382,"Skyrim.esm");
        auto* projectile=Lookup<RE::BGSProjectile>(forms::StormProjectile);
        resolved=resolved && projectile;
        projectile_visuals::SetStormBase(projectile);
    }
    void BeforeShout(RE::Actor& player) {
        std::vector<RE::ActiveEffect*> old;
        if (auto* effects=player.GetActiveEffectList())
            for (auto* effect : *effects) if (IsLive(effect) &&
                std::find(timers.begin(),timers.end(),effect->GetBaseObject())!=timers.end()) old.push_back(effect);
        for (auto* effect : old) effect->Dispel(true);
        Reset();
    }
    void HandleMessage(SKSE::MessagingInterface::Message* message) {
        if (!message) return;
        if (message->type==SKSE::MessagingInterface::kPreLoadGame || message->type==SKSE::MessagingInterface::kNewGame) {
            Reset(); loading=message->type==SKSE::MessagingInterface::kPreLoadGame; projectile_visuals::Clear();
        }
        if (message->type==SKSE::MessagingInterface::kPostLoadGame) { loading=false; lastTick=progression::Now(); }
    }
    void FrameUpdate() {
        const auto now=progression::Now();
        const auto delta=static_cast<float>(std::max(0.0,now-lastTick)); lastTick=now;
        if (loading || progression::Paused()) return;
        auto* player=RE::PlayerCharacter::GetSingleton(); if (!player) { Reset(); return; }
        std::uint32_t tier{}; auto* effect=Timer(*player,tier);
        if (!Available() || player->IsDead()) {
            if (effect) effect->Dispel(true);
            EndWeather(*player); Reset(); return;
        }
        if (resume && !effect) {
            auto scale=resume->scaling; scale.duration=resume->remaining/settings.duration[resume->tier];
            core::CastFeatureSpell(roots[resume->tier],*player,player,scale);
            effect=Timer(*player,tier);
            if (effect) effect->duration=effect->elapsedSeconds+resume->remaining;
        }
        if (!effect) { EndWeather(*player); Reset(); return; }
        if (effect!=identity || effect->usUniqueID!=identityID) {
            state=resume && resume->tier==tier ? *resume : Saved{tier,0,Random(settings.initialMin,settings.initialMax),{},0,core::ScalingForSpell(roots[tier])};
            resume.reset(); identity=effect; identityID=effect->usUniqueID;
        }
        state.remaining=effect->duration-effect->elapsedSeconds;
        MaintainWeather(*player,state.remaining);
        auto* cell=player->GetParentCell();
        if (settings.pauseIndoors && (!cell || cell->IsInteriorCell())) return;
        Advance(*player,delta);
    }
}
