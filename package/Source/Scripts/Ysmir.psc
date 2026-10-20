Scriptname Ysmir Hidden
; -1 cancels this target, 0 pauses indoors/loading, 1 prepares one native RemoteCast.
Int Function StormReady(ObjectReference akSource, Actor akTarget) Global Native
Function StormFinished(ObjectReference akSource) Global Native
Float Function GetScaledPushForce(Float afVanillaPushForce, Actor akCaster, ActiveMagicEffect akEffect) Global Native
Int Function GetDragonSoulCount() Global Native
Function CastFromEffect(Spell akSpell, Actor akTarget, Actor akCaster, ActiveMagicEffect akWrapper) Global Native
