# Ysmir

- Keep the runtime native: no SKSE, DLL, additional mod masters or new scripts unless explicitly requested.
- Preserve released FormIDs in `inputs/stable-formids.json`; only append new IDs. Runtime identifiers use `YSMR_` and assets use `ysmir/`.
- Build with `rebuild.ps1` and the documented local inputs. Run `verify.py` after data changes; report game testing separately from offline verification.
- Do not commit game files, reference plugins, generated runtime assets or installation ZIPs. Publish source and reports; see `THIRD_PARTY.md`.
