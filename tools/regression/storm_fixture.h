// Controlled engine services for compiling the unchanged production storm adapter.
#include <unordered_set>
namespace RE {
using FormID=std::uint32_t;
template<class T> using NiPointer=std::shared_ptr<T>;
template<class T> using BSTSmartPointer=std::shared_ptr<T>;
struct TESForm {
    inline static FormID next=100;
    inline static std::unordered_map<FormID,TESForm*> forms;
    FormID id=next++;
    TESForm(){forms[id]=this;}
    virtual ~TESForm(){forms.erase(id);}
    FormID GetFormID(){return id;}
    template<class T> static T* LookupByID(FormID id){auto it=forms.find(id);return it==forms.end()?nullptr:dynamic_cast<T*>(it->second);}
};
struct NiPoint3 {
    float x{},y{},z{};
    float GetDistance(const NiPoint3& p)const{return std::sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z));}
};
struct TESObjectCELL { bool interior{}; bool IsInteriorCell(){return interior;} };
struct TESBoundObject : TESForm {};
struct EffectSetting : TESForm {};
struct Effect { EffectSetting* baseEffect{}; struct { float magnitude{}; std::uint32_t duration{}; } effectItem; };
struct SpellItem : TESForm { std::vector<Effect*> effects; };
struct ActiveEffect;
struct Actor;
struct TESObjectREFR;
template<class T> struct Handle {
    T* ptr{};
    std::uint32_t native_handle()const{return ptr?ptr->id:0;}
    NiPointer<T> get()const{return ptr&&!ptr->deleted?NiPointer<T>(ptr,[](T*){}):nullptr;}
};
using ObjectRefHandle=Handle<TESObjectREFR>;
using ActorHandle=Handle<Actor>;
struct TESObjectREFR : TESForm {
    bool deleted{},disabled{};
    TESBoundObject* base{};
    NiPoint3 position;
    inline static std::vector<std::unique_ptr<TESObjectREFR>> placed;
    inline static bool failPlace=false,visiting=false;
    template<class T> T* As(){return dynamic_cast<T*>(this);}
    ObjectRefHandle GetHandle(){return {this};}
    TESBoundObject* GetBaseObject(){return base;}
    NiPoint3 GetPosition(){return position;}
    void SetPosition(NiPoint3 p){position=p;}
    void Disable(){disabled=true;}
    void SetDelete(bool v){deleted=v;}
    NiPointer<TESObjectREFR> PlaceObjectAtMe(TESBoundObject* b,bool persist){
        assert(persist&&!visiting);
        if(failPlace)return {};
        auto p=std::make_unique<TESObjectREFR>();p->base=b;
        auto out=NiPointer<TESObjectREFR>(p.get(),[](TESObjectREFR*){});
        placed.push_back(std::move(p));return out;
    }
};
struct Actor : TESObjectREFR {
    bool dead{},loaded=true,hostile=true,hasCell=true;
    TESObjectCELL cell;
    std::vector<ActiveEffect*> effects;
    bool IsDead(){return dead;} bool Is3DLoaded(){return loaded;} bool IsHostileToActor(Actor*){return hostile;}
    TESObjectCELL* GetParentCell(){return hasCell?&cell:nullptr;}
    ActorHandle GetHandle(){return {this};}
    auto* GetActiveEffectList(){return &effects;}
};
struct PlayerCharacter : Actor { static PlayerCharacter* GetSingleton(){static PlayerCharacter p;return &p;} };
struct ActiveEffect {
    enum class Flag{kDispelled};
    struct { bool dispelled{}; bool any(Flag)const{return dispelled;} } flags;
    float duration{},elapsedSeconds{}; SpellItem* spell{}; EffectSetting* base{};
    std::uint16_t usUniqueID=1;
    NiPointer<Actor> GetCasterActor(){return {PlayerCharacter::GetSingleton(),[](Actor*){}};}
    EffectSetting* GetBaseObject(){return base;} void Dispel(bool){flags.dispelled=true;}
};
struct ProjectileHandle { int id{}; explicit operator bool()const{return id!=0;} };
struct Projectile { struct LaunchData {TESObjectREFR* shooter{};SpellItem* spell{};TESObjectREFR* desiredTarget{};}; };
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
    template<class F> void ForEachReferenceInRange(Actor*,float,F&& f){
        TESObjectREFR::visiting=true;for(auto* p:refs)f(p);TESObjectREFR::visiting=false;if(afterScan)afterScan();
    }
};
struct TESDataHandler {
    static TESDataHandler* GetSingleton(){static TESDataHandler t;return &t;}
    template<class T> T* LookupForm(std::uint32_t,const char*){return nullptr;}
};
struct StaticFunctionTag {};
struct Args { SpellItem* spell;TESObjectREFR *source,*aim;Actor *target,*blame;int words;float low,high,retry; };
Args* MakeFunctionArguments(SpellItem* s,TESObjectREFR* f,TESObjectREFR* a,Actor* t,Actor* b,int n,float l,float h,float r){
    return new Args{s,f,a,t,b,n,l,h,r};
}
namespace BSScript {
struct IStackCallbackFunctor {};
struct IVirtualMachine { template<class T>void RegisterFunction(const char*,const char*,T){} };
namespace Internal {
struct VirtualMachine : IVirtualMachine {
    std::vector<Args> calls; bool fail=false;
    static VirtualMachine* GetSingleton(){static VirtualMachine vm;return &vm;}
    bool DispatchStaticCall(const char* script,const char* fn,Args* args,BSTSmartPointer<IStackCallbackFunctor>& cb){
        assert(std::string(script)=="YsmirStormScript"&&std::string(fn)=="Burst"&&!cb);
        if(!fail)calls.push_back(*args);delete args;return !fail;
    }
};
}
}
}
namespace fixture {
using Snapshot=ysmir::progression::ScalingSnapshot;
int casts=0,prepared=0; Snapshot snapshot,lastPrepared;
std::vector<std::unique_ptr<RE::ActiveEffect>> allocated;
}
namespace SKSE {
namespace log { template<class...T> void warn(const char*,T&&...){} }
struct PapyrusInterface {
    template<class F>bool Register(F f){return f(RE::BSScript::Internal::VirtualMachine::GetSingleton());}
};
PapyrusInterface* GetPapyrusInterface(){static PapyrusInterface p;return &p;}
struct SerializationInterface {
    struct Record { std::uint32_t type,version; std::vector<std::byte> bytes; };
    std::vector<Record> records; std::size_t index{},offset{}; bool resolve=true,shortRead=false;
    std::unordered_set<RE::FormID> unresolved;
    void SetUniqueID(std::uint32_t){} void SetSaveCallback(void(*)(SerializationInterface*)){}
    void SetLoadCallback(void(*)(SerializationInterface*)){} void SetRevertCallback(void(*)(SerializationInterface*)){}
    template<class T> void WriteRecord(std::uint32_t type,std::uint32_t version,const T& value){
        Record r{type,version,std::vector<std::byte>(sizeof(T))};std::memcpy(r.bytes.data(),&value,sizeof(T));records.push_back(r);
    }
    bool GetNextRecordInfo(std::uint32_t& t,std::uint32_t& v,std::uint32_t& n){
        if(index==records.size())return false;
        auto& r=records[index++];t=r.type;v=r.version;n=static_cast<std::uint32_t>(r.bytes.size());offset=0;return true;
    }
    std::uint32_t ReadRecordData(void* p,std::uint32_t n){
        auto& b=records[index-1].bytes;auto count=std::min(n,static_cast<std::uint32_t>(b.size()-offset));
        if(shortRead&&count)--count;std::memcpy(p,b.data()+offset,count);offset+=count;return count;
    }
    bool ResolveFormID(RE::FormID in,RE::FormID& out){out=in;return resolve&&!unresolved.contains(in);}
};
SerializationInterface* GetSerializationInterface(){static SerializationInterface io;return &io;}
}
namespace ysmir::core {
bool IsPlayerActor(RE::Actor* p){return p==RE::PlayerCharacter::GetSingleton();}
RE::TESShout* PrivateShout(std::uint32_t){return nullptr;}
progression::ScalingSnapshot ScalingForSpell(RE::SpellItem*){return fixture::snapshot;}
void PrepareFeatureSpell(RE::SpellItem*,const progression::ScalingSnapshot& s){++fixture::prepared;fixture::lastPrepared=s;}
bool CastFeatureSpell(RE::SpellItem* spell,RE::Actor& player,RE::Actor*,const progression::ScalingSnapshot& s){
    if(!spell)return false;++fixture::casts;
    for(auto* item:spell->effects){
        auto e=std::make_unique<RE::ActiveEffect>();e->spell=spell;e->base=item->baseEffect;
        e->duration=static_cast<float>(item->effectItem.duration)*s.duration;
        player.effects.push_back(e.get());fixture::allocated.push_back(std::move(e));
    }
    return true;
}
}
namespace ysmir::projectile_visuals { void SetStormBase(RE::BGSProjectile*){} void Clear(){} }
namespace ysmir::storm_call { bool Available(); }
