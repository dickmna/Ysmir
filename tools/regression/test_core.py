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
integer = body("src/shout_dispatch.cpp", "template<class T> T ScaleInteger(")
floating = body("src/shout_dispatch.cpp", "float ScaleFloat(")
scaled = body("src/shout_dispatch.cpp", "RE::Effect::EffectItem ScaledItem(")
launch = body("src/shout_dispatch.cpp", "RE::ProjectileHandle LaunchFeatureSpell(")
directed = body("src/shout_dispatch.cpp", "struct DirectedLaunch {") + ";"
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
struct MagicCaster;
struct Actor {
TESObjectCELL cell;TESObjectCELL* GetParentCell(){return &cell;}
template<class T>T* As(){return static_cast<T*>(this);}
MagicCaster* GetMagicCaster(MagicSystem::CastingSource);
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;} };
struct MagicCaster { void CastSpellImmediate(SpellItem*,bool,Actor*,float,bool,float,Actor*); };
struct ProjectileHandle { int id{}; explicit operator bool()const{return id!=0;} };
struct Projectile {
struct ProjectileRot {float x,z;};
struct LaunchData {
Actor* shooter;NiPoint3 origin;float angleX{},angleZ{};SpellItem* spell;Actor* desiredTarget{};
TESObjectCELL* parentCell{};MagicSystem::CastingSource castingSource=MagicSystem::CastingSource::kOther;
bool alwaysHit=false,autoAim=false,useOrigin=false;float power=1.25f,scale=1.5f;
LaunchData(Actor* a,NiPoint3 o,ProjectileRot,SpellItem* s):shooter(a),origin(o),spell(s){}
};
static inline LaunchData* observed{};
static ProjectileHandle* Launch(ProjectileHandle* result,LaunchData& data){
delete observed;observed=new LaunchData(data);result->id=1;return result;}
};
}
enum class MagnitudeRule {Multiply,CloakRadius,SlowTime};
struct EffectBase {void* effect{};RE::Effect::EffectItem item;MagnitudeRule rule{};};
std::mutex mutex;std::unordered_set<RE::SpellItem*> authorized;
const Snapshot* casting{};Snapshot applied,tracked;int writes{},tracks{};
struct CastScope {const Snapshot* old=casting;CastScope(const Snapshot& s){casting=&s;}~CastScope(){casting=old;}};
void Apply(RE::SpellItem*,const Snapshot& s){applied=s;++writes;}
struct DirectedLaunch;
thread_local DirectedLaunch* directedLaunch{};
namespace ysmir::core {
bool IsPlayerActor(RE::Actor* a){return a==RE::PlayerCharacter::GetSingleton();}
Snapshot ScalingForSpell(RE::SpellItem*){return applied;}
void TrackProjectile(RE::ProjectileHandle,const Snapshot& s){tracked=s;++tracks;}
}
"""
engine = r"""
RE::Actor* blamed{};
bool hasCaster=true;
RE::MagicCaster* RE::Actor::GetMagicCaster(RE::MagicSystem::CastingSource){
static RE::MagicCaster c;return hasCaster?&c:nullptr;}
void RE::MagicCaster::CastSpellImmediate(RE::SpellItem* s,bool,RE::Actor* target,float power,bool,float,RE::Actor* blame){
assert(power==1);blamed=blame;
RE::Projectile::LaunchData d(RE::PlayerCharacter::GetSingleton(),{1,2,3},{0,0},s);
d.desiredTarget=target;d.castingSource=RE::MagicSystem::CastingSource::kInstant;
RE::ProjectileHandle h;LaunchHook::Launch(&h,d);
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
RE::SpellItem spell,other;RE::Actor& player=*RE::PlayerCharacter::GetSingleton();RE::Actor target;authorized.insert(&spell);
LaunchHook::original=RE::Projectile::Launch;
scale.magnitude=4;const RE::NiPoint3 from{0,0,3500},to{0,0,96};
auto result=ysmir::core::LaunchFeatureSpell(&spell,target,target,from,to,scale);
auto& d=*RE::Projectile::observed;
assert(result&&d.spell==&spell&&d.shooter==&player&&blamed==&target&&d.desiredTarget==&target&&d.parentCell==target.GetParentCell());
assert(d.useOrigin&&d.alwaysHit&&d.autoAim&&d.power==1.25f&&d.scale==1.5f&&d.castingSource==RE::MagicSystem::CastingSource::kInstant);
assert(std::isfinite(d.angleX)&&d.angleX>1.57f&&d.angleZ==0&&d.origin.z==3500);
assert(applied.magnitude==4&&tracked.magnitude==4&&tracks==1&&!casting&&!directedLaunch);++cases;
ysmir::core::LaunchFeatureSpell(&spell,player,target,from,to,scale);
assert(RE::Projectile::observed->shooter==&player&&blamed==&player);++cases;
enabled=false;assert(!ysmir::core::LaunchFeatureSpell(&spell,player,target,from,to,scale));enabled=true;
assert(!ysmir::core::LaunchFeatureSpell(&other,player,target,from,to,scale));assert(writes==2&&tracks==2);++cases;
hasCaster=false;assert(!ysmir::core::LaunchFeatureSpell(&spell,player,target,from,to,scale));assert(writes==2);++cases;
delete RE::Projectile::observed;
std::cout<<"{\"passed\":true,\"cases\":"<<cases<<"}";
}
"""
source = (prefix + "\nnamespace ysmir::progression {\n" + capture + "\n}\n" +
          integer + "\n" + floating + "\n" + scaled +
          "\n" + directed + "\nstruct LaunchHook {\nstatic inline decltype(&RE::Projectile::Launch) original;\n" +
          launch_hook + "\n};\n" + engine + "\nnamespace ysmir::core {\n" + launch + "\n}\n" + main)
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
                               (("Capture",capture),("ScaleInteger",integer),("ScaleFloat",floating),
                                ("ScaledItem",scaled),("LaunchFeatureSpell",launch),("DirectedLaunch",directed),("LaunchHook",launch_hook))}
(OUT / "results.json").write_text(json.dumps(result, indent=2)+"\n", "utf-8")
print(json.dumps(result, indent=2))
