using Mutagen.Bethesda;
using Mutagen.Bethesda.Skyrim;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Plugins.Records;
using Mutagen.Bethesda.Plugins.Binary.Parameters;
using System.Text.Json;
using System.Security.Cryptography;
using Mutagen.Bethesda.Archives;

sealed partial class YsmirBuild
{
    static string ResourcePath(string path) => System.Text.RegularExpressions.Regex.Replace(path,"forceful tongue",@"ysmir\resources",System.Text.RegularExpressions.RegexOptions.IgnoreCase);
    void AssetsAndModels() {
        // Forward FT display models even where another reference replaced the
        // functional effect (notably the direct-actor-value Elemental Fury).
        foreach(var e in mod.MagicEffects)if(ft.MagicEffects.TryGetValue(e.FormKey,out var original)&&!original.MenuDisplayObject.IsNull)
            e.MenuDisplayObject.SetTo(M(original.MenuDisplayObject.FormKey));
        var elementalModel=M(ft.MagicEffects[K("Skyrim",0x2C56F)].MenuDisplayObject.FormKey);
        foreach(var e in mod.MagicEffects.Where(e=>e.EditorID?.StartsWith("YSMR_Elemental_VoiceElementalFury")==true))e.MenuDisplayObject.SetTo(elementalModel);
        var ftPath=Environment.GetEnvironmentVariable("YSMIR_FT_PLUGIN")??Path.Combine(root,"inputs","ft",FT);
        var archivePath=Path.ChangeExtension(ftPath,".bsa");
        var archive=Archive.CreateReader(GameRelease.SkyrimSE,archivePath);
        // Preserve FT's inventory art without replacing shared vanilla files.
        var privateModels=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase){
            ["meshes/magic/maginvshoutfire.nif"]="meshes/ysmir/inventory/fire.nif",
            ["meshes/dlc02/effects/maginvshoutbendwill.nif"]="meshes/ysmir/inventory/bendwill.nif"
        };
        foreach(var stat in mod.Statics)if(stat.Model is {} model) {
            var path=model.File.ToString().Replace('\\','/');
            if(!path.StartsWith("meshes/",StringComparison.OrdinalIgnoreCase))path="meshes/"+path;
            if(privateModels.TryGetValue(path,out var replacement))model.File=replacement[7..];
            else if(path.Contains("forceful tongue",StringComparison.OrdinalIgnoreCase))model.File=ResourcePath(model.File.GivenPath);
        }
        foreach(var art in mod.ArtObjects)if(art.Model is {} model) {
            if(model.File.GivenPath.Contains("forceful tongue",StringComparison.OrdinalIgnoreCase))model.File=ResourcePath(model.File.GivenPath);
        }
        var assets=new List<object>();
        foreach(var file in archive.Files) {
            var path=file.Path.Replace('\\','/');
            if(!(path.StartsWith("meshes/",StringComparison.OrdinalIgnoreCase)||path.StartsWith("textures/",StringComparison.OrdinalIgnoreCase)))continue;
            if(path.Contains("phantom",StringComparison.OrdinalIgnoreCase)||path.Equals("meshes/dungeons/nordic/chambers/powerwordsah.nif",StringComparison.OrdinalIgnoreCase))continue;
            if(privateModels.TryGetValue(path,out var replacement))path=replacement;
            path=ResourcePath(path).Replace('\\','/');
            var data=file.GetBytes();
            if(path.EndsWith(".nif",StringComparison.OrdinalIgnoreCase)) {
                // Equal-length ASCII path substitution preserves all NIF block
                // sizes, string lengths, geometry, materials and animations.
                var renamed=System.Text.Encoding.Latin1.GetBytes(ResourcePath(System.Text.Encoding.Latin1.GetString(data)));
                if(renamed.Length!=data.Length)throw new InvalidDataException("NIF path substitution changed its size");
                data=renamed;
            }
            var target=Path.Combine(root,"package",path);Directory.CreateDirectory(Path.GetDirectoryName(target)!);File.WriteAllBytes(target,data);
            assets.Add(new{path,bytes=data.Length,sha256=Convert.ToHexString(SHA256.HashData(data)).ToLowerInvariant()});
        }
        File.WriteAllText(Path.Combine(root,"reports","assets.json"),JsonSerializer.Serialize(assets,new JsonSerializerOptions{WriteIndented=true}));
    }
    void OwnNamespace() {
        foreach(var r in mod.EnumerateMajorRecords())
            if(r.EditorID?.StartsWith("FT_",StringComparison.Ordinal)==true)r.EditorID="YSMR_"+r.EditorID[3..];
        foreach(var e in mod.MagicEffects)foreach(var s in e.VirtualMachineAdapter?.Scripts??[])
            foreach(var p in s.Properties)if(p.Name=="FT_DLC2VoiceDragonAspectSummon")p.Name="YSMR_DragonAspectSummon";
        var ids=mod.EnumerateMajorRecords().Select(r=>r.EditorID).Where(id=>id!=null).ToArray();
        if(ids.Distinct(StringComparer.OrdinalIgnoreCase).Count()!=ids.Length)throw new InvalidDataException("Duplicate EditorID after namespace migration");
    }
    static readonly Dictionary<string,string> scriptNames=new(StringComparer.OrdinalIgnoreCase){
        ["FT_DLC2VoiceFrostBreathScript"]="YSMR_FrostBreath",
        ["DLC2VoiceBattleFuryScript"]="YSMR_BattleFury",["DLC2DBSummonAncientScript"]="YSMR_AncientDragonborn",
        ["FT_ShoutSpeedScript"]="YSMR_SpeedRefresh",["MQpathtoHHShrineEffectScript"]="YSMR_Shrine"
    };
    void KeywordsAndCleanup() {
        foreach(var l in mod.FormLists)foreach(var item in l.Items.Where(x=>sources.TryGetValue(x.FormKey,out var src)&&Phantom(src)).ToArray())l.Items.Remove(item);
        foreach(var effect in mod.MagicEffects)foreach(var script in effect.VirtualMachineAdapter?.Scripts??[]) {
            // FT only caches PlayerRef here; the original script calls
            // Game.GetPlayer() and otherwise implements the same fire-wyrm logic.
            if(script.Name.Equals("DLC2VoiceFireBreathScript",StringComparison.OrdinalIgnoreCase))
                foreach(var p in script.Properties.Where(p=>p.Name=="PlayerRef").ToArray())script.Properties.Remove(p);
            if(scriptNames.TryGetValue(script.Name,out var name))script.Name=name;
        }
        // Derive the spell graph from the actual 81 voice spells and script/ability
        // links, rather than applying MagicShout to every magic effect in the game.
        var spellKeys=mod.Shouts.SelectMany(s=>s.WordsOfPower.Select(w=>w.Spell.FormKey)).Concat(magnitudeExtras).ToHashSet();
        var queue=new Queue<FormKey>(spellKeys);var effectKeys=new HashSet<FormKey>();
        while(queue.TryDequeue(out var key)) {
            var item=Res(key);
            if(item is not ISpellGetter && item is not IObjectEffectGetter)continue;
            if(!mod.EnumerateMajorRecords().Any(r=>r.FormKey==key))Copy(item,key);
            IEnumerable<IEffectGetter> itemEffects=item is ISpellGetter spell?spell.Effects:((IObjectEffectGetter)item).Effects;
            foreach(var e in itemEffects) {
                var ek=e.BaseEffect.FormKey;effectKeys.Add(ek);
                if(Res(ek) is not IMagicEffectGetter me)throw new InvalidDataException("Missing MGEF "+ek);
                foreach(var link in me.EnumerateFormLinks()) {
                    if(link.FormKey.IsNull)continue;
                    var linked=mod.EnumerateMajorRecords().FirstOrDefault(r=>r.FormKey==link.FormKey)??sources.GetValueOrDefault(link.FormKey)??official.GetValueOrDefault(link.FormKey);
                    if(linked is ISpellGetter or IObjectEffectGetter && spellKeys.Add(link.FormKey))queue.Enqueue(link.FormKey);
                }
            }
        }
        var privateEffects=new Dictionary<FormKey,FormKey>();
        // These generic effects are also used by ordinary spells, staves and
        // scripted abilities. Isolate the complete FT/Ysmir change, including
        // Storm Call's MagicShout keyword, rather than tagging shared records.
        var sharedEffects=new uint[]{0xA44C0,0xEA075,0xEA076,0xE4CB6}.Select(id=>K("Skyrim",id)).ToHashSet();
        var cleanups=new Dictionary<FormKey,FormKey>();
        var cleanupCache=new Dictionary<string,FormKey>();
        foreach(var key in effectKeys) {
            var source=(IMagicEffectGetter)Res(key);
            MagicEffect e;
            if(!sharedEffects.Contains(key)&&mod.MagicEffects.TryGetValue(key,out var existing))e=existing;
            else {
                // Shared unmodified vanilla effects get a shout-only copy.
                e=(MagicEffect)Copy(source,NewKey());e.EditorID="YSMR_Shout_"+source.EditorID;privateEffects[key]=e.FormKey;
                if(sharedEffects.Contains(key))mod.Remove(new[]{key});
            }
            if((e.Flags&MagicEffect.Flag.DispelWithKeywords)!=0) {
                // KWDA is an imperative dispel selector on these records. Split
                // that native housekeeping instruction from the actual effect.
                // Tagging the selector with MagicShout would cancel ALL shouts.
                var selector=(e.Keywords??new()).Where(k=>k.FormKey!=K("Skyrim",0x46B99)).ToArray();
                var signature=e.CastType+":"+e.TargetType+":"+string.Join(";",selector.Select(k=>k.FormKey));
                if(!cleanupCache.TryGetValue(signature,out var cleanupKey)) {
                    var cleanup=Effect("YSMR_DispelSelector"+(cleanupCache.Count+1),MagicEffectArchetype.TypeEnum.Script,e.TargetType);
                    cleanup.CastType=e.CastType;cleanup.Flags=MagicEffect.Flag.DispelWithKeywords|MagicEffect.Flag.NoMagnitude|MagicEffect.Flag.NoDuration|MagicEffect.Flag.NoArea|MagicEffect.Flag.HideInUI|MagicEffect.Flag.NoHitEvent;
                    cleanup.Keywords=new();foreach(var k in selector)cleanup.Keywords.Add(new FormLink<IKeywordGetter>(k.FormKey));
                    cleanupCache[signature]=cleanupKey=cleanup.FormKey;
                }
                cleanups[key]=cleanupKey;e.Flags &= ~MagicEffect.Flag.DispelWithKeywords;
            }
            e.Keywords??=new(); if(!e.Keywords.Any(k=>k.FormKey==K("Skyrim",0x46B99)))e.Keywords.Add(new FormLink<IKeywordGetter>(K("Skyrim",0x46B99)));
        }
        foreach(var key in spellKeys) {
            var item=(IMajorRecord)Res(key);
            var itemEffects=item is Spell spell?spell.Effects:((ObjectEffect)item).Effects;
            foreach(var e in itemEffects.ToArray())if(cleanups.TryGetValue(e.BaseEffect.FormKey,out var cleanupKey)) {
                var helper=Eff(cleanupKey,0,(uint)(e.Data?.Area??0),0);helper.Conditions.AddRange(e.Conditions.Select(c=>c.DeepCopy()));
                itemEffects.Insert(itemEffects.IndexOf(e),helper);
            }
            item.RemapLinks(privateEffects);
        }
        // Keep the remapping local to shout effects, never to base-game spells.
        foreach(var key in effectKeys)if(mod.MagicEffects.TryGetValue(privateEffects.GetValueOrDefault(key,key),out var e))e.RemapLinks(privateEffects);
        var all=mod.EnumerateMajorRecords().ToDictionary(r=>r.FormKey);
        // Remove unreachable new display/wall/support records after Phantom removal.
        var live=new HashSet<FormKey>();var visit=new Queue<FormKey>(all.Keys.Where(k=>k.ModKey!=output).Concat(new[]{M(K(FT,0xF89))}));
        while(visit.TryDequeue(out var key))if(live.Add(key)&&all.TryGetValue(key,out var r))foreach(var link in r.EnumerateFormLinks())if(all.ContainsKey(link.FormKey))visit.Enqueue(link.FormKey);
        mod.Remove(all.Keys.Where(k=>k.ModKey==output&&!live.Contains(k)).ToArray());
    }
    void PreserveFormIDs() {
        // The released IDs are a build input, not a consequence of iteration
        // order. Reserve removed IDs too; new records only append to this map.
        var manifest=JsonSerializer.Deserialize<Dictionary<string,uint>>(File.ReadAllText(Path.Combine(root,"inputs","stable-formids.json")))!;
        var records=mod.EnumerateMajorRecords().Where(r=>r.FormKey.ModKey==output).ToArray();
        var next=manifest.Values.Max()+1;
        var keys=new Dictionary<FormKey,FormKey>();
        foreach(var r in records.OrderBy(r=>r.EditorID,StringComparer.Ordinal)) {
            if(string.IsNullOrEmpty(r.EditorID))throw new InvalidDataException("New record lacks EditorID");
            if(!manifest.TryGetValue(r.EditorID,out var id))manifest[r.EditorID]=id=next++;
            keys[r.FormKey]=new FormKey(output,id);
        }
        if(manifest.Values.Distinct().Count()!=manifest.Count||manifest.Values.Any(id=>id<0x800||id>0xFFF))throw new InvalidDataException("Invalid stable FormID manifest");
        mod.Remove(records.Select(r=>r.FormKey).ToArray());
        foreach(var r in records)Copy(r,keys[r.FormKey]);
        foreach(var r in mod.EnumerateMajorRecords())r.RemapLinks(keys);
        foreach(var key in remap.Keys.ToArray())remap[key]=keys.GetValueOrDefault(remap[key],remap[key]);
        File.WriteAllText(Path.Combine(root,"reports","stable-formids.json"),JsonSerializer.Serialize(manifest,new JsonSerializerOptions{WriteIndented=true}));
    }
    public void Run() {
        Inputs();Foundation();Console.WriteLine("Foundation ready");Elemental();Sprint();SoulTear();Storm();Progression();AssetsAndModels();Console.WriteLine("Features ready");KeywordsAndCleanup();OwnNamespace();PreserveFormIDs();Console.WriteLine("Keyword graph ready");
        var all=mod.EnumerateMajorRecords().ToDictionary(r=>r.FormKey);
        var errors=all.Values.SelectMany(r=>r.EnumerateFormLinks().Where(l=>!l.FormKey.IsNull&&l.FormKey!=K("Skyrim",0x14)&&!all.ContainsKey(l.FormKey)&&!official.ContainsKey(l.FormKey)).Select(l=>$"{r.EditorID} {r.FormKey} -> {l.FormKey}")).Distinct().ToArray();
        File.WriteAllLines(Path.Combine(root,"reports","unresolved.txt"),errors);
        if(errors.Length>0)throw new InvalidDataException("Unresolved links: "+string.Join("; ",errors.Take(15)));
        mod.ModHeader.Author="dickmna";
        mod.ModHeader.Description="Ysmir - Shout System Overhaul 1.2.0. Standalone native records, assets and Papyrus. 27 shouts, 5-second cooldown, MagicShout soul growth; native piercing Soul Tear and 100 additional reanimation slots.";
        mod.ModHeader.Flags|=SkyrimModHeader.HeaderFlag.Small;
        var path=Path.Combine(root,"package",output.FileName.String);
        var masters=new[]{"Skyrim","Update","Dawnguard","Dragonborn"}.Select(n=>K(n,0).ModKey).ToArray();
        mod.ModHeader.MasterReferences.Clear();foreach(var master in masters)mod.ModHeader.MasterReferences.Add(new MasterReference{Master=master});
        Console.WriteLine("Writing plugin");
        mod.WriteToBinary(path,new BinaryWriteParameters{MastersListContent=MastersListContentOption.NoCheck,MastersListOrdering=new MastersListOrderingByLoadOrder(masters)});
        using var check=SkyrimMod.CreateFromBinaryOverlay(path,SkyrimRelease.SkyrimSE);
        var masterCount=check.ModHeader.MasterReferences.Count;
        var questKey=M(K(FT,0xF89));
        Directory.CreateDirectory(Path.Combine(root,"package","SEQ"));
        // SEQ is an array of file-relative uint32 FormIDs, with no count/header.
        using(var w=new BinaryWriter(File.Create(Path.Combine(root,"package","SEQ",Path.GetFileNameWithoutExtension(output.FileName.String)+".seq"))))w.Write(((uint)masterCount<<24)|questKey.ID);
        var report=new {
            output=Path.GetFileName(path),inputs=inputHashes,sha256=Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))),bytes=new FileInfo(path).Length,
            masters=check.ModHeader.MasterReferences.Select(m=>m.Master.ToString()).ToArray(),records=all.Count,newRecords=all.Count(p=>p.Key.ModKey==output),
            shouts=check.Shouts.Select(s=>new {id=s.FormKey.ToString(),s.EditorID,flags=s.MajorFlags.ToString(),words=s.WordsOfPower.Select(w=>new{spell=w.Spell.FormKey.ToString(),w.RecoveryTime}).ToArray()}).ToArray(),
            spells=check.Spells.Select(s=>new{id=s.FormKey.ToString(),s.EditorID,effects=s.Effects.Select(e=>new{id=e.BaseEffect.FormKey.ToString(),e.Data?.Magnitude,e.Data?.Area,e.Data?.Duration}).ToArray()}).ToArray(),
            effects=check.MagicEffects.Select(e=>new{id=e.FormKey.ToString(),e.EditorID,type=e.Archetype.Type.ToString(),flags=e.Flags.ToString(),projectile=e.Projectile.FormKey.ToString(),keywords=e.Keywords?.Select(k=>k.FormKey.ToString()).ToArray(),scripts=e.VirtualMachineAdapter?.Scripts.Select(s=>s.Name).ToArray(),links=e.EnumerateFormLinks().Where(l=>!l.FormKey.IsNull).Select(l=>l.FormKey.ToString()).ToArray(),properties=e.VirtualMachineAdapter?.Scripts.SelectMany(s=>s.Properties.OfType<IScriptObjectPropertyGetter>().Select(p=>new{script=s.Name,p.Name,id=p.Object.FormKey.ToString()})).ToArray()}).ToArray(),
            baseline=check.Spells.Where(s=>official.ContainsKey(s.FormKey)).Select(s=>(ISpellGetter)official[s.FormKey]).Select(s=>new{id=s.FormKey.ToString(),s.EditorID,effects=s.Effects.Select(e=>new{id=e.BaseEffect.FormKey.ToString(),name=official[e.BaseEffect.FormKey].EditorID,e.Data?.Magnitude,e.Data?.Area,e.Data?.Duration}).ToArray()}).ToArray(),
            restoreCount=numericRestores.Count,numericRestores,unresolved=errors
        };
        File.WriteAllText(Path.Combine(root,"reports","build.json"),JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
        Console.WriteLine($"Wrote {path}: {all.Count} records, {check.Shouts.Count} shouts, {new FileInfo(path).Length} bytes.");
    }
}
