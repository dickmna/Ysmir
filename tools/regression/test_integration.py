"""Regression checks for exact source bodies with stubbed engine services.
Run from an x64 Native Tools shell:
  python tools/regression/test_integration.py BUILD_DIRECTORY
This does not launch Skyrim or substitute for gameplay testing.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(sys.argv[1]).resolve()
HERE.mkdir(parents=True, exist_ok=True)
OUTPUT = HERE

def function_body(path, signature):
    source = (ROOT / path).read_text(encoding="utf-8")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index+1]
    raise ValueError(signature)

cone = (ROOT / "src/true_cone.cpp").read_text(encoding="utf-8").replace('#include "modules.h"', "")
cast = function_body("src/shout_dispatch.cpp", "void CastFromEffect(")
snapshot_body = function_body("src/shout_dispatch.cpp", "std::shared_ptr<EffectContext> EffectSnapshot(")
scale_body = function_body("src/shout_dispatch.cpp", "float ScaleFloat(")
push_body = function_body("src/shout_dispatch.cpp", "float PushForce(")
end_body = function_body("src/shout_dispatch.cpp", "void EndEffect(")
finish_body = function_body("src/shout_dispatch.cpp", "static void Finish(")
visuals_source = (ROOT / "src/projectile_visuals.cpp").read_text()
assert "true_cone::PatchVisuals" in visuals_source
assert "true_cone::VisualScale" in visuals_source
assert "ApplyCollision" not in visuals_source
assert "write_vfunc(0xAB" not in visuals_source  # Visual adapter never hooks physics updates.
prefix = r"""
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace fixture { bool coneEnabled=true,scaleVFX=true,coneVisuals=true,globallyEnabled=true; double now=0; int casts=0; float lastMagnitude=0; }
namespace RE {
struct StaticFunctionTag {};
struct SpellItem { template<class T> T* As() { return static_cast<T*>(this); } };
struct ActiveEffect {
    enum class Flag { kDispelled };
    struct Flags { bool dispelled=false; bool any(Flag) const { return dispelled; } } flags;
    std::uint16_t usUniqueID=1;
    SpellItem* spell{};
};
struct Actor;
struct ActorHandle {
    Actor* ptr{};
    std::shared_ptr<Actor> get() const { return ptr ? std::shared_ptr<Actor>(ptr,[](Actor*){}) : nullptr; }
};
struct Actor { ActorHandle GetHandle() { return {this}; } bool IsDead() const { return false; } };
inline Actor player;
struct TESForm { template<class T> static T* LookupByID(std::uint32_t) { return &player; } };
struct UI { static UI* GetSingleton() { static UI ui; return &ui; } bool GameIsPaused() { return false; } };
// This is the declared CommonLib pointee layout. Native RTTI establishes that
// the actual object in this location is a bhk wrapper, whose +0x20 is materialID.
struct hkpSphereShape { std::array<std::byte,0x20> prefix{}; float radius{}; };
static_assert(offsetof(hkpSphereShape,radius)==0x20);
struct BGSProjectile { struct Data { float coneSpread=15,collisionRadius=16; } data; };
struct ConeProjectile {
    struct ConeData { float initialCollisionSphereRadius=16,coneAngleTangent=.25f; hkpSphereShape* collisionShape{}; } cone;
    struct Runtime { float distanceMoved=0; } runtime;
    BGSProjectile base;
    ConeData& GetConeRuntimeData() { return cone; }
    Runtime& GetProjectileRuntimeData() { return runtime; }
    BGSProjectile* GetProjectileBase() { return &base; }
};
}
namespace ysmir::config {
bool Bool(const char*,const char* key,bool fallback) {
    if(std::string(key)=="Enabled")return fixture::coneEnabled;
    if(std::string(key)=="bScaleVFX")return fixture::scaleVFX;
    if(std::string(key)=="PatchVisuals")return fixture::coneVisuals;
    return fallback;
}
float Float(const char*,const char*,float fallback) { return fallback; }
}
namespace ysmir::progression {
struct ScalingSnapshot {
    std::uint32_t souls{};
    float distance{1},magnitude{1},duration{1},area{1},collision{1},vfx{1},density{1},cooldown{1};
};
bool Enabled() { return fixture::globallyEnabled; }
double Now() { return fixture::now; }
}
namespace ysmir::core {
bool IsPlayerActor(RE::Actor* p) { return p==&RE::player; }
bool CastFeatureSpell(RE::SpellItem*,RE::Actor&,RE::Actor*,const progression::ScalingSnapshot& s) { ++fixture::casts; fixture::lastMagnitude=s.magnitude; return true; }
}
namespace SKSE {
namespace log {
template<class... A> void warn(const char*,A&&...) {}
template<class... A> void info(const char*,A&&...) {}
}
struct Tasks {
    std::vector<std::function<void()>> tasks;
    void AddTask(std::function<void()> f) { tasks.push_back(std::move(f)); }
    void Drain() { auto pending=std::move(tasks); tasks.clear(); for(auto& f:pending) f(); }
};
Tasks* GetTaskInterface() { static Tasks t; return &t; }
}
struct EffectContext {
    std::uint16_t id;
    RE::SpellItem* spell;
    ysmir::progression::ScalingSnapshot snapshot;
    std::atomic<bool> active{true};
    EffectContext(std::uint16_t key,RE::SpellItem* source,const ysmir::progression::ScalingSnapshot& value)
        : id(key),spell(source),snapshot(value) {}
};
std::unordered_map<RE::ActiveEffect*,std::shared_ptr<EffectContext>> activeEffects;
std::mutex snapshotMutex;
void (*finish)(RE::ActiveEffect*)=[](RE::ActiveEffect*){};
struct Plan { std::vector<RE::SpellItem*> spells; };
std::unordered_set<RE::SpellItem*> authorized;
std::unordered_map<RE::SpellItem*,Plan> plans;
std::atomic_uint32_t generation{0};
"""
main = r"""
int main() {
    using namespace ysmir;
    progression::ScalingSnapshot s; s.collision=2; s.distance=4; s.vfx=3;
    RE::hkpSphereShape nativeSphere; nativeSphere.radius=.2286f;
    RE::hkpSphereShape declaredPointee;
    auto* actualPtr=&nativeSphere;
    std::memcpy(declaredPointee.prefix.data()+0x10,&actualPtr,sizeof(actualPtr));
    declaredPointee.radius=std::bit_cast<float>(std::uint32_t{0});
    RE::ConeProjectile p;
    p.cone.collisionShape=&declaredPointee;
    p.cone.coneAngleTangent=.005f; // Deliberately narrower than the old 15 degree floor.
    p.cone.initialCollisionSphereRadius=96; // Read native state without taking ownership of it.
    true_cone::Initialize();
    assert(true_cone::VisualScale(&p,s)==1);
    p.runtime.distanceMoved=256;
    const auto grown=true_cone::VisualScale(&p,s);
    assert(grown>1 && grown<4 && p.cone.initialCollisionSphereRadius==96);
    assert(p.cone.coneAngleTangent==.005f);
    assert(declaredPointee.radius==0 && nativeSphere.radius==.2286f);
    s.collision=1;
    assert(true_cone::VisualScale(&p,s)==grown);
    assert(p.cone.initialCollisionSphereRadius==96);
    fixture::coneEnabled=false;true_cone::Initialize();
    assert(true_cone::VisualScale(&p,s)==1&&!true_cone::PatchVisuals());
    fixture::coneEnabled=true;fixture::scaleVFX=false;true_cone::Initialize();
    assert(true_cone::VisualScale(&p,s)==1&&!true_cone::PatchVisuals());
    fixture::scaleVFX=true;fixture::coneVisuals=false;true_cone::Initialize();
    assert(true_cone::VisualScale(&p,s)==1&&!true_cone::PatchVisuals());
    fixture::coneVisuals=true;true_cone::Initialize();

    RE::SpellItem parent,child;
    authorized.insert(&child); plans[&parent].spells.push_back(&child);
    RE::ActiveEffect wrapper; wrapper.spell=&parent;
    auto registerParent=[&] {
        activeEffects.insert_or_assign(&wrapper,std::make_shared<EffectContext>(std::uint16_t{1},&parent,s));
    };
    auto enqueue=[&] { CastFromEffect(nullptr,&child,&RE::player,&RE::player,&wrapper); };
    registerParent();
    enqueue();
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==1); // An active parent still casts normally.
    enqueue();
    Finish(&wrapper);
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==1); // Finish invalidates work before Papyrus cleanup.
    activeEffects.at(&wrapper)->snapshot.magnitude=2;
    wrapper.flags.dispelled=true;
    assert(PushForce(nullptr,10,&RE::player,&wrapper)==20);
    assert(!EffectSnapshot(&wrapper)); // Read-only push scaling cannot revive casting.
    enqueue();
    assert(SKSE::GetTaskInterface()->tasks.empty());
    wrapper.flags.dispelled=false;
    registerParent(); enqueue(); EndEffect(&wrapper,true);
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==1); // Destruction also invalidates work.
    registerParent(); enqueue(); ++generation;
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==1); // Loading remains a cancellation boundary.
    registerParent(); enqueue(); fixture::globallyEnabled=false;
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==1);
    enqueue();
    assert(SKSE::GetTaskInterface()->tasks.empty()); // Disabled entry cannot enqueue.
    fixture::globallyEnabled=true;
    s.magnitude=3; registerParent(); enqueue(); s.magnitude=9;
    SKSE::GetTaskInterface()->Drain();
    assert(fixture::casts==2 && fixture::lastMagnitude==3); // Recast does not rewrite an old effect's snapshot.
    RE::SpellItem unrelated; authorized.insert(&unrelated);
    CastFromEffect(nullptr,&unrelated,&RE::player,&RE::player,&wrapper);
    assert(SKSE::GetTaskInterface()->tasks.empty()); // Must belong to the parent graph.
    ++wrapper.usUniqueID;
    assert(!EffectSnapshot(&wrapper)); // A recycled address is not the old instance.

    std::cout << R"({
      "passed": true,
      "scope": "Exact source bodies with engine-service stubs; no gameplay",
      "authoredConeAnglePreserved": true,
      "nativeInitialConeRadiusPreserved": true,
      "coneVisualScaleIndependentOfCollisionFactor": true,
      "noDirectPhysicsObjectWrite": true,
      "coneMasterSwitchPreserved": true,
      "globalVFXSwitchPreserved": true,
      "coneVisualSwitchIndependent": true,
      "activeParentCasts": true,
      "finishedParentCancels": true,
      "finishedInstantPushRetainsSnapshot": true,
      "destroyedParentCancels": true,
      "loadGenerationCancels": true,
      "globalDisableCancelsQueuedCast": true,
      "globalDisableRejectsNewCast": true,
      "delayedCastKeepsOriginalSnapshot": true,
      "unrelatedChildRejected": true,
      "reusedEffectAddressRejected": true
    })";
}
"""
generated = prefix + "\n" + cone + "\n" + snapshot_body + "\n" + scale_body + "\n" + push_body + "\n" + end_body + "\n" + finish_body + "\n" + cast + "\n" + main
(HERE / "audit_fixture.cpp").write_text(generated, encoding="utf-8")
built = subprocess.run(
    ["cl.exe", "/nologo", "/std:c++20", "/EHsc", "/Od", "/W4", "/MD",
     "audit_fixture.cpp", "/Fe:audit_fixture.exe", "/Fo:audit_fixture.obj"],
    cwd=HERE, capture_output=True, text=True
)
(HERE / "fixture-build.log").write_text(built.stdout+built.stderr, encoding="utf-8")
print(built.stdout+built.stderr)
built.check_returncode()
run = subprocess.run([str(HERE / "audit_fixture.exe")], cwd=HERE, capture_output=True, text=True, check=True)
result = json.loads(run.stdout)
result["sourceBodiesSha256"] = {name: hashlib.sha256(body.encode()).hexdigest() for name,body in (("true_cone.cpp",cone),("CastFromEffect",cast),("EffectSnapshot",snapshot_body),("EndEffect",end_body),("Finish",finish_body))}
(OUTPUT / "ysmir-fixture-results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(run.stdout)
