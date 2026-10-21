# Ysmir

- Use Shout Progression's shared progression model and preserve the selected upstream mechanics. Keep integration code small; do not add another gameplay controller for an existing feature.
- Keep authored projectile angles and initial cone collision radii unchanged at runtime. Never write through ConeProjectile collisionShape as an hkpSphereShape; the actual 1.7.104 pointer is a bhk wrapper.
- Scale cone VFX uniformly around the original anchor with smooth distance growth. Keep native birth size; do not add per-model layer scaling, UV edits, height offsets, or replacement projectile NIFs.
- Preserve FormIds and the generated RuntimeForms.h/ESP pairing. Upstream build inputs are SHA-256 pinned in SourceRules.cs; do not publish those inputs or game files in the source archive.
- Validate native builds, integration regressions, generated records and runtime metadata. Offline checks are not gameplay tests; report that boundary explicitly.
- Source/provenance: docs/ARCHITECTURE.md, docs/COMPATIBILITY.md, docs/THIRD_PARTY_NOTICES.md. Build commands: docs/BUILD.md.
