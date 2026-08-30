#include <replicafabric/core/identity.hpp>
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

#include "replicafabric_test.hpp"

namespace {

// Distinct identity kinds are distinct C++ types; they never convert.
static_assert(!std::is_convertible_v<replicafabric::ReplicaId, replicafabric::ModelId>);
static_assert(!std::is_convertible_v<replicafabric::ReplicaSetId, replicafabric::ReplicaId>);

}  // namespace

RF_TEST_CASE(identity_round_trip_string) {
  using replicafabric::ReplicaId;
  ReplicaId id(0x0123456789abcdefULL, 0xfedcba9876543210ULL);
  const std::string s = id.str();
  RF_CHECK_EQ(s.size(), 32u);
  RF_CHECK_EQ(s, std::string("0123456789abcdeffedcba9876543210"));
  ReplicaId parsed = ReplicaId::from_string(s);
  RF_CHECK(parsed == id);
}

RF_TEST_CASE(identity_accepts_hyphenated) {
  using replicafabric::ReplicaId;
  ReplicaId id = ReplicaId::from_string(
      "01234567-89ab-cdef-fedc-ba9876543210");
  RF_CHECK(id == ReplicaId(0x0123456789abcdefULL, 0xfedcba9876543210ULL));
}

RF_TEST_CASE(identity_rejects_malformed) {
  using replicafabric::ReplicaId;
  RF_CHECK_THROWS(ReplicaId::from_string("not-a-uuid"));
  RF_CHECK_THROWS(ReplicaId::from_string("12345"));
  RF_CHECK_THROWS(ReplicaId::from_string("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz"));
  ReplicaId out;
  RF_CHECK(!ReplicaId::try_parse("short", out));
  RF_CHECK(ReplicaId::try_parse("0123456789abcdef0123456789abcdef", out));
  RF_CHECK(!out.is_null());
}

RF_TEST_CASE(identity_bytes_round_trip) {
  using replicafabric::ReplicaSetId;
  std::array<std::uint8_t, 16> b{};
  for (int i = 0; i < 16; ++i) b[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i * 17);
  ReplicaSetId a = ReplicaSetId::from_bytes(b);
  const auto back = a.to_bytes();
  RF_CHECK(back == b);
  ReplicaSetId a2 = ReplicaSetId::from_bytes(back);
  RF_CHECK(a == a2);
}

RF_TEST_CASE(identity_null_and_ordering) {
  using replicafabric::WorkerId;
  RF_CHECK(WorkerId::null().is_null());
  WorkerId a(1, 2);
  WorkerId b(1, 3);
  RF_CHECK(a < b);
  RF_CHECK(a <= b);
  RF_CHECK(b > a);
  RF_CHECK(b >= a);
  RF_CHECK(!(a == b));
  RF_CHECK(a != b);
  RF_CHECK(a < WorkerId(2, 0));
}

RF_TEST_CASE(identity_hash_distinctness) {
  using replicafabric::NodeId;
  std::hash<NodeId> h;
  // Cheap sanity: equal identities hash equal.
  NodeId a(100, 200);
  NodeId b(100, 200);
  RF_CHECK_EQ(h(a), h(b));
}

RF_TEST_CASE(enum_to_from_string_round_trip) {
  using replicafabric::ReplicaLifecycle;
  for (int i = 0; i < 20; ++i) {
    ReplicaLifecycle v;
    if (!replicafabric::from_int(v, i)) break;
    const auto s = replicafabric::to_string(v);
    ReplicaLifecycle v2;
    RF_CHECK(replicafabric::from_string(v2, s));
    RF_CHECK(v2 == v);
  }
}

RF_TEST_CASE(enum_rejects_invalid) {
  using replicafabric::ReplicaSetLifecycle;
  ReplicaSetLifecycle v;
  RF_CHECK(!replicafabric::from_string(v, "NOPE"));
  RF_CHECK(!replicafabric::from_int(v, 999));
  RF_CHECK(!replicafabric::from_int(v, -1));
  RF_CHECK(!replicafabric::from_string(v, ""));
}

RF_TEST_CASE(enum_names_are_canonical) {
  using replicafabric::HealthState;
  RF_CHECK_EQ(replicafabric::to_string(HealthState::HEALTHY), std::string_view("HEALTHY"));
  RF_CHECK_EQ(replicafabric::to_string(HealthState::QUARANTINED), std::string_view("QUARANTINED"));
}

