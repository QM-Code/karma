#include "karma/network.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

#include "karma/components.h"

namespace karma::network {
namespace {

constexpr std::size_t kMaxReplicatedTagLength = 4096u;

bool bytesDirty(std::span<const std::byte> previous, std::span<const std::byte> current) {
  if (previous.size() != current.size()) {
    return true;
  }
  return !current.empty() &&
         std::memcmp(previous.data(), current.data(), current.size()) != 0;
}

bool finiteVec3(const math::Vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finiteQuat(const math::Quat& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

bool validAuthorityMode(uint8_t value) {
  return value <= static_cast<uint8_t>(components::AuthorityMode::Custom);
}

bool validReplicationPolicy(uint8_t value) {
  return value <= static_cast<uint8_t>(components::ReplicationPolicy::OwnerInput);
}

void writeVec3(network::BinaryWriter& writer, const math::Vec3& value) {
  writer.writeFloat32(value.x);
  writer.writeFloat32(value.y);
  writer.writeFloat32(value.z);
}

bool readVec3(network::BinaryReader& reader, math::Vec3& value) {
  return reader.readFloat32(value.x) &&
         reader.readFloat32(value.y) &&
         reader.readFloat32(value.z);
}

void writeQuat(network::BinaryWriter& writer, const math::Quat& value) {
  writer.writeFloat32(value.x);
  writer.writeFloat32(value.y);
  writer.writeFloat32(value.z);
  writer.writeFloat32(value.w);
}

bool readQuat(network::BinaryReader& reader, math::Quat& value) {
  return reader.readFloat32(value.x) &&
         reader.readFloat32(value.y) &&
         reader.readFloat32(value.z) &&
         reader.readFloat32(value.w);
}

bool encodeTransform(const world::World& world,
                     world::Entity entity,
                     network::BinaryWriter& writer) {
  if (!world.has<components::TransformComponent>(entity)) {
    return false;
  }
  const auto& transform = world.get<components::TransformComponent>(entity);
  const math::Vec3 position = transform.getPosition();
  const math::Quat rotation = transform.getRotation();
  const math::Vec3 scale = transform.getScale();
  const float rotation_length_squared = math::lengthSquared(rotation);
  if (!finiteVec3(position) || !finiteVec3(scale) || !finiteQuat(rotation) ||
      !std::isfinite(rotation_length_squared) || rotation_length_squared <= 1.0e-12f) {
    return false;
  }
  writeVec3(writer, position);
  writeQuat(writer, math::normalize(rotation));
  writeVec3(writer, scale);
  return true;
}

bool applyTransform(world::World& world,
                    world::Entity entity,
                    network::BinaryReader& reader,
                    bool server_override) {
  (void)server_override;
  constexpr std::size_t kEncodedTransformSize = 10u * sizeof(float);
  if (reader.remaining() != kEncodedTransformSize) {
    return false;
  }
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  if (!readVec3(reader, position) || !readQuat(reader, rotation) || !readVec3(reader, scale)) {
    return false;
  }
  const float rotation_length_squared = math::lengthSquared(rotation);
  if (!finiteVec3(position) || !finiteVec3(scale) || !finiteQuat(rotation) ||
      !std::isfinite(rotation_length_squared) || rotation_length_squared <= 1.0e-12f) {
    return false;
  }
  if (!world.has<components::TransformComponent>(entity)) {
    world.add(entity, components::TransformComponent{});
  }
  auto& transform = world.get<components::TransformComponent>(entity);
  transform.setPosition(position);
  transform.setRotation(math::normalize(rotation));
  transform.setScale(scale);
  return true;
}

bool encodeTag(const world::World& world,
               world::Entity entity,
               network::BinaryWriter& writer) {
  if (!world.has<components::TagComponent>(entity)) {
    return false;
  }
  const std::string& name = world.get<components::TagComponent>(entity).name;
  if (name.size() > kMaxReplicatedTagLength) {
    return false;
  }
  writer.writeString(name);
  return true;
}

bool applyTag(world::World& world,
              world::Entity entity,
              network::BinaryReader& reader,
              bool server_override) {
  (void)server_override;
  std::string name;
  if (!reader.readString(name, kMaxReplicatedTagLength) || !reader.exhausted()) {
    return false;
  }
  world.add(entity, components::TagComponent{.name = std::move(name)});
  return true;
}

bool hasReplicatedEntry(const components::NetworkReplicatedComponent& replicated,
                        uint32_t component_type) {
  return std::any_of(replicated.components.begin(),
                     replicated.components.end(),
                     [component_type](const components::ReplicatedComponentEntry& entry) {
                       return entry.component_type == component_type;
                     });
}

struct EncodedComponent {
  uint32_t component_type = 0;
  components::ReplicationPolicy policy = components::ReplicationPolicy::Snapshot;
  std::vector<std::byte> bytes;
};

std::vector<std::byte> encodeDespawnPayload(components::NetworkEntityId id) {
  network::BinaryWriter writer;
  writer.writeUInt64(id);
  return writer.takeBytes();
}

std::vector<std::byte> encodeComponentPayload(components::NetworkEntityId id,
                                              uint32_t component_type,
                                              std::span<const std::byte> bytes) {
  network::BinaryWriter writer;
  writer.writeUInt64(id);
  writer.writeUInt32(component_type);
  writer.writeUInt32(static_cast<uint32_t>(bytes.size()));
  writer.writeBytes(bytes);
  return writer.takeBytes();
}

bool readAuthority(network::BinaryReader& reader,
                   components::NetworkAuthorityComponent& authority) {
  uint8_t mode = 0;
  uint32_t owner = 0;
  uint8_t server_can_override = 0;
  if (!reader.readUInt8(mode) ||
      !reader.readUInt32(owner) ||
      !reader.readUInt8(server_can_override)) {
    return false;
  }
  if (!validAuthorityMode(mode) || server_can_override > 1u) {
    return false;
  }
  authority.mode = static_cast<components::AuthorityMode>(mode);
  authority.owner_peer = owner;
  authority.server_can_override = server_can_override != 0;
  return true;
}

void writeAuthority(network::BinaryWriter& writer,
                    const components::NetworkAuthorityComponent& authority) {
  writer.writeUInt8(static_cast<uint8_t>(authority.mode));
  writer.writeUInt32(authority.owner_peer);
  writer.writeUInt8(authority.server_can_override ? 1 : 0);
}

bool canApplyClientComponent(const components::NetworkAuthorityComponent& authority,
                             uint32_t peer,
                             bool server_override) {
  if (server_override) {
    return true;
  }
  switch (authority.mode) {
    case components::AuthorityMode::Server:
      return false;
    case components::AuthorityMode::Owner:
      return authority.owner_peer == peer;
    case components::AuthorityMode::Client:
      return true;
    case components::AuthorityMode::Custom:
      return false;
  }
  return false;
}

struct EventStamp {
  uint32_t tick = 0;
  uint32_t sequence = 0;
};

bool stampOlder(EventStamp candidate, EventStamp latest) {
  if (candidate.tick != latest.tick) {
    return network::isPacketSequenceNewer(latest.tick, candidate.tick);
  }
  return candidate.sequence != latest.sequence &&
         network::isPacketSequenceNewer(latest.sequence, candidate.sequence);
}

void accumulateEventStats(NetworkRuntimeStats& stats, const network::SessionEvent& event) {
  stats.events_received += 1;
  switch (event.type) {
    case network::SessionEventType::ProtocolError:
      stats.protocol_errors += 1;
      break;
    case network::SessionEventType::CustomMessage:
      stats.custom_messages += 1;
      break;
    case network::SessionEventType::InputCommand:
      stats.input_commands += 1;
      break;
    case network::SessionEventType::ReplicationMessage:
      stats.replication_messages += 1;
      break;
    default:
      break;
  }
}

}  // namespace

bool ComponentReplicationRegistry::registerReplicator(ComponentReplicator replicator) {
  if (replicator.component_type == 0 ||
      !replicator.encode_full ||
      !replicator.decode_apply) {
    return false;
  }
  if (!replicator.encode_delta) {
    replicator.encode_delta =
        [encode_full = replicator.encode_full](const world::World& world,
                                               world::Entity entity,
                                               std::span<const std::byte> previous,
                                               network::BinaryWriter& writer) {
          (void)previous;
          return encode_full(world, entity, writer);
        };
  }
  if (!replicator.is_dirty) {
    replicator.is_dirty = bytesDirty;
  }
  return replicators_.emplace(replicator.component_type, std::move(replicator)).second;
}

const ComponentReplicator* ComponentReplicationRegistry::find(uint32_t component_type) const {
  auto it = replicators_.find(component_type);
  if (it == replicators_.end()) {
    return nullptr;
  }
  return &it->second;
}

void registerBuiltinReplicators(ComponentReplicationRegistry& registry) {
  registry.registerReplicator(ComponentReplicator{
      .component_type = kTransformComponentWireId,
      .name = "TransformComponent",
      .authority = components::AuthorityMode::Server,
      .encode_full = encodeTransform,
      .decode_apply = applyTransform,
      .is_dirty = bytesDirty,
  });
  registry.registerReplicator(ComponentReplicator{
      .component_type = kTagComponentWireId,
      .name = "TagComponent",
      .authority = components::AuthorityMode::Server,
      .encode_full = encodeTag,
      .decode_apply = applyTag,
      .is_dirty = bytesDirty,
  });
}

void ServerReplicationState::reset() {
  next_id_ = 1;
  spawned_.clear();
  last_sent_.clear();
  authority_rejects_ = 0;
}

void ServerReplicationState::ensureNetworkIds(world::World& world) {
  std::unordered_set<components::NetworkEntityId> used_ids;
  for (const world::Entity entity : world.view<components::NetworkIdentityComponent>()) {
    auto& identity = world.get<components::NetworkIdentityComponent>(entity);
    if (identity.id != components::kInvalidNetworkEntityId &&
        used_ids.insert(identity.id).second) {
      continue;
    }
    identity.id = components::kInvalidNetworkEntityId;
  }

  const auto advance_id = [](components::NetworkEntityId id) {
    return id == std::numeric_limits<components::NetworkEntityId>::max() ? 1u : id + 1u;
  };
  for (const world::Entity entity : world.view<components::NetworkIdentityComponent>()) {
    auto& identity = world.get<components::NetworkIdentityComponent>(entity);
    if (identity.id != components::kInvalidNetworkEntityId) {
      continue;
    }
    if (next_id_ == components::kInvalidNetworkEntityId) {
      next_id_ = 1u;
    }
    while (used_ids.contains(next_id_)) {
      next_id_ = advance_id(next_id_);
    }
    identity.id = next_id_;
    used_ids.insert(identity.id);
    next_id_ = advance_id(next_id_);
  }
}

void ServerReplicationState::removePeer(network::PeerId peer) {
  for (auto spawned_it = spawned_.begin(); spawned_it != spawned_.end();) {
    spawned_it->second.erase(peer.value);
    if (spawned_it->second.empty()) {
      spawned_it = spawned_.erase(spawned_it);
    } else {
      ++spawned_it;
    }
  }

  for (auto it = last_sent_.begin(); it != last_sent_.end();) {
    if (it->first.peer == peer.value) {
      it = last_sent_.erase(it);
    } else {
      ++it;
    }
  }
}

network::MultiSendResult ServerReplicationState::replicate(
    world::World& world,
    network::ServerSession& session,
    const ComponentReplicationRegistry& registry,
    uint32_t tick,
    const ReplicationVisibilityPredicate& visibility) {
  ensureNetworkIds(world);
  network::MultiSendResult result{};

  for (const world::Entity entity :
       world.view<components::NetworkIdentityComponent,
                  components::NetworkReplicatedComponent>()) {
    const auto& identity = world.get<components::NetworkIdentityComponent>(entity);
    const auto& replicated = world.get<components::NetworkReplicatedComponent>(entity);
    if (identity.id == components::kInvalidNetworkEntityId) {
      continue;
    }

    for (const network::PeerId peer_id : session.peers()) {
      const network::SessionPeer* peer = session.peer(peer_id);
      if (!peer) {
        continue;
      }

      bool visible = replicated.visible_by_default;
      if (visible && visibility) {
        visible = visibility(*peer, entity, identity.id);
      }

      if (!visible) {
        if (hasSpawnedFor(identity.id, peer_id)) {
          result.attempted += 1;
          if (sendDespawn(session, identity.id, peer_id, tick)) {
            result.sent += 1;
          } else if (result.first_error == network::SendStatus::Ok) {
            result.first_error = network::SendStatus::BackendError;
          }
        }
        continue;
      }

      if (!hasSpawnedFor(identity.id, peer_id)) {
        std::vector<std::byte> payload = encodeSpawn(world, entity, registry, peer_id);
        result.attempted += 1;
        const network::SendResult sent = session.sendTo(peer_id,
                                                    network::MessageType::EntitySpawn,
                                                    payload,
                                                    network::Delivery::Reliable,
                                                    0,
                                                    tick);
        if (sent.ok()) {
          result.sent += 1;
          markSpawnedFor(identity.id, peer_id);
        } else if (result.first_error == network::SendStatus::Ok) {
          result.first_error = sent.status;
        }
        continue;
      }

      std::unordered_set<uint32_t> processed_components;
      for (const auto& entry : replicated.components) {
        if (entry.component_type == 0u ||
            !validReplicationPolicy(static_cast<uint8_t>(entry.policy)) ||
            !processed_components.insert(entry.component_type).second) {
          continue;
        }
        const ComponentReplicator* replicator = registry.find(entry.component_type);
        if (!replicator) {
          continue;
        }
        const SentComponentKey key{peer_id.value, identity.id, entry.component_type};
        auto previous_it = last_sent_.find(key);
        const std::span<const std::byte> previous =
            previous_it == last_sent_.end()
                ? std::span<const std::byte>{}
                : std::span<const std::byte>(previous_it->second);
        network::BinaryWriter full_writer;
        if (!replicator->encode_full(world, entity, full_writer)) {
          continue;
        }
        std::vector<std::byte> full = full_writer.takeBytes();
        if (!previous.empty() && !replicator->is_dirty(previous, full)) {
          continue;
        }

        const bool use_delta = entry.policy == components::ReplicationPolicy::Delta &&
                               !previous.empty();
        std::optional<std::vector<std::byte>> encoded =
            encodeComponent(world, entity, *replicator, previous, use_delta);
        if (!encoded) {
          continue;
        }
        std::vector<std::byte> payload =
            encodeComponentPayload(identity.id, entry.component_type, *encoded);
        const network::MessageType message_type =
            use_delta ? network::MessageType::ComponentDelta
                      : network::MessageType::ComponentSnapshot;
        result.attempted += 1;
        const network::SendResult sent = session.sendTo(peer_id,
                                                    message_type,
                                                    payload,
                                                    network::Delivery::Unreliable,
                                                    1,
                                                    tick);
        if (sent.ok()) {
          result.sent += 1;
          rememberComponent(peer_id, identity.id, entry.component_type, full);
        } else if (result.first_error == network::SendStatus::Ok) {
          result.first_error = sent.status;
        }
      }
    }
  }

  return result;
}

bool ServerReplicationState::sendDespawn(network::ServerSession& session,
                                         components::NetworkEntityId network_id,
                                         network::PeerId peer,
                                         uint32_t tick) {
  std::vector<std::byte> payload = encodeDespawnPayload(network_id);
  const network::SendResult sent = session.sendTo(peer,
                                             network::MessageType::EntityDespawn,
                                             payload,
                                             network::Delivery::Reliable,
                                             0,
                                             tick);
  if (!sent.ok()) {
    return false;
  }
  clearSpawnedFor(network_id, peer);
  for (auto it = last_sent_.begin(); it != last_sent_.end();) {
    if (it->first.peer == peer.value && it->first.entity == network_id) {
      it = last_sent_.erase(it);
    } else {
      ++it;
    }
  }
  return true;
}

bool ServerReplicationState::applyClientComponentEvent(
    world::World& world,
    const ComponentReplicationRegistry& registry,
    const network::SessionEvent& event,
    bool server_override) {
  if (event.type != network::SessionEventType::ReplicationMessage ||
      (event.message_type != network::MessageType::ComponentSnapshot &&
       event.message_type != network::MessageType::ComponentDelta) ||
      event.stale_sequence || (!server_override && !event.peer.isValid())) {
    return false;
  }

  network::BinaryReader reader(event.payload);
  uint64_t network_id = 0;
  uint32_t component_type = 0;
  uint32_t component_size = 0;
  std::vector<std::byte> component_bytes;
  if (!reader.readUInt64(network_id) ||
      !reader.readUInt32(component_type) ||
      !reader.readUInt32(component_size) ||
      !reader.readBytes(component_size, component_bytes) ||
      !reader.exhausted() ||
      network_id == components::kInvalidNetworkEntityId || component_type == 0u) {
    return false;
  }

  world::Entity entity{};
  bool found = false;
  for (const world::Entity candidate : world.view<components::NetworkIdentityComponent>()) {
    if (world.get<components::NetworkIdentityComponent>(candidate).id == network_id) {
      entity = candidate;
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }

  components::NetworkAuthorityComponent authority{};
  if (world.has<components::NetworkAuthorityComponent>(entity)) {
    authority = world.get<components::NetworkAuthorityComponent>(entity);
  }
  if (!canApplyClientComponent(authority, event.peer.value, server_override)) {
    authority_rejects_ += 1;
    return false;
  }

  const ComponentReplicator* replicator = registry.find(component_type);
  if (!replicator) {
    return false;
  }
  network::BinaryReader component_reader(component_bytes);
  return replicator->decode_apply(world, entity, component_reader, server_override) &&
         component_reader.exhausted();
}

std::size_t ServerReplicationState::SentComponentKeyHash::operator()(
    const SentComponentKey& key) const {
  const std::size_t a = std::hash<uint32_t>{}(key.peer);
  const std::size_t b = std::hash<uint64_t>{}(key.entity);
  const std::size_t c = std::hash<uint32_t>{}(key.component);
  return a ^ (b << 1u) ^ (c << 7u);
}

bool ServerReplicationState::hasSpawnedFor(components::NetworkEntityId id,
                                           network::PeerId peer) const {
  auto it = spawned_.find(id);
  return it != spawned_.end() && it->second.find(peer.value) != it->second.end();
}

void ServerReplicationState::markSpawnedFor(components::NetworkEntityId id,
                                            network::PeerId peer) {
  spawned_[id].insert(peer.value);
}

void ServerReplicationState::clearSpawnedFor(components::NetworkEntityId id,
                                             network::PeerId peer) {
  auto it = spawned_.find(id);
  if (it == spawned_.end()) {
    return;
  }
  it->second.erase(peer.value);
  if (it->second.empty()) {
    spawned_.erase(it);
  }
}

std::vector<std::byte> ServerReplicationState::encodeSpawn(
    world::World& world,
    world::Entity entity,
    const ComponentReplicationRegistry& registry,
    network::PeerId peer) {
  const auto& identity = world.get<components::NetworkIdentityComponent>(entity);
  const auto& replicated = world.get<components::NetworkReplicatedComponent>(entity);
  components::NetworkAuthorityComponent authority{};
  if (world.has<components::NetworkAuthorityComponent>(entity)) {
    authority = world.get<components::NetworkAuthorityComponent>(entity);
  }

  std::vector<EncodedComponent> encoded_components;
  std::unordered_set<uint32_t> encoded_types;
  for (const auto& entry : replicated.components) {
    if (entry.component_type == 0u ||
        !validReplicationPolicy(static_cast<uint8_t>(entry.policy)) ||
        !encoded_types.insert(entry.component_type).second ||
        encoded_components.size() == std::numeric_limits<uint16_t>::max()) {
      continue;
    }
    const ComponentReplicator* replicator = registry.find(entry.component_type);
    if (!replicator) {
      continue;
    }
    network::BinaryWriter component_writer;
    if (!replicator->encode_full(world, entity, component_writer)) {
      continue;
    }
    EncodedComponent encoded;
    encoded.component_type = entry.component_type;
    encoded.policy = entry.policy;
    encoded.bytes = component_writer.takeBytes();
    rememberComponent(peer, identity.id, entry.component_type, encoded.bytes);
    encoded_components.push_back(std::move(encoded));
  }

  network::BinaryWriter writer;
  writer.writeUInt64(identity.id);
  writeAuthority(writer, authority);
  writer.writeUInt16(static_cast<uint16_t>(encoded_components.size()));
  for (const EncodedComponent& component : encoded_components) {
    writer.writeUInt32(component.component_type);
    writer.writeUInt8(static_cast<uint8_t>(component.policy));
    writer.writeUInt32(static_cast<uint32_t>(component.bytes.size()));
    writer.writeBytes(component.bytes);
  }
  return writer.takeBytes();
}

std::optional<std::vector<std::byte>> ServerReplicationState::encodeComponent(
    world::World& world,
    world::Entity entity,
    const ComponentReplicator& replicator,
    std::span<const std::byte> previous,
    bool delta) {
  network::BinaryWriter writer;
  const bool ok = delta ? replicator.encode_delta(world, entity, previous, writer)
                        : replicator.encode_full(world, entity, writer);
  if (!ok) {
    return std::nullopt;
  }
  return writer.takeBytes();
}

void ServerReplicationState::rememberComponent(network::PeerId peer,
                                               components::NetworkEntityId id,
                                               uint32_t component_type,
                                               std::span<const std::byte> bytes) {
  last_sent_[SentComponentKey{peer.value, id, component_type}] =
      std::vector<std::byte>(bytes.begin(), bytes.end());
}

void ClientReplicationState::reset() {
  entities_.clear();
  entity_stamps_.clear();
  component_stamps_.clear();
  stale_rejects_ = 0;
}

bool ClientReplicationState::applyEvent(world::World& world,
                                        const ComponentReplicationRegistry& registry,
                                        const network::SessionEvent& event) {
  if (event.type != network::SessionEventType::ReplicationMessage) {
    return false;
  }
  if (event.stale_sequence) {
    stale_rejects_ += 1;
    return false;
  }
  const ReplicationStamp stamp{.tick = event.tick, .sequence = event.sequence};
  switch (event.message_type) {
    case network::MessageType::EntitySpawn:
      return applySpawn(world, registry, event.payload, stamp);
    case network::MessageType::EntityDespawn:
      return applyDespawn(world, event.payload, stamp);
    case network::MessageType::ComponentSnapshot:
    case network::MessageType::ComponentDelta:
      return applyComponentUpdate(world, registry, event.payload, stamp);
    case network::MessageType::AuthorityTransfer:
      return applyAuthorityTransfer(world, event.payload, stamp);
    default:
      return false;
  }
}

std::optional<world::Entity> ClientReplicationState::entityFor(
    components::NetworkEntityId id) const {
  auto it = entities_.find(id);
  if (it == entities_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool ClientReplicationState::applySpawn(world::World& world,
                                        const ComponentReplicationRegistry& registry,
                                        std::span<const std::byte> payload,
                                        ReplicationStamp stamp) {
  network::BinaryReader reader(payload);
  uint64_t network_id = 0;
  components::NetworkAuthorityComponent authority{};
  uint16_t component_count = 0;
  if (!reader.readUInt64(network_id) ||
      !readAuthority(reader, authority) ||
      !reader.readUInt16(component_count)) {
    return false;
  }
  if (network_id == components::kInvalidNetworkEntityId) {
    return false;
  }
  if (isEntityEventStale(network_id, stamp)) {
    stale_rejects_ += 1;
    return false;
  }

  std::vector<EncodedComponent> encoded_components;
  encoded_components.reserve(component_count);
  std::unordered_set<uint32_t> component_types;
  for (uint16_t i = 0; i < component_count; ++i) {
    EncodedComponent component;
    uint8_t policy = 0;
    uint32_t component_size = 0;
    if (!reader.readUInt32(component.component_type) ||
        !reader.readUInt8(policy) ||
        !reader.readUInt32(component_size) ||
        !reader.readBytes(component_size, component.bytes) ||
        component.component_type == 0u || !validReplicationPolicy(policy) ||
        !component_types.insert(component.component_type).second) {
      return false;
    }
    component.policy = static_cast<components::ReplicationPolicy>(policy);
    encoded_components.push_back(std::move(component));
  }
  if (!reader.exhausted()) {
    return false;
  }

  auto existing_it = entities_.find(network_id);
  world::Entity existing_entity{};
  if (existing_it != entities_.end()) {
    if (world.isAlive(existing_it->second)) {
      existing_entity = existing_it->second;
    } else {
      entities_.erase(existing_it);
    }
  }
  const auto stamp_it = entity_stamps_.find(network_id);
  const bool duplicate_stamp =
      stamp_it != entity_stamps_.end() && stamp_it->second.tick == stamp.tick &&
      stamp_it->second.sequence == stamp.sequence;
  if (duplicate_stamp && existing_entity.isValid()) {
    return true;
  }

  // Decode a full spawn into a staging entity. This keeps the currently mapped
  // entity untouched if any later component rejects the payload.
  const world::Entity entity = world.createEntity();

  components::NetworkReplicatedComponent replicated{};
  std::vector<uint32_t> applied_components;
  try {
    for (const EncodedComponent& component : encoded_components) {
      replicated.components.push_back(components::ReplicatedComponentEntry{
          .component_type = component.component_type,
          .policy = component.policy,
      });

      const ComponentReplicator* replicator = registry.find(component.component_type);
      if (!replicator) {
        continue;
      }
      network::BinaryReader component_reader(component.bytes);
      if (!replicator->decode_apply(world, entity, component_reader, true) ||
          !component_reader.exhausted()) {
        world.destroyEntity(entity);
        return false;
      }
      applied_components.push_back(component.component_type);
    }
    world.add(entity, components::NetworkIdentityComponent{.id = network_id});
    world.add(entity, authority);
    world.add(entity, std::move(replicated));
  } catch (...) {
    world.destroyEntity(entity);
    throw;
  }

  try {
    entities_.insert_or_assign(network_id, entity);
  } catch (...) {
    world.destroyEntity(entity);
    throw;
  }
  if (existing_entity.isValid()) {
    world.destroyEntity(existing_entity);
  }
  forgetComponentStamps(network_id);
  rememberEntityStamp(network_id, stamp);
  for (const uint32_t component_type : applied_components) {
    rememberComponentStamp(network_id, component_type, stamp);
  }
  return true;
}

bool ClientReplicationState::applyDespawn(world::World& world,
                                          std::span<const std::byte> payload,
                                          ReplicationStamp stamp) {
  network::BinaryReader reader(payload);
  uint64_t network_id = 0;
  if (!reader.readUInt64(network_id) || !reader.exhausted() ||
      network_id == components::kInvalidNetworkEntityId) {
    return false;
  }
  if (isEntityEventStale(network_id, stamp)) {
    stale_rejects_ += 1;
    return false;
  }
  auto it = entities_.find(network_id);
  if (it == entities_.end()) {
    return false;
  }
  if (world.isAlive(it->second)) {
    world.destroyEntity(it->second);
  }
  entities_.erase(it);
  forgetComponentStamps(network_id);
  rememberEntityStamp(network_id, stamp);
  return true;
}

bool ClientReplicationState::applyComponentUpdate(
    world::World& world,
    const ComponentReplicationRegistry& registry,
    std::span<const std::byte> payload,
    ReplicationStamp stamp) {
  network::BinaryReader reader(payload);
  uint64_t network_id = 0;
  uint32_t component_type = 0;
  uint32_t component_size = 0;
  std::vector<std::byte> component_bytes;
  if (!reader.readUInt64(network_id) ||
      !reader.readUInt32(component_type) ||
      !reader.readUInt32(component_size) ||
      !reader.readBytes(component_size, component_bytes) ||
      !reader.exhausted() ||
      network_id == components::kInvalidNetworkEntityId || component_type == 0u) {
    return false;
  }
  if (isComponentEventStale(network_id, component_type, stamp)) {
    stale_rejects_ += 1;
    return false;
  }

  auto it = entities_.find(network_id);
  if (it == entities_.end() || !world.isAlive(it->second)) {
    return false;
  }
  const ComponentReplicator* replicator = registry.find(component_type);
  if (!replicator) {
    return false;
  }
  network::BinaryReader component_reader(component_bytes);
  if (!replicator->decode_apply(world, it->second, component_reader, true) ||
      !component_reader.exhausted()) {
    return false;
  }

  if (world.has<components::NetworkReplicatedComponent>(it->second)) {
    auto& replicated = world.get<components::NetworkReplicatedComponent>(it->second);
    if (!hasReplicatedEntry(replicated, component_type)) {
      replicated.components.push_back(components::ReplicatedComponentEntry{
          .component_type = component_type,
          .policy = components::ReplicationPolicy::Snapshot,
      });
    }
  }
  rememberEntityStamp(network_id, stamp);
  rememberComponentStamp(network_id, component_type, stamp);
  return true;
}

bool ClientReplicationState::applyAuthorityTransfer(world::World& world,
                                                    std::span<const std::byte> payload,
                                                    ReplicationStamp stamp) {
  network::BinaryReader reader(payload);
  uint64_t network_id = 0;
  components::NetworkAuthorityComponent authority{};
  if (!reader.readUInt64(network_id) || !readAuthority(reader, authority) ||
      !reader.exhausted() || network_id == components::kInvalidNetworkEntityId) {
    return false;
  }
  if (isEntityEventStale(network_id, stamp)) {
    stale_rejects_ += 1;
    return false;
  }
  auto it = entities_.find(network_id);
  if (it == entities_.end() || !world.isAlive(it->second)) {
    return false;
  }
  world.add(it->second, authority);
  rememberEntityStamp(network_id, stamp);
  return true;
}

std::size_t ClientReplicationState::ComponentStampKeyHash::operator()(
    const ComponentStampKey& key) const {
  const std::size_t a = std::hash<uint64_t>{}(key.entity);
  const std::size_t b = std::hash<uint32_t>{}(key.component);
  return a ^ (b << 1u);
}

bool ClientReplicationState::isEntityEventStale(components::NetworkEntityId id,
                                                ReplicationStamp stamp) const {
  auto it = entity_stamps_.find(id);
  return it != entity_stamps_.end() &&
         stampOlder(EventStamp{stamp.tick, stamp.sequence},
                    EventStamp{it->second.tick, it->second.sequence});
}

bool ClientReplicationState::isComponentEventStale(components::NetworkEntityId id,
                                                   uint32_t component_type,
                                                   ReplicationStamp stamp) const {
  if (isEntityEventStale(id, stamp)) {
    return true;
  }
  auto it = component_stamps_.find(ComponentStampKey{id, component_type});
  return it != component_stamps_.end() &&
         stampOlder(EventStamp{stamp.tick, stamp.sequence},
                    EventStamp{it->second.tick, it->second.sequence});
}

void ClientReplicationState::rememberEntityStamp(components::NetworkEntityId id,
                                                 ReplicationStamp stamp) {
  auto it = entity_stamps_.find(id);
  if (it == entity_stamps_.end() ||
      stampOlder(EventStamp{it->second.tick, it->second.sequence},
                 EventStamp{stamp.tick, stamp.sequence})) {
    entity_stamps_[id] = stamp;
  }
}

void ClientReplicationState::rememberComponentStamp(components::NetworkEntityId id,
                                                    uint32_t component_type,
                                                    ReplicationStamp stamp) {
  const ComponentStampKey key{id, component_type};
  auto it = component_stamps_.find(key);
  if (it == component_stamps_.end() ||
      stampOlder(EventStamp{it->second.tick, it->second.sequence},
                 EventStamp{stamp.tick, stamp.sequence})) {
    component_stamps_[key] = stamp;
  }
}

void ClientReplicationState::forgetComponentStamps(components::NetworkEntityId id) {
  for (auto it = component_stamps_.begin(); it != component_stamps_.end();) {
    if (it->first.entity == id) {
      it = component_stamps_.erase(it);
    } else {
      ++it;
    }
  }
}

ServerNetworkRuntimeModule::ServerNetworkRuntimeModule(
    network::ServerSession& session,
    ComponentReplicationRegistry& registry)
    : ServerNetworkRuntimeModule(session,
                                 registry,
                                 ServerNetworkRuntimeConfig{.app_id = session.appId()}) {}

ServerNetworkRuntimeModule::ServerNetworkRuntimeModule(
    network::ServerSession& session,
    ComponentReplicationRegistry& registry,
    ServerNetworkRuntimeConfig config)
    : session_(&session),
      registry_(registry),
      event_handler_(std::move(config.event_handler)),
      visibility_(std::move(config.visibility)),
      replication_enabled_(config.replication_enabled) {}

ServerNetworkRuntimeModule::ServerNetworkRuntimeModule(
    std::unique_ptr<network::IServerTransport> transport,
    ComponentReplicationRegistry& registry,
    ServerNetworkRuntimeConfig config)
    : owned_session_(std::make_unique<network::ServerSession>(std::move(transport),
                                                          config.app_id)),
      session_(owned_session_.get()),
      registry_(registry),
      event_handler_(std::move(config.event_handler)),
      visibility_(std::move(config.visibility)),
      replication_enabled_(config.replication_enabled) {}

void ServerNetworkRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  (void)context;
}

void ServerNetworkRuntimeModule::onFrameBegin(world::World& world, float dt) {
  (void)dt;
  events_.clear();
  session_->poll(events_);
  for (const network::SessionEvent& event : events_) {
    accumulateEventStats(stats_, event);
    if (event.type == network::SessionEventType::PeerDisconnected) {
      replication_.removePeer(event.peer);
    } else if (replication_enabled_ &&
               event.type == network::SessionEventType::ReplicationMessage &&
               (event.message_type == network::MessageType::ComponentSnapshot ||
                event.message_type == network::MessageType::ComponentDelta)) {
      const std::size_t before = replication_.authorityRejects();
      replication_.applyClientComponentEvent(world, registry_, event);
      stats_.authority_rejects += replication_.authorityRejects() - before;
    }
    if (event_handler_) {
      event_handler_(event, world);
    }
  }
}

void ServerNetworkRuntimeModule::onAfterFixedUpdate(world::World& world,
                                                    float fixed_dt,
                                                    uint64_t fixed_tick) {
  (void)fixed_dt;
  if (!replication_enabled_) {
    return;
  }
  const network::MultiSendResult result =
      replication_.replicate(world,
                             *session_,
                             registry_,
                             static_cast<uint32_t>(fixed_tick),
                             visibility_);
  stats_.replication_sends_attempted += result.attempted;
  stats_.replication_sends_succeeded += result.sent;
}

void ServerNetworkRuntimeModule::onFrameEnd(world::World& world) {
  (void)world;
  session_->flush();
}

void ServerNetworkRuntimeModule::onUpdate(world::World& world,
                                          float dt,
                                          float interpolation_alpha) {
  (void)world;
  (void)dt;
  (void)interpolation_alpha;
}

ClientNetworkRuntimeModule::ClientNetworkRuntimeModule(
    network::ClientSession& session,
    ComponentReplicationRegistry& registry)
    : ClientNetworkRuntimeModule(session,
                                 registry,
                                 ClientNetworkRuntimeConfig{.app_id = session.appId()}) {}

ClientNetworkRuntimeModule::ClientNetworkRuntimeModule(
    network::ClientSession& session,
    ComponentReplicationRegistry& registry,
    ClientNetworkRuntimeConfig config)
    : session_(&session),
      registry_(registry),
      event_handler_(std::move(config.event_handler)),
      replication_enabled_(config.replication_enabled) {}

ClientNetworkRuntimeModule::ClientNetworkRuntimeModule(
    std::unique_ptr<network::IClientTransport> transport,
    ComponentReplicationRegistry& registry,
    ClientNetworkRuntimeConfig config)
    : owned_session_(std::make_unique<network::ClientSession>(std::move(transport),
                                                          config.app_id)),
      session_(owned_session_.get()),
      registry_(registry),
      event_handler_(std::move(config.event_handler)),
      replication_enabled_(config.replication_enabled) {}

void ClientNetworkRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  (void)context;
}

void ClientNetworkRuntimeModule::onFrameBegin(world::World& world, float dt) {
  (void)dt;
  events_.clear();
  session_->poll(events_);
  for (const network::SessionEvent& event : events_) {
    accumulateEventStats(stats_, event);
    if (replication_enabled_ && event.type == network::SessionEventType::ReplicationMessage) {
      const std::size_t before = replication_.staleRejects();
      replication_.applyEvent(world, registry_, event);
      stats_.stale_replication_rejects += replication_.staleRejects() - before;
    }
    if (event_handler_) {
      event_handler_(event, world);
    }
  }
}

void ClientNetworkRuntimeModule::onFrameEnd(world::World& world) {
  (void)world;
  session_->flush();
}

void ClientNetworkRuntimeModule::onUpdate(world::World& world,
                                          float dt,
                                          float interpolation_alpha) {
  (void)world;
  (void)dt;
  (void)interpolation_alpha;
}

}  // namespace karma::network
