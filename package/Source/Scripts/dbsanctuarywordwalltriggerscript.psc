ScriptName DBSanctuaryWordWallTriggerScript Extends WordWallTriggerScript
{ A variant on WordWallTriggerScript for the DB Sanctuary. This variant changes some of the radii,
since the Word Wall SFX were distracting at their default volume and interfered with the opening scene.


This script controls the imagespaces and animations associated with the wordwall

What you need to set up regarding the word wall:
- The Word Wall
- The three different word Text of the shout on the word wall that the player learns
- An xmarker next to each of the three words, which acts as the look target
- The three mono sounds, one placed at each of the three words the player learns
- The stereo sound the player hears when close to the wall
- The WordWallTrigger that has the WordWallTriggerScript and the WordWallTrigger02Script on it

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

;-- Functions ---------------------------------------

; Skipped compiler generated GetState

; Skipped compiler generated GotoState

Bool Function isLooking()
  Return Game.getPlayer().HasLOS(lookTarget) && !wordLearned
EndFunction

Function StartFX()
  If Game.getPlayer().getDistance(lookTarget) < innerRadius as Float
    Self.GotoState("done")
    wordLearned = True
    wordIMODWordLearned.Apply(1 as Float)
    Utility.wait(2 as Float)
    wordWall.playAnimation("Learned")
    IMODLoop02.remove()
    IMODLoop03.remove()
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
    Return
  ElseIf (Game.getPlayer().getDistance(lookTarget) < middleRadius as Float) && (Game.getPlayer().getDistance(lookTarget) >= innerRadius as Float)
    wordSound.enableNoWait(False)
    soundWallWhisper.enableNoWait(False)
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
    wordSound.disableNoWait(False)
    soundWallWhisper.disableNoWait(False)
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
  wordWall.playAnimation("Dark1Wild")
  Self.RegisterForSingleUpdate(0.5)
EndFunction

Function StopWordWall()
  wordSound.disableNoWait(False)
  soundWallWhisper.disableNoWait(False)
  iInTrigger -= 1
EndFunction

Event onUpdate()
  Bool cleanUpFinished = True
  While iInTrigger > 0 && !wordLearned
    If Self.isLooking()
      Self.StartFX()
      cleanUpFinished = False
      doOnce = False
    ElseIf !cleanUpFinished
      Self.StopFX()
      cleanUpFinished = True
    ElseIf !doOnce && Game.getPlayer().IsInCombat()
      doOnce = True
      wordWall.playAnimation("DarkXWild")
    EndIf
    Utility.wait(0 as Float)
  EndWhile
  If !cleanUpFinished
    Self.StopFX()
  EndIf
EndEvent

;-- State -------------------------------------------
State done

  Function StartWordWall()
    ; Empty function
  EndFunction
EndState
