#include "karma/scenes.h"

#include "scene_document_parser.h"

#include <exception>
#include <fstream>
#include <string_view>

#include <nlohmann/json.hpp>

namespace karma::scenes {

namespace {

using Json = nlohmann::json;

bool fail(SceneLoadResult& result, std::string message) {
  result.diagnostics.push_back(std::move(message));
  result.document.reset();
  return false;
}

bool hasSceneDocumentExtension(const std::filesystem::path& path) {
  const std::string filename = path.filename().string();
  constexpr std::string_view suffix = ".kscene.json";
  return filename.size() >= suffix.size() &&
         filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool readJsonFile(const std::filesystem::path& path, Json& out, SceneLoadResult& result) {
  std::ifstream stream(path);
  if (!stream) {
    return fail(result, "failed to open scene document: " + path.string());
  }
  try {
    stream >> out;
  } catch (const std::exception& e) {
    return fail(result, std::string("failed to parse scene document JSON: ") + e.what());
  }
  if (!out.is_object()) {
    return fail(result, "scene document root must be an object");
  }
  return true;
}

}  // namespace

SceneLoadResult loadSceneDocument(const SceneLoadDesc& desc) {
  SceneLoadResult result{};
  if (desc.path.empty()) {
    fail(result, "scene document path must not be empty");
    return result;
  }
  if (desc.require_kscene_json_extension && !hasSceneDocumentExtension(desc.path)) {
    fail(result, "scene document path must end with .kscene.json: " + desc.path.string());
    return result;
  }

  Json root;
  if (!readJsonFile(desc.path, root, result)) {
    return result;
  }

  detail::parseSceneDocument(root, desc.path, result);
  return result;
}

SceneLoadResult loadSceneDocument(const std::filesystem::path& path) {
  return loadSceneDocument(SceneLoadDesc{.path = path});
}

}  // namespace karma::scenes
