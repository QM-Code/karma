#include "karma/rendering.h"

#include "karma/core.h"
#include "private/rendering/backend.hpp"

#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace karma::rendering {
namespace {

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

RendererExecutionMode resolveExecutionMode(GraphicsDeviceCreateInfo create_info) {
  RendererExecutionMode mode = create_info.execution_mode;
  if (const char* value = std::getenv("KARMA_RENDER_THREAD")) {
    mode = envFlagEnabled(value) ? RendererExecutionMode::Threaded
                                : RendererExecutionMode::Synchronous;
  }
#if !defined(KARMA_RENDER_BACKEND_DILIGENT)
  mode = RendererExecutionMode::Synchronous;
#endif
  return mode;
}

struct OwnedInstancedDrawItem {
  InstanceId instance = kInvalidInstance;
  MeshId mesh = kInvalidMesh;
  MaterialId material = kInvalidMaterial;
  std::vector<DrawMaterialBinding> materials;
  std::vector<std::string> render_tags;
  std::vector<InstancedLodDrawDesc> lods;
  InstanceGpuLayout gpu_layout = InstanceGpuLayout::Matrix4x4Params;
  std::vector<InstanceData> instances;
  std::vector<PlanarInstanceData> planar_instances;
  bool payload_changed = true;
  uint64_t revision = 0;
  glm::vec3 bounds_center{0.0f};
  float bounds_radius = 0.0f;
  bool bounds_valid = false;
  LayerId layer = 0;
  bool dynamic = false;
  bool visible = true;
  bool shadow_visible = true;

  explicit OwnedInstancedDrawItem(InstancedDrawItem&& item)
      : instance(item.instance),
        mesh(item.mesh),
        material(item.material),
        materials(std::move(item.materials)),
        render_tags(std::move(item.render_tags)),
        lods(std::move(item.lods)),
        gpu_layout(item.gpu_layout),
        instances(item.instances.begin(), item.instances.end()),
        planar_instances(item.planar_instances.begin(), item.planar_instances.end()),
        payload_changed(item.payload_changed),
        revision(item.revision),
        bounds_center(item.bounds_center),
        bounds_radius(item.bounds_radius),
        bounds_valid(item.bounds_valid),
        layer(item.layer),
        dynamic(item.dynamic),
        visible(item.visible),
        shadow_visible(item.shadow_visible) {}

  void submit(rendering::backend::Backend& backend) {
    InstancedDrawItem item{};
    item.instance = instance;
    item.mesh = mesh;
    item.material = material;
    item.materials = std::move(materials);
    item.render_tags = std::move(render_tags);
    item.lods = std::move(lods);
    item.gpu_layout = gpu_layout;
    item.instances = std::span<const InstanceData>(instances.data(), instances.size());
    item.planar_instances =
        std::span<const PlanarInstanceData>(planar_instances.data(), planar_instances.size());
    item.payload_changed = payload_changed;
    item.revision = revision;
    item.bounds_center = bounds_center;
    item.bounds_radius = bounds_radius;
    item.bounds_valid = bounds_valid;
    item.layer = layer;
    item.dynamic = dynamic;
    item.visible = visible;
    item.shadow_visible = shadow_visible;
    backend.submitInstanced(item);
  }
};

}  // namespace

class GraphicsDevice::RenderScheduler {
 public:
  using Backend = rendering::backend::Backend;
  using RenderCommand = std::function<void(Backend&)>;

  RenderScheduler(karma::platform::Window& window, const GraphicsDeviceCreateInfo& create_info)
      : threaded_(resolveExecutionMode(create_info) == RendererExecutionMode::Threaded) {
    if (threaded_) {
      startRenderThread(window, create_info);
    } else {
      backend_ = rendering::backend::CreateGraphicsBackend(window, create_info);
      {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        backend_available_ = backend_ != nullptr && backend_->isValid();
      }
      refreshCachedStats(0.0f, 0.0f, 0.0f);
    }
  }

  ~RenderScheduler() {
    if (!threaded_) {
      backend_.reset();
      return;
    }

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stopping_ = true;
    }
    queue_cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  bool hasBackend() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return backend_available_;
  }

  Backend* backendIfSynchronous() {
    return threaded_ ? nullptr : backend_.get();
  }

  const Backend* backendIfSynchronous() const {
    return threaded_ ? nullptr : backend_.get();
  }

  void beginFrame(const FrameInfo& frame) {
    std::lock_guard<std::mutex> lock(record_mutex_);
    if (current_frame_) {
      throw std::logic_error("GraphicsDevice::beginFrame called before endFrame");
    }
    current_frame_.emplace();
    current_frame_->commands = std::move(recycled_frame_commands_);
    current_frame_->commands.clear();
    current_frame_->record_start = core::SteadyClock::now();
    current_frame_->commands.push_back([frame](Backend& backend) {
      backend.beginFrame(frame);
    });
    current_frame_->commands.insert(current_frame_->commands.end(),
                                    std::make_move_iterator(pre_frame_commands_.begin()),
                                    std::make_move_iterator(pre_frame_commands_.end()));
    pre_frame_commands_.clear();
  }

  void recordFrameCommand(RenderCommand command) {
    std::lock_guard<std::mutex> lock(record_mutex_);
    if (current_frame_) {
      current_frame_->commands.push_back(std::move(command));
    } else {
      pre_frame_commands_.push_back(std::move(command));
    }
  }

  bool recordFrameCommandIfActive(RenderCommand command) {
    std::lock_guard<std::mutex> lock(record_mutex_);
    if (!current_frame_) {
      return false;
    }
    current_frame_->commands.push_back(std::move(command));
    return true;
  }

  void endFrame(bool wait_for_completion) {
    FramePacket packet;
    {
      std::lock_guard<std::mutex> lock(record_mutex_);
      if (!current_frame_) {
        return;
      }
      current_frame_->commands.push_back([](Backend& backend) {
        backend.endFrame();
      });
      current_frame_->record_ms =
          static_cast<float>(core::elapsedMillisecondsSince(current_frame_->record_start));
      packet = std::move(*current_frame_);
      current_frame_.reset();
    }

    if (wait_for_completion || !threaded_) {
      submitFrameAndWait(std::move(packet));
    } else {
      submitLatestFrame(std::move(packet));
    }
  }

  void waitIdle() {
    if (!threaded_) {
      return;
    }
    invokeVoid([](Backend*) {});
  }

  void resize(int width, int height) {
    invokeVoid([width, height](Backend* backend) {
      if (backend != nullptr) {
        backend->resize(width, height);
      }
    });
  }

  void prewarmRendererResources(bool include_ui) {
    invokeVoid([include_ui](Backend* backend) {
      if (backend != nullptr) {
        backend->prewarmRendererResources(include_ui);
      }
    });
  }

  void flushRenderStateCache() {
    invokeVoid([](Backend* backend) {
      if (backend != nullptr) {
        backend->flushRenderStateCache();
      }
    });
  }

  template <typename F>
  auto invoke(F&& fn) -> std::invoke_result_t<F, Backend*> {
    using Result = std::invoke_result_t<F, Backend*>;
    if (!threaded_ || std::this_thread::get_id() == render_thread_id_) {
      if constexpr (std::is_void_v<Result>) {
        fn(backend_.get());
        if (!threaded_) {
          refreshCachedStats(0.0f, 0.0f, 0.0f);
        }
        return;
      } else {
        Result result = fn(backend_.get());
        if (!threaded_) {
          refreshCachedStats(0.0f, 0.0f, 0.0f);
        }
        return result;
      }
    }

    auto promise = std::make_shared<std::promise<Result>>();
    auto future = promise->get_future();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      work_queue_.push_back([this, fn = std::forward<F>(fn), promise]() mutable {
        try {
          if constexpr (std::is_void_v<Result>) {
            fn(backend_.get());
            promise->set_value();
          } else {
            promise->set_value(fn(backend_.get()));
          }
        } catch (...) {
          promise->set_exception(std::current_exception());
        }
      });
    }
    queue_cv_.notify_one();

    const auto wait_start = core::SteadyClock::now();
    if constexpr (std::is_void_v<Result>) {
      future.get();
      recordCommandWait(core::elapsedMillisecondsSince(wait_start));
      return;
    } else {
      Result result = future.get();
      recordCommandWait(core::elapsedMillisecondsSince(wait_start));
      return result;
    }
  }

  template <typename F>
  void invokeVoid(F&& fn) {
    invoke(std::forward<F>(fn));
  }

  ForwardPlusStats getForwardPlusStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return forward_plus_stats_;
  }

  InstancingStats getInstancingStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return instancing_stats_;
  }

  ParticlePassStats getParticlePassStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return particle_pass_stats_;
  }

  RendererCommandStats getRendererCommandStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return command_stats_;
  }

  RendererFrameTimingStats getRendererFrameTimingStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return frame_timing_stats_;
  }

 private:
  struct FramePacket {
    FramePacket() : commands(), record_start(), record_ms(0.0f), submit_ms(0.0f) {}

    std::vector<RenderCommand> commands;
    core::SteadyClock::time_point record_start{};
    float record_ms = 0.0f;
    float submit_ms = 0.0f;
  };

  void startRenderThread(karma::platform::Window& window,
                         const GraphicsDeviceCreateInfo& create_info) {
    auto init_promise = std::make_shared<std::promise<void>>();
    auto init_future = init_promise->get_future();
    worker_ = std::thread([this, &window, create_info, init_promise]() mutable {
      render_thread_id_ = std::this_thread::get_id();
      try {
        backend_ = rendering::backend::CreateGraphicsBackend(window, create_info);
        {
          std::lock_guard<std::mutex> lock(stats_mutex_);
          backend_available_ = backend_ != nullptr && backend_->isValid();
        }
        refreshCachedStats(0.0f, 0.0f, 0.0f);
        init_promise->set_value();
      } catch (...) {
        init_promise->set_exception(std::current_exception());
        return;
      }
      workerLoop();
      backend_.reset();
      {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        backend_available_ = false;
      }
    });
    try {
      init_future.get();
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        stopping_ = true;
      }
      queue_cv_.notify_all();
      if (worker_.joinable()) {
        worker_.join();
      }
      throw;
    }
  }

  void workerLoop() {
    while (true) {
      std::optional<FramePacket> frame;
      std::function<void()> work;
      const auto wait_start = core::SteadyClock::now();
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] {
          return stopping_ || pending_frame_.has_value() || !work_queue_.empty();
        });
        pending_render_thread_wait_ms_ +=
            static_cast<float>(core::elapsedMillisecondsSince(wait_start));

        if (pending_frame_) {
          frame = std::move(pending_frame_);
          pending_frame_.reset();
        } else if (!work_queue_.empty()) {
          work = std::move(work_queue_.front());
          work_queue_.pop_front();
        } else if (stopping_) {
          break;
        }
      }

      if (frame) {
        executeFrame(*frame);
      } else if (work) {
        work();
      }
    }
  }

  void executeFrame(FramePacket& packet) {
    const auto frame_start = core::SteadyClock::now();
    if (backend_ != nullptr) {
      for (const RenderCommand& command : packet.commands) {
        command(*backend_);
      }
    }
    const float frame_ms =
        static_cast<float>(core::elapsedMillisecondsSince(frame_start));
    refreshCachedStats(packet.record_ms, packet.submit_ms, frame_ms);
    packet.commands.clear();
    std::lock_guard<std::mutex> lock(record_mutex_);
    if (packet.commands.capacity() > recycled_frame_commands_.capacity()) {
      recycled_frame_commands_ = std::move(packet.commands);
    }
  }

  void submitLatestFrame(FramePacket packet) {
    const auto submit_start = core::SteadyClock::now();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      ++submitted_frames_;
      if (pending_frame_) {
        ++dropped_frames_;
      }
      packet.submit_ms =
          static_cast<float>(core::elapsedMillisecondsSince(submit_start));
      pending_frame_ = std::move(packet);
    }
    queue_cv_.notify_one();
  }

  void submitFrameAndWait(FramePacket packet) {
    const auto submit_start = core::SteadyClock::now();
    packet.submit_ms = static_cast<float>(core::elapsedMillisecondsSince(submit_start));
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      ++submitted_frames_;
    }
    if (!threaded_) {
      executeFrame(packet);
      return;
    }
    invokeVoid([this, packet = std::move(packet)](Backend*) mutable {
      executeFrame(packet);
    });
  }

  void recordCommandWait(double wait_ms) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    pending_command_wait_ms_ += static_cast<float>(wait_ms);
  }

  void refreshCachedStats(float record_ms, float submit_ms, float render_thread_frame_ms) {
    RendererFrameTimingStats timing{};
    ForwardPlusStats forward_plus{};
    InstancingStats instancing{};
    ParticlePassStats particles{};
    RendererCommandStats commands{};
    if (backend_ != nullptr) {
      timing = backend_->getRendererFrameTimingStats();
      forward_plus = backend_->getForwardPlusStats();
      instancing = backend_->getInstancingStats();
      particles = backend_->getParticlePassStats();
      commands = backend_->getRendererCommandStats();
    }

    uint32_t queue_depth = 0;
    uint64_t submitted = 0;
    uint64_t completed = 0;
    uint64_t dropped = 0;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      queue_depth = static_cast<uint32_t>((pending_frame_ ? 1u : 0u) + work_queue_.size());
      submitted = submitted_frames_;
      dropped = dropped_frames_;
      if (render_thread_frame_ms > 0.0f) {
        ++completed_frames_;
      }
      completed = completed_frames_;
    }

    float wait_ms = 0.0f;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      wait_ms = pending_render_thread_wait_ms_;
      pending_render_thread_wait_ms_ = 0.0f;
    }

    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      const float command_wait_ms = pending_command_wait_ms_;
      pending_command_wait_ms_ = 0.0f;
      timing.submitted_frames = submitted;
      timing.completed_frames = completed;
      timing.dropped_frames = dropped;
      timing.render_queue_depth = queue_depth;
      timing.frame_record_ms = record_ms;
      timing.frame_submit_ms = submit_ms;
      timing.render_thread_wait_ms = wait_ms;
      timing.render_thread_frame_ms = render_thread_frame_ms;
      timing.render_thread_command_wait_ms = command_wait_ms;
      frame_timing_stats_ = timing;
      forward_plus_stats_ = forward_plus;
      instancing_stats_ = instancing;
      particle_pass_stats_ = particles;
      command_stats_ = commands;
    }
  }

  bool threaded_ = false;
  std::unique_ptr<Backend> backend_;
  std::thread worker_;
  std::thread::id render_thread_id_{};

  mutable std::mutex record_mutex_;
  std::optional<FramePacket> current_frame_;
  std::vector<RenderCommand> pre_frame_commands_;
  std::vector<RenderCommand> recycled_frame_commands_;

  mutable std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  std::deque<std::function<void()>> work_queue_;
  std::optional<FramePacket> pending_frame_;
  bool stopping_ = false;
  float pending_render_thread_wait_ms_ = 0.0f;
  uint64_t submitted_frames_ = 0;
  uint64_t completed_frames_ = 0;
  uint64_t dropped_frames_ = 0;

  mutable std::mutex stats_mutex_;
  bool backend_available_ = false;
  ForwardPlusStats forward_plus_stats_{};
  InstancingStats instancing_stats_{};
  ParticlePassStats particle_pass_stats_{};
  RendererCommandStats command_stats_{};
  RendererFrameTimingStats frame_timing_stats_{};
  float pending_command_wait_ms_ = 0.0f;
};

GraphicsDevice::GraphicsDevice(karma::platform::Window& window,
                               const GraphicsDeviceCreateInfo& create_info) {
  scheduler_ = std::make_unique<RenderScheduler>(window, create_info);
}

GraphicsDevice::~GraphicsDevice() = default;

bool GraphicsDevice::isValid() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ != nullptr && scheduler_->hasBackend();
}

void GraphicsDevice::beginFrame(const FrameInfo& frame) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  framebuffer_width_ = frame.width;
  framebuffer_height_ = frame.height;
  if (scheduler_) {
    scheduler_->beginFrame(frame);
  }
}

void GraphicsDevice::endFrame(bool wait_for_completion) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->endFrame(wait_for_completion);
  }
}

void GraphicsDevice::waitIdle() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->waitIdle();
  }
}

void GraphicsDevice::resize(int width, int height) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  framebuffer_width_ = width;
  framebuffer_height_ = height;
  if (scheduler_) {
    scheduler_->resize(width, height);
  }
}

void GraphicsDevice::prewarmRendererResources(bool include_ui) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    if (!scheduler_->recordFrameCommandIfActive(
            [include_ui](RenderScheduler::Backend& backend) {
              backend.prewarmRendererResources(include_ui);
            })) {
      scheduler_->prewarmRendererResources(include_ui);
    }
  }
}

void GraphicsDevice::flushRenderStateCache() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->flushRenderStateCache();
  }
}

void GraphicsDevice::getFramebufferSize(int& width, int& height) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  width = framebuffer_width_;
  height = framebuffer_height_;
}

MeshId GraphicsDevice::createMesh(const world::MeshData& mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([&](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createMesh(mesh) : kInvalidMesh;
  }) : kInvalidMesh;
}

void GraphicsDevice::updateMesh(MeshId mesh, const world::MeshData& data) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([&](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->updateMesh(mesh, data);
      }
    });
  }
}

void GraphicsDevice::destroyMesh(MeshId mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([mesh](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyMesh(mesh);
      }
    });
  }
}

bool GraphicsDevice::getMeshBounds(MeshId mesh, glm::vec3& center, float& radius) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([&](RenderScheduler::Backend* backend) {
    return backend != nullptr && backend->getMeshBounds(mesh, center, radius);
  }) : false;
}

bool GraphicsDevice::getMeshMaterialSlots(
    MeshId mesh,
    std::vector<world::MeshMaterialSlot>& out_slots) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!scheduler_) {
    out_slots.clear();
    return false;
  }
  return scheduler_->invoke([&](RenderScheduler::Backend* backend) {
    if (backend == nullptr) {
      out_slots.clear();
      return false;
    }
    return backend->getMeshMaterialSlots(mesh, out_slots);
  });
}

MeshId GraphicsDevice::registerRuntimeMesh(const std::string& key,
                                           const world::MeshData& mesh) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (key.empty() || scheduler_ == nullptr || !scheduler_->hasBackend()) {
    return kInvalidMesh;
  }

  auto it = runtime_meshes_.find(key);
  if (it != runtime_meshes_.end()) {
    updateMesh(it->second.mesh, mesh);
    it->second.data = mesh;
    return it->second.mesh;
  }

  const MeshId id = createMesh(mesh);
  if (id != kInvalidMesh) {
    runtime_meshes_.emplace(key, RuntimeMeshRegistration{.mesh = id, .data = mesh});
  }
  return id;
}

void GraphicsDevice::unregisterRuntimeMesh(const std::string& key) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto it = runtime_meshes_.find(key);
  if (it == runtime_meshes_.end()) {
    return;
  }
  if (it->second.mesh != kInvalidMesh) {
    destroyMesh(it->second.mesh);
  }
  runtime_meshes_.erase(it);
}

MeshId GraphicsDevice::findRuntimeMesh(const std::string& key) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const auto it = runtime_meshes_.find(key);
  return it != runtime_meshes_.end() ? it->second.mesh : kInvalidMesh;
}

MaterialId GraphicsDevice::createMaterial(const ResolvedMaterialDesc& material) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([&](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createMaterial(material) : kInvalidMaterial;
  }) : kInvalidMaterial;
}

MaterialId GraphicsDevice::createMaterial(const MaterialDesc& material) {
  return createMaterial(ResolvedMaterialDesc::fromSurface(material));
}

MaterialId GraphicsDevice::createMaterialFromAsset(const std::filesystem::path& path,
                                                   uint32_t material_index) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([&](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createMaterialFromAsset(path, material_index)
                              : kInvalidMaterial;
  }) : kInvalidMaterial;
}

void GraphicsDevice::updateMaterial(MaterialId material, const MaterialDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([&](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->updateMaterial(material, desc);
      }
    });
  }
}

void GraphicsDevice::destroyMaterial(MaterialId material) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([material](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyMaterial(material);
      }
    });
  }
}

void GraphicsDevice::setMaterialFloat(MaterialId material, std::string_view name, float value) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  const std::string name_copy{name};
  if (scheduler_) {
    scheduler_->invokeVoid([material, name_copy, value](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setMaterialFloat(material, name_copy, value);
      }
    });
  }
}

TextureId GraphicsDevice::createTexture(const TextureDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([desc](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createTexture(desc) : kInvalidTexture;
  }) : kInvalidTexture;
}

bool GraphicsDevice::supportsTextureFormat(TextureFormat format) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([format](RenderScheduler::Backend* backend) {
    return backend != nullptr && backend->supportsTextureFormat(format);
  }) : false;
}

bool GraphicsDevice::uploadTexture(TextureId texture, const TextureUploadData& upload) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([texture, &upload](RenderScheduler::Backend* backend) {
    return backend != nullptr && backend->uploadTexture(texture, upload);
  }) : false;
}

std::vector<TextureUploadBatchResult> GraphicsDevice::createAndUploadTextures(
    std::vector<TextureUploadBatchRequest> requests) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!scheduler_ || requests.empty()) {
    return {};
  }
  return scheduler_->invoke([requests = std::move(requests)](
                                RenderScheduler::Backend* backend) mutable {
    std::vector<TextureUploadBatchResult> results;
    results.reserve(requests.size());
    if (backend == nullptr) {
      results.resize(requests.size());
      return results;
    }
    for (TextureUploadBatchRequest& request : requests) {
      TextureUploadBatchResult result{};
      if (!validateTextureUpload(request.desc, request.upload)) {
        results.push_back(result);
        continue;
      }
      const auto create_start = core::SteadyClock::now();
      result.texture = backend->createTexture(request.desc);
      result.create_ms =
          static_cast<float>(core::elapsedMillisecondsSince(create_start));
      if (result.texture != kInvalidTexture) {
        const auto upload_start = core::SteadyClock::now();
        result.uploaded = backend->uploadTexture(result.texture, request.upload);
        result.upload_ms =
            static_cast<float>(core::elapsedMillisecondsSince(upload_start));
        if (!result.uploaded) {
          backend->destroyTexture(result.texture);
          result.texture = kInvalidTexture;
        }
      }
      results.push_back(result);
    }
    return results;
  });
}

void GraphicsDevice::destroyTexture(TextureId texture) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([texture](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyTexture(texture);
      }
    });
  }
}

RenderTargetId GraphicsDevice::createRenderTarget(const RenderTargetDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([desc](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createRenderTarget(desc) : kDefaultRenderTarget;
  }) : kDefaultRenderTarget;
}

void GraphicsDevice::destroyRenderTarget(RenderTargetId target) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([target](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyRenderTarget(target);
      }
    });
  }
}

TerrainId GraphicsDevice::createTerrain(const TerrainDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([desc](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createTerrain(desc) : kInvalidTerrain;
  }) : kInvalidTerrain;
}

void GraphicsDevice::destroyTerrain(TerrainId terrain) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([terrain](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyTerrain(terrain);
      }
    });
  }
}

void GraphicsDevice::uploadTerrainTile(TerrainId terrain, const TerrainTileData& tile) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([terrain, tile](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->uploadTerrainTile(terrain, tile);
      }
    });
  }
}

void GraphicsDevice::uploadTerrainMaterialLayer(TerrainId terrain,
                                                const TerrainMaterialLayerData& layer) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([terrain, layer](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->uploadTerrainMaterialLayer(terrain, layer);
      }
    });
  }
}

void GraphicsDevice::clearTerrainMaterialLayers(TerrainId terrain) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([terrain](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->clearTerrainMaterialLayers(terrain);
      }
    });
  }
}

void GraphicsDevice::evictTerrainTile(TerrainId terrain, TerrainTileCoord coord) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([terrain, coord](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->evictTerrainTile(terrain, coord);
      }
    });
  }
}

void GraphicsDevice::submitTerrain(const TerrainDrawItem& item) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([item](RenderScheduler::Backend& backend) {
      backend.submitTerrain(item);
    });
  }
}

TerrainCapabilities GraphicsDevice::getTerrainCapabilities() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->getTerrainCapabilities() : TerrainCapabilities{};
  }) : TerrainCapabilities{};
}

TerrainStats GraphicsDevice::getTerrainStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->getTerrainStats() : TerrainStats{};
  }) : TerrainStats{};
}

DeformationId GraphicsDevice::createDeformation(const DeformationDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([desc](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->createDeformation(desc) : kInvalidDeformation;
  }) : kInvalidDeformation;
}

void GraphicsDevice::updateDeformation(DeformationId deformation,
                                       const DeformationDesc& desc) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([deformation, desc](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->updateDeformation(deformation, desc);
      }
    });
  }
}

void GraphicsDevice::destroyDeformation(DeformationId deformation) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([deformation](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->destroyDeformation(deformation);
      }
    });
  }
}

DeformationStats GraphicsDevice::getDeformationStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->getDeformationStats() : DeformationStats{};
  }) : DeformationStats{};
}

void GraphicsDevice::submit(DrawItem item) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([item = std::move(item)](
                                       RenderScheduler::Backend& backend) {
      backend.submit(item);
    });
  }
}

void GraphicsDevice::submitInstanced(InstancedDrawItem item) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    OwnedInstancedDrawItem owned{std::move(item)};
    scheduler_->recordFrameCommand([owned = std::move(owned)](
                                       RenderScheduler::Backend& backend) mutable {
      owned.submit(backend);
    });
  }
}

void GraphicsDevice::submitParticles(ParticleBatch batch) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([batch = std::move(batch)](RenderScheduler::Backend& backend) mutable {
      backend.submitParticles(std::move(batch));
    });
  }
}

void GraphicsDevice::submitPackedParticles(PackedParticleBatch batch) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([batch = std::move(batch)](RenderScheduler::Backend& backend) mutable {
      backend.submitPackedParticles(std::move(batch));
    });
  }
}

void GraphicsDevice::submitParticleEmitter(const ParticleEmitterGpuDesc& emitter) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([emitter](RenderScheduler::Backend& backend) {
      backend.submitParticleEmitter(emitter);
    });
  }
}

void GraphicsDevice::submitParticleBeam(const ParticleBeamGpuDesc& beam) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([beam](RenderScheduler::Backend& backend) {
      backend.submitParticleBeam(beam);
    });
  }
}

void GraphicsDevice::setParticleSystemStats(const ParticlePassStats& stats) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([stats](RenderScheduler::Backend& backend) {
      backend.setParticleSystemStats(stats);
    });
  }
}

void GraphicsDevice::retireInstance(InstanceId instance) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([instance](RenderScheduler::Backend& backend) {
      backend.retireInstance(instance);
    });
  }
}

void GraphicsDevice::renderLayer(LayerId layer,
                                 RenderTargetId target,
                                 const FrameGraphDesc& frame_graph) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    std::shared_ptr<const FrameGraphDesc> snapshot;
    for (const auto& cached : frame_graph_snapshots_) {
      if (cached && frameGraphsEquivalent(*cached, frame_graph)) {
        snapshot = cached;
        break;
      }
    }
    if (!snapshot) {
      snapshot = std::make_shared<const FrameGraphDesc>(frame_graph);
      constexpr std::size_t kMaxFrameGraphSnapshots = 8u;
      if (frame_graph_snapshots_.size() >= kMaxFrameGraphSnapshots) {
        frame_graph_snapshots_.erase(frame_graph_snapshots_.begin());
      }
      frame_graph_snapshots_.push_back(snapshot);
    }
    scheduler_->recordFrameCommand([layer, target, snapshot = std::move(snapshot)](
                                       RenderScheduler::Backend& backend) {
      backend.renderLayer(layer, target, *snapshot);
    });
  }
}

void GraphicsDevice::drawLine(const math::Vec3& start, const math::Vec3& end,
                              const math::Color& color, bool depth_test, float thickness) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand(
        [start, end, color, depth_test, thickness](RenderScheduler::Backend& backend) {
      backend.drawLine(start, end, color, depth_test, thickness);
    });
  }
}

unsigned int GraphicsDevice::getRenderTargetTextureId(RenderTargetId target) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->invoke([target](RenderScheduler::Backend* backend) {
    return backend != nullptr ? backend->getRenderTargetTextureId(target) : 0u;
  }) : 0u;
}

void GraphicsDevice::setCamera(const CameraData& camera) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([camera](RenderScheduler::Backend& backend) {
      backend.setCamera(camera);
    });
  }
}

void GraphicsDevice::setCameraActive(bool active) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([active](RenderScheduler::Backend& backend) {
      backend.setCameraActive(active);
    });
  }
}

void GraphicsDevice::setDirectionalLight(const DirectionalLightData& light) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([light](RenderScheduler::Backend& backend) {
      backend.setDirectionalLight(light);
    });
  }
}

void GraphicsDevice::setLights(const std::vector<LightData>& lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([lights](RenderScheduler::Backend& backend) {
      backend.setLights(lights);
    });
  }
}

void GraphicsDevice::setEnvironmentMap(const std::filesystem::path& path, float intensity,
                                       bool draw_skybox) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    if (!scheduler_->recordFrameCommandIfActive(
            [path, intensity, draw_skybox](RenderScheduler::Backend& backend) {
              backend.setEnvironmentMap(path, intensity, draw_skybox);
            })) {
      scheduler_->invokeVoid([path, intensity, draw_skybox](RenderScheduler::Backend* backend) {
        if (backend != nullptr) {
          backend->setEnvironmentMap(path, intensity, draw_skybox);
        }
      });
    }
  }
}

void GraphicsDevice::setClearColor(const math::Color& color) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([color](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setClearColor(color);
      }
    });
  }
}

void GraphicsDevice::setVsync(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([enabled](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setVsync(enabled);
      }
    });
  }
}

void GraphicsDevice::setAnisotropy(bool enabled, int level) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([enabled, level](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setAnisotropy(enabled, level);
      }
    });
  }
}

void GraphicsDevice::setGenerateMips(bool enabled) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([enabled](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setGenerateMips(enabled);
      }
    });
  }
}

void GraphicsDevice::setForwardPlusSettings(int tile_size,
                                            int max_lights_per_tile,
                                            int max_local_lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid(
        [tile_size, max_lights_per_tile, max_local_lights](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setForwardPlusSettings(tile_size, max_lights_per_tile, max_local_lights);
      }
    });
  }
}

ForwardPlusStats GraphicsDevice::getForwardPlusStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->getForwardPlusStats() : ForwardPlusStats{};
}

void GraphicsDevice::setInstancingCpuTimings(float render_system_extraction_ms,
                                             float forward_state_collection_ms) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand(
        [render_system_extraction_ms, forward_state_collection_ms](RenderScheduler::Backend& backend) {
      backend.setInstancingCpuTimings(render_system_extraction_ms,
                                      forward_state_collection_ms);
    });
  }
}

InstancingStats GraphicsDevice::getInstancingStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->getInstancingStats() : InstancingStats{};
}

ParticlePassStats GraphicsDevice::getParticlePassStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->getParticlePassStats() : ParticlePassStats{};
}

RendererCommandStats GraphicsDevice::getRendererCommandStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->getRendererCommandStats() : RendererCommandStats{};
}

RendererFrameTimingStats GraphicsDevice::getRendererFrameTimingStats() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return scheduler_ ? scheduler_->getRendererFrameTimingStats() : RendererFrameTimingStats{};
}

void GraphicsDevice::setShadowSettings(const ShadowSettings& requested_settings) {
  const ShadowSettings settings = clampShadowSettings(requested_settings);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid(
        [settings](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setShadowSettings(settings.bias,
                                   settings.map_size,
                                   settings.pcf_radius,
                                   settings.raster_depth_bias,
                                   settings.raster_slope_bias,
                                   settings.receiver_bias_scale,
                                   settings.normal_bias_scale);
      }
    });
  }
}

void GraphicsDevice::setShadowSettings(float bias,
                                       int map_size,
                                       int pcf_radius,
                                       int raster_depth_bias,
                                       float raster_slope_bias,
                                       float receiver_bias_scale,
                                       float normal_bias_scale) {
  setShadowSettings(ShadowSettings{
      .bias = bias,
      .map_size = map_size,
      .pcf_radius = pcf_radius,
      .raster_depth_bias = raster_depth_bias,
      .raster_slope_bias = raster_slope_bias,
      .receiver_bias_scale = receiver_bias_scale,
      .normal_bias_scale = normal_bias_scale,
  });
}

void GraphicsDevice::setPointShadowSettings(
    const PointShadowSettings& requested_settings) {
  const PointShadowSettings settings =
      clampPointShadowSettings(requested_settings);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([settings](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setPointShadowSettings(settings.constant_bias,
                                        settings.slope_bias_scale,
                                        settings.normal_bias_scale,
                                        settings.receiver_bias_scale);
      }
    });
  }
}

void GraphicsDevice::setPointShadowSettings(float constant_bias,
                                            float slope_bias_scale,
                                            float normal_bias_scale,
                                            float receiver_bias_scale) {
  setPointShadowSettings(PointShadowSettings{
      .constant_bias = constant_bias,
      .slope_bias_scale = slope_bias_scale,
      .normal_bias_scale = normal_bias_scale,
      .receiver_bias_scale = receiver_bias_scale,
  });
}

void GraphicsDevice::setPointShadowLightLimit(int max_lights) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([max_lights](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setPointShadowLightLimit(max_lights);
      }
    });
  }
}

void GraphicsDevice::setLocalLightingSettings(
    const LocalLightingSettings& requested_settings) {
  const LocalLightingSettings settings =
      clampLocalLightingSettings(requested_settings);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid(
        [settings](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setLocalLightingSettings(
            settings.distance_damping,
            settings.range_falloff_exponent,
            settings.ao_affects_local_lights,
            settings.directional_shadow_lift_strength);
      }
    });
  }
}

void GraphicsDevice::setLocalLightingSettings(float distance_damping,
                                              float range_falloff_exponent,
                                              bool ao_affects_local_lights,
                                              float directional_shadow_lift_strength) {
  setLocalLightingSettings(LocalLightingSettings{
      .distance_damping = distance_damping,
      .range_falloff_exponent = range_falloff_exponent,
      .ao_affects_local_lights = ao_affects_local_lights,
      .directional_shadow_lift_strength = directional_shadow_lift_strength,
  });
}

void GraphicsDevice::setExposure(float exposure) {
  exposure = clampLightingExposure(exposure);
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->invokeVoid([exposure](RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->setExposure(exposure);
      }
    });
  }
}

TextureId GraphicsDevice::createTextureRGBA8(int width, int height, const void* pixels) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  rendering::TextureDesc desc{};
  desc.width = width;
  desc.height = height;
  desc.format = rendering::TextureFormat::RGBA8;
  desc.srgb = false;
  desc.generate_mips = false;
  const TextureId id = createTexture(desc);
  if (pixels != nullptr && id != kInvalidTexture) {
    updateTextureRGBA8(id, width, height, pixels);
  }
  return id;
}

void GraphicsDevice::updateTextureRGBA8(TextureId texture, int width, int height, const void* pixels) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_ && pixels != nullptr && width > 0 && height > 0) {
    scheduler_->invokeVoid([texture, width, height, pixels](
                               RenderScheduler::Backend* backend) {
      if (backend != nullptr) {
        backend->updateTextureRGBA8(texture, width, height, pixels);
      }
    });
  }
}

void GraphicsDevice::renderUi(const karma::rendering::UIDrawData& draw_data) {
  if (!validateUIDrawData(draw_data)) {
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (scheduler_) {
    scheduler_->recordFrameCommand([draw_data](RenderScheduler::Backend& backend) {
      backend.renderUi(draw_data);
    });
  }
}

}  // namespace karma::rendering
