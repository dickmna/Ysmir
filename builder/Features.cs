using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;

sealed partial class YsmirBuild
{
    void MiraakCooldowns() {
        // Add only the official Miraak records that need a cooldown change.
        // His other named shouts and Greybeard variants already use 5/5/5.
        // Run after the player spell graph is built: no copied spells, keyword
        // edits, quest scripts or new FormIDs are needed for NPC cooldowns.
        foreach(var original in official.Values.OfType<IShoutGetter>().Where(s=>s.FormKey.ModKey==K("Dragonborn",0).ModKey && s.EditorID?.Contains("Miraak",StringComparison.OrdinalIgnoreCase)==true)) {
            if(original.WordsOfPower.All(w=>w.RecoveryTime==5))continue;
            var shout=(Shout)Copy(original,original.FormKey);
            foreach(var word in shout.WordsOfPower)word.RecoveryTime=5;
        }
    }
    readonly List<FormKey> magnitudeExtras=new();
    readonly List<FormKey> fixedMagnitude=new(), fixedDuration=new();
    void Elemental() {
        // Only the plugin's native spell/effect graph is used; no animation assets or DAC.
        var imported=new List<IMajorRecord>();
        foreach(var r in elemental.EnumerateMajorRecords()) {
            if(r is IShoutGetter)continue;
            var key=r.FormKey.ModKey==elemental.ModKey ? NewKey() : r.FormKey;
            if(key!=r.FormKey)remap[r.FormKey]=key;
            var c=Copy(r,key); if(key.ModKey==output)c.EditorID="YSMR_Elemental_"+r.EditorID; imported.Add(c);
        }
        foreach(var r in imported)r.RemapLinks(remap);
        // The direct actor-value implementation no longer uses FT's former
        // Elemental Fury weapon enchantment or its weapon-hit effect.
        mod.Remove(new[]{K("Skyrim",0x2C593),K("Skyrim",0x2C594)});
        var active=new Keyword(NewKey(),SkyrimRelease.SkyrimSE){EditorID="YSMR_ElementalFuryActive"};mod.Keywords.Add(active);
        for(int tier=0;tier<3;tier++) {
            var voice=mod.Spells[K("Skyrim",new uint[]{0x2C595,0x9CD4E,0x9CD4F}[tier])];
            voice.Name=Txt("Elemental Fury");
            var e=mod.MagicEffects[voice.Effects[0].BaseEffect.FormKey];
            // Recasting any rank replaces the previous rank through native
            // keyword dispelling; no stacked speed bonuses or extra script.
            e.Keywords=new(){new FormLink<IKeywordGetter>(active.FormKey)};
            e.Flags|=MagicEffect.Flag.DispelWithKeywords|MagicEffect.Flag.PowerAffectsDuration;
            e.Description=Txt("Attack faster for <dur> seconds. Attacks may release bursts of fire, frost and shock.");
            // Restore the actual vanilla bonuses and duration from the original spell.
            voice.Effects[0].Data!.Magnitude=new[]{1.3f,1.5f,1.7f}[tier];voice.Effects[0].Data!.Duration=15;
            var script=Script(e,"YSMR_ElementalFury");
            foreach(var (name,id) in new[]{("Fire",0x850u),("Frost",0x860u),("Shock",0x870u)})Prop(script,name,M(K(EF,id+(uint)tier)));
            // Radius is structural, not damage: never amplify the 12-foot burst cloak.
            foreach(var (id,shader) in new[]{(0x800u,0x1B212u),(0x810u,0x1F03Au),(0x820u,0x57C67u)}) {
                var cloak=mod.MagicEffects[M(K(EF,id+(uint)tier))];
                cloak.Flags &= ~(MagicEffect.Flag.PowerAffectsMagnitude|MagicEffect.Flag.PowerAffectsDuration);
                // Reduced Shaders: original fire/frost/shock surface shaders,
                // identical at all ranks. No full-cloak art or extra script.
                cloak.HitShader.SetTo(K("Skyrim",shader));
            }
            foreach(var id in new[]{0x850u,0x860u,0x870u}) {
                fixedMagnitude.Add(M(K(EF,id+(uint)tier)));
                fixedDuration.Add(M(K(EF,id+(uint)tier)));
            }
            foreach(var id in new[]{0x853u,0x863u,0x873u})magnitudeExtras.Add(M(K(EF,id+(uint)tier)));
        }
    }
    void Sprint() {
        // Reuse the reference's hostile-only cloak. Zero additional damage.
        var effect=(MagicEffect)Copy(sprint.MagicEffects[K(WS,0x800)],NewKey());effect.EditorID="YSMR_SprintStagger";
        var hit=Spell("YSMR_SprintStaggerSpell",TargetType.Aimed);hit.CastType=CastType.Concentration;
        hit.Effects.Add(Eff(effect.FormKey,1,0,1));
        var cloak=(MagicEffect)Copy(sprint.MagicEffects[K(WS,0x802)],NewKey());cloak.EditorID="YSMR_SprintCollision";
        cloak.Archetype=new MagicEffectCloakArchetype{Association=new FormLink<ISpellGetter>(hit.FormKey)};
        cloak.Flags &= ~(MagicEffect.Flag.PowerAffectsMagnitude|MagicEffect.Flag.PowerAffectsDuration);
        var collision=Spell("YSMR_SprintCollisionSpell");collision.Effects.Add(Eff(cloak.FormKey,9,0,1));
        fixedMagnitude.Add(collision.FormKey);fixedDuration.Add(collision.FormKey);
        // Keep geometry in a separate native helper cast, so the movement buff
        // can grow in duration without growing the collision radius or lifetime.
        for(int i=0;i<3;i++) {
            var voice=mod.Spells[K("Skyrim",new uint[]{0x2F7BE,0x2F7BF,0x2F7C0}[i])];
            fixedMagnitude.Add(voice.FormKey);
            var movement=mod.MagicEffects[voice.Effects.Last().BaseEffect.FormKey];
            Prop(Script(movement,"magicCastAnotherSpellScript"),"CastSpellName",collision.FormKey);
        }
    }
    void SoulTear() {
        // A private native Cone retains the original visual, speed, range and
        // zero spread, but survives actor collisions. Do not change the shared
        // projectile, which Dawnguard quest effects also use.
        var projectile=(Projectile)Copy(Orig<IProjectileGetter>("Dawnguard",0x7CBB),NewKey());
        projectile.EditorID="YSMR_SoulTearPiercing";projectile.Type=Projectile.TypeEnum.Cone;
        foreach(var id in new uint[]{0x7CB5,0x15714,0x1AA33,0x7CB3})
            mod.MagicEffects[K("Dawnguard",id)].Projectile.SetTo(projectile.FormKey);
        // Keep each rank's complete payload and original death/reanimation script.
        foreach(var id in new uint[]{0x15716,0x15715,0x38BD})magnitudeExtras.Add(K("Dawnguard",id));
        // FT's first two ranks gain functional damage; third rank retains vanilla 300.
        var three=mod.Spells[K("Dawnguard",0x38BD)];
        three.Effects.Single(e=>e.BaseEffect.FormKey==K("Dawnguard",0x1AA33)).Data!.Magnitude=300;
    }
    static ConditionFloat Cond(ConditionData data,float compare=1,CompareOperator op=CompareOperator.EqualTo,bool or=false) =>
        new(){Data=data,ComparisonValue=compare,CompareOperator=op,Flags=or?Condition.Flag.OR:0};
    static GetIsIDConditionData Is(FormKey key) {var d=new GetIsIDConditionData();d.Object.Link.SetTo(key);return d;}
    static IsInListConditionData In(FormKey key) {var d=new IsInListConditionData();d.FormList.Link.SetTo(key);return d;}
    void Storm() {
        var effects=new HashSet<FormKey>();
        var shout=mod.Shouts[K("Skyrim",0x7097D)];
        foreach(var word in shout.WordsOfPower) {
            var s=mod.Spells[word.Spell.FormKey];
            foreach(var e in s.Effects){effects.Add(e.BaseEffect.FormKey);if(e.Data!.Area>0)e.Data.Area=600;}
        }
        foreach(var key in effects) {
            var e=mod.MagicEffects.TryGetValue(key,out var v)?v:(MagicEffect)Copy(Res(key),key);
            if(new uint[]{0xE3F0A,0xE3F09,0xD5E81}.Contains(key.ID))e.Description=Txt("Summons a lightning storm for <dur> seconds. Its bolts damage Health and Magicka.");
        }
        // Keep the game's original emitter and FT's configuration. The private
        // damage copy receives MagicShout in KeywordsAndCleanup, allowing the
        // original Companion's Insight perk to handle its own protection rules.
        Copy(Orig<IMagicEffectGetter>("Skyrim",0xE4CB6),K("Skyrim",0xE4CB6));
        foreach(var name in new[]{"StormCallLightningBolt01","StormCallLightningBolt02","StormCallLightningBolt03"}) {
            var original=official.Values.OfType<ISpellGetter>().Single(s=>s.EditorID==name);
            Copy(original,original.FormKey);magnitudeExtras.Add(original.FormKey);
        }
    }
    FormList List(string name,IEnumerable<FormKey> items) {
        var l=new FormList(NewKey(),SkyrimRelease.SkyrimSE){EditorID=name};foreach(var k in items)l.Items.Add(new FormLink<ISkyrimMajorRecordGetter>(k));mod.FormLists.Add(l);return l;
    }
    static PerkCondition Tab(byte tab,params Condition[] conditions) {var p=new PerkCondition{RunOnTabIndex=tab};p.Conditions.AddRange(conditions);return p;}
    void Progression() {
        var perk=mod.Perks[M(K(FT,0xF86))];perk.EditorID="YSMR_DragonSoulPower";perk.Name=Txt("Dragon Soul Power");
        perk.Description=Txt("Each held dragon soul increases the power of your Voice by 2.5%.");perk.Conditions.Clear();perk.Effects.Clear();
        var slowIDs=new uint[]{0x48AD0,0x48AD1,0x48AD2}.Select(x=>K("Skyrim",x)).ToArray();
        fixedMagnitude.AddRange(slowIDs);
        var magnitudeExceptions=List("YSMR_FixedMagnitudeSpells",fixedMagnitude.Distinct());
        var durationExceptions=List("YSMR_FixedDurationSpells",fixedDuration.Distinct());
        // HasKeyword on the magic-item tab is the same native pattern used by
        // BCE's Empowered Voice. No allowlist and no third-party form references.
        foreach(var (entry,exceptions) in new[]{(APerkEntryPointEffect.EntryType.ModSpellMagnitude,magnitudeExceptions),(APerkEntryPointEffect.EntryType.ModSpellDuration,durationExceptions)}) {
            var p=new PerkEntryPointModifyActorValue{EntryPoint=entry,PerkConditionTabCount=3,Priority=10,ActorValue=ActorValue.DragonSouls,Value=.025f,Modification=PerkEntryPointModifyActorValue.ModificationType.MultiplyOnePlusAVMult};
            var keyword=new HasKeywordConditionData();keyword.Keyword.Link.SetTo(K("Skyrim",0x46B99));
            p.Conditions.Add(Tab(1,Cond(keyword),Cond(In(exceptions.FormKey),0)));perk.Effects.Add(p);
        }
        var playerTime=new PerkEntryPointModifyValue{EntryPoint=APerkEntryPointEffect.EntryType.ModPlayerMagicSlowdown,PerkConditionTabCount=1,Priority=100,Modification=PerkEntryPointModifyValue.ModificationType.Set,Value=1};perk.Effects.Add(playerTime);
        // The Ritual Stone's native pattern: add 100 only when evaluating the
        // actual secondary reanimation spell, not the outer Voice spell.
        var soulTearLimit=new PerkEntryPointModifyValue{EntryPoint=APerkEntryPointEffect.EntryType.ModCommandedActorLimit,PerkConditionTabCount=2,Priority=0,Modification=PerkEntryPointModifyValue.ModificationType.Add,Value=100};
        soulTearLimit.Conditions.Add(Tab(1,Cond(Is(K("Dawnguard",0x7CBA)))));perk.Effects.Add(soulTearLimit);
        mod.MagicEffects[K("Skyrim",0x48ACD)].Flags &= ~MagicEffect.Flag.PowerAffectsMagnitude;
        // FT's fixed PushForce is a script parameter, not a spell magnitude.
        foreach(var script in ((MagicEffect)Named("FT_VoiceUnrelentingForceEffect03")).VirtualMachineAdapter?.Scripts??[])
            if(script.Name.Equals("VoicePushEffectScript",StringComparison.OrdinalIgnoreCase))script.Name="YSMR_VoicePush";
    }
}
