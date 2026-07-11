#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "karma/foliage.h"
#include "karma/prefabs.h"

namespace {

std::filesystem::path makeTempDir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("karma_foliage_tests_" + std::to_string(stamp));
  std::filesystem::create_directories(path);
  return path;
}

std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

void writeBytes(const std::filesystem::path& path,
                const std::vector<uint8_t>& bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  assert(stream.good());
}

karma::foliage::FoliageInstance instance(float x,
                                         float y,
                                         float z,
                                         float yaw = 0.0f) {
  karma::foliage::FoliageInstance result{};
  result.position = {x, y, z};
  result.yaw_radians = yaw;
  result.scale = {1.0f + x * 0.01f, 2.0f, 1.0f};
  result.params = {x, y, z, yaw};
  return result;
}

void testCanonicalRoundTripAndIndex() {
  using namespace karma::foliage;
  const auto dir = makeTempDir();
  const auto first_path = dir / "first.kfoliage";
  const auto second_path = dir / "second.kfoliage";

  FoliageDocument first{};
  first.chunk_size = 16.0f;
  first.chunks = {
      {.coord = {1, 0}, .instances = {instance(20.0f, 3.0f, 2.0f)}},
      {.coord = {0, 0},
       .instances = {instance(8.0f, 2.0f, 3.0f),
                     instance(1.0f, 1.0f, 2.0f)}},
  };
  FoliageDocument second{};
  second.chunk_size = 16.0f;
  second.chunks = {
      {.coord = {0, 0},
       .instances = {instance(1.0f, 1.0f, 2.0f),
                     instance(8.0f, 2.0f, 3.0f)}},
      {.coord = {1, 0}, .instances = {instance(20.0f, 3.0f, 2.0f)}},
  };

  std::string error;
  assert(writeFoliageFile(first_path, first, &error));
  assert(writeFoliageFile(first_path, first, &error));
  assert(error.empty());
  assert(writeFoliageFile(second_path, second, &error));
  assert(readBytes(first_path) == readBytes(second_path));
  assert(std::filesystem::file_size(first_path) ==
         48u + 2u * 52u + 3u * kFoliageInstanceRecordSize);

  const auto index = readFoliageFileIndex(first_path, &error);
  assert(index.has_value());
  assert(index->chunk_size == 16.0f);
  assert(index->instance_count == 3u);
  assert(index->chunks.size() == 2u);
  assert((index->chunks[0].coord == FoliageChunkCoord{0, 0}));
  assert((index->chunks[1].coord == FoliageChunkCoord{1, 0}));
  assert(index->find({9, 9}) == nullptr);

  const auto chunk = readFoliageChunk(first_path, *index, {0, 0}, &error);
  assert(chunk.has_value());
  assert(chunk->instances.size() == 2u);
  assert(chunk->instances[0].position.x == 1.0f);
  assert(chunk->instances[1].position.x == 8.0f);
  const auto limited_chunk =
      readFoliageChunk(first_path, *index, {0, 0}, 1u, &error);
  assert(limited_chunk.has_value());
  assert(limited_chunk->instances.size() == 1u);

  const auto loaded = readFoliageFile(first_path, &error);
  assert(loaded.has_value());
  assert(loaded->instanceCount() == 3u);
  assert(loaded->chunks.size() == 2u);
  std::filesystem::remove_all(dir);
}

void testFileValidationFailures() {
  using namespace karma::foliage;
  const auto dir = makeTempDir();
  FoliageDocument document{};
  document.chunk_size = 8.0f;
  document.chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
  };
  std::string error;
  const auto valid_path = dir / "valid.kfoliage";
  assert(writeFoliageFile(valid_path, document, &error));

  auto bytes = readBytes(valid_path);
  bytes[0] = 'X';
  const auto bad_magic = dir / "bad_magic.kfoliage";
  {
    std::ofstream stream(bad_magic, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  assert(!readFoliageFileIndex(bad_magic, &error));
  assert(error.find("magic") != std::string::npos);

  bytes = readBytes(valid_path);
  bytes[8] = 2u;
  const auto bad_version = dir / "bad_version.kfoliage";
  {
    std::ofstream stream(bad_version, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  assert(!readFoliageFileIndex(bad_version, &error));
  assert(error.find("version") != std::string::npos);

  bytes = readBytes(valid_path);
  std::fill(bytes.begin() + 24u, bytes.begin() + 32u, uint8_t{0u});
  const auto impossible_counts = dir / "impossible_counts.kfoliage";
  {
    std::ofstream stream(impossible_counts, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  assert(!readFoliageFileIndex(impossible_counts, &error));
  assert(error.find("chunk count exceeds") != std::string::npos);

  bytes = readBytes(valid_path);
  bytes.resize(bytes.size() - 1u);
  const auto truncated = dir / "truncated.kfoliage";
  {
    std::ofstream stream(truncated, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  assert(!readFoliageFile(truncated, &error));
  assert(!error.empty());

  document.chunks[0].coord = {2, 0};
  assert(!writeFoliageFile(dir / "misindexed.kfoliage", document, &error));
  assert(error.find("wrong chunk") != std::string::npos);
  std::filesystem::remove_all(dir);
}

void testDeterministicPaintEraseAndUndo() {
  using namespace karma::foliage;
  FoliagePaintBrush brush{};
  brush.center = {15.5f, 0.0f, 15.5f};
  brush.radius = 8.0f;
  brush.density = 0.25f;
  brush.min_spacing = 0.8f;
  brush.min_scale = {0.5f, 0.75f, 0.5f};
  brush.max_scale = {1.5f, 2.0f, 1.5f};
  brush.max_slope_degrees = 40.0f;
  brush.seed = 42u;
  const FoliageSurfaceSampler flat = [](float x, float z) {
    return std::optional<FoliageSurfaceSample>{FoliageSurfaceSample{
        .height = x * 0.01f + z * 0.02f,
        .normal = {0.0f, 1.0f, 0.0f},
    }};
  };

  FoliageLayer first(16.0f);
  FoliageLayer second(16.0f);
  const auto first_edit = first.paint(brush, flat);
  const auto second_edit = second.paint(brush, flat);
  assert(!first_edit.empty());
  assert(first.toDocument().chunks.size() >= 1u);
  assert(first.toDocument().chunks == second.toDocument().chunks);
  assert(first.instanceCount() == second.instanceCount());

  std::vector<FoliageInstance> flattened;
  for (const auto& [coord, instances] : first.chunks()) {
    (void)coord;
    flattened.insert(flattened.end(), instances.begin(), instances.end());
  }
  for (std::size_t a = 0u; a < flattened.size(); ++a) {
    for (std::size_t b = a + 1u; b < flattened.size(); ++b) {
      const float dx = flattened[a].position.x - flattened[b].position.x;
      const float dz = flattened[a].position.z - flattened[b].position.z;
      assert(dx * dx + dz * dz >= brush.min_spacing * brush.min_spacing - 1.0e-5f);
    }
  }

  assert(first.applyEdit(first_edit, true));
  assert(first.instanceCount() == 0u);
  assert(first.applyEdit(first_edit));
  assert(first.instanceCount() == first_edit.added.size());

  FoliageEraseBrush erase{};
  erase.center = brush.center;
  erase.radius = brush.radius;
  erase.strength = 1.0f;
  const auto erased = first.erase(erase);
  assert(!erased.removed.empty());
  assert(first.instanceCount() == 0u);
  assert(first.applyEdit(erased, true));
  assert(first.instanceCount() == erased.removed.size());

  FoliageLayer rejected;
  brush.max_slope_degrees = 10.0f;
  const auto steep = [](float, float) {
    return std::optional<FoliageSurfaceSample>{FoliageSurfaceSample{
        .height = 1.0f,
        .normal = {0.8660254f, 0.5f, 0.0f},
    }};
  };
  assert(rejected.paint(brush, steep).empty());
}

void testPaintFiniteExtremeRanges() {
  using namespace karma::foliage;

  constexpr float kMaxFloat = std::numeric_limits<float>::max();
  FoliagePaintBrush brush{};
  brush.radius = 1.0f;
  brush.density = 4.0f;
  brush.min_spacing = 0.0f;
  brush.min_yaw_radians = -kMaxFloat;
  brush.max_yaw_radians = kMaxFloat;
  brush.min_scale = {-kMaxFloat, -kMaxFloat, -kMaxFloat};
  brush.max_scale = {kMaxFloat, kMaxFloat, kMaxFloat};
  brush.seed = 91u;

  FoliageLayer layer(16.0f);
  const FoliageEditResult edit = layer.paint(
      brush, [](float, float) {
        return std::optional<FoliageSurfaceSample>{FoliageSurfaceSample{
            .height = 0.0f,
            .normal = {0.0f, 1.0f, 0.0f},
        }};
      });
  assert(!edit.empty());
  for (const FoliageInstanceEdit& addition : edit.added) {
    assert(karma::math::isFinite(addition.instance.position));
    assert(std::isfinite(addition.instance.yaw_radians));
    assert(karma::math::isFinite(addition.instance.scale));
  }

  const auto dir = makeTempDir();
  std::string error;
  assert(writeFoliageFile(dir / "finite_extremes.kfoliage",
                          layer.toDocument(),
                          &error));
  assert(error.empty());
  std::filesystem::remove_all(dir);
}

void testMillionInstanceChunkIndex() {
  using namespace karma::foliage;
  FoliageLayer layer(32.0f);
  constexpr uint32_t kChunks = 100u;
  constexpr uint32_t kPerChunk = 10000u;
  for (uint32_t chunk = 0u; chunk < kChunks; ++chunk) {
    std::vector<FoliageInstance> instances;
    instances.reserve(kPerChunk);
    for (uint32_t i = 0u; i < kPerChunk; ++i) {
      FoliageInstance value{};
      value.position = {
          static_cast<float>(chunk) * 32.0f +
              static_cast<float>(i % 100u) * 0.25f,
          0.0f,
          static_cast<float>(i / 100u) * 0.25f,
      };
      value.params[0] = static_cast<float>(i);
      instances.push_back(value);
    }
    assert(layer.replaceChunk(
        {static_cast<int32_t>(chunk), 0}, std::move(instances)));
  }
  assert(layer.instanceCount() == 1000000u);
  assert(layer.chunks().size() == kChunks);
  assert(layer.toDocument().instanceCount() == 1000000u);
  assert(!layer.replaceChunk({100, 0}, {instance(3201.0f, 0.0f, 1.0f)}));
  FoliagePaintBrush brush{};
  brush.radius = 1.0f;
  brush.density = 1.0f;
  brush.min_spacing = 0.0f;
  assert(layer.paint(brush, [](float, float) {
                return std::optional<FoliageSurfaceSample>{
                    FoliageSurfaceSample{.normal = {0.0f, 1.0f, 0.0f}}};
              }).empty());
}

karma::world::Entity addPrimaryCamera(karma::world::World& world,
                                      karma::math::Vec3 position) {
  const auto camera = world.createEntity();
  world.add(camera, karma::components::TransformComponent{position});
  karma::components::CameraComponent component{};
  component.is_primary = true;
  world.add(camera, std::move(component));
  return camera;
}

void testWorldLifetimeHandleMoveAndDestruction() {
  using namespace karma;

  {
    world::World first;
    world::World second;
    auto lease = first.lifetimeHandle().lock();
    auto replacement = second.lifetimeHandle().lock();
    assert(lease.get() == &first);
    lease = std::move(replacement);
    assert(lease.get() == &second);
    assert(replacement.get() == nullptr);
  }

  world::World::LifetimeHandle handle;
  uint64_t original_id = 0u;
  {
    world::World original;
    original_id = original.instanceId();
    handle = original.lifetimeHandle();
    {
      auto lease = handle.lock();
      assert(lease.get() == &original);
    }

    world::World moved(std::move(original));
    assert(moved.instanceId() == original_id);
    assert(original.instanceId() != original_id);
    {
      auto lease = handle.lock();
      assert(lease.get() == &moved);
    }
    {
      auto lease = original.lifetimeHandle().lock();
      assert(lease.get() == &original);
    }
  }

  auto expired = handle.lock();
  assert(expired.get() == nullptr);
}

void testRuntimeOverrideResidencyAndCleanup() {
  using namespace karma;
  using namespace karma::foliage;
  world::World world;
  const auto camera = addPrimaryCamera(world, {10.0f, 0.0f, 0.0f});
  const auto source = world.createEntity();
  world.add(source, components::TransformComponent{{10.0f, 0.0f, 0.0f}});
  components::FoliageComponent component{};
  component.mesh_asset_key = "tree";
  component.view_distance = 100.0f;
  component.max_resident_instances = 2u;
  world.add(source, component);

  FoliageDocument document{};
  document.chunk_size = 16.0f;
  document.chunks = {
      {.coord = {0, 0},
       .instances = {instance(1.0f, 2.0f, 1.0f),
                     instance(2.0f, 2.0f, 1.0f),
                     instance(3.0f, 2.0f, 1.0f)}},
      {.coord = {20, 0}, .instances = {instance(321.0f, 0.0f, 1.0f)}},
  };

  FoliageRuntimeModule module;
  module.setLayerOverride(
      source, std::make_shared<const FoliageDocument>(document));
  module.onAttach({});
  module.onUpdate(world, 0.0f, 1.0f);
  const auto stats = module.stats();
  assert(stats.source_count == 1u);
  assert(stats.resident_chunks == 1u);
  assert(stats.resident_instances == 2u);

  const auto& proxies = world.storage<components::InstancedMeshComponent>().denseEntities();
  assert(proxies.size() == 1u);
  const auto proxy_entity = proxies.front();
  const auto& proxy = world.get<components::InstancedMeshComponent>(proxy_entity);
  assert(proxy.gpu_layout == rendering::InstanceGpuLayout::PositionYawScaleParams);
  assert(proxy.planar_instances.size() == 2u);
  assert(proxy.planar_instances[0].position.x == 11.0f);

  world.get<components::TransformComponent>(camera).setPosition({1000.0f, 0.0f, 0.0f});
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.stats().resident_instances == 0u);
  assert(world.get<components::InstancedMeshComponent>(proxy_entity)
             .planar_instances.empty());

  world.destroyEntity(source);
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.stats().source_count == 0u);
  assert(!world.isAlive(proxy_entity));
  module.onDetach();
}

void testRuntimeOverrideValidationAndLifetime() {
  using namespace karma;
  using namespace karma::foliage;

  world::World world;
  addPrimaryCamera(world, {});
  const auto source = world.createEntity();
  components::FoliageComponent component{};
  component.mesh_asset_key = "grass";
  component.view_distance = 100.0f;
  world.add(source, component);

  auto invalid = std::make_shared<FoliageDocument>();
  invalid->chunk_size = 0.0f;
  FoliageRuntimeModule module;
  module.setLayerOverride(source, invalid);
  module.onAttach({});
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.diagnostics().size() == 1u);
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.diagnostics().size() == 1u);

  auto oversized = std::make_shared<FoliageDocument>();
  oversized->chunk_size = 16.0f;
  oversized->chunks.push_back(FoliageChunk{
      .coord = {0, 0},
      .instances = std::vector<FoliageInstance>(
          kMaxAuthoredFoliageInstances + 1u,
          instance(1.0f, 0.0f, 1.0f)),
  });
  module.setLayerOverride(source, oversized);
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.diagnostics().size() == 2u);
  assert(module.diagnostics().back().message.find("authored instance limit") !=
         std::string::npos);
  oversized.reset();

  auto valid = std::make_shared<FoliageDocument>();
  valid->chunk_size = 16.0f;
  valid->chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
      {.coord = {0, 0}, .instances = {instance(2.0f, 0.0f, 1.0f)}},
  };
  std::weak_ptr<const FoliageDocument> lifetime = valid;
  module.setLayerOverride(source, valid);
  valid.reset();
  module.onUpdate(world, 0.0f, 1.0f);
  assert(!lifetime.expired());
  assert(module.stats().resident_instances == 2u);

  world.destroyEntity(source);
  module.onUpdate(world, 0.0f, 1.0f);
  assert(lifetime.expired());
  module.onDetach();
}

void testRuntimeGlobalResidentBudget() {
  using namespace karma;
  using namespace karma::foliage;

  world::World world;
  addPrimaryCamera(world, {});
  auto document = std::make_shared<FoliageDocument>();
  document->chunk_size = 16.0f;
  document->chunks.push_back(FoliageChunk{
      .coord = {0, 0},
      .instances = std::vector<FoliageInstance>(
          60000u, instance(1.0f, 0.0f, 1.0f)),
  });

  FoliageRuntimeModule module;
  for (uint32_t index = 0u; index < 2u; ++index) {
    const auto source = world.createEntity();
    components::FoliageComponent component{};
    component.mesh_asset_key = "grass";
    component.view_distance = 100.0f;
    component.max_resident_instances = 60000u;
    world.add(source, component);
    module.setLayerOverride(source, document);
  }
  module.onAttach({});
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.stats().source_count == 2u);
  assert(module.stats().resident_instances ==
         kDefaultMaxResidentFoliageInstances);

  std::size_t proxy_instances = 0u;
  for (const world::Entity proxy :
       world.storage<components::InstancedMeshComponent>().denseEntities()) {
    proxy_instances +=
        world.get<components::InstancedMeshComponent>(proxy).planar_instances.size();
  }
  assert(proxy_instances == kDefaultMaxResidentFoliageInstances);
  module.onDetach();
}

void testRuntimeWorldSwitchIsolation() {
  using namespace karma;
  using namespace karma::foliage;

  world::World first_world;
  addPrimaryCamera(first_world, {});
  const auto first_source = first_world.createEntity();
  components::FoliageComponent component{};
  component.mesh_asset_key = "grass";
  component.view_distance = 100.0f;
  first_world.add(first_source, component);

  auto document = std::make_shared<FoliageDocument>();
  document->chunk_size = 16.0f;
  document->chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
  };
  std::weak_ptr<const FoliageDocument> lifetime = document;

  FoliageRuntimeModule module;
  module.setLayerOverride(first_source, document);
  document.reset();
  module.onAttach({});
  module.onUpdate(first_world, 0.0f, 1.0f);
  assert(module.stats().resident_instances == 1u);

  world::World second_world;
  addPrimaryCamera(second_world, {});
  const auto second_source = second_world.createEntity();
  assert(second_source == first_source);
  component.sidecar_path = "missing_after_world_switch.kfoliage";
  second_world.add(second_source, component);
  module.onUpdate(second_world, 0.0f, 1.0f);
  assert(module.stats().resident_instances == 0u);
  assert(lifetime.expired());
  module.onDetach();
}

void testRuntimeWorldMoveAndDestructionCleanup() {
  using namespace karma;
  using namespace karma::foliage;

  auto document = std::make_shared<FoliageDocument>();
  document->chunk_size = 16.0f;
  document->chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
  };

  FoliageRuntimeModule module;
  module.onAttach({});
  world::World original;
  addPrimaryCamera(original, {});
  const world::Entity source = original.createEntity();
  components::FoliageComponent component{};
  component.mesh_asset_key = "grass";
  component.view_distance = 100.0f;
  original.add(source, component);
  module.setLayerOverride(source, document);
  module.onUpdate(original, 0.0f, 1.0f);
  assert(module.stats().resident_instances == 1u);
  const auto proxies =
      original.storage<components::InstancedMeshComponent>().denseEntities();
  assert(proxies.size() == 1u);
  const world::Entity proxy = proxies.front();

  world::World moved(std::move(original));
  module.onUpdate(moved, 0.0f, 1.0f);
  const auto moved_proxies =
      moved.storage<components::InstancedMeshComponent>().denseEntities();
  assert(moved_proxies.size() == 1u);
  assert(moved_proxies.front() == proxy);
  assert(moved.isAlive(proxy));
  module.onDetach();
  assert(!moved.isAlive(proxy));

  FoliageRuntimeModule after_world_destruction;
  after_world_destruction.onAttach({});
  {
    world::World temporary;
    addPrimaryCamera(temporary, {});
    const world::Entity temporary_source = temporary.createEntity();
    temporary.add(temporary_source, component);
    after_world_destruction.setLayerOverride(temporary_source, document);
    after_world_destruction.onUpdate(temporary, 0.0f, 1.0f);
    assert(after_world_destruction.stats().source_count == 1u);
  }
  // Detach after the owning world has died must not dereference its old
  // address. The lifetime handle simply suppresses proxy cleanup in that case.
  after_world_destruction.onDetach();
  assert(after_world_destruction.stats().source_count == 0u);
}

void testRuntimeQueuedLimitGrowthRequeues() {
  using namespace karma;
  using namespace karma::foliage;

  const auto dir = makeTempDir();
  FoliageDocument blocker{};
  blocker.chunk_size = 16.0f;
  blocker.chunks.push_back(FoliageChunk{
      .coord = {0, 0},
      .instances = std::vector<FoliageInstance>(
          kDefaultMaxResidentFoliageInstances - 1u,
          instance(1.0f, 0.0f, 1.0f)),
  });
  FoliageDocument target{};
  target.chunk_size = 16.0f;
  target.chunks = {
      {.coord = {0, 0},
       .instances = {instance(2.0f, 0.0f, 1.0f),
                     instance(3.0f, 0.0f, 1.0f)}},
  };
  std::string error;
  assert(writeFoliageFile(dir / "blocker.kfoliage", blocker, &error));
  assert(writeFoliageFile(dir / "target.kfoliage", target, &error));

  world::World world;
  addPrimaryCamera(world, {});
  const world::Entity blocker_source = world.createEntity();
  components::FoliageComponent blocker_component{};
  blocker_component.sidecar_path = "blocker.kfoliage";
  blocker_component.mesh_asset_key = "grass";
  blocker_component.chunk_size = 16.0f;
  blocker_component.view_distance = 100.0f;
  blocker_component.max_resident_instances =
      kDefaultMaxResidentFoliageInstances - 1u;
  world.add(blocker_source, blocker_component);

  const world::Entity target_source = world.createEntity();
  components::FoliageComponent target_component{};
  target_component.sidecar_path = "target.kfoliage";
  target_component.mesh_asset_key = "grass";
  target_component.chunk_size = 16.0f;
  target_component.view_distance = 100.0f;
  target_component.max_resident_instances = 1u;
  world.add(target_source, target_component);

  FoliageRuntimeModule module;
  module.setReferenceRoot(dir);
  module.onAttach({});
  module.onUpdate(world, 0.0f, 1.0f);

  // Free the global budget and grow the target while its one-instance request
  // is queued behind the large blocker request.
  world.get<components::FoliageComponent>(blocker_source).visible = false;
  world.get<components::FoliageComponent>(target_source)
      .max_resident_instances = 2u;
  module.onUpdate(world, 0.0f, 1.0f);
  for (int attempt = 0; attempt < 1000; ++attempt) {
    module.onUpdate(world, 0.0f, 1.0f);
    if (module.stats().resident_instances == 2u) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(module.stats().resident_instances == 2u);
  assert(module.diagnostics().empty());
  module.onDetach();
  std::filesystem::remove_all(dir);
}

void testRuntimeFailedChunkDoesNotConsumeBudget() {
  using namespace karma;
  using namespace karma::foliage;

  const auto dir = makeTempDir();
  const auto path = dir / "one_corrupt_chunk.kfoliage";
  FoliageDocument document{};
  document.chunk_size = 16.0f;
  document.chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
      {.coord = {1, 0}, .instances = {instance(17.0f, 0.0f, 1.0f)}},
  };
  std::string error;
  assert(writeFoliageFile(path, document, &error));
  const auto index = readFoliageFileIndex(path, &error);
  assert(index.has_value());
  assert(index->chunks.size() == 2u);
  std::vector<uint8_t> bytes = readBytes(path);
  const std::size_t payload =
      static_cast<std::size_t>(index->chunks.front().data_offset);
  assert(payload + 4u <= bytes.size());
  bytes[payload + 0u] = 0x00u;
  bytes[payload + 1u] = 0x00u;
  bytes[payload + 2u] = 0xC0u;
  bytes[payload + 3u] = 0x7Fu;  // quiet NaN in little-endian f32
  writeBytes(path, bytes);

  world::World world;
  addPrimaryCamera(world, {});
  const world::Entity source = world.createEntity();
  components::FoliageComponent component{};
  component.sidecar_path = path.filename();
  component.mesh_asset_key = "grass";
  component.chunk_size = 16.0f;
  component.view_distance = 100.0f;
  component.max_resident_instances = 1u;
  world.add(source, component);

  FoliageRuntimeModule module;
  module.setReferenceRoot(dir);
  module.onAttach({});
  for (int attempt = 0; attempt < 1000; ++attempt) {
    module.onUpdate(world, 0.0f, 1.0f);
    if (module.stats().resident_instances == 1u) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(module.stats().resident_instances == 1u);
  assert(module.diagnostics().size() == 1u);
  const auto& proxies =
      world.storage<components::InstancedMeshComponent>().denseEntities();
  assert(proxies.size() == 1u);
  const auto& proxy =
      world.get<components::InstancedMeshComponent>(proxies.front());
  assert(proxy.planar_instances.size() == 1u);
  assert(proxy.planar_instances.front().position.x == 17.0f);
  module.onDetach();
  std::filesystem::remove_all(dir);
}

void testRuntimeRejectsInvalidRendererStateOnce() {
  using namespace karma;
  using namespace karma::foliage;

  world::World world;
  addPrimaryCamera(world, {});
  const world::Entity source = world.createEntity();
  components::FoliageComponent component{};
  component.mesh_asset_key = "grass";
  component.view_distance = 100.0f;
  component.lods = {{.start_distance =
                          std::numeric_limits<float>::quiet_NaN(),
                     .mesh_asset_key = "grass_lod"}};
  world.add(source, component);
  auto document = std::make_shared<FoliageDocument>();
  document->chunk_size = 16.0f;
  document->chunks = {
      {.coord = {0, 0}, .instances = {instance(1.0f, 0.0f, 1.0f)}},
  };

  FoliageRuntimeModule module;
  module.setLayerOverride(source, document);
  module.onAttach({});
  module.onUpdate(world, 0.0f, 1.0f);
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.diagnostics().size() == 1u);
  assert(module.diagnostics().front().message.find("LOD distances") !=
         std::string::npos);
  const auto& proxies =
      world.storage<components::InstancedMeshComponent>().denseEntities();
  assert(proxies.size() == 1u);
  const world::Entity proxy_entity = proxies.front();
  const auto& suppressed =
      world.get<components::InstancedMeshComponent>(proxy_entity);
  assert(!suppressed.visible);
  assert(suppressed.mesh_asset_key.empty());
  assert(suppressed.lods.empty());
  assert(suppressed.planar_instances.empty());

  world.get<components::FoliageComponent>(source).lods.front().start_distance =
      10.0f;
  module.onUpdate(world, 0.0f, 1.0f);
  assert(module.stats().resident_instances == 1u);
  const auto& restored =
      world.get<components::InstancedMeshComponent>(proxy_entity);
  assert(restored.visible);
  assert(restored.mesh_asset_key == "grass");
  assert(restored.lods.size() == 1u);
  module.onDetach();
}

void testRuntimeFileStreamingAndDiagnostics() {
  using namespace karma;
  using namespace karma::foliage;
  const auto dir = makeTempDir();
  FoliageDocument document{};
  document.chunk_size = 16.0f;
  document.chunks = {
      {.coord = {0, 0},
       .instances = {instance(1.0f, 0.0f, 1.0f),
                     instance(2.0f, 0.0f, 1.0f)}},
  };
  std::string error;
  assert(writeFoliageFile(dir / "layer.kfoliage", document, &error));

  world::World world;
  addPrimaryCamera(world, {});
  const auto source = world.createEntity();
  components::FoliageComponent component{};
  component.sidecar_path = "layer.kfoliage";
  component.mesh_asset_key = "grass";
  component.chunk_size = 16.0f;
  component.view_distance = 100.0f;
  world.add(source, component);

  FoliageRuntimeModule module;
  module.setReferenceRoot(dir);
  module.onAttach({});
  for (int attempt = 0; attempt < 200; ++attempt) {
    module.onUpdate(world, 0.0f, 1.0f);
    if (module.stats().resident_instances == 2u) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(module.stats().resident_instances == 2u);
  module.onDetach();

  world::World corrupt_world;
  addPrimaryCamera(corrupt_world, {});
  const auto corrupt_source = corrupt_world.createEntity();
  component.sidecar_path = "missing.kfoliage";
  corrupt_world.add(corrupt_source, component);
  FoliageRuntimeModule corrupt_module;
  corrupt_module.setReferenceRoot(dir);
  corrupt_module.onAttach({});
  corrupt_module.onUpdate(corrupt_world, 0.0f, 1.0f);
  assert(!corrupt_module.diagnostics().empty());
  corrupt_module.onDetach();
  std::filesystem::remove_all(dir);
}

void testComponentSerializerRoundTripAndValidation() {
  using namespace karma;
  prefabs::ComponentSerializerRegistry registry;
  prefabs::registerBuiltinComponentSerializers(registry);
  const prefabs::ComponentSerializer* serializer =
      registry.find("FoliageComponent");
  assert(serializer != nullptr);

  world::World source_world;
  const world::Entity source = source_world.createEntity();
  components::FoliageComponent authored{};
  authored.sidecar_path = "forest/layers/oaks.kfoliage";
  authored.mesh_asset_key = "forest/oak_near";
  authored.materials = {{.slot = 0u, .material_key = "forest/oak_bark"}};
  authored.lods = {
      {.start_distance = 48.0f,
       .mesh_asset_key = "forest/oak_mid",
       .materials = {{.slot = 0u, .material_key = "forest/oak_atlas"}},
       .render_mode = rendering::InstanceLodRenderMode::Mesh,
       .shadow_visible = true},
      {.start_distance = 128.0f,
       .mesh_asset_key = "forest/oak_billboard",
       .materials = {{.slot = 0u, .material_key = "forest/oak_atlas"}},
       .render_mode = rendering::InstanceLodRenderMode::UprightBillboard,
       .shadow_visible = false},
  };
  authored.chunk_size = 24.0f;
  authored.view_distance = 320.0f;
  authored.max_resident_instances = 54321u;
  authored.source_revision = 17u;
  authored.visible = false;
  authored.shadow_visible = false;
  std::string validation_error;
  assert(foliage::validateFoliageComponent(authored, &validation_error));
  assert(validation_error.empty());
  source_world.add(source, authored);

  assert(serializer->has(source_world, source));
  const nlohmann::json serialized = serializer->serialize(source_world, source);
  assert(serialized["sidecar_path"] == "forest/layers/oaks.kfoliage");
  assert(serialized["mesh_asset_key"] == "forest/oak_near");
  assert(serialized["materials"][0]["material_key"] == "forest/oak_bark");
  assert(serialized["lods"].size() == 2u);
  assert(serialized["lods"][1]["render_mode"] == "upright_billboard");
  assert(serialized["max_resident_instances"] == 54321u);
  assert(serialized["source_revision"] == 17u);

  world::World loaded_world;
  const world::Entity loaded_entity = loaded_world.createEntity();
  assert(serializer->deserialize(loaded_world, loaded_entity, serialized));
  const auto& loaded =
      loaded_world.get<components::FoliageComponent>(loaded_entity);
  assert(loaded.sidecar_path.generic_string() ==
         "forest/layers/oaks.kfoliage");
  assert(loaded.mesh_asset_key == authored.mesh_asset_key);
  assert(loaded.materials == authored.materials);
  assert(loaded.lods.size() == 2u);
  assert(loaded.lods[0].start_distance == 48.0f);
  assert(loaded.lods[0].mesh_asset_key == "forest/oak_mid");
  assert(loaded.lods[0].materials == authored.lods[0].materials);
  assert(loaded.lods[0].shadow_visible);
  assert(loaded.lods[1].render_mode ==
         rendering::InstanceLodRenderMode::UprightBillboard);
  assert(loaded.chunk_size == authored.chunk_size);
  assert(loaded.view_distance == authored.view_distance);
  assert(loaded.max_resident_instances == authored.max_resident_instances);
  assert(loaded.source_revision == authored.source_revision);
  assert(!loaded.visible);
  assert(!loaded.shadow_visible);

  const auto rejects = [&](nlohmann::json payload) {
    world::World rejected_world;
    const world::Entity rejected_entity = rejected_world.createEntity();
    assert(!serializer->deserialize(rejected_world, rejected_entity, payload));
    assert(!rejected_world.has<components::FoliageComponent>(rejected_entity));
  };

  nlohmann::json invalid = serialized;
  invalid["sidecar_path"] = "";
  rejects(invalid);
  invalid = serialized;
  invalid["sidecar_path"] = "/absolute/forest.kfoliage";
  rejects(invalid);
  invalid = serialized;
  invalid["sidecar_path"] = "forest/../outside.kfoliage";
  rejects(invalid);
  invalid = serialized;
  invalid["sidecar_path"] = "C:/forest/oaks.kfoliage";
  rejects(invalid);
  invalid = serialized;
  invalid["sidecar_path"] = "forest\\oaks.kfoliage";
  rejects(invalid);
  invalid = serialized;
  invalid["mesh_asset_key"] = "";
  rejects(invalid);
  invalid = serialized;
  invalid["chunk_size"] = std::numeric_limits<double>::infinity();
  rejects(invalid);
  invalid = serialized;
  invalid["view_distance"] = 0.0f;
  rejects(invalid);
  invalid = serialized;
  invalid["max_resident_instances"] = 0u;
  rejects(invalid);
  invalid = serialized;
  invalid["lods"][1]["start_distance"] = 48.0f;
  rejects(invalid);
  invalid = serialized;
  invalid["lods"][0]["mesh_asset_key"] = "";
  rejects(invalid);
  invalid = serialized;
  invalid["lods"].push_back(serialized["lods"][1]);
  invalid["lods"].push_back(serialized["lods"][1]);
  rejects(invalid);

  components::FoliageComponent direct = authored;
  direct.sidecar_path.clear();
  assert(foliage::validateFoliageComponent(direct, &validation_error));
  direct.lods[0].start_distance =
      std::numeric_limits<float>::quiet_NaN();
  assert(!foliage::validateFoliageComponent(direct, &validation_error));
  assert(validation_error.find("LOD distances") != std::string::npos);

  direct = authored;
  direct.lods[0].render_mode =
      static_cast<rendering::InstanceLodRenderMode>(255u);
  assert(!foliage::validateFoliageComponent(direct, &validation_error));
  assert(validation_error.find("render mode") != std::string::npos);

  direct = authored;
  direct.materials[0].material_key.clear();
  assert(!foliage::validateFoliageComponent(direct, &validation_error));
  assert(validation_error.find("material key") != std::string::npos);
}

}  // namespace

int main() {
  testCanonicalRoundTripAndIndex();
  testFileValidationFailures();
  testDeterministicPaintEraseAndUndo();
  testPaintFiniteExtremeRanges();
  testMillionInstanceChunkIndex();
  testWorldLifetimeHandleMoveAndDestruction();
  testRuntimeOverrideResidencyAndCleanup();
  testRuntimeOverrideValidationAndLifetime();
  testRuntimeGlobalResidentBudget();
  testRuntimeWorldSwitchIsolation();
  testRuntimeWorldMoveAndDestructionCleanup();
  testRuntimeQueuedLimitGrowthRequeues();
  testRuntimeFailedChunkDoesNotConsumeBudget();
  testRuntimeRejectsInvalidRendererStateOnce();
  testRuntimeFileStreamingAndDiagnostics();
  testComponentSerializerRoundTripAndValidation();
  return 0;
}
