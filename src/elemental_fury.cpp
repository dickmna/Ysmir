#include "core.h"
#include "modules.h"
#include "RuntimeForms.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <random>

namespace
{
    constexpr RE::FormID ElementalFury=0x32921;
    std::array<std::array<RE::SpellItem*,3>,3> spells{};
    std::array<RE::EffectSetting*,3> activation{};
    std::array<std::array<double,3>,3> ready{};
    std::array<ysmir::progression::ScalingSnapshot,3> snapshots{};
    bool enabled{true}; float chance{.5f}, cooldown{2.f};
    std::mt19937 rng{std::random_device{}()};

    // DAC timestamps are wall time, including menu time, and belong to a rule.
    double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    class AnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static AnimationSink& Get() { static AnimationSink value; return value; }
        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
        {
            auto* player=RE::PlayerCharacter::GetSingleton();
            if (!enabled || !ysmir::progression::Enabled() || !event || !player || event->holder!=player ||
                _stricmp(event->tag.c_str(),"hitframe")!=0 || ysmir::progression::Paused()) return RE::BSEventNotifyControl::kContinue;
            auto* target=player->AsMagicTarget();
            if (!target) return RE::BSEventNotifyControl::kContinue;
            std::uniform_real_distribution<float> roll(0.f,1.f);
            const auto now=Now();
            for (std::uint32_t tier=0; tier<3; ++tier) {
                if (!activation[tier] || !target->HasMagicEffect(activation[tier])) continue;
                for (std::uint32_t element=0; element<3; ++element)
                    if (roll(rng)<chance && now>ready[element][tier]) {
                        ready[element][tier]=now+cooldown;
                        ysmir::core::CastFeatureSpell(spells[element][tier],*player,player,snapshots[tier]);
                    }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    void Attach()
    { if (auto* player=RE::PlayerCharacter::GetSingleton()) player->AddAnimationGraphEventSink(&AnimationSink::Get()); }
}

namespace ysmir::elemental_fury
{
    void OnShout(RE::TESShout* shout,std::uint32_t tier,const progression::ScalingSnapshot& value)
    {
        if (shout && shout->GetFormID()==ElementalFury && tier<3) snapshots[tier]=value;
    }
    void HandleMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) return;
        if (message->type==SKSE::MessagingInterface::kDataLoaded) {
            enabled=config::Bool("ElementalFury","bEnable",true);
            chance=std::clamp(config::Float("ElementalFury","fChancePerElement",.5f),0.f,1.f);
            cooldown=std::clamp(config::Float("ElementalFury","fElementCooldownSeconds",2.f),0.f,60.f);
            auto* data=RE::TESDataHandler::GetSingleton();
            auto* shout=core::PrivateShout(ElementalFury);
            for (std::size_t element=0; element<3; ++element)
                for (std::size_t tier=0; tier<3; ++tier) {
                    spells[element][tier]=data ? data->LookupForm<RE::SpellItem>(forms::ElementalSpells[element][tier],forms::PluginName) : nullptr;
                    if (!spells[element][tier]) enabled=false;
                }
            for (std::size_t tier=0; tier<3; ++tier) {
                if (shout && shout->variations[tier].spell)
                    for (auto* effect : shout->variations[tier].spell->effects)
                        if (effect && effect->baseEffect && effect->baseEffect->data.delivery==RE::MagicSystem::Delivery::kSelf) {
                            activation[tier]=effect->baseEffect; break;
                        }
                if (!activation[tier]) enabled=false;
            }
            if (!enabled) SKSE::log::info("Elemental Fury adapter disabled by configuration or incomplete data");
            Attach();
        }
        if (message->type==SKSE::MessagingInterface::kPostLoadGame || message->type==SKSE::MessagingInterface::kNewGame) {
            snapshots.fill(core::CurrentScaling()); ready={}; Attach();
        }
    }
}
