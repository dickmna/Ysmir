Scriptname YSMR_ShoutCooldown extends ReferenceAlias

Event OnSpellCast(Form akSpell)
    If akSpell as Shout
        Actor player = GetActorReference()
        player.SetVoiceRecoveryTime(5.0 * player.GetActorValue("ShoutRecoveryMult"))
    EndIf
EndEvent
