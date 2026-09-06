"""Verify the shipped DLL metadata and hooks against an actual Steam 1.7.104 EXE.

Usage: python verify_latest_native.py SkyrimSE.exe Ysmir.dll report.json [versionlib-1-7-104-0.bin]
Requires pefile==2024.8.26 and capstone==5.0.6. Does not launch or modify the game.
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys
import capstone
import pefile

exe_path, dll_path, report_path = map(Path, sys.argv[1:4])
src = Path(__file__).resolve().parents[2] / 'src'
header = (src / 'RuntimeCompatibility.h').read_text('utf-8')
values = {k: int(v, 16) for k, v in re.findall(r'(\w+) = (0x[\dA-F]+);', header)}
exe, dll = pefile.PE(exe_path), pefile.PE(dll_path)
raw = exe_path.read_bytes()
base = exe.OPTIONAL_HEADER.ImageBase
assert exe.FILE_HEADER.Machine == dll.FILE_HEADER.Machine == 0x8664
version = exe.VS_FIXEDFILEINFO[0]
assert (version.FileVersionMS >> 16, version.FileVersionMS & 65535,
        version.FileVersionLS >> 16, version.FileVersionLS & 65535) == (1, 7, 104, 0)
exports = {e.name.decode(): e.address for e in dll.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
assert {'SKSEPlugin_Load', 'SKSEPlugin_Version'} <= exports.keys()
metadata = dll.get_data(exports['SKSEPlugin_Version'], 0x350)
u32 = lambda off: struct.unpack_from('<I', metadata, off)[0]
pack = lambda a,b,c,d: a << 24 | b << 16 | c << 4 | d
assert u32(0) == 1 and u32(4) == pack(0, 4, 0, 0)
assert metadata[8:264].split(b'\0')[0] == b'Ysmir'
assert u32(0x304) == 2  # Supports Address Library format 5; no false NoStructUse claim.
assert u32(0x308) == 5  # Uses Address Library and updated post-629 structures.
assert u32(0x30C) == pack(1, 7, 104, 0) and u32(0x310) == 0
assert u32(0x34C) == pack(2, 3, 1, 0)

cs = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
main = values['MainUpdateRva']
instructions = {i.address-base: i for i in cs.disasm(exe.get_data(main, 0xC6F), base+main)}
site = main + values['FrameCallOffset']
call = instructions[site]  # Must be a full instruction boundary, not an E8 inside operands.
expected_bytes = bytes(int(x,16) for x in re.findall(r'0x[\dA-F]+', header.split('FrameCallBytes{')[1].split('}')[0]))
assert call.mnemonic == 'call' and bytes(call.bytes) == expected_bytes and call.size == 5
target = int(call.op_str,16) - base
assert target == 0x346440
callee = list(cs.disasm(exe.get_data(target, 0x3D), base+target))
assert callee[-1].mnemonic == 'ret' and sum(i.size for i in callee) == 0x3D

def positions(needle):
    offset = 0
    while (offset := raw.find(needle, offset)) >= 0:
        yield offset
        offset += 1

vtables = []
for name, key, slots in [
    ('VoiceSpellFireHandler', 'VoiceVTableRva', [1]),
    ('Projectile', 'ProjectileVTableRva', [0xAA, 0xAD]),
    ('BeamProjectile', 'BeamVTableRva', [0xAA, 0xAD]),
    ('MissileProjectile', 'MissileVTableRva', [0xAA, 0xAD]),
    ('ConeProjectile', 'ConeVTableRva', [0xAB, 0xC0]),
]:
    vt = values[key]
    col_va = struct.unpack('<Q', exe.get_data(vt-8,8))[0]
    col = exe.get_data(col_va-base,24)
    signature, displacement, cd, type_rva, _, self_rva = struct.unpack('<6I',col)
    assert (signature,displacement,cd,self_rva) == (1,0,0,col_va-base)
    assert exe.get_string_at_rva(type_rva+16) == f'.?AV{name}@@'.encode()
    row = {'class': name, 'vtableRva': hex(vt), 'slots': []}
    for slot in slots:
        function = struct.unpack('<Q', exe.get_data(vt+slot*8,8))[0]-base
        assert exe.get_section_by_rva(function).IMAGE_SCN_MEM_EXECUTE
        row['slots'].append({'slot': hex(slot), 'targetRva': hex(function)})
    vtables.append(row)

report = {
    'passed': True, 'runtime': '1.7.104.0', 'minimumSKSE': '2.3.1',
    'addressLibraryEncoding': 5, 'pluginVersion': '0.4.0',
    'exeSha256': hashlib.sha256(raw).hexdigest(),
    'dllSha256': hashlib.sha256(dll_path.read_bytes()).hexdigest(),
    'dllSize': dll_path.stat().st_size, 'exports': sorted(exports),
    'imports': sorted(x.dll.decode() for x in dll.DIRECTORY_ENTRY_IMPORT),
    'mainUpdateRva': hex(main), 'frameCallRva': hex(site), 'frameCallOffset': hex(values['FrameCallOffset']),
    'frameCallBytes': bytes(call.bytes).hex(' '), 'frameCallTargetRva': hex(target),
    'frameCallee': [f'{i.mnemonic} {i.op_str}' for i in callee],
    'vtableHooks': vtables, 'gameplayTested': False,
}
if len(sys.argv) > 4:
    library = Path(sys.argv[4]).read_bytes()
    assert struct.unpack_from('<5I', library) == (5,1,7,104,0)
    count = struct.unpack_from('<I',library,92)[0]
    assert len(library) == 96 + count*4
    offsets = struct.unpack_from(f'<{count}I',library,96)
    assert offsets[36564] == main
    vtable_header = (src.parent/'external/CommonLibSSE-NG/include/RE/Offsets_VTABLE.h').read_text('utf-8')
    for vtable in vtables:
        entry = re.search(r'VTABLE_'+vtable['class']+r'\{ REL::VariantID\(\d+, (\d+),',vtable_header)
        assert entry
        address_id = int(entry.group(1))
        assert offsets[address_id] == int(vtable['vtableRva'],16)
        vtable['addressLibraryID'] = address_id
    report['addressLibrary'] = {'sha256':hashlib.sha256(library).hexdigest(),
                                'format':5,'version':'1.7.104.0','entries':count,'verifiedHookAddresses':6}
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('frameCallee','vtableHooks')},ensure_ascii=False))
