# Network Discovery And Server Directory

Karma provides LAN discovery and shared server-directory cache infrastructure in
`karma::network`. It does not provide a hosted public master server.

## LAN Discovery

Use an explicit discovery port. The helper `defaultLanDiscoveryPort(game_port)`
returns `game_port + 1`, clamped at `65535`.

Servers advertise with `LanServerAdvertiser`:

```cpp
auto listing = karma::network::makeLanServerListing(app_id,
                                                    game_port,
                                                    "My Server",
                                                    "arena",
                                                    "coop");
karma::network::LanServerAdvertiser advertiser({
    .discovery_port = karma::network::defaultLanDiscoveryPort(game_port),
    .app_id = app_id,
    .game_port = game_port,
    .listing = listing,
});
advertiser.start();
advertiser.poll(std::chrono::steady_clock::now());
```

Browsers query and cache with `LanServerBrowser`:

```cpp
karma::network::LanServerBrowser browser({
    .discovery_port = karma::network::defaultLanDiscoveryPort(game_port),
    .app_id = app_id,
    .game_port = game_port,
});
browser.start();
browser.sendQuery();

std::vector<karma::network::ServerListEvent> events;
browser.poll(std::chrono::steady_clock::now(), events);
```

Discovery is poll-driven and thread-free. Advertisers send periodic UDP
broadcast beacons and respond to explicit browser queries. Browsers ignore
malformed packets, unsupported discovery protocol versions, and mismatched
`app_id` packets.

LAN datagrams are capped at `kLanDiscoveryMaxDatagramSize` (`1200` bytes).
Oversized metadata fails encode/send instead of being truncated.

## Server Metadata

`ServerListing` carries stable typed fields and arbitrary attributes:

- `server_id`
- `connect_endpoint`
- `game_port`
- `app_id`
- `protocol_version`
- `name`, `map`, `mode`
- `current_players`, `max_players`
- `attributes`
- source and TTL bookkeeping

Use `makeLanServerId()` or `makeLanServerListing()` when a game does not already
have a stable server id. The generated id is stable for the same app id, game
port, host fingerprint, and optional salt.

## Cache Queries

`ServerListCache` can store LAN and master-list results together. It supports
upsert, removal, TTL expiry, lookup by id or endpoint, and filtered listing:

```cpp
karma::network::ServerListQuery query;
query.source = karma::network::ServerListSource::Lan;
query.text = "arena";
query.hide_full = true;
query.sort = karma::network::ServerListSort::PlayerCount;
query.descending = true;
query.pinned_server_ids = {"favorite-server"};

auto visible = cache.list(query);
```

Pinned ids are returned before unpinned rows while preserving the requested
sort order within each group.

## Master Lists

Core Karma only defines `IMasterServerClient`, `MasterServerQuery`, and
`MasterServerEvent`. Authentication, HTTP implementation, schema ownership,
rate limiting, persistence, and hosting are game or service concerns.

`ServerDirectory` can combine a `LanServerBrowser` and any
`IMasterServerClient` into one `ServerListCache`.

The example header `examples/network/http_master_adapter.h` shows a simple JSON
HTTP adapter shape while still requiring the game to supply an HTTP transport.
It is intentionally outside the engine library.

## Examples

- `network_server` advertises itself on LAN discovery port `game_port + 1`.
- `network_client --lan [port] [name]` discovers a LAN server before connecting.
- `network_discovery_directory` is a graphical directory lab with LAN
  advertise/query/cache events, an embedded local server, selected-server
  connection probes, cache sorting/filtering/pinning, and fake master-list
  integration.
