#include "features/ui/native/development_path.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using karma::ui::native::isPortableDevelopmentPath;
using karma::ui::native::resolveDevelopmentPath;

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    static std::atomic_uint64_t sequence{0};
    path = std::filesystem::temp_directory_path() /
           ("karma_ui_portable_path_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directories(path);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

void testLoosePathsRequirePortableRelativeLexicalForm() {
  TemporaryDirectory temporary;
  const std::filesystem::path root = temporary.path / "root";
  const std::filesystem::path valid = root / "themes" / "menu.kstyle.json5";
  std::filesystem::create_directories(valid.parent_path());
  std::ofstream(valid) << "{format: 'karma.ui.theme', version: 2, styles: {}}";

  assert(isPortableDevelopmentPath("themes/menu.kstyle.json5"));
  assert(resolveDevelopmentPath("themes/menu.kstyle.json5", root, root) ==
         std::filesystem::weakly_canonical(valid));

  assert(!isPortableDevelopmentPath("themes\\menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("C:menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("C:/menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("C:\\menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("file:menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("https://example.test/menu.kstyle.json5"));
  assert(!isPortableDevelopmentPath("/menu.kstyle.json5"));

  assert(!resolveDevelopmentPath("themes\\menu.kstyle.json5", root, root));
  assert(!resolveDevelopmentPath("C:menu.kstyle.json5", root, root));
  assert(!resolveDevelopmentPath("C:/menu.kstyle.json5", root, root));
  assert(!resolveDevelopmentPath("file:menu.kstyle.json5", root, root));
  assert(!resolveDevelopmentPath("https://example.test/menu.kstyle.json5",
                                 root, root));
}

}  // namespace

int main() {
  testLoosePathsRequirePortableRelativeLexicalForm();
  std::cout << "ui_development_path_tests: ok\n";
  return 0;
}
