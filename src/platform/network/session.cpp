#include "karma/network.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace karma::network {
namespace {

MessageType customMessageType(Delivery delivery) {
  return delivery == Delivery::Reliable ? MessageType::CustomReliable
                                        : MessageType::CustomUnreliable;
}

bool isReplicationMessage(MessageType type) {
  switch (type) {
    case MessageType::EntitySpawn:
    case MessageType::EntityDespawn:
    case MessageType::ComponentSnapshot:
    case MessageType::ComponentDelta:
    case MessageType::AuthorityTransfer:
      return true;
    default:
      return false;
  }
}

uint16_t packetFlagsFor(Delivery delivery) {
  return delivery == Delivery::Reliable ? PacketFlagReliable : PacketFlagNone;
}

SessionEvent makePayloadEvent(SessionEventType event_type,
                              PeerId peer,
                              const TransportEvent& transport_event,
                              const Packet& packet,
                              bool stale_sequence) {
  return SessionEvent{
      .type = event_type,
      .peer = peer,
      .message_type = packet.header.message_type,
      .channel = transport_event.channel,
      .endpoint = transport_event.endpoint,
      .tick = packet.header.tick,
      .sequence = packet.header.sequence,
      .stale_sequence = stale_sequence,
      .payload = packet.payload,
  };
}

MultiSendResult accumulate(MultiSendResult result, SendResult send_result) {
  result.attempted += 1;
  if (send_result.ok()) {
    result.sent += 1;
  } else if (result.first_error == SendStatus::Ok) {
    result.first_error = send_result.status;
  }
  return result;
}

bool sequenceIsStale(uint32_t candidate, uint32_t latest) {
  return candidate != 0u && latest != 0u &&
         !isPacketSequenceNewer(candidate, latest);
}

}  // namespace

ServerSession::ServerSession(std::unique_ptr<IServerTransport> transport,
                             uint32_t app_id)
    : transport_(std::move(transport)), app_id_(app_id) {}

void ServerSession::poll(std::vector<SessionEvent>& out_events) {
  if (!transport_) {
    return;
  }
  transport_events_.clear();
  transport_->poll(transport_events_);
  for (const TransportEvent& event : transport_events_) {
    handleTransportEvent(event, out_events);
  }
}

void ServerSession::disconnect(PeerId peer, DisconnectReason reason) {
  if (transport_) {
    transport_->disconnect(peer, reason);
  }
  peers_.erase(peer.value);
}

void ServerSession::flush() {
  if (transport_) {
    transport_->flush();
  }
}

SendResult ServerSession::sendTo(PeerId peer,
                                 MessageType type,
                                 std::span<const std::byte> payload,
                                 Delivery delivery,
                                 ChannelId channel,
                                 uint32_t tick) {
  auto* info = mutablePeer(peer);
  if (!info || info->state != SessionPeerState::Connected) {
    return {.status = SendStatus::UnknownPeer};
  }
  return sendPacket(*info, type, payload, delivery, channel, tick);
}

MultiSendResult ServerSession::broadcast(MessageType type,
                                         std::span<const std::byte> payload,
                                         Delivery delivery,
                                         ChannelId channel,
                                         uint32_t tick) {
  return sendWhere(
      [](const SessionPeer& peer) {
        return peer.state == SessionPeerState::Connected;
      },
      type,
      payload,
      delivery,
      channel,
      tick);
}

MultiSendResult ServerSession::sendWhere(const RecipientPredicate& predicate,
                                         MessageType type,
                                         std::span<const std::byte> payload,
                                         Delivery delivery,
                                         ChannelId channel,
                                         uint32_t tick) {
  MultiSendResult result{};
  if (!predicate) {
    return result;
  }
  for (auto& [id, peer] : peers_) {
    (void)id;
    if (peer.state != SessionPeerState::Connected || !predicate(peer)) {
      continue;
    }
    result = accumulate(result, sendPacket(peer, type, payload, delivery, channel, tick));
  }
  return result;
}

SendResult ServerSession::sendCustomTo(PeerId peer,
                                       std::span<const std::byte> payload,
                                       Delivery delivery,
                                       ChannelId channel,
                                       uint32_t tick) {
  return sendTo(peer, customMessageType(delivery), payload, delivery, channel, tick);
}

MultiSendResult ServerSession::broadcastCustom(std::span<const std::byte> payload,
                                               Delivery delivery,
                                               ChannelId channel,
                                               uint32_t tick) {
  return broadcast(customMessageType(delivery), payload, delivery, channel, tick);
}

MultiSendResult ServerSession::sendCustomWhere(const RecipientPredicate& predicate,
                                               std::span<const std::byte> payload,
                                               Delivery delivery,
                                               ChannelId channel,
                                               uint32_t tick) {
  return sendWhere(predicate, customMessageType(delivery), payload, delivery, channel, tick);
}

void ServerSession::addPeerToGroup(PeerId peer, const std::string& group) {
  if (auto* info = mutablePeer(peer)) {
    info->groups.insert(group);
  }
}

void ServerSession::removePeerFromGroup(PeerId peer, const std::string& group) {
  if (auto* info = mutablePeer(peer)) {
    info->groups.erase(group);
  }
}

bool ServerSession::peerInGroup(PeerId peer, const std::string& group) const {
  const auto* info = this->peer(peer);
  return info && info->groups.find(group) != info->groups.end();
}

const SessionPeer* ServerSession::peer(PeerId peer) const {
  auto it = peers_.find(peer.value);
  if (it == peers_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<PeerId> ServerSession::peers() const {
  std::vector<PeerId> ids;
  ids.reserve(peers_.size());
  for (const auto& [id, peer] : peers_) {
    if (peer.state == SessionPeerState::Connected) {
      ids.push_back(PeerId{id});
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

SessionPeer* ServerSession::mutablePeer(PeerId peer) {
  auto it = peers_.find(peer.value);
  if (it == peers_.end()) {
    return nullptr;
  }
  return &it->second;
}

void ServerSession::handleTransportEvent(const TransportEvent& event,
                                         std::vector<SessionEvent>& out_events) {
  switch (event.type) {
    case TransportEvent::Type::Connect:
      peers_[event.peer.value] = SessionPeer{
          .id = event.peer,
          .endpoint = event.endpoint,
          .state = SessionPeerState::TransportConnected,
      };
      break;
    case TransportEvent::Type::Disconnect: {
      out_events.push_back(SessionEvent{
          .type = SessionEventType::PeerDisconnected,
          .peer = event.peer,
          .disconnect_reason = event.disconnect_reason,
          .endpoint = event.endpoint,
      });
      peers_.erase(event.peer.value);
      break;
    }
    case TransportEvent::Type::Receive: {
      auto* peer = mutablePeer(event.peer);
      if (!peer) {
        return;
      }
      DecodeResult decoded = decodePacket(event.payload, app_id_);
      if (!decoded.ok()) {
        emitProtocolError(event.peer, event.endpoint, decoded.status, out_events);
        disconnect(event.peer, DisconnectReason::ProtocolError);
        return;
      }
      handlePacket(*peer, event, std::move(decoded.packet), out_events);
      break;
    }
  }
}

void ServerSession::handlePacket(SessionPeer& peer,
                                 const TransportEvent& event,
                                 Packet packet,
                                 std::vector<SessionEvent>& out_events) {
  const bool stale_sequence =
      sequenceIsStale(packet.header.sequence, peer.last_received_sequence);
  if (packet.header.sequence != 0u &&
      (peer.last_received_sequence == 0u ||
       isPacketSequenceNewer(packet.header.sequence, peer.last_received_sequence))) {
    peer.last_received_sequence = packet.header.sequence;
  }

  if (packet.header.message_type == MessageType::HandshakeRequest) {
    if (stale_sequence) {
      return;
    }
    if (peer.state != SessionPeerState::TransportConnected) {
      emitProtocolError(peer.id, peer.endpoint, DecodeStatus::UnexpectedMessage, out_events);
      disconnect(peer.id, DisconnectReason::ProtocolError);
      return;
    }
    BinaryReader request_reader(packet.payload);
    std::string client_name;
    if (!request_reader.readString(client_name, kMaxPeerNameLength) ||
        !request_reader.exhausted()) {
      emitProtocolError(peer.id, peer.endpoint, DecodeStatus::MalformedPayload, out_events);
      disconnect(peer.id, DisconnectReason::ProtocolError);
      return;
    }
    peer.name = std::move(client_name);
    BinaryWriter payload;
    payload.writeUInt32(peer.id.value);
    payload.writeUInt32(packet.header.tick);
    const SendResult handshake_send =
        sendPacket(peer,
                   MessageType::HandshakeAccept,
                   payload.bytes(),
                   Delivery::Reliable,
                   0,
                   packet.header.tick);
    if (!handshake_send.ok()) {
      disconnect(peer.id, DisconnectReason::TransportError);
      return;
    }
    peer.state = SessionPeerState::Connected;
    out_events.push_back(SessionEvent{
        .type = SessionEventType::PeerConnected,
        .peer = peer.id,
        .message_type = MessageType::HandshakeRequest,
        .channel = event.channel,
        .endpoint = peer.endpoint,
        .tick = packet.header.tick,
        .sequence = packet.header.sequence,
        .stale_sequence = stale_sequence,
        .payload = std::move(packet.payload),
    });
    return;
  }

  if (peer.state != SessionPeerState::Connected) {
    emitProtocolError(peer.id, peer.endpoint, DecodeStatus::UnexpectedMessage, out_events);
    disconnect(peer.id, DisconnectReason::ProtocolError);
    return;
  }

  switch (packet.header.message_type) {
    case MessageType::Disconnect:
      if (stale_sequence) {
        break;
      }
      out_events.push_back(SessionEvent{
          .type = SessionEventType::PeerDisconnected,
          .peer = peer.id,
          .message_type = MessageType::Disconnect,
          .channel = event.channel,
          .disconnect_reason = DisconnectReason::Remote,
          .endpoint = peer.endpoint,
          .tick = packet.header.tick,
          .sequence = packet.header.sequence,
          .stale_sequence = stale_sequence,
          .payload = std::move(packet.payload),
      });
      disconnect(peer.id, DisconnectReason::Remote);
      break;
    case MessageType::Ping:
      if (stale_sequence) {
        break;
      }
      sendPacket(peer,
                 MessageType::Pong,
                 packet.payload,
                 Delivery::Unreliable,
                 event.channel,
                 packet.header.tick);
      break;
    case MessageType::Pong:
      out_events.push_back(makePayloadEvent(SessionEventType::Pong,
                                            peer.id,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    case MessageType::CustomReliable:
    case MessageType::CustomUnreliable:
      out_events.push_back(makePayloadEvent(SessionEventType::CustomMessage,
                                            peer.id,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    case MessageType::InputCommand:
      if (stale_sequence) {
        break;
      }
      out_events.push_back(makePayloadEvent(SessionEventType::InputCommand,
                                            peer.id,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    default:
      if (isReplicationMessage(packet.header.message_type)) {
        out_events.push_back(makePayloadEvent(SessionEventType::ReplicationMessage,
                                              peer.id,
                                              event,
                                              packet,
                                              stale_sequence));
      } else {
        emitProtocolError(peer.id,
                          peer.endpoint,
                          DecodeStatus::UnexpectedMessage,
                          out_events);
        disconnect(peer.id, DisconnectReason::ProtocolError);
      }
      break;
  }
}

SendResult ServerSession::sendPacket(SessionPeer& peer,
                                     MessageType type,
                                     std::span<const std::byte> payload,
                                     Delivery delivery,
                                     ChannelId channel,
                                     uint32_t tick) {
  if (!transport_) {
    return {.status = SendStatus::NotConnected};
  }
  if (payload.size() > kMaxPacketPayloadSize) {
    return {.status = SendStatus::InvalidPayload};
  }
  PacketHeader header{};
  header.app_id = app_id_;
  header.message_type = type;
  header.flags = packetFlagsFor(delivery);
  header.tick = tick;
  peer.last_sent_sequence = nextPacketSequence(peer.last_sent_sequence);
  header.sequence = peer.last_sent_sequence;
  std::vector<std::byte> bytes = encodePacket(header, payload);
  return transport_->send(peer.id, channel, bytes.data(), bytes.size(), delivery, false);
}

void ServerSession::emitProtocolError(PeerId peer,
                                      Endpoint endpoint,
                                      DecodeStatus status,
                                      std::vector<SessionEvent>& out_events) {
  out_events.push_back(SessionEvent{
      .type = SessionEventType::ProtocolError,
      .peer = peer,
      .disconnect_reason = DisconnectReason::ProtocolError,
      .decode_status = status,
      .endpoint = std::move(endpoint),
  });
}

ClientSession::ClientSession(std::unique_ptr<IClientTransport> transport,
                             uint32_t app_id,
                             std::string client_name)
    : transport_(std::move(transport)),
      app_id_(app_id),
      client_name_(std::move(client_name)) {
  if (client_name_.size() > kMaxPeerNameLength) {
    throw std::invalid_argument("Client session name exceeds kMaxPeerNameLength.");
  }
}

ConnectResult ClientSession::connect(const std::string& host,
                                     uint16_t port,
                                     int timeout_ms) {
  connected_ = false;
  last_received_sequence_ = 0;
  last_sent_sequence_ = 0;
  if (!transport_) {
    return {.status = ConnectStatus::BackendError};
  }
  ConnectResult result = transport_->connect(host, port, timeout_ms);
  if (result.connected()) {
    const SendResult handshake = sendHandshake();
    if (!handshake.ok()) {
      transport_->disconnect(DisconnectReason::TransportError);
      return {.status = ConnectStatus::BackendError};
    }
    transport_->flush();
  }
  return result;
}

void ClientSession::poll(std::vector<SessionEvent>& out_events) {
  if (!transport_) {
    return;
  }
  transport_events_.clear();
  transport_->poll(transport_events_);
  for (const TransportEvent& event : transport_events_) {
    handleTransportEvent(event, out_events);
  }
}

void ClientSession::disconnect(DisconnectReason reason) {
  if (connected_) {
    BinaryWriter payload;
    payload.writeUInt8(static_cast<uint8_t>(reason));
    sendPacket(MessageType::Disconnect, payload.bytes(), Delivery::Reliable, 0, 0);
  }
  connected_ = false;
  if (transport_) {
    transport_->disconnect(reason);
  }
}

void ClientSession::flush() {
  if (transport_) {
    transport_->flush();
  }
}

bool ClientSession::isTransportConnected() const {
  return transport_ && transport_->isConnected();
}

PeerId ClientSession::serverPeer() const {
  return transport_ ? transport_->serverPeer() : PeerId{};
}

std::optional<Endpoint> ClientSession::remoteEndpoint() const {
  return transport_ ? transport_->remoteEndpoint() : std::nullopt;
}

SendResult ClientSession::send(MessageType type,
                               std::span<const std::byte> payload,
                               Delivery delivery,
                               ChannelId channel,
                               uint32_t tick) {
  if (!connected_) {
    return {.status = SendStatus::NotConnected};
  }
  return sendPacket(type, payload, delivery, channel, tick);
}

SendResult ClientSession::sendCustom(std::span<const std::byte> payload,
                                     Delivery delivery,
                                     ChannelId channel,
                                     uint32_t tick) {
  return send(customMessageType(delivery), payload, delivery, channel, tick);
}

SendResult ClientSession::sendInputCommand(std::span<const std::byte> payload,
                                           uint32_t tick,
                                           ChannelId channel) {
  return send(MessageType::InputCommand, payload, Delivery::Unreliable, channel, tick);
}

SendResult ClientSession::sendHandshake(uint32_t tick) {
  BinaryWriter payload;
  payload.writeString(client_name_);
  return sendPacket(MessageType::HandshakeRequest,
                    payload.bytes(),
                    Delivery::Reliable,
                    0,
                    tick);
}

void ClientSession::handleTransportEvent(const TransportEvent& event,
                                         std::vector<SessionEvent>& out_events) {
  switch (event.type) {
    case TransportEvent::Type::Connect:
      break;
    case TransportEvent::Type::Disconnect:
      connected_ = false;
      out_events.push_back(SessionEvent{
          .type = SessionEventType::PeerDisconnected,
          .peer = event.peer,
          .disconnect_reason = event.disconnect_reason,
          .endpoint = event.endpoint,
      });
      break;
    case TransportEvent::Type::Receive: {
      DecodeResult decoded = decodePacket(event.payload, app_id_);
      if (!decoded.ok()) {
        emitProtocolError(event.peer, event.endpoint, decoded.status, out_events);
        disconnect(DisconnectReason::ProtocolError);
        return;
      }
      handlePacket(event, std::move(decoded.packet), out_events);
      break;
    }
  }
}

void ClientSession::handlePacket(const TransportEvent& event,
                                 Packet packet,
                                 std::vector<SessionEvent>& out_events) {
  const bool stale_sequence =
      sequenceIsStale(packet.header.sequence, last_received_sequence_);
  if (packet.header.sequence != 0u &&
      (last_received_sequence_ == 0u ||
       isPacketSequenceNewer(packet.header.sequence, last_received_sequence_))) {
    last_received_sequence_ = packet.header.sequence;
  }

  if (!connected_ && packet.header.message_type != MessageType::HandshakeAccept) {
    emitProtocolError(event.peer,
                      event.endpoint,
                      DecodeStatus::UnexpectedMessage,
                      out_events);
    disconnect(DisconnectReason::ProtocolError);
    return;
  }

  switch (packet.header.message_type) {
    case MessageType::HandshakeAccept: {
      if (stale_sequence) {
        break;
      }
      if (connected_) {
        emitProtocolError(event.peer,
                          event.endpoint,
                          DecodeStatus::UnexpectedMessage,
                          out_events);
        disconnect(DisconnectReason::ProtocolError);
        return;
      }
      BinaryReader reader(packet.payload);
      uint32_t peer_id = 0;
      uint32_t server_tick = 0;
      if (!reader.readUInt32(peer_id) || !reader.readUInt32(server_tick) ||
          !reader.exhausted() || peer_id == 0u) {
        emitProtocolError(event.peer, event.endpoint, DecodeStatus::MalformedPayload, out_events);
        disconnect(DisconnectReason::ProtocolError);
        return;
      }
      connected_ = true;
      out_events.push_back(SessionEvent{
          .type = SessionEventType::PeerConnected,
          .peer = PeerId{peer_id},
          .message_type = MessageType::HandshakeAccept,
          .channel = event.channel,
          .endpoint = event.endpoint,
          .tick = server_tick,
          .sequence = packet.header.sequence,
          .stale_sequence = stale_sequence,
          .payload = std::move(packet.payload),
      });
      break;
    }
    case MessageType::Disconnect:
      if (stale_sequence) {
        break;
      }
      connected_ = false;
      out_events.push_back(SessionEvent{
          .type = SessionEventType::PeerDisconnected,
          .peer = event.peer,
          .message_type = MessageType::Disconnect,
          .channel = event.channel,
          .disconnect_reason = DisconnectReason::Remote,
          .endpoint = event.endpoint,
          .tick = packet.header.tick,
          .sequence = packet.header.sequence,
          .stale_sequence = stale_sequence,
          .payload = std::move(packet.payload),
      });
      break;
    case MessageType::Ping:
      if (stale_sequence) {
        break;
      }
      sendPacket(MessageType::Pong,
                 packet.payload,
                 Delivery::Unreliable,
                 event.channel,
                 packet.header.tick);
      break;
    case MessageType::Pong:
      out_events.push_back(makePayloadEvent(SessionEventType::Pong,
                                            event.peer,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    case MessageType::CustomReliable:
    case MessageType::CustomUnreliable:
      out_events.push_back(makePayloadEvent(SessionEventType::CustomMessage,
                                            event.peer,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    case MessageType::InputCommand:
      if (stale_sequence) {
        break;
      }
      out_events.push_back(makePayloadEvent(SessionEventType::InputCommand,
                                            event.peer,
                                            event,
                                            packet,
                                            stale_sequence));
      break;
    default:
      if (isReplicationMessage(packet.header.message_type)) {
        out_events.push_back(makePayloadEvent(SessionEventType::ReplicationMessage,
                                              event.peer,
                                              event,
                                              packet,
                                              stale_sequence));
      } else {
        emitProtocolError(event.peer,
                          event.endpoint,
                          DecodeStatus::UnexpectedMessage,
                          out_events);
        disconnect(DisconnectReason::ProtocolError);
      }
      break;
  }
}

SendResult ClientSession::sendPacket(MessageType type,
                                     std::span<const std::byte> payload,
                                     Delivery delivery,
                                     ChannelId channel,
                                     uint32_t tick) {
  if (!transport_ || !transport_->isConnected()) {
    return {.status = SendStatus::NotConnected};
  }
  if (payload.size() > kMaxPacketPayloadSize) {
    return {.status = SendStatus::InvalidPayload};
  }
  PacketHeader header{};
  header.app_id = app_id_;
  header.message_type = type;
  header.flags = packetFlagsFor(delivery);
  header.tick = tick;
  last_sent_sequence_ = nextPacketSequence(last_sent_sequence_);
  header.sequence = last_sent_sequence_;
  std::vector<std::byte> bytes = encodePacket(header, payload);
  return transport_->send(channel, bytes.data(), bytes.size(), delivery, false);
}

void ClientSession::emitProtocolError(PeerId peer,
                                      Endpoint endpoint,
                                      DecodeStatus status,
                                      std::vector<SessionEvent>& out_events) {
  out_events.push_back(SessionEvent{
      .type = SessionEventType::ProtocolError,
      .peer = peer,
      .disconnect_reason = DisconnectReason::ProtocolError,
      .decode_status = status,
      .endpoint = std::move(endpoint),
  });
}

}  // namespace karma::network
