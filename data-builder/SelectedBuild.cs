using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Archives;
using Mutagen.Bethesda.Strings;

// All inputs are versioned upstream plugins plus the user's official game files.
// FormIds.json preserves identifiers only; it contains no reference ESP or game data.
internal sealed class SelectedBuild : IDisposable
{
    static readonly ModKey OutputKey = ModKey.FromFileName("Ysmir.esp");
    readonly List<ISkyrimModDisposableGetter> masters = new(), inputs = new();
    readonly Dictionary<FormKey, IMajorRecordGetter> official = new(), selected = new();
    readonly Dictionary<string,uint> identifiers;
    readonly Dictionary<FormKey, FormKey> map = new();
    readonly HashSet<FormKey> importedOfficial = new(), importedSelected = new();
    readonly SkyrimMod mod = new(OutputKey, SkyrimRelease.SkyrimSE);
    readonly List<IShoutGetter> roots = new();
    readonly List<FormKey> runtimeRoots = new();
    readonly string strings = Path.Combine(Path.GetTempPath(), "Ysmir-strings-" + Guid.NewGuid().ToString("N"));
    uint nextId = 0x150000;
    static FormKey K(string master, uint id) => new(ModKey.FromFileName(master + ".esm"), id);
    static string Suffix(FormKey key) => Path.GetFileNameWithoutExtension(key.ModKey.FileName.String) + $"_{key.ID:X6}";
    static bool Gameplay(IMajorRecordGetter r) => r is ISpellGetter or IMagicEffectGetter or IProjectileGetter;
    static void SetArchetype(MagicEffect e, MagicEffectArchetype.TypeEnum type) => e.Archetype = new MagicEffectArchetype { Type=type, ActorValue=((IMagicEffectGetter)e).Archetype.ActorValue };
    FormKey Allocate() { while (identifiers.ContainsValue(nextId)) ++nextId; return new(OutputKey,nextId++); }
    FormKey Id(string name) {
        if (!identifiers.TryGetValue(name,out var id)) { id=Allocate().ID; identifiers.Add(name,id); }
        return new(OutputKey,id);
    }
    IMajorRecord Named(string name) => mod.EnumerateMajorRecords().Single(r=>r.EditorID==name);
    FormKey Private(FormKey key) => map[key];
    static FormKey RootKey(IShoutGetter s) { var parts=s.EditorID!.Split('_'); return K(parts[2],Convert.ToUInt32(parts[3],16)); }
    Spell Voice(FormKey key,int tier) => mod.Spells[mod.Shouts[Private(key)].WordsOfPower[tier].Spell.FormKey];
    ISpellGetter OriginalVoice(FormKey key,int tier) => (ISpellGetter)official[((IShoutGetter)official[key]).WordsOfPower[tier].Spell.FormKey];

    public SelectedBuild(string data,string inputDirectory)
    {
        identifiers=JsonSerializer.Deserialize<Dictionary<string,string>>(File.ReadAllText(Path.Combine(AppContext.BaseDirectory,"FormIds.json")))!
            .ToDictionary(p=>p.Key,p=>Convert.ToUInt32(p.Value,16),StringComparer.Ordinal);
        Directory.CreateDirectory(strings);
        foreach (var archiveName in new[]{"Skyrim - Interface.bsa","Update.bsa","Dawnguard.bsa","Dragonborn.bsa"}) {
            var archivePath=Path.Combine(data,archiveName);
            if (!File.Exists(archivePath)) continue;
            var archive=Archive.CreateReader(GameRelease.SkyrimSE,archivePath);
            if (archive.TryGetFolder("strings",out var folder))
                foreach(var file in folder.Files.Where(f=>f.Path.Contains("_english.",StringComparison.OrdinalIgnoreCase)))
                    File.WriteAllBytes(Path.Combine(strings,Path.GetFileName(file.Path)),file.GetBytes());
        }
        foreach(var name in new[]{"Skyrim","Update","Dawnguard","Dragonborn"}) {
            var key=ModKey.FromFileName(name+".esm");
            var loaded=SkyrimMod.CreateFromBinaryOverlay(new ModPath(key,Path.Combine(data,key.FileName.String)),SkyrimRelease.SkyrimSE,
                new BinaryReadParameters { StringsParam=new StringsReadParameters { StringsFolderOverride=strings,BsaFolderOverride=strings,TargetLanguage=Language.English } });
            masters.Add(loaded);
            foreach(var record in loaded.EnumerateMajorRecords()) official[record.FormKey]=record;
        }
        foreach(var pair in official) selected[pair.Key]=pair.Value;
        foreach(var spec in SourceRules.Inputs) {
            var path=Path.Combine(inputDirectory,spec.File);
            if(Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)))!=spec.Sha256)
                throw new InvalidDataException("Unexpected upstream version: "+spec.File);
            var loaded=SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName(spec.File),path),SkyrimRelease.SkyrimSE);
            inputs.Add(loaded);
            foreach(var record in loaded.EnumerateMajorRecords()) selected[record.FormKey]=record;
        }
    }

    string Name(IMajorRecordGetter source)
    {
        var key=source.FormKey;
        var special=SourceRules.ImportedName(key,source.EditorID);
        if(special!=null) return special;
        if(official.ContainsKey(key)) {
            var type=source switch { ISpellGetter=>"Spell",IMagicEffectGetter=>"Effect",IProjectileGetter=>"Projectile",INpcGetter=>"Npc",IStaticGetter=>"Inventory",_=>"Record" };
            var conventional="YSMIR_Player"+type+"_"+Suffix(key);
            if(identifiers.ContainsKey(conventional) || source is ISpellGetter or IMagicEffectGetter or IProjectileGetter or INpcGetter) return conventional;
        }
        var name=source.EditorID ?? Suffix(key);
        if(identifiers.ContainsKey(name)) return name;
        return "YSMIR_Selected_"+name;
    }

    bool CanImport(IMajorRecordGetter record)
    {
        if(Gameplay(record) || record is IPerkGetter or IObjectEffectGetter) return true;
        if(record is IArtObjectGetter or IEffectShaderGetter) return !official.ContainsKey(record.FormKey);
        if(record is INpcGetter) return SourceRules.Heroes.Any(id=>record.FormKey==K("Skyrim",id)) || record.FormKey==K("Dragonborn",0x0265AA) || record.FormKey==K("Dragonborn",0x01F990);
        return !official.ContainsKey(record.FormKey) && record is IKeywordGetter or IStaticGetter;
    }

    FormKey Import(FormKey key,bool selectedMechanics=false,bool keepNumbers=false)
    {
        if(key.IsNull) return key;
        var db=selectedMechanics ? selected : official;
        if(!db.TryGetValue(key,out var source)) {
            if(official.ContainsKey(key)) return key;
            throw new InvalidDataException("Unresolved upstream dependency: "+key);
        }
        if(!CanImport(source)) {
            if(!official.ContainsKey(key)) throw new InvalidDataException("Unselected upstream record type: "+key+" "+source.GetType().Name);
            return key;
        }
        if(importedSelected.Contains(key) || (!selectedMechanics && importedOfficial.Contains(key))) return map[key];
        var name=Name(source);
        if(!map.TryGetValue(key,out var target)) map[key]=target=Id(name);
        (selectedMechanics ? importedSelected : importedOfficial).Add(key);
        // Official projectile values remain official, including when referenced by selected mechanics.
        if(!keepNumbers && source is IProjectileGetter && official.TryGetValue(key,out var vanillaProjectile)) source=vanillaProjectile;
        var copy=Copy(source,target); copy.EditorID=name;
        if(!keepNumbers && copy is Spell spell && official.TryGetValue(key,out var vanilla) && vanilla is ISpellGetter original) {
            foreach(var effect in spell.Effects) {
                var matches=original.Effects.Where(e=>e.BaseEffect.FormKey==effect.BaseEffect.FormKey).ToArray();
                if(matches.Length==1) effect.Data=matches[0].Data?.DeepCopy();
            }
        }
        foreach(var link in source.EnumerateFormLinks(false)) {
            if(link.FormKey.IsNull) continue;
            if(!db.TryGetValue(link.FormKey,out var next)) {
                if(!official.ContainsKey(link.FormKey)) throw new InvalidDataException("Missing record referenced by "+source.EditorID+": "+link.FormKey);
                continue;
            }
            // NPCs keep their original official spells/perks/equipment; only their selected NPC templates and custom assets are private.
            if(source is INpcGetter && official.ContainsKey(link.FormKey) && next is not INpcGetter) continue;
            if(CanImport(next)) Import(link.FormKey,selectedMechanics,keepNumbers);
            else if(!official.ContainsKey(link.FormKey)) throw new InvalidDataException("Selected graph reaches unsupported record: "+source.EditorID+" -> "+link.FormKey);
        }
        return target;
    }

    FormKey Display(FormKey key)
    {
        if(key.IsNull) return key;
        if(map.TryGetValue(key,out var existing)) return existing;
        var source=(IStaticGetter)selected[key];
        var name=official.ContainsKey(key) ? "YSMIR_Inventory_"+Suffix(key) : Name(source);
        var target=Id(name); map[key]=target;
        Copy(source,target).EditorID=name;
        return target;
    }

    IMajorRecord Copy(IMajorRecordGetter r, FormKey target)
    {
        // Remove only our generated copy when a selected behavior replaces it.
        mod.Remove(new[] { target });
        return r switch
        {
            ISpellGetter x => mod.Spells.DuplicateInAsNewRecord(x, target),
            IMagicEffectGetter x => mod.MagicEffects.DuplicateInAsNewRecord(x, target),
            IProjectileGetter x => mod.Projectiles.DuplicateInAsNewRecord(x, target),
            IPerkGetter x => mod.Perks.DuplicateInAsNewRecord(x, target),
            INpcGetter x => mod.Npcs.DuplicateInAsNewRecord(x, target),
            IObjectEffectGetter x => mod.ObjectEffects.DuplicateInAsNewRecord(x, target),
            IStaticGetter x => mod.Statics.DuplicateInAsNewRecord(x, target),
            IEffectShaderGetter x => mod.EffectShaders.DuplicateInAsNewRecord(x, target),
            IArtObjectGetter x => mod.ArtObjects.DuplicateInAsNewRecord(x, target),
            IExplosionGetter x => mod.Explosions.DuplicateInAsNewRecord(x, target),
            IHazardGetter x => mod.Hazards.DuplicateInAsNewRecord(x, target),
            IGlobalGetter x => mod.Globals.DuplicateInAsNewRecord(x, target),
            IFormListGetter x => mod.FormLists.DuplicateInAsNewRecord(x, target),
            IKeywordGetter x => mod.Keywords.DuplicateInAsNewRecord(x, target),
            _ => throw new InvalidDataException($"Unexpected selected asset type: {r.FormKey} {r.GetType().Name}")
        };
    }

    public void Build(string destination)
    {
        Directory.CreateDirectory(destination);
        mod.ModHeader.Author="dickmna; selected FT assets by Delta";
        mod.ModHeader.Description="Ysmir 0.5.0: direct upstream build, shared cast contexts and centered projectile scaling.";
        foreach(var rule in SourceRules.Shouts) {
            var key=rule.Key;
            var original=(IShoutGetter)official[key];
            var useSelected=rule.Mechanic is SourceRules.Mechanism.Forceful or SourceRules.Mechanism.Elemental or SourceRules.Mechanism.SlowTime or SourceRules.Mechanism.DragonAspect;
            var keep=rule.Mechanic==SourceRules.Mechanism.DragonAspect;
            var source=keep ? (IShoutGetter)selected[key] : original;
            var name="YSMIR_PlayerShout_"+Suffix(key);
            var target=Id(name); map[key]=target;
            var copy=mod.Shouts.DuplicateInAsNewRecord(source,target); copy.EditorID=name;
            var menuSource=inputs[0].Shouts.TryGetValue(key,out var ftShout) ? ftShout : original;
            copy.MenuDisplayObject.SetTo(Display(menuSource.MenuDisplayObject.FormKey));
            for(var tier=0;tier<3;++tier) {
                var spellKey=source.WordsOfPower[tier].Spell.FormKey;
                copy.WordsOfPower[tier].Spell.SetTo(Import(spellKey,useSelected,keep));
                if(rule.Mechanic==SourceRules.Mechanism.Whirlwind) {
                    var voice=mod.Spells[copy.WordsOfPower[tier].Spell.FormKey];
                    foreach(var effect in ((ISpellGetter)selected[spellKey]).Effects.Where(e=>e.BaseEffect.FormKey.ModKey.FileName.String==SourceRules.Whirlwind)) {
                        Import(effect.BaseEffect.FormKey,true); voice.Effects.Add(effect.DeepCopy());
                    }
                }
                // Dismay's added area is part of FT's mechanism, not an existing vanilla numeric field.
                if(key==K("Skyrim",0x02395A)) {
                    var authored=(ISpellGetter)selected[spellKey];
                    foreach(var effect in mod.Spells[copy.WordsOfPower[tier].Spell.FormKey].Effects)
                        effect.Data!.Area=authored.Effects.Single(e=>e.BaseEffect.FormKey==effect.BaseEffect.FormKey).Data!.Area;
                }
            }
            var menu=original.DeepCopy(); menu.MenuDisplayObject.SetTo(copy.MenuDisplayObject.FormKey);
            if(keep) menu.Description=source.Description?.DeepCopy();
            mod.Shouts.Set(menu); roots.Add(copy); runtimeRoots.Add(target);
        }

        foreach(var id in new uint[]{0x008448,0x008449,0x00844A})
            SetArchetype(mod.MagicEffects[Private(K("Dawnguard",id))],MagicEffectArchetype.TypeEnum.Absorb);
        var weakness=selected.Values.OfType<IMagicEffectGetter>().Single(e=>e.EditorID=="FT_VoiceMarkedForDeathWeaknessMagicEffect");
        Import(weakness.FormKey,true);
        for(var tier=0;tier<3;++tier) {
            var key=K("Skyrim",0x07097C); var authored=(ISpellGetter)selected[((IShoutGetter)official[key]).WordsOfPower[tier].Spell.FormKey];
            Voice(key,tier).Effects.Add(authored.Effects.Single(e=>e.BaseEffect.FormKey==weakness.FormKey).DeepCopy());
        }
        SetArchetype(mod.MagicEffects[Private(K("Skyrim",0x10319C))],MagicEffectArchetype.TypeEnum.ValueModifier);
        mod.MagicEffects[Private(K("Skyrim",0x10319C))].Flags &= ~MagicEffect.Flag.Recover;
        SetArchetype(mod.MagicEffects[Private(K("Skyrim",0x10319D))],MagicEffectArchetype.TypeEnum.PeakValueModifier);
        mod.MagicEffects[Private(K("Skyrim",0x10319D))].Flags |= MagicEffect.Flag.Recover;

        foreach(var id in SourceRules.Heroes) {
            var key=Import(K("Skyrim",id),true,true);
            mod.Npcs[key].Configuration.Level=new NpcLevel { Level=150 };
        }
        var wyrm=K("Dragonborn",0x01F990);
        var wyrmId=Import(wyrm);
        var officialWyrm=(Npc)Copy(official[wyrm],wyrmId); officialWyrm.EditorID=Name(official[wyrm]);
        var ftWyrm=(INpcGetter)selected[wyrm];
        officialWyrm.AIData.Aggression=ftWyrm.AIData.Aggression;
        officialWyrm.DefaultPackageList.SetTo(ftWyrm.DefaultPackageList.FormKey);

        for(var tier=0;tier<3;++tier) {
            var actualKey=Import(new FormKey(ModKey.FromFileName(SourceRules.SlowTime),(uint)(0x800+tier)),true);
            var actual=mod.Spells[actualKey];
            actual.Effects.Single().Data!.Magnitude=new[]{.3f,.2f,.1f}[tier];
            actual.Effects.Single().Data!.Duration=new[]{8,12,16}[tier];
            runtimeRoots.Add(actualKey);
        }
        for(var element=0;element<3;++element)
            for(var tier=0;tier<3;++tier)
                runtimeRoots.Add(Import(new FormKey(ModKey.FromFileName(SourceRules.Elemental),(uint)(0x850+element*0x10+tier)),true));

        // Remap once, after all source-key allocations and selected replacements exist.
        foreach(var record in mod.EnumerateMajorRecords().Where(r=>r.FormKey.ModKey==OutputKey)) {
            if(record is Npc) {
                var npcMap=map.Where(p=>selected.TryGetValue(p.Key,out var r) && (r is INpcGetter || !official.ContainsKey(p.Key))).ToDictionary(p=>p.Key,p=>p.Value);
                record.RemapLinks(npcMap);
            } else record.RemapLinks(map);
        }
        IsolateElementalTargets();
        BuildSoulTearCone();
        var stormProjectile=BuildStormProjectile(); runtimeRoots.Add(stormProjectile);
        foreach(var effect in mod.MagicEffects) {
            if(effect.VirtualMachineAdapter is not { } vmad) continue;
            foreach(var script in vmad.Scripts) {
                script.Name=script.Name.ToLowerInvariant() switch {
                    "voicepusheffectscript"=>"YsmirVoicePushEffectScript",
                    "dlc2whirlwinddamagescript"=>"YsmirWhirlwindDamageScript",
                    "dlc2dbsummonancientscript"=>"YsmirDragonAspectSummonScript",
                    "magiccastanotherspellscript" when effect.EditorID?.StartsWith("YSMIR_SlowTimeWrapperEffect")==true =>"YsmirSlowTimeScript",
                    _=>script.Name
                };
                if(script.Name=="YsmirSlowTimeScript")
                    foreach(var property in script.Properties.Where(p=>p.Name!="CastSpellName").ToArray()) script.Properties.Remove(property);
            }
        }
        for(var tier=0;tier<3;++tier) {
            var voice=Voice(K("Skyrim",0x07097D),tier);
            var name=$"YSMIR_StormDuration{tier+1}";
            var stub=mod.MagicEffects.DuplicateInAsNewRecord((IMagicEffectGetter)official[K("Skyrim",0x10E4FD)],Id(name));
            stub.EditorID=name; stub.VirtualMachineAdapter=null; voice.Effects.Clear();
            var e=new Effect { Data=new EffectData { Duration=60*(tier+1) } }; e.BaseEffect.SetTo(stub.FormKey); voice.Effects.Add(e);
        }
        // Only dependencies reachable from the selected player roots enter the artifact.
        var records = mod.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        var reachable = Closure(runtimeRoots.Concat(mod.Shouts.Where(s => s.FormKey.ModKey != OutputKey).Select(s => s.FormKey)), records, false);
        mod.Remove(records.Keys.Where(k => k.ModKey == OutputKey && !reachable.Contains(k)).ToArray());
        records = mod.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        var playerGraph = Closure(runtimeRoots.Where(k => k != stormProjectile), records, true);
        var keyword = K("Skyrim", 0x046B99);
        foreach (var key in playerGraph)
            if (records[key] is MagicEffect effect)
            {
                // Classification is independent of the five magic schools.
                effect.MagicSkill = ActorValue.None;
                effect.Keywords ??= new();
                if ((effect.Flags & MagicEffect.Flag.DispelWithKeywords) != 0)
                {
                    // Here KWDA is an instruction to DISPEL, not a classification.
                    // Adding MagicShout would dispel every active shout, including
                    // Dragon Aspect itself. Preserve the authored selector list.
                    foreach (var link in effect.Keywords.Where(k => k.FormKey == keyword).ToArray()) effect.Keywords.Remove(link);
                }
                else if (!effect.Keywords.Any(k => k.FormKey == keyword)) effect.Keywords.Add(new FormLink<IKeywordGetter>(keyword));
            }
        // A shout whose only effects use KWDA as dispel selectors receives a
        // zero-cost, inert classification effect with matching delivery/cast type.
        // This keeps every voice spell classified without changing its dispel rules.
        var markers = new Dictionary<(CastType, TargetType), MagicEffect>();
        foreach (var spell in roots.SelectMany(r => mod.Shouts[r.FormKey].WordsOfPower.Select(w => mod.Spells[w.Spell.FormKey])).Distinct())
        {
            if (spell.Effects.Any(e => mod.MagicEffects[e.BaseEffect.FormKey].Keywords?.Any(k => k.FormKey == keyword) == true)) continue;
            var key = (spell.CastType, spell.TargetType);
            if (!markers.TryGetValue(key, out var marker))
            {
                marker = new MagicEffect(Id($"YSMIR_ShoutClassification_{spell.CastType}_{spell.TargetType}"), SkyrimRelease.SkyrimSE) {
                    EditorID = $"YSMIR_ShoutClassification_{spell.CastType}_{spell.TargetType}",
                    Archetype = new MagicEffectArchetype { Type = MagicEffectArchetype.TypeEnum.Script },
                    CastType = spell.CastType, TargetType = spell.TargetType,
                    Flags = MagicEffect.Flag.NoMagnitude | MagicEffect.Flag.NoDuration | MagicEffect.Flag.NoArea | MagicEffect.Flag.HideInUI,
                    Keywords = new()
                };
                marker.Keywords.Add(new FormLink<IKeywordGetter>(keyword));
                mod.MagicEffects.Add(marker); markers[key] = marker;
            }
            var item = new Effect { Data = new EffectData() }; item.BaseEffect.SetTo(marker.FormKey); spell.Effects.Add(item);
        }
        records = mod.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        playerGraph = Closure(runtimeRoots.Where(k => k != stormProjectile), records, true);
        var missing = records.Values.SelectMany(r => r.EnumerateFormLinks(false)
            .Where(l => !l.FormKey.IsNull && l.FormKey != K("Skyrim", 0x14) && !records.ContainsKey(l.FormKey) && !official.ContainsKey(l.FormKey))
            .Select(l => $"{r.EditorID}: {r.FormKey} -> {l.FormKey}")).Distinct().ToArray();
        if (missing.Length != 0) throw new InvalidDataException(string.Join("\n", missing));
        if (mod.Shouts.Count != roots.Count * 2 || records.Values.Any(r => r.EditorID?.Contains("PhantomForm") == true))
            throw new InvalidDataException("Selected shout boundary violated.");
        mod.WriteToBinary(Path.Combine(destination, "Ysmir.esp"), new BinaryWriteParameters
        {
            MastersListContent = MastersListContentOption.Iterate,
            MastersListOrdering = new MastersListOrderingByLoadOrder(masters.Select(m => m.ModKey))
        });
        ExportManifest(destination, playerGraph);
        File.WriteAllText(Path.Combine(destination, "selected-records.json"), JsonSerializer.Serialize(new
        {
            version = SourceRules.Version, inputs = SourceRules.Inputs.Select(x => new { file=x.File, sha256=x.Sha256 }), records = records.Count, privateRecords = records.Keys.Count(k => k.ModKey == OutputKey),
            playerEffects = playerGraph.Count(k => records[k] is IMagicEffectGetter), unresolvedLinks = missing,
            recordTypes = records.Values.GroupBy(r => r.GetType().Name).ToDictionary(g => g.Key, g => g.Count()),
            scripts = mod.MagicEffects.Where(e => e.VirtualMachineAdapter != null).SelectMany(e => e.VirtualMachineAdapter!.Scripts.Select(s => s.Name)).Distinct().Order(),
            selected = records.Values.Select(r => new { key = r.FormKey.ToString(), edid = r.EditorID, type = r.GetType().Name }),
            numbers = roots.Select(r => new { shout = r.EditorID, words = mod.Shouts[r.FormKey].WordsOfPower.Select(w => new { cooldown = w.RecoveryTime, spell = w.Spell.FormKey.ToString(), effects = mod.Spells[w.Spell.FormKey].Effects.Select(e => new { effect = e.BaseEffect.FormKey.ToString(), e.Data }) }) })
        }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"Built {records.Count} records; {playerGraph.Count(k => records[k] is IMagicEffectGetter)} player MGEFs; no unresolved links.");
    }

    void IsolateElementalTargets()
    {
        foreach(var element in new[]{"Fire","Frost","Shock"}) {
            var original=(MagicEffect)Named($"YSMIR_ElementalFury{element}EnemyEffect1");
            var projectile=(IProjectileGetter)mod.Projectiles[original.Projectile.FormKey];
            for(var tier=1;tier<=3;++tier) {
                var name=$"YSMIR_ElementalFury{element}EnemyEffect{tier}";
                var enemy=tier==1 ? original : mod.MagicEffects.DuplicateInAsNewRecord(original,Id(name));
                enemy.EditorID=name;
                var projectileName=$"YSMIR_ElementalFury{element}Projectile{tier}";
                var clone=mod.Projectiles.DuplicateInAsNewRecord(projectile,Id(projectileName)); clone.EditorID=projectileName;
                enemy.Projectile.SetTo(clone.FormKey);
                ((Spell)Named($"YSMIR_ElementalFury{element}Damage{tier}")).Effects.Single().BaseEffect.SetTo(enemy.FormKey);
            }
        }
    }
    void BuildSoulTearCone()
    {
        var source=(IProjectileGetter)official[K("Dawnguard",0x007CBB)];
        var cone=new Projectile(Id("YSMIR_SoulTearConeProjectile"),SkyrimRelease.SkyrimSE) {
            EditorID="YSMIR_SoulTearConeProjectile", ObjectBounds=source.ObjectBounds.DeepCopy(), Model=source.Model?.DeepCopy(),
            Type=Projectile.TypeEnum.Cone, Flags=Projectile.Flag.Supersonic|Projectile.Flag.PassThroughSmallTransparent,
            Gravity=0,Speed=1300,Range=4000,ConeSpread=15,CollisionRadius=16,ImpactForce=source.ImpactForce,
            TracerChance=source.TracerChance,FadeDuration=source.FadeDuration,SoundLevel=source.SoundLevel
        };
        mod.Projectiles.Add(cone);
        foreach(var id in new uint[]{0x007CB5,0x015714,0x01AA33,0x007CB3}) {
            var effect=mod.MagicEffects[Private(K("Dawnguard",id))];
            effect.TargetType=TargetType.Aimed; effect.Projectile.SetTo(cone.FormKey);
        }
        for(var tier=0;tier<3;++tier) Voice(K("Dawnguard",0x007CB6),tier).TargetType=TargetType.Aimed;
    }
    FormKey BuildStormProjectile()
    {
        var projectile=mod.Projectiles.DuplicateInAsNewRecord((IProjectileGetter)official[K("Skyrim",0x0E4CB5)],Id("YSMIR_StormCallProjectile"));
        projectile.EditorID="YSMIR_StormCallProjectile";
        projectile.Model ??=new Model(); projectile.Model.File="Magic\\LightningBolt03.nif";
        projectile.Flags=(Projectile.Flag)0x0200; projectile.Type=(Projectile.TypeEnum)0x0004;
        projectile.Gravity=0; projectile.Speed=90000; projectile.Range=10000;
        projectile.Light.Clear(); projectile.MuzzleFlash.Clear(); projectile.TracerChance=0;
        projectile.ExplosionAltTriggerProximity=0; projectile.ExplosionAltTriggerTimer=0; projectile.Explosion.Clear();
        projectile.Sound.SetTo(K("Skyrim",0x057C6C)); projectile.MuzzleFlashDuration=0;
        projectile.FadeDuration=.5f; projectile.ImpactForce=400; projectile.CountdownSound.Clear(); projectile.DisaleSound.Clear();
        projectile.DefaultWeaponSource.Clear(); projectile.ConeSpread=0; projectile.CollisionRadius=10;
        projectile.Lifetime=.35f; projectile.RelaunchInterval=.15f; projectile.DecalData.Clear(); projectile.CollisionLayer.Clear(); projectile.SoundLevel=0;
        return projectile.FormKey;
    }
    HashSet<FormKey> Closure(IEnumerable<FormKey> start, IReadOnlyDictionary<FormKey, IMajorRecord> db, bool gameplayOnly)
    {
        var pending = new Queue<FormKey>(start);
        var result = new HashSet<FormKey>();
        while (pending.TryDequeue(out var key))
        {
            if (!db.TryGetValue(key, out var r) || !result.Add(key)) continue;
            IEnumerable<FormKey> links = r is IShoutGetter s && gameplayOnly ? s.WordsOfPower.Select(w => w.Spell.FormKey) :
                r.EnumerateFormLinks(false).Select(l => l.FormKey);
            foreach (var linked in links)
                if (db.TryGetValue(linked, out var next) && (!gameplayOnly || Gameplay(next) || next is IPerkGetter or IObjectEffectGetter)) pending.Enqueue(linked);
        }
        return result;
    }

    void ExportManifest(string destination, HashSet<FormKey> graph)
    {
        string H(FormKey k) => $"0x{k.ID:X6}";
        var t = new StringBuilder("// Generated by SelectedBuild.cs from the final ESP.\n#pragma once\n#include <array>\n#include <cstdint>\nnamespace ysmir::forms {\ninline constexpr char PluginName[] = \"Ysmir.esp\";\nstruct PlayerShoutMapping { const char* masterFile; std::uint32_t localFormID; std::uint32_t privateFormID; std::array<std::uint32_t,3> privateSpellIDs; };\ninline constexpr std::array<PlayerShoutMapping," + roots.Count + "> PlayerShouts{{\n");
        foreach (var r in roots)
        {
            var key = RootKey(r);
            t.AppendLine($"{{ \"{key.ModKey.FileName}\", {H(key)}, {H(r.FormKey)}, {{ {string.Join(", ", mod.Shouts[r.FormKey].WordsOfPower.Select(w => H(w.Spell.FormKey)))} }} }},");
        }
        t.AppendLine("}};\ninline constexpr std::array<std::array<std::uint32_t,3>,3> ElementalSpells{{");
        foreach (var element in new[] { "Fire", "Frost", "Shock" })
            t.AppendLine("{ " + string.Join(", ", Enumerable.Range(1, 3).Select(i => H(Named($"YSMIR_ElementalFury{element}{i}").FormKey))) + " },");
        t.AppendLine("}};\ninline constexpr std::array<std::uint32_t,3> SlowTimeActualSpells{ " + string.Join(", ", Enumerable.Range(1, 3).Select(i => H(Named($"YSMIR_SlowTimeActual{i}").FormKey))) + " };");
        t.AppendLine($"inline constexpr std::uint32_t StormProjectile = {H(Named("YSMIR_StormCallProjectile").FormKey)};");
        var spells = mod.Spells.Where(s => graph.Contains(s.FormKey)).OrderBy(s => s.FormKey.ID).ToArray();
        t.AppendLine($"inline constexpr std::array<std::uint32_t,{spells.Length}> PlayerSpells{{ " + string.Join(", ", spells.Select(s => H(s.FormKey))) + " };");
        IEnumerable<FormKey> ChildSpells(ISpellGetter spell)
        {
            var pending = new Queue<FormKey>(spell.Effects.Select(e => e.BaseEffect.FormKey));
            var visited = new HashSet<FormKey>();
            while (pending.TryDequeue(out var key))
            {
                if (!visited.Add(key)) continue;
                if (mod.Spells.ContainsKey(key) && graph.Contains(key)) { yield return key; continue; }
                IMajorRecordGetter? record = mod.MagicEffects.TryGetValue(key, out var effect) ? effect : mod.Perks.TryGetValue(key, out var perk) ? perk : null;
                if (record != null) foreach (var link in record.EnumerateFormLinks(false)) pending.Enqueue(link.FormKey);
            }
        }
        var edges = spells.SelectMany(s => ChildSpells(s).Select(child => (Parent: s.FormKey, Child: child)))
            .Distinct().OrderBy(e => e.Parent.ID).ThenBy(e => e.Child.ID).ToArray();
        t.AppendLine("struct SpellEdge { std::uint32_t parent, child; };");
        t.AppendLine($"inline constexpr std::array<SpellEdge,{edges.Length}> PlayerSpellEdges{{{{");
        foreach (var edge in edges) t.AppendLine($"{{ {H(edge.Parent)}, {H(edge.Child)} }},");
        t.AppendLine("}};");
        t.AppendLine("}");
        File.WriteAllText(Path.Combine(destination, "RuntimeForms.h"), t.ToString().Replace("\r\n", "\n"), new UTF8Encoding(false));
    }

    public void Dispose()
    {
        foreach(var loaded in inputs.Concat(masters)) loaded.Dispose();
        // Only the uniquely created temporary strings directory belongs to this invocation.
        if(Directory.Exists(strings)) Directory.Delete(strings,true);
    }
    public static int Main(string[] args)
    {
        if(args.Length!=3) { Console.Error.WriteLine("Usage: builder GAME_DATA UPSTREAM_INPUT_DIRECTORY OUTPUT_DIRECTORY"); return 2; }
        try { using var builder=new SelectedBuild(args[0],args[1]); builder.Build(args[2]); return 0; }
        catch(Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
