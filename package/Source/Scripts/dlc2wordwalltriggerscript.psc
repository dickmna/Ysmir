ScriptName DLC2WordWallTriggerScript Extends ObjectReference
{ This script controls the imagespaces and animations associated with the wordwall

What you need to set up regarding the word wall:
- The Word Wall
- The three different word Text of the shout on the word wall that the player learns
- An xmarker next to each of the three words, which acts as the look target
- The three mono sounds, one placed at each of the three words the player learns
- The stereo sound the player hears when close to the wall
- The WordWallTrigger that has the DLC2WordWallTriggerScript and the DLC2WordWallTrigger02Script on it

The only properties you need to tag are:
- WordWall01 -> point this to the first word text of shout
- WordWall02 -> point this to the second word text of shout
- WordWall03 -> point this to the third word text of shout
- myWord01 -> the first word of the shout
- myWord02 -> the second word of the shout
- myWord03 -> the third word of the shout
- lookTargetWord01 -> the xmarker in front of the first word
- lookTargetWord02 -> the xmarker in front of the second word
- lookTargetWord03 -> the xmarker in front of the third word
- word01Sound -> the mono sound for word which should be placed next to the first word on the wall
- word02Sound -> the mono sound for word which should be placed next to the second word on the wall
- word03Sound -> the mono sound for word which should be placed next to the third word on the wall
- soundWallWhisper -> place this near the wall, it's the stereo sound the player hears when he enters the trigger
- shoutGlobal -> point this to the global variable of your shout
Optional properties:
- myQuest -> optional, if you want to set a stage on a quest once the player learns the word
- myStage -> if you are using a quest, then this is the stage you want to set the quest to
- myEnabler -> optional, this ref gets enabled once the player learns the word }

;-- Variables ---------------------------------------
Bool doOnce
Bool finishedIMOD02
Bool finishedIMOD03
Bool finishedWordAnim02
Bool finishedWordAnim03
Int iInTrigger
Bool inTrigger

;-- Properties --------------------------------------
wordwalllistenerquestscript Property FreeformHighHrothgarA Auto
{ Pointer to WordWallListernQuestScript attached to FreeformHighHrothgarA }
ImageSpaceModifier Property IMODLoop02 Auto
{ IMOD02 (Middle) loop to play }
ImageSpaceModifier Property IMODLoop03 Auto
{ IMOD03 (Inner) loop to play }
wordwalllistenerquestscript Property WICastMagic04 Auto
{ Pointer to WordWallListernQuestScript attached to WICastMagic }
Int Property innerRadius = 300 Auto
{ player needs to be inside this radius to learn the word }
ObjectReference Property lookTarget Auto hidden
ObjectReference Property lookTargetWord01 Auto
{ the xmarker the player needs to be looking at for word01 }
ObjectReference Property lookTargetWord02 Auto
{ the xmarker the player needs to be looking at for word02 }
ObjectReference Property lookTargetWord03 Auto
{ the xmarker the player needs to be looking at for word03 }
Int Property middleRadius = 700 Auto
{ middle radius of word effects }
ObjectReference Property myEnabler Auto
{ if needed, reference to enable when player learns the word }
ObjectReference Property myLocRefMarker Auto
{ loc ref that gets disabled once word has been learned }
Quest Property myQuest Auto
{ if needed, quest to use for when the player learns the word }
Int Property myStage Auto
{ if needed, stage to set myQuest to when player learns word }
WordOfPower Property myWord Auto hidden
WordOfPower Property myWord01 Auto
{ first word of the shout you want the player to learn }
WordOfPower Property myWord02 Auto
{ second word of the shout you want the player to learn }
WordOfPower Property myWord03 Auto
{ third word of the shout you want the player to learn }
Int Property outerRadius = 1200 Auto
{ outer radius of word effects }
GlobalVariable Property shoutGlobal Auto
{ the shout global used in determing which word to enable }
ObjectReference Property soundWallWhisper Auto
{ stereo sound that plays in the player's head }
ObjectReference Property word01Sound Auto
{ mono sound that plays from word01 }
ObjectReference Property word02Sound Auto
{ mono sound that plays from word02 }
ObjectReference Property word03Sound Auto
{ mono sound that plays from word03 }
ImageSpaceModifier Property wordIMODWordLearned Auto
{ IMOD to play when player learns word }
Bool Property wordLearned Auto hidden
ObjectReference Property wordSound Auto hidden
ObjectReference Property wordWall Auto hidden
ObjectReference Property wordWall01 Auto
{ The text ref for word01 that is placed on the word wall }
ObjectReference Property wordWall02 Auto
{ The text ref for word02 that is placed on the word wall }
ObjectReference Property wordWall03 Auto
{ The text ref for word03 that is placed on the word wall }

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Bool Function isLooking()
  Return Game.getPlayer().HasLOS(lookTarget) && !wordLearned
EndFunction

Function StartFX()
  If (Game.getPlayer().getDistance(lookTarget) < innerRadius as Float) && !Game.getPlayer().IsInCombat() || (Game.getPlayer().getDistance(lookTarget) <= 200 as Float)
    Self.GotoState("done")
    wordLearned = True
    wordIMODWordLearned.Apply(1 as Float)
    Utility.wait(2 as Float)
    wordWall.playAnimation("Learned")
    finishedIMOD02 = False
    finishedIMOD03 = False
    Game.teachWord(myWord)
    Game.UnlockWord(myWord)
    shoutGlobal.value = shoutGlobal.value + 1 as Float
    wordSound.disableNoWait(False)
    soundWallWhisper.disableNoWait(False)
    If myQuest
      myQuest.setStage(myStage)
    EndIf
    If myEnabler
      myEnabler.enableNoWait(False)
    EndIf
    If myLocRefMarker
      myLocRefMarker.disableNoWait(False)
    EndIf
    Self.PokeWordWallListenerQuests()
    Return
  ElseIf (Game.getPlayer().getDistance(lookTarget) < middleRadius as Float) && (Game.getPlayer().getDistance(lookTarget) >= innerRadius as Float)
    If !finishedIMOD03 && !Game.getPlayer().IsInCombat()
      IMODLoop03.ApplyCrossFade(0.25)
      finishedIMOD02 = False
      finishedIMOD03 = True
    EndIf
    If !finishedWordAnim03
      wordWall.playAnimation("Dark3Wild")
      finishedWordAnim02 = True
      finishedWordAnim03 = True
    EndIf
    Return
  ElseIf (Game.getPlayer().getDistance(lookTarget) < outerRadius as Float) && (Game.getPlayer().getDistance(lookTarget) >= middleRadius as Float)
    If finishedIMOD03
      finishedWordAnim03 = False
      wordWall.playAnimation("Dark3ExitWild")
      finishedWordAnim02 = True
    EndIf
    If !finishedIMOD02 && !Game.getPlayer().IsInCombat()
      IMODLoop02.ApplyCrossFade(0.25)
      finishedIMOD03 = False
      finishedIMOD02 = True
    EndIf
    If !finishedWordAnim02
      wordWall.playAnimation("Dark2Wild")
      finishedWordAnim02 = True
      finishedWordAnim03 = False
    EndIf
    Return
  ElseIf (Game.getPlayer().getDistance(lookTarget) < 2000 as Float) && (Game.getPlayer().getDistance(lookTarget) >= outerRadius as Float)
    If finishedIMOD02 || finishedIMOD03
      wordWall.playAnimation("Dark2ExitWild")
      ImageSpaceModifier.RemoveCrossFade(0.5)
      finishedIMOD02 = False
      finishedIMOD03 = False
      finishedWordAnim02 = False
      finishedWordAnim03 = False
    EndIf
    Return
  EndIf
EndFunction

Function StopFX()
  If wordLearned
    ImageSpaceModifier.RemoveCrossFade(3.0)
  Else
    ImageSpaceModifier.RemoveCrossFade(0.5)
  EndIf
  finishedIMOD02 = False
  finishedIMOD03 = False
  finishedWordAnim02 = False
  finishedWordAnim03 = False
  If !finishedWordAnim02 && !finishedWordAnim03 && iInTrigger <= 0
    wordWall.playAnimation("Dark1Wild")
  ElseIf !wordLearned
    wordWall.playAnimation("Dark1Wild")
  EndIf
EndFunction

Function StartWordWall()
  iInTrigger += 1
  wordSound.enableNoWait(False)
  soundWallWhisper.enableNoWait(False)
  wordWall.playAnimation("Dark1Wild")
  Debug.tracestack("registering for update", 0)
  Self.RegisterForSingleUpdate(0.5)
EndFunction

Function StopWordWall()
  wordSound.disableNoWait(False)
  soundWallWhisper.disableNoWait(False)
  iInTrigger -= 1
EndFunction

Event onUpdate()
  Self.GotoState("updating")
  Bool cleanUpFinished = True
  While iInTrigger > 0 && !wordLearned
    If Self.isLooking()
      Self.StartFX()
      cleanUpFinished = False
      doOnce = False
    ElseIf !cleanUpFinished
      Self.StopFX()
      cleanUpFinished = True
    ElseIf !doOnce
      doOnce = True
      wordWall.playAnimation("DarkXWild")
    EndIf
    Utility.wait(0 as Float)
  EndWhile
  If !cleanUpFinished
    Self.StopFX()
  EndIf
  If Self.GetState() != "done"
    Self.GotoState("")
  EndIf
EndEvent

Function PokeWordWallListenerQuests()
  If WICastMagic04.isRunning()
    WICastMagic04.CheckLocRefMarker(myLocRefMarker)
  EndIf
  If FreeformHighHrothgarA.isRunning()
    FreeformHighHrothgarA.CheckLocRefMarker(myLocRefMarker)
  EndIf
EndFunction

;-- State -------------------------------------------
State done

  Function StartWordWall()
    ; Empty function
  EndFunction
EndState

;-- State -------------------------------------------
State updating

  Event onUpdate()
    ; Empty function
  EndEvent
EndState
