"""Allowlisted binary migration audit against the released 1.1.4 archives."""
from pathlib import Path
import argparse, hashlib, json, re, struct, zipfile
from verify import ROOT, PKG, REPORT, read_records, entries, field, vmad

def resource(data):
    return re.sub(b'forceful tongue', lambda _: b'ysmir\\resources', data, flags=re.I)

def main():
    name='Ysmir - Shout System Overhaul'
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runtime',nargs='?',type=Path,default=ROOT.parent/'outputs'/f'{name} v1.1.4.zip')
    parser.add_argument('source',nargs='?',type=Path,default=ROOT.parent/'outputs'/f'{name} v1.1.4 - Source.zip')
    args=parser.parse_args()
    baseline=args.runtime;baseline_source=args.source
    report={'status':'PASS','verification':'Offline data comparison only; Skyrim not executed',
            'baselineArchiveSHA256':hashlib.sha256(baseline.read_bytes()).hexdigest()}
    with zipfile.ZipFile(baseline) as z:
        saved=ROOT/'baselines'/'1.1.4.esp';saved.parent.mkdir(exist_ok=True)
        saved.write_bytes(z.read(name+'.esp'))
        old={r['fid']:r for r in read_records(saved)[1:]}
        new={r['fid']:r for r in read_records(PKG/(name+'.esp'))[1:]}
        assert set(new)-set(old)=={0x4000861} and not set(old)-set(new)
        changes=[];renamed=0
        for fid,r in old.items():
            n=new[fid];expected=[]
            assert (r['tag'],r['flags'])==(n['tag'],n['flags'])
            if r['edid'].startswith('FT_'):renamed+=1
            for k,v in r['fields']:
                if k=='EDID' and v.startswith(b'FT_'):v=b'YSMR_'+v[3:]
                if k=='MODL':v=resource(v)
                if k=='VMAD':
                    a=b'FT_DLC2VoiceDragonAspectSummon';b=b'YSMR_DragonAspectSummon'
                    v=v.replace(struct.pack('<H',len(a))+a,struct.pack('<H',len(b))+b)
                if fid in (0x2007CB5,0x2015714,0x201AA33,0x2007CB3) and k=='DATA':
                    v=v[:72]+struct.pack('<I',0x4000861)+v[76:]
                expected.append((k,v))
            if fid==0x4000824:
                assert entries(n)[:3]==entries(r) and len(entries(n))==4
                # Existing entries and their record fields are an exact prefix.
                assert n['fields'][:len(expected)]==expected
            else:
                assert n['fields']==expected,(hex(fid),r['edid'],[(k,v.hex()) for k,v in n['fields'] if (k,v) not in expected])
            if n['fields']!=r['fields']:changes.append({'id':f'{fid:08X}','type':r['tag'],'editorID':n['edid']})
        assets=[]
        for oldpath in z.namelist():
            if not oldpath.lower().endswith(('.nif','.dds')):continue
            path=resource(oldpath.encode()).decode().replace('\\','/')
            raw=z.read(oldpath);actual=(PKG/path).read_bytes()
            expected=resource(raw) if path.lower().endswith('.nif') else raw
            assert actual==expected,(oldpath,path)
            assert len(actual)==len(raw)
            assets.append({'path':path,'bytes':len(raw),'changedTextureNamespace':raw!=actual})
        assert len(assets)==21
        # Every renamed embedded NIF texture path resolves to a shipped file.
        for a in assets:
            if not a['path'].endswith('.nif'):continue
            raw=(PKG/a['path']).read_bytes()
            for match in re.finditer(rb'textures[\\/]ysmir[\\/]resources[\\/][A-Za-z0-9_. -]+\.dds',raw,re.I):
                assert (PKG/match.group().decode().replace('\\','/')).is_file(),match.group()
    with zipfile.ZipFile(baseline_source) as z:
        oldscripts=[n for n in z.namelist() if n.endswith('.psc')]
        assert len(oldscripts)==11
        for path in oldscripts:
            oldtext=z.read(path).decode('utf-8-sig').replace('\r\n','\n')
            newtext=(ROOT/path).read_text(encoding='utf-8-sig')
            assert newtext==oldtext.replace('FT_DLC2VoiceDragonAspectSummon','YSMR_DragonAspectSummon'),path
    report.update(existingRecordsPreserved=len(old),addedRecords=1,renamedEditorIDs=renamed,
                  existingFormIDsUnchanged=True,unexpectedRecordChanges=0,changedRecords=changes,
                  assets=assets,scriptsBefore=11,scriptsAfter=11,newRuntimeScripts=0,
                  projectileChanges=['EditorID','Type: Missile -> Cone'],
                  commandedActorLimit={'entryPoint':68,'function':'Add','value':100,'spell':'Dawnguard.esm:007CBA'},
                  pluginSHA256=hashlib.sha256((PKG/(name+'.esp')).read_bytes()).hexdigest())
    (REPORT/'migration-1.2.0.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('changedRecords','assets')},indent=2))

if __name__=='__main__':main()
