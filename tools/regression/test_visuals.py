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
#include <functional>
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
struct NiPoint2 {float x{1},y{1};};
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
    NiMatrix3 operator*(const NiMatrix3& b)const{
        NiMatrix3 result;result.m.fill(0);
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)result.m[i*3+j]+=m[i*3+k]*b.m[k*3+j];
        return result;
    }
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
    std::string name;
    NiAVObject* parent{};
    std::vector<NiAVObject*> children;
    struct Bound {NiPoint3 center;float radius{};} worldBound;
    bool culled=true;
    virtual ~NiAVObject()=default;
    virtual BSGeometry* AsGeometry(){return nullptr;}
    virtual NiAVObject* AsNode(){return this;}
    Flags& GetFlags(){return flags;}
    void SetAppCulled(bool v){culled=v;}
    void UpdateTransformAndBounds(NiUpdateData& data){
        world=local;
        if(parent){world.translate=parent->world*local.translate;world.rotate=parent->world.rotate*local.rotate;world.scale*=parent->world.scale;}
        for(auto* child:children)child->UpdateTransformAndBounds(data);
    }
};
struct NiAlphaProperty {
    enum class AlphaFunction {kZero,kOne,kOther};
    bool blending{};AlphaFunction source{AlphaFunction::kOther},dest{AlphaFunction::kOther};
    bool GetAlphaBlending()const{return blending;}
    AlphaFunction GetDestBlendMode()const{return dest;}AlphaFunction GetSrcBlendMode()const{return source;}
};
namespace BSGraphics {enum class TextureAddressMode{kClampSClampT,kClampSWrapT,kWrapSClampT,kWrapSWrapT};}
struct BSShaderMaterial {enum class Type{kEffect,kOther};virtual ~BSShaderMaterial()=default;float alpha{.5f};Type type{Type::kOther};NiPoint2 texCoordScale[2];Type GetType(){return type;}};
struct BSEffectShaderMaterial:BSShaderMaterial {float baseColorScale{2};std::string sourceTexturePath="textures/effects/VaporTile01.dds";BSGraphics::TextureAddressMode effectClampMode=BSGraphics::TextureAddressMode::kWrapSWrapT;BSEffectShaderMaterial(){type=Type::kEffect;}};
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
    NiAVObject* AsNode()override{return nullptr;}
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
    struct Runtime {float distanceMoved{},range{512};SpellItem* spell{};} runtime;
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
namespace SKSE::log {template<class... A>void info(const char*,A&&...) {}}
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
void Attach(RE::NiAVObject& parent,RE::NiAVObject& child){parent.children.push_back(&child);child.parent=&parent;}
struct Scene {
    RE::NiAVObject root,parent,refract,wisps,trail,nested,decoration;
    RE::BSGeometry mesh,other;
    RE::BSEffectShaderMaterial material;
    RE::BSShaderProperty shader;
    RE::SpellItem spell;
    RE::ConeProjectile cone;
    Scene(bool wave=false){
        cone.root=&root;cone.runtime.spell=&spell;shader.material=&material;mesh.runtime.shaderProperty.reset(&shader);
        if(wave){
            cone.base.model="Magic\\VoicePushProjectile03.nif";
            refract.name="RefractMesh03";wisps.name="Wisps03";trail.name="Trail12";nested.name="Trail11";decoration.name="TimeHelper04";
            Attach(root,refract);Attach(root,wisps);Attach(root,trail);Attach(trail,nested);Attach(root,decoration);Attach(wisps,mesh);
        }else Attach(root,mesh);
    }
    ~Scene(){ysmir::projectile_visuals::Clear();}
};
void Reset(){ysmir::projectile_visuals::Clear();fixture::toggles.clear();fixture::numbers.clear();fixture::snapshot={};fixture::enabled=fixture::owned=fixture::authorized=true;ysmir::true_cone::Initialize();ysmir::projectile_visuals::Initialize();ysmir::projectile_visuals::SetStormBase(nullptr);}
void Pass(const char* name){passed.emplace_back(name);}
std::function<void(RE::ConeProjectile*)> step;
void Native(RE::ConeProjectile* p){++fixture::nativeCalls;if(step)step(p);}
}
int main(){
using namespace ysmir;
Reset();
{
    Scene s(true);fixture::snapshot.distance=6;s.root.local.scale=1.5f;s.root.local.translate={10,20,30};s.cone.position={1,2,3};
    s.refract.local.translate={0,250,-28};s.wisps.local.translate={0,180,-12};s.trail.local.translate={0,-20,5};
    s.wisps.local.scale=2;s.trail.local.scale=3;s.nested.local.scale=.5f;s.nested.local.translate={4,8,12};
    RE::NiUpdateData data;s.root.UpdateTransformAndBounds(data);
    const auto refract=s.refract.world*RE::NiPoint3{1,2,3};
    const auto smoke=s.wisps.world*RE::NiPoint3{3,4,5};
    const auto tail=s.nested.world*RE::NiPoint3{6,7,8};
    const auto decoration=s.decoration.world*RE::NiPoint3{9,10,11};
    Apply(&s.cone,nullptr);
    assert(s.root.local.scale==1.5f&&s.refract.local.scale==1&&s.wisps.local.scale==2&&s.trail.local.scale==3);
    Equal(s.root.local.translate,{10,20,30});Equal(s.refract.local.translate,{0,250,-28});
    assert(s.shader.material==&s.material&&s.material.alpha==.5f);Pass("birthPreservesNativeRootChildrenOffsetsAndMaterials");
    s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,5.25f)&&s.refract.local.scale==1&&s.wisps.local.scale==2&&s.trail.local.scale==3);
    assert(s.nested.local.scale==.5f&&s.decoration.local.scale==1);
    Equal(s.refract.local.translate,{0,250,-28});Equal(s.wisps.local.translate,{0,180,-12});Equal(s.nested.local.translate,{4,8,12});
    auto grown=[&](RE::NiPoint3 point){return s.cone.position+(point-s.cone.position)*3.5f;};
    Equal(s.refract.world*RE::NiPoint3{1,2,3},grown(refract));Equal(s.wisps.world*RE::NiPoint3{3,4,5},grown(smoke));
    Equal(s.nested.world*RE::NiPoint3{6,7,8},grown(tail));Equal(s.decoration.world*RE::NiPoint3{9,10,11},grown(decoration));
    Pass("allLayerSizesGapsNestedNodesAndDecorationsShareOneTransform");
    assert(s.shader.material==&s.material&&s.material.texCoordScale[0].x==1&&s.material.texCoordScale[1].y==1);
    assert(Close(s.material.alpha,1-std::pow(.5f,2.f))&&s.material.baseColorScale==2);
    Pass("legacyOpacityCompensationWithoutUVEditsOrMaterialReplacement");
    for(int i=0;i<120;++i)Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,5.25f)&&Close(s.material.alpha,1-std::pow(.5f,2.f)));
    Equal(s.nested.world*RE::NiPoint3{6,7,8},grown(tail));Pass("repeatedUpdatesDoNotAccumulateScaleOffsetsOrDensity");
    fixture::snapshot.distance=99;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,5.25f));Pass("castSnapshotSurvivesSubsequentSoulChanges");
    projectile_visuals::Clear();assert(s.root.local.scale==1.5f&&s.material.alpha==.5f&&s.nested.local.scale==.5f);
    Equal(s.root.local.translate,{10,20,30});Pass("clearRestoresOwnedRootTransformAndDensity");
}
Reset();
{
    Scene s(true);fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    ConeHook::update3D=Native;step=[&](RE::ConeProjectile*){
        assert(s.root.local.scale==1&&s.wisps.local.scale==1);
        s.root.local.scale=2.5f;s.wisps.local.scale=4;s.wisps.local.translate={4,5,6};
        s.material.alpha=.25f;s.material.texCoordScale[0]={2,3};
    };
    const auto before=fixture::nativeCalls;ConeHook::Update3D(&s.cone);
    assert(fixture::nativeCalls==before+1&&Close(s.root.local.scale,6.25f)&&s.wisps.local.scale==4&&Close(s.wisps.world.scale,25));
    Equal(s.wisps.local.translate,{4,5,6});assert(s.material.texCoordScale[0].x==2&&s.material.texCoordScale[0].y==3);
    assert(Close(s.material.alpha,1-std::pow(.75f,1.6f)));
    Pass("nativeAnimationRunsOnceBeforeUniformGrowthAndDensity");
    step=[&](RE::ConeProjectile*){assert(s.root.local.scale==2.5f&&s.wisps.local.scale==4);};
    ConeHook::Update3D(&s.cone);assert(Close(s.root.local.scale,6.25f)&&Close(s.material.alpha,1-std::pow(.75f,1.6f)));
    Pass("stalledNativeAnimationDoesNotCompoundRootScaleOrDensity");
    fixture::toggles["PatchVisuals"]=false;true_cone::Initialize();ConeHook::Update3D(&s.cone);
    assert(s.root.local.scale==2.5f&&s.wisps.local.scale==4);Pass("disabledVisualHookRestoresRootBeforeNativeUpdate");
    projectile_visuals::Clear();assert(s.material.alpha==.25f);step={};
}
Reset();
{
    Scene s(true);fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    s.root.local.scale=7;s.root.local.translate={9,8,7};s.material.alpha=.125f;projectile_visuals::Clear();
    assert(s.root.local.scale==7&&s.material.alpha==.125f);Equal(s.root.local.translate,{9,8,7});
    Pass("cleanupDoesNotOverwriteLaterExternalEdits");
}
for(const char* model:{"Magic/VoicePushProjectile01.nif","MAGIC/VOICEPUSHPROJECTILE02.NIF","Magic/VoicePushProjectile03.nif","Magic/VoiceFlameWaveProject01.nif","Magic/VoiceFlameWaveProject02.nif","Magic/VoiceFlameWaveProject03.nif","Magic/VoiceFrostWaveProject01.nif","Magic/VoiceFrostWaveProject02.nif","Magic/VoiceFrostWaveProject03.nif","Magic/VoiceSoulTear03.nif","Custom/Replacement.nif"}){
    Reset();Scene s(true);s.cone.base.model=model;s.refract.name="CustomWave";s.wisps.name="CustomSmoke";
    fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,2.5f)&&s.refract.local.scale==1&&s.wisps.local.scale==1&&s.shader.material==&s.material);
}
Pass("allModelNamesAndReplacementHierarchiesUseTheSameRootPath");
Reset();
{
    Scene s(true);fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    RE::NiAVObject replacement;replacement.local.scale=3;replacement.local.translate={5,6,7};
    s.cone.root=&replacement;Apply(&s.cone,nullptr);
    assert(s.root.local.scale==1&&s.material.alpha==.5f&&Close(replacement.local.scale,7.5f));
    projectile_visuals::Clear();assert(replacement.local.scale==3);Equal(replacement.local.translate,{5,6,7});
    Pass("rootReplacementRestoresOldSceneAndBindsNewAnchor");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;s.root.local.scale=1.5f;s.root.local.translate={10,20,30};s.root.local.rotate={{0,-1,0,0,0,-1,1,0,0}};
    s.parent.world.translate={100,200,300};s.parent.world.scale=2;s.parent.world.rotate={{0,0,1,1,0,0,0,1,0}};
    s.root.parent=&s.parent;s.cone.position={130,260,310};
    const auto anchor=s.root.local.Invert()*(s.parent.world.Invert()*s.cone.position);
    Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,1.5f));Equal(s.root.local.translate,{10,20,30});
    s.cone.runtime.distanceMoved=128;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,2.203125f));
    Equal(s.parent.world*(s.root.local*anchor),s.cone.position);
    s.cone.runtime.distanceMoved=256;s.cone.position={300,400,500};Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,3.75f));Equal(s.parent.world*(s.root.local*anchor),s.cone.position);
    assert(Close(s.material.alpha,1-std::pow(.5f,1.6f)));Pass("movingAnchorSurvivesRotatedScaledParentWithoutExtraHeight");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;fixture::snapshot.collision=100;fixture::snapshot.vfx=99;
    s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,2.5f));
    assert(s.cone.cone.initialCollisionSphereRadius==16&&s.cone.cone.coneAngleTangent==.25f);
    float previous=1;for(int d=0;d<=16384;++d){s.cone.runtime.distanceMoved=static_cast<float>(d);Apply(&s.cone,nullptr);assert(s.root.local.scale>=previous&&s.root.local.scale<=4);previous=s.root.local.scale;}
    assert(previous>3.9f);Pass("distanceGrowthIsSmoothMonotoneAndIndependentOfCollisionAndVFXFactors");
    fixture::snapshot.distance=12;s.cone.runtime.distanceMoved=16384;assert(true_cone::VisualScale(&s.cone,fixture::snapshot)>11);
    Pass("highSoulGrowthHasNoArtificialFourTimesCap");
    s.cone.cone.collisionShape=&s;true_cone::VisualScale(&s.cone,fixture::snapshot);
    assert(s.cone.cone.collisionShape==&s&&s.cone.cone.initialCollisionSphereRadius==16&&s.cone.cone.coneAngleTangent==.25f);
    Pass("nativeCollisionShapeRadiusAndAngleAreUntouched");
}
Reset();
{
    Scene s(true);s.cone.runtime.distanceMoved=10000;Apply(&s.cone,nullptr);
    assert(s.refract.local.scale==1&&s.root.local.scale==1&&s.shader.material==&s.material&&s.material.alpha==.5f);
    Pass("zeroSoulsLeavesNativeAppearanceUnchangedAtEveryDistance");
    auto& c=s.cone;auto& k=fixture::snapshot;k.distance=6;c.runtime.distanceMoved=0;
    assert(true_cone::VisualScale(&c,k)==1);
    c.runtime.distanceMoved=.001f;assert(Close(true_cone::VisualScale(&c,k),1));Pass("birthStartsAtOneWithContinuousEntrance");
    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(float d:{-1.f,nan,inf}){c.runtime.distanceMoved=d;assert(true_cone::VisualScale(&c,k)==1);}c.runtime.distanceMoved=256;
    for(float r:{0.f,-1.f,nan,inf}){c.runtime.range=r;assert(true_cone::VisualScale(&c,k)==1);}c.runtime.range=512;
    for(float d:{0.f,-1.f,nan,inf}){k.distance=d;assert(true_cone::VisualScale(&c,k)==1);}
    Pass("invalidDistanceRangeOrGrowthCannotWriteNaNToScene");
    k.distance=6;
    for(float r:{0.f,-1.f,nan,inf,10000.f})for(float t:{0.f,-1.f,nan,inf,100.f}){
        c.cone.initialCollisionSphereRadius=r;c.cone.coneAngleTangent=t;
        assert(Close(true_cone::VisualScale(&c,k),3.5f));
    }
    Pass("visualCurveHasNoDependencyOnCollisionRadiusOrAngle");
}
Reset();
{
    Scene s;auto& p=s.cone.runtime;auto& k=fixture::snapshot;k.distance=6;p.range=60000;
    const std::array<float,5> distance{0,15000,30000,45000,60000};
    const std::array<float,5> expected{1,1.78125f,3.5f,5.21875f,6};
    for(std::size_t i=0;i<distance.size();++i){p.distanceMoved=distance[i];assert(Close(true_cone::VisualScale(&s.cone,k),expected[i]));}
    Pass("hundredSoulsFivePercentMatchesQuarterFlightMilestones");
    for(float range:{.001f,512.f,6000.f,60000.f,1e30f}){
        p.range=range;p.distanceMoved=range*.5f;assert(Close(true_cone::VisualScale(&s.cone,k),3.5f));
        p.distanceMoved=range;assert(true_cone::VisualScale(&s.cone,k)==6);
    }
    Pass("differentInstanceRangesShareNormalizedCurveWithoutDoubleGrowth");
    p.range=60000;
    for(float d:{60000.f,120000.f,std::numeric_limits<float>::max()}){
        p.distanceMoved=d;assert(true_cone::VisualScale(&s.cone,k)==6);
    }
    Pass("endOfRangeAndFrameOvershootStayAtSoulDistanceMultiplier");
    p.distanceMoved=60000;
    for(float growth:{1.f,1.05f,2.f,6.f,12.f,1000.f,std::numeric_limits<float>::max()}){
        k.distance=growth;assert(true_cone::VisualScale(&s.cone,k)==growth);
    }
    Pass("endpointMatchesEachFiniteGrowthIncludingLargeSoulCounts");
    k.distance=6;p.distanceMoved=6;
    assert(true_cone::VisualScale(&s.cone,k)-1<1e-5f);
    p.distanceMoved=59994;assert(6-true_cone::VisualScale(&s.cone,k)<1e-5f);
    Pass("smoothstepHasGentleEntranceAndExitAcrossFullFlight");
}
Reset();
{
    Scene first,second;fixture::snapshot.distance=6;first.cone.runtime.range=60000;first.cone.runtime.distanceMoved=30000;Apply(&first.cone,nullptr);
    fixture::snapshot.distance=2;second.cone.runtime.range=2000;second.cone.runtime.distanceMoved=1000;Apply(&second.cone,nullptr);
    Apply(&first.cone,nullptr);assert(Close(first.root.local.scale,3.5f)&&Close(second.root.local.scale,1.5f));
    first.cone.runtime.distanceMoved=45000;Apply(&first.cone,nullptr);
    assert(Close(first.root.local.scale,5.21875f)&&Close(second.root.local.scale,1.5f));
    Pass("simultaneousProjectilesKeepTheirOwnRangeAndCastGrowth");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;
    fixture::numbers["fBaseVisualScale"]=999;fixture::toggles["PatchRuntimeCollision"]=true;fixture::toggles["NativeWaveGrowth"]=true;true_cone::Initialize();projectile_visuals::Initialize();
    assert(Close(true_cone::VisualScale(&s.cone,fixture::snapshot),2.5f));Apply(&s.cone,nullptr);assert(Close(s.root.local.scale,2.5f)&&s.material.texCoordScale[0].x==1);
    Pass("legacyLayerFixedScaleAndCollisionKeysCannotRestoreRemovedBehavior");
    fixture::numbers["fVisualTransitionDistance"]=1;true_cone::Initialize();
    assert(Close(true_cone::VisualScale(&s.cone,fixture::snapshot),2.5f));
    fixture::numbers["fVisualTransitionDistance"]=100000;true_cone::Initialize();
    assert(Close(true_cone::VisualScale(&s.cone,fixture::snapshot),2.5f));Pass("removedTransitionDistanceCannotOverrideFullFlightProgress");
}
for(const char* key:{"Enabled","PatchVisuals","bScaleVFX"}){
    Reset();Scene s(true);fixture::snapshot.distance=4;fixture::toggles[key]=false;true_cone::Initialize();s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(s.root.local.scale==1&&s.refract.local.scale==1&&s.material.alpha==.5f&&s.shader.material==&s.material);
}
Pass("coneAndGlobalVisualSwitchesSkipAllVisualChanges");
for(int mode=0;mode<4;++mode){
    Reset();Scene s(true);fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;
    if(mode==0)fixture::enabled=false;if(mode==1)fixture::owned=false;if(mode==2)fixture::authorized=false;if(mode==3)s.cone.id=0;
    Apply(&s.cone,nullptr);assert(s.root.local.scale==1&&s.refract.local.scale==1&&s.shader.material==&s.material);
}
Pass("disabledForeignUnapprovedAndUnassignedProjectilesAreUntouched");
Reset();
{
    Scene s(true);fixture::snapshot.distance=4;fixture::toggles["DensityCompensation"]=false;projectile_visuals::Initialize();s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.root.local.scale,2.5f)&&s.material.alpha==.5f&&s.shader.material==&s.material);Pass("densitySwitchDisablesOnlyOpacityIntensityCompensation");
}
Reset();
{
    for(const char* editor:{"YSMIR_Cyclone","YSMIR_BendWill"}){
        Scene s;s.cone.base.editor=editor;fixture::snapshot.distance=4;s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
        assert(Close(s.root.local.scale,2.5f)&&s.material.alpha==.5f);
    }
    Pass("existingCycloneAndBendWillDensityExclusionsRemain");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;RE::NiAlphaProperty alpha;alpha.blending=true;alpha.dest=RE::NiAlphaProperty::AlphaFunction::kOne;
    s.mesh.runtime.alphaProperty.reset(&alpha);s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);
    assert(Close(s.material.baseColorScale,3.2f)&&s.material.alpha==.5f);
    RE::BSLightingShaderProperty light;RE::BSShaderMaterial other;light.material=&other;s.mesh.runtime.shaderProperty.reset(&light);Apply(&s.cone,nullptr);
    assert(Close(light.emissiveMult,4.8f)&&other.alpha==.5f&&s.material.baseColorScale==2);
    light.emissiveMult=5;Apply(&s.cone,nullptr);assert(Close(light.emissiveMult,8));
    projectile_visuals::Clear();assert(light.emissiveMult==5);Pass("additiveShaderSwapAndCleanupRemainSupported");
}
Reset();
{
    Scene s;fixture::snapshot.distance=4;Attach(s.root,s.other);s.other.runtime.shaderProperty.reset(&s.shader);
    s.cone.runtime.distanceMoved=256;Apply(&s.cone,nullptr);assert(Close(s.material.alpha,1-std::pow(.5f,1.6f)));
    projectile_visuals::Clear();assert(s.material.alpha==.5f);Pass("sharedMaterialIsNotCompensatedTwice");
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
    assert(s.root.flags.bits==7&&!s.root.culled&&s.material.alpha==.5f);Pass("stormScaleBoundsAndVisibilityRemainUnchanged");
}
Reset();
{
    Scene s(true);fixture::snapshot.distance=4;fixture::nativeCalls=0;
    ConeHook::loaded=[](RE::ConeProjectile* p){++fixture::nativeCalls;p->runtime.distanceMoved=0;p->cone.initialCollisionSphereRadius=16;};
    ConeHook::update3D=[](RE::ConeProjectile* p){assert(p->cone.initialCollisionSphereRadius==16);++fixture::nativeCalls;};
    ConeHook::Loaded(&s.cone);assert(s.refract.local.scale==1&&fixture::nativeCalls==1);
    s.cone.runtime.distanceMoved=256;ConeHook::Update3D(&s.cone);assert(Close(s.root.local.scale,2.5f)&&s.refract.local.scale==1&&fixture::nativeCalls==2&&s.cone.cone.initialCollisionSphereRadius==16);
    Pass("loadAndRenderHooksRunOriginalOnceWithoutWritingPhysics");
    s.cone.alive=false;fixture::now+=10;projectile_visuals::FrameUpdate();assert(states.empty()&&materials.empty());
    assert(s.root.local.scale==1&&s.material.alpha==.5f&&s.shader.material->texCoordScale[0].x==1);Pass("expiredProjectileCleanupRestoresVisualsAndReleasesState");
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
