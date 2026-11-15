"""Audit the removal of Miraak overrides against release 1.2.4 and official masters."""
from pathlib import Path
import argparse, hashlib, json, subprocess, zipfile
from verify import ROOT, PKG, read_records, field, fields, U32, F32
from audit_1_2_3 import official_records, MASTERS

BASELINE_COMMIT = '147daad6325541ec8695830dbf2682be5d3c3c41'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--baseline-zip', required=True, type=Path)
    parser.add_argument('--game-data', required=True, type=Path)
    args = parser.parse_args()
    checks = []

    def check(ok, message):
        assert ok, message
        checks.append(message)

    old_bytes = args.baseline.read_bytes()
    check(hashlib.sha256(old_bytes).hexdigest() == '90dfbfc0326ea399ba0760540e771a9f471ad8263c84280cf1e119254d31cfef',
          'Baseline is the released 1.2.4 plugin')
    old_records = read_records(args.baseline)
    plugin = PKG/'Ysmir - Shout System Overhaul.esp'
    records = read_records(plugin)
    old = {r['fid']: r for r in old_records[1:]}
    new = {r['fid']: r for r in records[1:]}
    expected = json.loads((ROOT/'inputs/miraak-shouts.json').read_text())
    removed = {int(r['id'], 16) for r in expected if r['overrideNeeded']}
    check(old.keys()-new.keys() == removed and len(removed) == 5, 'Exactly five official Miraak overrides removed')
    check(not new.keys()-old.keys() and len(new) == 350, 'No added records; 350 records remain')
    for fid, current in new.items():
        check(current == old[fid], 'Unchanged record: '+current['edid'])
    strip = lambda r: [(k, v) for k, v in r['fields'] if k not in ('SNAM', 'HEDR')]
    check(strip(old_records[0]) == strip(records[0]), 'Header masters, flags and author remain unchanged')
    old_h, new_h = field(old_records[0], 'HEDR'), field(records[0], 'HEDR')
    check(old_h[:4]+old_h[8:] == new_h[:4]+new_h[8:] and U32(old_h, 4)-U32(new_h, 4) == 5,
          'Header count decreases by five; version format and next FormID unchanged')
    check(b'1.2.5' in field(records[0], 'SNAM'), 'Plugin description declares version 1.2.5')

    official = {}
    for name in MASTERS:
        official.update({r['fid']: r for r in official_records(args.game_data/name)})
    for row in expected:
        fid = int(row['id'], 16)
        record = official[fid]
        check(fid not in new, 'No override for Miraak-used shout: '+row['editorID'])
        check(record['edid'] == row['editorID'] and [F32(v, 8) for v in fields(record, 'SNAM')] == row['cooldowns'],
              'Inherited cooldowns match official master and baseline: '+row['editorID'])
    check(not any('miraak' in r['edid'].lower() for r in new.values() if r['tag'] == 'SHOU'), 'No Miraak-specific shout records remain')
    check(len([r for r in new.values() if r['tag'] == 'SHOU']) == 27, 'Only the 27 player shout records remain')

    normalized = lambda b: b.replace(b'\r\n', b'\n')
    scripts = sorted((PKG/'Source/Scripts').glob('*.psc'))
    check(len(scripts) == 12, 'Twelve source scripts retained')
    for source in scripts:
        rel = source.relative_to(ROOT).as_posix()
        baseline = subprocess.check_output(['git', 'show', BASELINE_COMMIT+':'+rel], cwd=ROOT)
        check(normalized(source.read_bytes()) == normalized(baseline), 'Source unchanged since 1.2.4: '+source.name)
    stable = 'inputs/stable-formids.json'
    check(normalized((ROOT/stable).read_bytes()) == normalized(subprocess.check_output(['git', 'show', BASELINE_COMMIT+':'+stable], cwd=ROOT)),
          'Released FormID map unchanged')
    with zipfile.ZipFile(args.baseline_zip) as archive:
        check(archive.testzip() is None and archive.read(plugin.name) == old_bytes, 'Baseline install ZIP contains verified 1.2.4 plugin')
        old_scripts = {n for n in archive.namelist() if n.lower().endswith('.pex')}
        current_scripts = {p.relative_to(PKG).as_posix() for p in (PKG/'Scripts').glob('*.pex')}
        check(old_scripts == current_scripts and len(current_scripts) == 12, 'Runtime script inventory unchanged')
        assets = [a['path'] for a in json.loads((ROOT/'reports/assets.json').read_text())]
        retained = assets+[p.relative_to(PKG).as_posix() for p in (PKG/'SEQ').glob('*.seq')]
        check(len(retained) == 22, 'Twenty-one assets and the existing SEQ compared')
        for name in retained:
            check(archive.read(name) == (PKG/name).read_bytes(), 'Byte-identical retained file: '+name)
    result = dict(status='PASS', verification='Offline only; no in-game execution', baseline='1.2.4', version='1.2.5',
                  records=len(new), unchangedRecords=len(new), changedRecords=[], addedRecords=[],
                  removedRecords=[r for r in expected if r['overrideNeeded']], scriptsUnchanged=12, checks=checks,
                  pluginSHA256=hashlib.sha256(plugin.read_bytes()).hexdigest())
    (ROOT/'reports/migration-1.2.5.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if k != 'checks'}, indent=2))
    print('Passed', len(checks), 'migration assertions')


if __name__ == '__main__':
    main()
