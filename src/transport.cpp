#include <replicafabric/distributed/transport.hpp>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>

namespace replicafabric {

namespace {
constexpr std::uint32_t kMaxFrame = 16u * 1024u * 1024u;  // 16 MiB
bool g_ws_init = false;
}

bool tcp_init() {
  if (g_ws_init) return true;
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
  g_ws_init = true;
  return true;
}
void tcp_shutdown() {
  if (g_ws_init) {
    WSACleanup();
    g_ws_init = false;
  }
}

std::uintptr_t TcpConnection::invalid_socket() { return static_cast<std::uintptr_t>(INVALID_SOCKET); }

TcpConnection::TcpConnection(TcpConnection&& other) noexcept : sock_(other.sock_) { other.sock_ = invalid_socket(); }
TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
  if (this != &other) {
    close();
    sock_ = other.sock_;
    other.sock_ = invalid_socket();
  }
  return *this;
}
TcpConnection::~TcpConnection() { close(); }
void TcpConnection::close() {
  if (valid()) {
    ::closesocket(static_cast<SOCKET>(sock_));
    sock_ = invalid_socket();
  }
}

void TcpConnection::set_timeout_ms(int ms) {
  if (!valid()) return;
  const DWORD t = static_cast<DWORD>(ms);
  ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
  ::setsockopt(static_cast<SOCKET>(sock_), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}

bool TcpConnection::connect_to(const std::string& host, std::uint16_t port) {
  if (!tcp_init()) return false;
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  const std::string portstr = std::to_string(port);
  if (::getaddrinfo(host.c_str(), portstr.c_str(), &hints, &result) != 0) return false;
  for (struct addrinfo* ai = result; ai; ai = ai->ai_next) {
    SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (s == INVALID_SOCKET) continue;
    if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
      sock_ = static_cast<std::uintptr_t>(s);
      ::freeaddrinfo(result);
      return true;
    }
    ::closesocket(s);
  }
  ::freeaddrinfo(result);
  return false;
}

bool TcpConnection::listen_on(std::uint16_t port) {
  if (!tcp_init()) return false;
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  BOOL reuse = TRUE;
  ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::closesocket(s);
    return false;
  }
  if (::listen(s, SOMAXCONN) != 0) {
    ::closesocket(s);
    return false;
  }
  sock_ = static_cast<std::uintptr_t>(s);
  return true;
}

bool TcpConnection::accept_one(TcpConnection& out) {
  if (!valid()) return false;
  sockaddr_in cliaddr{};
  int len = static_cast<int>(sizeof(cliaddr));
  SOCKET c = ::accept(static_cast<SOCKET>(sock_), reinterpret_cast<sockaddr*>(&cliaddr), &len);
  if (c == INVALID_SOCKET) return false;
  out.close();
  out.sock_ = static_cast<std::uintptr_t>(c);
  return true;
}

bool TcpConnection::send_all(const std::uint8_t* data, std::size_t n) {
  if (!valid()) return false;
  std::size_t sent = 0;
  while (sent < n) {
    int chunk = static_cast<int>(n - sent);
    if (chunk > 1 << 20) chunk = 1 << 20;
    const int r = ::send(static_cast<SOCKET>(sock_), reinterpret_cast<const char*>(data + sent), chunk, 0);
    if (r == SOCKET_ERROR) return false;
    sent += static_cast<std::size_t>(r);
  }
  return true;
}

bool TcpConnection::recv_exact(std::uint8_t* data, std::size_t n) {
  if (!valid()) return false;
  std::size_t got = 0;
  while (got < n) {
    const int r = ::recv(static_cast<SOCKET>(sock_), reinterpret_cast<char*>(data + got), static_cast<int>(n - got), 0);
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

bool TcpConnection::write_frame(const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxFrame) return false;
  std::array<std::uint8_t, 4> hdr{};
  const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
  // big-endian length prefix
  hdr[0] = static_cast<std::uint8_t>((len >> 24) & 0xFF);
  hdr[1] = static_cast<std::uint8_t>((len >> 16) & 0xFF);
  hdr[2] = static_cast<std::uint8_t>((len >> 8) & 0xFF);
  hdr[3] = static_cast<std::uint8_t>(len & 0xFF);
  if (!send_all(hdr.data(), 4)) return false;
  if (!payload.empty() && !send_all(payload.data(), payload.size())) return false;
  return true;
}

bool TcpConnection::read_frame(std::vector<std::uint8_t>& payload) {
  std::array<std::uint8_t, 4> hdr{};
  if (!recv_exact(hdr.data(), 4)) return false;
  const std::uint32_t len = (static_cast<std::uint32_t>(hdr[0]) << 24) |
                            (static_cast<std::uint32_t>(hdr[1]) << 16) |
                            (static_cast<std::uint32_t>(hdr[2]) << 8) |
                            static_cast<std::uint32_t>(hdr[3]);
  if (len > kMaxFrame) return false;
  payload.resize(len);
  if (len > 0 && !recv_exact(payload.data(), len)) return false;
  return true;
}

}  // namespace replicafabric