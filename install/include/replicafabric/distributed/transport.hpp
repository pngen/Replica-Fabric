#pragma once

// Replica Fabric - framed TCP transport (Windows-first Winsock).
//
// Messages are length-prefixed frames: a 4-byte big-endian length followed by
// the payload. Framing is strict: oversized lengths, truncated payloads, and
// partial reads are rejected. Blocking I/O is used; the coordinator and worker
// each run on their own thread so no blocking I/O is performed while a replica
// state lock is held.

#include <cstdint>
#include <string>
#include <vector>

namespace replicafabric {

class TcpConnection {
public:
  TcpConnection() = default;
  TcpConnection(const TcpConnection&) = delete;
  TcpConnection& operator=(const TcpConnection&) = delete;
  TcpConnection(TcpConnection&& other) noexcept;
  TcpConnection& operator=(TcpConnection&& other) noexcept;
  ~TcpConnection();

  // Client: connect to host:port. Returns false on failure.
  bool connect_to(const std::string& host, std::uint16_t port);

  // Server: bind/listen on port (all interfaces). Returns false on failure.
  bool listen_on(std::uint16_t port);

  // Accept one incoming connection (blocking). Returns false on failure.
  bool accept_one(TcpConnection& out);

  // Framed send/recv. length_prefix includes the payload size (max 16 MiB).
  bool write_frame(const std::vector<std::uint8_t>& payload);
  bool read_frame(std::vector<std::uint8_t>& payload);

  // Raw helpers.
  bool send_all(const std::uint8_t* data, std::size_t n);
  bool recv_exact(std::uint8_t* data, std::size_t n);

  void close();
  bool valid() const noexcept { return sock_ != invalid_socket(); }
  void set_timeout_ms(int ms);

private:
  static std::uintptr_t invalid_socket();
  std::uintptr_t sock_ = invalid_socket();
};

// Initialize/finalize Winsock once for the process.
bool tcp_init();
void tcp_shutdown();

}  // namespace replicafabric
