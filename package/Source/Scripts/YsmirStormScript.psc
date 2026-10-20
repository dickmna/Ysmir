Scriptname YsmirStormScript Hidden

; One independent burst; SKSE discovers targets and validates each actual cast.
; Waits are sequential and never pre-queue the remaining bolts.
Function Burst(Spell akBolt, ObjectReference akSource, ObjectReference akAim, Actor akTarget, Actor akBlame, Int aiWords, Float afGapMin, Float afGapMax, Float afRetry) Global
    Int strikes = 0
    Bool running = True
    While running && strikes < aiWords
        Int status = Ysmir.StormReady(akSource, akTarget)
        If status < 0
            running = False
        ElseIf status == 0
            Utility.Wait(afRetry)
        Else
            akBolt.RemoteCast(akSource, akBlame, akAim)
            strikes += 1
            If strikes < aiWords
                Utility.Wait(Utility.RandomFloat(afGapMin, afGapMax))
            EndIf
        EndIf
    EndWhile
    Ysmir.StormFinished(akSource)
EndFunction
