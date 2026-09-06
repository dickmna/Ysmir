Scriptname YsmirSlowTimeScript extends ActiveMagicEffect
Spell Property CastSpellName Auto
Event OnEffectStart(Actor akTarget, Actor akCaster)
    Ysmir.CastSlowTime(CastSpellName, akTarget, akCaster, GetBaseObject())
EndEvent
