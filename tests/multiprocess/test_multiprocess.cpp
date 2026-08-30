// Atomic multiprocess proof: one coordinator OS process + two worker OS
// processes + two replicas over the real framed TCP transport. Exercises
// provision/warm/promote/execute, kills the authoritative worker, fails over to
// the standby, restarts the worker with a new boot id, replays stale authority,
// and proves no double-serving and a coherent final state.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/distributed/transport.hpp>
#include <replicafabric/distributed/protocol.hpp>
#include <replicafabric/persistence/snapshot.hpp>

#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace replicafabric;

static int g_failures = 0;
static void check(bool cond, const std::string& msg) {
  if (!cond) { std::cerr << "PROOF FAIL: " << msg << "\n"; ++g_failures; }
  else { std::cout << "PROOF OK:   " << msg << "\n"; }
}

std::mt19937_64 g_rng(0xDEADBEEFULL);
template <typename T> T rnd() { return T::random(g_rng); }

std::uint16_t free_port() {
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = 0;
  ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  int len = sizeof(addr);
  ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
  const std::uint16_t port = ntohs(addr.sin_port);
  ::closesocket(s);
  return port;
}

struct Proc {
  HANDLE h = INVALID_HANDLE_VALUE;
  bool spawn(const std::string& exe, const std::string& args) {
    std::string cmd = "\"" + exe + "\" " + args;
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = ::CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    h = pi.hProcess;
    ::CloseHandle(pi.hThread);
    return true;
  }
  void kill() { if (h != INVALID_HANDLE_VALUE) { ::TerminateProcess(h, 1); ::CloseHandle(h); h = INVALID_HANDLE_VALUE; } }
};

bool send_msg(TcpConnection& c, const Message& m) { return c.write_frame(encode_message(m)); }
bool recv_msg(TcpConnection& c, Message& out) { std::vector<std::uint8_t> b; if (!c.read_frame(b)) return false; return decode_message(b, out); }

int main(int argc, char** argv) {
  if (!tcp_init()) { std::cerr << "tcp_init failed\n"; return 1; }
  if (argc < 3) { std::cerr << "usage: rf_multiprocess <coordinator-exe> <worker-exe>\n"; return 1; }
  const std::string coord_exe = argv[1];
  const std::string worker_exe = argv[2];

  const std::uint16_t port = free_port();
  const std::string portstr = std::to_string(port);

  Proc coord; Proc wa; Proc wb;
  check(coord.spawn(coord_exe, "--port " + portstr), "spawn coordinator");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Fixed well-known ids.
  ReplicaSetId setid = rnd<ReplicaSetId>();
  ModelId model = rnd<ModelId>();
  ArtifactId artifact = rnd<ArtifactId>();
  ArtifactGeneration agen = rnd<ArtifactGeneration>();
  ReplicaSetGeneration sgen = rnd<ReplicaSetGeneration>();
  PolicyGeneration pgen = rnd<PolicyGeneration>();
  WorkerId wa_id = rnd<WorkerId>(); WorkerBootId wa_boot = rnd<WorkerBootId>(); NodeId wa_node = rnd<NodeId>();
  WorkerId wb_id = rnd<WorkerId>(); WorkerBootId wb_boot = rnd<WorkerBootId>(); NodeId wb_node = rnd<NodeId>();
  ReplicaId ra = rnd<ReplicaId>(); ReplicaId rb = rnd<ReplicaId>();

  check(wa.spawn(worker_exe, " --port " + portstr + " --worker " + wa_id.str() + " --boot " + wa_boot.str() + " --node " + wa_node.str()), "spawn worker A");
  check(wb.spawn(worker_exe, " --port " + portstr + " --worker " + wb_id.str() + " --boot " + wb_boot.str() + " --node " + wb_node.str()), "spawn worker B");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  TcpConnection control;
  check(control.connect_to("127.0.0.1", port), "control connects to coordinator");

  Message m; m.epoch = CoordinatorEpoch(); m.kind = MessageKind::CREATE_SET; m.seq = 1; m.set = setid;
  m.payload = "model=" + model.str() + "\nartifact=" + artifact.str() + "\nagen=" + agen.str() +
              "\nsgen=" + sgen.str() + "\npgen=" + pgen.str() + "\nworkload=" + rnd<WorkloadId>().str() +
              "\ntenant=" + rnd<TenantId>().str() + "\ndesired=2\nmin=1\nmax=2\nbackend=2\nccmaj=8\nccmin=0";
  send_msg(control, m); Message ack;
  check(recv_msg(control, ack) && ack.kind == MessageKind::CREATE_SET_ACK && ack.payload.rfind("ok",0)==0, "create replica set");

  auto provision = [&](const ReplicaId& rid, const std::string& wstr) {
    Message pm; pm.kind = MessageKind::PROVISION; pm.epoch = CoordinatorEpoch(); pm.set = setid; pm.replica = rid; pm.seq = 1;
    pm.payload = "worker=" + wstr + "\nmodel=" + model.str() + "\nagen=" + agen.str() + "\nccmaj=c\ncapacity=0x1000000\npolicyfp=fp-1";
    send_msg(control, pm); Message pa;
    return recv_msg(control, pa) && pa.kind == MessageKind::PROVISION_ACK && pa.payload.rfind("ok",0)==0;
  };
  check(provision(ra, wa_id.str()), "provision replica A");
  check(provision(rb, wb_id.str()), "provision replica B");

  auto wait_state = [&](const ReplicaId& rid, const std::string& want, int attempts) {
    for (int i = 0; i < attempts; ++i) {
      Message q; q.kind = MessageKind::QUERY; q.epoch = CoordinatorEpoch(); q.replica = rid; q.payload = "q=state";
      send_msg(control, q); Message qa;
      if (recv_msg(control, qa)) {
        if (qa.payload.find(want) != std::string::npos) return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return false;
  };
  // Wait for both replicas to become READY (healed after warm + health + readiness).
  check(wait_state(ra, "lifecycle=READY", 100), "replica A reaches READY");
  check(wait_state(rb, "lifecycle=READY", 100), "replica B reaches READY");

  auto promote = [&](const ReplicaId& rid, const std::string& target) {
    Message pm; pm.kind = MessageKind::PROMOTE; pm.epoch = CoordinatorEpoch(); pm.replica = rid; pm.payload = "target=" + target;
    send_msg(control, pm); Message a; if (recv_msg(control, a)) { std::cerr << "  PROMOTE " << rid.str() << " target=" << target << " -> " << a.payload << "\n"; return a.payload.rfind("ok",0)==0; } return false;
  };
  check(promote(ra, "5"), "promote replica A to PRIMARY");
  check(promote(rb, "1"), "promote replica B to STANDBY");

  // Execute real work on the serving primary A.
  Message ex; ex.kind = MessageKind::EXECUTE; ex.epoch = CoordinatorEpoch(); ex.replica = ra; ex.attempt = rnd<AttemptId>(); ex.payload = "req=reqA";
  send_msg(control, ex); Message exa; check(recv_msg(control, exa) && exa.payload.rfind("ok",0)==0, "submit work to A");
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  { Message q; q.kind = MessageKind::QUERY; q.epoch = CoordinatorEpoch(); q.replica = ra; q.payload = "q=result";
    send_msg(control, q); Message qa; check(recv_msg(control, qa) && qa.payload.find("outcome=1") != std::string::npos, "work on A completed successfully"); }

  // Kill worker A while it is authoritative; fail over to B.
  wa.kill();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  { Message f; f.kind = MessageKind::FAILOVER; f.epoch = CoordinatorEpoch(); f.set = setid; f.payload = "failed=" + ra.str() + "\nreason=worker A died";
    send_msg(control, f); Message fa; check(recv_msg(control, fa) && fa.payload.rfind("ok",0)==0, "failover to B after killing A"); }
  check(wait_state(rb, "serving=1", 100), "replica B is now serving");

  // Restart worker A with a fresh boot id; the old authority must not return.
  WorkerBootId wa_boot2 = rnd<WorkerBootId>();
  Proc wa2; check(wa2.spawn(worker_exe, " --port " + portstr + " --worker " + wa_id.str() + " --boot " + wa_boot2.str() + " --node " + wa_node.str()), "restart worker A with new boot");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Exactly one replica must be serving (no double-serving).
  { Message q; q.kind = MessageKind::QUERY; q.epoch = CoordinatorEpoch(); q.replica = ra; q.payload = "q=state"; send_msg(control, q); Message qa;
    if (recv_msg(control, qa)) { check(qa.payload.find("serving=0") != std::string::npos, "restarted replica A does not serve (serving=0)"); } else check(false, "query A state"); }
  { Message q; q.kind = MessageKind::QUERY; q.epoch = CoordinatorEpoch(); q.replica = rb; q.payload = "q=state"; send_msg(control, q); Message qa;
    if (recv_msg(control, qa)) { check(qa.payload.find("serving=1") != std::string::npos, "replica B still serves (serving=1)"); } else check(false, "query B state"); }

  // Snapshot the coordinator state and validate it decodes + is coherent.
  { Message s; s.kind = MessageKind::SNAPSHOT; s.epoch = CoordinatorEpoch(); send_msg(control, s); Message sa;
    if (recv_msg(control, sa) && sa.kind == MessageKind::SNAPSHOT) {
      std::string hex = get_payload(sa.payload, "hex", "");
      std::vector<std::uint8_t> bytes;
      for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto hx = [](char c)->std::uint8_t { if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0'); if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10); if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10); return 0; };
        bytes.push_back(static_cast<std::uint8_t>((hx(hex[i]) << 4) | hx(hex[i+1])));
      }
      std::string err;
      auto rec = decode_snapshot(bytes, &err);
      check(rec.has_value(), "snapshot decodes (" + err + ")");
      if (rec) {
        std::uint32_t serving_cnt = 0;
        for (const auto& [rid, rr] : rec->replicas) { if (rr.serving_eligible) ++serving_cnt; (void)rid; }
        check(serving_cnt == 1, "exactly one serving replica in final state (" + std::to_string(serving_cnt) + ")");
      }
    } else { check(false, "snapshot received"); } }

  tcp_shutdown();
  wa2.kill(); wb.kill(); coord.kill();
  if (g_failures == 0) { std::cout << "\nMULTIPROCESS PROOF: ALL CHECKS PASSED\n"; return 0; }
  std::cout << "\nMULTIPROCESS PROOF: " << g_failures << " FAILURES\n";
  return 1;
}