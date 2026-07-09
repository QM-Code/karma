#pragma once

#include "karma/app.h"
#include "karma/components.h"
#include "karma/platform.h"
#include "karma/world.h"



#include <cstddef>
#include <cstdint>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace karma::network {

/// \ingroup karma_platform
/// Wire protocol version for Karma networking v1 packets.
inline constexpr uint16_t kProtocolVersion = 1;
/// \ingroup karma_platform
/// Fixed packet header size in bytes.
inline constexpr uint16_t kPacketHeaderSize = 28;
/// Maximum application payload accepted by the built-in session protocol.
inline constexpr std::size_t kMaxPacketPayloadSize = 1024u * 1024u;
/// Maximum UTF-8 byte length accepted for a session peer name.
inline constexpr std::size_t kMaxPeerNameLength = 64u;
/// \ingroup karma_platform
/// Packet magic for quick malformed datagram rejection.
inline constexpr uint32_t kPacketMagic = 0x4B4E4554u;  // KNET

/// \ingroup karma_platform
/// Built-in network message types understood by the v1 session layer.
enum class MessageType : uint16_t {
  HandshakeRequest = 1,
  HandshakeAccept = 2,
  Disconnect = 3,
  Ping = 4,
  Pong = 5,
  CustomReliable = 6,
  CustomUnreliable = 7,
  EntitySpawn = 8,
  EntityDespawn = 9,
  ComponentSnapshot = 10,
  ComponentDelta = 11,
  AuthorityTransfer = 12,
  InputCommand = 13
};

/// \ingroup karma_platform
/// Packet flags stored in `PacketHeader::flags`.
enum PacketFlag : uint16_t {
  PacketFlagNone = 0,
  PacketFlagReliable = 1u << 0u,
  PacketFlagFragment = 1u << 1u,
  PacketFlagAck = 1u << 2u
};

/// \ingroup karma_platform
/// Versioned binary packet header.
struct PacketHeader {
  uint32_t magic = kPacketMagic;
  uint16_t version = kProtocolVersion;
  uint16_t header_size = kPacketHeaderSize;
  uint32_t app_id = 0;
  MessageType message_type = MessageType::CustomReliable;
  uint16_t flags = PacketFlagNone;
  uint32_t tick = 0;
  uint32_t sequence = 0;
  uint32_t payload_length = 0;
};

/// Returns true when `candidate` is newer than `reference` in the wrapping
/// 32-bit packet sequence space. Comparisons exactly half a range apart are
/// intentionally treated as unordered.
constexpr bool isPacketSequenceNewer(uint32_t candidate, uint32_t reference) {
  const uint32_t distance = candidate - reference;
  return distance != 0u && distance < 0x80000000u;
}

/// Advances a packet sequence while reserving zero for unsequenced packets.
constexpr uint32_t nextPacketSequence(uint32_t current) {
  return current == std::numeric_limits<uint32_t>::max() ? 1u : current + 1u;
}

/// \ingroup karma_platform
/// Decoded protocol packet.
struct Packet {
  PacketHeader header{};
  std::vector<std::byte> payload;
};

/// \ingroup karma_platform
/// Packet decode status.
enum class DecodeStatus {
  Ok,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  HeaderSizeMismatch,
  AppIdMismatch,
  PayloadLengthMismatch,
  PayloadTooLarge,
  MalformedPayload,
  UnexpectedMessage
};

/// \ingroup karma_platform
/// Result for packet decode attempts.
struct DecodeResult {
  DecodeStatus status = DecodeStatus::TooSmall;
  Packet packet{};

  bool ok() const { return status == DecodeStatus::Ok; }
};

/// \ingroup karma_platform
/// Binary writer used by protocol and component replication payloads.
class BinaryWriter {
 public:
  void writeUInt8(uint8_t value);
  void writeUInt16(uint16_t value);
  void writeUInt32(uint32_t value);
  void writeUInt64(uint64_t value);
  void writeFloat32(float value);
  void writeBytes(std::span<const std::byte> bytes);
  void writeString(std::string_view value);

  const std::vector<std::byte>& bytes() const { return bytes_; }
  std::vector<std::byte> takeBytes() { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

/// \ingroup karma_platform
/// Bounds-checked binary reader used by protocol and replication payloads.
class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  bool readUInt8(uint8_t& value);
  bool readUInt16(uint16_t& value);
  bool readUInt32(uint32_t& value);
  bool readUInt64(uint64_t& value);
  bool readFloat32(float& value);
  bool readBytes(uint32_t size, std::vector<std::byte>& value);
  bool readString(
      std::string& value,
      std::size_t max_size = std::numeric_limits<std::size_t>::max());

  std::span<const std::byte> remainingBytes() const;
  std::size_t remaining() const { return bytes_.size() - offset_; }
  bool exhausted() const { return offset_ == bytes_.size(); }

 private:
  bool readRaw(std::byte* out, std::size_t size);

  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

/// \ingroup karma_platform
/// Encodes `header` and `payload` into one wire packet. Returns an empty vector
/// when the payload exceeds `kMaxPacketPayloadSize`.
std::vector<std::byte> encodePacket(PacketHeader header,
                                    std::span<const std::byte> payload);

/// \ingroup karma_platform
/// Decodes a wire packet. Set `expected_app_id` to `0` to skip app id matching.
DecodeResult decodePacket(std::span<const std::byte> bytes,
                          uint32_t expected_app_id = 0,
                          uint16_t expected_version = kProtocolVersion);

}  // namespace karma::network


#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace karma::network {

/// \ingroup karma_platform
/// Stable transport peer handle exposed by client/server transports.
struct PeerId {
  uint32_t value = 0;

  static constexpr uint32_t kInvalidValue = 0;

  constexpr bool isValid() const { return value != kInvalidValue; }

  friend constexpr bool operator==(PeerId a, PeerId b) {
    return a.value == b.value;
  }

  friend constexpr bool operator!=(PeerId a, PeerId b) {
    return !(a == b);
  }

  friend constexpr bool operator<(PeerId a, PeerId b) {
    return a.value < b.value;
  }
};

/// \ingroup karma_platform
/// Transport channel index.
using ChannelId = uint8_t;

/// \ingroup karma_platform
/// Packet delivery mode.
enum class Delivery {
  Reliable,
  Unreliable
};

/// \ingroup karma_platform
/// Remote endpoint metadata known by the transport.
struct Endpoint {
  std::string ip;
  uint16_t port = 0;
};

/// \ingroup karma_platform
/// Source used to populate a server-list entry.
enum class ServerListSource {
  Lan,
  Master
};

/// \ingroup karma_platform
/// Typed server metadata used by LAN discovery and master-list integrations.
struct ServerListing {
  std::string server_id;
  Endpoint connect_endpoint{};
  uint16_t game_port = 0;
  uint32_t app_id = 0;
  uint16_t protocol_version = kProtocolVersion;
  std::string name;
  std::string map;
  std::string mode;
  uint16_t current_players = 0;
  uint16_t max_players = 0;
  std::unordered_map<std::string, std::string> attributes;
  ServerListSource source = ServerListSource::Lan;
  std::chrono::steady_clock::time_point last_seen{};
  std::chrono::milliseconds ttl{0};
  std::chrono::steady_clock::time_point expires_at{};
};

/// \ingroup karma_platform
/// Server-list cache event kind.
enum class ServerListEventType {
  Found,
  Updated,
  Removed,
  Expired
};

/// \ingroup karma_platform
/// Server-list cache event.
struct ServerListEvent {
  ServerListEventType type = ServerListEventType::Found;
  ServerListing listing{};
};

/// \ingroup karma_platform
/// Sort order used by `ServerListCache::list(ServerListQuery)`.
enum class ServerListSort {
  ServerId,
  Name,
  Source,
  PlayerCount,
  Capacity,
  LastSeen,
  Endpoint
};

/// \ingroup karma_platform
/// Open-ended cache query for server-browser consumers.
struct ServerListQuery {
  std::optional<ServerListSource> source;
  std::optional<uint32_t> app_id;
  std::string text;
  std::string map;
  std::string mode;
  bool hide_full = false;
  std::unordered_map<std::string, std::string> attributes;
  std::vector<std::string> pinned_server_ids;
  ServerListSort sort = ServerListSort::Name;
  bool descending = false;
};

/// \ingroup karma_platform
/// Reusable cache for LAN and master-list server results.
class ServerListCache {
 public:
  ServerListEventType upsert(ServerListing listing,
                             ServerListSource source,
                             std::chrono::steady_clock::time_point now,
                             std::chrono::milliseconds ttl = std::chrono::milliseconds{0});
  bool removeByServerId(const std::string& server_id,
                        ServerListing* removed = nullptr);
  bool removeByEndpoint(const Endpoint& endpoint,
                        ServerListing* removed = nullptr);
  std::vector<ServerListing> expire(
      std::chrono::steady_clock::time_point now,
      std::optional<ServerListSource> source = std::nullopt);

  std::optional<ServerListing> findByServerId(const std::string& server_id) const;
  std::optional<ServerListing> findByEndpoint(const Endpoint& endpoint) const;
  std::vector<ServerListing> list() const;
  std::vector<ServerListing> list(const ServerListQuery& query) const;
  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  void clear();

 private:
  static std::string endpointKey(const Endpoint& endpoint);
  static std::string listingKey(const ServerListing& listing);
  bool removeByKey(const std::string& key, ServerListing* removed);
  void indexListing(const std::string& key, const ServerListing& listing);
  void unindexListing(const std::string& key, const ServerListing& listing);

  std::unordered_map<std::string, ServerListing> entries_;
  std::unordered_map<std::string, std::string> id_to_key_;
  std::unordered_map<std::string, std::string> endpoint_to_key_;
};

/// \ingroup karma_platform
/// LAN discovery runtime configuration.
struct LanDiscoveryConfig {
  uint16_t discovery_port = 0;
  uint32_t app_id = 0;
  uint16_t game_port = 0;
  ServerListing listing{};
  std::chrono::milliseconds beacon_interval{1000};
  std::chrono::milliseconds entry_ttl{5000};
};

/// \ingroup karma_platform
/// Default convention for LAN discovery ports: `game_port + 1`, clamped at `65535`.
uint16_t defaultLanDiscoveryPort(uint16_t game_port);
/// \ingroup karma_platform
/// Creates a stable server id from app id, game port, and an optional process/game salt.
std::string makeLanServerId(uint32_t app_id,
                            uint16_t game_port,
                            const std::string& salt = {});
/// \ingroup karma_platform
/// Builds a normalized LAN listing with a generated stable id when `server_id` is empty.
ServerListing makeLanServerListing(uint32_t app_id,
                                   uint16_t game_port,
                                   std::string name,
                                   std::string map = {},
                                   std::string mode = {},
                                   std::string server_id = {});

/// \ingroup karma_platform
/// LAN discovery wire protocol version.
inline constexpr uint16_t kLanDiscoveryVersion = 1;
/// \ingroup karma_platform
/// LAN discovery packet magic for malformed datagram rejection.
inline constexpr uint32_t kLanDiscoveryMagic = 0x4B444953u;  // KDIS
/// \ingroup karma_platform
/// Maximum LAN discovery datagram size.
inline constexpr std::size_t kLanDiscoveryMaxDatagramSize = 1200;

/// \ingroup karma_platform
/// LAN discovery datagram kind.
enum class LanDiscoveryMessageType : uint8_t {
  Query = 1,
  Advertisement = 2
};

/// \ingroup karma_platform
/// LAN discovery operation status.
enum class LanDiscoveryStatus {
  Ok,
  NotOpen,
  InvalidConfig,
  BindFailed,
  WouldBlock,
  EncodeFailed,
  OversizedPacket,
  BackendError
};

/// \ingroup karma_platform
/// Result for LAN discovery socket and send operations.
struct LanDiscoveryResult {
  LanDiscoveryStatus status = LanDiscoveryStatus::BackendError;
  std::size_t bytes = 0;

  constexpr bool ok() const { return status == LanDiscoveryStatus::Ok; }
  constexpr bool wouldBlock() const { return status == LanDiscoveryStatus::WouldBlock; }
};

/// \ingroup karma_platform
/// LAN discovery decode status.
enum class LanDiscoveryDecodeStatus {
  Ok,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  UnknownMessageType,
  AppIdMismatch,
  Malformed,
  OversizedPacket
};

/// \ingroup karma_platform
/// Decoded LAN discovery datagram.
struct LanDiscoveryPacket {
  LanDiscoveryMessageType message_type = LanDiscoveryMessageType::Query;
  uint32_t app_id = 0;
  uint64_t nonce = 0;
  ServerListing listing{};
};

/// \ingroup karma_platform
/// LAN discovery packet encode result.
struct LanDiscoveryEncodeResult {
  LanDiscoveryStatus status = LanDiscoveryStatus::EncodeFailed;
  std::vector<std::byte> bytes;

  bool ok() const { return status == LanDiscoveryStatus::Ok; }
};

/// \ingroup karma_platform
/// LAN discovery packet decode result.
struct LanDiscoveryDecodeResult {
  LanDiscoveryDecodeStatus status = LanDiscoveryDecodeStatus::Malformed;
  LanDiscoveryPacket packet{};

  bool ok() const { return status == LanDiscoveryDecodeStatus::Ok; }
};

/// \ingroup karma_platform
/// Encodes a LAN discovery query datagram.
LanDiscoveryEncodeResult encodeLanDiscoveryQuery(uint32_t app_id, uint64_t nonce = 0);
/// \ingroup karma_platform
/// Encodes a LAN discovery advertisement datagram.
LanDiscoveryEncodeResult encodeLanDiscoveryAdvertisement(const ServerListing& listing,
                                                         uint64_t nonce = 0);
/// \ingroup karma_platform
/// Decodes a LAN discovery datagram. Set `expected_app_id` to `0` to skip app id matching.
LanDiscoveryDecodeResult decodeLanDiscoveryPacket(std::span<const std::byte> bytes,
                                                  uint32_t expected_app_id = 0,
                                                  uint16_t expected_version =
                                                      kLanDiscoveryVersion);

/// \ingroup karma_platform
/// Minimal nonblocking UDP datagram socket used by LAN discovery.
class ILanDatagramSocket {
 public:
  virtual ~ILanDatagramSocket() = default;

  virtual LanDiscoveryResult open(uint16_t port, bool enable_broadcast = true) = 0;
  virtual void close() = 0;
  virtual bool isOpen() const = 0;
  virtual LanDiscoveryResult sendTo(const Endpoint& endpoint,
                                    std::span<const std::byte> payload) = 0;
  virtual LanDiscoveryResult receive(Endpoint& from,
                                     std::vector<std::byte>& payload) = 0;
};

/// \ingroup karma_platform
/// Creates the platform UDP socket used by LAN discovery.
std::unique_ptr<ILanDatagramSocket> createLanDatagramSocket();

/// \ingroup karma_platform
/// Poll-driven LAN server advertiser using UDP broadcast.
class LanServerAdvertiser {
 public:
  explicit LanServerAdvertiser(LanDiscoveryConfig config);
  LanServerAdvertiser(LanDiscoveryConfig config,
                      std::unique_ptr<ILanDatagramSocket> socket);
  ~LanServerAdvertiser();

  LanDiscoveryResult start();
  void stop();
  bool isRunning() const { return running_; }

  const ServerListing& listing() const { return listing_; }
  void updateListing(ServerListing listing);

  LanDiscoveryResult advertiseNow(uint64_t nonce = 0);
  LanDiscoveryResult poll(std::chrono::steady_clock::time_point now);

 private:
  void normalizeListing();
  LanDiscoveryResult sendAdvertisement(const Endpoint& endpoint, uint64_t nonce);

  LanDiscoveryConfig config_;
  std::unique_ptr<ILanDatagramSocket> socket_;
  ServerListing listing_{};
  bool running_ = false;
  std::optional<std::chrono::steady_clock::time_point> next_beacon_;
};

/// \ingroup karma_platform
/// Poll-driven LAN server browser using UDP broadcast.
class LanServerBrowser {
 public:
  explicit LanServerBrowser(LanDiscoveryConfig config);
  LanServerBrowser(LanDiscoveryConfig config,
                   std::unique_ptr<ILanDatagramSocket> socket);
  LanServerBrowser(LanDiscoveryConfig config,
                   ServerListCache& cache,
                   std::unique_ptr<ILanDatagramSocket> socket = {});
  ~LanServerBrowser();

  LanDiscoveryResult start();
  void stop();
  bool isRunning() const { return running_; }

  ServerListCache& cache() { return *cache_; }
  const ServerListCache& cache() const { return *cache_; }

  LanDiscoveryResult sendQuery(uint64_t nonce = 0);
  LanDiscoveryResult poll(std::chrono::steady_clock::time_point now,
                          std::vector<ServerListEvent>& out_events);

 private:
  void handleAdvertisement(const LanDiscoveryPacket& packet,
                           const Endpoint& sender,
                           std::chrono::steady_clock::time_point now,
                           std::vector<ServerListEvent>& out_events);

  LanDiscoveryConfig config_;
  std::unique_ptr<ILanDatagramSocket> socket_;
  ServerListCache owned_cache_;
  ServerListCache* cache_ = nullptr;
  bool running_ = false;
  uint64_t next_nonce_ = 1;
};

/// \ingroup karma_platform
/// Open-ended master-list query parameters.
struct MasterServerQuery {
  std::unordered_map<std::string, std::string> filters;
  std::unordered_map<std::string, std::string> attributes;
};

/// \ingroup karma_platform
/// Master-list event kind.
enum class MasterServerEventType {
  Listing,
  Removed,
  Published,
  Unpublished,
  Error
};

/// \ingroup karma_platform
/// Master-list event produced by game- or library-provided transports.
struct MasterServerEvent {
  MasterServerEventType type = MasterServerEventType::Listing;
  ServerListing listing{};
  std::string server_id;
  std::string error;
  std::unordered_map<std::string, std::string> attributes;
};

/// \ingroup karma_platform
/// Abstract master-list client. Karma does not prescribe HTTP, auth, or schema details.
class IMasterServerClient {
 public:
  virtual ~IMasterServerClient() = default;

  virtual bool publish(const ServerListing& listing) = 0;
  virtual bool unpublish(const std::string& server_id) = 0;
  virtual bool requestList(const MasterServerQuery& query) = 0;
  virtual void poll(std::vector<MasterServerEvent>& out_events) = 0;
};

/// \ingroup karma_platform
/// Combines LAN discovery and master-list events into one cache.
class ServerDirectory {
 public:
  ServerDirectory();
  explicit ServerDirectory(ServerListCache cache);
  ~ServerDirectory();

  ServerListCache& cache() { return cache_; }
  const ServerListCache& cache() const { return cache_; }

  LanServerBrowser& enableLanDiscovery(LanDiscoveryConfig config);
  LanServerBrowser& enableLanDiscovery(LanDiscoveryConfig config,
                                       std::unique_ptr<ILanDatagramSocket> socket);
  void disableLanDiscovery();
  LanServerBrowser* lanBrowser() { return lan_browser_.get(); }
  const LanServerBrowser* lanBrowser() const { return lan_browser_.get(); }

  void setMasterClient(std::unique_ptr<IMasterServerClient> client);
  IMasterServerClient* masterClient() { return master_client_.get(); }
  const IMasterServerClient* masterClient() const { return master_client_.get(); }

  bool publishToMaster(const ServerListing& listing);
  bool unpublishFromMaster(const std::string& server_id);
  bool requestMasterList(const MasterServerQuery& query);

  void poll(std::chrono::steady_clock::time_point now,
            std::vector<ServerListEvent>& out_events);

 private:
  ServerListCache cache_;
  std::unique_ptr<LanServerBrowser> lan_browser_;
  std::unique_ptr<IMasterServerClient> master_client_;
  std::vector<MasterServerEvent> master_events_;
};

/// \ingroup karma_platform
/// High-level reason associated with transport disconnect events.
enum class DisconnectReason {
  None,
  Local,
  Remote,
  Timeout,
  Kicked,
  TransportError,
  ProtocolError
};

/// \ingroup karma_platform
/// Result code for an outbound transport send.
enum class SendStatus {
  Ok,
  NotConnected,
  UnknownPeer,
  InvalidChannel,
  InvalidPayload,
  BackendError
};

/// \ingroup karma_platform
/// Result for an outbound transport send.
struct SendResult {
  SendStatus status = SendStatus::BackendError;
  std::size_t bytes_queued = 0;

  constexpr bool ok() const { return status == SendStatus::Ok; }
};

/// \ingroup karma_platform
/// Result code for client connection attempts.
enum class ConnectStatus {
  Connected,
  HostCreateFailed,
  ResolveFailed,
  NoAvailablePeer,
  Timeout,
  BackendError
};

/// \ingroup karma_platform
/// Result for a client connection attempt.
struct ConnectResult {
  ConnectStatus status = ConnectStatus::BackendError;
  PeerId peer{};
  Endpoint endpoint{};

  constexpr bool connected() const { return status == ConnectStatus::Connected; }
};

/// \ingroup karma_platform
/// Network transport event.
struct TransportEvent {
  enum class Type {
    Receive,
    Connect,
    Disconnect
  };

  Type type{};
  PeerId peer{};
  ChannelId channel = 0;
  DisconnectReason disconnect_reason = DisconnectReason::None;
  std::vector<std::byte> payload;
  Endpoint endpoint{};
};

/// \ingroup karma_platform
/// Client-side transport interface.
class IClientTransport {
 public:
  virtual ~IClientTransport() = default;

  virtual ConnectResult connect(const std::string& host, uint16_t port, int timeout_ms) = 0;
  virtual void disconnect(DisconnectReason reason = DisconnectReason::Local) = 0;
  virtual bool isConnected() const = 0;
  virtual PeerId serverPeer() const = 0;

  virtual void poll(std::vector<TransportEvent>& out_events) = 0;

  virtual SendResult send(ChannelId channel,
                          const std::byte* data,
                          std::size_t size,
                          Delivery delivery,
                          bool flush) = 0;
  virtual void flush() = 0;

  virtual std::optional<Endpoint> remoteEndpoint() const = 0;
};

/// \ingroup karma_platform
/// Server-side transport interface.
class IServerTransport {
 public:
  virtual ~IServerTransport() = default;

  virtual void poll(std::vector<TransportEvent>& out_events) = 0;

  virtual SendResult send(PeerId peer,
                          ChannelId channel,
                          const std::byte* data,
                          std::size_t size,
                          Delivery delivery,
                          bool flush) = 0;
  virtual void disconnect(PeerId peer,
                          DisconnectReason reason = DisconnectReason::Local) = 0;
  virtual void flush() = 0;
  virtual std::optional<Endpoint> endpoint(PeerId peer) const = 0;
  virtual std::vector<PeerId> peers() const = 0;
};

}  // namespace karma::network


#include <cstdint>
#include <memory>


namespace karma::network {

/// \ingroup karma_platform
/// Creates the configured default client transport.
std::unique_ptr<IClientTransport> createDefaultClientTransport();
/// Creates the configured default server transport.
std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients = 50,
                                                               int num_channels = 2);

}  // namespace karma::network


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


namespace karma::network {

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
  std::string name;
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
  SendResult sendHandshake(uint32_t tick = 0);
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

}  // namespace karma::network


#include <memory>


namespace karma::network {

/// \ingroup karma_platform
/// Creates an ENet client transport.
std::unique_ptr<IClientTransport> createEnetClientTransport();
/// \ingroup karma_platform
/// Creates an ENet server transport.
std::unique_ptr<IServerTransport> createEnetServerTransport(uint16_t port,
                                                            int max_clients = 50,
                                                            int num_channels = 2);

}  // namespace karma::network



namespace karma::network {

/// \ingroup karma_features
/// Global network mode for process-level multiplayer behavior.
enum class NetworkRole {
  Offline,
  Server,
  Client,
  ListenServer
};

/// \ingroup karma_features
/// Returns true for roles that own server-side session and replication flow.
constexpr bool isServerRole(NetworkRole role) {
  return role == NetworkRole::Server || role == NetworkRole::ListenServer;
}

/// \ingroup karma_features
/// Returns true for roles that participate as a client endpoint.
constexpr bool isClientRole(NetworkRole role) {
  return role == NetworkRole::Client || role == NetworkRole::ListenServer;
}

/// \ingroup karma_features
/// Returns true for roles that should make authoritative simulation decisions.
constexpr bool isAuthorityRole(NetworkRole role) {
  return isServerRole(role);
}

/// \ingroup karma_features
/// Runtime role plus the local transport peer id when one has been assigned.
struct NetworkRoleContext {
  NetworkRole role = NetworkRole::Offline;
  network::PeerId local_peer{};

  constexpr bool isServer() const { return isServerRole(role); }
  constexpr bool isClient() const { return isClientRole(role); }
  constexpr bool isAuthority() const { return isAuthorityRole(role); }
};

}  // namespace karma::network


#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace karma::network {

/// \ingroup karma_features
/// Stable component type ids used by built-in v1 replicators.
inline constexpr uint32_t kTransformComponentWireId = 1;
inline constexpr uint32_t kTagComponentWireId = 2;

/// \ingroup karma_features
/// Per-component encode/apply hooks for binary replication.
struct ComponentReplicator {
  uint32_t component_type = 0;
  std::string name;
  components::AuthorityMode authority = components::AuthorityMode::Server;
  std::function<bool(const world::World&, world::Entity, network::BinaryWriter&)> encode_full;
  std::function<bool(const world::World&,
                     world::Entity,
                     std::span<const std::byte>,
                     network::BinaryWriter&)>
      encode_delta;
  std::function<bool(world::World&, world::Entity, network::BinaryReader&, bool)> decode_apply;
  std::function<bool(std::span<const std::byte>, std::span<const std::byte>)> is_dirty;
};

/// \ingroup karma_features
/// Registry for explicit component replication hooks.
class ComponentReplicationRegistry {
 public:
  bool registerReplicator(ComponentReplicator replicator);
  const ComponentReplicator* find(uint32_t component_type) const;
  bool contains(uint32_t component_type) const { return find(component_type) != nullptr; }

 private:
  std::unordered_map<uint32_t, ComponentReplicator> replicators_;
};

/// \ingroup karma_features
/// Registers built-in TransformComponent and TagComponent replicators.
void registerBuiltinReplicators(ComponentReplicationRegistry& registry);

/// \ingroup karma_features
/// Per-entity recipient visibility predicate used by server replication.
using ReplicationVisibilityPredicate =
    std::function<bool(const network::SessionPeer&,
                       world::Entity,
                       components::NetworkEntityId)>;

/// \ingroup karma_features
/// Callback invoked by networking runtime modules after session event polling.
using NetworkEventHandler = std::function<void(const network::SessionEvent&, world::World&)>;

/// \ingroup karma_features
/// Runtime-facing counters for network polling, replication, and rejection paths.
struct NetworkRuntimeStats {
  uint64_t events_received = 0;
  uint64_t protocol_errors = 0;
  uint64_t custom_messages = 0;
  uint64_t input_commands = 0;
  uint64_t replication_messages = 0;
  uint64_t replication_sends_attempted = 0;
  uint64_t replication_sends_succeeded = 0;
  uint64_t authority_rejects = 0;
  uint64_t stale_replication_rejects = 0;
};

/// \ingroup karma_features
/// Configuration for the authoritative server networking runtime module.
struct ServerNetworkRuntimeConfig {
  uint32_t app_id = 0;
  ReplicationVisibilityPredicate visibility;
  NetworkEventHandler event_handler;
  bool replication_enabled = true;
};

/// \ingroup karma_features
/// Configuration for the client networking runtime module.
struct ClientNetworkRuntimeConfig {
  uint32_t app_id = 0;
  NetworkEventHandler event_handler;
  bool replication_enabled = true;
};

/// \ingroup karma_features
/// Server-side replication state and last-sent dirty tracking.
class ServerReplicationState {
 public:
  void reset();
  void ensureNetworkIds(world::World& world);
  void removePeer(network::PeerId peer);

  network::MultiSendResult replicate(world::World& world,
                                 network::ServerSession& session,
                                 const ComponentReplicationRegistry& registry,
                                 uint32_t tick,
                                 const ReplicationVisibilityPredicate& visibility = {});

  bool sendDespawn(network::ServerSession& session,
                   components::NetworkEntityId network_id,
                   network::PeerId peer,
                   uint32_t tick);
  bool applyClientComponentEvent(world::World& world,
                                 const ComponentReplicationRegistry& registry,
                                 const network::SessionEvent& event,
                                 bool server_override = false);
  std::size_t authorityRejects() const { return authority_rejects_; }

 private:
  struct SentComponentKey {
    uint32_t peer = 0;
    components::NetworkEntityId entity = 0;
    uint32_t component = 0;

    friend bool operator==(const SentComponentKey& a, const SentComponentKey& b) {
      return a.peer == b.peer && a.entity == b.entity && a.component == b.component;
    }
  };

  struct SentComponentKeyHash {
    std::size_t operator()(const SentComponentKey& key) const;
  };

  bool hasSpawnedFor(components::NetworkEntityId id, network::PeerId peer) const;
  void markSpawnedFor(components::NetworkEntityId id, network::PeerId peer);
  void clearSpawnedFor(components::NetworkEntityId id, network::PeerId peer);
  std::vector<std::byte> encodeSpawn(world::World& world,
                                     world::Entity entity,
                                     const ComponentReplicationRegistry& registry,
                                     network::PeerId peer);
  std::optional<std::vector<std::byte>> encodeComponent(world::World& world,
                                                        world::Entity entity,
                                                        const ComponentReplicator& replicator,
                                                        std::span<const std::byte> previous,
                                                        bool delta);
  void rememberComponent(network::PeerId peer,
                         components::NetworkEntityId id,
                         uint32_t component_type,
                         std::span<const std::byte> bytes);

  components::NetworkEntityId next_id_ = 1;
  std::unordered_map<components::NetworkEntityId, std::unordered_set<uint32_t>> spawned_;
  std::unordered_map<SentComponentKey, std::vector<std::byte>, SentComponentKeyHash> last_sent_;
  std::size_t authority_rejects_ = 0;
};

/// \ingroup karma_features
/// Client-side replicated entity map and snapshot application.
class ClientReplicationState {
 public:
  void reset();
  bool applyEvent(world::World& world,
                  const ComponentReplicationRegistry& registry,
                  const network::SessionEvent& event);

  std::optional<world::Entity> entityFor(components::NetworkEntityId id) const;
  std::size_t staleRejects() const { return stale_rejects_; }

 private:
  struct ReplicationStamp {
    uint32_t tick = 0;
    uint32_t sequence = 0;
  };

  struct ComponentStampKey {
    components::NetworkEntityId entity = 0;
    uint32_t component = 0;

    friend bool operator==(const ComponentStampKey& a, const ComponentStampKey& b) {
      return a.entity == b.entity && a.component == b.component;
    }
  };

  struct ComponentStampKeyHash {
    std::size_t operator()(const ComponentStampKey& key) const;
  };

  bool applySpawn(world::World& world,
                  const ComponentReplicationRegistry& registry,
                  std::span<const std::byte> payload,
                  ReplicationStamp stamp);
  bool applyDespawn(world::World& world,
                    std::span<const std::byte> payload,
                    ReplicationStamp stamp);
  bool applyComponentUpdate(world::World& world,
                            const ComponentReplicationRegistry& registry,
                            std::span<const std::byte> payload,
                            ReplicationStamp stamp);
  bool applyAuthorityTransfer(world::World& world,
                              std::span<const std::byte> payload,
                              ReplicationStamp stamp);
  bool isEntityEventStale(components::NetworkEntityId id, ReplicationStamp stamp) const;
  bool isComponentEventStale(components::NetworkEntityId id,
                             uint32_t component_type,
                             ReplicationStamp stamp) const;
  void rememberEntityStamp(components::NetworkEntityId id, ReplicationStamp stamp);
  void rememberComponentStamp(components::NetworkEntityId id,
                              uint32_t component_type,
                              ReplicationStamp stamp);
  void forgetComponentStamps(components::NetworkEntityId id);

  std::unordered_map<components::NetworkEntityId, world::Entity> entities_;
  std::unordered_map<components::NetworkEntityId, ReplicationStamp> entity_stamps_;
  std::unordered_map<ComponentStampKey, ReplicationStamp, ComponentStampKeyHash>
      component_stamps_;
  std::size_t stale_rejects_ = 0;
};

/// \ingroup karma_features
/// Server runtime module that polls a session, dispatches events, replicates after
/// fixed simulation, and flushes at frame end.
class ServerNetworkRuntimeModule final : public app::RuntimeModule {
 public:
  using EventHandler = NetworkEventHandler;

  ServerNetworkRuntimeModule(network::ServerSession& session,
                             ComponentReplicationRegistry& registry);
  ServerNetworkRuntimeModule(network::ServerSession& session,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);
  ServerNetworkRuntimeModule(std::unique_ptr<network::IServerTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);

  network::ServerSession& session() { return *session_; }
  const network::ServerSession& session() const { return *session_; }
  ServerReplicationState& replicationState() { return replication_; }
  const ServerReplicationState& replicationState() const { return replication_; }
  const NetworkRuntimeStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  void setEventHandler(EventHandler handler) { event_handler_ = std::move(handler); }
  void setVisibilityPredicate(ReplicationVisibilityPredicate predicate) {
    visibility_ = std::move(predicate);
  }
  void setReplicationEnabled(bool enabled) { replication_enabled_ = enabled; }
  bool replicationEnabled() const { return replication_enabled_; }

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onFrameBegin(world::World& world, float dt) override;
  void onAfterFixedUpdate(world::World& world, float fixed_dt, uint64_t fixed_tick) override;
  void onFrameEnd(world::World& world) override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<network::ServerSession> owned_session_;
  network::ServerSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ServerReplicationState replication_;
  EventHandler event_handler_;
  ReplicationVisibilityPredicate visibility_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<network::SessionEvent> events_;
};

/// \ingroup karma_features
/// Client runtime module that polls a session, applies replication events before
/// simulation, and flushes at frame end.
class ClientNetworkRuntimeModule final : public app::RuntimeModule {
 public:
  using EventHandler = NetworkEventHandler;

  ClientNetworkRuntimeModule(network::ClientSession& session,
                             ComponentReplicationRegistry& registry);
  ClientNetworkRuntimeModule(network::ClientSession& session,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);
  ClientNetworkRuntimeModule(std::unique_ptr<network::IClientTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);

  network::ClientSession& session() { return *session_; }
  const network::ClientSession& session() const { return *session_; }
  ClientReplicationState& replicationState() { return replication_; }
  const ClientReplicationState& replicationState() const { return replication_; }
  const NetworkRuntimeStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  void setEventHandler(EventHandler handler) { event_handler_ = std::move(handler); }
  void setReplicationEnabled(bool enabled) { replication_enabled_ = enabled; }
  bool replicationEnabled() const { return replication_enabled_; }

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onFrameBegin(world::World& world, float dt) override;
  void onFrameEnd(world::World& world) override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<network::ClientSession> owned_session_;
  network::ClientSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ClientReplicationState replication_;
  EventHandler event_handler_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<network::SessionEvent> events_;
};

}  // namespace karma::network
