#pragma once

#include <deque>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/network.h"

namespace karma::examples::network_demo {

struct HttpMasterRequest {
  std::string method;
  std::string path;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
};

struct HttpMasterResponse {
  int status = 0;
  std::string body;
};

class IHttpMasterTransport {
 public:
  virtual ~IHttpMasterTransport() = default;
  virtual bool send(const HttpMasterRequest& request,
                    HttpMasterResponse& response) = 0;
};

class HttpMasterServerClient final : public network::IMasterServerClient {
 public:
  explicit HttpMasterServerClient(std::unique_ptr<IHttpMasterTransport> transport,
                                  std::string bearer_token = {})
      : transport_(std::move(transport)), bearer_token_(std::move(bearer_token)) {}

  bool publish(const network::ServerListing& listing) override {
    nlohmann::json body;
    body["server"] = toJson(listing);
    HttpMasterResponse response;
    const bool ok = sendJson("POST", "/servers", body, response);
    events_.push_back(network::MasterServerEvent{
        .type = ok ? network::MasterServerEventType::Published
                   : network::MasterServerEventType::Error,
        .listing = listing,
        .server_id = listing.server_id,
        .error = ok ? std::string{} : "publish failed",
    });
    return ok;
  }

  bool unpublish(const std::string& server_id) override {
    HttpMasterResponse response;
    const bool ok = sendJson("DELETE", "/servers/" + server_id, {}, response);
    events_.push_back(network::MasterServerEvent{
        .type = ok ? network::MasterServerEventType::Unpublished
                   : network::MasterServerEventType::Error,
        .server_id = server_id,
        .error = ok ? std::string{} : "unpublish failed",
    });
    return ok;
  }

  bool requestList(const network::MasterServerQuery& query) override {
    nlohmann::json body;
    body["filters"] = query.filters;
    body["attributes"] = query.attributes;

    HttpMasterResponse response;
    const bool ok = sendJson("POST", "/servers/query", body, response);
    if (!ok) {
      events_.push_back(network::MasterServerEvent{
          .type = network::MasterServerEventType::Error,
          .error = "request list failed",
      });
      return false;
    }

    try {
      const nlohmann::json parsed = nlohmann::json::parse(response.body.empty()
                                                             ? "{}"
                                                             : response.body);
      const auto servers_it = parsed.find("servers");
      if (servers_it != parsed.end() && servers_it->is_array()) {
        for (const nlohmann::json& item : *servers_it) {
          events_.push_back(network::MasterServerEvent{
              .type = network::MasterServerEventType::Listing,
              .listing = fromJson(item),
              .attributes = query.attributes,
          });
        }
      }
    } catch (const nlohmann::json::exception& error) {
      events_.push_back(network::MasterServerEvent{
          .type = network::MasterServerEventType::Error,
          .error = error.what(),
      });
      return false;
    }
    return true;
  }

  void poll(std::vector<network::MasterServerEvent>& out_events) override {
    out_events.insert(out_events.end(),
                      std::make_move_iterator(events_.begin()),
                      std::make_move_iterator(events_.end()));
    events_.clear();
  }

 private:
  bool sendJson(const std::string& method,
                const std::string& path,
                const nlohmann::json& body,
                HttpMasterResponse& response) {
    if (!transport_) {
      return false;
    }
    HttpMasterRequest request{
        .method = method,
        .path = path,
        .body = body.is_null() ? std::string{} : body.dump(),
        .headers = {{"content-type", "application/json"}},
    };
    if (!bearer_token_.empty()) {
      request.headers["authorization"] = "Bearer " + bearer_token_;
    }
    if (!transport_->send(request, response)) {
      return false;
    }
    return response.status >= 200 && response.status < 300;
  }

  static nlohmann::json toJson(const network::Endpoint& endpoint) {
    return nlohmann::json{
        {"ip", endpoint.ip},
        {"port", endpoint.port},
    };
  }

  static nlohmann::json toJson(const network::ServerListing& listing) {
    return nlohmann::json{
        {"server_id", listing.server_id},
        {"connect_endpoint", toJson(listing.connect_endpoint)},
        {"game_port", listing.game_port},
        {"app_id", listing.app_id},
        {"protocol_version", listing.protocol_version},
        {"name", listing.name},
        {"map", listing.map},
        {"mode", listing.mode},
        {"current_players", listing.current_players},
        {"max_players", listing.max_players},
        {"attributes", listing.attributes},
    };
  }

  static network::Endpoint endpointFromJson(const nlohmann::json& json) {
    return network::Endpoint{
        .ip = json.value("ip", std::string{}),
        .port = json.value("port", static_cast<uint16_t>(0)),
    };
  }

  static network::ServerListing fromJson(const nlohmann::json& json) {
    network::ServerListing listing{
        .server_id = json.value("server_id", std::string{}),
        .game_port = json.value("game_port", static_cast<uint16_t>(0)),
        .app_id = json.value("app_id", static_cast<uint32_t>(0)),
        .protocol_version = json.value("protocol_version", network::kProtocolVersion),
        .name = json.value("name", std::string{}),
        .map = json.value("map", std::string{}),
        .mode = json.value("mode", std::string{}),
        .current_players = json.value("current_players", static_cast<uint16_t>(0)),
        .max_players = json.value("max_players", static_cast<uint16_t>(0)),
        .source = network::ServerListSource::Master,
    };
    if (const auto endpoint = json.find("connect_endpoint");
        endpoint != json.end() && endpoint->is_object()) {
      listing.connect_endpoint = endpointFromJson(*endpoint);
    }
    if (const auto attributes = json.find("attributes");
        attributes != json.end() && attributes->is_object()) {
      listing.attributes = attributes->get<std::unordered_map<std::string, std::string>>();
    }
    return listing;
  }

  std::unique_ptr<IHttpMasterTransport> transport_;
  std::string bearer_token_;
  std::vector<network::MasterServerEvent> events_;
};

}  // namespace karma::examples::network_demo
