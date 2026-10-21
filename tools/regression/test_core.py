"""Exercise the production growth rules and real-spell launch arguments.
Run with MSVC on PATH: python tools/regression/test_core.py BUILD_DIR
The launch sink is simulated; this does not measure in-game spell impacts.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(sys.argv[1]).resolve() / "core"
OUT.mkdir(parents=True, exist_ok=True)

def body(path, signature):
    text = (ROOT / path).read_text("utf-8")
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if not depth:
            return text[start:pos+1]
    raise ValueError(signature)

capture = body("src/progression.cpp", "ScalingSnapshot Capture()")
visual_density = body("src/progression.cpp", "float VisualDensity(")
integer = body("src/shout_dispatch.cpp", "template<class T> T ScaleInteger(")
floating = body("src/shout_dispatch.cpp", "float ScaleFloat(")
scaled = body("src/shout_dispatch.cpp", "RE::Effect::EffectItem ScaledItem(")
prepare = body("src/shout_dispatch.cpp", "void PrepareFeatureSpell(")
owned = body("src/shout_dispatch.cpp", "bool IsPlayerOwnedProjectile(")
launch_hook = body("src/shout_dispatch.cpp", "static RE::ProjectileHandle* Launch(")
prefix = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <unordered_set>
""" + f'#include "{(ROOT / "src/progression.h").as_posix()}"\n' + r"""
using Snapshot=ysmir::progression::ScalingSnapshot;
bool enabled=true,vfxEnabled=true,cooldownEnabled=true;
std::uint32_t souls{};
std::array<float,5> rates{.05f,.05f,.05f,.05f,.05f};
float vfxRate=.05f,densityAt4x=2.2f,cooldownRate=.05f;
std::uint32_t ReadSouls(){return souls;}
namespace ysmir::progression { bool Enabled(){return enabled;} }
namespace RE {
struct Effect { struct EffectItem { float magnitude{};std::uint32_t area{},duration{}; }; };
struct NiPoint3 { float x{},y{},z{}; };
struct SpellItem { template<class T>T* As(){return static_cast<T*>(this);} };
struct TESObjectCELL {};
namespace MagicSystem { enum class CastingSource{kInstant,kOther}; }
struct TESObjectREFR {
virtual ~TESObjectREFR()=default;
template<class T>T* As(){return dynamic_cast<T*>(this);}
};
struct Actor : TESObjectREFR {};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;} };
struct Projectile;
struct ProjectileHandle {
Projectile* ptr{};int id{};
ProjectileHandle()=default;ProjectileHandle(Projectile* p);
explicit operator bool()const{return id!=0;}
int native_handle()const{return id;}
std::shared_ptr<Projectile> get()const{return {ptr,[](Projectile*){}};}
};
struct Projectile {
int id=1;
struct Ref {TESObjectREFR* p{};std::shared_ptr<TESObjectREFR> get(){return {p,[](TESObjectREFR*){}};}};
struct {Ref shooter;} runtime;
auto& GetProjectileRuntimeData(){return runtime;}
struct LaunchData {
TESObjectREFR* shooter;NiPoint3 origin;float angleX=.2f,angleZ=.5f;SpellItem* spell;TESObjectREFR* desiredTarget{};
TESObjectCELL* parentCell{};MagicSystem::CastingSource castingSource=MagicSystem::CastingSource::kOther;
bool alwaysHit=false,autoAim=false,useOrigin=false;float power=1.25f,scale=1.5f;
LaunchData(TESObjectREFR* a,NiPoint3 o,SpellItem* s):shooter(a),origin(o),spell(s){}
};
static inline LaunchData* observed{};static inline int launches{};
static ProjectileHandle* Launch(ProjectileHandle* result,LaunchData& data){
delete observed;observed=new LaunchData(data);++launches;static Projectile p;*result=ProjectileHandle(&p);return result;}
};
ProjectileHandle::ProjectileHandle(Projectile* p):ptr(p),id(p?p->id:0){}
}
enum class MagnitudeRule {Multiply,CloakRadius,SlowTime};
struct EffectBase {void* effect{};RE::Effect::EffectItem item;MagnitudeRule rule{};};
std::mutex mutex,snapshotMutex;std::unordered_set<RE::SpellItem*> authorized;
struct Context {RE::ProjectileHandle handle;};std::unordered_map<int,Context> projectileContexts;
const Snapshot* casting{};Snapshot applied,tracked;int writes{},tracks{},visuals{},feedback{};
struct CastScope {const Snapshot* old=casting;CastScope(const Snapshot& s){casting=&s;}~CastScope(){casting=old;}};
struct InstanceScope : CastScope {InstanceScope(RE::SpellItem*,const Snapshot& s):CastScope(s){}};
void Apply(RE::SpellItem*,const Snapshot& s){applied=s;++writes;}
namespace ysmir::core {
bool IsPlayerActor(RE::Actor* a){return a==RE::PlayerCharacter::GetSingleton();}
Snapshot ScalingForSpell(RE::SpellItem*){return applied;}
void TrackProjectile(RE::ProjectileHandle,const Snapshot& s){tracked=s;++tracks;}
}
namespace ysmir::storm_call {
RE::SpellItem* bolt{};bool accept=true;Snapshot inherited;
bool IsBolt(RE::SpellItem* s){return s&&s==bolt;}
bool AcceptLaunch(const RE::Projectile::LaunchData&,Snapshot& s){s=inherited;return accept;}
void Launched(RE::TESObjectREFR*){++feedback;}
}
namespace ysmir::projectile_visuals {
void TrackStorm(RE::ProjectileHandle h,const Snapshot& s){++visuals;ysmir::core::TrackProjectile(h,s);}
}
"""
main = r"""
int main(){
int cases=0;
for(auto n:{0u,1u,20u,60u,1000u,UINT32_MAX}){
souls=n;auto s=ysmir::progression::Capture();const float expected=1.f+.05f*static_cast<float>(n);
assert(s.souls==n&&s.distance==expected&&s.magnitude==expected&&s.area==expected&&s.duration==expected);
const float density=1+(2.2f-1)*(expected-1)/3;
assert(s.collision==expected&&s.vfx==expected&&std::abs(s.density-density)<=1e-6f*std::max(1.f,density));
assert(std::isfinite(s.cooldown)&&s.cooldown>0&&s.cooldown<=1);
if(n<=1000)assert(std::abs(s.cooldown-static_cast<float>(std::pow(1.0-static_cast<double>(.05f),n)))<1e-6f);
}
++cases;
souls=60;vfxEnabled=false;cooldownEnabled=false;rates[2]=0;auto off=ysmir::progression::Capture();
assert(off.duration==1&&off.magnitude==4&&off.vfx==1&&off.density==1&&off.cooldown==1);
enabled=false;off=ysmir::progression::Capture();assert(off.souls==60&&off.magnitude==1&&off.distance==1);enabled=true;++cases;
Snapshot scale;scale.magnitude=2;scale.area=3;scale.duration=4;
EffectBase ordinary{nullptr,{10,5,8},MagnitudeRule::Multiply};
auto item=ScaledItem(ordinary,false,scale);assert(item.magnitude==20&&item.area==15&&item.duration==32);
auto cloak=ordinary;cloak.rule=MagnitudeRule::CloakRadius;assert(ScaledItem(cloak,false,scale).magnitude==30);++cases;
EffectBase slow{nullptr,{.3f,0,8},MagnitudeRule::SlowTime};
item=ScaledItem(slow,true,scale);assert(item.magnitude==.15f&&item.duration==8&&item.area==0);
scale.magnitude=100;assert(ScaledItem(slow,true,scale).magnitude==.01f);++cases;
assert(ScaleInteger<std::uint32_t>(0,100)==0);
assert(ScaleInteger<std::uint32_t>(UINT32_MAX,100)==UINT32_MAX);
assert(std::isfinite(ScaleFloat(std::numeric_limits<float>::max(),100)));++cases;
RE::SpellItem spell,other;RE::Actor& player=*RE::PlayerCharacter::GetSingleton();RE::Actor target;
RE::TESObjectREFR marker,aim;authorized.insert(&spell);LaunchHook::original=RE::Projectile::Launch;
ysmir::storm_call::bolt=&spell;ysmir::storm_call::inherited.magnitude=4;
RE::Projectile::LaunchData d(&marker,{1,2,3500},&spell);d.desiredTarget=&aim;RE::TESObjectCELL cell;d.parentCell=&cell;
RE::ProjectileHandle result;
LaunchHook::Launch(&result,d);auto& got=*RE::Projectile::observed;
assert(result&&got.shooter==&marker&&got.desiredTarget==&aim&&got.spell==&spell&&got.parentCell==&cell);
assert(got.origin.x==1&&got.origin.y==2&&got.origin.z==3500&&got.angleX==.2f&&got.angleZ==.5f);
assert(!got.useOrigin&&!got.alwaysHit&&!got.autoAim&&got.power==1.25f&&got.scale==1.5f&&got.castingSource==RE::MagicSystem::CastingSource::kOther);
assert(tracks==1&&visuals==1&&feedback==1&&tracked.magnitude==4&&!casting);++cases;
ysmir::storm_call::accept=false;LaunchHook::Launch(&result,d);
assert(!result&&RE::Projectile::launches==1&&tracks==1&&feedback==1);ysmir::storm_call::accept=true;++cases;
ysmir::storm_call::bolt=nullptr;applied.magnitude=7;d.shooter=&player;LaunchHook::Launch(&result,d);
assert(result&&tracked.magnitude==7&&tracks==2&&visuals==1);
Snapshot explicitScale;explicitScale.magnitude=9;
{CastScope ctx(explicitScale);LaunchHook::Launch(&result,d);}assert(tracked.magnitude==9&&!casting);++cases;
d.shooter=&target;LaunchHook::Launch(&result,d);d.spell=&other;d.shooter=&player;LaunchHook::Launch(&result,d);
assert(RE::Projectile::launches==5&&tracks==3);++cases;
scale.magnitude=4;ysmir::core::PrepareFeatureSpell(&spell,scale);assert(writes==1&&applied.magnitude==4);
ysmir::core::PrepareFeatureSpell(&other,scale);enabled=false;ysmir::core::PrepareFeatureSpell(&spell,scale);enabled=true;
assert(writes==1);++cases;
RE::Projectile trackedMarker,untrackedMarker,playerBolt;trackedMarker.id=13;untrackedMarker.id=13;playerBolt.id=14;
trackedMarker.runtime.shooter.p=&marker;untrackedMarker.runtime.shooter.p=&marker;playerBolt.runtime.shooter.p=&player;
projectileContexts.emplace(13,Context{RE::ProjectileHandle(&trackedMarker)});
assert(ysmir::core::IsPlayerOwnedProjectile(&trackedMarker)&&ysmir::core::IsPlayerOwnedProjectile(&playerBolt));
assert(!ysmir::core::IsPlayerOwnedProjectile(&untrackedMarker)&&!ysmir::core::IsPlayerOwnedProjectile(nullptr));++cases;
delete RE::Projectile::observed;
std::cout<<"{\"passed\":true,\"cases\":"<<cases<<"}";
}
"""
source = (prefix + "\nnamespace ysmir::progression {\n" + visual_density + "\n" + capture + "\n}\n" +
          integer + "\n" + floating + "\n" + scaled +
          "\nstruct LaunchHook {\nstatic inline decltype(&RE::Projectile::Launch) original;\n" +
          launch_hook + "\n};\nnamespace ysmir::core {\n" + prepare + "\n" + owned + "\n}\n" + main)
(OUT / "fixture.cpp").write_text(source, "utf-8")
built = subprocess.run(["cl.exe", "/nologo", "/std:c++20", "/EHsc", "/Od", "/W4", "/MD",
                        "fixture.cpp", "/Fe:fixture.exe", "/Fo:fixture.obj"],
                       cwd=OUT, capture_output=True, text=True)
(OUT / "build.log").write_text(built.stdout+built.stderr, "utf-8")
if built.returncode:
    print(built.stdout+built.stderr)
built.check_returncode()
run = subprocess.run([str(OUT / "fixture.exe")], cwd=OUT, capture_output=True, text=True)
if run.returncode:
    print(run.stdout+run.stderr)
run.check_returncode()
result = json.loads(run.stdout)
result["boundary"] = "Actual growth and launch function bodies; fake launch sink, not engine damage evaluation"
result["sourceBodiesSha256"] = {key: hashlib.sha256(value.encode()).hexdigest() for key, value in
                               (("Capture",capture),("VisualDensity",visual_density),("ScaleInteger",integer),("ScaleFloat",floating),
                                ("ScaledItem",scaled),("PrepareFeatureSpell",prepare),("IsPlayerOwnedProjectile",owned),("LaunchHook",launch_hook))}
(OUT / "results.json").write_text(json.dumps(result, indent=2)+"\n", "utf-8")
print(json.dumps(result, indent=2))
