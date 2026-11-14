ScriptName YSMR_SpeedRefresh Extends ActiveMagicEffect

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Event OnEffectStart(Actor akTarget, Actor akCaster)
  akTarget.ModActorValue("CarryWeight", 0.100000001)
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
  Utility.Wait(0.100000001)
  akTarget.ModActorValue("CarryWeight", -0.100000001)
EndEvent
