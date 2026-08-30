#include <replicafabric/persistence/snapshot.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace replicafabric {
namespace {

std::array<std::uint64_t, 256> make_crc64_table() {
  std::array<std::uint64_t, 256> t{};
  for (std::uint64_t i = 0; i < 256; ++i) {
    std::uint64_t crc = i;
    for (int k = 0; k < 8; ++k) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xC96C5795D7870F42ULL) : (crc >> 1);
    }
    t[static_cast<std::size_t>(i)] = crc;
  }
  return t;
}
const std::array<std::uint64_t, 256>& crc64_table() {
  static const std::array<std::uint64_t, 256> table = make_crc64_table();
  return table;
}
std::uint64_t crc64(const std::uint8_t* data, std::size_t len) {
  std::uint64_t crc = 0;
  const auto& table = crc64_table();
  for (std::size_t i = 0; i < len; ++i) crc = table[static_cast<std::uint8_t>(crc) ^ data[i]] ^ (crc >> 8);
  return crc;
}
constexpr std::uint8_t kMagic[8] = {'R','F','S','N','A','P','0','1'};

class Writer {
public:
  void u8(std::uint8_t v) { b_.push_back(v); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) b_.push_back(static_cast<std::uint8_t>((v >> (8*i)) & 0xFF)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) b_.push_back(static_cast<std::uint8_t>((v >> (8*i)) & 0xFF)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
  void f64(double v) { std::uint64_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); u64(bits); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void bytes(const std::uint8_t* p, std::size_t n) { b_.insert(b_.end(), p, p + n); }
  void string(std::string_view s) { u64(static_cast<std::uint64_t>(s.size())); bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
  void strings(const std::vector<std::string>& v) { u64(v.size()); for (const auto& s : v) string(s); }
  const std::vector<std::uint8_t>& buffer() const { return b_; }
private:
  std::vector<std::uint8_t> b_;
};

class Reader {
public:
  Reader(const std::uint8_t* p, std::size_t len) : p_(p), len_(len) {}
  bool u8(std::uint8_t& v) { if (pos_ + 1 > len_) return fail("u8 truncation"); v = p_[pos_++]; return true; }
  bool u32(std::uint32_t& v) {
    if (pos_ + 4 > len_) return fail("u32 truncation");
    v = 0; for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p_[pos_+i]) << (8*i); pos_ += 4; return true;
  }
  bool u64(std::uint64_t& v) {
    if (pos_ + 8 > len_) return fail("u64 truncation");
    v = 0; for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p_[pos_+i]) << (8*i); pos_ += 8; return true;
  }
  bool i64(std::int64_t& v) { std::uint64_t u = 0; if (!u64(u)) return false; v = static_cast<std::int64_t>(u); return true; }
  bool f64(double& v) {
    std::uint64_t bits = 0; if (!u64(bits)) return false;
    double d = 0.0; std::memcpy(&d, &bits, sizeof(d));
    if (std::isnan(d) || std::isinf(d)) return fail("NaN/Inf double");
    v = d; return true;
  }
  bool boolean(bool& v) { std::uint8_t b = 0; if (!u8(b)) return false; if (b > 1) return fail("invalid boolean"); v = (b == 1); return true; }
  bool bytes(std::uint8_t* out, std::size_t n) { if (n > len_ - pos_) return fail("byte range overflow"); if (out) std::memcpy(out, p_ + pos_, n); pos_ += n; return true; }
  bool string(std::string& out) { std::uint64_t n = 0; if (!u64(n)) return false; if (n > len_ - pos_) return fail("string length overflow"); out.assign(reinterpret_cast<const char*>(p_+pos_), static_cast<std::size_t>(n)); pos_ += static_cast<std::size_t>(n); return true; }
  bool strings(std::vector<std::string>& out) {
    std::uint64_t n = 0; if (!u64(n)) return false; if (n > len_ - pos_) return fail("string vector count");
    out.clear(); out.reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; ++i) { std::string s; if (!string(s)) return false; out.push_back(std::move(s)); }
    return true;
  }
  bool fully_consumed() const { return pos_ == len_; }
  bool ok() const { return ok_; }
  const std::string& error() const { return err_; }
  std::size_t remaining() const { return len_ - pos_; }
private:
  bool fail(const char* msg) { if (ok_) err_ = msg; ok_ = false; return false; }
  const std::uint8_t* p_; std::size_t len_; std::size_t pos_ = 0; bool ok_ = true; std::string err_;
};

template <typename IdT> bool read_id(Reader& r, IdT& out) { std::array<std::uint8_t,16> b{}; if (!r.bytes(b.data(),16)) return false; out = IdT::from_bytes(b); return true; }
template <typename IdT> void write_id(Writer& w, const IdT& id) { const auto b = id.to_bytes(); w.bytes(b.data(), 16); }
template <typename E> void write_enum(Writer& w, E e) { w.u8(static_cast<std::uint8_t>(e)); }

void write_cc(Writer& w, const ComputeCapability& c) { w.u32(static_cast<std::uint32_t>(c.major)); w.u32(static_cast<std::uint32_t>(c.minor)); }
bool read_cc(Reader& r, ComputeCapability& c) { std::uint32_t a, b; if (!r.u32(a) || !r.u32(b)) return false; c.major = static_cast<int>(a); c.minor = static_cast<int>(b); return true; }
void write_device(Writer& w, const DeviceCapability& d) { write_enum(w, d.kind); w.string(d.device_id); write_cc(w, d.compute); w.u64(d.memory_bytes); }
void write_backend(Writer& w, const BackendCapability& b) { write_enum(w, b.kind); w.string(b.name); write_cc(w, b.compute); write_enum(w, b.numeric); }
void write_inventory(Writer& w, const ResourceInventory& i) {
  w.u64(i.total_memory_bytes); w.u64(i.free_memory_bytes); w.u32(static_cast<std::uint32_t>(i.accelerator_count));
  w.u64(i.devices.size()); for (const auto& d : i.devices) write_device(w, d);
  w.u64(i.backends.size()); for (const auto& b : i.backends) write_backend(w, b);
}
bool read_device(Reader& r, DeviceCapability& d) { std::uint8_t k; if (!r.u8(k) || !from_int(d.kind, k)) return false; if (!r.string(d.device_id)) return false; if (!read_cc(r, d.compute)) return false; return r.u64(d.memory_bytes); }
bool read_backend(Reader& r, BackendCapability& b) { std::uint8_t k; if (!r.u8(k) || !from_int(b.kind, k)) return false; if (!r.string(b.name)) return false; if (!read_cc(r, b.compute)) return false; std::uint8_t n; if (!r.u8(n) || !from_int(b.numeric, n)) return false; return true; }
bool read_inventory(Reader& r, ResourceInventory& i) {
  if (!r.u64(i.total_memory_bytes) || !r.u64(i.free_memory_bytes)) return false;
  std::uint32_t ac; if (!r.u32(ac)) return false; i.accelerator_count = static_cast<int>(ac);
  std::uint64_t nd; if (!r.u64(nd)) return false; if (nd > r.remaining()) return false; i.devices.clear(); i.devices.reserve(static_cast<std::size_t>(nd));
  for (std::uint64_t x = 0; x < nd; ++x) { DeviceCapability d; if (!read_device(r, d)) return false; i.devices.push_back(std::move(d)); }
  std::uint64_t nb; if (!r.u64(nb)) return false; if (nb > r.remaining()) return false; i.backends.clear(); i.backends.reserve(static_cast<std::size_t>(nb));
  for (std::uint64_t x = 0; x < nb; ++x) { BackendCapability b; if (!read_backend(r, b)) return false; i.backends.push_back(std::move(b)); }
  return true;
}

// --- enum readers -----------------------------------------------------------
bool re_enum(Reader& r, ReplicaSetLifecycle& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, ReplicaLifecycle& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, HealthState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, HealthEvidenceKind& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, WarmthState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, ReadinessState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, PromotionState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, ReplicaRole& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, DrainState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, FailoverState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, MemoryResidencyState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, PlacementState& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, AcceleratorKind& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, BackendKind& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, NumericMode& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }
bool re_enum(Reader& r, WorkOutcome& v) { std::uint8_t b; return r.u8(b) && from_int(v, b); }

void write_health_policy(Writer& w, const HealthPolicy& p) {
  w.boolean(p.enabled); w.u64(p.expected_interval_ns); w.u64(p.stale_after_ns); w.u64(p.degrade_after_ns);
  w.u32(p.unhealthy_after_misses); w.u32(p.quarantine_after_failures); w.u32(p.min_healthy_observations); w.f64(p.health_threshold); write_enum(w, p.initial);
}
bool read_health_policy(Reader& r, HealthPolicy& p) {
  if (!r.boolean(p.enabled) || !r.u64(p.expected_interval_ns) || !r.u64(p.stale_after_ns) || !r.u64(p.degrade_after_ns)) return false;
  if (!r.u32(p.unhealthy_after_misses) || !r.u32(p.quarantine_after_failures) || !r.u32(p.min_healthy_observations)) return false;
  if (!r.f64(p.health_threshold)) return false; std::uint8_t b; if (!r.u8(b) || !from_int(p.initial, b)) return false; return true;
}
void write_warming_policy(Writer& w, const WarmingPolicy& p) {
  w.boolean(p.enabled); w.boolean(p.require_artifact_loaded); w.boolean(p.require_weights_resident); w.boolean(p.require_adapters_active);
  w.boolean(p.require_kernel_init); w.boolean(p.require_graph_init); w.boolean(p.require_allocator_init); w.boolean(p.require_device_context);
  w.boolean(p.require_bounded_warmup); w.boolean(p.require_endpoint); w.u64(p.warmup_work_steps); w.u64(p.warmup_timeout_ns);
}
bool read_warming_policy(Reader& r, WarmingPolicy& p) {
  if (!r.boolean(p.enabled) || !r.boolean(p.require_artifact_loaded) || !r.boolean(p.require_weights_resident) || !r.boolean(p.require_adapters_active)) return false;
  if (!r.boolean(p.require_kernel_init) || !r.boolean(p.require_graph_init) || !r.boolean(p.require_allocator_init) || !r.boolean(p.require_device_context)) return false;
  if (!r.boolean(p.require_bounded_warmup) || !r.boolean(p.require_endpoint)) return false;
  return r.u64(p.warmup_work_steps) && r.u64(p.warmup_timeout_ns);
}
void write_draining_policy(Writer& w, const DrainingPolicy& p) {
  w.boolean(p.reject_new_work); w.boolean(p.allow_existing_to_finish); w.boolean(p.allow_force_cancel); w.u64(p.grace_period_ns); w.u64(p.force_cancel_after_ns);
}
bool read_draining_policy(Reader& r, DrainingPolicy& p) {
  if (!r.boolean(p.reject_new_work) || !r.boolean(p.allow_existing_to_finish) || !r.boolean(p.allow_force_cancel)) return false;
  return r.u64(p.grace_period_ns) && r.u64(p.force_cancel_after_ns);
}
void write_promotion_policy(Writer& w, const PromotionPolicy& p) {
  w.boolean(p.require_ready); w.boolean(p.require_healthy); write_enum(w, p.min_health); w.boolean(p.require_compatible);
  w.boolean(p.require_placement_valid); w.boolean(p.require_resources); w.boolean(p.require_authority_current); w.boolean(p.allow_canary); w.boolean(p.auto_promote_standby);
}
bool read_promotion_policy(Reader& r, PromotionPolicy& p) {
  if (!r.boolean(p.require_ready) || !r.boolean(p.require_healthy)) return false; std::uint8_t b; if (!r.u8(b) || !from_int(p.min_health, b)) return false;
  if (!r.boolean(p.require_compatible) || !r.boolean(p.require_placement_valid) || !r.boolean(p.require_resources) || !r.boolean(p.require_authority_current)) return false;
  return r.boolean(p.allow_canary) && r.boolean(p.auto_promote_standby);
}
void write_failover_policy(Writer& w, const FailoverPolicy& p) { w.boolean(p.require_diversity); w.boolean(p.prefer_standby_first); w.u32(p.max_attempts); w.boolean(p.preserve_generation); w.boolean(p.reject_ambiguous_outcome); }
bool read_failover_policy(Reader& r, FailoverPolicy& p) { if (!r.boolean(p.require_diversity) || !r.boolean(p.prefer_standby_first)) return false; if (!r.u32(p.max_attempts)) return false; return r.boolean(p.preserve_generation) && r.boolean(p.reject_ambiguous_outcome); }
void write_policies(Writer& w, const PolicyBundle& p) { write_health_policy(w, p.health); write_warming_policy(w, p.warming); write_draining_policy(w, p.draining); write_promotion_policy(w, p.promotion); write_failover_policy(w, p.failover); }
bool read_policies(Reader& r, PolicyBundle& p) { return read_health_policy(r, p.health) && read_warming_policy(r, p.warming) && read_draining_policy(r, p.draining) && read_promotion_policy(r, p.promotion) && read_failover_policy(r, p.failover); }

void write_placement_policy(Writer& w, const PlacementPolicy& p) {
  w.strings(p.anti_affinity_domains);
  w.u64(p.synthetic_domains.size()); for (const auto& d : p.synthetic_domains) { w.string(d.label); w.string(d.kind); }
  w.boolean(p.require_diversity); w.string(p.preferred_host); w.string(p.preferred_numa); w.string(p.preferred_device); w.string(p.preferred_failure_domain); w.u32(p.tie_break_strategy);
}
bool read_placement_policy(Reader& r, PlacementPolicy& p) {
  if (!r.strings(p.anti_affinity_domains)) return false;
  std::uint64_t n; if (!r.u64(n)) return false; if (n > r.remaining()) return false; p.synthetic_domains.clear(); p.synthetic_domains.reserve(static_cast<std::size_t>(n));
  for (std::uint64_t i = 0; i < n; ++i) { SyntheticFailureDomain d; if (!r.string(d.label) || !r.string(d.kind)) return false; p.synthetic_domains.push_back(std::move(d)); }
  if (!r.boolean(p.require_diversity)) return false;
  return r.string(p.preferred_host) && r.string(p.preferred_numa) && r.string(p.preferred_device) && r.string(p.preferred_failure_domain) && r.u32(p.tie_break_strategy);
}

void write_compat_req(Writer& w, const CompatibilityRequirements& c) {
  write_id(w, c.model_id); w.string(c.model_revision); w.string(c.tokenizer_vocab); w.strings(c.required_adapters);
  write_enum(w, c.backend); w.string(c.runtime_name); w.string(c.architecture); write_cc(w, c.min_compute);
  write_enum(w, c.numeric_mode); write_id(w, c.artifact_generation); w.string(c.kernel_abi); w.string(c.policy_fingerprint);
}
bool read_compat_req(Reader& r, CompatibilityRequirements& c) {
  if (!read_id(r, c.model_id) || !r.string(c.model_revision) || !r.string(c.tokenizer_vocab) || !r.strings(c.required_adapters)) return false;
  std::uint8_t b; if (!r.u8(b) || !from_int(c.backend, b)) return false;
  if (!r.string(c.runtime_name) || !r.string(c.architecture) || !read_cc(r, c.min_compute)) return false;
  std::uint8_t n; if (!r.u8(n) || !from_int(c.numeric_mode, n)) return false;
  return read_id(r, c.artifact_generation) && r.string(c.kernel_abi) && r.string(c.policy_fingerprint);
}
void write_replica_compat(Writer& w, const ReplicaCompatibility& c) {
  write_id(w, c.model_id); w.string(c.model_revision); w.string(c.tokenizer_vocab); w.strings(c.adapters);
  write_enum(w, c.backend); w.string(c.runtime_name); w.string(c.architecture); write_cc(w, c.compute);
  write_enum(w, c.numeric_mode); write_id(w, c.artifact_generation); w.string(c.kernel_abi); w.string(c.policy_fingerprint);
}
bool read_replica_compat(Reader& r, ReplicaCompatibility& c) {
  if (!read_id(r, c.model_id) || !r.string(c.model_revision) || !r.string(c.tokenizer_vocab) || !r.strings(c.adapters)) return false;
  std::uint8_t b; if (!r.u8(b) || !from_int(c.backend, b)) return false;
  if (!r.string(c.runtime_name) || !r.string(c.architecture) || !read_cc(r, c.compute)) return false;
  std::uint8_t n; if (!r.u8(n) || !from_int(c.numeric_mode, n)) return false;
  return read_id(r, c.artifact_generation) && r.string(c.kernel_abi) && r.string(c.policy_fingerprint);
}

void write_health_record(Writer& w, const HealthRecord& h) {
  write_enum(w, h.state); write_enum(w, h.kind); w.string(h.source); w.u64(h.updated_at_mono); w.i64(h.updated_at_wall);
  w.f64(h.confidence); write_id(w, h.generation); w.string(h.reason); w.u32(h.consecutive_stale); w.u32(h.consecutive_failures);
}
bool read_health_record(Reader& r, HealthRecord& h) {
  std::uint8_t s, k; if (!r.u8(s) || !from_int(h.state, s)) return false; if (!r.u8(k) || !from_int(h.kind, k)) return false;
  if (!r.string(h.source) || !r.u64(h.updated_at_mono) || !r.i64(h.updated_at_wall) || !r.f64(h.confidence)) return false;
  return read_id(r, h.generation) && r.string(h.reason) && r.u32(h.consecutive_stale) && r.u32(h.consecutive_failures);
}
void write_readiness_record(Writer& w, const ReadinessRecord& x) {
  write_enum(w, x.state);
  w.boolean(x.factors.model_loaded); w.boolean(x.factors.artifact_validated); w.boolean(x.factors.adapters_present); w.boolean(x.factors.kernel_prepared);
  w.boolean(x.factors.graph_prepared); w.boolean(x.factors.memory_available); w.boolean(x.factors.device_context_initialized); w.boolean(x.factors.warmup_complete);
  w.boolean(x.factors.dependencies_ready); w.boolean(x.factors.endpoint_registered); w.boolean(x.factors.policy_current);
  w.u64(x.updated_at_mono); w.i64(x.updated_at_wall); w.string(x.reason);
}
bool read_readiness_record(Reader& r, ReadinessRecord& out) {
  std::uint8_t s; if (!r.u8(s) || !from_int(out.state, s)) return false;
  if (!r.boolean(out.factors.model_loaded) || !r.boolean(out.factors.artifact_validated) || !r.boolean(out.factors.adapters_present) || !r.boolean(out.factors.kernel_prepared)) return false;
  if (!r.boolean(out.factors.graph_prepared) || !r.boolean(out.factors.memory_available) || !r.boolean(out.factors.device_context_initialized) || !r.boolean(out.factors.warmup_complete)) return false;
  if (!r.boolean(out.factors.dependencies_ready) || !r.boolean(out.factors.endpoint_registered) || !r.boolean(out.factors.policy_current)) return false;
  return r.u64(out.updated_at_mono) && r.i64(out.updated_at_wall) && r.string(out.reason);
}
void write_warming_record(Writer& w, const WarmingRecord& x) {
  write_enum(w, x.state); w.u64(x.steps_completed); w.u64(x.steps_required);
  w.boolean(x.artifact_loading_done); w.boolean(x.weights_resident); w.boolean(x.adapters_active); w.boolean(x.kernel_init_done);
  w.boolean(x.graph_init_done); w.boolean(x.allocator_init_done); w.boolean(x.device_context_done); w.boolean(x.warmup_execution_done); w.boolean(x.endpoint_registered);
  w.u64(x.started_warming_mono); w.u64(x.became_warm_mono); w.i64(x.became_warm_wall); w.string(x.message);
}
bool read_warming_record(Reader& r, WarmingRecord& out) {
  std::uint8_t s; if (!r.u8(s) || !from_int(out.state, s)) return false;
  if (!r.u64(out.steps_completed) || !r.u64(out.steps_required)) return false;
  if (!r.boolean(out.artifact_loading_done) || !r.boolean(out.weights_resident) || !r.boolean(out.adapters_active) || !r.boolean(out.kernel_init_done)) return false;
  if (!r.boolean(out.graph_init_done) || !r.boolean(out.allocator_init_done) || !r.boolean(out.device_context_done) || !r.boolean(out.warmup_execution_done) || !r.boolean(out.endpoint_registered)) return false;
  return r.u64(out.started_warming_mono) && r.u64(out.became_warm_mono) && r.i64(out.became_warm_wall) && r.string(out.message);
}

void write_replica_set(Writer& w, const ReplicaSetState& s) {
  write_id(w, s.id); write_id(w, s.model_id); write_id(w, s.workload_id); write_id(w, s.tenant_id);
  w.u32(s.desired_count); w.u32(s.min_healthy); w.u32(s.max_replicas);
  write_id(w, s.artifact_id); write_id(w, s.artifact_generation); write_id(w, s.generation); write_id(w, s.policy_generation);
  write_enum(w, s.lifecycle);
  write_compat_req(w, s.compatibility); write_policies(w, s.policies); write_placement_policy(w, s.placement_policy);
  write_enum(w, s.backend); w.string(s.runtime_name); write_cc(w, s.min_compute); w.u64(s.memory_requirement_bytes); w.u32(s.accelerator_requirement);
}
bool read_replica_set(Reader& r, ReplicaSetState& s) {
  if (!read_id(r, s.id) || !read_id(r, s.model_id) || !read_id(r, s.workload_id) || !read_id(r, s.tenant_id)) return false;
  if (!r.u32(s.desired_count) || !r.u32(s.min_healthy) || !r.u32(s.max_replicas)) return false;
  if (!read_id(r, s.artifact_id) || !read_id(r, s.artifact_generation) || !read_id(r, s.generation) || !read_id(r, s.policy_generation)) return false;
  std::uint8_t b; if (!r.u8(b) || !from_int(s.lifecycle, b)) return false;
  if (!read_compat_req(r, s.compatibility) || !read_policies(r, s.policies) || !read_placement_policy(r, s.placement_policy)) return false;
  std::uint8_t bk; if (!r.u8(bk) || !from_int(s.backend, bk)) return false;
  return r.string(s.runtime_name) && read_cc(r, s.min_compute) && r.u64(s.memory_requirement_bytes) && r.u32(s.accelerator_requirement);
}

void write_replica(Writer& w, const ReplicaState& x) {
  write_id(w, x.id); write_id(w, x.set_id); write_id(w, x.generation);
  write_id(w, x.node_id); write_id(w, x.worker_id); write_id(w, x.boot_id);
  write_id(w, x.artifact_id); write_id(w, x.artifact_generation);
  write_enum(w, x.backend); w.string(x.backend_identity); w.string(x.device_identity); write_enum(w, x.accelerator); write_cc(w, x.compute_capability);
  write_enum(w, x.memory_residency); write_enum(w, x.warmth); write_enum(w, x.readiness); write_enum(w, x.health); write_enum(w, x.lifecycle);
  write_id(w, x.placement_id); write_enum(w, x.placement); write_enum(w, x.promotion); write_enum(w, x.role);
  w.boolean(x.serving_eligible); w.u64(x.active_requests); w.u64(x.reserved_capacity); w.u64(x.capacity_total);
  w.u64(x.start_time_mono); w.i64(x.start_time_wall);
  write_health_record(w, x.health_record); write_readiness_record(w, x.readiness_record); write_warming_record(w, x.warming_record);
  write_id(w, x.coordinator_epoch); write_id(w, x.set_generation); write_id(w, x.health_generation); write_id(w, x.policy_generation);
  w.string(x.policy_fingerprint); write_replica_compat(w, x.compatibility);
}
bool read_replica(Reader& r, ReplicaState& out) {
  std::uint8_t a;
  if (!read_id(r, out.id) || !read_id(r, out.set_id) || !read_id(r, out.generation)) return false;
  if (!read_id(r, out.node_id) || !read_id(r, out.worker_id) || !read_id(r, out.boot_id)) return false;
  if (!read_id(r, out.artifact_id) || !read_id(r, out.artifact_generation)) return false;
  if (!r.u8(a) || !from_int(out.backend, a)) return false;
  if (!r.string(out.backend_identity) || !r.string(out.device_identity)) return false;
  if (!r.u8(a) || !from_int(out.accelerator, a)) return false;
  if (!read_cc(r, out.compute_capability)) return false;
  if (!r.u8(a) || !from_int(out.memory_residency, a)) return false;
  if (!r.u8(a) || !from_int(out.warmth, a)) return false;
  if (!r.u8(a) || !from_int(out.readiness, a)) return false;
  if (!r.u8(a) || !from_int(out.health, a)) return false;
  if (!r.u8(a) || !from_int(out.lifecycle, a)) return false;
  if (!read_id(r, out.placement_id)) return false;
  if (!r.u8(a) || !from_int(out.placement, a)) return false;
  if (!r.u8(a) || !from_int(out.promotion, a)) return false;
  if (!r.u8(a) || !from_int(out.role, a)) return false;
  if (!r.boolean(out.serving_eligible)) return false;
  if (!r.u64(out.active_requests) || !r.u64(out.reserved_capacity) || !r.u64(out.capacity_total)) return false;
  if (!r.u64(out.start_time_mono) || !r.i64(out.start_time_wall)) return false;
  if (!read_health_record(r, out.health_record)) return false;
  if (!read_readiness_record(r, out.readiness_record)) return false;
  if (!read_warming_record(r, out.warming_record)) return false;
  if (!read_id(r, out.coordinator_epoch) || !read_id(r, out.set_generation) || !read_id(r, out.health_generation) || !read_id(r, out.policy_generation)) return false;
  return r.string(out.policy_fingerprint) && read_replica_compat(r, out.compatibility);
}

void write_worker(Writer& w, const WorkerRegistration& x) {
  write_id(w, x.worker_id); write_id(w, x.boot_id); write_id(w, x.node_id);
  write_inventory(w, x.inventory); write_id(w, x.hosted_set_generation); w.u32(x.protocol_version);
}
bool read_worker(Reader& r, WorkerRegistration& out) {
  if (!read_id(r, out.worker_id) || !read_id(r, out.boot_id) || !read_id(r, out.node_id)) return false;
  if (!read_inventory(r, out.inventory)) return false;
  return read_id(r, out.hosted_set_generation) && r.u32(out.protocol_version);
}
void write_promotion(Writer& w, const PromotionRecord& p) {
  write_id(w, p.id); write_id(w, p.replica_id); write_id(w, p.set_id); write_id(w, p.replica_generation); write_id(w, p.set_generation); write_id(w, p.epoch);
  write_enum(w, p.from); write_enum(w, p.to); w.u64(p.when_mono); w.i64(p.when_wall); w.string(p.reason);
}
bool read_promotion(Reader& r, PromotionRecord& out) {
  if (!read_id(r, out.id) || !read_id(r, out.replica_id) || !read_id(r, out.set_id)) return false;
  if (!read_id(r, out.replica_generation) || !read_id(r, out.set_generation) || !read_id(r, out.epoch)) return false;
  std::uint8_t a; if (!r.u8(a) || !from_int(out.from, a)) return false; if (!r.u8(a) || !from_int(out.to, a)) return false;
  return r.u64(out.when_mono) && r.i64(out.when_wall) && r.string(out.reason);
}
void write_drain(Writer& w, const DrainRecord& d) {
  write_id(w, d.id); write_id(w, d.replica_id); write_id(w, d.set_id); write_enum(w, d.state);
  w.u64(d.started_mono); w.u64(d.completed_mono); w.boolean(d.force_cancelled); w.u64(d.quiesced_requests); w.string(d.reason);
}
bool read_drain(Reader& r, DrainRecord& out) {
  if (!read_id(r, out.id) || !read_id(r, out.replica_id) || !read_id(r, out.set_id)) return false;
  std::uint8_t a; if (!r.u8(a) || !from_int(out.state, a)) return false;
  return r.u64(out.started_mono) && r.u64(out.completed_mono) && r.boolean(out.force_cancelled) && r.u64(out.quiesced_requests) && r.string(out.reason);
}
void write_failover(Writer& w, const FailoverRecord& f) {
  write_id(w, f.id); write_id(w, f.set_id); write_id(w, f.failed_replica); write_id(w, f.replacement); write_enum(w, f.state);
  write_id(w, f.epoch); w.u64(f.started_mono); w.u64(f.completed_mono); w.boolean(f.generation_preserved);
  w.u64(f.ambiguous_outcomes.size()); for (const auto& o : f.ambiguous_outcomes) write_enum(w, o);
  w.string(f.reason);
}
bool read_failover(Reader& r, FailoverRecord& out) {
  if (!read_id(r, out.id) || !read_id(r, out.set_id) || !read_id(r, out.failed_replica) || !read_id(r, out.replacement)) return false;
  std::uint8_t a; if (!r.u8(a) || !from_int(out.state, a)) return false;
  if (!read_id(r, out.epoch)) return false;
  if (!r.u64(out.started_mono) || !r.u64(out.completed_mono) || !r.boolean(out.generation_preserved)) return false;
  std::uint64_t n; if (!r.u64(n)) return false; if (n > r.remaining()) return false; out.ambiguous_outcomes.clear();
  for (std::uint64_t i = 0; i < n; ++i) { WorkOutcome o; std::uint8_t b; if (!r.u8(b) || !from_int(o, b)) return false; out.ambiguous_outcomes.push_back(o); }
  return r.string(out.reason);
}

}  // namespace (anon)
std::vector<std::uint8_t> make_snapshot(const ReplicaSetController& ctrl) {
  Writer w;
  w.bytes(kMagic, 8);
  w.u32(Snapshot::kFormatVersion);
  w.u32(Snapshot::kSchemaVersion);
  Writer body;

  std::vector<const ReplicaSetState*> sets;
  for (const auto& [id, s] : ctrl.sets()) { sets.push_back(&s); (void)id; }
  std::sort(sets.begin(), sets.end(), [](const ReplicaSetState* a, const ReplicaSetState* b) { return a->id < b->id; });
  body.u64(sets.size());
  for (const auto* s : sets) write_replica_set(body, *s);

  std::vector<const ReplicaState*> reps;
  for (const auto& [id, r] : ctrl.replicas()) { reps.push_back(&r); (void)id; }
  std::sort(reps.begin(), reps.end(), [](const ReplicaState* a, const ReplicaState* b) { return a->id < b->id; });
  body.u64(reps.size());
  for (const auto* r : reps) write_replica(body, *r);

  std::vector<const WorkerRegistration*> ws;
  for (const auto& [id, wr] : ctrl.workers()) { ws.push_back(&wr); (void)id; }
  std::sort(ws.begin(), ws.end(), [](const WorkerRegistration* a, const WorkerRegistration* b) { return a->worker_id < b->worker_id; });
  body.u64(ws.size());
  for (const auto* wr : ws) write_worker(body, *wr);

  const auto& promos = ctrl.promotions();
  body.u64(promos.size());
  for (const auto& p : promos) write_promotion(body, p);
  const auto& drains = ctrl.drains();
  body.u64(drains.size());
  for (const auto& d : drains) write_drain(body, d);
  const auto& fails = ctrl.failovers();
  body.u64(fails.size());
  for (const auto& f : fails) write_failover(body, f);

  write_id(body, ctrl.epoch());

  w.u64(body.buffer().size());
  w.bytes(body.buffer().data(), body.buffer().size());
  w.u64(crc64(w.buffer().data(), w.buffer().size()));
  return w.buffer();
}

std::optional<RecoveredState> decode_snapshot(const std::vector<std::uint8_t>& bytes,
                                              std::string* error) {
  auto set_err = [&](const std::string& e) { if (error) *error = e; };
  const std::size_t n = bytes.size();
  if (n < 24 + 8) { set_err("snapshot too short"); return std::nullopt; }
  const std::uint8_t* data = bytes.data();
  if (std::memcmp(data, kMagic, 8) != 0) { set_err("bad magic"); return std::nullopt; }

  Reader hdr(data, n);
  std::uint8_t mg[8];
  if (!hdr.bytes(mg, 8)) { set_err("bad magic"); return std::nullopt; }
  std::uint32_t ver = 0, schema = 0;
  if (!hdr.u32(ver) || !hdr.u32(schema)) { set_err("header truncation"); return std::nullopt; }
  if (ver != Snapshot::kFormatVersion) { set_err("incompatible format version"); return std::nullopt; }
  if (schema != Snapshot::kSchemaVersion) { set_err("incompatible schema version"); return std::nullopt; }
  std::uint64_t body_len = 0;
  if (!hdr.u64(body_len)) { set_err("body length truncation"); return std::nullopt; }

  const std::size_t body_start = 24;
  const std::size_t body_end = body_start + static_cast<std::size_t>(body_len);
  if (body_end > n || n - body_end != 8) { set_err("body length mismatch or trailing garbage"); return std::nullopt; }
  std::uint64_t stored_crc = 0;
  for (int i = 0; i < 8; ++i) stored_crc |= static_cast<std::uint64_t>(data[body_end + static_cast<std::size_t>(i)]) << (8 * i);
  if (crc64(data, body_end) != stored_crc) { set_err("checksum corruption"); return std::nullopt; }

  Reader body(data + body_start, static_cast<std::size_t>(body_len));
  RecoveredState state;

  std::uint64_t nset = 0; if (!body.u64(nset)) { set_err("set count"); return std::nullopt; }
  if (nset > body.remaining()) { set_err("set count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < nset; ++i) { ReplicaSetState s; if (!read_replica_set(body, s)) { set_err("set decode: " + body.error()); return std::nullopt; } if (!state.sets.emplace(s.id, std::move(s)).second) { set_err("duplicate replica-set id"); return std::nullopt; } }

  std::uint64_t nrep = 0; if (!body.u64(nrep)) { set_err("replica count"); return std::nullopt; }
  if (nrep > body.remaining()) { set_err("replica count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < nrep; ++i) { ReplicaState r; if (!read_replica(body, r)) { set_err("replica decode: " + body.error()); return std::nullopt; } if (!state.replicas.emplace(r.id, std::move(r)).second) { set_err("duplicate replica id"); return std::nullopt; } }

  std::uint64_t nw = 0; if (!body.u64(nw)) { set_err("worker count"); return std::nullopt; }
  if (nw > body.remaining()) { set_err("worker count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < nw; ++i) { WorkerRegistration w; if (!read_worker(body, w)) { set_err("worker decode: " + body.error()); return std::nullopt; } state.workers.push_back(std::move(w)); }

  std::uint64_t np = 0; if (!body.u64(np)) { set_err("promotion count"); return std::nullopt; }
  if (np > body.remaining()) { set_err("promotion count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < np; ++i) { PromotionRecord p; if (!read_promotion(body, p)) { set_err("promotion decode: " + body.error()); return std::nullopt; } state.promotions.push_back(std::move(p)); }

  std::uint64_t ndr = 0; if (!body.u64(ndr)) { set_err("drain count"); return std::nullopt; }
  if (ndr > body.remaining()) { set_err("drain count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < ndr; ++i) { DrainRecord d; if (!read_drain(body, d)) { set_err("drain decode: " + body.error()); return std::nullopt; } state.drains.push_back(std::move(d)); }

  std::uint64_t nf = 0; if (!body.u64(nf)) { set_err("failover count"); return std::nullopt; }
  if (nf > body.remaining()) { set_err("failover count overflow"); return std::nullopt; }
  for (std::uint64_t i = 0; i < nf; ++i) { FailoverRecord f; if (!read_failover(body, f)) { set_err("failover decode: " + body.error()); return std::nullopt; } state.failovers.push_back(std::move(f)); }

  CoordinatorEpoch ep;
  if (!read_id(body, ep)) { set_err("epoch decode"); return std::nullopt; }
  if (!body.fully_consumed()) { set_err("trailing bytes in body"); return std::nullopt; }
  return state;
}

std::optional<std::string> validate_recovered_state(const RecoveredState& state) {
  for (const auto& [id, r] : state.replicas) {
    (void)id;
    if (r.id.is_null() || r.set_id.is_null() || r.generation.is_null()) return "replica with null identity in recovered state";
    if (!state.sets.count(r.set_id)) return "replica references an unknown replica set";
  }
  for (const auto& [sid, s] : state.sets) {
    (void)sid;
    if (s.id.is_null()) return "replica set with null identity";
    if (s.max_replicas == 0 || s.desired_count == 0 || s.min_healthy == 0 || s.min_healthy > s.max_replicas || s.desired_count > s.max_replicas)
      return "replica set counts invalid in recovered state";
  }
  return std::nullopt;
}

void install_recovered_state(const RecoveredState& state, ReplicaSetController& ctrl) {
  ctrl.sets().clear(); ctrl.replicas().clear(); ctrl.workers().clear();
  ctrl.promotions_mut().clear(); ctrl.drains_mut().clear(); ctrl.failovers_mut().clear();
  for (const auto& [id, s] : state.sets) ctrl.sets().emplace(id, s);
  for (const auto& [id, r] : state.replicas) {
    ReplicaState r2 = r;
    r2.serving_eligible = false;
    r2.boot_id = WorkerBootId();
    r2.health = HealthState::STARTING;
    r2.health_record.state = HealthState::STARTING;
    r2.readiness = ReadinessState::UNKNOWN;
    r2.warmth = WarmthState::COLD;
    r2.lifecycle = ReplicaLifecycle::DECLARED;
    r2.promotion = PromotionState::NOT_PROMOTED;
    r2.role = ReplicaRole::NONE;
    r2.active_requests = 0;
    r2.reserved_capacity = 0;
    ctrl.replicas().emplace(id, std::move(r2));
  }
  for (const auto& w : state.workers) ctrl.workers().emplace(w.worker_id, w);
  for (const auto& p : state.promotions) ctrl.promotions_mut().push_back(p);
  for (const auto& d : state.drains) ctrl.drains_mut().push_back(d);
  for (const auto& f : state.failovers) ctrl.failovers_mut().push_back(f);
}

bool recover_into(const std::vector<std::uint8_t>& bytes, ReplicaSetController& ctrl, std::string* error) {
  auto rec = decode_snapshot(bytes, error);
  if (!rec) return false;
  auto verr = validate_recovered_state(*rec);
  if (verr) { if (error) *error = *verr; return false; }
  install_recovered_state(*rec, ctrl);
  return true;
}

std::vector<std::uint8_t> capture_and_clear_authority(const ReplicaSetController& ctrl) {
  return make_snapshot(ctrl);
}

}  // namespace replicafabric
