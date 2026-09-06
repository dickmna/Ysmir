Scriptname YsmirDragonAspectSummonScript extends ActiveMagicEffect

; Forceful Tongue's combat summon lifecycle, bound only to the private graph.
Spell Property FT_DLC2VoiceDragonAspectSummon Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Ysmir.CastFromEffect(FT_DLC2VoiceDragonAspectSummon, akTarget, akTarget, Self)
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
    akTarget.DispelSpell(FT_DLC2VoiceDragonAspectSummon)
EndEvent
