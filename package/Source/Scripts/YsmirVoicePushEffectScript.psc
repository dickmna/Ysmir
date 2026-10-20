Scriptname YsmirVoicePushEffectScript extends ActiveMagicEffect

Int Property PushForce Auto

Event OnEffectStart(Actor Target, Actor Caster)
    If Target && Caster
        Float force = Ysmir.GetScaledPushForce(PushForce as Float, Caster, Self)
        Caster.PushActorAway(Target, force)
    EndIf
EndEvent
