"""Package the verified local output; reference assets are not redistributed by GitHub."""
from pathlib import Path
import hashlib, json, zipfile

root=Path(__file__).resolve().parent
pkg=root/'package'
report=json.loads((root/'reports/verification.json').read_text(encoding='utf-8'))
assert report['status']=='PASS'
plugin=pkg/'Ysmir - Shout System Overhaul.esp'
assert hashlib.sha256(plugin.read_bytes()).hexdigest()==report['pluginSHA256']
assets=json.loads((root/'reports/assets.json').read_text(encoding='utf-8'))
files=[plugin,*sorted((pkg/'Scripts').glob('*.pex')),*sorted((pkg/'SEQ').glob('*.seq')),pkg/'Ysmir - 说明.txt']
files.extend(pkg/a['path'] for a in assets)
assert len(files)==35 and sum(p.suffix=='.pex' for p in files)==11
for a in assets:
    assert hashlib.sha256((pkg/a['path']).read_bytes()).hexdigest()==a['sha256']
out=root/'dist';out.mkdir(exist_ok=True)
archive=out/'Ysmir - Shout System Overhaul v1.2.3.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for p in files:z.write(p,p.relative_to(pkg).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
print('SHA256:',hashlib.sha256(archive.read_bytes()).hexdigest())
