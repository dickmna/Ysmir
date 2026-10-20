using System;
using System.Collections.Generic;
using Mutagen.Bethesda.Plugins;

internal static class SourceRules
{
    internal const string Version = "0.7.0";
    internal const string Forceful = "ForcefulTongue.esp";
    internal const string Elemental = "Elemental Fury for MCO ADXP.esp";
    internal const string ElementalPatch = "Elemental Fury Shout Overhaul - Forceful Tongue Patch.esp";
    internal const string SlowTime = "Slow Time Edit.esp";
    internal const string Whirlwind = "WhirlwindSprintRedone version 2.esp";
    internal static readonly (string File, string Sha256)[] Inputs = {
        (Forceful, "E3DB5982E36CBB1CBA7D33029B4B2B3E394B80CBCBDB1C722D949E3056E2BAC2"),
        (Elemental, "239CFFFCC6D06E313250BCD82752E1811E3D1A01B304EA006E3E33E8861B4C5D"),
        (ElementalPatch, "14DD66FD4D8977FBC521F21CDDD164C1F46D5BBB41C288F87C7C7E6B918138DC"),
        (SlowTime, "DEA71666EA073934E550B3D8D0157929928E34BB7998093D40AA790139A5190C"),
        (Whirlwind, "66FD54CB9A923C6D122756320A57B5984AF58949F47EB811A9ECD7F791B4C837")
    };
    internal enum Mechanism { Official, Forceful, Elemental, SlowTime, Whirlwind, DragonAspect, SoulTear, Storm, Drain, Marked, Valor }
    internal sealed record ShoutRule(string Master, uint Id, Mechanism Mechanic)
    {
        internal FormKey Key => new(ModKey.FromFileName(Master + ".esm"), Id);
    }
    // Explicit player scope. Battle Fury, NPC/dragon shouts and FT acquisition systems are absent.
    internal static readonly ShoutRule[] Shouts = {
        new("Dawnguard", 0x0030D2, Mechanism.Official), new("Dawnguard", 0x007CB6, Mechanism.SoulTear),
        new("Dawnguard", 0x008A62, Mechanism.Drain), new("Skyrim", 0x013E07, Mechanism.Official),
        new("Dragonborn", 0x0179D8, Mechanism.Official), new("Dragonborn", 0x01DF92, Mechanism.DragonAspect),
        new("Dragonborn", 0x0200C0, Mechanism.Official), new("Skyrim", 0x02395A, Mechanism.Forceful),
        new("Skyrim", 0x02F7BA, Mechanism.Whirlwind), new("Skyrim", 0x032920, Mechanism.Official),
        new("Skyrim", 0x032921, Mechanism.Elemental), new("Skyrim", 0x03CD34, Mechanism.Official),
        new("Skyrim", 0x03F9EA, Mechanism.Official), new("Skyrim", 0x044250, Mechanism.Official),
        new("Skyrim", 0x046B8C, Mechanism.Official), new("Skyrim", 0x048AC9, Mechanism.SlowTime),
        new("Skyrim", 0x05196A, Mechanism.Valor), new("Skyrim", 0x05D16B, Mechanism.Official),
        new("Skyrim", 0x05FC77, Mechanism.Official), new("Skyrim", 0x07097B, Mechanism.Forceful),
        new("Skyrim", 0x07097C, Mechanism.Marked), new("Skyrim", 0x07097D, Mechanism.Storm),
        new("Skyrim", 0x07097E, Mechanism.Forceful), new("Skyrim", 0x07097F, Mechanism.Official),
        new("Skyrim", 0x070980, Mechanism.Official), new("Skyrim", 0x070981, Mechanism.Official)
    };
    internal static readonly uint[] Heroes = { 0x0923F9, 0x0923FA, 0x0923FB, 0x044236, 0x044237, 0x044238 };

    internal static string? ImportedName(FormKey key, string? original)
    {
        if (key.ModKey.FileName.String == "Dragonborn.esm")
            return key.ID switch {
                0x01DF8F => "YSMIR_PlayerPerk_Dragonborn_01DF8F", 0x01DF9B => "YSMIR_PlayerPerk_Dragonborn_01DF9B",
                0x031853 => "YSMIR_Selected_FT_DLC2VoiceDragonAspectCombatSpell",
                0x031854 => "YSMIR_Selected_FT_DLC2VoiceDragonAspectCombatEffect",
                0x031855 => "YSMIR_Selected_FT_DLC2VoiceDragonAspectSummon",
                0x031856 => "YSMIR_Selected_FT_DLC2VoiceDragonAspectSummonEffect", _ => null
            };
        if (key.ModKey.FileName.String == SlowTime)
            return key.ID switch {
                0x800 => "YSMIR_SlowTimeActual1", 0x801 => "YSMIR_SlowTimeActual2", 0x802 => "YSMIR_SlowTimeActual3",
                0x803 => "YSMIR_SlowTimeWrapperEffect1", 0x805 => "YSMIR_SlowTimeWrapperEffect2", 0x806 => "YSMIR_SlowTimeWrapperEffect3",
                0x804 => "YSMIR_SlowTimePlayerExemptionPerk", _ => null
            };
        if (key.ModKey.FileName.String == Whirlwind) return "YSMIR_Whirlwind_" + original;
        if (key.ModKey.FileName.String != Elemental) return null;
        for (var element = 0; element < 3; ++element) {
            var name = new[] { "Fire", "Frost", "Shock" }[element];
            var effect = 0x800u + (uint)element * 0x10;
            var spell = 0x850u + (uint)element * 0x10;
            if (key.ID >= effect && key.ID < effect + 3) return $"YSMIR_ElementalFury{name}CloakEffect{key.ID - effect + 1}";
            if (key.ID == effect + 3) return $"YSMIR_ElementalFury{name}EnemyEffect1";
            if (key.ID >= spell && key.ID < spell + 3) return $"YSMIR_ElementalFury{name}{key.ID - spell + 1}";
            if (key.ID >= spell + 3 && key.ID < spell + 6) return $"YSMIR_ElementalFury{name}Damage{key.ID - spell - 2}";
        }
        if (original == "FlameBlastDmgFXShader") return "YSMIR_ElementalFuryFireEnemyShader";
        return key.ID switch { 0x830 => "YSMIR_ElementalFuryActiveEffect2", 0x831 => "YSMIR_ElementalFuryActiveEffect3", _ => "YSMIR_" + original };
    }
}
