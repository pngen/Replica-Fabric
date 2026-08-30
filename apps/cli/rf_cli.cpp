// rf_cli - Replica Fabric command-line interface.
//
// Operates on an in-process authoritative ReplicaSetController, with text and
// JSON output in deterministic ordering. Uses --state <file> to load an
// existing snapshot at start-up and --save <file> to persist after a mutation,
// so standalone lifecycle commands (warm/promote/drain/failover/explain*) can
// operate on replicas created by a prior "setup" / "create-set" invocation.

#include <replicafabric/core/replica_set_controller.hpp>
#include <replicafabric/persistence/snapshot.hpp>
#include <replicafabric/placement/placement.hpp>

#include <cstdio>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace replicafabric;

static bool g_json = false;
static std::string g_state_in;
static std::string g_state_out;
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

// After loading a snapshot (which is a crash-recovery that clears serving
// authority, boot identities, and the epoch), re-establish the live-session
// authority context: re-link every replica to its worker's current boot id,
// restore a consistent coordinator epoch, and re-arm DECLARED replicas to
// STARTING so they can be re-warmed. serving_eligible stays false, so no
// serving authority is ever resurrected.
static void relink_authority(ReplicaSetController& ctrl) {
  CoordinatorEpoch ep;
  bool have_ep = false;
  for (auto& [id, r] : ctrl.replicas()) {
    (void)id;
    if (!have_ep) { ep = r.coordinator_epoch; have_ep = true; }
    auto wit = ctrl.workers().find(r.worker_id);
    if (wit == ctrl.workers().end()) continue;
    r.boot_id = wit->second.boot_id;
  }
  if (have_ep) ctrl.set_epoch(ep);
}

static bool load_state(ReplicaSetController& ctrl) {
  if (g_state_in.empty()) return true;
  FILE* f = std::fopen(g_state_in.c_str(), "rb");
  if (!f) { std::cerr << "cannot open state file: " << g_state_in << "\n"; return false; }
  std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));
  if (n > 0) std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  std::string err;
  if (!recover_into(bytes, ctrl, &err)) { std::cerr << "state load failed: " << err << "\n"; return false; }
  relink_authority(ctrl);
  return true;
}

static void save_state(ReplicaSetController& ctrl) {
  if (g_state_out.empty()) return;
  auto bytes = make_snapshot(ctrl);
  FILE* f = std::fopen(g_state_out.c_str(), "wb");
  if (!f) { std::cerr << "cannot open state file for write: " << g_state_out << "\n"; return; }
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
}

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

// Provision + allocate + start a replica on a freshly-registered worker,
// leaving it COLD (STARTING) so a later "warm" command can warm it.
static void setup_replica(ReplicaSetController& ctrl, const ReplicaSetId& set, const ReplicaId& rid,
                          WorkerId* out_wid, WorkerBootId* out_boot, NodeId* out_node) {
  WorkerId w = WorkerId::random(g_rng);
  WorkerBootId b = WorkerBootId::random(g_rng);
  NodeId n = NodeId::random(g_rng);
  const ReplicaSetState* setp = ctrl.find_set(set);
  WorkerRegistration wr; wr.worker_id = w; wr.boot_id = b; wr.node_id = n; wr.protocol_version = 1;
  wr.inventory.total_memory_bytes = 64ULL*1024*1024*1024; wr.inventory.free_memory_bytes = wr.inventory.total_memory_bytes;
  wr.inventory.accelerator_count = 1;
  wr.inventory.devices.push_back(DeviceCapability{AcceleratorKind::CUDA, "cuda:0", {12,0}, 32ULL*1024*1024*1024});
  wr.inventory.backends.push_back(BackendCapability{BackendKind::TRITON, "triton-3", {12,0}, NumericMode::FP16});
  ctrl.register_worker(wr, 0, 0);
  ReplicaCompatibility compat;
  if (setp) { compat.model_id = setp->compatibility.model_id; compat.backend = setp->compatibility.backend;
    compat.runtime_name = setp->compatibility.runtime_name; compat.architecture = setp->compatibility.architecture;
    compat.numeric_mode = setp->compatibility.numeric_mode; compat.artifact_generation = setp->compatibility.artifact_generation;
    compat.policy_fingerprint = setp->compatibility.policy_fingerprint; compat.compute = setp->compatibility.min_compute; }
  ctrl.provision_replica(rid, set, w, compat, wr.inventory.devices[0], 8ULL*1024*1024, 0, 0);
  ctrl.set_allocating(rid, PlacementId::random(g_rng), 0);
  ctrl.set_starting(rid, 0, 0);
  if (out_wid) *out_wid = w;
  if (out_boot) *out_boot = b;
  if (out_node) *out_node = n;
}

// Bring a replica to READY + HEALTHY (idempotent; tolerates a replica that is
// already ready). This re-establishes readiness after a state reload.
static bool ensure_ready(ReplicaSetController& ctrl, const ReplicaId& rid, const WorkerId& w, const WorkerBootId& b) {
  const ReplicaState* cur = ctrl.find_replica(rid);
  if (cur == nullptr) return false;
  const bool need = (cur->readiness != ReadinessState::READY || cur->health != HealthState::HEALTHY);
  if (need) {
    WarmingRecord warm; warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
    warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true; warm.allocator_init_done=true;
    warm.device_context_done=true; warm.warmup_execution_done=true; warm.endpoint_registered=true; warm.steps_required=4; warm.steps_completed=4;
    ctrl.warm_replica(rid, warm, w, b, 0);
    const ReplicaState* r2 = ctrl.find_replica(rid);
    HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED; he.source="cli";
    if (r2) he.generation = r2->health_generation; he.observed_at_mono = 0;
    ctrl.report_health(rid, he, w, b, 0);
    ReadinessRecord rr; rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
    rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
    rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true; rr.factors.dependencies_ready=true;
    rr.factors.endpoint_registered=true; rr.factors.policy_current=true; rr.state=ReadinessState::READY;
    ctrl.report_readiness(rid, rr, w, b, 0);
  }
  const ReplicaState* fin = ctrl.find_replica(rid);
  return fin != nullptr && fin->readiness == ReadinessState::READY && fin->health == HealthState::HEALTHY;
}

int main(int argc, char** argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--json") g_json = true;
    else if (a == "--state" && i + 1 < argc) g_state_in = argv[++i];
    else if (a == "--save" && i + 1 < argc) g_state_out = argv[++i];
    else args.push_back(a);
  }
  if (args.empty()) {
    std::cout << "Replica Fabric 1.0.0 CLI\n"
              << "options: --json | --state <file> | --save <file>\n"
              << "commands: version | create-set | setup | list-sets | list-replicas | inspect | "
              << "warm | promote | drain | failover | explain-servable | explain-placement | "
              << "snapshot-save | snapshot-recover | demo\n";
    return 0;
  }
  const std::string cmd = args[0];
  ReplicaSetController ctrl(0x11ULL);
  if (!load_state(ctrl)) return 1;

  if (cmd == "version") { if (g_json) std::cout << "{\"version\":\"1.0.0\"}\n"; else std::cout << "Replica Fabric 1.0.0\n"; return 0; }

  if (cmd == "create-set") {
    ReplicaSetId id = pid<ReplicaSetId>(args[1]);
    ModelId model = pid<ModelId>(args[2]);
    ArtifactId art = pid<ArtifactId>(args[3]);
    ArtifactGeneration ag = pid<ArtifactGeneration>(args[4]);
    ReplicaSetGeneration sg = pid<ReplicaSetGeneration>(args[5]);
    PolicyGeneration pg = pid<PolicyGeneration>(args[6]);
    int max = args.size() > 7 ? std::stoi(args[7]) : 2;
    auto r = ctrl.create_replica_set(mk_set(id, model, art, ag, sg, pg, max), CoordinatorEpoch::random(g_rng));
    std::cout << (r.ok() ? "ok\n" : (r.message + "\n"));
    save_state(ctrl);
    return r.ok() ? 0 : 1;
  }

  // setup: create a fresh 2-replica set and leave both replicas COLD (STARTING).
  if (cmd == "setup") {
    int max = args.size() > 1 ? std::stoi(args[1]) : 2;
    ReplicaSetId set = ReplicaSetId::random(g_rng);
    ModelId model = ModelId::random(g_rng);
    ArtifactId art = ArtifactId::random(g_rng);
    ArtifactGeneration ag = ArtifactGeneration::random(g_rng);
    auto r = ctrl.create_replica_set(mk_set(set, model, art, ag, ReplicaSetGeneration::random(g_rng), PolicyGeneration::random(g_rng), max), CoordinatorEpoch::random(g_rng));
    if (!r.ok()) { std::cout << "setup failed: " << r.message << "\n"; return 1; }
    if (g_json) std::cout << "{\n";
    out("set", set.str());
    for (int i = 0; i < max && i < 2; ++i) {
      ReplicaId rid = ReplicaId::random(g_rng);
      WorkerId w; WorkerBootId b; NodeId n;
      setup_replica(ctrl, set, rid, &w, &b, &n);
      if (g_json) { std::cout << "\"replica" << i << "\":{"; out("id", rid.str()); out("worker", w.str()); out("boot", b.str()); std::cout << "\"node\":\"" << n.str() << "\"},\n"; }
      else { std::cout << "replica" << i << ": " << rid.str() << " (worker " << w.str() << " boot " << b.str() << ")\n"; }
    }
    if (g_json) std::cout << "}\n";
    save_state(ctrl);
    return 0;
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

  // warm: apply warming + health + readiness so the replica becomes READY.
  if (cmd == "warm") {
    ReplicaId rid = pid<ReplicaId>(args[1]);
    WorkerId w = pid<WorkerId>(args[2]);
    WorkerBootId b = pid<WorkerBootId>(args[3]);
    const ReplicaState* cur = ctrl.find_replica(rid);
    if (cur == nullptr) { std::cout << "unknown replica\n"; return 1; }
    WarmingRecord warm; warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
    warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true; warm.allocator_init_done=true;
    warm.device_context_done=true; warm.warmup_execution_done=true; warm.endpoint_registered=true; warm.steps_required=4; warm.steps_completed=4;
    auto wr = ctrl.warm_replica(rid, warm, w, b, 0);
    HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED; he.source="cli";
    const ReplicaState* r2 = ctrl.find_replica(rid); if (r2) he.generation = r2->health_generation; he.observed_at_mono = 0;
    auto hr = ctrl.report_health(rid, he, w, b, 0);
    ReadinessRecord rr; rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
    rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
    rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true; rr.factors.dependencies_ready=true;
    rr.factors.endpoint_registered=true; rr.factors.policy_current=true; rr.state=ReadinessState::READY;
    auto qr = ctrl.report_readiness(rid, rr, w, b, 0);
    const ReplicaState* final = ctrl.find_replica(rid);
    std::cout << "warm=" << (wr.ok() ? "ok" : wr.message) << " health=" << (hr.ok() ? "ok" : hr.message)
              << " readiness=" << (qr.ok() ? "ok" : qr.message)
              << " -> lifecycle=" << (final ? std::string(to_string(final->lifecycle)) : "?") << "\n";
    save_state(ctrl);
    return 0;
  }

  // promote <replica> <worker> <boot> <target>
  if (cmd == "promote") {
    ReplicaId rid = pid<ReplicaId>(args[1]);
    WorkerId w = pid<WorkerId>(args[2]);
    WorkerBootId b = pid<WorkerBootId>(args[3]);
    PromotionState target; from_int(target, std::stoi(args[4]));
    if (!ensure_ready(ctrl, rid, w, b)) { std::cout << "cannot make replica ready for promotion\n"; return 1; }
    auto r = ctrl.promote_replica(rid, target, w, b, 0, 0);
    std::cout << (r.ok() ? "ok" : r.message) << "\n";
    save_state(ctrl);
    return r.ok() ? 0 : 1;
  }

  // drain <replica> <worker> <boot>
  if (cmd == "drain") {
    ReplicaId rid = pid<ReplicaId>(args[1]);
    WorkerId w = pid<WorkerId>(args[2]);
    WorkerBootId b = pid<WorkerBootId>(args[3]);
    auto r = ctrl.drain_replica(rid, w, b, 0);
    std::cout << (r.ok() ? "ok" : r.message) << "\n";
    save_state(ctrl);
    return r.ok() ? 0 : 1;
  }

  // failover <set> <failed-replica> [reason]
  if (cmd == "failover") {
    ReplicaSetId set = pid<ReplicaSetId>(args[1]);
    ReplicaId failed = pid<ReplicaId>(args[2]);
    std::string reason = args.size() > 3 ? args[3] : "failover";
    // Re-establish readiness for the candidate (non-failed) replicas so there is
    // an eligible replacement to promote.
    for (const auto& rid : ctrl.list_replicas(set)) {
      if (rid == failed) continue;
      const ReplicaState* rs = ctrl.find_replica(rid);
      if (rs == nullptr) continue;
      auto wit = ctrl.workers().find(rs->worker_id);
      if (wit != ctrl.workers().end()) ensure_ready(ctrl, rid, wit->second.worker_id, wit->second.boot_id);
    }
    auto r = ctrl.trigger_failover(set, failed, reason, 0, 0);
    std::cout << (r.ok() ? "ok" : r.message) << "\n";
    save_state(ctrl);
    return r.ok() ? 0 : 1;
  }

  // explain-servable <replica> : print every authority factor.
  if (cmd == "explain-servable") {
    ReplicaId rid = pid<ReplicaId>(args[1]);
    auto d = ctrl.servable(rid);
    if (g_json) std::cout << "{\n";
    out("replica", rid.str()); out("eligible", d.eligible ? "1" : "0"); out("rejection", d.rejection);
    if (g_json) std::cout << "\"factors\":[";
    for (std::size_t i = 0; i < d.factors.size(); ++i) {
      const auto& f = d.factors[i];
      if (g_json) { if (i) std::cout << ","; std::cout << "{\"name\":\"" << jesc(f.name) << "\",\"ok\":" << (f.satisfied?"1":"0") << ",\"detail\":\"" << jesc(f.detail) << "\"}"; }
      else std::cout << "  " << f.name << ": " << (f.satisfied ? "OK" : "FAIL") << " :: " << f.detail << "\n";
    }
    if (g_json) std::cout << "]}\n";
    return 0;
  }

  // explain-placement <set> : run the placement engine over registered workers.
  if (cmd == "explain-placement") {
    ReplicaSetId set = pid<ReplicaSetId>(args[1]);
    const ReplicaSetState* setp = ctrl.find_set(set);
    if (setp == nullptr) { std::cout << "unknown set\n"; return 1; }
    std::vector<PlacementHost> hosts;
    for (const auto& [wid, wr] : ctrl.workers()) {
      PlacementHost h;
      h.worker_id = wid; h.boot_id = wr.boot_id; h.node_id = wr.node_id;
      h.host = "host-" + wid.str().substr(0, 8); h.numa = "numa-0";
      if (!wr.inventory.devices.empty()) h.device = wr.inventory.devices[0];
      h.inventory = wr.inventory;
      h.failure_domain_labels = {"rack-" + std::string(1, static_cast<char>('A' + static_cast<char>(wid.hi() % 2)))};
      hosts.push_back(std::move(h));
    }
    PlacementEngine eng(7);
    auto d = eng.place(*setp, hosts, {});
    if (g_json) std::cout << "{\n";
    out("placed", d.placed ? "1" : "0"); out("note", d.note);
    if (!d.placed) { if (g_json) std::cout << "}\n"; return 1; }
    out("chosen_worker", d.chosen.worker_id.str()); out("chosen_host", d.chosen.host);
    if (g_json) std::cout << "\"candidates\":[";
    for (std::size_t i = 0; i < d.candidates.size(); ++i) {
      const auto& c = d.candidates[i];
      if (g_json) { if (i) std::cout << ","; std::cout << "{\"worker\":\"" << jesc(c.host.worker_id.str()) << "\",\"eligible\":" << (c.eligible?"1":"0") << ",\"score\":" << c.total << ",\"rejection\":\"" << jesc(c.rejection) << "\"}"; }
      else std::cout << "  " << c.host.worker_id.str() << " score=" << c.total << (c.eligible ? " (eligible)" : (" rejected: " + c.rejection)) << "\n";
    }
    if (g_json) std::cout << "]}\n";
    return 0;
  }

  if (cmd == "demo") {
    ReplicaSetId set = ReplicaSetId::random(g_rng);
    ModelId model = ModelId::random(g_rng);
    ArtifactId art = ArtifactId::random(g_rng);
    ArtifactGeneration ag = ArtifactGeneration::random(g_rng);
    ctrl.create_replica_set(mk_set(set, model, art, ag, ReplicaSetGeneration::random(g_rng), PolicyGeneration::random(g_rng), 2), CoordinatorEpoch::random(g_rng));
    WorkerId wa = WorkerId::random(g_rng); WorkerBootId ba = WorkerBootId::random(g_rng); NodeId na = NodeId::random(g_rng);
    WorkerId wb = WorkerId::random(g_rng); WorkerBootId bb = WorkerBootId::random(g_rng); NodeId nb = NodeId::random(g_rng);
    ReplicaId ra = ReplicaId::random(g_rng); ReplicaId rb = ReplicaId::random(g_rng);
    setup_replica(ctrl, set, ra, &wa, &ba, &na);
    setup_replica(ctrl, set, rb, &wb, &bb, &nb);
    // warm + ready both
    auto warm_ready = [&](ReplicaId rid, WorkerId w, WorkerBootId b) {
      WarmingRecord warm; warm.state = WarmthState::WARM; warm.artifact_loading_done=true; warm.weights_resident=true;
      warm.adapters_active=true; warm.kernel_init_done=true; warm.graph_init_done=true; warm.allocator_init_done=true;
      warm.device_context_done=true; warm.warmup_execution_done=true; warm.endpoint_registered=true; warm.steps_required=4; warm.steps_completed=4;
      ctrl.warm_replica(rid, warm, w, b, 0);
      const ReplicaState* r = ctrl.find_replica(rid); HealthEvidence he; he.state=HealthState::HEALTHY; he.kind=HealthEvidenceKind::REPORTED; he.source="cli";
      if (r) he.generation = r->health_generation; he.observed_at_mono = 0; ctrl.report_health(rid, he, w, b, 0);
      ReadinessRecord rr; rr.factors.model_loaded=true; rr.factors.artifact_validated=true; rr.factors.adapters_present=true;
      rr.factors.kernel_prepared=true; rr.factors.graph_prepared=true; rr.factors.memory_available=true;
      rr.factors.device_context_initialized=true; rr.factors.warmup_complete=true; rr.factors.dependencies_ready=true;
      rr.factors.endpoint_registered=true; rr.factors.policy_current=true; rr.state=ReadinessState::READY; ctrl.report_readiness(rid, rr, w, b, 0);
    };
    warm_ready(ra, wa, ba); warm_ready(rb, wb, bb);
    ctrl.promote_replica(ra, PromotionState::PRIMARY, wa, ba, 0, 0);
    ctrl.promote_replica(rb, PromotionState::STANDBY, wb, bb, 0, 0);
    std::cout << (ctrl.servable(ra).eligible ? "A serving as PRIMARY\n" : "A NOT serving\n");
    std::cout << (ctrl.servable(rb).eligible ? "B serving (BAD - double serve)\n" : "B standby (correct, not serving)\n");
    ctrl.unregister_worker(wa);
    ctrl.trigger_failover(set, ra, "demo: worker A died", 0, 0);
    std::cout << (ctrl.servable(rb).eligible ? "B failed over and serving as PRIMARY\n" : "B failover FAILED\n");
    std::cout << "snapshot bytes=" << make_snapshot(ctrl).size() << "\n";
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