// rf_worker - Replica Fabric worker process.
#include <replicafabric/core/identity_kinds.hpp>
#include <replicafabric/core/enums.hpp>
#include <replicafabric/distributed/transport.hpp>
#include <replicafabric/distributed/protocol.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace replicafabric;

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  WorkerId wid; WorkerBootId bid; NodeId nid;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::stoul(argv[++i]));
    else if (a == "--worker" && i + 1 < argc) wid = WorkerId::from_string(argv[++i]);
    else if (a == "--boot" && i + 1 < argc) bid = WorkerBootId::from_string(argv[++i]);
    else if (a == "--node" && i + 1 < argc) nid = NodeId::from_string(argv[++i]);
  }
  if (port == 0 || wid.is_null()) { std::cerr << "rf_worker: --port/--worker required\n"; return 2; }

  TcpConnection conn;
  if (!conn.connect_to(host, port)) { std::cerr << "rf_worker: connect failed\n"; return 3; }

  Message reg; reg.kind = MessageKind::REGISTER; reg.worker = wid; reg.boot = bid;
  reg.payload = "node_hex=" + nid.str() + "\nbackend=" + std::to_string(static_cast<int>(BackendKind::TRITON)) +
                "\nmem=0x800000000\ndevice=cuda:0\nccmaj=c\ndevmem=0x800000000";
  conn.write_frame(encode_message(reg));

  std::vector<std::uint8_t> ack;
  if (!conn.read_frame(ack)) return 4;
  Message am; decode_message(ack, am);
  if (am.kind != MessageKind::REGISTER_ACK || am.payload.rfind("ok", 0) != 0) {
    std::cerr << "rf_worker: register rejected: " << am.payload << "\n";
    return 5;
  }
  std::cerr << "rf_worker: registered id=" << wid.str() << " boot=" << bid.str() << "\n";

  while (true) {
    std::vector<std::uint8_t> f;
    if (!conn.read_frame(f)) break;
    Message m;
    if (!decode_message(f, m)) continue;
    switch (m.kind) {
      case MessageKind::WARM: {
        // bounded warmup (synthetic; the CUDA-backed example does real work).
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Message done; done.kind = MessageKind::WARM_DONE; done.epoch = m.epoch;
        done.worker = wid; done.boot = bid; done.replica = m.replica; done.set = m.set;
        done.payload = "steps=4";
        conn.write_frame(encode_message(done));
        Message h; h.kind = MessageKind::HEALTH; h.epoch = m.epoch; h.worker = wid; h.boot = bid;
        h.replica = m.replica; h.set = m.set;
        h.payload = "state=2\nsource=worker";
        conn.write_frame(encode_message(h));
        Message rrmsg; rrmsg.kind = MessageKind::READINESS; rrmsg.epoch = m.epoch;
        rrmsg.worker = wid; rrmsg.boot = bid; rrmsg.replica = m.replica; rrmsg.set = m.set;
        rrmsg.payload = "mask=0x7ff";
        conn.write_frame(encode_message(rrmsg));
        break;
      }
      case MessageKind::EXECUTE: {
        std::uint64_t acc = 0;
        for (int i = 0; i < 100000; ++i) acc += static_cast<std::uint64_t>(i * 3);
        Message r; r.kind = MessageKind::EXECUTE_RESULT; r.epoch = m.epoch; r.worker = wid;
        r.boot = bid; r.replica = m.replica; r.set = m.set; r.attempt = m.attempt;
        r.payload = "req=" + get_payload(m.payload, "req", "?") + "\noutcome=1\nresult=" + std::to_string(acc);
        conn.write_frame(encode_message(r));
        break;
      }
      case MessageKind::PING: { Message p; p.kind = MessageKind::PONG; conn.write_frame(encode_message(p)); break; }
      default: break;
    }
  }
  return 0;
}
