#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace karma::net {

using ConnectionHandle = std::uintptr_t;

enum class Delivery {
  Reliable,
  Unreliable
};

struct Event {
  enum class Type {
    Receive,
    Connect,
    Disconnect,
    DisconnectTimeout
  };

  Type type{};
  ConnectionHandle connection = 0;
  std::vector<std::byte> payload;
  std::string peer_ip;
  uint16_t peer_port = 0;
};

class IClientTransport {
 public:
  virtual ~IClientTransport() = default;

  virtual bool connect(const std::string& host, uint16_t port, int timeout_ms) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  virtual void poll(std::vector<Event>& out_events) = 0;

  virtual void send(const std::byte* data, std::size_t size, Delivery delivery,
                    bool flush) = 0;

  virtual std::optional<std::string> getRemoteIp() const = 0;
  virtual std::optional<uint16_t> getRemotePort() const = 0;
};

class IServerTransport {
 public:
  virtual ~IServerTransport() = default;

  virtual void poll(std::vector<Event>& out_events) = 0;

  virtual void send(ConnectionHandle connection, const std::byte* data, std::size_t size,
                    Delivery delivery, bool flush) = 0;
  virtual void disconnect(ConnectionHandle connection) = 0;
};

}  // namespace karma::net
