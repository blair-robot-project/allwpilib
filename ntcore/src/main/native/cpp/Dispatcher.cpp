// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "Dispatcher.h"

#include <algorithm>
#include <iterator>

#include <wpi/SmallVector.h>
#include <wpi/StringExtras.h>
#include <wpi/json_serializer.h>
#include <wpi/raw_ostream.h>
#include <wpi/timestamp.h>
#include <wpinet/TCPAcceptor.h>
#include <wpinet/TCPConnector.h>

#include "IConnectionNotifier.h"
#include "IStorage.h"
#include "Log.h"
#include "NetworkConnection.h"

using namespace nt;

static std::string ConnInfoToJson(bool connected, const ConnectionInfo& info) {
  std::string str;
  wpi::raw_string_ostream os{str};
  wpi::json::serializer s{os, ' ', 0};
  os << "{\"connected\":" << (connected ? "true" : "false");
  os << ",\"remote_id\":\"";
  s.dump_escaped(info.remote_id, false);
  os << "\",\"remote_ip\":\"";
  s.dump_escaped(info.remote_ip, false);
  os << "\",\"remote_port\":";
  s.dump_integer(static_cast<uint64_t>(info.remote_port));
  os << ",\"protocol_version\":";
  s.dump_integer(static_cast<uint64_t>(info.protocol_version));
  os << "}";
  os.flush();
  return str;
}

void Dispatcher::StartServer(std::string_view persist_filename,
                             const char* listen_address, unsigned int port) {
  std::string listen_address_copy(wpi::trim(listen_address));
  DispatcherBase::StartServer(
      persist_filename,
      std::unique_ptr<wpi::NetworkAcceptor>(new wpi::TCPAcceptor(
          static_cast<int>(port), listen_address_copy.c_str(), m_logger)));
}

void Dispatcher::SetServer(const char* server_name, unsigned int port) {
  std::string server_name_copy(wpi::trim(server_name));
  SetConnector([=]() -> std::unique_ptr<wpi::NetworkStream> {
    return wpi::TCPConnector::connect(server_name_copy.c_str(),
                                      static_cast<int>(port), m_logger, 1);
  });
}

void Dispatcher::SetServer(
    wpi::span<const std::pair<std::string_view, unsigned int>> servers) {
  wpi::SmallVector<std::pair<std::string, int>, 16> servers_copy;
  for (const auto& server : servers) {
    servers_copy.emplace_back(std::string{wpi::trim(server.first)},
                              static_cast<int>(server.second));
  }

  SetConnector([=]() -> std::unique_ptr<wpi::NetworkStream> {
    wpi::SmallVector<std::pair<const char*, int>, 16> servers_copy2;
    for (const auto& server : servers_copy) {
      servers_copy2.emplace_back(server.first.c_str(), server.second);
    }
    return wpi::TCPConnector::connect_parallel(servers_copy2, m_logger, 1);
  });
}

void Dispatcher::SetServerTeam(unsigned int team, unsigned int port) {
  std::pair<std::string_view, unsigned int> servers[5];

  // 10.te.am.2
  auto fixed = fmt::format("10.{}.{}.2", static_cast<int>(team / 100),
                           static_cast<int>(team % 100));
  servers[0] = {fixed, port};

  // 172.22.11.2
  servers[1] = {"172.22.11.2", port};

  // roboRIO-<team>-FRC.local
  auto mdns = fmt::format("roboRIO-{}-FRC.local", team);
  servers[2] = {mdns, port};

  // roboRIO-<team>-FRC.lan
  auto mdns_lan = fmt::format("roboRIO-{}-FRC.lan", team);
  servers[3] = {mdns_lan, port};

  // roboRIO-<team>-FRC.frc-field.local
  auto field_local = fmt::format("roboRIO-{}-FRC.frc-field.local", team);
  servers[4] = {field_local, port};

  SetServer(servers);
}

void Dispatcher::SetServerOverride(const char* server_name, unsigned int port) {
  std::string server_name_copy(wpi::trim(server_name));
  SetConnectorOverride([=]() -> std::unique_ptr<wpi::NetworkStream> {
    return wpi::TCPConnector::connect(server_name_copy.c_str(),
                                      static_cast<int>(port), m_logger, 1);
  });
}

void Dispatcher::ClearServerOverride() {
  ClearConnectorOverride();
}

DispatcherBase::DispatcherBase(IStorage& storage, IConnectionNotifier& notifier,
                               wpi::Logger& logger)
    : m_storage(storage), m_notifier(notifier), m_logger(logger) {
  m_active = false;
  m_update_rate = 100;
}

DispatcherBase::~DispatcherBase() {
  Stop();

  {
    std::scoped_lock lock(m_user_mutex);
    for (auto&& datalog : m_dataloggers) {
      m_notifier.Remove(datalog.notifier);
    }
  }
}

unsigned int DispatcherBase::GetNetworkMode() const {
  return m_networkMode;
}

void DispatcherBase::StartLocal() {
  {
    std::scoped_lock lock(m_user_mutex);
    if (m_active) {
      return;
    }
    m_active = true;
  }
  m_networkMode = NT_NET_MODE_LOCAL;
  m_storage.SetDispatcher(this, false);
}

void DispatcherBase::StartServer(
    std::string_view persist_filename,
    std::unique_ptr<wpi::NetworkAcceptor> acceptor) {
  {
    std::scoped_lock lock(m_user_mutex);
    if (m_active) {
      return;
    }
    m_active = true;
  }
  m_networkMode = NT_NET_MODE_SERVER | NT_NET_MODE_STARTING;
  m_persist_filename = persist_filename;
  m_server_acceptor = std::move(acceptor);

  // Load persistent file.  Ignore errors, but pass along warnings.
  if (!persist_filename.empty()) {
    bool first = true;
    m_storage.LoadPersistent(
        persist_filename, [&](size_t line, const char* msg) {
          if (first) {
            first = false;
            WARNING("When reading initial persistent values from '{}':",
                    persist_filename);
          }
          WARNING("{}:{}: {}", persist_filename, line, msg);
        });
  }

  m_storage.SetDispatcher(this, true);

  m_dispatch_thread = std::thread(&Dispatcher::DispatchThreadMain, this);
  m_clientserver_thread = std::thread(&Dispatcher::ServerThreadMain, this);
}

void DispatcherBase::StartClient() {
  {
    std::scoped_lock lock(m_user_mutex);
    if (m_active) {
      return;
    }
    m_active = true;
  }
  m_networkMode = NT_NET_MODE_CLIENT | NT_NET_MODE_STARTING;
  m_storage.SetDispatcher(this, false);

  m_dispatch_thread = std::thread(&Dispatcher::DispatchThreadMain, this);
  m_clientserver_thread = std::thread(&Dispatcher::ClientThreadMain, this);
}

void DispatcherBase::Stop() {
  m_active = false;

  // wake up dispatch thread with a flush
  m_flush_cv.notify_one();

  // wake up client thread with a reconnect
  {
    std::scoped_lock lock(m_user_mutex);
    m_client_connector = nullptr;
  }
  ClientReconnect();

  // wake up server thread by shutting down the socket
  if (m_server_acceptor) {
    m_server_acceptor->shutdown();
  }

  // join threads, with timeout
  if (m_dispatch_thread.joinable()) {
    m_dispatch_thread.join();
  }
  if (m_clientserver_thread.joinable()) {
    m_clientserver_thread.join();
  }

  std::vector<std::shared_ptr<INetworkConnection>> conns;
  {
    std::scoped_lock lock(m_user_mutex);
    conns.swap(m_connections);
  }

  // close all connections
  conns.resize(0);
}

void DispatcherBase::SetUpdateRate(double interval) {
  // don't allow update rates faster than 5 ms or slower than 1 second
  if (interval < 0.005) {
    interval = 0.005;
  } else if (interval > 1.0) {
    interval = 1.0;
  }
  m_update_rate = static_cast<unsigned int>(interval * 1000);
}

void DispatcherBase::SetIdentity(std::string_view name) {
  std::scoped_lock lock(m_user_mutex);
  m_identity = name;
}

void DispatcherBase::Flush() {
  auto now = wpi::Now();
  {
    std::scoped_lock lock(m_flush_mutex);
    // don't allow flushes more often than every 5 ms
    if ((now - m_last_flush) < 5000) {
      return;
    }
    m_last_flush = now;
    m_do_flush = true;
  }
  m_flush_cv.notify_one();
}

std::vector<ConnectionInfo> DispatcherBase::GetConnections() const {
  std::vector<ConnectionInfo> conns;
  if (!m_active) {
    return conns;
  }

  std::scoped_lock lock(m_user_mutex);
  for (auto& conn : m_connections) {
    if (conn->state() != NetworkConnection::kActive) {
      continue;
    }
    conns.emplace_back(conn->info());
  }

  return conns;
}

bool DispatcherBase::IsConnected() const {
  if (!m_active) {
    return false;
  }

  if (m_networkMode == NT_NET_MODE_LOCAL) {
    return true;
  }

  std::scoped_lock lock(m_user_mutex);
  for (auto& conn : m_connections) {
    if (conn->state() == NetworkConnection::kActive) {
      return true;
    }
  }

  return false;
}

unsigned int DispatcherBase::AddListener(
    std::function<void(const ConnectionNotification& event)> callback,
    bool immediate_notify) const {
  std::scoped_lock lock(m_user_mutex);
  unsigned int uid = m_notifier.Add(callback);
  // perform immediate notifications
  if (immediate_notify) {
    for (auto& conn : m_connections) {
      if (conn->state() != NetworkConnection::kActive) {
        continue;
      }
      m_notifier.NotifyConnection(true, conn->info(), uid);
    }
  }
  return uid;
}

unsigned int DispatcherBase::AddPolledListener(unsigned int poller_uid,
                                               bool immediate_notify) const {
  std::scoped_lock lock(m_user_mutex);
  unsigned int uid = m_notifier.AddPolled(poller_uid);
  // perform immediate notifications
  if (immediate_notify) {
    for (auto& conn : m_connections) {
      if (conn->state() != NetworkConnection::kActive) {
        continue;
      }
      m_notifier.NotifyConnection(true, conn->info(), uid);
    }
  }
  return uid;
}

unsigned int DispatcherBase::StartDataLog(wpi::log::DataLog& log,
                                          std::string_view name) {
  std::scoped_lock lock(m_user_mutex);
  auto now = nt::Now();
  unsigned int uid = m_dataloggers.emplace_back(log, name, now);
  m_dataloggers[uid].notifier =
      m_notifier.Add([this, uid](const ConnectionNotification& n) {
        std::scoped_lock lock(m_user_mutex);
        if (uid < m_dataloggers.size() && m_dataloggers[uid].entry) {
          m_dataloggers[uid].entry.Append(ConnInfoToJson(n.connected, n.conn),
                                          nt::Now());
        }
      });
  for (auto& conn : m_connections) {
    if (conn->state() != NetworkConnection::kActive) {
      continue;
    }
    m_dataloggers[uid].entry.Append(ConnInfoToJson(true, conn->info()), now);
  }
  return uid;
}

void DispatcherBase::StopDataLog(unsigned int logger) {
  std::scoped_lock lock(m_user_mutex);
  m_notifier.Remove(m_dataloggers.erase(logger).notifier);
}

void DispatcherBase::SetConnector(Connector connector) {
  std::scoped_lock lock(m_user_mutex);
  m_client_connector = std::move(connector);
}

void DispatcherBase::SetConnectorOverride(Connector connector) {
  std::scoped_lock lock(m_user_mutex);
  m_client_connector_override = std::move(connector);
}

void DispatcherBase::ClearConnectorOverride() {
  std::scoped_lock lock(m_user_mutex);
  m_client_connector_override = nullptr;
}

void DispatcherBase::DispatchThreadMain() {
  auto timeout_time = std::chrono::steady_clock::now();

  static const auto save_delta_time = std::chrono::seconds(1);
  auto next_save_time = timeout_time + save_delta_time;

  int count = 0;

  while (m_active) {
    // handle loop taking too long
    auto start = std::chrono::steady_clock::now();
    if (start > timeout_time) {
      timeout_time = start;
    }

    // wait for periodic or when flushed
    timeout_time += std::chrono::milliseconds(m_update_rate);
    std::unique_lock<wpi::mutex> flush_lock(m_flush_mutex);
    m_flush_cv.wait_until(flush_lock, timeout_time,
                          [&] { return !m_active || m_do_flush; });
    m_do_flush = false;
    flush_lock.unlock();
    if (!m_active) {
      break;  // in case we were woken up to terminate
    }

    // perform periodic persistent save
    if ((m_networkMode & NT_NET_MODE_SERVER) != 0 &&
        !m_persist_filename.empty() && start > next_save_time) {
      next_save_time += save_delta_time;
      // handle loop taking too long
      if (start > next_save_time) {
        next_save_time = start + save_delta_time;
      }
      const char* err = m_storage.SavePersistent(m_persist_filename, true);
      if (err) {
        WARNING("periodic persistent save: {}", err);
      }
    }

    {
      std::scoped_lock user_lock(m_user_mutex);
      bool reconnect = false;

      if (++count > 10) {
        DEBUG0("dispatch running {} connections", m_connections.size());
        count = 0;
      }

      for (auto& conn : m_connections) {
        // post outgoing messages if connection is active
        // only send keep-alives on client
        if (conn->state() == NetworkConnection::kActive) {
          conn->PostOutgoing((m_networkMode & NT_NET_MODE_CLIENT) != 0);
        }

        // if client, reconnect if connection died
        if ((m_networkMode & NT_NET_MODE_CLIENT) != 0 &&
            conn->state() == NetworkConnection::kDead) {
          reconnect = true;
        }
      }
      // reconnect if we disconnected (and a reconnect is not in progress)
      if (reconnect && !m_do_reconnect) {
        m_do_reconnect = true;
        m_reconnect_cv.notify_one();
      }
    }
  }
}

void DispatcherBase::QueueOutgoing(std::shared_ptr<Message> msg,
                                   INetworkConnection* only,
                                   INetworkConnection* except) {
  std::scoped_lock user_lock(m_user_mutex);
  for (auto& conn : m_connections) {
    if (conn.get() == except) {
      continue;
    }
    if (only && conn.get() != only) {
      continue;
    }
    auto state = conn->state();
    if (state != NetworkConnection::kSynchronized &&
        state != NetworkConnection::kActive) {
      continue;
    }
    conn->QueueOutgoing(msg);
  }
}

void DispatcherBase::ServerThreadMain() {
  if (m_server_acceptor->start() != 0) {
    m_active = false;
    m_networkMode = NT_NET_MODE_SERVER | NT_NET_MODE_FAILURE;
    return;
  }
  m_networkMode = NT_NET_MODE_SERVER;
  while (m_active) {
    auto stream = m_server_acceptor->accept();
    if (!stream) {
      m_active = false;
      return;
    }
    if (!m_active) {
      m_networkMode = NT_NET_MODE_NONE;
      return;
    }
    DEBUG0("server: client connection from {} port {}", stream->getPeerIP(),
           stream->getPeerPort());

    // add to connections list
    using namespace std::placeholders;
    auto conn = std::make_shared<NetworkConnection>(
        ++m_connections_uid, std::move(stream), m_notifier, m_logger,
        std::bind(&Dispatcher::ServerHandshake, this, _1, _2, _3),   // NOLINT
        std::bind(&IStorage::GetMessageEntryType, &m_storage, _1));  // NOLINT
    conn->set_process_incoming(
        std::bind(&IStorage::ProcessIncoming, &m_storage, _1, _2,  // NOLINT
                  std::weak_ptr<NetworkConnection>(conn)));
    {
      std::scoped_lock lock(m_user_mutex);
      // reuse dead connection slots
      bool placed = false;
      for (auto& c : m_connections) {
        if (c->state() == NetworkConnection::kDead) {
          c = conn;
          placed = true;
          break;
        }
      }
      if (!placed) {
        m_connections.emplace_back(conn);
      }
      conn->Start();
    }
  }
  m_networkMode = NT_NET_MODE_NONE;
}

void DispatcherBase::ClientThreadMain() {
  while (m_active) {
    // sleep between retries
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    Connector connect;

    // get next server to connect to
    {
      std::scoped_lock lock(m_user_mutex);
      if (m_client_connector_override) {
        connect = m_client_connector_override;
      } else {
        if (!m_client_connector) {
          m_networkMode = NT_NET_MODE_CLIENT | NT_NET_MODE_FAILURE;
          continue;
        }
        connect = m_client_connector;
      }
    }

    // try to connect (with timeout)
    DEBUG0("{}", "client trying to connect");
    auto stream = connect();
    if (!stream) {
      m_networkMode = NT_NET_MODE_CLIENT | NT_NET_MODE_FAILURE;
      continue;  // keep retrying
    }
    DEBUG0("{}", "client connected");
    m_networkMode = NT_NET_MODE_CLIENT;

    std::unique_lock lock(m_user_mutex);
    using namespace std::placeholders;
    auto conn = std::make_shared<NetworkConnection>(
        ++m_connections_uid, std::move(stream), m_notifier, m_logger,
        std::bind(&Dispatcher::ClientHandshake, this, _1, _2, _3),   // NOLINT
        std::bind(&IStorage::GetMessageEntryType, &m_storage, _1));  // NOLINT
    conn->set_process_incoming(
        std::bind(&IStorage::ProcessIncoming, &m_storage, _1, _2,  // NOLINT
                  std::weak_ptr<NetworkConnection>(conn)));
    m_connections.resize(0);  // disconnect any current
    m_connections.emplace_back(conn);
    conn->set_proto_rev(m_reconnect_proto_rev);
    conn->Start();

    // reconnect the next time starting with latest protocol revision
    m_reconnect_proto_rev = 0x0300;

    // block until told to reconnect
    m_do_reconnect = false;
    m_reconnect_cv.wait(lock, [&] { return !m_active || m_do_reconnect; });
  }
  m_networkMode = NT_NET_MODE_NONE;
}

bool DispatcherBase::ClientHandshake(
    NetworkConnection& conn, std::function<std::shared_ptr<Message>()> get_msg,
    std::function<void(wpi::span<std::shared_ptr<Message>>)> send_msgs) {
  // get identity
  std::string self_id;
  {
    std::scoped_lock lock(m_user_mutex);
    self_id = m_identity;
  }

  // send client hello
  DEBUG0("{}", "client: sending hello");
  auto msg = Message::ClientHello(self_id);
  send_msgs(wpi::span(&msg, 1));

  // wait for response
  msg = get_msg();
  if (!msg) {
    // disconnected, retry
    DEBUG0("{}", "client: server disconnected before first response");
    return false;
  }

  if (msg->Is(Message::kProtoUnsup)) {
    if (msg->id() == 0x0200) {
      ClientReconnect(0x0200);
    }
    return false;
  }

  bool new_server = true;
  if (conn.proto_rev() >= 0x0300) {
    // should be server hello; if not, disconnect.
    if (!msg->Is(Message::kServerHello)) {
      return false;
    }
    conn.set_remote_id(msg->str());
    if ((msg->flags() & 1) != 0) {
      new_server = false;
    }
    // get the next message
    msg = get_msg();
  }

  // receive initial assignments
  std::vector<std::shared_ptr<Message>> incoming;
  for (;;) {
    if (!msg) {
      // disconnected, retry
      DEBUG0("{}", "client: server disconnected during initial entries");
      return false;
    }
    DEBUG4("received init str={} id={} seq_num={}", msg->str(), msg->id(),
           msg->seq_num_uid());
    if (msg->Is(Message::kServerHelloDone)) {
      break;
    }
    // shouldn't receive a keep alive, but handle gracefully
    if (msg->Is(Message::kKeepAlive)) {
      msg = get_msg();
      continue;
    }
    if (!msg->Is(Message::kEntryAssign)) {
      // unexpected message
      DEBUG0(
          "client: received message ({}) other than entry assignment during "
          "initial handshake",
          msg->type());
      return false;
    }
    incoming.emplace_back(std::move(msg));
    // get the next message
    msg = get_msg();
  }

  // generate outgoing assignments
  NetworkConnection::Outgoing outgoing;

  m_storage.ApplyInitialAssignments(conn, incoming, new_server, &outgoing);

  if (conn.proto_rev() >= 0x0300) {
    outgoing.emplace_back(Message::ClientHelloDone());
  }

  if (!outgoing.empty()) {
    send_msgs(outgoing);
  }

  INFO("client: CONNECTED to server {} port {}", conn.stream().getPeerIP(),
       conn.stream().getPeerPort());
  return true;
}

bool DispatcherBase::ServerHandshake(
    NetworkConnection& conn, std::function<std::shared_ptr<Message>()> get_msg,
    std::function<void(wpi::span<std::shared_ptr<Message>>)> send_msgs) {
  // Wait for the client to send us a hello.
  auto msg = get_msg();
  if (!msg) {
    DEBUG0("{}", "server: client disconnected before sending hello");
    return false;
  }
  if (!msg->Is(Message::kClientHello)) {
    DEBUG0("{}", "server: client initial message was not client hello");
    return false;
  }

  // Check that the client requested version is not too high.
  unsigned int proto_rev = msg->id();
  if (proto_rev > 0x0300) {
    DEBUG0("{}", "server: client requested proto > 0x0300");
    auto toSend = Message::ProtoUnsup();
    send_msgs(wpi::span(&toSend, 1));
    return false;
  }

  if (proto_rev >= 0x0300) {
    conn.set_remote_id(msg->str());
  }

  // Set the proto version to the client requested version
  DEBUG0("server: client protocol {}", proto_rev);
  conn.set_proto_rev(proto_rev);

  // Send initial set of assignments
  NetworkConnection::Outgoing outgoing;

  // Start with server hello.  TODO: initial connection flag
  if (proto_rev >= 0x0300) {
    std::scoped_lock lock(m_user_mutex);
    outgoing.emplace_back(Message::ServerHello(0u, m_identity));
  }

  // Get snapshot of initial assignments
  m_storage.GetInitialAssignments(conn, &outgoing);

  // Finish with server hello done
  outgoing.emplace_back(Message::ServerHelloDone());

  // Batch transmit
  DEBUG0("{}", "server: sending initial assignments");
  send_msgs(outgoing);

  // In proto rev 3.0 and later, the handshake concludes with a client hello
  // done message, so we can batch the assigns before marking the connection
  // active.  In pre-3.0, we need to just immediately mark it active and hand
  // off control to the dispatcher to assign them as they arrive.
  if (proto_rev >= 0x0300) {
    // receive client initial assignments
    std::vector<std::shared_ptr<Message>> incoming;
    msg = get_msg();
    for (;;) {
      if (!msg) {
        // disconnected, retry
        DEBUG0("{}", "server: disconnected waiting for initial entries");
        return false;
      }
      if (msg->Is(Message::kClientHelloDone)) {
        break;
      }
      // shouldn't receive a keep alive, but handle gracefully
      if (msg->Is(Message::kKeepAlive)) {
        msg = get_msg();
        continue;
      }
      if (!msg->Is(Message::kEntryAssign)) {
        // unexpected message
        DEBUG0(
            "server: received message ({}) other than entry assignment during "
            "initial handshake",
            msg->type());
        return false;
      }
      incoming.push_back(msg);
      // get the next message (blocks)
      msg = get_msg();
    }
    for (auto& msg : incoming) {
      m_storage.ProcessIncoming(msg, &conn, std::weak_ptr<NetworkConnection>());
    }
  }

  INFO("server: client CONNECTED: {} port {}", conn.stream().getPeerIP(),
       conn.stream().getPeerPort());
  return true;
}

void DispatcherBase::ClientReconnect(unsigned int proto_rev) {
  if ((m_networkMode & NT_NET_MODE_SERVER) != 0) {
    return;
  }
  {
    std::scoped_lock lock(m_user_mutex);
    m_reconnect_proto_rev = proto_rev;
    m_do_reconnect = true;
  }
  m_reconnect_cv.notify_one();
}
