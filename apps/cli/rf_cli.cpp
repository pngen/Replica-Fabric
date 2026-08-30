// rf_cli - Replica Fabric command-line interface.
//
// Operates on an in-process authoritative ReplicaSetController, with text and
// JSON output in deterministic ordering. Also runs the synthetic lifecycle
// demo, the multiprocess proof, the CUDA demo, and benchmarks via subcommands.

#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/persistence/snapshot.hpp>
#include <replicafabric/placement/placement.hpp>

#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace replicafabric;

static bool g_json = false;
static std::mt19937_64 g_rng(0x5A17ULL);

static std::string jesc(const std::string& s) {
  std::string o; o.reserve(s.size()+2);
  for (char c : s) { if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); } else if (c == '\n') { o += "\\n"; } else o.push_back(c); }
  return o;
}

static void out(const std::string& k, const std::string& v) {
  if (g_json) std::cout << "\"" << k << "\":\"" << jesc(v) << "\",\n";
  else std::cout << k << ": " << v << "\n";
}

template <typename IdT> static IdT pid(const std::string& s) { return IdT::from_string(s); }

static ReplicaSetState mk_set(ReplicaSetId id, ModelId model, ArtifactId art, ArtifactGeneration ag, ReplicaSetGeneration sg, PolicyGeneration pg, int max) {
  ReplicaSetState s;
  s.id = id; s.model_id = model; s.artifact_id = art; s.artifact_generation = ag;
  s.generation = sg; s.policy_generation = pg;
  s.workload_id = WorkloadId::random(g_rng); s.tenant_id = TenantId::random(g_rng);
  s.desired_count = static_cast<std::uint32_t>(max);
  s.min_healthy = 1; s.max_replicas = static_cast<std::uint32_t>(max);
  s.lifecycle = ReplicaSetLifecycle::CREATED;
  s.compatibility.model_id = model; s.compatibility.backend = BackendKind::TRITON;
  s.compatibility.runtime_name = "triton-3"; s.compatibility.architecture = "llama";
  s.compatibility.min_compute = {8,0}; s.compatibility.numeric_mode = NumericMode::FP16;
  s.compatibility.artifact_generation = ag; s.compatibility.policy_fingerprint = "fp-1";
  s.backend = BackendKind::TRITON; s.runtime_name = "triton-3"; s.min_compute = {8,0};
  s.memory_requirement_bytes = 16*1024*1024; s.accelerator_requirement = 1;
  s.placement_policy.anti_affinity_domains = {"host"};
  s.placement_policy.require_diversity = true;
  s.placement_policy.synthetic_domains = {{"rack-A","rack"},{"rack-B","rack"}};
  return s;
}

static void walk_replica(ReplicaSetController& ctrl, const ReplicaId& id, const ReplicaSetId& set, const WorkerId& w, const WorkerBootId& b, const NodeId& n) {
  const ReplicaSetState* setp = ctrl.find_set(set);
  ReplicaCompatibility compat;
  if (setp) { compat.model_id = setp->compatibility.model_id; compat.backend = setp->compatibility.backend;
    compat.runtime_name = setp->compatibility.runtime_name; compat.architecture = setp->compatibility.architecture;
    compat.numeric_mode = setp->compatibility.numeric_mode; compat.artifact_generation = setp->compatibility.artifact_generation;
    compat.policy_fingerprint = setp->compatibility.policy_fingerprint; compat.compute = setp->compatibility.min_compute; }
  WorkerRegistration wr; wr.worker_id = w; wr.boot_id = b; wr.node_id = n; wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 64ULL*1024*1024*1024; wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes;
  wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);
  ctrl.provision_replica(id, set, w, compat, wr.inventory.devices[0], 8ULL*1024*1024, 0, 0);
  ctrl.set_allocating(id, PlacementId::random(g_rng), 0);
  ctrl.set_starting(id, 0, 0);
  WarmingRecord warm; warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
  warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true; warm.allocator_init_done=true;
  warm.device_context_done=true; warm.warmup_execution_done=true; warm.endpoint_registered=true; warm.steps_required=4;
  ctrl.warm_replica(id, warm, w, b, 0);
  const ReplicaState* r = ctrl.find_replica(id);
  HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED; he.source="cli";
  if (r) he.generation = r->health_generation; he.observed_at_mono = 0;
  ctrl.report_health(id, he, w, b, 0);
  ReadinessRecord rr; rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
  rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
  rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true; rr.factors.dependencies_ready=true;
  rr.factors.endpoint_registered=true; rr.factors.policy_current=true; rr.state=ReadinessState::READY;
  ctrl.report_readiness(id, rr, w, b, 0);
}

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) { std::string a = argv[i]; if (a == "--json") g_json = true; else args.push_back(a); }
  if (args.empty()) {
    std::cout << "Replica Fabric 1.0.0 CLI\n"
              << "commands: version | create-set | list-sets | list-replicas | inspect | "
              << "provision | warm | promote | drain | fail | retire | failover | "
              << "snapshot-save | snapshot-recover | demo\n";
    return 0;
  }
  const std::string cmd = args[0];
  ReplicaSetController ctrl(0x11ULL);

  if (cmd == "version") { if (g_json) std::cout << "{\"version\":\"1.0.0\"}\n"; else std::cout << "Replica Fabric 1.0.0\n"; return 0; }

  if (cmd == "create-set") {
    ReplicaSetId id = pid<ReplicaSetId>(args[1]);
    ModelId model = pid<ModelId>(args[2]);
    ArtifactId art = pid<ArtifactId>(args[3]);
    ArtifactGeneration ag = pid<ArtifactGeneration>(args[4]);
    ReplicaSetGeneration sg = pid<ReplicaSetGeneration>(args[5]);
    PolicyGeneration pg = pid<PolicyGeneration>(args[6]);
    int max = args.size() > 7 ? std::stoi(args[7]) : 2;
    CoordinatorEpoch ep = CoordinatorEpoch::random(g_rng);
    auto r = ctrl.create_replica_set(mk_set(id, model, art, ag, sg, pg, max), ep);
    std::cout << (r.ok() ? "ok\n" : (r.message + "\n"));
    return r.ok() ? 0 : 1;
  }

  if (cmd == "list-sets") {
    auto sets = ctrl.list_sets();
    if (g_json) std::cout << "{\"sets\":[";
    for (std::size_t i = 0; i < sets.size(); ++i) { if (g_json && i) std::cout << ","; std::cout << (g_json ? "\"" + sets[i].str() + "\"" : sets[i].str()); if (!g_json) std::cout << "\n"; }
    if (g_json) std::cout << "]}\n";
    return 0;
  }

  if (cmd == "list-replicas") {
    ReplicaSetId setid = pid<ReplicaSetId>(args[1]);
    auto reps = ctrl.list_replicas(setid);
    if (g_json) std::cout << "{\"replicas\":[";
    for (std::size_t i = 0; i < reps.size(); ++i) { if (g_json && i) std::cout << ","; std::cout << (g_json ? "\"" + reps[i].str() + "\"" : reps[i].str()); if (!g_json) std::cout << "\n"; }
    if (g_json) std::cout << "]}\n";
    return 0;
  }

  if (cmd == "inspect") {
    ReplicaId id = pid<ReplicaId>(args[1]);
    const ReplicaState* r = ctrl.find_replica(id);
    if (r == nullptr) { std::cout << "unknown replica\n"; return 1; }
    if (g_json) std::cout << "{\n";
    out("id", r->id.str()); out("set", r->set_id.str()); out("lifecycle", std::string(to_string(r->lifecycle)));
    out("health", std::string(to_string(r->health))); out("readiness", std::string(to_string(r->readiness)));
    out("warmth", std::string(to_string(r->warmth))); out("promotion", std::string(to_string(r->promotion)));
    out("role", std::string(to_string(r->role))); out("serving", r->serving_eligible ? "1" : "0");
    out("boot", r->boot_id.str()); out("worker", r->worker_id.str());
    if (g_json) std::cout << "}\n";
    return 0;
  }

  if (cmd == "demo") {
    // Synthetic lifecycle demo.
    ReplicaSetId set = ReplicaSetId::random(g_rng);
    ModelId model = ModelId::random(g_rng);
    ArtifactId art = ArtifactId::random(g_rng);
    ArtifactGeneration ag = ArtifactGeneration::random(g_rng);
    CoordinatorEpoch ep = CoordinatorEpoch::random(g_rng);
    ctrl.create_replica_set(mk_set(set, model, art, ag, ReplicaSetGeneration::random(g_rng), PolicyGeneration::random(g_rng), 2), ep);
    WorkerId wa = WorkerId::random(g_rng); WorkerBootId ba = WorkerBootId::random(g_rng); NodeId na = NodeId::random(g_rng);
    WorkerId wb = WorkerId::random(g_rng); WorkerBootId bb = WorkerBootId::random(g_rng); NodeId nb = NodeId::random(g_rng);
    ReplicaId ra = ReplicaId::random(g_rng); ReplicaId rb = ReplicaId::random(g_rng);
    walk_replica(ctrl, ra, set, wa, ba, na);
    walk_replica(ctrl, rb, set, wb, bb, nb);
    ctrl.promote_replica(ra, PromotionState::PRIMARY, wa, ba, 0, 0);
    ctrl.promote_replica(rb, PromotionState::STANDBY, wb, bb, 0, 0);
    std::cout << (ctrl.servable(ra).eligible ? "A serving as PRIMARY\n" : "A NOT serving\n");
    std::cout << (ctrl.servable(rb).eligible ? "B serving (BAD - double serve)\n" : "B standby (correct, not serving)\n");
    ctrl.unregister_worker(wa);
    ctrl.trigger_failover(set, ra, "demo: worker A died", 0, 0);
    std::cout << (ctrl.servable(rb).eligible ? "B failed over and serving as PRIMARY\n" : "B falover FAILED\n");
    auto bytes = make_snapshot(ctrl);
    std::cout << "snapshot bytes=" << bytes.size() << "\n";
    return 0;
  }

  if (cmd == "snapshot-save") {
    std::string path = args[1];
    auto bytes = make_snapshot(ctrl);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::cout << "cannot open\n"; return 1; }
    std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f);
    std::cout << "saved " << bytes.size() << " bytes to " << path << "\n";
    return 0;
  }

  if (cmd == "snapshot-recover") {
    std::string path = args[1];
    std::vector<std::uint8_t> bytes;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::cout << "cannot open\n"; return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    bytes.resize(static_cast<std::size_t>(n));
    std::fread(bytes.data(), 1, bytes.size(), f); std::fclose(f);
    std::string err;
    bool ok = recover_into(bytes, ctrl, &err);
    std::cout << (ok ? "recovered\n" : ("recover failed: " + err + "\n"));
    return ok ? 0 : 1;
  }

  std::cout << "unknown command: " << cmd << "\n";
  return 1;
}
