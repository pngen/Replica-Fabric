# Replica Fabric 1.0.0

A production-grade, vendor-neutral **C++20 runtime for governing replicated AI
model and runtime state** across heterogeneous infrastructure.

## Systems boundary

Replica Fabric sits between the artifact layer and the scheduling layer. It is
deliberately *not* either of the following:

| System | Owns | Does not own |
|---|---|---|
| **Model Cache** | reusable model artifacts (weights, adapters, tokenizers, compiled/engine artifacts, manifests, durable content) | live replicated runtime instances |
| **Inference Scheduler** | what work should run next and where | which replicas exist / are healthy / may serve |
| **Replica Fabric** | which live replica is current, healthy, ready, authoritative, draining, which generation is current, and which replicas may receive work | artifact storage, work scheduling |

In one sentence:

- **Model Cache** answers: *What reusable model artifact exists?*
- **Inference Scheduler** answers: *What work should run where?*
- **Replica Fabric** answers: *Which live replica is current, healthy, ready, authoritative, and allowed to serve that work?*

Replica Fabric is not a generic service-discovery layer. It models replica
lifecycle, authority, state transitions, health, readiness, generation,
placement, promotion, retirement, and failover explicitly.

## Identity model

Every named identity is a distinct strong 128-bit type. Distinct identity kinds
never silently convert. Identity kinds are never reused across authority
generations: `ReplicaId`, `ReplicaSetId`, `ModelId`, `ArtifactId`, `TenantId`,
`WorkloadId`, `NodeId`, `WorkerId`, `WorkerBootId`, `PlacementId`,
`PromotionId`, `DrainId`, `FailoverId`, `AttemptId`, `CoordinatorEpoch`,
`ReplicaGeneration`, `ReplicaSetGeneration`, `ArtifactGeneration`,
`HealthGeneration`, `PolicyGeneration`.

## Replica-set model

A replica set defines its identity, logical model/service, desired/min/max
counts, artifact identity and generation, runtime/backend/accelerator/memory
requirements, compatibility requirements, health/readiness/warming/draining/
promotion/failover policies, placement policy, generation, and lifecycle.

Replica-set lifecycle: `CREATED`, `PROVISIONING`, `AVAILABLE`, `DEGRADED`,
`DRAINING`, `RETIRED`, `FAILED`.

## Replica lifecycle

Each replica exposes full authority metadata, generation, worker boot identity,
artifact, device, memory residency, warmth, readiness, health, placement,
promotion, serving eligibility, active requests, reserved capacity, and timing.

Replica lifecycle: `DECLARED`, `ALLOCATING`, `STARTING`, `WARMING`, `READY`,
`SERVING`, `DRAINING`, `QUIESCED`, `FAILED`, `RETIRED`.

Transitions are explicit, guarded, and deterministic. Invalid transitions fail.

## Serving authority

Serving eligibility is **never** inferred from process existence alone. A
replica may serve only when all of the following agree: coordinator epoch,
replica-set generation, replica generation, worker boot identity, artifact
generation, health generation, promotion state, lifecycle, readiness, warmth,
placement validity, and policy generation/fingerprint. A restarted process gets
a fresh `WorkerBootId` and replica generation and cannot inherit prior serving
authority.

## Health, readiness, warmth

- **Health** states: `UNKNOWN`, `STARTING`, `HEALTHY`, `DEGRADED`, `UNHEALTHY`,
  `FAILED`, `QUARANTINED`. Evidence is typed by source (measured / reported /
  derived / heuristic / unknown) and carries freshness, confidence, generation,
  and the failure/recovery reason. Stale health reports do not restore
  eligibility.
- **Readiness** is separate from health. A replica can be healthy but not ready
  to absorb work. Readiness depends on model loaded, artifact validated,
  adapters present, kernel/graph state prepared, memory available, device
  context initialized, warmup complete, dependencies ready, endpoint registered,
  and policy generation current.
- **Warming** states: `COLD`, `WARMING`, `WARM`, `STALE_WARMTH`, `INVALIDATED`.
  Warming tracks artifact loading, weight residency, adapter activation, kernel
  and graph initialization, allocator initialization, CUDA context setup, and
  bounded warmup execution.

## Promotion, draining, failover

- **Promotion** is an explicit, validated authority transition (standby -> serving,
  canary -> serving, replacement -> primary, recovered -> serving, fresh generation
  -> current). Every promotion gets a stable `PromotionId` and auditability.
- **Draining** is first-class: reject new work, let existing finish, force
  cancellation only under explicit policy, track active work and reservations,
  quiesce, release resources, retire authority. A drained replica does not
  silently become serving again.
- **Failover** marks authority obsolete, rejects stale reports, selects an
  eligible replacement deterministically (anti-affinity aware), promotes it,
  and preserves generation semantics. Ambiguous outcomes of lost work are
  surfaced explicitly - never fabricated as successful.

## Placement

Placement considers accelerator capability, free memory, NUMA locality,
topology, artifact/model locality, warm state, transfer cost, current load,
failure-domain diversity, capacity, policy, and tenant constraints. It exposes
typed component factors and a deterministic tie-break, never a single opaque
score. Anti-affinity is real across host, NUMA, accelerator, and rack /
explicitly labelled synthetic failure domains (where physical rack data is
unavailable).

## Persistence

Authoritative state is persisted with a versioned, checksummed (CRC-64/ECMA)
binary encoding that strictly rejects malformed lengths, truncation, checksum
corruption, duplicate ids/fields, invalid enum values, NaN/Inf, overflow,
trailing garbage, and incompatible versions. Recovery never resurrects stale
serving authority: every replica's serving eligibility is cleared and its worker
boot identity forgotten, so live workers must re-establish a current boot
identity (and the coordinator a fresh epoch) before any replica serves again.

## Distributed authority

A real coordinator + worker runtime runs over framed TCP. Workers register
`WorkerId`, `WorkerBootId`, node identity, backend/accelerator capabilities,
resource inventory, and protocol version. The coordinator owns authoritative
lifecycle. Every message carries authority metadata; the coordinator validates
strictly in order (`CoordinatorEpoch`, `WorkerBootId`, `ReplicaSetGeneration`,
`ReplicaGeneration`, `ArtifactGeneration`, `HealthGeneration`, `AttemptId`) and
rejects stale messages so they can never mark a replica healthy/ready, promote
it, complete a drain, retire it, release resources, or resurrect serving
authority.

### Atomic multiprocess proof

The `rf_multiprocess` proof runs one coordinator OS process + two worker OS
processes + two replicas over the real framed TCP transport. It provisions and
warms both replicas, promotes A to primary (B standby), executes real work
through the coordinator, kills worker A while authoritative, fails over to B,
executes work through B, restarts worker A with a fresh boot id, and proves:
stale authority is rejected, no double-serving authority exists, and the final
state is coherent.

## CUDA validation

On the NVIDIA GeForce RTX 5090 (Blackwell sm_120) / CUDA 13.1 system, the
`rf_cuda_demo` and `cuda_tests` perform real device selection, context
initialization, bounded device allocation, host/device transfer, a real kernel
launch, synchronization, and CPU-reference verification, then teardown with
verified memory recovery. With a single GPU, physical multi-GPU failover is not
claimed; multiple OS worker processes and explicitly labelled synthetic
multi-device/failure-domain models are used for unavailable hardware.

## Build & install

Requirements: CMake >= 3.24, a C++20 compiler (MSVC 2022), and optionally the
CUDA 13.1 toolkit. After configuring, the library, CLI, examples, benchmarks,
and tests are discoverable via the `replicafabric::replicafabric` target.

Install and consume:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix <prefix>
```

A downstream consumer uses `find_package(ReplicaFabric CONFIG REQUIRED)` and links
`replicafabric::replicafabric`.

## Tests & benchmarks

The suite covers unit, property (fixed-seed), concurrency, adversarial,
persistence/corruption, real multiprocess, and CUDA validation. No test timeout
mechanism exists anywhere - no `timeoutMs`, no `TIMEOUT` properties, no
watchdogs, no process time limits. Tests run to natural completion, failure,
crash, or manual termination after diagnosing a genuine hang. Benchmarks measure
completion throughput for creation, transitions, health/readiness updates,
placement, promotion, failover, drain bookkeeping, snapshot persistence &
recovery, and multi-threaded / large-pool mutation.

## Physically unavailable hardware limitations

When hardware is not present (e.g. no CUDA device, no physical rack topology),
the runtime never fabricates success: CUDA probes report failure, and placement
uses clearly labelled synthetic failure domains. The distributed proof always
runs over real TCP with real OS processes.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs.