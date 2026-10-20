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

STORM_PREFIX = r"""
namespace RE {
using FormID=std::uint32_t;
struct NiPoint3 {
float x{},y{},z{};
float GetDistance(const NiPoint3& p)const{return std::sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z));}
};
struct TESObjectCELL { bool interior{}; bool IsInteriorCell(){return interior;} };
struct EffectSetting {};
struct Effect { EffectSetting* baseEffect{}; struct { float magnitude{}; std::uint32_t duration{}; } effectItem; };
struct SpellItem { std::vector<Effect*> effects; };
struct ActiveEffect;
struct Actor;
struct ActorHandle { Actor* ptr{}; std::shared_ptr<Actor> get()const{return ptr?std::shared_ptr<Actor>(ptr,[](Actor*){}):nullptr;} };
struct TESObjectREFR {
virtual ~TESObjectREFR()=default;
template<class T> T* As(){return dynamic_cast<T*>(this);}
};
struct Actor : TESObjectREFR {
bool dead{},loaded=true,hostile=true; NiPoint3 position; TESObjectCELL cell; bool hasCell=true;
std::vector<ActiveEffect*> effects;
bool IsDead(){return dead;} bool Is3DLoaded(){return loaded;} bool IsHostileToActor(Actor*){return hostile;}
TESObjectCELL* GetParentCell(){return hasCell?&cell:nullptr;}
NiPoint3 GetPosition(){return position;} ActorHandle GetHandle(){return {this};}
auto* GetActiveEffectList(){return &effects;}
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;} };
struct ActiveEffect {
enum class Flag{kDispelled};
struct { bool dispelled{}; bool any(Flag)const{return dispelled;} } flags;
float duration{},elapsedSeconds{}; SpellItem* spell{}; EffectSetting* base{};
std::uint16_t usUniqueID=1;
std::shared_ptr<Actor> GetCasterActor(){return std::shared_ptr<Actor>(PlayerCharacter::GetSingleton(),[](Actor*){});}
EffectSetting* GetBaseObject(){return base;} void Dispel(bool){flags.dispelled=true;}
};
struct ProjectileHandle { int id{}; explicit operator bool()const{return id!=0;} };
struct BGSProjectile {};
struct TESShout { struct { SpellItem* spell{}; } variations[3]; };
struct BGSListForm { bool excluded{}; bool HasForm(void*){return excluded;} };
struct Sky {
enum class Mode{kFull,kInterior}; Mode mode=Mode::kFull; void* currentWeather{};
static Sky* GetSingleton(){static Sky sky;return &sky;}
};
struct TESImageSpaceModifier {};
struct ImageSpaceModifierInstanceForm { static void Trigger(TESImageSpaceModifier*,float,void*){} };
struct BSISoundDescriptor {};
struct BGSSoundDescriptorForm : BSISoundDescriptor {};
struct BSSoundHandle { void SetPosition(NiPoint3){} void Play(){} };
struct BSAudioManager {
static BSAudioManager* GetSingleton(){static BSAudioManager a;return &a;}
bool GetSoundHandle(BSSoundHandle&,BSISoundDescriptor*){return true;}
};
namespace BSContainer { enum class ForEachResult{kContinue}; }
struct TES {
std::vector<TESObjectREFR*> refs; std::function<void()> afterScan;
static TES* GetSingleton(){static TES t;return &t;}
template<class F> void ForEachReferenceInRange(Actor*,float,F&& f){for(auto* p:refs)f(p);if(afterScan)afterScan();}
};
struct TESDataHandler {
static TESDataHandler* GetSingleton(){static TESDataHandler t;return &t;}
template<class T> T* LookupForm(std::uint32_t,const char*){return nullptr;}
};
}
namespace fixture {
using Snapshot=ysmir::progression::ScalingSnapshot;
struct Launch { RE::SpellItem* spell; RE::Actor* blame; RE::Actor* target; RE::NiPoint3 from,to; Snapshot scale; };
std::vector<Launch> launches; int casts=0,visuals=0; Snapshot snapshot;
std::vector<std::unique_ptr<RE::ActiveEffect>> allocated;
}
namespace SKSE {
struct SerializationInterface {
struct Record { std::uint32_t type,version; std::vector<std::byte> bytes; };
std::vector<Record> records; std::size_t index{},offset{}; bool resolve=true,shortRead=false;
void SetUniqueID(std::uint32_t){} void SetSaveCallback(void(*)(SerializationInterface*)){}
void SetLoadCallback(void(*)(SerializationInterface*)){} void SetRevertCallback(void(*)(SerializationInterface*)){}
template<class T> void WriteRecord(std::uint32_t type,std::uint32_t version,const T& value){
Record r{type,version,std::vector<std::byte>(sizeof(T))};std::memcpy(r.bytes.data(),&value,sizeof(T));records.push_back(r);}
bool GetNextRecordInfo(std::uint32_t& t,std::uint32_t& v,std::uint32_t& n){
if(index==records.size())return false;auto& r=records[index++];t=r.type;v=r.version;n=static_cast<std::uint32_t>(r.bytes.size());offset=0;return true;}
std::uint32_t ReadRecordData(void* p,std::uint32_t n){
auto& b=records[index-1].bytes;auto count=std::min(n,static_cast<std::uint32_t>(b.size()-offset));
if(shortRead&&count)--count;std::memcpy(p,b.data()+offset,count);offset+=count;return count;}
bool ResolveFormID(RE::FormID in,RE::FormID& out){out=in;return resolve;}
};
SerializationInterface* GetSerializationInterface(){static SerializationInterface io;return &io;}
}
namespace ysmir::core {
bool IsPlayerActor(RE::Actor* p){return p==RE::PlayerCharacter::GetSingleton();}
RE::TESShout* PrivateShout(std::uint32_t){return nullptr;}
progression::ScalingSnapshot ScalingForSpell(RE::SpellItem*){return fixture::snapshot;}
bool CastFeatureSpell(RE::SpellItem* spell,RE::Actor& player,RE::Actor*,const progression::ScalingSnapshot& s){
if(!spell)return false;++fixture::casts;
for(auto* item:spell->effects){
auto e=std::make_unique<RE::ActiveEffect>();e->spell=spell;e->base=item->baseEffect;
e->duration=static_cast<float>(item->effectItem.duration)*s.duration;
player.effects.push_back(e.get());fixture::allocated.push_back(std::move(e));}
return true;}
RE::ProjectileHandle LaunchFeatureSpell(RE::SpellItem* spell,RE::Actor& blame,RE::Actor& target,
const RE::NiPoint3& from,const RE::NiPoint3& to,const progression::ScalingSnapshot& s){
fixture::launches.push_back({spell,&blame,&target,from,to,s});return {1};}
}
namespace ysmir::projectile_visuals {
void TrackStorm(RE::ProjectileHandle,const progression::ScalingSnapshot&){++fixture::visuals;}
void SetStormBase(RE::BGSProjectile*){} void Clear(){}
}
namespace ysmir::storm_call { bool Available(); }
""" + f'\n#include "{(ROOT / "src/RuntimeForms.h").as_posix()}"\n'

STORM_MAIN = r"""
int main(){
auto& player=*RE::PlayerCharacter::GetSingleton();
RE::Actor first,second,friendActor,dead,unloaded,far,high,newcomer;
first.position={10,0,0};second.position={20,0,0};friendActor.hostile=false;
dead.dead=true;unloaded.loaded=false;far.position={101,0,0};high.position={0,0,101};
auto* world=RE::TES::GetSingleton();
world->refs={&player,&first,&second,&friendActor,&dead,&unloaded,&far,&high};
settings.radius=100;settings.spread={0,0,0};settings.staggerMin=settings.staggerMax=.2f;
settings.minimum=settings.maximum={1,1,1};settings.initialMin=settings.initialMax=0;
RE::SpellItem root[3],bolt[3],weathers[3];RE::EffectSetting timer[3],weatherBase[3];
RE::Effect timerItem[3],weatherItem[3];
for(int i=0;i<3;++i){
roots[i]=&root[i];bolts[i]=&bolt[i];weather[i]=&weathers[i];timers[i]=&timer[i];
timerItem[i].baseEffect=&timer[i];timerItem[i].effectItem.duration=60*(i+1);
weatherItem[i].baseEffect=&weatherBase[i];weatherItem[i].effectItem.duration=60*(i+1);
root[i].effects={&timerItem[i]};weathers[i].effects={&weatherItem[i]};}
resolved=true;
for(std::uint32_t tier=0;tier<3;++tier){
state={};state.tier=tier;fixture::launches.clear();Advance(player,0);
assert(fixture::launches.size()==2&&state.count==tier);
for(std::uint32_t i=0;i<tier;++i){Advance(player,.19f);assert(fixture::launches.size()==2*(i+1));
Advance(player,.011f);assert(fixture::launches.size()==2*(i+2));}
assert(state.count==0);++fixture::cases;
}
// Membership is recomputed before every pass, and once more before launch.
state={};state.tier=2;fixture::launches.clear();Advance(player,0);
first.hostile=false;second.dead=true;newcomer.position={30,0,0};world->refs.push_back(&newcomer);
Advance(player,.201f);assert(fixture::launches.size()==3&&fixture::launches.back().target==&newcomer);
world->afterScan=[&]{newcomer.hostile=false;};Advance(player,.201f);
assert(fixture::launches.size()==3);world->afterScan={};++fixture::cases;
first.hostile=true;second.dead=false;newcomer.hostile=true;world->refs={&first,&second};
state={};state.tier=2;fixture::launches.clear();Advance(player,0);Advance(player,5);
assert(fixture::launches.size()==4&&state.count==1&&state.pending[0]>.19f);
Advance(player,.1f);assert(fixture::launches.size()==4);
Advance(player,.101f);assert(fixture::launches.size()==6&&state.count==0);++fixture::cases;
world->refs.clear();state={};state.tier=2;Advance(player,0);
assert(state.count==0&&state.cycle==settings.retry);++fixture::cases;
// Radius growth remains spherical, and the configured blame and launch geometry survive.
world->refs={&far,&high};state={};state.scaling.area=2;state.scaling.magnitude=3;
fixture::launches.clear();assert(StrikePass(player)==2);
for(auto& l:fixture::launches){
assert(l.blame==l.target&&l.spell==bolts[0]&&l.scale.magnitude==3);
assert(l.from.z-l.target->position.z==3500&&l.to.z-l.target->position.z==96);}
settings.blameCaster=true;StrikePass(player);assert(fixture::launches.back().blame==&player);++fixture::cases;
// v4 roundtrip and exact v3 migration; bad records must not start a storm.
state={};state.tier=2;state.scaling.magnitude=3;state.count=2;state.pending={.2f,.4f};state.cycle=2;
RE::ActiveEffect active;active.spell=roots[2];active.base=timers[2];active.duration=100;active.elapsedSeconds=12;
player.effects={&active};identity=&active;identityID=active.usUniqueID;
SKSE::SerializationInterface io;Save(&io);assert(io.records.size()==1&&io.records[0].version==4);
Load(&io);assert(resume&&resume->remaining==88&&resume->count==2&&resume->scaling.magnitude==3);++fixture::cases;
Legacy legacy{0x14,3,42,2,{.1f,.3f},2,{2,3,4,5,6,7}};
SKSE::SerializationInterface old;old.WriteRecord(RecordID,3,legacy);Load(&old);
assert(resume&&resume->tier==2&&resume->remaining==42&&resume->scaling.collision==6);
assert(std::abs(resume->scaling.density-3.4f)<.001f);
auto completedLegacy=legacy;completedLegacy.count=0;completedLegacy.pending={-.01f,-.02f};
SKSE::SerializationInterface completed;completed.WriteRecord(RecordID,3,completedLegacy);Load(&completed);
assert(resume&&resume->count==0&&resume->pending[0]==0&&resume->pending[1]==0);++fixture::cases;
const Saved valid=*resume;
for(int fault=0;fault<6;++fault){
Saved bad=valid;
if(fault==0)bad.tier=3;if(fault==1)bad.count=3;if(fault==2)bad.remaining=-1;
if(fault==3)bad.pending[0]=-.1f;if(fault==4)bad.scaling.magnitude=std::numeric_limits<float>::infinity();
if(fault==5)bad.scaling.area=.5f;
SKSE::SerializationInterface invalid;invalid.WriteRecord(RecordID,4,bad);Load(&invalid);assert(!resume);}
SKSE::SerializationInterface wrong;legacy.caster=0x15;wrong.WriteRecord(RecordID,3,legacy);Load(&wrong);assert(!resume);
SKSE::SerializationInterface truncated;truncated.WriteRecord(RecordID,4,valid);truncated.shortRead=true;Load(&truncated);assert(!resume);
++fixture::cases;
SKSE::SerializationInterface unknown;std::array<std::byte,1500> junk{};unknown.WriteRecord(123,1,junk);
Saved good;good.remaining=10;unknown.WriteRecord(RecordID,4,good);Load(&unknown);assert(resume&&resume->remaining==10);++fixture::cases;
// Timer is the engine effect, not another wall-clock countdown.
Reset();world->refs={&first,&second};fixture::launches.clear();player.effects={&active};
active.flags.dispelled=false;active.elapsedSeconds=12;fixture::now+=1;
ysmir::storm_call::FrameUpdate();assert(state.remaining==88&&fixture::launches.size()==2);
const auto pendingBefore=state.pending[0];player.cell.interior=true;
active.elapsedSeconds+=10;fixture::now+=10;ysmir::storm_call::FrameUpdate();
assert(state.remaining==78&&state.pending[0]==pendingBefore&&fixture::launches.size()==2);
player.cell.interior=false;fixture::now+=.05;ysmir::storm_call::FrameUpdate();
assert(fixture::launches.size()==2);++fixture::cases;
// A weather effect is cast once, extended on recast and dispelled on expiry.
auto weatherEffects=WeatherEffects(player);assert(weatherEffects.size()==1);
const auto oldCasts=fixture::casts;MaintainWeather(player,200);
assert(fixture::casts==oldCasts&&weatherEffects[0]->duration-weatherEffects[0]->elapsedSeconds==200);
ysmir::storm_call::BeforeShout(player);assert(active.flags.dispelled&&WeatherEffects(player).size()==1);
active.flags.dispelled=false;active.elapsedSeconds=0;active.duration=200;active.usUniqueID++;
fixture::now+=.01;ysmir::storm_call::FrameUpdate();assert(fixture::casts==oldCasts);++fixture::cases;
fixture::enabled=false;ysmir::storm_call::FrameUpdate();
assert(active.flags.dispelled&&!identity&&!resume&&WeatherEffects(player).empty());fixture::enabled=true;++fixture::cases;
// Restore old saves without a timer by creating the native effect with remaining time.
player.effects.clear();Reset();good.tier=1;good.remaining=23;good.scaling.magnitude=4;resume=good;
fixture::now+=.1;ysmir::storm_call::FrameUpdate();std::uint32_t restoredTier{};
auto* restored=Timer(player,restoredTier);
assert(restored&&restoredTier==1&&restored->duration-restored->elapsedSeconds==23&&state.scaling.magnitude==4);
restored->elapsedSeconds=restored->duration;fixture::now+=.1;ysmir::storm_call::FrameUpdate();
assert(!identity&&WeatherEffects(player).empty());++fixture::cases;
// Indoors / exception weather defer the vanilla weather cast; they do not defer time.
player.effects.clear();fixture::allocated.clear();Reset();active.flags.dispelled=false;
active.duration=60;active.elapsedSeconds=0;active.spell=roots[0];active.base=timers[0];player.effects={&active};
player.cell.interior=true;const auto castCount=fixture::casts;
ysmir::storm_call::FrameUpdate();assert(fixture::casts==castCount);
player.cell.interior=false;RE::BGSListForm blocked;blocked.excluded=true;exceptions=&blocked;
ysmir::storm_call::FrameUpdate();assert(fixture::casts==castCount);
blocked.excluded=false;ysmir::storm_call::FrameUpdate();assert(fixture::casts==castCount+1);++fixture::cases;
// Global pause / loading never launch queued strikes.
fixture::launches.clear();fixture::paused=true;fixture::now+=100;ysmir::storm_call::FrameUpdate();
assert(fixture::launches.empty());fixture::paused=false;loading=true;ysmir::storm_call::FrameUpdate();
assert(fixture::launches.empty());loading=false;++fixture::cases;
std::cout<<"{\"passed\":true,\"cases\":"<<fixture::cases<<"}";
}
"""

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
