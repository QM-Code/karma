#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "karma/karma.h"
#include "karma/ui.h"

#include "shared.h"

namespace demo = karma::examples::network_demo;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kTextSmall = 64;
constexpr std::size_t kTextMedium = 128;
constexpr std::size_t kTextLarge = 256;

uint16_t clampPort(int value) {
  return static_cast<uint16_t>(std::clamp(value, 1, 65535));
}

const char* sourceName(karma::network::ServerListSource source) {
  switch (source) {
    case karma::network::ServerListSource::Lan:
      return "LAN";
    case karma::network::ServerListSource::Master:
      return "Master";
  }
  return "Unknown";
}

const char* cacheEventName(karma::network::ServerListEventType type) {
  switch (type) {
    case karma::network::ServerListEventType::Found:
      return "found";
    case karma::network::ServerListEventType::Updated:
      return "updated";
    case karma::network::ServerListEventType::Removed:
      return "removed";
    case karma::network::ServerListEventType::Expired:
      return "expired";
  }
  return "event";
}

const char* discoveryStatusName(karma::network::LanDiscoveryStatus status) {
  switch (status) {
    case karma::network::LanDiscoveryStatus::Ok:
      return "ok";
    case karma::network::LanDiscoveryStatus::NotOpen:
      return "not open";
    case karma::network::LanDiscoveryStatus::InvalidConfig:
      return "invalid config";
    case karma::network::LanDiscoveryStatus::BindFailed:
      return "bind failed";
    case karma::network::LanDiscoveryStatus::WouldBlock:
      return "would block";
    case karma::network::LanDiscoveryStatus::EncodeFailed:
      return "encode failed";
    case karma::network::LanDiscoveryStatus::OversizedPacket:
      return "oversized packet";
    case karma::network::LanDiscoveryStatus::BackendError:
      return "backend error";
  }
  return "unknown";
}

const char* connectStatusName(karma::network::ConnectStatus status) {
  switch (status) {
    case karma::network::ConnectStatus::Connected:
      return "connected";
    case karma::network::ConnectStatus::HostCreateFailed:
      return "host create failed";
    case karma::network::ConnectStatus::ResolveFailed:
      return "resolve failed";
    case karma::network::ConnectStatus::NoAvailablePeer:
      return "no peer";
    case karma::network::ConnectStatus::Timeout:
      return "timeout";
    case karma::network::ConnectStatus::BackendError:
      return "backend error";
  }
  return "unknown";
}

std::string endpointText(const karma::network::Endpoint& endpoint) {
  return endpoint.ip + ":" + std::to_string(endpoint.port);
}

std::string listingKey(const karma::network::ServerListing& listing) {
  if (!listing.server_id.empty()) {
    return listing.server_id;
  }
  return endpointText(listing.connect_endpoint);
}

void copyText(char* out, std::size_t size, const std::string& value) {
  if (size == 0) {
    return;
  }
  std::snprintf(out, size, "%s", value.c_str());
}

bool textNotEmpty(const char* text) {
  return text && text[0] != '\0';
}

struct MasterCatalogEntry {
  karma::network::ServerListing listing;
};

class DemoMasterServerClient final : public karma::network::IMasterServerClient {
 public:
  bool publish(const karma::network::ServerListing& listing) override {
    upsertCatalog(listing);
    events_.push_back(karma::network::MasterServerEvent{
        .type = karma::network::MasterServerEventType::Listing,
        .listing = listing,
        .server_id = listing.server_id,
        .attributes = {{"operation", "publish"}},
    });
    published_count_ += 1;
    return true;
  }

  bool unpublish(const std::string& server_id) override {
    eraseCatalog(server_id);
    events_.push_back(karma::network::MasterServerEvent{
        .type = karma::network::MasterServerEventType::Removed,
        .server_id = server_id,
        .attributes = {{"operation", "unpublish"}},
    });
    unpublished_count_ += 1;
    return true;
  }

  bool requestList(const karma::network::MasterServerQuery& query) override {
    last_query_ = query;
    requested_count_ += 1;
    for (const MasterCatalogEntry& entry : catalog_) {
      if (!matches(entry.listing, query)) {
        continue;
      }
      events_.push_back(karma::network::MasterServerEvent{
          .type = karma::network::MasterServerEventType::Listing,
          .listing = entry.listing,
          .server_id = entry.listing.server_id,
          .attributes = query.attributes,
      });
    }
    return true;
  }

  void poll(std::vector<karma::network::MasterServerEvent>& out_events) override {
    out_events.insert(out_events.end(),
                      std::make_move_iterator(events_.begin()),
                      std::make_move_iterator(events_.end()));
    events_.clear();
  }

  void addCatalogListing(karma::network::ServerListing listing) {
    upsertCatalog(std::move(listing));
  }

  void eraseCatalog(const std::string& server_id) {
    catalog_.erase(std::remove_if(catalog_.begin(),
                                  catalog_.end(),
                                  [&](const MasterCatalogEntry& entry) {
                                    return entry.listing.server_id == server_id;
                                  }),
                   catalog_.end());
  }

  const std::vector<MasterCatalogEntry>& catalog() const { return catalog_; }
  const karma::network::MasterServerQuery& lastQuery() const { return last_query_; }
  int publishedCount() const { return published_count_; }
  int unpublishedCount() const { return unpublished_count_; }
  int requestedCount() const { return requested_count_; }

 private:
  void upsertCatalog(karma::network::ServerListing listing) {
    listing.source = karma::network::ServerListSource::Master;
    auto it = std::find_if(catalog_.begin(),
                           catalog_.end(),
                           [&](const MasterCatalogEntry& entry) {
                             return entry.listing.server_id == listing.server_id;
                           });
    if (it == catalog_.end()) {
      catalog_.push_back(MasterCatalogEntry{.listing = std::move(listing)});
    } else {
      it->listing = std::move(listing);
    }
  }

  static bool matchesAttribute(const karma::network::ServerListing& listing,
                               const std::string& key,
                               const std::string& value) {
    if (key == "name") {
      return listing.name == value;
    }
    if (key == "map") {
      return listing.map == value;
    }
    if (key == "mode") {
      return listing.mode == value;
    }
    auto it = listing.attributes.find(key);
    return it != listing.attributes.end() && it->second == value;
  }

  static bool matches(const karma::network::ServerListing& listing,
                      const karma::network::MasterServerQuery& query) {
    for (const auto& [key, value] : query.filters) {
      if (!matchesAttribute(listing, key, value)) {
        return false;
      }
    }
    return true;
  }

  std::vector<MasterCatalogEntry> catalog_;
  std::vector<karma::network::MasterServerEvent> events_;
  karma::network::MasterServerQuery last_query_;
  int published_count_ = 0;
  int unpublished_count_ = 0;
  int requested_count_ = 0;
};

class ClientProbe {
 public:
  void connect(const karma::network::ServerListing& listing,
               uint32_t app_id,
               std::deque<std::string>& log) {
    disconnect(log);
    auto transport = karma::network::createDefaultClientTransport();
    if (!transport) {
      status_ = "no default transport";
      log.push_front("client: no default transport");
      return;
    }

    session_ = std::make_unique<karma::network::ClientSession>(
        std::move(transport),
        app_id,
        "directory-ui");
    const auto result = session_->connect(listing.connect_endpoint.ip,
                                          listing.connect_endpoint.port,
                                          250);
    status_ = connectStatusName(result.status);
    log.push_front("client: " + status_ + " " + endpointText(listing.connect_endpoint));
    if (!result.connected()) {
      session_.reset();
    }
  }

  void disconnect(std::deque<std::string>& log) {
    if (session_) {
      session_->disconnect();
      log.push_front("client: disconnected");
      session_.reset();
    }
    status_ = "idle";
  }

  void ping(std::deque<std::string>& log) {
    if (!session_ || !session_->isConnected()) {
      log.push_front("client: ping skipped");
      return;
    }
    const auto sent = session_->send(karma::network::MessageType::Ping,
                                     std::span<const std::byte>{},
                                     karma::network::Delivery::Unreliable,
                                     0);
    log.push_front(sent.ok() ? "client: ping sent" : "client: ping failed");
  }

  void poll(std::deque<std::string>& log) {
    if (!session_) {
      return;
    }
    events_.clear();
    session_->poll(events_);
    for (const auto& event : events_) {
      switch (event.type) {
        case karma::network::SessionEventType::PeerConnected:
          status_ = "session connected";
          log.push_front("client: session connected");
          break;
        case karma::network::SessionEventType::PeerDisconnected:
          status_ = "disconnected";
          log.push_front("client: session disconnected");
          break;
        case karma::network::SessionEventType::Pong:
          log.push_front("client: pong");
          break;
        case karma::network::SessionEventType::ProtocolError:
          status_ = "protocol error";
          log.push_front("client: protocol error");
          break;
        default:
          break;
      }
    }
  }

  const std::string& status() const { return status_; }
  bool active() const { return session_ != nullptr; }
  bool connected() const { return session_ && session_->isConnected(); }

 private:
  std::unique_ptr<karma::network::ClientSession> session_;
  std::vector<karma::network::SessionEvent> events_;
  std::string status_ = "idle";
};

class EmbeddedServer {
 public:
  bool start(uint16_t port, uint32_t app_id, std::deque<std::string>& log) {
    stop(log);
    auto transport = karma::network::createDefaultServerTransport(port, 8, 2);
    if (!transport) {
      status_ = "no transport";
      log.push_front("embedded server: no default transport");
      return false;
    }
    session_ = std::make_unique<karma::network::ServerSession>(std::move(transport), app_id);
    port_ = port;
    status_ = "listening";
    log.push_front("embedded server: listening on " + std::to_string(port));
    return true;
  }

  void stop(std::deque<std::string>& log) {
    if (session_) {
      for (const karma::network::PeerId peer : session_->peers()) {
        session_->disconnect(peer);
      }
      session_.reset();
      log.push_front("embedded server: stopped");
    }
    status_ = "stopped";
    port_ = 0;
  }

  void poll(std::deque<std::string>& log) {
    if (!session_) {
      return;
    }
    events_.clear();
    session_->poll(events_);
    for (const auto& event : events_) {
      switch (event.type) {
        case karma::network::SessionEventType::PeerConnected:
          log.push_front("embedded server: peer " +
                         std::to_string(event.peer.value) + " connected");
          break;
        case karma::network::SessionEventType::PeerDisconnected:
          log.push_front("embedded server: peer " +
                         std::to_string(event.peer.value) + " disconnected");
          break;
        case karma::network::SessionEventType::ProtocolError:
          log.push_front("embedded server: protocol error");
          break;
        default:
          break;
      }
    }
    session_->flush();
  }

  const std::string& status() const { return status_; }
  uint16_t port() const { return port_; }
  bool running() const { return session_ != nullptr; }
  std::size_t peerCount() const { return session_ ? session_->peers().size() : 0; }

 private:
  std::unique_ptr<karma::network::ServerSession> session_;
  std::vector<karma::network::SessionEvent> events_;
  std::string status_ = "stopped";
  uint16_t port_ = 0;
};

class DirectoryLab {
 public:
  DirectoryLab() {
    copyText(server_id_.data(), server_id_.size(), "karma-directory-local");
    copyText(name_.data(), name_.size(), "Karma Directory Local");
    copyText(map_.data(), map_.size(), "showcase");
    copyText(mode_.data(), mode_.size(), "coop");
    copyText(attribute_key_.data(), attribute_key_.size(), "build");
    copyText(attribute_value_.data(), attribute_value_.size(), "graphical");
    copyText(master_filter_key_.data(), master_filter_key_.size(), "mode");
    copyText(master_filter_value_.data(), master_filter_value_.size(), "coop");

    auto master = std::make_unique<DemoMasterServerClient>();
    master_ = master.get();
    directory_.setMasterClient(std::move(master));
    seedMasterCatalog();
    startEmbeddedServer();
    startAdvertiser();
    startBrowser();
    requestMasterList();
  }

  void update(float dt) {
    elapsed_ += dt;
    const auto now = Clock::now();
    trimLog();

    if (advertiser_ && advertiser_->isRunning()) {
      advertiser_->updateListing(makeLocalListing());
      const auto result = advertiser_->poll(now);
      if (!result.ok() && result.status != last_advertiser_error_) {
        last_advertiser_error_ = result.status;
        addLog(std::string("advertiser: ") + discoveryStatusName(result.status));
      }
    }

    auto* browser = directory_.lanBrowser();
    if (auto_query_ && browser && browser->isRunning() && now >= next_auto_query_) {
      const auto result = browser->sendQuery();
      if (!result.ok()) {
        addLog(std::string("browser query: ") + discoveryStatusName(result.status));
      }
      next_auto_query_ = now + std::chrono::milliseconds(1500);
    }

    events_.clear();
    directory_.poll(now, events_);
    for (const auto& event : events_) {
      addLog(std::string(sourceName(event.listing.source)) + ": " +
             cacheEventName(event.type) + " " + listingLabel(event.listing));
    }

    client_.poll(log_);
    embedded_server_.poll(log_);
    trimLog();
  }

  void draw(karma::app::UIContext& ctx) {
    const auto frame = ctx.frame();
    ImGui::SetNextWindowSize(ImVec2(1180.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Karma Server Directory");

    ImGui::Text("App %08X", app_id_);
    ImGui::SameLine();
    ImGui::Text("Viewport %dx%d", frame.viewport_w, frame.viewport_h);
    ImGui::SameLine();
    ImGui::Text("Client %s", client_.status().c_str());
    ImGui::SameLine();
    ImGui::Text("Server %s", embedded_server_.status().c_str());

    if (ImGui::BeginTabBar("directory_tabs")) {
      if (ImGui::BeginTabItem("Runtime")) {
        drawRuntimeTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("LAN")) {
        drawLanTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Master")) {
        drawMasterTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Cache")) {
        drawCacheTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Events")) {
        drawEventsTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  std::vector<karma::network::ServerListing> listings() const {
    return cacheListings();
  }

 private:
  karma::network::ServerListSort selectedSort() const {
    switch (sort_index_) {
      case 0:
        return karma::network::ServerListSort::Name;
      case 1:
        return karma::network::ServerListSort::PlayerCount;
      case 2:
        return karma::network::ServerListSort::Source;
      case 3:
        return karma::network::ServerListSort::LastSeen;
      case 4:
        return karma::network::ServerListSort::Endpoint;
      case 5:
        return karma::network::ServerListSort::Capacity;
      case 6:
        return karma::network::ServerListSort::ServerId;
      default:
        return karma::network::ServerListSort::Name;
    }
  }

  karma::network::ServerListQuery makeCacheQuery() const {
    karma::network::ServerListQuery query{
        .app_id = app_id_,
        .text = cache_search_.data(),
        .pinned_server_ids = pinned_server_ids_,
        .sort = selectedSort(),
        .descending = sort_descending_,
    };
    if (show_lan_ && !show_master_) {
      query.source = karma::network::ServerListSource::Lan;
    } else if (show_master_ && !show_lan_) {
      query.source = karma::network::ServerListSource::Master;
    }
    query.hide_full = hide_full_;
    return query;
  }

  std::vector<karma::network::ServerListing> cacheListings() const {
    if (!show_lan_ && !show_master_) {
      return {};
    }
    return directory_.cache().list(makeCacheQuery());
  }

  void toggleSelectedPin() {
    if (selected_key_.empty()) {
      addLog("cache: no selected server to pin");
      return;
    }
    auto it = std::find(pinned_server_ids_.begin(),
                        pinned_server_ids_.end(),
                        selected_key_);
    if (it == pinned_server_ids_.end()) {
      pinned_server_ids_.insert(pinned_server_ids_.begin(), selected_key_);
      addLog("cache: pinned " + selected_key_);
    } else {
      pinned_server_ids_.erase(it);
      addLog("cache: unpinned " + selected_key_);
    }
  }

  bool selectedPinned() const {
    return std::find(pinned_server_ids_.begin(),
                     pinned_server_ids_.end(),
                     selected_key_) != pinned_server_ids_.end();
  }

  karma::network::LanDiscoveryConfig makeConfig() const {
    return karma::network::LanDiscoveryConfig{
        .discovery_port = clampPort(discovery_port_),
        .app_id = app_id_,
        .game_port = clampPort(game_port_),
        .listing = makeLocalListing(),
        .beacon_interval = std::chrono::milliseconds(beacon_interval_ms_),
        .entry_ttl = std::chrono::milliseconds(entry_ttl_ms_),
    };
  }

  karma::network::ServerListing makeLocalListing() const {
    karma::network::ServerListing listing =
        karma::network::makeLanServerListing(app_id_,
                                             clampPort(game_port_),
                                             name_.data(),
                                             map_.data(),
                                             mode_.data(),
                                             server_id_.data());
    listing.connect_endpoint.ip = connect_ip_.data();
    listing.current_players =
        static_cast<uint16_t>(std::clamp(current_players_, 0, 65535));
    listing.max_players = static_cast<uint16_t>(std::clamp(max_players_, 0, 65535));
    listing.attributes = {{"example", "discovery_directory"}};
    listing.ttl = std::chrono::milliseconds(entry_ttl_ms_);
    if (textNotEmpty(attribute_key_.data())) {
      listing.attributes[attribute_key_.data()] = attribute_value_.data();
    }
    return listing;
  }

  karma::network::ServerListing makeFakeMasterListing() {
    const int index = ++master_listing_counter_;
    return karma::network::ServerListing{
        .server_id = "master-demo-" + std::to_string(index),
        .connect_endpoint = karma::network::Endpoint{
            .ip = "203.0.113." + std::to_string(10 + index),
            .port = static_cast<uint16_t>(27000 + index),
        },
        .game_port = static_cast<uint16_t>(27000 + index),
        .app_id = app_id_,
        .protocol_version = karma::network::kProtocolVersion,
        .name = "Master Demo " + std::to_string(index),
        .map = index % 2 == 0 ? "showcase" : "arena",
        .mode = index % 2 == 0 ? "coop" : "dm",
        .current_players = static_cast<uint16_t>(index % 6),
        .max_players = 12,
        .attributes = {
            {"region", index % 2 == 0 ? "local" : "remote"},
            {"playlist", index % 2 == 0 ? "casual" : "ranked"},
        },
        .source = karma::network::ServerListSource::Master,
        .ttl = std::chrono::milliseconds(30000),
    };
  }

  void seedMasterCatalog() {
    if (!master_) {
      return;
    }
    for (int i = 0; i < 3; ++i) {
      master_->addCatalogListing(makeFakeMasterListing());
    }
  }

  void startAdvertiser() {
    advertiser_ = std::make_unique<karma::network::LanServerAdvertiser>(makeConfig());
    const auto result = advertiser_->start();
    addLog(std::string("advertiser start: ") + discoveryStatusName(result.status));
    if (!result.ok()) {
      advertiser_.reset();
    }
  }

  void stopAdvertiser() {
    if (advertiser_) {
      advertiser_->stop();
      advertiser_.reset();
      addLog("advertiser stopped");
    }
  }

  void startBrowser() {
    auto& browser = directory_.enableLanDiscovery(makeConfig());
    const auto result = browser.start();
    addLog(std::string("browser start: ") + discoveryStatusName(result.status));
    if (!result.ok()) {
      directory_.disableLanDiscovery();
    } else {
      browser.sendQuery();
      next_auto_query_ = Clock::now() + std::chrono::milliseconds(1500);
    }
  }

  void stopBrowser() {
    directory_.disableLanDiscovery();
    addLog("browser stopped");
  }

  void restartLan() {
    stopAdvertiser();
    stopBrowser();
    startAdvertiser();
    startBrowser();
  }

  void startEmbeddedServer() {
    embedded_server_.start(clampPort(game_port_), app_id_, log_);
  }

  void stopEmbeddedServer() {
    embedded_server_.stop(log_);
  }

  void restartRuntime() {
    client_.disconnect(log_);
    stopEmbeddedServer();
    restartLan();
    startEmbeddedServer();
  }

  void requestMasterList() {
    karma::network::MasterServerQuery query;
    if (textNotEmpty(master_filter_key_.data()) && textNotEmpty(master_filter_value_.data())) {
      query.filters[master_filter_key_.data()] = master_filter_value_.data();
    }
    query.attributes["requester"] = "graphical_directory";
    if (directory_.requestMasterList(query)) {
      addLog("master request queued");
    }
  }

  void publishLocalToMaster() {
    if (directory_.publishToMaster(makeLocalListing())) {
      addLog("master publish queued");
    }
  }

  void unpublishSelectedFromMaster() {
    auto selected = selectedListing();
    if (!selected) {
      addLog("master unpublish skipped");
      return;
    }
    if (directory_.unpublishFromMaster(selected->server_id)) {
      addLog("master unpublish queued " + selected->server_id);
    }
  }

  std::optional<karma::network::ServerListing> selectedListing() const {
    for (const auto& listing : cacheListings()) {
      if (listingKey(listing) == selected_key_) {
        return listing;
      }
    }
    return std::nullopt;
  }

  static std::string listingLabel(const karma::network::ServerListing& listing) {
    if (!listing.name.empty()) {
      return listing.name + " (" + endpointText(listing.connect_endpoint) + ")";
    }
    return endpointText(listing.connect_endpoint);
  }

  void addLog(std::string message) {
    log_.push_front(std::move(message));
    trimLog();
  }

  void trimLog() {
    while (log_.size() > 120) {
      log_.pop_back();
    }
  }

  void drawRuntimeTab() {
    const bool advertiser_running = advertiser_ && advertiser_->isRunning();
    auto* browser = directory_.lanBrowser();
    const bool browser_running = browser && browser->isRunning();

    ImGui::Columns(3, "runtime_columns", false);
    ImGui::Text("Advertiser");
    ImGui::Text("%s", advertiser_running ? "running" : "stopped");
    if (advertiser_running) {
      if (ImGui::Button("Stop Advertiser")) {
        stopAdvertiser();
      }
    } else if (ImGui::Button("Start Advertiser")) {
      startAdvertiser();
    }
    ImGui::NextColumn();

    ImGui::Text("Browser");
    ImGui::Text("%s", browser_running ? "running" : "stopped");
    if (browser_running) {
      if (ImGui::Button("Query LAN")) {
        const auto result = browser->sendQuery();
        addLog(std::string("browser query: ") + discoveryStatusName(result.status));
      }
      ImGui::SameLine();
      if (ImGui::Button("Stop Browser")) {
        stopBrowser();
      }
    } else if (ImGui::Button("Start Browser")) {
      startBrowser();
    }
    ImGui::Checkbox("Auto Query", &auto_query_);
    ImGui::NextColumn();

    ImGui::Text("Client");
    ImGui::Text("%s", client_.status().c_str());
    if (ImGui::Button("Connect Selected")) {
      auto selected = selectedListing();
      if (selected) {
        client_.connect(*selected, app_id_, log_);
      } else {
        addLog("client: no selection");
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Ping")) {
      client_.ping(log_);
    }
    if (ImGui::Button("Disconnect")) {
      client_.disconnect(log_);
    }
    ImGui::Columns(1);

    ImGui::Separator();
    ImGui::Text("Embedded server: %s on %u",
                embedded_server_.status().c_str(),
                embedded_server_.port());
    ImGui::SameLine();
    ImGui::Text("Peers: %zu", embedded_server_.peerCount());
    if (embedded_server_.running()) {
      if (ImGui::Button("Stop Embedded Server")) {
        stopEmbeddedServer();
      }
    } else if (ImGui::Button("Start Embedded Server")) {
      startEmbeddedServer();
    }

    ImGui::Separator();
    ImGui::Text("Cache entries: %zu", directory_.cache().size());
    ImGui::SameLine();
    ImGui::Text("Elapsed: %.1fs", elapsed_);
    if (ImGui::Button("Publish Local")) {
      publishLocalToMaster();
    }
    ImGui::SameLine();
    if (ImGui::Button("Request Master")) {
      requestMasterList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart LAN")) {
      restartLan();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart Runtime")) {
      restartRuntime();
    }
  }

  void drawLanTab() {
    ImGui::InputInt("Game Port", &game_port_);
    ImGui::InputInt("Discovery Port", &discovery_port_);
    ImGui::InputInt("Beacon ms", &beacon_interval_ms_);
    ImGui::InputInt("Entry TTL ms", &entry_ttl_ms_);
    beacon_interval_ms_ = std::max(beacon_interval_ms_, 100);
    entry_ttl_ms_ = std::max(entry_ttl_ms_, 250);

    ImGui::Separator();
    ImGui::InputText("Server ID", server_id_.data(), server_id_.size());
    ImGui::InputText("Name", name_.data(), name_.size());
    ImGui::InputText("Map", map_.data(), map_.size());
    ImGui::InputText("Mode", mode_.data(), mode_.size());
    ImGui::InputText("Advertised IP", connect_ip_.data(), connect_ip_.size());
    ImGui::InputInt("Players", &current_players_);
    ImGui::InputInt("Capacity", &max_players_);
    current_players_ = std::clamp(current_players_, 0, 65535);
    max_players_ = std::clamp(max_players_, 0, 65535);
    ImGui::InputText("Attribute Key", attribute_key_.data(), attribute_key_.size());
    ImGui::InputText("Attribute Value", attribute_value_.data(), attribute_value_.size());

    if (ImGui::Button("Advertise Now")) {
      if (advertiser_ && advertiser_->isRunning()) {
        const auto result = advertiser_->advertiseNow();
        addLog(std::string("advertise now: ") + discoveryStatusName(result.status));
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply And Restart")) {
      restartRuntime();
    }
  }

  void drawMasterTab() {
    ImGui::InputText("Filter Key", master_filter_key_.data(), master_filter_key_.size());
    ImGui::InputText("Filter Value", master_filter_value_.data(), master_filter_value_.size());

    if (ImGui::Button("Request List")) {
      requestMasterList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Fake Listing")) {
      if (master_) {
        master_->addCatalogListing(makeFakeMasterListing());
        addLog("master catalog added");
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Publish Local")) {
      publishLocalToMaster();
    }
    ImGui::SameLine();
    if (ImGui::Button("Unpublish Selected")) {
      unpublishSelectedFromMaster();
    }

    if (!master_) {
      return;
    }
    ImGui::Separator();
    ImGui::Text("Published %d", master_->publishedCount());
    ImGui::SameLine();
    ImGui::Text("Unpublished %d", master_->unpublishedCount());
    ImGui::SameLine();
    ImGui::Text("Requested %d", master_->requestedCount());

    if (ImGui::BeginTable("master_catalog", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("ID");
      ImGui::TableSetupColumn("Endpoint");
      ImGui::TableSetupColumn("Map");
      ImGui::TableSetupColumn("Mode");
      ImGui::TableSetupColumn("Players");
      ImGui::TableHeadersRow();
      for (const auto& entry : master_->catalog()) {
        const auto& listing = entry.listing;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(listing.server_id.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(endpointText(listing.connect_endpoint).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(listing.map.c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(listing.mode.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%u/%u", listing.current_players, listing.max_players);
      }
      ImGui::EndTable();
    }
  }

  void drawCacheTab() {
    ImGui::InputText("Search", cache_search_.data(), cache_search_.size());
    ImGui::SameLine();
    ImGui::Checkbox("LAN", &show_lan_);
    ImGui::SameLine();
    ImGui::Checkbox("Master", &show_master_);
    ImGui::SameLine();
    ImGui::Checkbox("Hide Full", &hide_full_);
    ImGui::Combo("Sort",
                 &sort_index_,
                 "Name\0Players\0Source\0Last Seen\0Endpoint\0Capacity\0Server ID\0");
    ImGui::SameLine();
    ImGui::Checkbox("Descending", &sort_descending_);
    if (ImGui::Button(selectedPinned() ? "Unpin Selected" : "Pin Selected")) {
      toggleSelectedPin();
    }
    ImGui::SameLine();
    ImGui::Text("Pinned: %zu", pinned_server_ids_.size());

    const auto listings = cacheListings();
    if (ImGui::BeginTable("server_cache",
                          8,
                          ImGuiTableFlags_Borders |
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0.0f, 360.0f))) {
      ImGui::TableSetupColumn("Server");
      ImGui::TableSetupColumn("Source");
      ImGui::TableSetupColumn("Endpoint");
      ImGui::TableSetupColumn("Map");
      ImGui::TableSetupColumn("Mode");
      ImGui::TableSetupColumn("Players");
      ImGui::TableSetupColumn("TTL");
      ImGui::TableSetupColumn("Protocol");
      ImGui::TableHeadersRow();

      const auto now = Clock::now();
      for (const auto& listing : listings) {
        const std::string key = listingKey(listing);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const bool selected = key == selected_key_;
        if (ImGui::Selectable(listing.name.empty() ? key.c_str() : listing.name.c_str(),
                              selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
          selected_key_ = key;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(sourceName(listing.source));
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(endpointText(listing.connect_endpoint).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(listing.map.c_str());
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(listing.mode.c_str());
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%u/%u", listing.current_players, listing.max_players);
        ImGui::TableSetColumnIndex(6);
        if (listing.expires_at == Clock::time_point::max()) {
          ImGui::TextUnformatted("none");
        } else {
          const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
              listing.expires_at - now);
          ImGui::Text("%lld ms",
                      static_cast<long long>(std::max<int64_t>(remaining.count(), 0)));
        }
        ImGui::TableSetColumnIndex(7);
        ImGui::Text("%u", listing.protocol_version);
      }
      ImGui::EndTable();
    }

    const auto selected = selectedListing();
    if (!selected) {
      ImGui::TextUnformatted("No server selected");
      return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted(selected->server_id.c_str());
    ImGui::Text("Endpoint: %s", endpointText(selected->connect_endpoint).c_str());
    ImGui::Text("Name: %s", selected->name.c_str());
    ImGui::Text("Map: %s", selected->map.c_str());
    ImGui::Text("Mode: %s", selected->mode.c_str());
    ImGui::Text("Players: %u/%u", selected->current_players, selected->max_players);
    if (ImGui::TreeNode("Attributes")) {
      for (const auto& [key, value] : selected->attributes) {
        ImGui::Text("%s = %s", key.c_str(), value.c_str());
      }
      ImGui::TreePop();
    }
  }

  void drawEventsTab() {
    if (ImGui::Button("Clear")) {
      log_.clear();
    }
    ImGui::Separator();
    ImGui::BeginChild("event_log", ImVec2(0.0f, 0.0f), true);
    for (const std::string& line : log_) {
      ImGui::TextUnformatted(line.c_str());
    }
    ImGui::EndChild();
  }

  static constexpr uint32_t app_id_ = demo::kDemoAppId;
  int game_port_ = demo::kDefaultPort;
  int discovery_port_ = karma::network::defaultLanDiscoveryPort(demo::kDefaultPort);
  int beacon_interval_ms_ = 1000;
  int entry_ttl_ms_ = 5000;
  int current_players_ = 1;
  int max_players_ = 8;
  int master_listing_counter_ = 0;
  float elapsed_ = 0.0f;
  bool auto_query_ = true;
  bool show_lan_ = true;
  bool show_master_ = true;
  bool hide_full_ = false;
  bool sort_descending_ = false;
  int sort_index_ = 0;
  karma::network::LanDiscoveryStatus last_advertiser_error_ =
      karma::network::LanDiscoveryStatus::Ok;

  std::array<char, kTextSmall> server_id_{};
  std::array<char, kTextMedium> name_{};
  std::array<char, kTextSmall> map_{};
  std::array<char, kTextSmall> mode_{};
  std::array<char, kTextSmall> connect_ip_{};
  std::array<char, kTextSmall> attribute_key_{};
  std::array<char, kTextLarge> attribute_value_{};
  std::array<char, kTextSmall> master_filter_key_{};
  std::array<char, kTextSmall> master_filter_value_{};
  std::array<char, kTextMedium> cache_search_{};

  karma::network::ServerDirectory directory_;
  DemoMasterServerClient* master_ = nullptr;
  std::unique_ptr<karma::network::LanServerAdvertiser> advertiser_;
  std::vector<karma::network::ServerListEvent> events_;
  std::deque<std::string> log_;
  std::string selected_key_;
  std::vector<std::string> pinned_server_ids_;
  ClientProbe client_;
  EmbeddedServer embedded_server_;
  Clock::time_point next_auto_query_ = Clock::now();
};

class DirectoryGame final : public karma::app::GameInterface {
 public:
  explicit DirectoryGame(std::shared_ptr<DirectoryLab> lab)
      : lab_(std::move(lab)) {}

  void onStart() override {
    auto camera = world->createEntity();
    karma::components::TransformComponent camera_xform{};
    camera_xform.setPosition({0.0f, 4.0f, 9.0f});
    camera_xform.setRotation(karma::math::fromYawPitch(0.0f, -0.45f));
    world->add(camera, camera_xform);
    world->add(camera, karma::components::CameraComponent{.is_primary = true});

    auto light = world->createEntity();
    karma::components::TransformComponent light_xform{};
    light_xform.setRotation(karma::math::fromYawPitch(0.4f, -0.8f));
    world->add(light, light_xform);
    world->add(light, karma::components::LightComponent{
        .type = karma::components::LightComponent::Type::Directional,
        .color = {0.92f, 0.98f, 1.0f, 1.0f},
        .intensity = 0.8f});
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    if (!lab_) {
      return;
    }
    lab_->update(dt);
    drawDirectoryBars();
  }

  void onShutdown() override {}

 private:
  void drawDirectoryBars() {
    if (!graphics) {
      return;
    }
    const auto listings = lab_->listings();
    const float spacing = 0.7f;
    const float base_x = -static_cast<float>(listings.size()) * spacing * 0.5f;
    for (std::size_t i = 0; i < listings.size(); ++i) {
      const auto& listing = listings[i];
      const float x = base_x + static_cast<float>(i) * spacing;
      const float height = 0.4f + static_cast<float>(listing.current_players) * 0.18f;
      const karma::math::Color color =
          listing.source == karma::network::ServerListSource::Lan
              ? karma::math::Color{0.2f, 0.9f, 0.55f, 1.0f}
              : karma::math::Color{0.35f, 0.55f, 1.0f, 1.0f};
      graphics->drawLine({x, 0.0f, 0.0f}, {x, height, 0.0f}, color);
      graphics->drawLine({x - 0.2f, height, 0.0f}, {x + 0.2f, height, 0.0f}, color);
    }
    graphics->drawLine({-5.0f, 0.0f, 0.0f},
                       {5.0f, 0.0f, 0.0f},
                       {0.35f, 0.35f, 0.35f, 1.0f});
  }

  std::shared_ptr<DirectoryLab> lab_;
};

}  // namespace

int main() {
  auto lab = std::make_shared<DirectoryLab>();
  DirectoryGame game(lab);

  karma::app::EngineApp engine;
  engine.setUi(karma::ui::imgui::createUiLayer(
      [lab](karma::app::UIContext& ctx) {
        lab->draw(ctx);
      }));

  karma::app::EngineConfig config;
  config.window.title = "Karma Network Directory";
  config.window.width = 1280;
  config.window.height = 800;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.loading_splash.enabled = false;
  config.environment_intensity = 0.03f;
  config.frame_pacing_fps = 120.0f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
