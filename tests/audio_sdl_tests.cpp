#ifdef NDEBUG
#undef NDEBUG
#endif

#include <SDL3/SDL.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "karma/audio.h"
#include "private/audio/backends/sdl/clip.hpp"

namespace {

bool near(float actual, float expected, float epsilon = 1.0e-5f) {
  return std::abs(actual - expected) <= epsilon;
}

template <class Value>
void writeLittleEndian(std::ofstream& stream, Value value) {
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    stream.put(static_cast<char>((static_cast<uint64_t>(value) >> (byte * 8u)) &
                                 0xFFu));
  }
}

std::filesystem::path writeTestWav() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("karma-audio-sdl-" + std::to_string(nonce) + ".wav");
  constexpr uint32_t sample_rate = 48000;
  constexpr uint16_t channels = 2;
  constexpr uint16_t bits_per_sample = 16;
  constexpr uint32_t frame_count = sample_rate;
  constexpr uint32_t data_size =
      frame_count * channels * (bits_per_sample / 8u);

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  assert(stream);
  stream.write("RIFF", 4);
  writeLittleEndian<uint32_t>(stream, 36u + data_size);
  stream.write("WAVEfmt ", 8);
  writeLittleEndian<uint32_t>(stream, 16u);
  writeLittleEndian<uint16_t>(stream, 1u);
  writeLittleEndian<uint16_t>(stream, channels);
  writeLittleEndian<uint32_t>(stream, sample_rate);
  writeLittleEndian<uint32_t>(stream,
                              sample_rate * channels * (bits_per_sample / 8u));
  writeLittleEndian<uint16_t>(stream, channels * (bits_per_sample / 8u));
  writeLittleEndian<uint16_t>(stream, bits_per_sample);
  stream.write("data", 4);
  writeLittleEndian<uint32_t>(stream, data_size);
  for (uint32_t frame = 0; frame < frame_count; ++frame) {
    const int32_t sample_value =
        static_cast<int32_t>((frame % 128u) * 128u) - 8192;
    const int16_t sample = static_cast<int16_t>(sample_value);
    writeLittleEndian<uint16_t>(stream, static_cast<uint16_t>(sample));
    writeLittleEndian<uint16_t>(stream, static_cast<uint16_t>(sample));
  }
  stream.close();
  assert(stream);
  return path;
}

void testMixerPitchLoopingAndSpatialization() {
  using karma::audio::AudioPlaybackOptions;
  using karma::audio::backend::ListenerState;
  using karma::audio::backend::SdlAudioClip;

  {
    SdlAudioClip clip({1.0f, 1.0f, 2.0f, 2.0f,
                       3.0f, 3.0f, 4.0f, 4.0f},
                      2,
                      1);
    AudioPlaybackOptions options{};
    options.spatialized = false;
    options.pitch = 2.0f;
    const uint64_t voice = clip.play(options);
    assert(voice != 0);
    std::array<float, 4> output{};
    clip.mix(output.data(), 2, 2, ListenerState{});
    assert(near(output[0], 1.0f));
    assert(near(output[1], 1.0f));
    assert(near(output[2], 3.0f));
    assert(near(output[3], 3.0f));
    assert(!clip.isPlaying(voice));
  }

  {
    SdlAudioClip clip({1.0f, 1.0f, 2.0f, 2.0f}, 2, 1);
    AudioPlaybackOptions options{};
    options.spatialized = false;
    options.looping = true;
    const uint64_t voice = clip.play(options);
    std::array<float, 10> output{};
    clip.mix(output.data(), 5, 2, ListenerState{});
    for (std::size_t frame = 0; frame < 5; ++frame) {
      const float expected = frame % 2u == 0u ? 1.0f : 2.0f;
      assert(near(output[frame * 2u], expected));
      assert(near(output[frame * 2u + 1u], expected));
    }
    assert(clip.isPlaying(voice));
    assert(clip.stop(voice));
    assert(!clip.isPlaying(voice));
  }

  {
    SdlAudioClip clip({1.0f, 1.0f}, 2, 2);
    AudioPlaybackOptions right{};
    right.position = {5.0f, 0.0f, 0.0f};
    right.min_distance = 0.0f;
    right.max_distance = 10.0f;
    right.looping = true;
    const uint64_t voice = clip.play(right);
    std::array<float, 2> output{};
    clip.mix(output.data(), 1, 2, ListenerState{});
    assert(near(output[0], 0.0f));
    assert(near(output[1], 0.5f));

    assert(clip.setPosition(voice, {-5.0f, 0.0f, 0.0f}));
    output = {};
    clip.mix(output.data(), 1, 2, ListenerState{});
    assert(near(output[0], 0.5f));
    assert(near(output[1], 0.0f));
  }
}

void testSdlSubsystemReferenceCounting() {
  assert(SDL_WasInit(SDL_INIT_AUDIO) == 0u);
  auto first = std::make_unique<karma::audio::Audio>();
  assert(first->isAvailable());
  assert((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u);
  {
    karma::audio::Audio second;
    assert(second.isAvailable());
    first.reset();
    assert((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u);
  }
  assert(SDL_WasInit(SDL_INIT_AUDIO) == 0u);
}

void testClipAndVoiceRetainBackendAndConcurrentCopies() {
  const std::filesystem::path wav = writeTestWav();
  assert(SDL_WasInit(SDL_INIT_AUDIO) == 0u);

  std::optional<karma::audio::AudioClip> clip;
  {
    karma::audio::Audio audio;
    clip.emplace(audio.loadClip(wav.string(), 32));
  }
  assert((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u);

  std::optional<karma::audio::AudioClip> copied(*clip);
  constexpr std::size_t voice_count = 16;
  std::array<karma::audio::AudioVoice, voice_count> voices{};
  std::array<std::thread, voice_count> workers;
  for (std::size_t index = 0; index < workers.size(); ++index) {
    workers[index] = std::thread([&, index] {
      karma::audio::AudioPlaybackOptions options{};
      options.position.x = index % 2u == 0u ? -2.0f : 2.0f;
      options.looping = true;
      voices[index] = (index % 2u == 0u ? *clip : *copied).play(options);
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  for (const karma::audio::AudioVoice& voice : voices) {
    assert(voice);
    assert(voice.isPlaying());
  }

  clip.reset();
  copied.reset();
  assert((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0u);
  for (const karma::audio::AudioVoice& voice : voices) {
    assert(voice.stop());
  }
  voices = {};
  assert(SDL_WasInit(SDL_INIT_AUDIO) == 0u);
  std::filesystem::remove(wav);
}

}  // namespace

int main() {
  testMixerPitchLoopingAndSpatialization();
  testSdlSubsystemReferenceCounting();
  testClipAndVoiceRetainBackendAndConcurrentCopies();
  return 0;
}
