Scriptname YSMR_ElementalFury extends ActiveMagicEffect
; Replaces DAC's three independent 50% hitframe rolls and 2-second cooldowns.
; All functions used here are vanilla Papyrus. No modified animation is needed.
Spell Property Fire Auto
Spell Property Frost Auto
Spell Property Shock Auto
Actor bearer
Bool running
Float fireReady
Float frostReady
Float shockReady
Float lastClock

Event OnEffectStart(Actor akTarget, Actor akCaster)
  bearer = akTarget
  running = True
  RegisterForAnimationEvent(bearer, "HitFrame")
EndEvent

Event OnAnimationEvent(ObjectReference akSource, String asEventName)
  If !running || akSource != bearer || asEventName != "HitFrame"
    Return
  EndIf
  Float now = Utility.GetCurrentRealTime()
  ; Real-time timestamps reset when the game executable restarts.
  If now < lastClock
    fireReady = 0.0
    frostReady = 0.0
    shockReady = 0.0
  EndIf
  lastClock = now
  If now >= fireReady && Utility.RandomInt(1, 100) <= 50
    fireReady = now + 2.0
    Fire.Cast(bearer, bearer)
  EndIf
  If running && now >= frostReady && Utility.RandomInt(1, 100) <= 50
    frostReady = now + 2.0
    Frost.Cast(bearer, bearer)
  EndIf
  If running && now >= shockReady && Utility.RandomInt(1, 100) <= 50
    shockReady = now + 2.0
    Shock.Cast(bearer, bearer)
  EndIf
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
  running = False
  UnregisterForAnimationEvent(akTarget, "HitFrame")
EndEvent
