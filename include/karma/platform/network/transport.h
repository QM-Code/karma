#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace karma::net {

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

}  // namespace karma::net
