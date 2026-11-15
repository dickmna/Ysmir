"""Independently audit 1.2.3 against a local 1.2.0 ESP and official masters.

No game files are written. This verifies data, not in-game AI or quest behavior.
"""
from pathlib import Path
import argparse, hashlib, json, mmap, struct, zlib
from verify import read_records, subrecords, field, fields, U16, U32, F32, ROOT

HEROES = {0x44236, 0x44237, 0x44238, 0x923F9, 0x923FA, 0x923FB, 0xCD9F7, 0xCDA00, 0xCDA01}
ANIMAL_SPELLS = {0x9E0CC, 0x9E0CD, 0x9E0CE}
ANIMAL_EFFECTS = {0x9E0C8, 0x9E0CA, 0x9E0CB}
REMOVED = {0x04000809, 0x0400080A}
MASTERS = ['Skyrim.esm', 'Update.esm', 'Dawnguard.esm', 'Dragonborn.esm']


def official_records(path):
    """Read just SHOU and Miraak NPC records without loading a whole master."""
    with path.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as raw:
        n = U32(raw, 4)
        header = list(subrecords(raw[24:24+n]))
        masters = [v.rstrip(b'\0').decode() for k, v in header if k == 'MAST'] + [path.name]

        def remap(fid):
            if not fid:
                return 0
            return (MASTERS.index(masters[fid >> 24]) << 24) | (fid & 0xFFFFFF)

        def walk(start, end):
            p = start
            while p < end:
                tag, size = raw[p:p+4].decode(), U32(raw, p+4)
                if tag == 'GRUP':
                    yield from walk(p+24, p+size)
                    p += size
                    continue
                if tag in ('SHOU', 'NPC_'):
                    data = raw[p+24:p+24+size]
                    if U32(raw, p+8) & 0x40000:
                        data = zlib.decompress(data[4:])
                    fs = list(subrecords(data))
                    edid = next((v.rstrip(b'\0').decode() for k, v in fs if k == 'EDID'), '')
                    if tag == 'SHOU' or (edid.startswith('DLC2Miraak') and 'Dragon' not in edid):
                        mapped = []
                        for k, v in fs:
                            if k in ('MDOB', 'SPLO', 'TPLT'):
                                v = struct.pack('<I', remap(U32(v)))
                            elif k == 'SNAM' and tag == 'SHOU':
                                v = struct.pack('<II', remap(U32(v)), remap(U32(v, 4))) + v[8:]
                            mapped.append((k, v))
                        yield dict(tag=tag, fid=remap(U32(raw, p+12)), edid=edid,
                                   flags=U32(raw, p+8), fields=mapped)
                p += 24+size
        yield from walk(0, len(raw))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--game-data', required=True, type=Path)
    args = parser.parse_args()
    baseline = read_records(args.baseline)
    plugin = ROOT / 'package' / 'Ysmir - Shout System Overhaul.esp'
    current = read_records(plugin)
    old = {r['fid']: r for r in baseline[1:]}
    new = {r['fid']: r for r in current[1:]}
    expected = json.loads((ROOT/'inputs/miraak-shouts.json').read_text())
    added = {int(r['id'], 16) for r in expected if r['overrideNeeded']}
    checks = []

    def check(ok, text):
        assert ok, text
        checks.append(text)

    check(hashlib.sha256(args.baseline.read_bytes()).hexdigest() ==
          '91ae623bbe9fffd9eba3624cb50cdb37cc0e79c23c7f746004c46c16f294c44d',
          'Baseline is the published 1.2.0 ESP')
    check(set(old)-set(new) == REMOVED, 'Only the two unused animal attribute effects are removed')
    check(set(new)-set(old) == added, 'Only five official Miraak shout overrides are added')
    changed = []
    for fid in sorted(set(old) & set(new)):
        a, b = old[fid], new[fid]
        check((a['tag'], a['flags'], a['edid']) == (b['tag'], b['flags'], b['edid']),
              f'Record identity and flags preserved: {a["edid"]}')
        if a['fields'] == b['fields']:
            continue
        changed.append(fid)
        if fid in HEROES:
            acbs = bytearray(field(a, 'ACBS'))
            for offset in (4, 6, 20):
                struct.pack_into('<h', acbs, offset, 0)
            wanted = [(k, bytes(acbs) if k == 'ACBS' else v) for k, v in a['fields']]
            check(b['fields'] == wanted, f'Only Health/Magicka/Stamina offsets change: {a["edid"]}')
        elif fid in ANIMAL_SPELLS:
            wanted, skip = [], False
            for k, v in a['fields']:
                if k == 'EFID':
                    skip = U32(v) in REMOVED
                if not skip:
                    wanted.append((k, v))
            check(b['fields'] == wanted, f'Only animal attribute payloads are removed: {a["edid"]}')
        elif fid in ANIMAL_EFFECTS:
            check([(k, v) for k, v in a['fields'] if k != 'DNAM'] ==
                  [(k, v) for k, v in b['fields'] if k != 'DNAM'],
                  f'Animal allegiance behavior unchanged; description corrected: {a["edid"]}')
        else:
            raise AssertionError(f'Unexpected change: {a["edid"]} {fid:08X}')
    check(set(changed) == HEROES | ANIMAL_SPELLS | ANIMAL_EFFECTS,
          'Exactly nine hero records and six Animal Allegiance records change')
    check([(k, v) for k, v in baseline[0]['fields'] if k not in ('HEDR', 'SNAM')] ==
          [(k, v) for k, v in current[0]['fields'] if k not in ('HEDR', 'SNAM')],
          'Plugin header changes only record counts and version description')

    official = {}
    for name in MASTERS:
        official.update({r['fid']: r for r in official_records(args.game_data/name)})
    for fid in added:
        a, b = official[fid], new[fid]
        # Localized display strings become English strings in the standalone ESP.
        wanted = [(k, v[:8]+struct.pack('<f', 5) if k == 'SNAM' else v)
                  for k, v in a['fields'] if k not in ('FULL', 'DESC')]
        check(b['flags'] == a['flags'] and
              [(k, v) for k, v in b['fields'] if k not in ('FULL', 'DESC')] == wanted,
              f'Miraak override changes only cooldowns; all word/spell/art links preserved: {a["edid"]}')
    miraak_actors = [r for r in official.values() if r['tag'] == 'NPC_']
    check(len(miraak_actors) == 7, 'All seven original Miraak actor variants audited')
    for actor in miraak_actors:
        for v in fields(actor, 'SPLO'):
            shout = new.get(U32(v)) or official.get(U32(v))
            if shout and shout['tag'] == 'SHOU':
                check(all(F32(w, 8) == 5 for w in fields(shout, 'SNAM')),
                      f'Miraak learned shout uses five seconds: {actor["edid"]} / {shout["edid"]}')
    for r in expected:
        fid = int(r['id'], 16)
        original = official[fid]
        check([F32(v, 8) for v in fields(original, 'SNAM')] == r['cooldowns'],
              'Independent official cooldown matches audit input: '+r['editorID'])
    check(not any(r['tag'] == 'QUST' for fid, r in new.items() if fid in added | set(changed)),
          'No quest record or alias changes')
    stable = json.loads((ROOT/'inputs/stable-formids.json').read_text())
    check({stable['YSMR_VoiceAnimalAllegianceFortifyHealthEffect'],
           stable['YSMR_VoiceAnimalAllegianceFortifyStaminaEffect']} == {0x809, 0x80A},
          'Removed effect IDs remain reserved for save compatibility')
    report = dict(status='PASS',version='1.2.3',baseline='1.2.0',gameExecuted=False,
                  originalRecords=len(old),currentRecords=len(new),changedRecords=len(changed),
                  unchangedRecords=len(set(old)&set(new))-len(changed),
                  addedOverrides=[f'{f:08X}' for f in sorted(added)],
                  removedPrivateEffects=[f'{f:08X}' for f in sorted(REMOVED)],
                  miraakActorVariants=len(miraak_actors),checks=checks,
                  pluginSHA256=hashlib.sha256(plugin.read_bytes()).hexdigest())
    (ROOT/'reports/migration-1.2.3.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='checks'},indent=2))
    print('Passed',len(checks),'independent audit assertions')


if __name__ == '__main__':
    main()
