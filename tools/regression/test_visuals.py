"""Compile the actual cone and projectile-visual modules against small scene stubs.

Checks transforms, material changes, hook ordering and growth without Skyrim.
Run from an x64 Native Tools shell: python tools/regression/test_visuals.py BUILD_DIR
The stubs do not simulate particles, Havok or renderer visibility.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(sys.argv[1]).resolve() / "visuals"
OUT.mkdir(parents=True, exist_ok=True)

def body(path, signature):
    text = (ROOT / path).read_text("utf8")
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for pos in range(opening, len(text)):
        depth += (text[pos] == "{") - (text[pos] == "}")
        if not depth:
            return text[start:pos+1]
    raise ValueError(signature)

sources = {name: (ROOT / "src" / name).read_text("utf8") for name in ("true_cone.cpp", "projectile_visuals.cpp")}
visual_density = body("src/progression.cpp", "float VisualDensity(")
prefix = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
''' + f'#include "{(ROOT / "src/progression.h").as_posix()}"\n' + r'''
namespace fixture {
std::map<std::string,bool> toggles;
std::map<std::string,float> numbers;
bool enabled=true,owned=true,authorized=true;
double now=0;
ysmir::progression::ScalingSnapshot snapshot;
float densityAt4x=2.2f;
int nextID=1,nativeCalls=0;
}
namespace RE {
template<class T> struct NiPointer {
    T* p{};
    NiPointer()=default; NiPointer(T* value):p(value){}
    T* get()const{return p;} T* operator->()const{return p;}
    explicit operator bool()const{return p!=nullptr;}
    void reset(T* value=nullptr){p=value;}
};
struct NiPoint3 {
    float x{},y{},z{};
    NiPoint3 operator+(NiPoint3 b)const{return {x+b.x,y+b.y,z+b.z};}
    NiPoint3 operator-(NiPoint3 b)const{return {x-b.x,y-b.y,z-b.z};}
    NiPoint3 operator*(float f)const{return {x*f,y*f,z*f};}
};
struct NiMatrix3 {
    std::array<float,9> m{1,0,0,0,1,0,0,0,1};
    NiPoint3 operator*(NiPoint3 p)const{return {m[0]*p.x+m[1]*p.y+m[2]*p.z,m[3]*p.x+m[4]*p.y+m[5]*p.z,m[6]*p.x+m[7]*p.y+m[8]*p.z};}
    NiMatrix3 Transpose()const{return {{m[0],m[3],m[6],m[1],m[4],m[7],m[2],m[5],m[8]}};}
};
struct NiTransform {
    NiMatrix3 rotate;
    NiPoint3 translate;
    float scale{1};
    NiPoint3 operator*(NiPoint3 p)const{return translate+rotate*(p*scale);}
    NiTransform Invert()const {NiTransform v;v.scale=1/scale;v.rotate=rotate.Transpose();v.translate=v.rotate*(translate*(-v.scale));return v;}
};
struct NiUpdateData {};
struct BSGeometry;
struct NiAVObject {
    enum class Flag {kFixedBound,kForceUpdate,kAlwaysDraw};
    struct Flags {unsigned bits{};template<class... A>void set(A... a){((bits|=1u<<static_cast<unsigned>(a)),...);}} flags;
    NiTransform local,world;
    NiAVObject* parent{};
    std::vector<NiAVObject*> children;
    struct Bound {NiPoint3 center;float radius{};} worldBound;
    bool culled=true;
    virtual ~NiAVObject()=default;
    virtual BSGeometry* AsGeometry(){return nullptr;}
    Flags& GetFlags(){return flags;}
    void SetAppCulled(bool v){culled=v;}
    void UpdateTransformAndBounds(NiUpdateData&){world=local;if(parent){world.translate=parent->world*local.translate;world.scale*=parent->world.scale;}}
};
struct NiAlphaProperty {
    enum class AlphaFunction {kZero,kOne,kOther};
    bool blending{};AlphaFunction source{AlphaFunction::kOther},dest{AlphaFunction::kOther};
    bool GetAlphaBlending()const{return blending;}
    AlphaFunction GetDestBlendMode()const{return dest;}AlphaFunction GetSrcBlendMode()const{return source;}
};
struct BSShaderMaterial {enum class Type{kEffect,kOther};virtual ~BSShaderMaterial()=default;float alpha{.5f};Type type{Type::kOther};};
struct BSEffectShaderMaterial:BSShaderMaterial {float baseColorScale{2};BSEffectShaderMaterial(){type=Type::kEffect;}};
struct BSShaderProperty {
    virtual ~BSShaderProperty()=default;
    BSShaderMaterial* material{};
    struct EffectData {NiAlphaProperty::AlphaFunction srcBlend,destBlend;}* effectData{};
    float QMaterialAlpha(){return material->alpha;}void SetMaterialAlpha(float v){material->alpha=v;}
    BSShaderMaterial::Type GetMaterialType(){return material->type;}
};
struct BSLightingShaderProperty:BSShaderProperty {float emissiveMult{3};};
struct BSGeometry:NiAVObject {
    struct Runtime {NiPointer<BSShaderProperty> shaderProperty;NiPointer<NiAlphaProperty> alphaProperty;} runtime;
    BSGeometry* AsGeometry()override{return this;}Runtime& GetGeometryRuntimeData(){return runtime;}
};
namespace BSVisit {
enum class BSVisitControl{kContinue};
template<class F>void TraverseScenegraphObjects(NiAVObject* o,F f){f(o);for(auto* child:o->children)TraverseScenegraphObjects(child,f);}
}
struct SpellItem {template<class T>T* As(){return static_cast<T*>(this);}};
struct BGSProjectile {
    bool cone{};std::string editor="Shout",model="vanilla.nif";
    bool IsCone(){return cone;}const char* GetFormEditorID(){return editor.c_str();}const char* GetModel(){return model.c_str();}
};
struct Projectile {
    struct Runtime {float distanceMoved{};SpellItem* spell{};} runtime;
    BGSProjectile base;NiAVObject* root{};NiPoint3 position;
    int id=fixture::nextID++;bool alive=true;
    BGSProjectile* GetProjectileBase(){return &base;}Runtime& GetProjectileRuntimeData(){return runtime;}
    NiAVObject* Get3D(){return root;}NiPoint3 GetPosition(){return position;}
};
struct ConeProjectile:Projectile {
    struct ConeData {float initialCollisionSphereRadius{16},coneAngleTangent{.25f};void* collisionShape{};} cone;
    ConeProjectile(){base.cone=true;}ConeData& GetConeRuntimeData(){return cone;}
};
struct ProjectileHandle {
    Projectile* p{};ProjectileHandle()=default;ProjectileHandle(Projectile* value):p(value){}
    std::uint32_t native_handle()const{return p?p->id:0;}
    NiPointer<Projectile> get()const{return {p&&p->alive?p:nullptr};}
};
struct Address {std::uintptr_t address()const{return 0;}};
inline Address VTABLE_Projectile[1],VTABLE_BeamProjectile[1],VTABLE_MissileProjectile[1],VTABLE_ConeProjectile[1];
}
template<class T,class U>T netimmerse_cast(U* v){return dynamic_cast<T>(v);}
namespace REL {template<class T>struct Relocation {Relocation(std::uintptr_t){}Relocation(RE::Address){}template<class F>std::uintptr_t write_vfunc(int,F){return 0;}};}
namespace ysmir::config {
bool Bool(const char*,const char* key,bool fallback){auto f=fixture::toggles.find(key);return f==fixture::toggles.end()?fallback:f->second;}
float Float(const char*,const char* key,float fallback){auto f=fixture::numbers.find(key);return f==fixture::numbers.end()?fallback:f->second;}
std::string String(const char*,const char*,const char* fallback){return fallback;}
}
namespace ysmir::progression {using fixture::densityAt4x;bool Enabled(){return fixture::enabled;}double Now(){return fixture::now;}
''' + visual_density + r'''
}
namespace ysmir::core {
bool IsPlayerOwnedProjectile(RE::Projectile*){return fixture::owned;}
bool IsPlayerVoiceSpellRuntimeAuthorized(RE::SpellItem* p){return p&&fixture::authorized;}
progression::ScalingSnapshot ScalingForProjectile(RE::Projectile*){return fixture::snapshot;}
void TrackProjectile(const RE::ProjectileHandle&,const progression::ScalingSnapshot&){}
}
'''
main = r'''
namespace {
std::vector<std::string> passed;
bool Close(float a,float b){return std::abs(a-b)<=2e-5f*std::max({1.f,std::abs(a),std::abs(b)});}
void Equal(RE::NiPoint3 a,RE::NiPoint3 b){assert(Close(a.x,b.x)&&Close(a.y,b.y)&&Close(a.z,b.z));}
struct Scene {
    RE::NiAVObject root,parent;
    RE::BSGeometry mesh;
    RE::BSShaderMaterial material;
    RE::BSShaderProperty shader;
    RE::SpellItem spell;
    RE::ConeProjectile cone;
    Scene(){cone.root=&root;cone.runtime.spell=&spell;root.children.push_back(&mesh);shader.material=&material;mesh.runtime.shaderProperty.reset(&shader);}
    ~Scene(){ysmir::projectile_visuals::Clear();}
};
void Reset(){ysmir::projectile_visuals::Clear();fixture::toggles.clear();fixture::numbers.clear();fixture::snapshot={};fixture::enabled=fixture::owned=fixture::authorized=true;ysmir::true_cone::Initialize();ysmir::projectile_visuals::SetStormBase(nullptr);}
void Pass(const char* name){passed.emplace_back(name);}
}
int main(){
using namespace ysmir;
projectile_visuals::Initialize();Reset();
{
    Scene s;s.root.local.scale=1.5f;s.root.local.translate={10,20,30};s.root.local.rotate={{0,-1,0,0,0,-1,1,0,0}};
    s.parent.world.translate={100,200,300};s.parent.world.scale=2;s.parent.world.rotate={{0,0,1,1,0,0,0,1,0}};
    s.root.parent=&s.parent;s.cone.position={130,260,310};
    const auto anchor=s.root.local.Invert()*(s.parent.world.Invert()*s.cone.position);
    Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,1.5f));Equal(s.root.local.translate,{10,20,30});
    assert(s.material.alpha==.5f);Pass("birthPreservesScalePositionAndDensity");
    s.cone.runtime.distanceMoved=128;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,3.75f));
    Equal(s.parent.world*(s.root.local*anchor),s.cone.position);Pass("smoothstepMidpointAndRotatedParentAnchor");
    s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,6));
    s.cone.position={300,400,500};s.cone.runtime.distanceMoved=512;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,6));Equal(s.parent.world*(s.root.local*anchor),s.cone.position);
    Pass("endOfTransitionAndMovingAnchorWithoutHeightOffset");
    for(int i=0;i<120;++i)Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,6));Equal(s.parent.world*(s.root.local*anchor),s.cone.position);
    assert(Close(s.material.alpha,1-std::pow(.5f,2.2f)));Pass("repeatedUpdatesDoNotAccumulateSizeOrDensity");
    s.root.local.scale=2;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,8));
    Equal(s.parent.world*(s.root.local*anchor),s.cone.position);Pass("nativeScaleControllerBecomesNewBaseline");
    RE::NiAVObject replacement;replacement.local.scale=3;replacement.local.translate={5,6,7};
    const auto otherAnchor=replacement.local.Invert()*s.cone.position;
    s.cone.root=&replacement;Apply(&s.cone,nullptr);assert(Close(replacement.local.scale,12));
    Equal(replacement.local*otherAnchor,s.cone.position);Pass("newRootGetsItsOwnOriginalAnchorAndScale");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;fixture::snapshot.collision=100;fixture::snapshot.vfx=99;
    Apply(&s.cone,nullptr);s.cone.runtime.distanceMoved=128;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,4.5f));s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,10));assert(s.cone.cone.initialCollisionSphereRadius==16&&s.cone.cone.coneAngleTangent==.25f);
    Pass("nativeGeometryRatioAndDistanceGrowthWithoutDoubleScaling");
    fixture::snapshot.distance=20;fixture::snapshot.collision=1;fixture::snapshot.vfx=1;
    Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,10));Pass("projectileKeepsItsCastSnapshot");
    float previous=1;for(int d=0;d<=16384;++d){s.cone.runtime.distanceMoved=static_cast<float>(d);Apply(&s.cone,nullptr);assert(s.root.local.scale>=previous&&s.root.local.scale<=16);previous=s.root.local.scale;}
    assert(previous>15);Pass("distanceCurveIsMonotoneAndDoesNotCapSoulsAtFourTimes");
    const auto saved=s.cone.cone;s.cone.runtime.distanceMoved=256;s.cone.cone.collisionShape=&s;
    true_cone::VisualScale(&s.cone,fixture::snapshot);assert(s.cone.cone.collisionShape==&s&&s.cone.cone.initialCollisionSphereRadius==saved.initialCollisionSphereRadius&&s.cone.cone.coneAngleTangent==saved.coneAngleTangent);
    Pass("collisionParametersAndShapePointerRemainUntouched");
}
Reset();
{
    Scene s;auto& c=s.cone;auto& k=fixture::snapshot;k.distance=6;
    c.runtime.distanceMoved=256;c.cone.coneAngleTangent=0;assert(true_cone::VisualScale(&c,k)==4);
    c.cone.initialCollisionSphereRadius=0;assert(true_cone::VisualScale(&c,k)==4);
    c.cone.coneAngleTangent=.25f;c.runtime.distanceMoved=0;assert(true_cone::VisualScale(&c,k)==1);
    c.runtime.distanceMoved=.001f;assert(Close(true_cone::VisualScale(&c,k),1));
    c.runtime.distanceMoved=256;assert(true_cone::VisualScale(&c,k)==24);Pass("zeroRadiusAndZeroAngleStayFiniteAndContinuousAtBirth");
    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(float d:{-1.f,nan,inf}){c.runtime.distanceMoved=d;assert(true_cone::VisualScale(&c,k)==1);}
    c.runtime.distanceMoved=256;
    for(float r:{-1.f,nan,inf}){c.cone.initialCollisionSphereRadius=r;assert(true_cone::VisualScale(&c,k)==1);}
    c.cone.initialCollisionSphereRadius=16;
    for(float t:{-1.f,nan,inf}){c.cone.coneAngleTangent=t;assert(true_cone::VisualScale(&c,k)==1);}
    c.cone.coneAngleTangent=.25f;
    for(float d:{0.f,nan,inf}){k.distance=d;assert(true_cone::VisualScale(&c,k)==1);}
    Pass("invalidRuntimeGeometryNeverReachesTheSceneAsNaN");
}
Reset();
{
    Scene s;s.cone.runtime.distanceMoved=128;auto& k=fixture::snapshot;
    fixture::numbers["fBaseVisualScale"]=2;fixture::numbers["fVisualTransitionDistance"]=512;true_cone::Initialize();
    s.cone.runtime.distanceMoved=256;assert(Close(true_cone::VisualScale(&s.cone,k),1.5f));
    fixture::numbers["fBaseVisualScale"]=1;true_cone::Initialize();assert(true_cone::VisualScale(&s.cone,k)==1);
    Pass("calibrationAndTransitionAreSimpleConfigValues");
}
Reset();
{
    Scene s;auto value=[&](float d){s.cone.runtime.distanceMoved=d;return true_cone::VisualScale(&s.cone,fixture::snapshot);};
    assert(std::abs((value(.1f)-value(0))/.1f)<.0001f);
    assert(std::abs((value(256)-value(255.9f))/.1f)<.0001f);
    assert(value(256)==value(256.1f));Pass("smoothstepHasNoSlopeJumpAtFixedTargetEndpoints");
}
for(const char* key:{"Enabled","PatchVisuals","bScaleVFX"}){
    Reset();Scene s;fixture::toggles[key]=false;true_cone::Initialize();s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(s.root.local.scale==1&&s.material.alpha==.5f&&s.cone.cone.initialCollisionSphereRadius==16);
}
Pass("coneAndGlobalVisualSwitchesSkipBothSizeAndDensity");
Reset();
{
    Scene s;fixture::toggles["PatchRuntimeCollision"]=false;true_cone::Initialize();s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,4)&&s.cone.cone.initialCollisionSphereRadius==16);Pass("legacyCollisionKeyCannotRestoreCollisionGrowth");
}
for(int mode=0;mode<4;++mode){
    Reset();Scene s;s.cone.runtime.distanceMoved=256;
    if(mode==0)fixture::enabled=false;if(mode==1)fixture::owned=false;if(mode==2)fixture::authorized=false;if(mode==3)s.cone.id=0;
    Apply(&s.cone,nullptr);assert(s.root.local.scale==1&&s.material.alpha==.5f);
}
Pass("disabledForeignUnapprovedAndUnassignedProjectilesAreUntouched");
Reset();
{
    Scene s;s.cone.base.editor="YSMIR_Cyclone";s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,4)&&s.material.alpha==.5f);Pass("existingDensityExclusionStillScalesGeometry");
}
Reset();
{
    Scene s;RE::BSEffectShaderMaterial material;RE::NiAlphaProperty alpha;alpha.blending=true;alpha.dest=RE::NiAlphaProperty::AlphaFunction::kOne;
    s.shader.material=&material;s.mesh.runtime.alphaProperty.reset(&alpha);s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(material.baseColorScale,4.4f)&&material.alpha==.5f);
    RE::BSLightingShaderProperty light;RE::BSShaderMaterial other;light.material=&other;s.mesh.runtime.shaderProperty.reset(&light);Apply(&s.cone,nullptr);
    assert(Close(light.emissiveMult,6.6f)&&other.alpha==.5f);assert(material.baseColorScale==2);
    light.emissiveMult=5;Apply(&s.cone,nullptr);assert(Close(light.emissiveMult,11));
    projectile_visuals::Clear();assert(light.emissiveMult==5);Pass("additiveMaterialSwapControllerAndCleanupPreserved");
}
Reset();
{
    Scene s;RE::BSGeometry shared;s.root.children.push_back(&shared);shared.runtime.shaderProperty.reset(&s.shader);
    s.cone.runtime.distanceMoved=128;Apply(&s.cone,nullptr);assert(Close(s.material.alpha,1-std::pow(.5f,1.6f)));
    s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);assert(Close(s.material.alpha,1-std::pow(.5f,2.2f)));
    projectile_visuals::Clear();assert(Close(s.material.alpha,.5f));Pass("sharedMaterialIsNotCompensatedTwiceAndRestoresBaseline");
}
Reset();
{
    Scene s;s.cone.base.cone=false;s.root.local.scale=2;fixture::snapshot.vfx=6;fixture::snapshot.distance=4;
    Apply(&s.cone,nullptr);assert(s.root.local.scale==12);s.cone.runtime.distanceMoved=2000;Apply(&s.cone,nullptr);
    assert(s.root.local.scale==12&&s.material.alpha==.5f);Pass("nonConeVisualGrowthRemainsImmediateAndIndependent");
}
Reset();
{
    Scene s;s.cone.base.cone=false;projectile_visuals::SetStormBase(&s.cone.base);fixture::owned=fixture::authorized=false;
    Apply(&s.cone,nullptr);assert(s.root.local.scale==1);
    progression::ScalingSnapshot storm;storm.vfx=3;storm.distance=6;projectile_visuals::TrackStorm(RE::ProjectileHandle(&s.cone),storm);
    assert(s.root.local.scale==3&&s.root.worldBound.radius>=54000&&s.mesh.worldBound.radius>=54000);
    assert(s.root.flags.bits==7&&!s.root.culled&&s.material.alpha==.5f);Pass("trackedStormScaleBoundsAndAlwaysDrawAreUnchanged");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;fixture::nativeCalls=0;
    ConeHook::loaded=[](RE::ConeProjectile* p){++fixture::nativeCalls;p->runtime.distanceMoved=0;p->cone.initialCollisionSphereRadius=16;};
    ConeHook::update=[](RE::ConeProjectile* p,float){assert(p->cone.initialCollisionSphereRadius==16);++fixture::nativeCalls;p->runtime.distanceMoved=256;};
    ConeHook::Loaded(&s.cone);assert(s.root.local.scale==1&&fixture::nativeCalls==1);
    ConeHook::Update(&s.cone,.016f);assert(Close(s.root.local.scale,10)&&fixture::nativeCalls==2&&s.cone.cone.initialCollisionSphereRadius==16);
    Pass("loadAndUpdateHooksReadAfterNativeWorkWithoutPhysicsPrepass");
    s.cone.alive=false;fixture::now=10;projectile_visuals::FrameUpdate();assert(states.empty()&&materials.empty());
    Pass("expiredProjectileStateAndMaterialBindingsArePruned");
}
std::cout<<"{\"passed\":true,\"cases\":"<<passed.size()<<",\"scenarios\":[";
for(std::size_t i=0;i<passed.size();++i)std::cout<<(i?",":"")<<'"'<<passed[i]<<'"';
std::cout<<"]}";
}
'''

compiled_sources = "\n".join("\n".join(line for line in source.splitlines() if line not in (
    '#include "modules.h"', '#include "core.h"', '#include <RE/B/BSVisit.h>')) for source in sources.values())
(OUT / "fixture.cpp").write_text(prefix + compiled_sources + main, "utf8")
built = subprocess.run(["cl.exe", "/nologo", "/std:c++20", "/EHsc", "/Od", "/W4", "/MD", "fixture.cpp", "/Fe:fixture.exe", "/Fo:fixture.obj"], cwd=OUT, capture_output=True, text=True)
(OUT / "build.log").write_text(built.stdout+built.stderr, "utf8")
if built.returncode:
    print(built.stdout+built.stderr)
built.check_returncode()
run = subprocess.run([str(OUT / "fixture.exe")], cwd=OUT, capture_output=True, text=True)
if run.returncode:
    print(run.stdout+run.stderr)
run.check_returncode()
result = json.loads(run.stdout)
result["sourceSha256"] = {name: hashlib.sha256(source.encode()).hexdigest() for name, source in sources.items()}
result["visualDensityBodySha256"] = hashlib.sha256(visual_density.encode()).hexdigest()
result["boundary"] = "Complete production cone/visual modules with scene, material and engine-service stubs; no renderer, particles or Havok simulation"
(OUT / "results.json").write_text(json.dumps(result, indent=2)+"\n", "utf8")
print(json.dumps(result, indent=2))
