"""Read the shipped ESP/PEX/SEQ independently of the Mutagen builder.

Offline structural/behavioral-data validation; this does not run Skyrim.
"""
from pathlib import Path
import struct, zlib, json, hashlib, re, math

ROOT = Path(__file__).resolve().parent
PKG = ROOT / 'package'
REPORT = ROOT / 'reports'
U32 = lambda b, p=0: struct.unpack_from('<I', b, p)[0]
U16 = lambda b, p=0: struct.unpack_from('<H', b, p)[0]
F32 = lambda b, p=0: struct.unpack_from('<f', b, p)[0]

def subrecords(b):
    p, extra = 0, None
    while p < len(b):
        assert p + 6 <= len(b)
        tag, n = b[p:p+4].decode('ascii'), U16(b, p+4)
        p += 6
        if tag == 'XXXX':
            assert n == 4
            extra = U32(b, p); p += 4
            continue
        if extra is not None:
            n, extra = extra, None
        assert p+n <= len(b)
        yield tag, b[p:p+n]
        p += n
    assert p == len(b)

def read_records(path):
    raw = path.read_bytes()
    def walk(start, end):
        p = start
        while p < end:
            assert p+24 <= end
            tag, n = raw[p:p+4].decode('ascii'), U32(raw, p+4)
            if tag == 'GRUP':
                assert n >= 24 and p+n <= end
                yield from walk(p+24, p+n)
                p += n
                continue
            assert p+24+n <= end
            flags, fid = U32(raw, p+8), U32(raw, p+12)
            b = raw[p+24:p+24+n]
            if flags & 0x40000:
                expected = U32(b); b = zlib.decompress(b[4:])
                assert len(b) == expected
            fields = list(subrecords(b))
            edid = next((v.rstrip(b'\0').decode('utf-8') for k,v in fields if k == 'EDID'), '')
            yield dict(tag=tag, fid=fid, flags=flags, fields=fields, edid=edid)
            p += 24+n
        assert p == end
    return list(walk(0,len(raw)))

def fields(r, tag): return [v for k,v in r['fields'] if k == tag]
def field(r, tag): return fields(r,tag)[0]
def effects(r):
    result = []
    for k,v in r['fields']:
        if k == 'EFID': result.append(dict(id=U32(v),conditions=[]))
        elif k == 'EFIT': result[-1].update(mag=F32(v),area=U32(v,4),duration=U32(v,8))
        elif k == 'CTDA' and result: result[-1]['conditions'].append(v)
    return result

def vmad(r, alias_info=None):
    if not fields(r,'VMAD'): return []
    b = field(r,'VMAD'); p = 0
    version, fmt, count = struct.unpack_from('<HHH',b); p=6
    assert fmt in (1,2) and version in (4,5)
    def string():
        nonlocal p
        n=U16(b,p);p+=2;s=b[p:p+n].decode('utf-8');p+=n;return s
    def value(t):
        nonlocal p
        if t==1:
            fid=U32(b,p+(4 if fmt==2 else 0));p+=8;return fid
        if t==2: return string()
        if t in (3,4):
            v=U32(b,p) if t==3 else F32(b,p);p+=4;return v
        if t==5: v=b[p];p+=1;return v
        if 11<=t<=15:
            n=U32(b,p);p+=4;return [value(t-10) for _ in range(n)]
        raise AssertionError(('Unknown VMAD property',t))
    def scripts(n):
        nonlocal p
        result=[]
        for _ in range(n):
            name=string();status=b[p];p+=1;size=U16(b,p);p+=2;props=[]
            for _ in range(size):
                key=string();t,status=b[p:p+2];p+=2;props.append((key,t,value(t)))
            result.append((name,props))
        return result
    result=scripts(count)
    if r['tag']=='QUST' and p<len(b):
        assert b[p]==2; p+=1
        fragments=U16(b,p);p+=2;string()
        for _ in range(fragments):p+=9;string();string()
        aliases=U16(b,p);p+=2
        for _ in range(aliases):
            # Alias object is serialized before its own version/format header.
            alias_format=U16(b,p+10)
            assert alias_format==fmt==2
            unused,alias,fid=struct.unpack_from('<HhI',b,p);p+=8
            version,fmt,n=struct.unpack_from('<HHH',b,p);p+=6
            assert unused==0 and version==5
            bound=scripts(n);result.extend(bound)
            if alias_info is not None:alias_info.append(dict(quest=fid,alias=alias,scripts=bound))
        assert p==len(b)
    assert p<=len(b)
    return result

def entries(r):
    result=[];tab=None
    for k,v in r['fields']:
        if k=='PRKE': result.append(dict(type=v[0],priority=v[2],conditions=[]))
        elif k=='DATA' and result: result[-1]['entry'],result[-1]['function'],result[-1]['tabs']=v
        elif k=='PRKC': tab=v[0]
        elif k=='CTDA' and result: result[-1]['conditions'].append((tab,v))
        elif k=='EPFD': result[-1]['values']=list(struct.unpack('<'+'f'*(len(v)//4),v))
    return result

def main():
    checks=[]
    def check(ok, message):
        assert ok, message
        checks.append(message)
    path=PKG/'Ysmir - Shout System Overhaul.esp'
    records=read_records(path); header=records[0]; rs=records[1:]
    byid={r['fid']:r for r in rs}; named={r['edid']:r for r in rs}
    check(len(byid)==len(rs),'All records unique; group/subrecord boundaries valid')
    masters=[v.rstrip(b'\0').decode() for v in fields(header,'MAST')]
    check(masters==['Skyrim.esm','Update.esm','Dawnguard.esm','Dragonborn.esm'],'Only four official masters')
    check(bool(header['flags']&0x200),'ESL flag set')
    own=len(masters)
    check(all(0x800<=(r['fid']&0xFFFFFF)<=0xFFF for r in rs if r['fid']>>24==own),'All new IDs fit legacy ESL range 800-FFF')
    stable=json.loads((ROOT/'inputs'/'stable-formids.json').read_text())
    check(all((r['fid']&0xFFFFFF)==stable[r['edid']] for r in rs if r['fid']>>24==own),'Released FormIDs remain stable and are independent of build order')
    check(not any(r['flags']&0x20 for r in rs),'No deleted records')
    check(not any(re.search('phantom|wordwallbroken',r['edid'],re.I) for r in rs),'No Phantom Form records')
    miraak_baseline=json.loads((ROOT/'inputs'/'miraak-shouts.json').read_text())
    miraak_ids={int(r['id'],16) for r in miraak_baseline}
    all_shouts=[r for r in rs if r['tag']=='SHOU']
    npc_shouts=[r for r in all_shouts if r['fid'] in miraak_ids]
    shouts=[r for r in all_shouts if r['fid'] not in miraak_ids]
    check(not npc_shouts and len(all_shouts)==27,'Only player shout records are overridden; no Miraak cooldown overrides')
    check(not any('miraak' in r['edid'].lower() for r in all_shouts),'No Miraak-specific shout records are included')
    for original in miraak_baseline:
        fid=int(original['id'],16)
        check(fid not in byid,'Miraak/Greybeard shout inherits the original record: '+original['editorID'])
    check(len(shouts)==27,'27 vanilla/DLC player shouts')
    check(all(len(fields(r,'SNAM'))==3 and all(F32(v,8)==5 for v in fields(r,'SNAM')) for r in shouts),'All 81 shout slots use 5-second recovery')
    check(all(r['flags']==0 for r in shouts),'No shout treated as a daily power')
    for r in shouts:
        for v in fields(r,'SNAM'):
            s=byid[U32(v,4)]
            check(U32(field(s,'SPIT'),8)==11,'Voice spell type: '+s['edid'])
    for r in rs:
        for v in fields(r,'CTDA'):
            check(U16(v,8)<1024,'Native condition function: '+r['edid'])
    perk=named['YSMR_DragonSoulPower']; pe=entries(perk)
    check(len(pe)==4,'Four native perk entries: magnitude, duration, player slowdown, Soul Tear commanded-actor limit')
    check([(p['entry'],p['function']) for p in pe[:2]]==[(29,14),(30,14)],'General growth multiplies magnitude/duration by 1 + AV * coefficient')
    check(all(p['values'][0]==133 and math.isclose(p['values'][1],.025,abs_tol=1e-8) for p in pe[:2]),'Both general growth entries use DragonSouls and 2.5%')
    for p in pe[:2]:
        cs=p['conditions']
        check(len(cs)==2 and all(tab==1 for tab,_ in cs),'Growth conditions evaluate the native spell tab')
        check(U16(cs[0][1],8)==560 and U32(cs[0][1],12)==0x46B99 and F32(cs[0][1],4)==1,'Growth eligibility is HasKeyword MagicShout, including unknown third-party spell IDs')
        check(U16(cs[1][1],8)==372 and F32(cs[1][1],4)==0,'Only explicit fixed-parameter exclusions, no spell allowlist')
    fixed_mag={U32(v) for v in fields(named['YSMR_FixedMagnitudeSpells'],'LNAM')}
    fixed_dur={U32(v) for v in fields(named['YSMR_FixedDurationSpells'],'LNAM')}
    check(all(U16(v,8) not in (27,58,59) for _,v in sum((p['conditions'] for p in pe),[])),'No player-level or quest-stage gate in progression')
    check(pe[2]['entry']==70 and pe[2]['function']==1 and pe[2]['values']==[1.0],'Native player magic slowdown set to 1.0')
    limit=pe[3]
    check(limit['entry']==68 and limit['function']==2 and limit['values']==[100.0] and limit['tabs']==2,'Native Ritual Stone pattern adds exactly 100 commanded-actor slots')
    cs=limit['conditions']
    check(len(cs)==1 and cs[0][0]==1 and U16(cs[0][1],8)==72 and U32(cs[0][1],12)==0x2007CBA and F32(cs[0][1],4)==1 and cs[0][1][0]==0,'Extra slots are scoped by GetIsID on the spell tab to Soul Tear secondary reanimation only')
    simulation=[]
    for i,(fid,base,duration) in enumerate(((0x48AD0,.3,8),(0x48AD1,.2,12),(0x48AD2,.1,16))):
        e=effects(byid[fid]);check(len(e)==1 and e[0]['id']==0x48ACD and math.isclose(e[0]['mag'],base,abs_tol=1e-7) and e[0]['duration']==duration,'Slow Time rank '+str(i+1)+' retains vanilla base')
        check(fid in fixed_mag and fid not in fixed_dur,'Slow Time rank '+str(i+1)+' excludes strength growth and includes duration growth')
        for souls in (0,4,40,100):simulation.append(dict(rank=i+1,souls=souls,worldSpeed=e[0]['mag'],duration=duration*(1+.025*souls)))
    check(not any(p['entry']==29 and p['function']!=14 for p in pe),'Removed all six slowdown-strength and cap rules')
    for fid in (0x1861F,0x18627,0x1862B,0x9CAF0,0x9CAF1,0x9CAF2,0x2008A61,0x2008A5F,0x2008A5E):
        check(fid not in fixed_mag and fid not in fixed_dur,'Damage/debuff/absorption and duration both grow: '+byid[fid]['edid'])
    collision=named['YSMR_SprintCollisionSpell']
    check(collision['fid'] in fixed_mag and collision['fid'] in fixed_dur and effects(collision)[0]['mag']==9 and effects(collision)[0]['duration']==1,'Sprint collision geometry and lifetime stay fixed in a separate helper')
    for fid in (0x2F7BE,0x2F7BF,0x2F7C0):
        check(fid in fixed_mag and fid not in fixed_dur,'Sprint movement duration still grows; dash and speed values stay fixed')
        movement=byid[effects(byid[fid])[-1]['id']]
        check(any(n.lower()=='magiccastanotherspellscript' and any(v==collision['fid'] for _,t,v in props if t==1) for n,props in vmad(movement)),'Sprint uses the original native helper-cast script')
    projectile=named['YSMR_SoulTearPiercing']
    original=json.loads((ROOT/'inputs'/'soul-tear-projectile.json').read_text())
    expected=[]
    for k,h in original['fields']:
        v=bytes.fromhex(h)
        if k=='EDID':v=b'YSMR_SoulTearPiercing\0'
        if k=='DATA':v=v[:2]+struct.pack('<H',16)+v[4:]
        expected.append((k,v))
    check(projectile['tag']=='PROJ' and projectile['fields']==expected,'Piercing projectile matches original byte for byte except EditorID and native Cone type')
    data=field(projectile,'DATA')
    check(F32(data,8)==1066 and F32(data,12)==4000 and F32(data,68)==0 and F32(data,72)==24,'Soul Tear retains original speed, range, zero spread and collision radius')
    check(0x2007CBB not in byid and 0x200D2E1 not in byid and 0x200BFF8 not in byid,'Shared projectile and Dawnguard quest effects remain untouched')
    for fid in (0x2007CB5,0x2015714,0x201AA33,0x2007CB3):
        check(U32(field(byid[fid],'DATA'),72)==projectile['fid'],'Every Soul Tear damage/soul-trap effect uses the same piercing projectile: '+byid[fid]['edid'])
    for fid,damage,trap in ((0x2015716,30,10),(0x2015715,60,20),(0x20038BD,300,2)):
        es=effects(byid[fid])
        check(es[0]['mag']==damage and any(e['id']==0x2007CB3 and e['duration']==trap for e in es),'Soul Tear rank keeps its complete damage and soul-trap payload: '+byid[fid]['edid'])
    wait=vmad(byid[0x2016874])
    check(len(wait)==1 and wait[0][0].lower()=='dlc1magicsoulrendscript' and any(k=='SpellRef' and v==0x2007CBA for k,t,v in wait[0][1]),'Original per-target Soul Rend death script casts the spell matched by the limit perk')
    check(all(e['duration']==60 for e in effects(byid[0x2007CBA])) and effects(byid[0x2007CBA])[0]['mag']==99,'Reanimation keeps base duration 60 and level parameter 99')
    for fid in (0x20E16,0x562EA,0x562EB):
        bound=vmad(byid[fid])
        check(len(bound)==1 and bound[0][0].lower()=='dlc2voicefirebreathscript','Fire Breath uses original fire-wyrm script: '+byid[fid]['edid'])
        props={n:v for n,t,v in bound[0][1] if t==1}
        check(props=={'Wyrm':0x301F990,'DLC2BlackBookReward3':0x3020E99},'Native fire-wyrm properties preserve FT actor and original reward global, without obsolete PlayerRef')
    push=named['YSMR_VoiceUnrelentingForceEffect03']
    check(any(n=='YSMR_VoicePush' and any(k=='PushForce' and v==15 for k,t,v in props) for n,props in vmad(push)),'Unrelenting Force uses soul-scaled push with base force 15')
    # Reconstruct actual shout graph from raw EFID, archetype association,
    # equip-ability links and VMAD form properties.
    roots={U32(v,4) for r in shouts for v in fields(r,'SNAM')}
    seen=set(); todo=list(roots); tagged=set(); helpers=set()
    while todo:
        fid=todo.pop()
        if fid in seen:continue
        seen.add(fid);r=byid.get(fid)
        if not r or r['tag'] not in ('SPEL','ENCH'):continue
        for e in effects(r):
            m=byid[e['id']];kw=set(struct.unpack('<'+'I'*(len(field(m,'KWDA'))//4),field(m,'KWDA'))) if fields(m,'KWDA') else set()
            flags=U32(field(m,'DATA'))
            if m['edid'].startswith('YSMR_DispelSelector'):
                assert flags&0x100 and 0x46B99 not in kw;helpers.add(m['fid'])
            else:
                assert 0x46B99 in kw and not flags&0x100,(r['edid'],m['edid']);tagged.add(m['fid'])
            links=[U32(field(m,'DATA'),8)]
            # VMAD captures cloak/secondary spell casts in this build.
            for name,props in vmad(m):
                links += [v for _,t,v in props if t==1]
            links += [U32(v) for v in fields(m,'ESCE') if len(v)==4]
            for link in links:
                if link in byid and byid[link]['tag'] in ('SPEL','ENCH'):todo.append(link)
    check(bool(tagged) and bool(helpers),'All reachable action effects have MagicShout; dispel selectors exclude it')
    check(not any(U32(field(r,'DATA'))&0x100 and fields(r,'KWDA') and 0x46B99 in struct.unpack('<'+'I'*(len(field(r,'KWDA'))//4),field(r,'KWDA')) for r in rs if r['tag']=='MGEF'),'No effect can dispel every MagicShout effect')
    shared_names={0xA44C0:'StaggerPushFFAimed',0xEA075:'UnspecifiedDamageFFAimed',0xEA076:'FrostDamageFFAimedSilent',0xE4CB6:'ShockDamageBoltStormAimed'}
    for fid,name in shared_names.items():
        check(fid not in byid,'Shared non-shout effect remains untouched: '+name)
        private=named['YSMR_Shout_'+name]
        check(private['fid']>>24==own and 0x46B99 in struct.unpack('<'+'I'*(len(field(private,'KWDA'))//4),field(private,'KWDA')),'Shout-only replacement is tagged: '+name)
    check(not any(e['id'] in shared_names for r in rs if r['tag'] in ('SPEL','ENCH') for e in effects(r)),'All shipped spell/enchantment uses point to isolated shared-effect replacements')
    # Critical values whose identity/layout FT changes.
    for fid,mag in ((0x13E09,2),(0x13F39,5),(0x13F3A,10)):
        check(next(e['mag'] for e in effects(byid[fid]) if e['id']==named['YSMR_Shout_UnspecifiedDamageFFAimed']['fid'])==mag,'Unrelenting Force base damage restored')
    for fid,dur in ((0x44254,15),(0x28315,18),(0x28345,22)):
        check(effects(byid[fid])[0]['duration']==dur,'Dragonrend duration restored')
    battle=next(r for r in shouts if 'BattleFury' in r['edid'])
    for i,v in enumerate(fields(battle,'SNAM')):
        s=byid[U32(v,4)];e=effects(s)[0]
        check(math.isclose(e['mag'],[1.3,1.5,1.7][i],abs_tol=1e-6) and e['area']==25 and e['duration']==60*(i+1),'Battle Fury rank '+str(i+1)+' base restored')
        prop=next(v for _,props in vmad(byid[e['id']]) for n,t,v in props if n=='DLC2VoiceElementalFury')
        check(all(e['duration']==60*(i+1) for e in effects(byid[prop])),'Battle Fury secondary duration follows rank')
    arm=entries(byid[0x301DF8F]);check(next(p['values'][0] for p in arm if p['entry']==35)==1.25,'Dragon Aspect power-attack bonus is vanilla 25%')
    ancient=byid[0x30265AA]
    ancient_timer=next(v for _,props in vmad(ancient) for n,t,v in props if n.lower()=='timer')
    check(effects(byid[0x3031855])[0]['duration']==ancient_timer==20,'Ancient Dragonborn summon duration matches original actor timer, not a zero-duration script placeholder')
    heroes=[byid[x] for x in (0x44236,0x44237,0x44238,0x923F9,0x923FA,0x923FB,0xCD9F7,0xCDA00,0xCDA01)]
    check(all(U16(field(r,'ACBS'),8)==150 and not U32(field(r,'ACBS'))&0x80 for r in heroes),'All nine hero variants fixed at level 150')
    check(all(not U16(field(r,'ACBS'),18)&2 or U16(field(byid[U32(field(r,'TPLT'))],'ACBS'),8)==150 for r in heroes),'Hero stat templates also have level 150')
    for hero in heroes:
        acbs=field(hero,'ACBS')
        check(U32(acbs)&0x10 and all(U16(acbs,p)==0 for p in (4,6,20)),'Hero uses native auto-calculated stats with zero Health/Magicka/Stamina offsets: '+hero['edid'])
        check(all(U16(acbs,p)==150 for p in (8,10,12)),'Hero level and both bounds stay 150 independently of souls: '+hero['edid'])
        check(not any(U32(v)==perk['fid'] for v in fields(hero,'PRKR')) and not any(U32(v)==named['YSMR_GeneralControllerAbility']['fid'] for v in fields(hero,'SPLO')),'Hero does not inherit the player soul-growth perk or ability: '+hero['edid'])
        for v in fields(hero,'SPLO'):
            learned=byid.get(U32(v))
            if learned and learned['tag']=='SHOU':check(all(F32(w,8)==5 for w in fields(learned,'SNAM')),'Hero learned shout has five-second cooldowns: '+hero['edid']+' / '+learned['edid'])
    for fid,eid,area,duration in ((0x9E0CC,0x9E0C8,75,30),(0x9E0CD,0x9E0CA,150,45),(0x9E0CE,0x9E0CB,250,60)):
        es=effects(byid[fid])
        check(len(es)==1 and es[0]['id']==eid and es[0]['mag']==20 and es[0]['area']==area and es[0]['duration']==duration,'Animal Allegiance retains only its native allegiance payload: '+byid[fid]['edid'])
        check(U32(field(byid[eid],'DATA'),64)==38 and any(n=='VoiceCharmFactionScript' for n,_ in vmad(byid[eid])),'Animal Allegiance retains native rally archetype and faction script: '+byid[eid]['edid'])
        check(b'extra Health' not in field(byid[eid],'DNAM'),'Animal Allegiance description no longer advertises attribute buffs')
    check(not any('AnimalAllegianceFortify' in r['edid'] for r in rs) and 0x04000809 not in byid and 0x0400080A not in byid,'Unused animal Health/Stamina effects are omitted, and their released IDs stay reserved')
    storm=next(r for r in shouts if 'StormCall' in r['edid'])
    check(all(effects(byid[U32(v,4)])[0]['area']==600 for v in fields(storm,'SNAM')),'Storm Call all ranks use area 600')
    for fid,bolt in ((0xA1A58,0xE4CB7),(0xE3F0A,0xE4CB7),(0xA1A5C,0xE98A2),(0xE3F09,0xE98A2),(0xA1A5B,0xE98A3),(0xD5E81,0xE98A3)):
        m=byid[fid];cs=fields(m,'CTDA')
        check(len(cs)==1 and U16(cs[0],8)==300 and F32(cs[0],4)==0 and U32(cs[0],20)==1,'Storm retains its original exterior condition without extra friend filters: '+m['edid'])
        scripts=vmad(m)
        emitter=[p for n,p in scripts if n.lower()=='magicprojectilestormscript']
        check(len(emitter)==1 and any(n=='SpellRef' and t==1 and v==bolt for n,t,v in emitter[0]),'Original storm emitter casts the matching rank of lightning: '+m['edid'])
        check(not any(n.lower()=='ysmr_storm' or any(k=='CompanionsInsight' for k,_,_ in p) for n,p in scripts),'No custom storm script or Insight property: '+m['edid'])
    storm_damage=named['YSMR_Shout_ShockDamageBoltStormAimed']
    check(not fields(storm_damage,'CTDA'),'Storm damage uses native perk protection without duplicated target conditions')
    check(U32(field(storm_damage,'DATA'),72)==0xE4CB5 and 0xE4CB5 not in byid,'Original lightning projectile is referenced and not overridden')
    check(0x301E7F0 not in byid,'Original Companion Insight perk remains untouched')
    for fid in (0xE4CB7,0xE98A2,0xE98A3):
        es=effects(byid[fid])
        check(len(es)==1 and es[0]['id']==storm_damage['fid'] and not es[0]['conditions'],'Lightning uses only the MagicShout-tagged private damage effect: '+byid[fid]['edid'])
    quest=named['YSMR_GeneralControllerQuest']
    check(U16(field(quest,'DNAM'))&1 and U32(field(quest,'ALFR'))==0x14,'Native start-enabled player alias installs progression')
    alias_bindings=[];vmad(quest,alias_bindings)
    check(alias_bindings==[dict(quest=quest['fid'],alias=0,scripts=[('YSMR_ShoutCooldown',[])])],'Cooldown script binds only to the existing player alias, with no properties or new quest')
    check(len(fields(quest,'ALST'))==1 and U32(field(quest,'ALST'))==0,'Player alias ID and count stay unchanged')
    seq=next((PKG/'SEQ').glob('*.seq')).read_bytes()
    check(seq==struct.pack('<I',quest['fid']),'SEQ contains the correct raw quest FormID and no count header')
    script_sources={p.stem.lower():p.read_text(encoding='utf-8-sig') for p in (PKG/'Source'/'Scripts').glob('*.psc')}
    scripts=list((PKG/'Scripts').glob('*.pex'))
    check(len(scripts)==len(script_sources)==12,'Twelve source scripts and twelve compiled PEX files')
    check(not any(r['edid'].startswith('FT_') for r in rs),'All former FT EditorIDs use the YSMR namespace')
    check(not any('FT_' in k for r in rs for n,props in vmad(r) for k,t,v in props),'No FT script property remains in plugin bindings')
    check(not any('FT_' in s for s in script_sources.values()),'No FT identifier remains in shipped script sources')
    ancient=script_sources['ysmr_ancientdragonborn']
    check('Spell Property YSMR_DragonAspectSummon Auto' in ancient and any(n=='YSMR_AncientDragonborn' and any(k=='YSMR_DragonAspectSummon' for k,t,v in props) for r in rs for n,props in vmad(r)),'Renamed Ancient Dragonborn property matches its VMAD binding')
    stock=json.loads((ROOT/'inputs'/'stock-scripts.json').read_text())
    own_scripts={p.stem.lower() for p in scripts};stock_scripts=set(stock)
    check('ysmr_storm' not in own_scripts and 'magicprojectilestormscript' not in own_scripts and 'magicprojectilestormscript' in stock_scripts,'Storm script is supplied by the original game, not bundled or overridden')
    check('ysmr_firebreath' not in own_scripts and 'dlc2voicefirebreathscript' not in own_scripts and 'dlc2voicefirebreathscript' in stock_scripts,'Fire-wyrm script is supplied by Dragonborn, not bundled or overridden')
    bindings={n.lower() for r in rs for n,_ in vmad(r)}
    check(bindings<=own_scripts|stock_scripts,'Every bound script resolves to package or verified original-game inventory')
    for p in scripts:
        check(p.read_bytes()[:4]==bytes.fromhex('fa57c0de'),'Valid Skyrim PEX header: '+p.name)
    decompiled=ROOT/'verify-decompiled'
    cooldown=next(p for p in decompiled.glob('*.psc') if p.stem.lower()=='ysmr_shoutcooldown').read_text().lower()
    check('extends referencealias' in cooldown and re.findall(r'\bevent\s+(\w+)\(',cooldown)==['onspellcast'],'Compiled cooldown listener has exactly one native cast event on a reference alias')
    check('akspell as shout' in cooldown and 'getactorreference()' in cooldown,'Compiled cooldown listener filters Shout forms and obtains its alias actor')
    check('setvoicerecoverytime(' in cooldown and 'getactorvalue("shoutrecoverymult")' in cooldown and re.search(r'5\.0+\s*\*',cooldown),'Compiled cooldown uses five seconds times the current native recovery multiplier')
    check(not re.search(r'\b(while|register\w*|onupdate\w*|wait\w*|skse|game|getform\w*|setactorvalue|forceactorvalue|modactorvalue)\b',cooldown),'Cooldown contains no polling, waits, SKSE, form lookup or actor-value mutation')
    check('magicshout' not in cooldown and 'getnth' not in cooldown,'Cooldown accepts unknown Shout forms without a keyword, rank filter or SKSE shout API')
    cooldown_samples=[dict(multiplier=m,seconds=5*m) for m in (1,.8,.6,0)]
    for name in ('wordwalltriggerscript','dlc1wordwalltriggerscript','dlc2wordwalltriggerscript','dbsanctuarywordwalltriggerscript'):
        p=next(p for p in decompiled.glob('*.psc') if p.stem.lower()==name);s=p.read_text().lower()
        check(s.count('game.unlockword(')==1 and s.index('game.unlockword(')>s.index('game.teachword('),'Compiled free word unlock: '+name)
        check('modactorvalue("dragonsouls"' not in s,'Word unlock never debits/refunds souls: '+name)
    for name,needles in {'YSMR_ElementalFury':['registerforanimationevent','onanimationevent','getcurrentrealtime','lastclock','fire.cast','frost.cast','shock.cast'], 'YSMR_VoicePush':['pushactoraway','getactorvalue("dragonsouls")','0.025','game.getplayer()']}.items():
        p=next(p for p in decompiled.glob('*.psc') if p.stem.lower()==name.lower());s=p.read_text().lower()
        check(all(x in s for x in needles),'Compiled behavior present: '+name)
    assets=json.loads((REPORT/'assets.json').read_text())
    check(len(assets)==21 and sum(a['path'].endswith('.nif') for a in assets)==17 and sum(a['path'].endswith('.dds') for a in assets)==4,'All 17 retained models and four summon textures are packaged')
    for a in assets:
        check(hashlib.sha256((PKG/a['path']).read_bytes()).hexdigest()==a['sha256'],'Verified standalone asset: '+a['path'])
        check('/ysmir/' in a['path'].lower() and b'forceful tongue' not in (PKG/a['path']).read_bytes().lower(),'Asset and embedded texture paths use only the Ysmir namespace: '+a['path'])
    for r in rs:
        for model in fields(r,'MODL'):
            model_path=model.rstrip(b'\0').decode('utf-8').replace('\\','/')
            check('forceful tongue' not in model_path.lower(),'No old model namespace: '+r['edid'])
            if model_path.lower().startswith('ysmir/'):
                check((PKG/'meshes'/model_path).is_file(),'Custom Ysmir model reference resolves locally: '+r['edid'])
    asset_paths={a['path'].lower() for a in assets}
    check({'meshes/ysmir/inventory/fire.nif','meshes/ysmir/inventory/bendwill.nif'}<=asset_paths,'Fire and Bend Will inventory models use private Ysmir paths')
    check(not {'meshes/magic/maginvshoutfire.nif','meshes/dlc02/effects/maginvshoutbendwill.nif'}&asset_paths,'No loose-file replacements of shared vanilla inventory models')
    check(not any('phantom' in a['path'].lower() or 'powerwordsah' in a['path'].lower() for a in assets),'Removed Phantom Form models and word-wall mesh stay excluded')
    build=json.loads((REPORT/'build.json').read_text())
    check(not build['unresolved'] and build['sha256'].lower()==hashlib.sha256(path.read_bytes()).hexdigest(),'Builder link validation and checksum match this binary')
    result=dict(status='PASS',verification='Offline only; no in-game execution',records=len(rs),newRecords=sum(r['fid']>>24==own for r in rs),shouts=len(all_shouts),shoutSlots=sum(len(fields(r,'SNAM')) for r in all_shouts),playerShouts=len(shouts),miraakOverrides=len(npc_shouts),scripts=len(scripts),assets=len(assets),checkedActionEffects=len(tagged),housekeepingSelectors=len(helpers),checks=checks,slowTimeSamples=simulation,playerCooldownSamples=cooldown_samples,pluginSHA256=hashlib.sha256((PKG/'Ysmir - Shout System Overhaul.esp').read_bytes()).hexdigest())
    (REPORT/'verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k not in ('checks','slowTimeSamples')},indent=2))
    print('Passed',len(checks),'offline assertions')

if __name__=='__main__': main()
