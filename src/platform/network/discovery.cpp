#include "karma/network.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace karma::network {
namespace {

constexpr std::size_t kLanDiscoveryHeaderSize = 20;
constexpr const char* kBroadcastAddress = "255.255.255.255";

std::string endpointKeyString(const Endpoint& endpoint) {
  return endpoint.ip + ":" + std::to_string(endpoint.port);
}

std::string generatedServerId(const ServerListing& listing) {
  return makeLanServerId(listing.app_id, listing.game_port, listing.name);
}

uint32_t millisecondsToWire(std::chrono::milliseconds value) {
  if (value.count() <= 0) {
    return 0;
  }
  const auto max_value = static_cast<int64_t>(std::numeric_limits<uint32_t>::max());
  return static_cast<uint32_t>(std::min<int64_t>(value.count(), max_value));
}

void writeDiscoveryHeader(BinaryWriter& writer,
                          LanDiscoveryMessageType message_type,
                          uint32_t app_id,
                          uint64_t nonce) {
  writer.writeUInt32(kLanDiscoveryMagic);
  writer.writeUInt16(kLanDiscoveryVersion);
  writer.writeUInt8(static_cast<uint8_t>(message_type));
  writer.writeUInt8(0);
  writer.writeUInt32(app_id);
  writer.writeUInt64(nonce);
}

LanDiscoveryEncodeResult finishDiscoveryPacket(BinaryWriter& writer) {
  std::vector<std::byte> bytes = writer.takeBytes();
  if (bytes.size() > kLanDiscoveryMaxDatagramSize) {
    return {.status = LanDiscoveryStatus::OversizedPacket};
  }
  return {
      .status = LanDiscoveryStatus::Ok,
      .bytes = std::move(bytes),
  };
}

bool readDiscoveryHeader(BinaryReader& reader,
                         LanDiscoveryPacket& packet,
                         uint16_t expected_version,
                         LanDiscoveryDecodeStatus& status) {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint8_t message_type = 0;
  uint8_t reserved = 0;
  if (!reader.readUInt32(magic) ||
      !reader.readUInt16(version) ||
      !reader.readUInt8(message_type) ||
      !reader.readUInt8(reserved) ||
      !reader.readUInt32(packet.app_id) ||
      !reader.readUInt64(packet.nonce)) {
    status = LanDiscoveryDecodeStatus::TooSmall;
    return false;
  }
  (void)reserved;

  if (magic != kLanDiscoveryMagic) {
    status = LanDiscoveryDecodeStatus::BadMagic;
    return false;
  }
  if (version != expected_version) {
    status = LanDiscoveryDecodeStatus::UnsupportedVersion;
    return false;
  }
  if (message_type != static_cast<uint8_t>(LanDiscoveryMessageType::Query) &&
      message_type != static_cast<uint8_t>(LanDiscoveryMessageType::Advertisement)) {
    status = LanDiscoveryDecodeStatus::UnknownMessageType;
    return false;
  }
  packet.message_type = static_cast<LanDiscoveryMessageType>(message_type);
  return true;
}

ServerListing normalizedLanListing(const LanDiscoveryConfig& config,
                                   ServerListing listing) {
  listing.source = ServerListSource::Lan;
  listing.app_id = config.app_id;
  listing.game_port = config.game_port != 0 ? config.game_port : listing.game_port;
  if (listing.protocol_version == 0) {
    listing.protocol_version = kProtocolVersion;
  }
  if (listing.connect_endpoint.port == 0) {
    listing.connect_endpoint.port = listing.game_port;
  }
  if (listing.server_id.empty()) {
    listing.server_id = generatedServerId(listing);
  }
  listing.ttl = config.entry_ttl;
  return listing;
}

Endpoint broadcastEndpoint(uint16_t port) {
  return Endpoint{.ip = kBroadcastAddress, .port = port};
}

bool matchesQuery(const ServerListing& listing, const ServerListQuery& query) {
  if (query.source && listing.source != *query.source) {
    return false;
  }
  if (query.app_id && listing.app_id != *query.app_id) {
    return false;
  }
  if (!query.text.empty()) {
    const std::string endpoint = endpointKeyString(listing.connect_endpoint);
    const bool matched = listing.server_id.find(query.text) != std::string::npos ||
                         listing.name.find(query.text) != std::string::npos ||
                         listing.map.find(query.text) != std::string::npos ||
                         listing.mode.find(query.text) != std::string::npos ||
                         endpoint.find(query.text) != std::string::npos;
    if (!matched) {
      return false;
    }
  }
  if (!query.map.empty() && listing.map != query.map) {
    return false;
  }
  if (!query.mode.empty() && listing.mode != query.mode) {
    return false;
  }
  if (query.hide_full && listing.max_players > 0 &&
      listing.current_players >= listing.max_players) {
    return false;
  }
  for (const auto& [key, value] : query.attributes) {
    auto it = listing.attributes.find(key);
    if (it == listing.attributes.end() || it->second != value) {
      return false;
    }
  }
  return true;
}

std::size_t pinRank(const ServerListing& listing, const ServerListQuery& query) {
  const std::string key = listing.server_id.empty()
                              ? endpointKeyString(listing.connect_endpoint)
                              : listing.server_id;
  for (std::size_t i = 0; i < query.pinned_server_ids.size(); ++i) {
    if (query.pinned_server_ids[i] == key) {
      return i;
    }
  }
  return std::numeric_limits<std::size_t>::max();
}

int sourceRank(ServerListSource source) {
  return source == ServerListSource::Lan ? 0 : 1;
}

int compareListings(const ServerListing& lhs,
                    const ServerListing& rhs,
                    ServerListSort sort) {
  switch (sort) {
    case ServerListSort::ServerId:
      if (lhs.server_id != rhs.server_id) {
        return lhs.server_id < rhs.server_id ? -1 : 1;
      }
      break;
    case ServerListSort::Name:
      if (lhs.name != rhs.name) {
        return lhs.name < rhs.name ? -1 : 1;
      }
      break;
    case ServerListSort::Source:
      if (lhs.source != rhs.source) {
        return sourceRank(lhs.source) < sourceRank(rhs.source) ? -1 : 1;
      }
      break;
    case ServerListSort::PlayerCount:
      if (lhs.current_players != rhs.current_players) {
        return lhs.current_players < rhs.current_players ? -1 : 1;
      }
      break;
    case ServerListSort::Capacity:
      if (lhs.max_players != rhs.max_players) {
        return lhs.max_players < rhs.max_players ? -1 : 1;
      }
      break;
    case ServerListSort::LastSeen:
      if (lhs.last_seen != rhs.last_seen) {
        return lhs.last_seen < rhs.last_seen ? -1 : 1;
      }
      break;
    case ServerListSort::Endpoint: {
      const std::string lhs_endpoint = endpointKeyString(lhs.connect_endpoint);
      const std::string rhs_endpoint = endpointKeyString(rhs.connect_endpoint);
      if (lhs_endpoint != rhs_endpoint) {
        return lhs_endpoint < rhs_endpoint ? -1 : 1;
      }
      break;
    }
  }

  const std::string lhs_endpoint = endpointKeyString(lhs.connect_endpoint);
  const std::string rhs_endpoint = endpointKeyString(rhs.connect_endpoint);
  if (lhs.server_id != rhs.server_id) {
    return lhs.server_id < rhs.server_id ? -1 : 1;
  }
  if (lhs_endpoint != rhs_endpoint) {
    return lhs_endpoint < rhs_endpoint ? -1 : 1;
  }
  return 0;
}

uint64_t fnv1a64(const std::string& value) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : value) {
    hash ^= static_cast<uint64_t>(c);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string hostFingerprint() {
  if (const char* host = std::getenv("HOSTNAME")) {
    return host;
  }
  if (const char* host = std::getenv("COMPUTERNAME")) {
    return host;
  }
  return "local";
}

#if defined(_WIN32)
class WinsockGlobal {
 public:
  WinsockGlobal() {
    if (refCount()++ == 0) {
      WSADATA data{};
      initialized() = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
  }

  ~WinsockGlobal() {
    auto& count = refCount();
    if (count == 0) {
      return;
    }
    if (--count == 0 && initialized()) {
      WSACleanup();
      initialized() = false;
    }
  }

  bool ok() const { return initialized(); }

 private:
  static uint32_t& refCount() {
    static uint32_t count = 0;
    return count;
  }

  static bool& initialized() {
    static bool value = false;
    return value;
  }
};

using NativeSocketHandle = SOCKET;
constexpr NativeSocketHandle kInvalidNativeSocket = INVALID_SOCKET;

bool isWouldBlockError() {
  const int error = WSAGetLastError();
  return error == WSAEWOULDBLOCK;
}

void closeNativeSocket(NativeSocketHandle socket) {
  closesocket(socket);
}

bool setNonBlocking(NativeSocketHandle socket) {
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
}
#else
using NativeSocketHandle = int;
constexpr NativeSocketHandle kInvalidNativeSocket = -1;

bool isWouldBlockError() {
  return errno == EWOULDBLOCK || errno == EAGAIN;
}

void closeNativeSocket(NativeSocketHandle socket) {
  ::close(socket);
}

bool setNonBlocking(NativeSocketHandle socket) {
  const int flags = fcntl(socket, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

class NativeLanDatagramSocket final : public ILanDatagramSocket {
 public:
  NativeLanDatagramSocket() = default;
  ~NativeLanDatagramSocket() override { close(); }

  LanDiscoveryResult open(uint16_t port, bool enable_broadcast) override {
    close();

#if defined(_WIN32)
    if (!winsock_.ok()) {
      return {.status = LanDiscoveryStatus::BackendError};
    }
#endif

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidNativeSocket) {
      return {.status = LanDiscoveryStatus::BackendError};
    }

    int reuse = 1;
    setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#if !defined(_WIN32) && defined(SO_REUSEPORT)
    setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#endif

    if (enable_broadcast) {
      int broadcast = 1;
      if (setsockopt(socket_,
                     SOL_SOCKET,
                     SO_BROADCAST,
                     reinterpret_cast<const char*>(&broadcast),
                     sizeof(broadcast)) != 0) {
        close();
        return {.status = LanDiscoveryStatus::BackendError};
      }
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      close();
      return {.status = LanDiscoveryStatus::BindFailed};
    }

    if (!setNonBlocking(socket_)) {
      close();
      return {.status = LanDiscoveryStatus::BackendError};
    }
    return {.status = LanDiscoveryStatus::Ok};
  }

  void close() override {
    if (socket_ != kInvalidNativeSocket) {
      closeNativeSocket(socket_);
      socket_ = kInvalidNativeSocket;
    }
  }

  bool isOpen() const override {
    return socket_ != kInvalidNativeSocket;
  }

  LanDiscoveryResult sendTo(const Endpoint& endpoint,
                            std::span<const std::byte> payload) override {
    if (!isOpen()) {
      return {.status = LanDiscoveryStatus::NotOpen};
    }
    if (payload.size() > kLanDiscoveryMaxDatagramSize) {
      return {.status = LanDiscoveryStatus::OversizedPacket};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    const std::string ip = endpoint.ip.empty() ? kBroadcastAddress : endpoint.ip;
    if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
      return {.status = LanDiscoveryStatus::BackendError};
    }

    const auto sent = sendto(socket_,
                             reinterpret_cast<const char*>(payload.data()),
                             static_cast<int>(payload.size()),
                             0,
                             reinterpret_cast<const sockaddr*>(&address),
                             sizeof(address));
    if (sent < 0) {
      return {.status = isWouldBlockError() ? LanDiscoveryStatus::WouldBlock
                                            : LanDiscoveryStatus::BackendError};
    }
    return {
        .status = LanDiscoveryStatus::Ok,
        .bytes = static_cast<std::size_t>(sent),
    };
  }

  LanDiscoveryResult receive(Endpoint& from,
                             std::vector<std::byte>& payload) override {
    if (!isOpen()) {
      return {.status = LanDiscoveryStatus::NotOpen};
    }

    std::array<std::byte, kLanDiscoveryMaxDatagramSize> buffer{};
    sockaddr_in address{};
#if defined(_WIN32)
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    const auto received = recvfrom(socket_,
                                   reinterpret_cast<char*>(buffer.data()),
                                   static_cast<int>(buffer.size()),
                                   0,
                                   reinterpret_cast<sockaddr*>(&address),
                                   &address_size);
    if (received < 0) {
      return {.status = isWouldBlockError() ? LanDiscoveryStatus::WouldBlock
                                            : LanDiscoveryStatus::BackendError};
    }

    std::array<char, INET_ADDRSTRLEN> ip_buffer{};
    if (inet_ntop(AF_INET, &address.sin_addr, ip_buffer.data(), ip_buffer.size()) == nullptr) {
      from.ip.clear();
    } else {
      from.ip = ip_buffer.data();
    }
    from.port = ntohs(address.sin_port);
    payload.assign(buffer.begin(), buffer.begin() + received);
    return {
        .status = LanDiscoveryStatus::Ok,
        .bytes = static_cast<std::size_t>(received),
    };
  }

 private:
#if defined(_WIN32)
  WinsockGlobal winsock_;
#endif
  NativeSocketHandle socket_ = kInvalidNativeSocket;
};

}  // namespace

uint16_t defaultLanDiscoveryPort(uint16_t game_port) {
  if (game_port == std::numeric_limits<uint16_t>::max()) {
    return game_port;
  }
  return static_cast<uint16_t>(game_port + 1);
}

std::string makeLanServerId(uint32_t app_id,
                            uint16_t game_port,
                            const std::string& salt) {
  std::string seed = std::to_string(app_id) + ":" + std::to_string(game_port) +
                     ":" + hostFingerprint();
  if (!salt.empty()) {
    seed += ":" + salt;
  }
  std::ostringstream out;
  out << "lan-" << std::hex << std::setw(16) << std::setfill('0') << fnv1a64(seed);
  return out.str();
}

ServerListing makeLanServerListing(uint32_t app_id,
                                   uint16_t game_port,
                                   std::string name,
                                   std::string map,
                                   std::string mode,
                                   std::string server_id) {
  ServerListing listing{
      .server_id = std::move(server_id),
      .connect_endpoint = Endpoint{.port = game_port},
      .game_port = game_port,
      .app_id = app_id,
      .protocol_version = kProtocolVersion,
      .name = std::move(name),
      .map = std::move(map),
      .mode = std::move(mode),
      .source = ServerListSource::Lan,
  };
  if (listing.server_id.empty()) {
    listing.server_id = makeLanServerId(app_id, game_port, listing.name);
  }
  return listing;
}

ServerListEventType ServerListCache::upsert(ServerListing listing,
                                            ServerListSource source,
                                            std::chrono::steady_clock::time_point now,
                                            std::chrono::milliseconds ttl) {
  if (listing.connect_endpoint.port == 0) {
    listing.connect_endpoint.port = listing.game_port;
  }

  std::string key;
  if (!listing.server_id.empty()) {
    auto it = id_to_key_.find(listing.server_id);
    if (it != id_to_key_.end()) {
      key = it->second;
    }
  }
  if (key.empty()) {
    auto it = endpoint_to_key_.find(endpointKey(listing.connect_endpoint));
    if (it != endpoint_to_key_.end()) {
      key = it->second;
    }
  }
  if (key.empty()) {
    key = listingKey(listing);
  }

  const bool existing = entries_.find(key) != entries_.end();
  if (existing) {
    unindexListing(key, entries_.at(key));
  }

  listing.source = source;
  listing.last_seen = now;
  if (ttl.count() > 0) {
    listing.ttl = ttl;
  }
  listing.expires_at = listing.ttl.count() > 0
                           ? now + listing.ttl
                           : std::chrono::steady_clock::time_point::max();

  entries_[key] = std::move(listing);
  indexListing(key, entries_.at(key));
  return existing ? ServerListEventType::Updated : ServerListEventType::Found;
}

bool ServerListCache::removeByServerId(const std::string& server_id,
                                       ServerListing* removed) {
  auto it = id_to_key_.find(server_id);
  if (it == id_to_key_.end()) {
    return false;
  }
  return removeByKey(it->second, removed);
}

bool ServerListCache::removeByEndpoint(const Endpoint& endpoint,
                                       ServerListing* removed) {
  auto it = endpoint_to_key_.find(endpointKey(endpoint));
  if (it == endpoint_to_key_.end()) {
    return false;
  }
  return removeByKey(it->second, removed);
}

std::vector<ServerListing> ServerListCache::expire(
    std::chrono::steady_clock::time_point now,
    std::optional<ServerListSource> source) {
  std::vector<std::string> expired_keys;
  for (const auto& [key, listing] : entries_) {
    if (source && listing.source != *source) {
      continue;
    }
    if (listing.expires_at != std::chrono::steady_clock::time_point::max() &&
        listing.expires_at <= now) {
      expired_keys.push_back(key);
    }
  }

  std::vector<ServerListing> expired;
  expired.reserve(expired_keys.size());
  for (const std::string& key : expired_keys) {
    ServerListing removed;
    if (removeByKey(key, &removed)) {
      expired.push_back(std::move(removed));
    }
  }
  return expired;
}

std::optional<ServerListing> ServerListCache::findByServerId(
    const std::string& server_id) const {
  auto key_it = id_to_key_.find(server_id);
  if (key_it == id_to_key_.end()) {
    return std::nullopt;
  }
  auto entry_it = entries_.find(key_it->second);
  if (entry_it == entries_.end()) {
    return std::nullopt;
  }
  return entry_it->second;
}

std::optional<ServerListing> ServerListCache::findByEndpoint(
    const Endpoint& endpoint) const {
  auto key_it = endpoint_to_key_.find(endpointKey(endpoint));
  if (key_it == endpoint_to_key_.end()) {
    return std::nullopt;
  }
  auto entry_it = entries_.find(key_it->second);
  if (entry_it == entries_.end()) {
    return std::nullopt;
  }
  return entry_it->second;
}

std::vector<ServerListing> ServerListCache::list() const {
  std::vector<ServerListing> listings;
  listings.reserve(entries_.size());
  for (const auto& [key, listing] : entries_) {
    (void)key;
    listings.push_back(listing);
  }
  std::sort(listings.begin(),
            listings.end(),
            [](const ServerListing& lhs, const ServerListing& rhs) {
              if (lhs.server_id != rhs.server_id) {
                return lhs.server_id < rhs.server_id;
              }
              return endpointKeyString(lhs.connect_endpoint) <
                     endpointKeyString(rhs.connect_endpoint);
            });
  return listings;
}

std::vector<ServerListing> ServerListCache::list(const ServerListQuery& query) const {
  std::vector<ServerListing> listings;
  listings.reserve(entries_.size());
  for (const auto& [key, listing] : entries_) {
    (void)key;
    if (matchesQuery(listing, query)) {
      listings.push_back(listing);
    }
  }

  std::sort(listings.begin(),
            listings.end(),
            [&](const ServerListing& lhs, const ServerListing& rhs) {
              const std::size_t lhs_pin = pinRank(lhs, query);
              const std::size_t rhs_pin = pinRank(rhs, query);
              if (lhs_pin != rhs_pin) {
                return lhs_pin < rhs_pin;
              }
              int cmp = compareListings(lhs, rhs, query.sort);
              if (query.descending) {
                cmp = -cmp;
              }
              return cmp < 0;
            });
  return listings;
}

void ServerListCache::clear() {
  entries_.clear();
  id_to_key_.clear();
  endpoint_to_key_.clear();
}

std::string ServerListCache::endpointKey(const Endpoint& endpoint) {
  return endpointKeyString(endpoint);
}

std::string ServerListCache::listingKey(const ServerListing& listing) {
  if (!listing.server_id.empty()) {
    return "id:" + listing.server_id;
  }
  return "ep:" + endpointKey(listing.connect_endpoint);
}

bool ServerListCache::removeByKey(const std::string& key,
                                  ServerListing* removed) {
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    return false;
  }
  if (removed) {
    *removed = it->second;
  }
  unindexListing(key, it->second);
  entries_.erase(it);
  return true;
}

void ServerListCache::indexListing(const std::string& key,
                                   const ServerListing& listing) {
  if (!listing.server_id.empty()) {
    id_to_key_[listing.server_id] = key;
  }
  endpoint_to_key_[endpointKey(listing.connect_endpoint)] = key;
}

void ServerListCache::unindexListing(const std::string& key,
                                     const ServerListing& listing) {
  if (!listing.server_id.empty()) {
    auto it = id_to_key_.find(listing.server_id);
    if (it != id_to_key_.end() && it->second == key) {
      id_to_key_.erase(it);
    }
  }
  auto endpoint_it = endpoint_to_key_.find(endpointKey(listing.connect_endpoint));
  if (endpoint_it != endpoint_to_key_.end() && endpoint_it->second == key) {
    endpoint_to_key_.erase(endpoint_it);
  }
}

LanDiscoveryEncodeResult encodeLanDiscoveryQuery(uint32_t app_id, uint64_t nonce) {
  BinaryWriter writer;
  writeDiscoveryHeader(writer, LanDiscoveryMessageType::Query, app_id, nonce);
  return finishDiscoveryPacket(writer);
}

LanDiscoveryEncodeResult encodeLanDiscoveryAdvertisement(const ServerListing& listing,
                                                         uint64_t nonce) {
  if (listing.app_id == 0 || listing.game_port == 0) {
    return {.status = LanDiscoveryStatus::InvalidConfig};
  }
  if (listing.attributes.size() > std::numeric_limits<uint16_t>::max()) {
    return {.status = LanDiscoveryStatus::OversizedPacket};
  }

  BinaryWriter writer;
  writeDiscoveryHeader(writer, LanDiscoveryMessageType::Advertisement, listing.app_id, nonce);
  writer.writeUInt16(listing.game_port);
  writer.writeUInt16(listing.protocol_version);
  writer.writeUInt16(listing.current_players);
  writer.writeUInt16(listing.max_players);
  writer.writeUInt32(millisecondsToWire(listing.ttl));
  writer.writeString(listing.connect_endpoint.ip);
  writer.writeString(listing.server_id);
  writer.writeString(listing.name);
  writer.writeString(listing.map);
  writer.writeString(listing.mode);
  writer.writeUInt16(static_cast<uint16_t>(listing.attributes.size()));
  for (const auto& [key, value] : listing.attributes) {
    writer.writeString(key);
    writer.writeString(value);
  }
  return finishDiscoveryPacket(writer);
}

LanDiscoveryDecodeResult decodeLanDiscoveryPacket(std::span<const std::byte> bytes,
                                                  uint32_t expected_app_id,
                                                  uint16_t expected_version) {
  if (bytes.size() > kLanDiscoveryMaxDatagramSize) {
    return {.status = LanDiscoveryDecodeStatus::OversizedPacket};
  }
  if (bytes.size() < kLanDiscoveryHeaderSize) {
    return {.status = LanDiscoveryDecodeStatus::TooSmall};
  }

  BinaryReader reader(bytes);
  LanDiscoveryPacket packet;
  LanDiscoveryDecodeStatus status = LanDiscoveryDecodeStatus::Malformed;
  if (!readDiscoveryHeader(reader, packet, expected_version, status)) {
    return {.status = status};
  }
  if (expected_app_id != 0 && packet.app_id != expected_app_id) {
    return {.status = LanDiscoveryDecodeStatus::AppIdMismatch};
  }

  if (packet.message_type == LanDiscoveryMessageType::Query) {
    if (!reader.exhausted()) {
      return {.status = LanDiscoveryDecodeStatus::Malformed};
    }
    return {
        .status = LanDiscoveryDecodeStatus::Ok,
        .packet = packet,
    };
  }

  ServerListing listing;
  listing.source = ServerListSource::Lan;
  listing.app_id = packet.app_id;
  uint32_t ttl_ms = 0;
  uint16_t attribute_count = 0;
  if (!reader.readUInt16(listing.game_port) ||
      !reader.readUInt16(listing.protocol_version) ||
      !reader.readUInt16(listing.current_players) ||
      !reader.readUInt16(listing.max_players) ||
      !reader.readUInt32(ttl_ms) ||
      !reader.readString(listing.connect_endpoint.ip) ||
      !reader.readString(listing.server_id) ||
      !reader.readString(listing.name) ||
      !reader.readString(listing.map) ||
      !reader.readString(listing.mode) ||
      !reader.readUInt16(attribute_count)) {
    return {.status = LanDiscoveryDecodeStatus::Malformed};
  }
  listing.connect_endpoint.port = listing.game_port;
  listing.ttl = std::chrono::milliseconds(ttl_ms);

  for (uint16_t i = 0; i < attribute_count; ++i) {
    std::string key;
    std::string value;
    if (!reader.readString(key) || !reader.readString(value)) {
      return {.status = LanDiscoveryDecodeStatus::Malformed};
    }
    listing.attributes[std::move(key)] = std::move(value);
  }
  if (!reader.exhausted()) {
    return {.status = LanDiscoveryDecodeStatus::Malformed};
  }

  packet.listing = std::move(listing);
  return {
      .status = LanDiscoveryDecodeStatus::Ok,
      .packet = std::move(packet),
  };
}

std::unique_ptr<ILanDatagramSocket> createLanDatagramSocket() {
  return std::make_unique<NativeLanDatagramSocket>();
}

LanServerAdvertiser::LanServerAdvertiser(LanDiscoveryConfig config)
    : LanServerAdvertiser(std::move(config), createLanDatagramSocket()) {}

LanServerAdvertiser::LanServerAdvertiser(LanDiscoveryConfig config,
                                         std::unique_ptr<ILanDatagramSocket> socket)
    : config_(std::move(config)),
      socket_(std::move(socket)),
      listing_(normalizedLanListing(config_, config_.listing)) {
  if (!socket_) {
    socket_ = createLanDatagramSocket();
  }
}

LanServerAdvertiser::~LanServerAdvertiser() {
  stop();
}

LanDiscoveryResult LanServerAdvertiser::start() {
  if (config_.discovery_port == 0 || config_.game_port == 0 || config_.app_id == 0) {
    return {.status = LanDiscoveryStatus::InvalidConfig};
  }
  if (!socket_) {
    socket_ = createLanDatagramSocket();
  }
  const LanDiscoveryResult opened = socket_->open(config_.discovery_port, true);
  if (!opened.ok()) {
    return opened;
  }
  running_ = true;
  next_beacon_.reset();
  normalizeListing();
  return {.status = LanDiscoveryStatus::Ok};
}

void LanServerAdvertiser::stop() {
  if (socket_) {
    socket_->close();
  }
  running_ = false;
  next_beacon_.reset();
}

void LanServerAdvertiser::updateListing(ServerListing listing) {
  config_.listing = std::move(listing);
  normalizeListing();
}

LanDiscoveryResult LanServerAdvertiser::advertiseNow(uint64_t nonce) {
  if (!running_) {
    return {.status = LanDiscoveryStatus::NotOpen};
  }
  return sendAdvertisement(broadcastEndpoint(config_.discovery_port), nonce);
}

LanDiscoveryResult LanServerAdvertiser::poll(std::chrono::steady_clock::time_point now) {
  if (!running_) {
    return {.status = LanDiscoveryStatus::NotOpen};
  }

  for (int i = 0; i < 64; ++i) {
    Endpoint sender;
    std::vector<std::byte> payload;
    const LanDiscoveryResult received = socket_->receive(sender, payload);
    if (received.wouldBlock()) {
      break;
    }
    if (!received.ok()) {
      return received;
    }

    const LanDiscoveryDecodeResult decoded = decodeLanDiscoveryPacket(payload, config_.app_id);
    if (!decoded.ok() ||
        decoded.packet.message_type != LanDiscoveryMessageType::Query) {
      continue;
    }
    const LanDiscoveryResult sent = sendAdvertisement(sender, decoded.packet.nonce);
    if (!sent.ok()) {
      return sent;
    }
  }

  if (!next_beacon_ || now >= *next_beacon_) {
    const LanDiscoveryResult sent = advertiseNow();
    if (!sent.ok()) {
      return sent;
    }
    next_beacon_ = now + config_.beacon_interval;
  }
  return {.status = LanDiscoveryStatus::Ok};
}

void LanServerAdvertiser::normalizeListing() {
  listing_ = normalizedLanListing(config_, config_.listing);
}

LanDiscoveryResult LanServerAdvertiser::sendAdvertisement(const Endpoint& endpoint,
                                                          uint64_t nonce) {
  normalizeListing();
  LanDiscoveryEncodeResult encoded = encodeLanDiscoveryAdvertisement(listing_, nonce);
  if (!encoded.ok()) {
    return {.status = encoded.status};
  }
  return socket_->sendTo(endpoint, encoded.bytes);
}

LanServerBrowser::LanServerBrowser(LanDiscoveryConfig config)
    : LanServerBrowser(std::move(config), createLanDatagramSocket()) {}

LanServerBrowser::LanServerBrowser(LanDiscoveryConfig config,
                                   std::unique_ptr<ILanDatagramSocket> socket)
    : config_(std::move(config)),
      socket_(std::move(socket)),
      cache_(&owned_cache_) {
  if (!socket_) {
    socket_ = createLanDatagramSocket();
  }
}

LanServerBrowser::LanServerBrowser(LanDiscoveryConfig config,
                                   ServerListCache& cache,
                                   std::unique_ptr<ILanDatagramSocket> socket)
    : config_(std::move(config)),
      socket_(std::move(socket)),
      cache_(&cache) {
  if (!socket_) {
    socket_ = createLanDatagramSocket();
  }
}

LanServerBrowser::~LanServerBrowser() {
  stop();
}

LanDiscoveryResult LanServerBrowser::start() {
  if (config_.discovery_port == 0 || config_.app_id == 0) {
    return {.status = LanDiscoveryStatus::InvalidConfig};
  }
  if (!socket_) {
    socket_ = createLanDatagramSocket();
  }
  const LanDiscoveryResult opened = socket_->open(config_.discovery_port, true);
  if (!opened.ok()) {
    return opened;
  }
  running_ = true;
  return {.status = LanDiscoveryStatus::Ok};
}

void LanServerBrowser::stop() {
  if (socket_) {
    socket_->close();
  }
  running_ = false;
}

LanDiscoveryResult LanServerBrowser::sendQuery(uint64_t nonce) {
  if (!running_) {
    return {.status = LanDiscoveryStatus::NotOpen};
  }
  if (nonce == 0) {
    nonce = next_nonce_++;
  }
  LanDiscoveryEncodeResult encoded = encodeLanDiscoveryQuery(config_.app_id, nonce);
  if (!encoded.ok()) {
    return {.status = encoded.status};
  }
  return socket_->sendTo(broadcastEndpoint(config_.discovery_port), encoded.bytes);
}

LanDiscoveryResult LanServerBrowser::poll(std::chrono::steady_clock::time_point now,
                                          std::vector<ServerListEvent>& out_events) {
  if (!running_) {
    return {.status = LanDiscoveryStatus::NotOpen};
  }

  for (int i = 0; i < 64; ++i) {
    Endpoint sender;
    std::vector<std::byte> payload;
    const LanDiscoveryResult received = socket_->receive(sender, payload);
    if (received.wouldBlock()) {
      break;
    }
    if (!received.ok()) {
      return received;
    }
    const LanDiscoveryDecodeResult decoded = decodeLanDiscoveryPacket(payload, config_.app_id);
    if (!decoded.ok() ||
        decoded.packet.message_type != LanDiscoveryMessageType::Advertisement) {
      continue;
    }
    handleAdvertisement(decoded.packet, sender, now, out_events);
  }

  for (ServerListing& expired : cache_->expire(now, ServerListSource::Lan)) {
    out_events.push_back(ServerListEvent{
        .type = ServerListEventType::Expired,
        .listing = std::move(expired),
    });
  }
  return {.status = LanDiscoveryStatus::Ok};
}

void LanServerBrowser::handleAdvertisement(const LanDiscoveryPacket& packet,
                                           const Endpoint& sender,
                                           std::chrono::steady_clock::time_point now,
                                           std::vector<ServerListEvent>& out_events) {
  ServerListing listing = packet.listing;
  listing.source = ServerListSource::Lan;
  if (listing.connect_endpoint.ip.empty()) {
    listing.connect_endpoint.ip = sender.ip;
  }
  if (listing.connect_endpoint.port == 0) {
    listing.connect_endpoint.port = listing.game_port;
  }
  if (listing.server_id.empty()) {
    listing.server_id = generatedServerId(listing);
  }
  if (listing.ttl.count() <= 0) {
    listing.ttl = config_.entry_ttl;
  }
  const std::string server_id = listing.server_id;
  const Endpoint endpoint = listing.connect_endpoint;

  const ServerListEventType event_type =
      cache_->upsert(std::move(listing),
                     ServerListSource::Lan,
                     now,
                     packet.listing.ttl.count() > 0 ? packet.listing.ttl : config_.entry_ttl);
  auto stored = cache_->findByServerId(server_id);
  if (!stored) {
    stored = cache_->findByEndpoint(endpoint);
  }
  if (!stored) {
    return;
  }
  out_events.push_back(ServerListEvent{
      .type = event_type,
      .listing = *stored,
  });
}

ServerDirectory::ServerDirectory() = default;

ServerDirectory::ServerDirectory(ServerListCache cache)
    : cache_(std::move(cache)) {}

ServerDirectory::~ServerDirectory() = default;

LanServerBrowser& ServerDirectory::enableLanDiscovery(LanDiscoveryConfig config) {
  return enableLanDiscovery(std::move(config), createLanDatagramSocket());
}

LanServerBrowser& ServerDirectory::enableLanDiscovery(
    LanDiscoveryConfig config,
    std::unique_ptr<ILanDatagramSocket> socket) {
  lan_browser_ =
      std::make_unique<LanServerBrowser>(std::move(config), cache_, std::move(socket));
  return *lan_browser_;
}

void ServerDirectory::disableLanDiscovery() {
  lan_browser_.reset();
}

void ServerDirectory::setMasterClient(std::unique_ptr<IMasterServerClient> client) {
  master_client_ = std::move(client);
}

bool ServerDirectory::publishToMaster(const ServerListing& listing) {
  return master_client_ && master_client_->publish(listing);
}

bool ServerDirectory::unpublishFromMaster(const std::string& server_id) {
  return master_client_ && master_client_->unpublish(server_id);
}

bool ServerDirectory::requestMasterList(const MasterServerQuery& query) {
  return master_client_ && master_client_->requestList(query);
}

void ServerDirectory::poll(std::chrono::steady_clock::time_point now,
                           std::vector<ServerListEvent>& out_events) {
  if (lan_browser_ && lan_browser_->isRunning()) {
    lan_browser_->poll(now, out_events);
  }

  if (!master_client_) {
    return;
  }
  master_events_.clear();
  master_client_->poll(master_events_);
  for (MasterServerEvent& event : master_events_) {
    switch (event.type) {
      case MasterServerEventType::Listing: {
        ServerListing listing = event.listing;
        const std::string server_id = listing.server_id;
        const Endpoint endpoint = listing.connect_endpoint;
        const std::chrono::milliseconds ttl = listing.ttl;
        listing.source = ServerListSource::Master;
        const ServerListEventType type =
            cache_.upsert(std::move(listing),
                          ServerListSource::Master,
                          now,
                          ttl);
        std::optional<ServerListing> stored;
        if (!server_id.empty()) {
          stored = cache_.findByServerId(server_id);
        }
        if (!stored && endpoint.port != 0) {
          stored = cache_.findByEndpoint(endpoint);
        }
        if (stored) {
          out_events.push_back(ServerListEvent{
              .type = type,
              .listing = *stored,
          });
        }
        break;
      }
      case MasterServerEventType::Removed: {
        ServerListing removed;
        if (cache_.removeByServerId(event.server_id, &removed)) {
          out_events.push_back(ServerListEvent{
              .type = ServerListEventType::Removed,
              .listing = std::move(removed),
          });
        }
        break;
      }
      case MasterServerEventType::Published:
      case MasterServerEventType::Unpublished:
      case MasterServerEventType::Error:
        break;
    }
  }
}

}  // namespace karma::network
