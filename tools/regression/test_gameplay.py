"""Compile the actual storm / elemental adapters against controlled engine services.

Run in an x64 Native Tools shell: python tools/regression/test_gameplay.py BUILD_DIR
This checks decisions, state and call arguments, not Skyrim's VM, physics or rendering.
"""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=True)

COMMON = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
""" + f'\n#include "{(ROOT / "src/progression.h").as_posix()}"\n' + r"""
namespace fixture { bool enabled=true,paused=false; double now=10; int cases=0; }
namespace ysmir::progression {
bool Enabled(){return fixture::enabled;} bool Paused(){return fixture::paused;} double Now(){return fixture::now;}
}
namespace ysmir::config {
bool Bool(const char*,const char*,bool v){return v;}
float Float(const char*,const char*,float v){return v;}
}
namespace SKSE {
namespace log { template<class...T> void info(const char*,T&&...){} }
struct MessagingInterface {
enum { kPreLoadGame,kPostLoadGame,kNewGame,kDataLoaded };
struct Message { int type; };
};
}
"""

STORM_PREFIX = (ROOT / "tools/regression/storm_fixture.h").read_text("utf8") + f'\n#include "{(ROOT / "src/RuntimeForms.h").as_posix()}"\n'
STORM_MAIN = (ROOT / "tools/regression/storm_scenarios.cpp").read_text("utf8")

EF_PREFIX = r"""
namespace RE {
using FormID=std::uint32_t;
enum class BSEventNotifyControl{kContinue};
namespace MagicSystem { enum class Delivery{kSelf,kAimed}; }
struct EffectSetting { struct { MagicSystem::Delivery delivery=MagicSystem::Delivery::kSelf; } data; };
struct SpellItem { struct Effect { EffectSetting* baseEffect{}; }; std::vector<Effect*> effects; };
struct TESShout {
FormID id=0x32921;FormID GetFormID(){return id;}
struct { SpellItem* spell{}; } variations[3];
};
struct BSAnimationGraphEvent { void* holder{};std::string tag="hitframe"; };
template<class T> struct BSTEventSource {};
template<class T> struct BSTEventSink { virtual BSEventNotifyControl ProcessEvent(const T*,BSTEventSource<T>*)=0; };
struct PlayerCharacter {
std::vector<EffectSetting*> active;
static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;}
PlayerCharacter* AsMagicTarget(){return this;}
bool HasMagicEffect(EffectSetting* e){return std::find(active.begin(),active.end(),e)!=active.end();}
template<class T> void AddAnimationGraphEventSink(T*){}
};
struct TESDataHandler {
static TESDataHandler* GetSingleton(){static TESDataHandler d;return &d;}
template<class T> T* LookupForm(std::uint32_t,const char*){return nullptr;}
};
}
namespace fixture {
struct Cast {RE::SpellItem* spell;ysmir::progression::ScalingSnapshot scaling;};
std::vector<Cast> casts;std::function<void()> onCast;
}
namespace ysmir::core {
RE::TESShout* PrivateShout(std::uint32_t){return nullptr;}
progression::ScalingSnapshot CurrentScaling(){return {};}
bool CastFeatureSpell(RE::SpellItem* s,RE::PlayerCharacter&,RE::PlayerCharacter*,const progression::ScalingSnapshot& scale){
fixture::casts.push_back({s,scale});if(fixture::onCast)fixture::onCast();return true;}
}
""" + f'\n#include "{(ROOT / "src/RuntimeForms.h").as_posix()}"\n'

EF_MAIN = r"""
int main(){
auto* player=RE::PlayerCharacter::GetSingleton();RE::BSAnimationGraphEvent event{player,"hitframe"};
RE::EffectSetting effects[3];RE::SpellItem children[3][3];RE::TESShout shout;
for(int t=0;t<3;++t){activation[t]=&effects[t];snapshots[t].magnitude=static_cast<float>(t+2);
for(int e=0;e<3;++e)spells[e][t]=&children[e][t];}
auto hit=[&]{AnimationSink::Get().ProcessEvent(&event,nullptr);};
chance=1;player->active={&effects[0]};hit();assert(fixture::casts.size()==3);
for(int e=0;e<3;++e)assert(ready[e][0]==12&&fixture::casts[e].scaling.magnitude==2);
fixture::now=11;hit();assert(fixture::casts.size()==3);++fixture::cases;
// All nine rules keep separate timers; changing word cannot block a new word.
player->active={&effects[1]};hit();assert(fixture::casts.size()==6);
player->active={&effects[2]};hit();assert(fixture::casts.size()==9);
for(int e=0;e<3;++e)assert(ready[e][1]==13&&ready[e][2]==13);++fixture::cases;
progression_placeholder:
ysmir::progression::ScalingSnapshot s;s.magnitude=7;
ysmir::elemental_fury::OnShout(&shout,0,s);assert(ready[0][0]==12&&snapshots[0].magnitude==7&&snapshots[1].magnitude==3);
player->active={&effects[0]};hit();assert(fixture::casts.size()==9);++fixture::cases;
fixture::now=12;hit();assert(fixture::casts.size()==9); // DAC uses strictly greater than.
fixture::now=12.01;hit();assert(fixture::casts.size()==12&&fixture::casts.back().scaling.magnitude==7);++fixture::cases;
// If multiple activation effects overlap, each matching DAC rule is still evaluated.
ready={};fixture::casts.clear();player->active={&effects[0],&effects[1],&effects[2]};hit();
assert(fixture::casts.size()==9);++fixture::cases;
ready={};fixture::casts.clear();chance=0;hit();assert(fixture::casts.empty()&&ready[0][0]==0);++fixture::cases;
chance=1;fixture::onCast=[&]{assert(ready[0][0]>fixture::now);};player->active={&effects[0]};hit();
fixture::onCast={};assert(fixture::casts.size()==3);++fixture::cases;
fixture::casts.clear();ready={};fixture::enabled=false;hit();fixture::enabled=true;enabled=false;hit();enabled=true;
event.holder=nullptr;hit();event.holder=player;event.tag="other";hit();event.tag="hitframe";
fixture::paused=true;hit();fixture::paused=false;assert(fixture::casts.empty());++fixture::cases;
fixture::paused=true;fixture::now+=100;hit();fixture::paused=false;hit();
assert(fixture::casts.size()==3); // Wall time includes menus; dispatch still requires unpaused state.
++fixture::cases;
std::cout<<"{\"passed\":true,\"cases\":"<<fixture::cases<<"}";
}
""".replace("progression_placeholder:\n", "")


def run(name, source_file, prefix, main, clock_seam=False):
    original = (ROOT / source_file).read_text(encoding="utf-8")
    source = re.sub(r"^#include .*\n", "", original, flags=re.M)
    if clock_seam:
        source, count = re.subn(r"double Now\(\) \{[^\n]*\}", "double Now() { return fixture::now; }", source)
        assert count == 1
    folder = OUT / name
    folder.mkdir(exist_ok=True)
    (folder / "fixture.cpp").write_text(COMMON + prefix + source + main, encoding="utf-8")
    built = subprocess.run(
        ["cl.exe", "/nologo", "/std:c++20", "/EHsc", "/Od", "/W4", "/MD",
         "fixture.cpp", "/Fe:fixture.exe", "/Fo:fixture.obj"], cwd=folder,
        capture_output=True, text=True)
    (folder / "build.log").write_text(built.stdout + built.stderr, encoding="utf-8")
    if built.returncode:
        print(built.stdout + built.stderr)
    built.check_returncode()
    result = subprocess.run([str(folder / "fixture.exe")], cwd=folder, capture_output=True, text=True)
    if result.returncode:
        print(result.stdout + result.stderr)
    result.check_returncode()
    report = json.loads(result.stdout)
    report.update(source=source_file, sourceSha256=hashlib.sha256(original.encode()).hexdigest(),
                  boundary="Actual adapter; fake engine services" + ("; injectable wall clock" if clock_seam else ""))
    (folder / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


report = {
    "storm": run("storm", "src/storm_call.cpp", STORM_PREFIX, STORM_MAIN),
    "elementalFury": run("elemental-fury", "src/elemental_fury.cpp", EF_PREFIX, EF_MAIN, True),
}
(OUT / "gameplay-results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps(report, indent=2))
