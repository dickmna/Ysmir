int main(){
using namespace ysmir::storm_call;
auto& player=*RE::PlayerCharacter::GetSingleton();
RE::Actor first,second,friendActor,dead,unloaded,far,high,newcomer;
first.position={10,0,0};second.position={20,0,0};friendActor.hostile=false;
dead.dead=true;unloaded.loaded=false;far.position={101,0,0};high.position={0,0,101};
auto* world=RE::TES::GetSingleton();auto* vm=RE::BSScript::Internal::VirtualMachine::GetSingleton();
RE::TESBoundObject markerBase;marker=&markerBase;
settings.radius=100;settings.spread={0,0,0};settings.staggerMin=.12f;settings.staggerMax=.35f;
settings.minimum=settings.maximum={1,1,1};settings.initialMin=settings.initialMax=0;
RE::SpellItem root[3],bolt[3],weathers[3];RE::EffectSetting timer[3],weatherBase[3];
RE::Effect timerItem[3],weatherItem[3];
for(int i=0;i<3;++i){
    roots[i]=&root[i];bolts[i]=&bolt[i];weather[i]=&weathers[i];timers[i]=&timer[i];
    timerItem[i].baseEffect=&timer[i];timerItem[i].effectItem.duration=60*(i+1);
    weatherItem[i].baseEffect=&weatherBase[i];weatherItem[i].effectItem.duration=60*(i+1);
    root[i].effects={&timerItem[i]};weathers[i].effects={&weatherItem[i]};
}
resolved=true;
RE::ActiveEffect active;
auto start=[&](int tier){
    Reset();vm->calls.clear();player.effects.clear();fixture::allocated.clear();
    active={};active.duration=100;active.elapsedSeconds=12;active.spell=roots[tier];active.base=timers[tier];
    player.effects={&active};state.tier=tier;identity=&active;identityID=active.usUniqueID;
};
auto tick=[&](double dt){fixture::now+=dt;Advance(player,static_cast<float>(dt));};
for(int tier=0;tier<3;++tier){
    world->refs={&player,&first,&second,&friendActor,&dead,&unloaded,&far,&high};start(tier);tick(0);
    assert(targets.size()==2&&vm->calls.size()==2);
    for(const auto& c:vm->calls){
        assert(c.words==tier+1&&c.low==.12f&&c.high==.35f&&c.spell==bolts[tier]&&c.blame==c.target);
        assert(c.source!=c.aim&&Ready(nullptr,c.source,c.target)==1);
        assert(c.source->position.z-c.target->position.z==3500&&c.aim->position.z-c.target->position.z==96);
        assert(Find(c.source)->busy);
    }
    tick(20);assert(vm->calls.size()==2); // Busy targets never get a second concurrent stack.
    ++fixture::cases;
}
// Only the finished target starts its next burst; one slow stack cannot hold another back.
auto a=vm->calls[0],b=vm->calls[1];
Finished(nullptr,a.source);tick(.9);assert(vm->calls.size()==2);tick(.11);
assert(vm->calls.size()==3&&vm->calls.back().source==a.source&&Find(b.source)->busy);++fixture::cases;
// Random initial deadlines are independently drawn; duplicate references are ignored.
settings.initialMin=3;settings.initialMax=6;rng.seed(123);start(2);world->refs={&first,&second,&first};tick(0);
assert(targets.size()==2&&vm->calls.empty());
auto it=targets.begin();const double t1=it++->second.next,t2=it->second.next;
assert(t1!=t2&&t1>=fixture::now+3&&t1<=fixture::now+6&&t2>=fixture::now+3&&t2<=fixture::now+6);
tick(std::min(t1,t2)-fixture::now+.0001);assert(vm->calls.size()==1);
tick(std::max(t1,t2)-fixture::now+.0001);assert(vm->calls.size()==2);++fixture::cases;
settings.initialMin=settings.initialMax=0;
start(2);world->refs={&first,&second};tick(0);
first.hostile=false;second.dead=true;newcomer.position={30,0,0};world->refs.push_back(&newcomer);
const auto oldCalls=vm->calls;tick(.26);assert(targets.size()==1&&vm->calls.size()==3&&vm->calls.back().target==&newcomer);
for(auto c:oldCalls)assert(c.source->deleted&&c.aim->deleted&&Ready(nullptr,c.source,c.target)==-1);
first.hostile=true;second.dead=false;++fixture::cases;
// Recheck after discovery and immediately before a real launch.
start(2);world->refs={&newcomer};world->afterScan=[&]{newcomer.hostile=false;};tick(0);
assert(targets.empty()&&vm->calls.empty());world->afterScan={};newcomer.hostile=true;tick(.26);
auto c=vm->calls.back();fixture::Snapshot inherited;
RE::Projectile::LaunchData launch{c.source,c.spell,c.aim};
state.scaling.magnitude=4;state.scaling.area=2;
assert(Ready(nullptr,c.source,c.target)==1&&fixture::lastPrepared.magnitude==4);
assert(AcceptLaunch(launch,inherited)&&inherited.magnitude==4&&inherited.area==2);
for(int fault=0;fault<7;++fault){
    auto bad=launch;
    if(fault==0)newcomer.dead=true;
    if(fault==1)newcomer.hostile=false;
    if(fault==2)newcomer.position={0,0,201};
    if(fault==3)bad.shooter=&player;
    if(fault==4)bad.desiredTarget=&newcomer;
    if(fault==5)bad.spell=bolts[0];
    if(fault==6)active.flags.dispelled=true;
    assert(!AcceptLaunch(bad,inherited));
    newcomer.dead=false;newcomer.hostile=true;newcomer.position={30,0,0};active.flags.dispelled=false;
}
++fixture::cases;
// Grown 3D radius, including height, and player attribution are preserved.
start(1);state.scaling.area=2;settings.blameCaster=true;world->refs={&far,&high};tick(0);
assert(vm->calls.size()==2);for(auto call:vm->calls)assert(call.blame==&player);
settings.blameCaster=false;++fixture::cases;
// Interior pause preserves idle delay, and an already running stack sees paused status.
c=vm->calls[0];b=vm->calls[1];Finished(nullptr,b.source);auto due=Find(b.source)->next;
player.cell.interior=true;tick(10);assert(Find(b.source)->next==due+10&&Ready(nullptr,c.source,c.target)==0);
assert(!AcceptLaunch({c.source,c.spell,c.aim},inherited));
player.cell.interior=false;tick(.1);assert(vm->calls.size()==2&&Ready(nullptr,c.source,c.target)==1);
++fixture::cases;
// Allocation and dispatch failures retry without leaving a busy target.
start(0);world->refs={&first};RE::TESObjectREFR::failPlace=true;tick(0);assert(targets.empty());
RE::TESObjectREFR::failPlace=false;vm->fail=true;tick(.26);assert(targets.size()==1&&!targets.begin()->second.busy);
vm->fail=false;tick(.26);assert(vm->calls.size()==1&&targets.begin()->second.busy);++fixture::cases;
// v5 stores the storm snapshot and per-target source identities / idle remaining delay.
start(2);world->refs={&first,&second};tick(0);a=vm->calls[0];b=vm->calls[1];Finished(nullptr,b.source);
state.scaling.magnitude=3;
SKSE::SerializationInterface io;Save(&io);assert(io.records.size()==3&&io.records[0].version==5);
Reset(false);Load(&io);
assert(resume&&resume->remaining==88&&resume->scaling.magnitude==3&&restoredTargets.size()==2);
assert(Ready(nullptr,a.source,a.target)==0);
fixture::now+=100;FrameUpdate();
assert(Ready(nullptr,a.source,a.target)==1&&Find(a.source)->busy&&!Find(b.source)->busy);
assert(Find(b.source)->next>fixture::now+.9&&state.scaling.magnitude==3);
++fixture::cases;
// A stack returning before the first post-load frame must not strand a busy target.
SKSE::SerializationInterface early;Save(&early);Reset(false);Load(&early);Finished(nullptr,a.source);FrameUpdate();
assert(Find(a.source)&&!Find(a.source)->busy);++fixture::cases;
// Disabled/expired restore releases saved marker references even before rebinding.
SKSE::SerializationInterface disabled;Save(&disabled);Reset(false);Load(&disabled);
fixture::enabled=false;FrameUpdate();assert(targets.empty()&&restoredTargets.empty()&&a.source->deleted&&b.aim->deleted);
fixture::enabled=true;++fixture::cases;
// Missing restored actors still release their surviving persistent marker pair.
start(1);world->refs={&first};tick(0);c=vm->calls.back();
SKSE::SerializationInterface missing;Save(&missing);missing.unresolved.insert(first.id);
Reset(false);Load(&missing);RestoreTargets();
assert(targets.empty()&&c.source->deleted&&c.aim->deleted);++fixture::cases;
// v4 compatibility and exact v3 migration; invalid records never start a storm.
Saved valid;valid.tier=2;valid.remaining=42;valid.count=2;valid.pending={.1f,.3f};valid.scaling.magnitude=3;
SKSE::SerializationInterface previous;previous.WriteRecord(RecordID,4,valid);Load(&previous);
assert(resume&&resume->tier==2&&resume->remaining==42&&resume->scaling.magnitude==3);
Legacy legacy{0x14,3,42,2,{.1f,.3f},2,{2,3,4,5,6,7}};
SKSE::SerializationInterface old;old.WriteRecord(RecordID,3,legacy);Load(&old);
assert(resume&&resume->tier==2&&resume->scaling.collision==6&&std::abs(resume->scaling.density-3.4f)<.001f);
legacy.count=0;legacy.pending={-.01f,-.02f};SKSE::SerializationInterface completed;completed.WriteRecord(RecordID,3,legacy);Load(&completed);
assert(resume&&resume->count==0&&resume->pending[0]==0&&resume->pending[1]==0);++fixture::cases;
for(int fault=0;fault<6;++fault){
    Saved bad=valid;
    if(fault==0)bad.tier=3;if(fault==1)bad.count=3;if(fault==2)bad.remaining=-1;
    if(fault==3)bad.pending[0]=-.1f;if(fault==4)bad.scaling.magnitude=std::numeric_limits<float>::infinity();
    if(fault==5)bad.scaling.area=.5f;
    SKSE::SerializationInterface invalid;invalid.WriteRecord(RecordID,4,bad);Load(&invalid);assert(!resume);
}
legacy.caster=0x15;SKSE::SerializationInterface wrong;wrong.WriteRecord(RecordID,3,legacy);Load(&wrong);assert(!resume);
SKSE::SerializationInterface truncated;truncated.WriteRecord(RecordID,4,valid);truncated.shortRead=true;Load(&truncated);assert(!resume);
SKSE::SerializationInterface unknown;std::array<std::byte,1500> junk{};unknown.WriteRecord(123,1,junk);
unknown.WriteRecord(RecordID,4,valid);Load(&unknown);assert(resume&&resume->remaining==42);++fixture::cases;
// Weather stays owned by the original effect, extends on recast, ends on expiry.
start(2);world->refs={&first};FrameUpdate();c=vm->calls.back();
auto w=WeatherEffects(player);assert(w.size()==1);auto oldCasts=fixture::casts;MaintainWeather(player,200);
assert(fixture::casts==oldCasts&&w[0]->duration-w[0]->elapsedSeconds==200);
BeforeShout(player);assert(active.flags.dispelled&&Ready(nullptr,c.source,c.target)==-1&&WeatherEffects(player).size()==1);
active.flags.dispelled=false;active.elapsedSeconds=0;active.duration=200;active.usUniqueID++;FrameUpdate();
assert(fixture::casts==oldCasts);active.elapsedSeconds=200;FrameUpdate();assert(!identity&&WeatherEffects(player).empty());++fixture::cases;
// Old saves without the engine timer recreate only its remaining lifetime.
Reset();player.effects.clear();valid.tier=1;valid.remaining=23;valid.scaling.magnitude=4;resume=valid;FrameUpdate();
std::uint32_t tier{};auto* restored=Timer(player,tier);
assert(restored&&tier==1&&restored->duration-restored->elapsedSeconds==23&&state.scaling.magnitude==4);++fixture::cases;
// Indoors and exception weather delay weather application, not the storm lifetime.
start(0);player.cell.interior=true;oldCasts=fixture::casts;FrameUpdate();assert(fixture::casts==oldCasts);
active.elapsedSeconds+=10;fixture::now+=10;FrameUpdate();assert(state.remaining==78&&vm->calls.empty());
player.cell.interior=false;RE::BGSListForm blocked;blocked.excluded=true;exceptions=&blocked;FrameUpdate();
assert(fixture::casts==oldCasts);blocked.excluded=false;FrameUpdate();assert(fixture::casts==oldCasts+1);exceptions=nullptr;++fixture::cases;
// Menu/loading paths never create bursts, and preload does not delete old-world refs.
c=vm->calls.back();fixture::paused=true;fixture::now+=100;FrameUpdate();assert(Ready(nullptr,c.source,c.target)==0);
fixture::paused=false;RE::TESObjectREFR* oldSource=c.source;
SKSE::MessagingInterface::Message pre{SKSE::MessagingInterface::kPreLoadGame};HandleMessage(&pre);
assert(loading&&targets.empty()&&!oldSource->deleted&&Ready(nullptr,c.source,c.target)==0);
SKSE::MessagingInterface::Message post{SKSE::MessagingInterface::kPostLoadGame};HandleMessage(&post);
assert(!loading&&Ready(nullptr,c.source,c.target)==-1);++fixture::cases;
Reset();std::cout<<"{\"passed\":true,\"cases\":"<<fixture::cases<<"}";
}
