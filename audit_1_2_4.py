"""Audit the 1.2.4 change against a local released 1.2.3 ESP and install ZIP.

Checks serialized data and retained resources, not Skyrim's event timing.
"""
from pathlib import Path
import argparse, hashlib, json, zipfile
from verify import ROOT, PKG, read_records, field, fields, vmad, U32


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',required=True,type=Path)
    parser.add_argument('--baseline-zip',required=True,type=Path)
    args=parser.parse_args()
    checks=[]
    def check(ok,message):
        assert ok,message
        checks.append(message)
    old_bytes=args.baseline.read_bytes()
    check(hashlib.sha256(old_bytes).hexdigest()=='7829b04bf7ab47e03bd56fe33db51f67378dfff3473e96e7600bad84d51f7445','Baseline is the released 1.2.3 plugin')
    old_records=read_records(args.baseline)
    plugin=PKG/'Ysmir - Shout System Overhaul.esp'
    records=read_records(plugin)
    old={r['fid']:r for r in old_records[1:]}
    new={r['fid']:r for r in records[1:]}
    check(old.keys()==new.keys() and len(new)==355,'All 355 record identities remain unchanged; no new or removed records')
    quest=next(r for r in new.values() if r['edid']=='YSMR_GeneralControllerQuest')
    unchanged=[]
    for fid,r in old.items():
        current=new[fid]
        if fid==quest['fid']:
            check(not fields(r,'VMAD') and len(fields(current,'VMAD'))==1,'Existing controller quest gains exactly one VMAD subrecord')
            stripped={**current,'fields':[(k,v) for k,v in current['fields'] if k!='VMAD']}
            check(stripped==r,'Quest flags, existing player alias, progression ability and all other quest data are identical')
        else:
            check(current==r,'Unchanged record: '+r['edid'])
            unchanged.append(f'{fid:08X}')
    aliases=[];vmad(quest,aliases)
    check(aliases==[dict(quest=quest['fid'],alias=0,scripts=[('YSMR_ShoutCooldown',[])])],'The new script binds to this quest and its existing alias zero, without properties')
    check(U32(field(quest,'ALFR'))==0x14,'The listener alias is forced to the player')
    strip=lambda r:[(k,v) for k,v in r['fields'] if k!='SNAM']
    check(strip(old_records[0])==strip(records[0]),'Plugin header changes only its release description')
    check(b'1.2.4' in field(records[0],'SNAM'),'Plugin description declares version 1.2.4')
    with zipfile.ZipFile(args.baseline_zip) as archive:
        check(archive.testzip() is None,'Baseline installer passes ZIP integrity checks')
        check(archive.read(plugin.name)==old_bytes,'Baseline installer contains the verified 1.2.3 plugin')
        old_scripts={n for n in archive.namelist() if n.lower().endswith('.pex')}
        current_scripts={p.relative_to(PKG).as_posix() for p in (PKG/'Scripts').glob('*.pex')}
        check(len(old_scripts)==11 and current_scripts==old_scripts|{'Scripts/YSMR_ShoutCooldown.pex'},'Exactly one runtime script added; all eleven existing scripts retained')
        assets=[a['path'] for a in json.loads((ROOT/'reports/assets.json').read_text())]
        retained=assets+[p.relative_to(PKG).as_posix() for p in (PKG/'SEQ').glob('*.seq')]
        check(len(retained)==22,'Twenty-one assets and the existing SEQ are compared')
        for name in retained:
            check(archive.read(name)==(PKG/name).read_bytes(),'Byte-identical retained file: '+name)
    result=dict(status='PASS',verification='Offline only; no in-game execution',baseline='1.2.3',version='1.2.4',records=len(new),unchangedRecords=len(unchanged),changedRecords=[dict(id=f"{quest['fid']:08X}",editorID=quest['edid'],change='Adds VMAD binding on the existing player alias')],addedRecords=[],removedRecords=[],newScripts=['YSMR_ShoutCooldown'],checks=checks,pluginSHA256=hashlib.sha256(plugin.read_bytes()).hexdigest())
    (ROOT/'reports/migration-1.2.4.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='checks'},indent=2))
    print('Passed',len(checks),'migration assertions')


if __name__=='__main__':main()
