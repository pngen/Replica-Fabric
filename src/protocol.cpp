#include <replicafabric/distributed/protocol.hpp>

#include <array>
#include <cstring>

namespace replicafabric {

namespace {
template <typename IdT> void read_id(const std::uint8_t* p, IdT& out) {
  std::array<std::uint8_t, 16> b{};
  std::memcpy(b.data(), p, 16);
  out = IdT::from_bytes(b);
}
template <typename IdT> void write_id(std::vector<std::uint8_t>& v, const IdT& id) {
  const auto b = id.to_bytes();
  const std::uint8_t* p = b.data();
  v.insert(v.end(), p, p + 16);
}
void write_u8(std::vector<std::uint8_t>& v, std::uint8_t x) { v.push_back(x); }
void write_u64(std::vector<std::uint8_t>& v, std::uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back(static_cast<std::uint8_t>((x >> (8*i)) & 0xFF)); }
bool read_u8(const std::uint8_t* p, std::size_t n, std::size_t& pos, std::uint8_t& out) { if (pos + 1 > n) return false; out = p[pos++]; return true; }
bool read_u64(const std::uint8_t* p, std::size_t n, std::size_t& pos, std::uint64_t& out) { if (pos + 8 > n) return false; out = 0; for (int i = 0; i < 8; ++i) out |= static_cast<std::uint64_t>(p[pos+i]) << (8*i); pos += 8; return true; }
}  // namespace

std::vector<std::uint8_t> encode_message(const Message& m) {
  std::vector<std::uint8_t> v;
  write_u8(v, static_cast<std::uint8_t>(m.kind));
  write_u64(v, m.seq);
  write_id(v, m.epoch);
  write_id(v, m.worker);
  write_id(v, m.boot);
  write_id(v, m.replica);
  write_id(v, m.set);
  write_id(v, m.attempt);
  // payload as u32 length + bytes
  const std::uint32_t plen = static_cast<std::uint32_t>(m.payload.size());
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>((plen >> (8*i)) & 0xFF));
  v.insert(v.end(), m.payload.begin(), m.payload.end());
  return v;
}

bool decode_message(const std::vector<std::uint8_t>& bytes, Message& out) {
  std::size_t pos = 0;
  std::uint8_t kind = 0;
  if (!read_u8(bytes.data(), bytes.size(), pos, kind)) return false;
  out.kind = static_cast<MessageKind>(kind);
  std::uint64_t seq = 0;
  if (!read_u64(bytes.data(), bytes.size(), pos, seq)) return false;
  out.seq = seq;
  read_id(bytes.data() + pos, out.epoch); pos += 16;
  read_id(bytes.data() + pos, out.worker); pos += 16;
  read_id(bytes.data() + pos, out.boot); pos += 16;
  read_id(bytes.data() + pos, out.replica); pos += 16;
  read_id(bytes.data() + pos, out.set); pos += 16;
  read_id(bytes.data() + pos, out.attempt); pos += 16;
  if (pos + 4 > bytes.size()) return false;
  std::uint32_t plen = 0;
  for (int i = 0; i < 4; ++i) plen |= static_cast<std::uint32_t>(bytes[pos+i]) << (8*i);
  pos += 4;
  if (pos + plen > bytes.size()) return false;
  out.payload.assign(reinterpret_cast<const char*>(bytes.data() + pos), plen);
  pos += plen;
  return pos == bytes.size();
}

void set_payload(std::string& s, const std::string& k, const std::string& v) {
  s += k; s += '='; s += v; s += '\n';
}
std::string get_payload(const std::string& s, const std::string& k, const std::string& def) {
  const std::string needle = k + "=";
  std::size_t pos = 0;
  while (pos <= s.size()) {
    const std::size_t nl = s.find('\n', pos);
    const std::string line = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (line.size() >= needle.size() && line.compare(0, needle.size(), needle) == 0) {
      return line.substr(needle.size());
    }
    if (nl == std::string::npos) break;
    pos = nl + 1;
  }
  return def;
}

}  // namespace replicafabric