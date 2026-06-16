#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

#include "karma/rendering/renderer/post_process_profile_library.h"

int main() {
  karma::renderer::PostProcessProfileLibrary profiles;

  assert(!profiles.resolve("").bloom_enabled);
  assert(!profiles.resolve("missing").bloom_enabled);

  karma::renderer::PostProcessSettings default_profile{};
  default_profile.bloom_enabled = true;
  default_profile.bloom_intensity = 0.6f;
  profiles.setDefaultProfile(default_profile);
  assert(profiles.resolve("").bloom_enabled);
  assert(profiles.resolve("missing").bloom_enabled);
  assert(profiles.resolve("missing").bloom_intensity == 0.6f);

  karma::renderer::PostProcessSettings named_profile{};
  named_profile.tone_mapping_enabled = true;
  named_profile.tone_exposure = 1.25f;
  profiles.registerProfile("cinematic", named_profile);
  assert(profiles.resolve("cinematic").tone_mapping_enabled);
  assert(profiles.resolve("cinematic").tone_exposure == 1.25f);
  assert(!profiles.resolve("cinematic").bloom_enabled);

  karma::renderer::PostProcessSettings replacement_default{};
  replacement_default.depth_of_field_enabled = true;
  profiles.registerProfile(std::string(karma::renderer::kDefaultPostProcessProfileKey),
                           replacement_default);
  assert(profiles.resolve("").depth_of_field_enabled);
  assert(profiles.resolve("missing").depth_of_field_enabled);

  profiles.unregisterProfile("cinematic");
  assert(profiles.resolve("cinematic").depth_of_field_enabled);

  return 0;
}
