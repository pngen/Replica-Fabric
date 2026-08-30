#pragma once

// Replica Fabric - strongly typed 128-bit identities.
//
// Every named identity in the system (ReplicaId, ReplicaSetId, ...) is a
// distinct type over a 128-bit value. Distinct identities never implicitly
// convert to one another, so a ReplicaId cannot silently be used where a
// ModelId is required, and stale generation/authority identities cannot be
// confused across reuse.

#include <array>
#include <cstdint>
#include <functional>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

namespace replicafabric {

// Thrown when textual identity parsing fails.
class IdentityParseError : public std::invalid_argument {
public:
  using std::invalid_argument::invalid_argument;
};

namespace detail {

constexpr int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Write a 64-bit value as 16 lowercase hex characters into buf (buf>=17).
inline void write_hex(uint64_t v, char* buf) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (int i = 0; i < 16; ++i) {
    buf[i] = kHex[(v >> ((15 - i) * 4)) & 0xFULL];
  }
  buf[16] = '\0';
}

inline bool parse_hex(std::string_view s, uint64_t& out) noexcept {
  if (s.size() != 16) return false;
  uint64_t v = 0;
  bool any = false;
  for (char c : s) {
    const int d = hex_digit(c);
    if (d < 0) return false;
    v = (v << 4) | static_cast<uint64_t>(d);
    any = true;
  }
  if (!any) return false;
  out = v;
  return true;
}

}  // namespace detail

// A 128-bit identity value. Tag establishes the distinct identity kind.
template <typename Tag>
class Id {
public:
  using TagType = Tag;

  constexpr Id() noexcept = default;

  constexpr Id(uint64_t hi, uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

  static Id null() noexcept { return Id(); }

  static Id from_bytes(const std::array<uint8_t, 16>& b) noexcept {
    uint64_t hi = 0;
    uint64_t lo = 0;
    for (int i = 0; i < 8; ++i) hi = (hi << 8) | static_cast<uint64_t>(b[i]);
    for (int i = 0; i < 8; ++i) lo = (lo << 8) | static_cast<uint64_t>(b[8 + i]);
    return Id(hi, lo);
  }

  std::array<uint8_t, 16> to_bytes() const noexcept {
    std::array<uint8_t, 16> b{};
    for (int i = 0; i < 8; ++i) b[i] = static_cast<uint8_t>((hi_ >> ((7 - i) * 8)) & 0xFFULL);
    for (int i = 0; i < 8; ++i) b[8 + i] = static_cast<uint8_t>((lo_ >> ((7 - i) * 8)) & 0xFFULL);
    return b;
  }

  // Canonical 32-char lowercase hex, no separators.
  std::string str() const {
    std::string s;
    s.resize(32);
    char buf[17];
    detail::write_hex(hi_, buf);
    s.replace(0, 16, buf, 16);
    detail::write_hex(lo_, buf);
    s.replace(16, 16, buf, 16);
    return s;
  }

  // Accepts 32 hex digits with optional hyphens in 8-4-4-4-12 positions, then
  // normalizes; throws IdentityParseError on malformed input.
  static Id from_string(std::string_view s) {
    std::string cleaned;
    cleaned.reserve(32);
    for (char c : s) {
      if (c == '-') continue;
      if (detail::hex_digit(c) < 0) {
        throw IdentityParseError("invalid character in identity");
      }
      cleaned.push_back(c);
    }
    if (cleaned.size() != 32) {
      throw IdentityParseError("identity must contain 32 hex digits");
    }
    uint64_t hi = 0;
    uint64_t lo = 0;
    if (!detail::parse_hex(std::string_view(cleaned).substr(0, 16), hi) ||
        !detail::parse_hex(std::string_view(cleaned).substr(16, 16), lo)) {
      throw IdentityParseError("identity parse failed");
    }
    return Id(hi, lo);
  }

  // Strict round-trip parse of the canonical form; returns false on failure.
  static bool try_parse(std::string_view s, Id& out) noexcept {
    if (s.size() != 32) return false;
    uint64_t hi = 0;
    uint64_t lo = 0;
    if (!detail::parse_hex(s.substr(0, 16), hi)) return false;
    if (!detail::parse_hex(s.substr(16, 16), lo)) return false;
    out = Id(hi, lo);
    return true;
  }

  bool is_null() const noexcept { return hi_ == 0 && lo_ == 0; }
  uint64_t hi() const noexcept { return hi_; }
  uint64_t lo() const noexcept { return lo_; }

  template <typename Rng>
  static Id random(Rng& rng) {
    std::uniform_int_distribution<uint64_t> dist;
    return Id(dist(rng), dist(rng));
  }

  friend bool operator==(const Id& a, const Id& b) noexcept {
    return a.hi_ == b.hi_ && a.lo_ == b.lo_;
  }
  friend bool operator!=(const Id& a, const Id& b) noexcept { return !(a == b); }
  friend bool operator<(const Id& a, const Id& b) noexcept {
    if (a.hi_ != b.hi_) return a.hi_ < b.hi_;
    return a.lo_ < b.lo_;
  }
  friend bool operator>(const Id& a, const Id& b) noexcept { return b < a; }
  friend bool operator<=(const Id& a, const Id& b) noexcept { return !(b < a); }
  friend bool operator>=(const Id& a, const Id& b) noexcept { return !(a < b); }

private:
  uint64_t hi_ = 0;
  uint64_t lo_ = 0;
};

template <typename Tag>
std::ostream& operator<<(std::ostream& os, const Id<Tag>& id) {
  return os << id.str();
}

}  // namespace replicafabric

namespace std {
template <typename Tag>
struct hash<replicafabric::Id<Tag>> {
  size_t operator()(const replicafabric::Id<Tag>& id) const noexcept {
    const uint64_t h = id.hi() ^ (id.lo() * 0x9e3779b97f4a7c15ULL);
    return static_cast<size_t>(h ^ (h >> 32));
  }
};
}  // namespace std
