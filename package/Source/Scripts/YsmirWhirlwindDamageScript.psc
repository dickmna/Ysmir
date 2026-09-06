Scriptname YsmirWhirlwindDamageScript extends ActiveMagicEffect

; Keep the original DLC2WhirlwindDamageScript property name so the exact
; 49359 VMAD value (force=10) can be remapped without changing its baseline.
Int Property force Auto

Event OnEffectStart(Actor Target, Actor Caster)
    If Target && Caster
        Float scaledForce = Ysmir.GetScaledPushForce(force as Float, Caster, Self)
        Caster.PushActorAway(Target, scaledForce)
    EndIf
EndEvent
