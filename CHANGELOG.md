# Changelog

## 1.2.5 — 2026-11-16

- Removes all five Miraak-specific shout cooldown overrides, restoring inheritance from the official game records. Player and Nord hero five-second recovery remains intact.
- Keeps the existing native player cast listener, including third-party Shout support and native recovery modifiers. No scripts, FormIDs or dependencies added.
- Rewrites the English Nexus description from the user's Chinese revision and reconciles the wording with the current runtime behavior.

## 1.2.4 — 2026-11-16

- Adds one short native OnSpellCast listener to the existing player alias. Normal player shouts, including third-party Shout forms, set the current recovery timer to five seconds times the current ShoutRecoveryMult, preserving native cooldown reductions.
- No polling, waits, new quests, alias IDs, FormIDs, SKSE or additional mod masters. Twelve runtime scripts; all previous scripts and gameplay records remain unchanged except the controller quest's new VMAD binding.
- Cooldown compatibility does not depend on MagicShout. Soul growth remains the same native keyword-based magnitude/duration perk at 2.5% per held soul; script constants, area and NPC levels do not automatically scale.
- External shout menu records and NPC cooldowns are not rewritten. Script-managed cooldowns and old-save event attachment require in-game testing; published validation is offline only.

## 1.2.3 — 2026-11-16

- Keeps all nine Nord hero variants at fixed level 150 and removes their extra Health, Magicka and Stamina offsets. Their native race/class auto-calculation, gear, perks and spell lists remain intact; player souls affect summon duration, not hero level.
- Keeps every hero shout at 5/5/5 seconds and adds only five necessary Miraak shout overrides. Preserves official spell/word links and quest behavior; other Miraak/Greybeard variants already have five-second cooldowns.
- Removes Animal Allegiance's Health/Stamina buffs and their two unused private effects. Released FormIDs stay reserved and are never reassigned.
- Built directly from 1.2.0; 2.5% soul progression, 11 runtime scripts and four official masters remain unchanged. No new runtime scripts or FormIDs.

## 1.2.0 — 2026-11-15

- Soul Tear uses a private native Cone projectile, preserving the original range, speed, zero spread, collision radius and visual fields.
- Adds 100 commanded-actor slots only for the actual Soul Tear reanimation spell, through the existing player perk and the Ritual Stone's native condition pattern.
- Migrates 276 inherited EditorIDs, one script property and retained resource paths to the Ysmir namespace; preserves every released FormID.
- Adds one projectile record. Runtime remains 11 scripts, no SKSE and no other mod masters.
- Replaces this repository's former SKSE implementation on the main branch with the native implementation. Earlier history and release tags remain available.

## 1.1.4

- Uses the original Dragonborn fire-wyrm script and removes its redundant custom counterpart; 11 runtime scripts.

## 1.1.3

- Restores the original Storm Call script. Native Companion's Insight handles the MagicShout-tagged private lightning damage effect.

## 1.1.2

- Uses Reduced Shaders for the Elemental Fury bursts.

## 1.1.1

- Isolates shared stagger and damage effects, protects shared vanilla inventory paths, and stabilizes released FormIDs.

## 1.1.0

- Establishes generic native MagicShout magnitude/duration growth, duration-only Slow Time progression and standalone shout changes.
