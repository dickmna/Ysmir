ScriptName YSMR_Shrine Extends ActiveMagicEffect

;-- Variables ---------------------------------------

;-- Properties --------------------------------------
Message Property MQpathToHHShrineBlessingText Auto
Quest Property dunMasterQST Auto

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Event OnEffectStart(Actor akTarget, Actor akCaster)
  MQpathToHHShrineBlessingText.Show(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
EndEvent

Event OnEffectFinish(Actor akTarget, Actor akCaster)
  dunmasterqstscript QuestScript = dunMasterQST as dunmasterqstscript
  QuestScript.HHshrine01 = False
  QuestScript.HHshrine02 = False
  QuestScript.HHshrine03 = False
  QuestScript.HHshrine04 = False
  QuestScript.HHshrine05 = False
  QuestScript.HHshrine06 = False
  QuestScript.HHshrine07 = False
  QuestScript.HHshrine08 = False
  QuestScript.HHshrine09 = False
  QuestScript.HHshrine10 = False
EndEvent
