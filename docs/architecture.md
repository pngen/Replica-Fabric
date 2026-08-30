# Replica Fabric Architecture

## Layers

- **core** - strong 128-bit identities, all enums, error taxonomy, time, typed
  capabilities, compatibility, and policies. Device-agnostic and side-effect free.
- **model** - the ReplicaSet and Replica value model, health/readiness/warming
  records, and placement policy types.
- **authority** - the serving-authority gate (all authority factors must agree)
  and the guarded lifecycle transition tables. This is the heart of authority.
- **placement** - a deterministic placement engine with typed component-factor
  explanations and real anti-affinity / synthetic failure domains.
- **persistence** - a versioned, checksummed binary snapshot codec with strict
  corruption/truncation/duplicate/invalid rejection; recovery never resurrects
  serving authority.
- **distributed** - framed TCP transport, an authority-carrying protocol, and the
  coordinator (authoritative controller owner) + worker runtime.
- **cuda** - a real CUDA-backed replica path (device/context/alloc/kernel/
  CPU-reference verification/teardown with memory-recovery proof).
- **cli** - a text/JSON CLI over the controller and demos.

## Authority model

The coordinator owns a single ReplicaSetController. Every mutation validates
authority strictly in the fixed order (CoordinatorEpoch, WorkerBootId,
ReplicaSetGeneration, ReplicaGeneration, ArtifactGeneration, HealthGeneration,
AttemptId) and rejects stale messages so a stale producer can never mark a
replica healthy/ready, promote it, complete a drain, retire a fresh replica,
release resources, or resurrect serving authority. Serving eligibility is a
function of the whole authority set, not process existence.

## Concurrency

All controller state is mutated under a single mutex. Blocking network,
persistence, backend, and CUDA work is performed outside that lock; the
distributed layer and CLI drive I/O on separate threads/turns. This makes
self-deadlocks, lock-order inversion, and callbacks-under-lock impossible.

## Invariants enforced

- Identity is never silently reused across authority generations.
- A restarted worker yields a fresh WorkerBootId/replica generation and cannot
  inherit prior serving authority.
- Exactly one primary holds serving authority per replica set at a time; the
  gate prevents double-serving and stale resurrection.
- Health and readiness are distinct; warmth is operationally distinct from being
  alive.
- Invalid lifecycle transitions fail; draining must be explicitly completed and
  never silently reverses to serving.
- Persistence rejects every tested corruption class and never resurrects authority.