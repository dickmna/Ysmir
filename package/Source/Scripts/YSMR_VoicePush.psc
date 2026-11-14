Scriptname YSMR_VoicePush extends ActiveMagicEffect
; The vanilla push event, with player-only held-soul scaling. No SKSE or polling.
Int Property PushForce Auto

Event OnEffectStart(Actor Target, Actor Caster)
  If !Target || !Caster
    Return
  EndIf
  Float force = PushForce as Float
  If Caster == Game.GetPlayer()
    force *= 1.0 + 0.025 * Caster.GetActorValue("DragonSouls")
  EndIf
  Caster.PushActorAway(Target, force)
EndEvent
