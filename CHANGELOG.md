# Changelog

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
