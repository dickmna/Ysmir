Scriptname YsmirSlowTimeScript extends ActiveMagicEffect
Spell Property CastSpellName Auto
Event OnEffectStart(Actor akTarget, Actor akCaster)
    Ysmir.CastFromEffect(CastSpellName, akTarget, akCaster, Self)
EndEvent
