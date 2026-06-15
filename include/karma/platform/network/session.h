#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "karma/platform/network/protocol.h"
#include "karma/platform/network/transport.h"

namespace karma::net {

/// \ingroup karma_platform
/// Session-level peer state.
enum class SessionPeerState {
  TransportConnected,
  Connected
};

/// \ingroup karma_platform
/// Peer metadata tracked by `ServerSession`.
struct SessionPeer {
  PeerId id{};
  Endpoint endpoint{};
  SessionPeerState state = SessionPeerState::TransportConnected;
  uint32_t last_received_sequence = 0;
  uint32_t last_sent_sequence = 0;
  std::unordered_set<std::string> groups;
};

/// \ingroup karma_platform
/// Session event type emitted after transport polling and packet dispatch.
enum class SessionEventType {
  PeerConnected,
  PeerDisconnected,
  ProtocolError,
  CustomMessage,
  InputCommand,
  ReplicationMessage,
  Pong
};

/// \ingroup karma_platform
/// Session-level event.
struct SessionEvent {
  SessionEventType type = SessionEventType::CustomMessage;
  PeerId peer{};
  MessageType message_type = MessageType::CustomReliable;
  ChannelId channel = 0;
  DisconnectReason disconnect_reason = DisconnectReason::None;
  DecodeStatus decode_status = DecodeStatus::Ok;
  Endpoint endpoint{};
  uint32_t tick = 0;
  uint32_t sequence = 0;
  bool stale_sequence = false;
  std::vector<std::byte> payload;
};

/// \ingroup karma_platform
/// Aggregate result for sends that target several recipients.
struct MultiSendResult {
  std::size_t attempted = 0;
  std::size_t sent = 0;
  SendStatus first_error = SendStatus::Ok;

  bool ok() const { return attempted == sent; }
};

/// \ingroup karma_platform
/// Predicate used for explicit per-recipient session targeting.
using RecipientPredicate = std::function<bool(const SessionPeer&)>;

/// \ingroup karma_platform
/// Authoritative server session above a transport.
class ServerSession {
 public:
  explicit ServerSession(std::unique_ptr<IServerTransport> transport,
                         uint32_t app_id);

  IServerTransport* transport() { return transport_.get(); }
  const IServerTransport* transport() const { return transport_.get(); }
  uint32_t appId() const { return app_id_; }

  void poll(std::vector<SessionEvent>& out_events);
  void disconnect(PeerId peer, DisconnectReason reason = DisconnectReason::Local);
  void flush();

  SendResult sendTo(PeerId peer,
                    MessageType type,
                    std::span<const std::byte> payload,
                    Delivery delivery,
                    ChannelId channel,
                    uint32_t tick = 0);
  MultiSendResult broadcast(MessageType type,
                            std::span<const std::byte> payload,
                            Delivery delivery,
                            ChannelId channel,
                            uint32_t tick = 0);
  MultiSendResult sendWhere(const RecipientPredicate& predicate,
                            MessageType type,
                            std::span<const std::byte> payload,
                            Delivery delivery,
                            ChannelId channel,
                            uint32_t tick = 0);

  SendResult sendCustomTo(PeerId peer,
                          std::span<const std::byte> payload,
                          Delivery delivery,
                          ChannelId channel,
                          uint32_t tick = 0);
  MultiSendResult broadcastCustom(std::span<const std::byte> payload,
                                  Delivery delivery,
                                  ChannelId channel,
                                  uint32_t tick = 0);
  MultiSendResult sendCustomWhere(const RecipientPredicate& predicate,
                                  std::span<const std::byte> payload,
                                  Delivery delivery,
                                  ChannelId channel,
                                  uint32_t tick = 0);

  void addPeerToGroup(PeerId peer, const std::string& group);
  void removePeerFromGroup(PeerId peer, const std::string& group);
  bool peerInGroup(PeerId peer, const std::string& group) const;

  const SessionPeer* peer(PeerId peer) const;
  std::vector<PeerId> peers() const;

 private:
  SessionPeer* mutablePeer(PeerId peer);
  void handleTransportEvent(const TransportEvent& event,
                            std::vector<SessionEvent>& out_events);
  void handlePacket(SessionPeer& peer,
                    const TransportEvent& event,
                    Packet packet,
                    std::vector<SessionEvent>& out_events);
  SendResult sendPacket(SessionPeer& peer,
                        MessageType type,
                        std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick);
  void emitProtocolError(PeerId peer,
                         Endpoint endpoint,
                         DecodeStatus status,
                         std::vector<SessionEvent>& out_events);

  std::unique_ptr<IServerTransport> transport_;
  uint32_t app_id_ = 0;
  std::vector<TransportEvent> transport_events_;
  std::unordered_map<uint32_t, SessionPeer> peers_;
};

/// \ingroup karma_platform
/// Client session above a transport.
class ClientSession {
 public:
  explicit ClientSession(std::unique_ptr<IClientTransport> transport,
                         uint32_t app_id,
                         std::string client_name = {});

  IClientTransport* transport() { return transport_.get(); }
  const IClientTransport* transport() const { return transport_.get(); }
  uint32_t appId() const { return app_id_; }

  ConnectResult connect(const std::string& host, uint16_t port, int timeout_ms);
  void poll(std::vector<SessionEvent>& out_events);
  void disconnect(DisconnectReason reason = DisconnectReason::Local);
  void flush();

  bool isTransportConnected() const;
  bool isConnected() const { return connected_; }
  PeerId serverPeer() const;
  std::optional<Endpoint> remoteEndpoint() const;
  uint32_t lastReceivedSequence() const { return last_received_sequence_; }

  SendResult send(MessageType type,
                  std::span<const std::byte> payload,
                  Delivery delivery,
                  ChannelId channel,
                  uint32_t tick = 0);
  SendResult sendCustom(std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick = 0);
  SendResult sendInputCommand(std::span<const std::byte> payload,
                              uint32_t tick,
                              ChannelId channel = 1);

 private:
  void sendHandshake(uint32_t tick = 0);
  void handleTransportEvent(const TransportEvent& event,
                            std::vector<SessionEvent>& out_events);
  void handlePacket(const TransportEvent& event,
                    Packet packet,
                    std::vector<SessionEvent>& out_events);
  SendResult sendPacket(MessageType type,
                        std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick);
  void emitProtocolError(PeerId peer,
                         Endpoint endpoint,
                         DecodeStatus status,
                         std::vector<SessionEvent>& out_events);

  std::unique_ptr<IClientTransport> transport_;
  uint32_t app_id_ = 0;
  std::string client_name_;
  bool connected_ = false;
  uint32_t last_received_sequence_ = 0;
  uint32_t last_sent_sequence_ = 0;
  std::vector<TransportEvent> transport_events_;
};

}  // namespace karma::net
