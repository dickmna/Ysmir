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

// Build-time reference assets supply ONLY explicitly selected behavior graphs.
// All ordinary shout records are reconstructed from the winning official masters.
// The previous release is never a runtime dependency.
internal sealed class SelectedBuild : IDisposable
{
    static readonly ModKey OutputKey = ModKey.FromFileName("Ysmir.esp");
    readonly List<ISkyrimModDisposableGetter> masters = new();
    readonly Dictionary<FormKey, IMajorRecordGetter> official = new();
    readonly ISkyrimModDisposableGetter reference;
    readonly ISkyrimModDisposableGetter forceful;
    readonly Dictionary<FormKey, IMajorRecordGetter> assets;
    readonly SkyrimMod mod = new(OutputKey, SkyrimRelease.SkyrimSE);
    readonly Dictionary<FormKey, FormKey> map = new();
    readonly HashSet<FormKey> importedOfficial = new();
    readonly HashSet<FormKey> importedAssets = new();
    readonly List<IShoutGetter> roots;
    readonly List<FormKey> runtimeRoots = new();
    uint nextId = 0x140000;

    static FormKey K(string master, uint id) => new(ModKey.FromFileName(master + ".esm"), id);
    static string Suffix(FormKey key) => Path.GetFileNameWithoutExtension(key.ModKey.FileName.String) + $"_{key.ID:X6}";
    static bool Gameplay(IMajorRecordGetter r) => r is ISpellGetter or IMagicEffectGetter or IProjectileGetter;
    static void SetArchetype(MagicEffect e, MagicEffectArchetype.TypeEnum type) => e.Archetype = new MagicEffectArchetype { Type = type, ActorValue = ((IMagicEffectGetter)e).Archetype.ActorValue };
    FormKey Allocate() => new(OutputKey, nextId++);
    FormKey Private(FormKey key) => map.TryGetValue(key, out var value) ? value : map[key] = Allocate();
    IMajorRecordGetter Asset(string edid) => assets.Values.Single(r => r.EditorID == edid);

    public SelectedBuild(string data, string referencePath)
    {
        var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(referencePath)));
        if (hash != "DC122F95B4582F9E4F5D94437239AE0F4EC5E3FFBED8D6179A6A58DE672A5CC5")
            throw new InvalidDataException("Selected asset reference must be the audited 0.3.0 ESP.");
        // Resolve only official archives, without discovering the user's active mods.
        var strings = Path.Combine(Path.GetTempPath(), "Ysmir-0.4.0-official-strings");
        Directory.CreateDirectory(strings);
        foreach (var archiveName in new[] { "Skyrim - Interface.bsa", "Update.bsa", "Dawnguard.bsa", "Dragonborn.bsa" })
        {
            var archivePath = Path.Combine(data, archiveName);
            if (!File.Exists(archivePath)) continue;
            var archive = Archive.CreateReader(GameRelease.SkyrimSE, archivePath);
            if (archive.TryGetFolder("strings", out var folder))
                foreach (var file in folder.Files.Where(f => f.Path.Contains("_english.", StringComparison.OrdinalIgnoreCase)))
                    File.WriteAllBytes(Path.Combine(strings, Path.GetFileName(file.Path)), file.GetBytes());
        }
        foreach (var name in new[] { "Skyrim", "Update", "Dawnguard", "Dragonborn" })
        {
            var key = ModKey.FromFileName(name + ".esm");
            var loaded = SkyrimMod.CreateFromBinaryOverlay(new ModPath(key, Path.Combine(data, key.FileName.String)), SkyrimRelease.SkyrimSE,
                new BinaryReadParameters { StringsParam = new StringsReadParameters {
                    StringsFolderOverride = strings, BsaFolderOverride = strings, TargetLanguage = Language.English } });
            masters.Add(loaded);
            foreach (var r in loaded.EnumerateMajorRecords()) official[r.FormKey] = r;
        }
        reference = SkyrimMod.CreateFromBinaryOverlay(new ModPath(OutputKey, referencePath), SkyrimRelease.SkyrimSE);
        var ftPath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(referencePath))!, "ForcefulTongue.esp");
        if (Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(ftPath))) != "E3DB5982E36CBB1CBA7D33029B4B2B3E394B80CBCBDB1C722D949E3056E2BAC2")
            throw new InvalidDataException("Dragon Aspect requires the original Forceful Tongue 3.0.9.1 reference.");
        forceful = SkyrimMod.CreateFromBinaryOverlay(new ModPath(ModKey.FromFileName("ForcefulTongue.esp"), ftPath), SkyrimRelease.SkyrimSE);
        assets = reference.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        roots = reference.Shouts.Where(s => s.EditorID?.StartsWith("YSMIR_PlayerShout_") == true)
            .Where(s => RootKey(s) != K("Dragonborn", 0x02AD09)) // Battle Fury is explicitly outside the user's requested changes.
            .OrderBy(s => s.FormKey.ID).Cast<IShoutGetter>().ToList();
        foreach (var r in assets.Values.Where(r => r.EditorID?.StartsWith("YSMIR_Player") == true))
        {
            var parts = r.EditorID!.Split('_');
            if (parts.Length != 4) continue;
            var key = K(parts[2], Convert.ToUInt32(parts[3], 16));
            if (official.ContainsKey(key)) map[key] = r.FormKey;
        }
        foreach (var r in assets.Values.Where(r => r.EditorID?.StartsWith("YSMIR_ShoutOnly_") == true))
        {
            var key = K("Skyrim", Convert.ToUInt32(r.EditorID!.Split('_')[^1], 16));
            if (official.ContainsKey(key)) map[key] = r.FormKey;
        }
        foreach (var root in roots)
        {
            var key = RootKey(root);
            var source = (IShoutGetter)official[key];
            for (var tier = 0; tier < 3; ++tier)
                map[source.WordsOfPower[tier].Spell.FormKey] = root.WordsOfPower[tier].Spell.FormKey;
        }
    }

    static FormKey RootKey(IShoutGetter s)
    {
        var parts = s.EditorID!.Split('_');
        return K(parts[2], Convert.ToUInt32(parts[3], 16));
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

    IEnumerable<FormKey> ExecutableLinks(IMajorRecordGetter r, IReadOnlyDictionary<FormKey, IMajorRecordGetter> db)
    {
        if (r is ISpellGetter spell)
            return spell.Effects.Select(e => e.BaseEffect.FormKey);
        if (r is IMagicEffectGetter effect)
            return effect.EnumerateFormLinks(false).Select(l => l.FormKey)
                .Where(k => db.TryGetValue(k, out var linked) && linked is ISpellGetter or IProjectileGetter);
        return Array.Empty<FormKey>();
    }

    FormKey ImportOfficial(FormKey key)
    {
        if (!official.TryGetValue(key, out var source) || !Gameplay(source)) return key;
        var target = Private(key);
        if (!importedOfficial.Add(key)) return target;
        var copy = Copy(source, target);
        copy.EditorID = "YSMIR_Player" + (source is ISpellGetter ? "Spell_" : source is IMagicEffectGetter ? "Effect_" : "Projectile_") + Suffix(key);
        foreach (var linked in ExecutableLinks(source, official)) ImportOfficial(linked);
        return target;
    }

    FormKey ImportAsset(FormKey key)
    {
        if (key.ModKey != OutputKey) return key;
        if (!importedAssets.Add(key)) return key;
        var source = assets[key];
        Copy(source, key);
        foreach (var link in source.EnumerateFormLinks(false))
            if (!link.FormKey.IsNull && link.FormKey.ModKey == OutputKey) ImportAsset(link.FormKey);
        return key;
    }

    Spell Voice(FormKey shout, int tier) => mod.Spells[mod.Shouts[Private(shout)].WordsOfPower[tier].Spell.FormKey];
    ISpellGetter OriginalVoice(FormKey shout, int tier) => (ISpellGetter)official[((IShoutGetter)official[shout]).WordsOfPower[tier].Spell.FormKey];
    ISpellGetter ReferenceVoice(FormKey shout, int tier) => (ISpellGetter)assets[((IShoutGetter)assets[Private(shout)]).WordsOfPower[tier].Spell.FormKey];

    void UseSelectedShout(FormKey shout)
    {
        for (var tier = 0; tier < 3; ++tier)
        {
            var asset = ReferenceVoice(shout, tier);
            ImportAsset(asset.FormKey);
            mod.Shouts[Private(shout)].WordsOfPower[tier].Spell.SetTo(asset.FormKey);
        }
    }

    Dictionary<FormKey, FormKey> ImportForcefulGraph(IEnumerable<FormKey> start)
    {
        var db = forceful.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        var remap = new Dictionary<FormKey, FormKey>();
        FormKey Import(FormKey key)
        {
            if (remap.TryGetValue(key, out var existing)) return existing;
            if (!db.TryGetValue(key, out var source)) return key;
            if (source is not (ISpellGetter or IMagicEffectGetter or IPerkGetter or INpcGetter or IArtObjectGetter or IEffectShaderGetter or IProjectileGetter or IKeywordGetter or IObjectEffectGetter or IStaticGetter))
            {
                if (key.ModKey == forceful.ModKey) throw new InvalidDataException($"Unexpected selected FT dependency: {key}");
                return key;
            }
            var target = key.ModKey == forceful.ModKey
                ? assets.Values.FirstOrDefault(r => r.EditorID == source.EditorID)?.FormKey ?? Allocate()
                : Private(key);
            remap[key] = target;
            var copy = Copy(source, target);
            copy.EditorID = assets.TryGetValue(target, out var prior) ? prior.EditorID : "YSMIR_Selected_" + source.EditorID;
            foreach (var link in source.EnumerateFormLinks(false)) if (!link.FormKey.IsNull) Import(link.FormKey);
            return target;
        }
        foreach (var key in start) Import(key);
        var records = mod.EnumerateMajorRecords().ToDictionary(r => r.FormKey);
        foreach (var target in remap.Values) records[target].RemapLinks(remap);
        foreach (var effect in mod.MagicEffects.Where(e => remap.ContainsValue(e.FormKey) && e.VirtualMachineAdapter != null))
            foreach (var script in effect.VirtualMachineAdapter!.Scripts)
                if (script.Name.Equals("dlc2DBSummonAncientSCRIPT", StringComparison.OrdinalIgnoreCase)) script.Name = "YsmirDragonAspectSummonScript";
        return remap;
    }

    void UseForcefulDragonAspect()
    {
        var shout = K("Dragonborn", 0x01DF92);
        var original = forceful.Shouts[shout];
        // The official menu must not retain vanilla's once-per-day description.
        mod.Shouts[shout].Description = original.Description?.DeepCopy();
        mod.Shouts[Private(shout)].Description = original.Description?.DeepCopy();
        var remap = ImportForcefulGraph(original.WordsOfPower.Select(w => w.Spell.FormKey));
        for (var tier = 0; tier < 3; ++tier)
        {
            mod.Shouts[Private(shout)].WordsOfPower[tier].Spell.SetTo(remap[original.WordsOfPower[tier].Spell.FormKey]);
            mod.Shouts[Private(shout)].WordsOfPower[tier].RecoveryTime = original.WordsOfPower[tier].RecoveryTime;
        }
    }

    void UseQualityFixes()
    {
        // Fire Wyrm behavior only: preserve official level, stats, spells, attacks
        // and equipment; FT supplies hostile-only aggression and mobile packages.
        var wyrmKey = K("Dragonborn", 0x01F990);
        var wyrm = (Npc)Copy(official[wyrmKey], Private(wyrmKey));
        wyrm.EditorID = "YSMIR_PlayerNpc_Dragonborn_01F990";
        wyrm.AIData.Aggression = forceful.Npcs[wyrmKey].AIData.Aggression;
        wyrm.DefaultPackageList.SetTo(forceful.Npcs[wyrmKey].DefaultPackageList.FormKey);
        var replacement = new Dictionary<FormKey, FormKey> { [wyrmKey] = wyrm.FormKey };
        foreach (var effect in mod.MagicEffects) effect.RemapLinks(replacement);
    }

    public void Build(string destination)
    {
        Directory.CreateDirectory(destination);
        mod.ModHeader.Author = "dickmna; selected FT assets by Delta";
        mod.ModHeader.Description = "Ysmir 0.4.0: official baselines, selected shout behaviors, one progression core.";

        foreach (var root in roots)
        {
            var key = RootKey(root);
            var source = (IShoutGetter)official[key];
            var copy = mod.Shouts.DuplicateInAsNewRecord(source, Private(key));
            copy.EditorID = root.EditorID;
            copy.MenuDisplayObject.SetTo(ImportAsset(root.MenuDisplayObject.FormKey));
            for (var tier = 0; tier < 3; ++tier)
                copy.WordsOfPower[tier].Spell.SetTo(ImportOfficial(source.WordsOfPower[tier].Spell.FormKey));
            // Official overrides only change presentation (model/selected description).
            var menu = source.DeepCopy();
            menu.MenuDisplayObject.SetTo(copy.MenuDisplayObject.FormKey);
            mod.Shouts.Set(menu);
            runtimeRoots.Add(copy.FormKey);
        }
        // Remap official gameplay links once after the full official closure exists.
        var officialGraphMap = importedOfficial.ToDictionary(k => k, k => map[k]);
        foreach (var r in mod.EnumerateMajorRecords().Where(r => r.FormKey.ModKey == OutputKey)) r.RemapLinks(officialGraphMap);

        // Only these FT graphs are requested in their entirety; their numeric baselines
        // already match vanilla. Dismay introduces an authored 50/75/100 area mechanic.
        foreach (var id in new uint[] { 0x02395A, 0x07097B, 0x07097E, 0x032921 }) UseSelectedShout(K("Skyrim", id));
        // Explicit user exception: retain FT's COMPLETE Dragon Aspect feature graph.
        UseForcefulDragonAspect();
        UseQualityFixes();

        // Drain Vitality changes the transfer archetype, keeping every vanilla EFIT.
        foreach (var id in new uint[] { 0x008448, 0x008449, 0x00844A })
            SetArchetype(mod.MagicEffects[Private(K("Dawnguard", id))], MagicEffectArchetype.TypeEnum.Absorb);

        var marked = K("Skyrim", 0x07097C);
        var weakness = ImportAsset(Asset("FT_VoiceMarkedForDeathWeaknessMagicEffect").FormKey);
        for (var tier = 0; tier < 3; ++tier)
        {
            var requested = ReferenceVoice(marked, tier).Effects.Single(e => e.BaseEffect.FormKey == weakness).DeepCopy();
            Voice(marked, tier).Effects.Add(requested);
        }
        // Temporary armor debuff and actual health damage; retain 25/50/75, 1/2/3, 60s.
        SetArchetype(mod.MagicEffects[Private(K("Skyrim", 0x10319C))], MagicEffectArchetype.TypeEnum.ValueModifier);
        mod.MagicEffects[Private(K("Skyrim", 0x10319C))].Flags &= ~MagicEffect.Flag.Recover;
        SetArchetype(mod.MagicEffects[Private(K("Skyrim", 0x10319D))], MagicEffectArchetype.TypeEnum.PeakValueModifier);
        mod.MagicEffects[Private(K("Skyrim", 0x10319D))].Flags |= MagicEffect.Flag.Recover;

        UseSelectedShout(K("Skyrim", 0x048AC9));
        for (var tier = 1; tier <= 3; ++tier)
        {
            var actualKey = ImportAsset(Asset($"YSMIR_SlowTimeActual{tier}").FormKey);
            runtimeRoots.Add(actualKey);
            var actual = mod.Spells[actualKey];
            actual.Effects.Single().Data!.Magnitude = new[] { 0.3f, 0.2f, 0.1f }[tier - 1];
            actual.Effects.Single().Data!.Duration = new int[] { 8, 12, 16 }[tier - 1];
            var wrapper = mod.MagicEffects[Asset($"YSMIR_SlowTimeWrapperEffect{tier}").FormKey];
            foreach (var script in wrapper.VirtualMachineAdapter!.Scripts)
                if (script.Name.Equals("magiccastanotherspellscript", StringComparison.OrdinalIgnoreCase))
                {
                    script.Name = "YsmirSlowTimeScript";
                    foreach (var property in script.Properties.Where(p => p.Name != "CastSpellName").ToArray()) script.Properties.Remove(property);
                }
        }

        for (var tier = 0; tier < 3; ++tier)
        {
            var voice = Voice(K("Skyrim", 0x02F7BA), tier);
            foreach (var e in ReferenceVoice(K("Skyrim", 0x02F7BA), tier).Effects)
                if (assets[e.BaseEffect.FormKey].EditorID?.StartsWith("YSMIR_Whirlwind_") == true)
                {
                    ImportAsset(e.BaseEffect.FormKey);
                    voice.Effects.Add(e.DeepCopy());
                }
        }

        var soulCone = ImportAsset(Asset("YSMIR_SoulTearConeProjectile").FormKey);
        foreach (var id in new uint[] { 0x007CB5, 0x015714, 0x01AA33, 0x007CB3 })
        {
            var effect = mod.MagicEffects[Private(K("Dawnguard", id))];
            effect.TargetType = TargetType.Aimed;
            effect.Projectile.SetTo(soulCone);
        }
        for (var tier = 0; tier < 3; ++tier) Voice(K("Dawnguard", 0x007CB6), tier).TargetType = TargetType.Aimed;

        var heroes = new Dictionary<FormKey, FormKey>();
        // Explicit user exception: FT hero attributes/behavior with fixed level 150.
        // These six frozen reference records already contain that exact selection.
        foreach (var id in new uint[] { 0x0923F9, 0x0923FA, 0x0923FB, 0x044236, 0x044237, 0x044238 })
            heroes[K("Skyrim", id)] = ImportAsset(Asset($"YSMIR_PlayerNpc_Skyrim_{id:X6}").FormKey);
        foreach (var id in new uint[] { 0x051963, 0x051965, 0x051966 }) mod.MagicEffects[Private(K("Skyrim", id))].RemapLinks(heroes);

        foreach (var element in new[] { "Fire", "Frost", "Shock" })
            for (var tier = 1; tier <= 3; ++tier)
                runtimeRoots.Add(ImportAsset(Asset($"YSMIR_ElementalFury{element}{tier}").FormKey));
        var stormProjectile = ImportAsset(Asset("YSMIR_StormCallProjectile").FormKey);
        runtimeRoots.Add(stormProjectile);

        // Imported feature effects can refer to formerly overhauled projectiles.
        // Restore every official projectile baseline; custom cone/elemental/storm
        // projectiles have their own explicit provenance and are not in this map.
        foreach (var pair in map)
            if (official.TryGetValue(pair.Key, out var source) && source is IProjectileGetter && mod.Projectiles.ContainsKey(pair.Value))
                Copy(source, pair.Value).EditorID = "YSMIR_PlayerProjectile_" + Suffix(pair.Key);

        foreach (var effect in mod.MagicEffects)
        {
            if (effect.VirtualMachineAdapter is not { } vmad) continue;
            foreach (var script in vmad.Scripts)
                if (script.Name.Equals("VoicePushEffectScript", StringComparison.OrdinalIgnoreCase)) script.Name = "YsmirVoicePushEffectScript";
        }
        // The native storm adapter owns the storm. Its private voice roots only expose
        // the vanilla duration; no legacy Papyrus controller/hazard is executed twice.
        for (var tier = 0; tier < 3; ++tier)
        {
            var voice = Voice(K("Skyrim", 0x07097D), tier);
            var stub = mod.MagicEffects.DuplicateInAsNewRecord((IMagicEffectGetter)official[K("Skyrim", 0x10E4FD)], Allocate());
            stub.EditorID = $"YSMIR_StormDuration{tier + 1}";
            stub.VirtualMachineAdapter = null;
            voice.Effects.Clear();
            var e = new Effect { Data = new EffectData { Duration = 60 * (tier + 1) } };
            e.BaseEffect.SetTo(stub.FormKey);
            voice.Effects.Add(e);
        }

        // Reachability pruning makes unselected FT/world/quest/level systems impossible
        // to enter the runtime artifact, even though the frozen reference contains them.
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
                marker = new MagicEffect(Allocate(), SkyrimRelease.SkyrimSE) {
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
            version = "0.4.0", records = records.Count, privateRecords = records.Keys.Count(k => k.ModKey == OutputKey),
            playerEffects = playerGraph.Count(k => records[k] is IMagicEffectGetter), unresolvedLinks = missing,
            recordTypes = records.Values.GroupBy(r => r.GetType().Name).ToDictionary(g => g.Key, g => g.Count()),
            scripts = mod.MagicEffects.Where(e => e.VirtualMachineAdapter != null).SelectMany(e => e.VirtualMachineAdapter!.Scripts.Select(s => s.Name)).Distinct().Order(),
            selected = records.Values.Select(r => new { key = r.FormKey.ToString(), edid = r.EditorID, type = r.GetType().Name }),
            numbers = roots.Select(r => new { shout = r.EditorID, words = mod.Shouts[r.FormKey].WordsOfPower.Select(w => new { cooldown = w.RecoveryTime, spell = w.Spell.FormKey.ToString(), effects = mod.Spells[w.Spell.FormKey].Effects.Select(e => new { effect = e.BaseEffect.FormKey.ToString(), e.Data }) }) })
        }, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"Built {records.Count} records; {playerGraph.Count(k => records[k] is IMagicEffectGetter)} player MGEFs; no unresolved links.");
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
            t.AppendLine("{ " + string.Join(", ", Enumerable.Range(1, 3).Select(i => H(Asset($"YSMIR_ElementalFury{element}{i}").FormKey))) + " },");
        t.AppendLine("}};\ninline constexpr std::array<std::uint32_t,3> SlowTimeActualSpells{ " + string.Join(", ", Enumerable.Range(1, 3).Select(i => H(Asset($"YSMIR_SlowTimeActual{i}").FormKey))) + " };");
        t.AppendLine($"inline constexpr std::uint32_t StormProjectile = {H(Asset("YSMIR_StormCallProjectile").FormKey)};");
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

    public void Dispose() { reference.Dispose(); forceful.Dispose(); foreach (var m in masters) m.Dispose(); }

    public static int Main(string[] args)
    {
        if (args.Length != 3) { Console.Error.WriteLine("Usage: builder GAME_DATA AUDITED_REFERENCE_ESP OUTPUT_DIRECTORY"); return 2; }
        try
        {
            using var builder = new SelectedBuild(args[0], args[1]);
            builder.Build(args[2]);
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
    }
}
