ScriptName YSMR_FrostBreath Extends ActiveMagicEffect

;-- Variables ---------------------------------------
Bool AllGood
Actor SpellTarget

;-- Properties --------------------------------------
GlobalVariable Property DLC2BlackBookReward3 Auto
Actor Property PlayerRef Auto
ActorBase Property Wyrm Auto

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Event OnEffectStart(Actor akTarget, Actor akCaster)
  If (DLC2BlackBookReward3.GetValue() == 3 as Float) && akCaster == PlayerRef
    SpellTarget = akTarget
    AllGood = True
  EndIf
EndEvent

Event OnDying(Actor akKiller)
  If AllGood
    SpellTarget.PlaceActorAtMe(Wyrm, 4, None)
  EndIf
EndEvent
