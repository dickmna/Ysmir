#include "core.h"
#include "modules.h"
#include "RuntimeForms.h"
#include <SKSE/SKSE.h>
#include <algorithm>
#include <array>
#include <optional>
#include <random>

namespace
{
    constexpr RE::FormID ElementalFury=0x32921;
    std::array<std::array<RE::SpellItem*,3>,3> spells{};
    std::array<RE::EffectSetting*,3> activation{};
    std::array<double,3> ready{};
    ysmir::progression::ScalingSnapshot snapshot;
    bool enabled{true}; float chance{.5f}, cooldown{2.f};
    std::mt19937 rng{std::random_device{}()};

    std::optional<std::uint32_t> ActiveTier(RE::PlayerCharacter& player)
    {
        auto* target=player.AsMagicTarget();
        if (!target) return {};
        for (std::uint32_t reverse=0; reverse<3; ++reverse) {
            auto tier=2-reverse;
            if (activation[tier] && target->HasMagicEffect(activation[tier])) return tier;
        }
        return {};
    }
    class AnimationSink final : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static AnimationSink& Get() { static AnimationSink value; return value; }
        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
        {
            auto* player=RE::PlayerCharacter::GetSingleton();
            if (!enabled || !ysmir::progression::Enabled() || !event || !player || event->holder!=player ||
                _stricmp(event->tag.c_str(),"hitframe")!=0 || ysmir::progression::Paused()) return RE::BSEventNotifyControl::kContinue;
            const auto tier=ActiveTier(*player);
            if (!tier) return RE::BSEventNotifyControl::kContinue;
            std::uniform_real_distribution<float> roll(0.f,1.f);
            const auto now=ysmir::progression::Now();
            for (std::uint32_t element=0; element<3; ++element)
                if (now>=ready[element] && roll(rng)<=chance &&
                    ysmir::core::CastFeatureSpell(spells[element][*tier],*player,player,snapshot)) ready[element]=now+cooldown;
            return RE::BSEventNotifyControl::kContinue;
        }
    };
    void Attach()
    { if (auto* player=RE::PlayerCharacter::GetSingleton()) player->AddAnimationGraphEventSink(&AnimationSink::Get()); }
}

namespace ysmir::elemental_fury
{
    void OnShout(RE::TESShout* shout,std::uint32_t,const progression::ScalingSnapshot& value)
    {
        if (shout && shout->GetFormID()==ElementalFury) { snapshot=value; ready.fill(progression::Now()); }
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
            snapshot=core::CurrentScaling(); ready.fill(progression::Now()); Attach();
        }
    }
}
