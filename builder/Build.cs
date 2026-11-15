using Mutagen.Bethesda;
using Mutagen.Bethesda.Archives;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using Mutagen.Bethesda.Strings;
using System.Text.Json;
using System.Security.Cryptography;

using var build = new YsmirBuild(args.Length>0?Path.GetFullPath(args[0]):Path.GetFullPath(".."));
build.Run();

sealed partial class YsmirBuild : IDisposable
{
    readonly string root;
    readonly string game = Environment.GetEnvironmentVariable("YSMIR_GAME_DATA") ?? throw new InvalidOperationException("Set YSMIR_GAME_DATA to the original Skyrim Special Edition Data folder.");
    readonly List<ISkyrimModDisposableGetter> loaded = new();
    readonly Dictionary<FormKey, IMajorRecordGetter> official = new(), sources = new();
    readonly Dictionary<FormKey, FormKey> remap = new();
    readonly List<object> numericRestores = new();
    readonly List<object> inputHashes = new();
    readonly ModKey output = ModKey.FromFileName("Ysmir - Shout System Overhaul.esp");
    readonly SkyrimMod mod;
    ISkyrimModDisposableGetter ft = null!, elemental = null!, sprint = null!;
    uint nextID = 0x800;
    static readonly string FT = "ForcefulTongue.esp", EF = "Elemental Fury for MCO ADXP.esp", WS = "WhirlwindSprintRedone version 2.esp";
    static FormKey K(string master, uint id) => new(ModKey.FromFileName(master.Contains('.') ? master : master + ".esm"), id);
    FormKey NewKey() => new(output, nextID++);
    FormKey M(FormKey key) => remap.GetValueOrDefault(key, key);
    static TranslatedString Txt(string s) => new(Language.English, s);
    IMajorRecord Named(string id) => mod.EnumerateMajorRecords().Single(r => r.EditorID == id);
    T Orig<T>(string master, uint id) where T : IMajorRecordGetter => (T)official[K(master,id)];
    IMajorRecordGetter Res(FormKey key) => mod.EnumerateMajorRecords().FirstOrDefault(r=>r.FormKey==key) ?? sources.GetValueOrDefault(key) ?? official[key];
    static bool Phantom(IMajorRecordGetter r) => (r.EditorID ?? "").Contains("Phantom", StringComparison.OrdinalIgnoreCase) || (r.EditorID ?? "").Contains("WordWallBroken");
    static readonly (string,uint)[] PlayerShouts = {
        ("Skyrim",0x13E07),("Skyrim",0x2395A),("Skyrim",0x2F7BA),("Skyrim",0x32920),("Skyrim",0x32921),
        ("Skyrim",0x3CD34),("Skyrim",0x3F9EA),("Skyrim",0x44250),("Skyrim",0x46B8C),("Skyrim",0x48AC9),
        ("Skyrim",0x5196A),("Skyrim",0x5D16B),("Skyrim",0x5FC77),("Skyrim",0x7097B),("Skyrim",0x7097C),
        ("Skyrim",0x7097D),("Skyrim",0x7097E),("Skyrim",0x7097F),("Skyrim",0x70980),("Skyrim",0x70981),
        ("Dawnguard",0x30D2),("Dawnguard",0x7CB6),("Dawnguard",0x8A62),
        ("Dragonborn",0x179D8),("Dragonborn",0x1DF92),("Dragonborn",0x200C0),("Dragonborn",0x2AD09)
    };
    public YsmirBuild(string root) { this.root=root; mod=new SkyrimMod(output,SkyrimRelease.SkyrimSE); Directory.CreateDirectory(Path.Combine(root,"reports"));Directory.CreateDirectory(Path.Combine(root,"package")); }
    ISkyrimModDisposableGetter Load(string path, bool localized=false) {
        if(Path.GetExtension(path).Equals(".esp",StringComparison.OrdinalIgnoreCase))inputHashes.Add(new{name=Path.GetFileName(path),sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path)))});
        var read=localized?new BinaryReadParameters{StringsParam=new StringsReadParameters { StringsFolderOverride=Path.Combine(root,"inputs","strings"), BsaFolderOverride=Path.Combine(root,"inputs","strings"),TargetLanguage=Language.English }}:new BinaryReadParameters();
        var m=SkyrimMod.CreateFromBinaryOverlay(path,SkyrimRelease.SkyrimSE,read); loaded.Add(m); return m;
    }
    void Inputs() {
        var strings=Path.Combine(root,"inputs","strings"); Directory.CreateDirectory(strings);
        foreach(var archiveName in new[]{"Skyrim - Interface.bsa","Update.bsa","Dawnguard.bsa","HearthFires.bsa","Dragonborn.bsa"}) {
            var path=Path.Combine(game,archiveName); if(!File.Exists(path))continue;
            var archive=Archive.CreateReader(GameRelease.SkyrimSE,path);
            if(archive.TryGetFolder("strings",out var folder)) foreach(var file in folder.Files.Where(f=>f.Path.Contains("_english.",StringComparison.OrdinalIgnoreCase)))
                File.WriteAllBytes(Path.Combine(strings,Path.GetFileName(file.Path)),file.GetBytes());
        }
        foreach(var name in new[]{"Skyrim","Update","Dawnguard","HearthFires","Dragonborn"})
            foreach(var r in Load(Path.Combine(game,name+".esm"),true).EnumerateMajorRecords())official[r.FormKey]=r;
        ft=Load(Environment.GetEnvironmentVariable("YSMIR_FT_PLUGIN") ?? Path.Combine(root,"inputs","ft",FT));
        elemental=Load(Environment.GetEnvironmentVariable("YSMIR_ELEMENTAL_PLUGIN") ?? Path.Combine(root,"inputs","elemental","Main Files","No Shaders",EF));
        sprint=Load(Environment.GetEnvironmentVariable("YSMIR_SPRINT_PLUGIN") ?? Path.Combine(root,"inputs","sprint","data",WS));
        foreach(var m in new[]{ft,elemental,sprint}) foreach(var r in m.EnumerateMajorRecords())sources[r.FormKey]=r;
        Console.WriteLine($"Loaded {official.Count} official records; FT {ft.EnumerateMajorRecords().Count()} records");
    }
    IMajorRecord Copy(IMajorRecordGetter r, FormKey key) {
        mod.Remove(new[]{key});
        return r switch {
            IShoutGetter x => mod.Shouts.DuplicateInAsNewRecord(x,key),
            ISpellGetter x => mod.Spells.DuplicateInAsNewRecord(x,key),
            IMagicEffectGetter x => mod.MagicEffects.DuplicateInAsNewRecord(x,key),
            IProjectileGetter x => mod.Projectiles.DuplicateInAsNewRecord(x,key),
            IPerkGetter x => mod.Perks.DuplicateInAsNewRecord(x,key),
            INpcGetter x => mod.Npcs.DuplicateInAsNewRecord(x,key),
            IRaceGetter x => mod.Races.DuplicateInAsNewRecord(x,key),
            IObjectEffectGetter x => mod.ObjectEffects.DuplicateInAsNewRecord(x,key),
            IStaticGetter x => mod.Statics.DuplicateInAsNewRecord(x,key),
            IEffectShaderGetter x => mod.EffectShaders.DuplicateInAsNewRecord(x,key),
            IArtObjectGetter x => mod.ArtObjects.DuplicateInAsNewRecord(x,key),
            IExplosionGetter x => mod.Explosions.DuplicateInAsNewRecord(x,key),
            IHazardGetter x => mod.Hazards.DuplicateInAsNewRecord(x,key),
            IGlobalGetter x => mod.Globals.DuplicateInAsNewRecord(x,key),
            IFormListGetter x => mod.FormLists.DuplicateInAsNewRecord(x,key),
            IKeywordGetter x => mod.Keywords.DuplicateInAsNewRecord(x,key),
            IQuestGetter x => mod.Quests.DuplicateInAsNewRecord(x,key),
            IMessageGetter x => mod.Messages.DuplicateInAsNewRecord(x,key),
            ISoundDescriptorGetter x => mod.SoundDescriptors.DuplicateInAsNewRecord(x,key),
            _ => throw new InvalidOperationException("Unsupported record "+r.GetType()+" "+r.EditorID)
        };
    }
    void Foundation() {
        var keys=PlayerShouts.Select(x=>K(x.Item1,x.Item2)).ToHashSet();
        foreach(var r in ft.EnumerateMajorRecords()) {
            if(Phantom(r) || r is IWorldspaceGetter or ICellGetter or IPlacedGetter or IDialogTopicGetter or IDialogResponsesGetter or IWordOfPowerGetter)continue;
            if(r is IShoutGetter && !keys.Contains(r.FormKey))continue;
            if(r.FormKey==K("Skyrim",0x167D9)||r.FormKey==K("Skyrim",0xFB97E))continue;
            var target=r.FormKey.ModKey==ft.ModKey ? NewKey() : r.FormKey;
            if(target!=r.FormKey)remap[r.FormKey]=target;
            var copy=Copy(r,target);
            if(copy is Spell sp && official.GetValueOrDefault(r.FormKey) is ISpellGetter vanilla) {
                var occurrence=new Dictionary<FormKey,int>();
                foreach(var e in sp.Effects) {
                    var match=vanilla.Effects.Where(x=>x.BaseEffect.FormKey==e.BaseEffect.FormKey).ToArray();
                    int index=occurrence.GetValueOrDefault(e.BaseEffect.FormKey);occurrence[e.BaseEffect.FormKey]=index+1;
                    if(match.Length>0 && match[Math.Min(index,match.Length-1)].Data is {} data) {
                        numericRestores.Add(new{spell=r.EditorID,effect=e.BaseEffect.FormKey.ToString(),before=e.Data==null?null:new[]{e.Data.Magnitude,e.Data.Area,e.Data.Duration},after=new[]{data.Magnitude,data.Area,data.Duration}});
                        e.Data=data.DeepCopy();
                    }
                }
                sp.BaseCost=vanilla.BaseCost; sp.ChargeTime=vanilla.ChargeTime; sp.CastDuration=vanilla.CastDuration; sp.Range=vanilla.Range;
            }
        }
        foreach(var record in mod.EnumerateMajorRecords()) record.RemapLinks(remap);
        foreach(var shout in mod.Shouts) {
            shout.MajorFlags &= ~Shout.MajorFlag.TreatSpellsAsPowers;
            foreach(var word in shout.WordsOfPower)word.RecoveryTime=5;
        }
        foreach(var n in mod.Npcs.Where(n=>new uint[]{0x44236,0x44237,0x44238,0x923F9,0x923FA,0x923FB,0xCD9F7,0xCDA00,0xCDA01}.Contains(n.FormKey.ID)&&n.FormKey.ModKey==K("Skyrim",0).ModKey)) {
            n.Configuration.Level=new NpcLevel{Level=150}; n.Configuration.CalcMinLevel=150; n.Configuration.CalcMaxLevel=150;
            // Retain native class/race auto-calculation at level 150; remove
            // FT's extra attribute offsets from every base/template variant.
            n.Configuration.HealthOffset=0; n.Configuration.MagickaOffset=0; n.Configuration.StaminaOffset=0;
        }
        // Animal Allegiance keeps its original allegiance/rally effect only.
        // The unreachable FT attribute effects are pruned by KeywordsAndCleanup.
        foreach(var (spell,effect) in new[]{(0x9E0CCu,0x9E0C8u),(0x9E0CDu,0x9E0CAu),(0x9E0CEu,0x9E0CBu)}) {
            var voice=mod.Spells[K("Skyrim",spell)];
            foreach(var extra in voice.Effects.Where(e=>e.BaseEffect.FormKey!=K("Skyrim",effect)).ToArray())voice.Effects.Remove(extra);
            mod.MagicEffects[K("Skyrim",effect)].Description=Txt("Nearby animals up to level <mag> become your allies and won't flee for <dur> seconds.");
        }
        // FT's extra post-dash movement benefit has no vanilla analogue.
        // Isolate it from the original instantaneous dash and its numbers.
        for(int i=0;i<3;i++) {
            uint id=new uint[]{0x2F7B9,0x4372F,0x43730}[i];
            var extra=(MagicEffect)Copy(ft.MagicEffects[K("Skyrim",id)],NewKey()); extra.EditorID=$"YSMR_PostSprintSpeed{i+1}";extra.RemapLinks(remap);
            var voice=mod.Spells[K("Skyrim",new uint[]{0x2F7BE,0x2F7BF,0x2F7C0}[i])];
            Copy(Orig<IMagicEffectGetter>("Skyrim",id),K("Skyrim",id));
            voice.Effects.Add(Eff(extra.FormKey,25,0,(uint)(10*(i+1))));
        }
        foreach(var e in mod.Spells[K("Dawnguard",0x7CBA)].Effects)if(e.Data!=null)e.Data.Duration=60;
        // FT replaces the summon effect identity, so EFID matching cannot restore it.
        var durn=mod.Spells.Single(s=>s.EditorID=="FT_DLC1SummonDurnehviir");
        foreach(var e in durn.Effects)if(e.Data!=null)e.Data.Duration=180;
        // FT's old Slow Time display effect describes FT's 50/60/70% values.
        // The native time effect now owns its accurate description and duration.
        foreach(var id in new uint[]{0x48AD0,0x48AD1,0x48AD2}) {
            var s=mod.Spells[K("Skyrim",id)];
            foreach(var e in s.Effects.Where(e=>e.BaseEffect.FormKey!=K("Skyrim",0x48ACD)).ToArray())s.Effects.Remove(e);
        }
        var slow=mod.MagicEffects[K("Skyrim",0x48ACD)];slow.Flags &= ~MagicEffect.Flag.HideInUI;
        slow.Description=Txt("Slows the world for <dur> seconds without slowing you. Dragon souls extend the duration; the slowdown remains unchanged.");
        // Forward FT's attack-speed-controller branches using vanilla bonuses.
        foreach(var s in mod.Spells.Where(s=>s.EditorID?.StartsWith("FT_DLC2VoiceElementalFury")==true))
            foreach(var e in s.Effects.Where(e=>e.Conditions.OfType<ConditionFloat>().Any(c=>c.Data is GetGlobalValueConditionData&&c.ComparisonValue==1)))e.Data!.Magnitude-=1;
        // FT splits Battle Fury into a recipient controller and a cast buff.
        // Both layers use the original shout's 60/120/180-second duration.
        foreach(var word in mod.Shouts[K("Dragonborn",0x2AD09)].WordsOfPower) {
            var s=mod.Spells[word.Spell.FormKey];var v=(ISpellGetter)official[s.FormKey];
            s.Effects[0].Data=v.Effects[0].Data!.DeepCopy();
            var control=mod.MagicEffects[s.Effects[0].BaseEffect.FormKey];
            foreach(var prop in control.VirtualMachineAdapter!.Scripts.SelectMany(x=>x.Properties).OfType<ScriptObjectProperty>().Where(p=>p.Name=="DLC2VoiceElementalFury"))
                foreach(var e in mod.Spells[prop.Object.FormKey].Effects)e.Data!.Duration=v.Effects[0].Data!.Duration;
            control.Description=Txt("Nearby allies attack faster for <dur> seconds.");
        }
        // FT moves Dragonborn Force damage to its own effect at every rank.
        foreach(var id in new uint[]{0x13E09,0x13F39}) {
            var voice=mod.Spells[K("Skyrim",id)];
            var bonus=Orig<ISpellGetter>("Skyrim",id).Effects.Last(e=>e.BaseEffect.FormKey==K("Skyrim",0xEA075));
            voice.Effects.Single(e=>e.BaseEffect.FormKey==K("Dragonborn",0x1FF2B)).Data=bonus.Data!.DeepCopy();
        }
        // FT extends the arms perk to spell schools; its existing power-attack
        // component retains the vanilla +25%, while the new FT mechanics remain.
        foreach(var p in mod.Perks[K("Dragonborn",0x1DF8F)].Effects.OfType<PerkEntryPointModifyValue>())if(p.Conditions.Any(t=>t.Conditions.Any(c=>c.Data is IsPowerAttackingConditionData)))p.Value=1.25f;
        foreach(var id in new uint[]{0x1DF90,0x21730,0x21731})if(mod.MagicEffects.TryGetValue(K("Dragonborn",id),out var e))e.Description=Txt("Power attacks deal 25% more damage and Destruction and Restoration spells are 25% stronger for <dur> seconds.");
        // Vanilla uses a script for Ancient Dragonborn, with a 20-second actor
        // timer. FT changes the effect to SummonCreature: its duration cannot
        // be restored from the vanilla zero-duration script effect.
        foreach(var e in mod.Spells[K("Dragonborn",0x31855)].Effects)e.Data!.Duration=20;
        // Keep FT's display-only effects consistent with restored values.
        foreach(var id in new uint[]{0x8A61,0x8A5F,0x8A5E}) {
            var voice=mod.Spells[K("Dawnguard",id)];
            foreach(var e in voice.Effects.Where(e=>e.BaseEffect.FormKey.ModKey==output))e.Data!.Duration=30;
        }
        mod.MagicEffects[K("Skyrim",0xA0366)].Description=Txt("Encases targets in ice for <dur> seconds, dealing Frost Damage to Health and Stamina. Dragon souls increase the damage and duration. Attacks can break the ice.");
        ((MagicEffect)Named("FT_VoiceMarkedForDeathDummyEffect")).Description=Txt("Targets lose Health over time, and their Armor Rating and Magic Resistance are temporarily reduced.");
        mod.MagicEffects[K("Skyrim",0xE827F)].Description=Txt("You suffer 25% less stagger and cause 25% more stagger. Prevents Unrelenting Force from throwing you off your feet.");
        mod.MagicEffects[K("Skyrim",0x10A2DB)].Description=Txt("Your Shouts are 25% more effective.");
        mod.MagicEffects[K("Dragonborn",0x2647C)].Description=Txt("Unrelenting Force deals additional damage.");
        mod.MagicEffects[K("Dragonborn",0x26480)].Description=Txt("Ice Form lasts 50% longer. Frost Breath deals 25% extra damage and can raise a frost wyrm from a slain target. The wyrm explodes when it dies or expires.");
        foreach(var e in mod.MagicEffects.Where(e=>e.EditorID?.StartsWith("FT_DLC1VoiceDrainVitalityDummyEffect")==true))
            e.Description=Txt("Absorbs the target's attributes for <dur> seconds. Dragon souls increase the amount absorbed and duration.");
    }
    static Effect Eff(FormKey key,float magnitude=0,uint area=0,uint duration=0) {
        var e=new Effect{Data=new EffectData{Magnitude=magnitude,Area=(int)area,Duration=(int)duration}}; e.BaseEffect.SetTo(key);return e;
    }
    MagicEffect Effect(string name, MagicEffectArchetype.TypeEnum type,TargetType target=TargetType.Self) {
        var e=new MagicEffect(NewKey(),SkyrimRelease.SkyrimSE){EditorID=name,Name=Txt(name),Archetype=new MagicEffectArchetype{Type=type,ActorValue=ActorValue.None},CastType=CastType.FireAndForget,TargetType=target,Flags=MagicEffect.Flag.HideInUI|MagicEffect.Flag.NoArea,MagicSkill=ActorValue.None,ResistValue=ActorValue.None};mod.MagicEffects.Add(e);return e;
    }
    Spell Spell(string name, TargetType target=TargetType.Self) {
        var s=new Spell(NewKey(),SkyrimRelease.SkyrimSE){EditorID=name,Name=Txt(name),Type=SpellType.Spell,CastType=CastType.FireAndForget,TargetType=target,Flags=SpellDataFlag.NoAbsorbOrReflect};mod.Spells.Add(s);return s;
    }
    ScriptEntry Script(MagicEffect effect,string name) {
        effect.VirtualMachineAdapter??=new VirtualMachineAdapter();var s=new ScriptEntry{Name=name};effect.VirtualMachineAdapter.Scripts.Add(s);return s;
    }
    static void Prop(ScriptEntry s,string name,FormKey key) {var p=new ScriptObjectProperty{Name=name,Flags=ScriptProperty.Flag.Edited};p.Object.SetTo(key);s.Properties.Add(p);}
    public void Dispose(){foreach(var l in loaded)l.Dispose();}
}
