ScriptName YSMR_AncientDragonborn Extends ActiveMagicEffect

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
Spell Property YSMR_DragonAspectSummon Auto

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Event OnEffectStart(Actor akTarget, Actor akCaster)
  YSMR_DragonAspectSummon.Cast(akTarget as ObjectReference, None)
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
  akTarget.DispelSpell(YSMR_DragonAspectSummon)
EndEvent
