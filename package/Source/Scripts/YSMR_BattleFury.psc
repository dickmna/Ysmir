ScriptName YSMR_BattleFury Extends ActiveMagicEffect

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
Spell Property DLC2VoiceElementalFury Auto
Actor Property PlayerRef Auto

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Event OnEffectStart(Actor akTarget, Actor akCaster)
  DLC2VoiceElementalFury.Cast(PlayerRef as ObjectReference, akTarget as ObjectReference)
EndEvent
