#include "content/assets/ui_json_profile.h"
#include "content/assets/ui_json_validation.h"
#include "features/ui/native/authoring.h"
#include "karma/assets.h"
#include "karma/ui.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace karma::ui::detail {

struct SystemTestAccess {
  static void buildFrame(System& system,
                         rendering::UIDrawData& output) {
    system.buildFrame(0.0f, 320, 180, 320, 180, 1.0f, 1.0f, output);
  }
};

}  // namespace karma::ui::detail

namespace {

using karma::assets::detail::UiJsonKind;
using karma::assets::detail::UiJsonValidationIssue;
using karma::assets::detail::parseJsonProfile;
using karma::assets::detail::validateUiJsonProfile;
using karma::ui::native::ParsedTheme;
using karma::ui::native::StyleRule;
using karma::ui::native::compileSelector;
using karma::ui::native::parseThemeGraph;
using karma::ui::native::parseThemeSource;

const UiJsonValidationIssue* findIssue(
    const std::vector<UiJsonValidationIssue>& issues,
    std::string_view code,
    std::string_view pointer) {
  const auto found = std::find_if(
      issues.begin(), issues.end(), [&](const UiJsonValidationIssue& issue) {
        return issue.code == code && issue.json_pointer == pointer;
      });
  return found == issues.end() ? nullptr : &*found;
}

const StyleRule* findRule(const ParsedTheme& theme,
                          std::string_view selector) {
  const auto found = std::find_if(
      theme.rules.begin(), theme.rules.end(), [&](const StyleRule& rule) {
        return rule.selector == selector;
      });
  return found == theme.rules.end() ? nullptr : &*found;
}

bool hasDiagnostic(const ParsedTheme& theme, std::string_view code) {
  return std::any_of(theme.diagnostics.begin(), theme.diagnostics.end(),
                     [&](const karma::ui::Diagnostic& diagnostic) {
                       return diagnostic.code == code;
                     });
}

void testSelectorsCompileDuringThemeStaging() {
  const auto selector =
      compileSelector("window#main > panel.card:hover text.label");
  assert(selector.valid);
  assert(selector.compounds.size() == 3u);
  assert((selector.combinators == std::vector<char>{'>', ' '}));
  assert(selector.compounds[0].tag == "window");
  assert(selector.compounds[0].id == "main");
  assert(selector.compounds[1].tag == "panel");
  assert(selector.compounds[1].classes == std::vector<std::string>{"card"});
  assert(selector.compounds[1].pseudos ==
         std::vector<std::string>{"hover"});
  assert(selector.compounds[2].classes ==
         std::vector<std::string>{"label"});
  assert(!compileSelector("panel > > text").valid);

  std::size_t order = 0u;
  const ParsedTheme theme = parseThemeSource(
      R"JSON({
        "format": "karma.ui.theme",
        "version": 2,
        "defaults": {
          "button": {"appearance": {"box": {"background_color": "#123456"}}}
        },
        "styles": {
          "accent": {"appearance": {
            "text": {"color": "#abcdef"},
            "states": {"hover": {"text": {"color": "#ffffff"}}}
          }}
        }
      })JSON",
      "ui/compiled-selectors", order);
  assert(theme.diagnostics.empty());
  assert(!theme.rules.empty());
  assert(std::all_of(theme.rules.begin(), theme.rules.end(),
                     [](const StyleRule& rule) {
                       return rule.compiled_selector.valid;
                     }));
}

void testValidationAndSourceLocations() {
  const auto parsed = parseJsonProfile(
      "{\n"
      "  format: 'karma.ui.document',\n"
      "  version: 2,\n"
      "  root: {\n"
      "    type: 'panel',\n"
      "    layout: {\n"
      "      wdith: 100,\n"
      "    },\n"
      "    styles: ['good', 42],\n"
      "    children: {},\n"
      "  },\n"
      "}\n");
  assert(parsed);
  const auto issues =
      validateUiJsonProfile(*parsed.document, UiJsonKind::Document);

  const UiJsonValidationIssue* unknown =
      findIssue(issues, "UI_JSON_UNKNOWN_FIELD", "/root/layout/wdith");
  assert(unknown != nullptr);
  assert(unknown->line == 7u);
  assert(unknown->column == 7u);

  const UiJsonValidationIssue* style_type =
      findIssue(issues, "UI_JSON_TYPE", "/root/styles/1");
  assert(style_type != nullptr);
  assert(style_type->line == 9u);
  assert(style_type->column == 22u);

  const UiJsonValidationIssue* children_type =
      findIssue(issues, "UI_JSON_TYPE", "/root/children");
  assert(children_type != nullptr);
  assert(children_type->line == 10u);
  assert(children_type->column == 15u);

  const auto theme = parseJsonProfile(
      "{\n"
      "  format: 'karma.ui.theme',\n"
      "  version: 2,\n"
      "  styles: {\n"
      "    panel: { appearance: { box: { border_image: {\n"
      "      source: {asset: 'ui/frame'}, slice: [1, 2, 3],\n"
      "    } } } },\n"
      "  },\n"
      "}\n");
  assert(theme);
  const auto theme_issues =
      validateUiJsonProfile(*theme.document, UiJsonKind::Theme);
  const UiJsonValidationIssue* slice = findIssue(
      theme_issues, "UI_JSON_TYPE",
      "/styles/panel/appearance/box/border_image/slice");
  assert(slice != nullptr);
  assert(slice->line == 6u);
  assert(slice->column > 0u);

  const auto repeat_theme = parseJsonProfile(R"JSON({
    format: 'karma.ui.theme', version: 2,
    styles: {
      unknown: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1], repeat: 'space',
      }}}},
      malformed: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1], repeat: ['repeat'],
      }}}},
      too_many: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1],
        repeat: ['stretch', 'repeat', 'round'],
      }}}},
      bad_axis: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1],
        repeat: ['round', 'mirror'],
      }}}},
      bad_axis_type: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1],
        repeat: ['round', 1],
      }}}},
      bad_type: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1], repeat: true,
      }}}},
      unknown_field: {appearance: {box: {border_image: {
        source: {asset: 'ui/frame'}, slice: [1, 1, 1, 1], tiling: 'repeat',
      }}}},
    },
  })JSON");
  assert(repeat_theme);
  const auto repeat_issues =
      validateUiJsonProfile(*repeat_theme.document, UiJsonKind::Theme);
  assert(findIssue(repeat_issues, "KSTYLE2_BORDER_IMAGE_REPEAT",
                   "/styles/unknown/appearance/box/border_image/repeat") !=
         nullptr);
  assert(findIssue(repeat_issues, "UI_JSON_TYPE",
                   "/styles/malformed/appearance/box/border_image/repeat") !=
         nullptr);
  assert(findIssue(repeat_issues, "UI_JSON_TYPE",
                   "/styles/too_many/appearance/box/border_image/repeat") !=
         nullptr);
  assert(findIssue(repeat_issues, "KSTYLE2_BORDER_IMAGE_REPEAT",
                   "/styles/bad_axis/appearance/box/border_image/repeat/1") !=
         nullptr);
  assert(findIssue(
             repeat_issues, "UI_JSON_TYPE",
             "/styles/bad_axis_type/appearance/box/border_image/repeat/1") !=
         nullptr);
  assert(findIssue(repeat_issues, "UI_JSON_TYPE",
                   "/styles/bad_type/appearance/box/border_image/repeat") !=
         nullptr);
  assert(findIssue(
             repeat_issues, "UI_JSON_UNKNOWN_FIELD",
             "/styles/unknown_field/appearance/box/border_image/tiling") !=
         nullptr);

  std::size_t order = 0u;
  const ParsedTheme rejected =
      parseThemeSource(repeat_theme.document->value.dump(),
                       "ui/invalid-border-image-repeat", order);
  assert(rejected.rules.empty());
  assert(hasDiagnostic(rejected, "KSTYLE2_BORDER_IMAGE_REPEAT"));
  assert(hasDiagnostic(rejected, "UI_JSON_TYPE"));
  assert(hasDiagnostic(rejected, "UI_JSON_UNKNOWN_FIELD"));
}

void testBorderImageRepeatAuthoringContract() {
  const std::string source = R"JSON({
    format: 'karma.ui.theme', version: 2,
    styles: {
      scalar_stretch: {appearance: {box: {border_image: {
        source: {asset: 'ui/stretch'}, slice: [1, 1, 1, 1],
        repeat: 'stretch',
      }}}},
      scalar_repeat: {appearance: {box: {border_image: {
        source: {asset: 'ui/repeat'}, slice: [1, 1, 1, 1],
        repeat: 'repeat',
      }}}},
      scalar_round: {appearance: {box: {border_image: {
        source: {asset: 'ui/round'}, slice: [1, 1, 1, 1], repeat: 'round',
      }}}},
      two_axis: {appearance: {box: {border_image: {
        source: {asset: 'ui/axes'}, slice: [1, 1, 1, 1],
        repeat: ['round', 'repeat'],
      }}}},
      compatibility_default: {appearance: {box: {border_image: {
        source: {asset: 'ui/legacy'}, slice: [1, 1, 1, 1],
      }}}},
    },
  })JSON";
  const auto profile = parseJsonProfile(source);
  assert(profile);
  assert(validateUiJsonProfile(*profile.document, UiJsonKind::Theme).empty());

  std::size_t order = 0u;
  const ParsedTheme theme =
      parseThemeSource(source, "ui/border-image-repeat", order);
  assert(theme.diagnostics.empty());

  const StyleRule* stretch = findRule(theme, ".scalar_stretch");
  const StyleRule* repeat = findRule(theme, ".scalar_repeat");
  const StyleRule* round = findRule(theme, ".scalar_round");
  const StyleRule* axes = findRule(theme, ".two_axis");
  const StyleRule* compatibility =
      findRule(theme, ".compatibility_default");
  assert(stretch != nullptr && repeat != nullptr && round != nullptr &&
         axes != nullptr && compatibility != nullptr);
  assert(stretch->declarations.at("border-image-repeat") == "stretch");
  assert(repeat->declarations.at("border-image-repeat") == "repeat");
  assert(round->declarations.at("border-image-repeat") == "round");
  assert(axes->declarations.at("border-image-repeat") == "round repeat");

  // Keeping the declaration absent preserves the original all-stretch
  // compatibility path (horizontal and vertical both default to stretch).
  assert(!compatibility->declarations.contains("border-image-repeat"));
}

void testSkinningAndGameLayoutFieldsValidate() {
  const std::string theme_source = R"JSON({
    format: 'karma.ui.theme',
    version: 2,
    defaults: {
      button: {
        layout: {cursor: 'pointer'},
        appearance: {
          cursor: 'pointer',
          box: {
            border_image: {
              source: {asset: 'ui/button-frame', kind: 'texture'},
              slice: [8, 8, 8, 8],
              width: [4, 4, 4, 4],
              repeat: 'round',
            },
          },
          parts: {
            track: {box: {background_color: '#223344'}},
            thumb: {box: {
              border_image: {
                source: {asset: 'ui/track-frame', kind: 'texture'},
                slice: [3, 4, 5, 6],
                repeat: ['repeat', 'stretch'],
              },
            }},
            arrow: {text: {color: '#ffffff'}},
          },
        },
      },
    },
  })JSON";
  const auto theme = parseJsonProfile(theme_source);
  assert(theme);
  assert(validateUiJsonProfile(*theme.document, UiJsonKind::Theme).empty());
  std::size_t order = 0u;
  const ParsedTheme adapted =
      parseThemeSource(theme_source, "ui/skinning", order);
  assert(adapted.diagnostics.empty());
  const StyleRule* button = findRule(adapted, "button");
  assert(button != nullptr);
  assert(button->declarations.at("cursor") == "pointer");
  assert(button->declarations.at("border-image-source") ==
         "asset(\"ui/button-frame\")");
  assert(button->declarations.at("border-image-slice") == "8 8 8 8");
  assert(button->declarations.at("border-image-width") == "4 4 4 4");
  assert(button->declarations.at("border-image-repeat") == "round");
  assert(button->declarations.at("control-track-color") == "#223344");
  assert(!button->declarations.contains("background-color"));
  assert(button->declarations.at("control-thumb-border-image-source") ==
         "asset(\"ui/track-frame\")");
  assert(button->declarations.at("control-thumb-border-image-slice") ==
         "3 4 5 6");
  assert(button->declarations.at("control-thumb-border-image-repeat") ==
         "repeat stretch");

  const auto document = parseJsonProfile(R"JSON({
    format: 'karma.ui.document',
    version: 2,
    model: {},
    root: {
      type: 'tooltip',
      layout: {
        mode: 'overlay',
        anchors: {min: [0.5, 0.5], max: [0.5, 0.5]},
        pivot: [0.5, 0.5],
        position: [12, 18],
        offsets: {left: 1, top: 2, right: 3, bottom: 4},
      },
      props: {anchor: 'target', placement: 'bottom', delay_ms: 250},
    },
  })JSON");
  assert(document);
  assert(validateUiJsonProfile(*document.document,
                               UiJsonKind::Document).empty());

  const auto invalid_parts = parseJsonProfile(R"JSON({
    format: 'karma.ui.theme',
    version: 2,
    styles: {
      invalid: {
        appearance: {
          parts: {
            track: {
              states: {hover: {box: {background_color: '#ffffff'}}},
            },
            vertical_thumb: {
              states: {
                hvoer: {box: {background_color: '#ffffff'}},
                pressed: {box: {border_color: '#ffffff'}},
              },
            },
          },
        },
      },
    },
  })JSON");
  assert(invalid_parts);
  const auto part_issues =
      validateUiJsonProfile(*invalid_parts.document, UiJsonKind::Theme);
  assert(findIssue(part_issues, "KSTYLE2_PART_STATES",
                   "/styles/invalid/appearance/parts/track/states") != nullptr);
  assert(findIssue(
             part_issues, "KSTYLE2_PART_STATE",
             "/styles/invalid/appearance/parts/vertical_thumb/states/hvoer") !=
         nullptr);
  assert(findIssue(
             part_issues, "UI_JSON_UNKNOWN_FIELD",
             "/styles/invalid/appearance/parts/vertical_thumb/states/pressed/"
             "box/border_color") !=
         nullptr);
  assert(findIssue(
             part_issues, "KSTYLE2_PART_STATE_VALUE",
             "/styles/invalid/appearance/parts/vertical_thumb/states/pressed/"
             "box") !=
         nullptr);
}

void testCanvasAliasesAndStrictConstraints() {
  const auto valid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document',
    version: 2,
    canvas: {
      reference_size: [1920, 1080],
      scale_mode: 'pixel_perfect',
      safe_area: 'platform',
    },
    root: {type: 'body'},
  })JSON");
  assert(valid);
  assert(validateUiJsonProfile(*valid.document, UiJsonKind::Document).empty());

  const auto invalid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document',
    version: 2,
    canvas: {
      reference_size: [0, -720],
      scale_mode: 'browser',
      safe_area: 'desktop',
    },
    root: {type: 'body'},
  })JSON");
  assert(invalid);
  const auto issues =
      validateUiJsonProfile(*invalid.document, UiJsonKind::Document);
  assert(findIssue(issues, "KUI2_CANVAS_REFERENCE_SIZE",
                   "/canvas/reference_size/0") != nullptr);
  assert(findIssue(issues, "KUI2_CANVAS_REFERENCE_SIZE",
                   "/canvas/reference_size/1") != nullptr);
  assert(findIssue(issues, "KUI2_CANVAS_SCALE_MODE",
                   "/canvas/scale_mode") != nullptr);
  assert(findIssue(issues, "KUI2_CANVAS_SAFE_AREA",
                   "/canvas/safe_area") != nullptr);

  const auto missing_reference = parseJsonProfile(R"JSON({
    format: 'karma.ui.document', version: 2,
    canvas: {scale_mode: 'fit'},
    root: {type: 'body'},
  })JSON");
  assert(missing_reference);
  const auto missing_issues = validateUiJsonProfile(
      *missing_reference.document, UiJsonKind::Document);
  assert(findIssue(missing_issues, "KUI2_CANVAS_REFERENCE_SIZE",
                   "/canvas") != nullptr);
}

void testThemeRangesAndAdapterMappings() {
  const auto invalid = parseJsonProfile(R"JSON({
    format: 'karma.ui.theme',
    version: 2,
    variables: {invalid: null},
    fonts: {
      Broken: {
        src: {asset: 'ui/font', kind: 'font'},
        fallback: ['Ignored'],
        weight: 0,
        style: 'bold',
        face_index: -1,
      },
    },
    motions: {
      broken: {
        duration_ms: -1,
        delay_ms: 'later',
        iterations: 'forever',
        direction: 'sideways',
        keyframes: [
          {at: 1.1},
          {appearance: {box: {opacity: 1}}},
        ],
      },
    },
    styles: {
      broken: {appearance: {
        text: {font_family: [42]},
        parts: {background: {box: {opacity: 1}}},
        transitions: {opacity: {duration_ms: -20}},
        motion: '',
      }},
    },
  })JSON");
  assert(invalid);
  const auto issues =
      validateUiJsonProfile(*invalid.document, UiJsonKind::Theme);
  assert(findIssue(issues, "UI_JSON_TYPE", "/variables/invalid") != nullptr);
  assert(findIssue(issues, "UI_JSON_UNKNOWN_FIELD",
                   "/fonts/Broken/fallback") != nullptr);
  assert(findIssue(issues, "KSTYLE2_FONT_WEIGHT",
                   "/fonts/Broken/weight") != nullptr);
  assert(findIssue(issues, "KSTYLE2_FONT_STYLE",
                   "/fonts/Broken/style") != nullptr);
  assert(findIssue(issues, "KSTYLE2_FONT_FACE_INDEX",
                   "/fonts/Broken/face_index") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_DURATION",
                   "/motions/broken/duration_ms") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE",
                   "/motions/broken/delay_ms") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_ITERATIONS",
                   "/motions/broken/iterations") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_DIRECTION",
                   "/motions/broken/direction") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_KEYFRAME_AT",
                   "/motions/broken/keyframes/0/at") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_KEYFRAME_APPEARANCE",
                   "/motions/broken/keyframes/0") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_KEYFRAME_AT",
                   "/motions/broken/keyframes/1") != nullptr);
  assert(findIssue(issues, "KSTYLE2_TRANSITION_DURATION",
                   "/styles/broken/appearance/transitions/opacity/duration_ms") !=
         nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE",
                   "/styles/broken/appearance/text/font_family/0") != nullptr);
  assert(findIssue(issues, "KSTYLE2_MOTION_REFERENCE",
                   "/styles/broken/appearance/motion") != nullptr);
  assert(findIssue(issues, "KSTYLE2_WIDGET_PART",
                   "/styles/broken/appearance/parts/background") != nullptr);

  const std::string valid_source = R"JSON({
    format: 'karma.ui.theme',
    version: 2,
    fonts: {
      Display: {
        src: {asset: 'ui/display-font', kind: 'font'},
        weight: 1000,
        style: 'oblique',
        face_index: 0,
      },
    },
    motions: {
      pulse: {
        duration_ms: 120,
        delay_ms: -25,
        easing: 'linear',
        iterations: 2,
        direction: 'alternate',
        keyframes: [
          {at: 0, appearance: {box: {opacity: 0}}},
          {at: 1, appearance: {box: {opacity: 1}}},
        ],
      },
    },
    styles: {
      centered: {
        layout: {justify_self: 'center'},
        appearance: {motion: 'pulse'},
      },
    },
  })JSON";
  const auto valid = parseJsonProfile(valid_source);
  assert(valid);
  assert(validateUiJsonProfile(*valid.document, UiJsonKind::Theme).empty());
  std::size_t order = 0u;
  const ParsedTheme adapted =
      parseThemeSource(valid_source, "ui/strict-theme", order);
  assert(adapted.diagnostics.empty());
  const StyleRule* centered = findRule(adapted, ".centered");
  assert(centered != nullptr);
  assert(centered->declarations.at("justify-self") == "center");
  assert(centered->declarations.at("animation") ==
         "pulse 120ms linear -25ms 2 alternate both");
}

void testInlineAppearanceActionsAndSemanticRolesAreTruthful() {
  const auto invalid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document',
    version: 2,
    root: {
      type: 'button',
      appearance: {
        motion: 'pulse',
        states: {hover: {box: {opacity: 0.5}}},
      },
      on: {hover: 'hovered', click: ''},
      tooltip: 'ignored',
      semantics: {role: 'dialog'},
    },
  })JSON");
  assert(invalid);
  const auto issues =
      validateUiJsonProfile(*invalid.document, UiJsonKind::Document);
  assert(findIssue(issues, "KUI2_INLINE_APPEARANCE_MOTION",
                   "/root/appearance/motion") != nullptr);
  assert(findIssue(issues, "KUI2_INLINE_APPEARANCE_STATES",
                   "/root/appearance/states") != nullptr);
  assert(findIssue(issues, "KUI2_ACTION_EVENT", "/root/on/hover") != nullptr);
  assert(findIssue(issues, "KUI2_ACTION_NAME", "/root/on/click") != nullptr);
  assert(findIssue(issues, "UI_JSON_UNKNOWN_FIELD", "/root/tooltip") != nullptr);
  assert(findIssue(issues, "KUI2_SEMANTIC_ROLE",
                   "/root/semantics/role") != nullptr);

  const auto valid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document',
    version: 2,
    root: {
      type: 'button',
      appearance: {box: {opacity: 1}},
      on: {
        click: 'clicked', change: 'changed', cancel: 'cancelled',
        toggle: 'toggled', close: 'closed', select: 'selected',
      },
      semantics: {role: 'tab-list'},
    },
  })JSON");
  assert(valid);
  assert(validateUiJsonProfile(*valid.document, UiJsonKind::Document).empty());
}

void testRuntimeMappedPropsAreStrict() {
  const auto valid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document', version: 2,
    root: {
      type: 'img',
      props: {
        state: {bind: 'window.state', mode: 'two_way'},
        title: {expr: 'window.title'},
        text: {loc: 'window.title', args: {
          name: {bind: 'profile.name'}, count: {expr: 'rows.length'},
        }},
        min: 0, max: 100, step: 5,
        disabled: {bind: 'window.disabled'}, checked: false,
        expanded: true, selected: false, open: true, collapsed: false,
        orientation: 'horizontal',
        resizable: true, closable: true, collapsible: false,
        scrollbar_placement: 'overlay', scrollbar_visibility: 'always',
        scroll_x: 'visible', scroll_y: 'scroll',
        pointer_events: 'auto', sampling: 'nearest',
        object_fit: 'scale-down', object_position: 'right bottom',
        items: {bind: 'rows'}, item: 'row', key: {expr: 'row.id'},
        overscan: 2, item_extent: 24,
        position: [12, 24], size: [320, 180], z: 4,
        anchor: 'launcher', placement: 'bottom', delay_ms: 0,
      },
    },
  })JSON");
  assert(valid);
  assert(validateUiJsonProfile(*valid.document, UiJsonKind::Document).empty());

  const auto invalid = parseJsonProfile(R"JSON({
    format: 'karma.ui.document', version: 2,
    root: {
      type: 'img',
      props: {
        state: true, title: 42,
        text: {loc: 'window.title', args: {count: 3}},
        min: 'zero', max: [], step: false,
        disabled: 'no', checked: 1,
        orientation: 'diagonal',
        resizable: {bind: 'window.resizable'}, closable: 'yes',
        scrollbar_placement: 'outside', scrollbar_visibility: 'sometimes',
        scroll_x: 'clip', scroll_y: 1,
        pointer_events: 'all', sampling: 'cubic',
        object_fit: 'stretch', object_position: [0.5, 0.5],
        items: [], item: '', key: {expr: ''},
        overscan: -1, item_extent: -24,
        position: [12], size: [320, '180'], z: 'front',
        anchor: '', placement: 'left', delay_ms: -1,
      },
    },
  })JSON");
  assert(invalid);
  const auto issues =
      validateUiJsonProfile(*invalid.document, UiJsonKind::Document);
  assert(findIssue(issues, "KUI2_PROP_ENUM", "/root/props/scroll_x") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/scroll_y") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM",
                   "/root/props/pointer_events") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM", "/root/props/sampling") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM", "/root/props/object_fit") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE",
                   "/root/props/object_position") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/position") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/size") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/z") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/state") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/title") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE",
                   "/root/props/text/args/count") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/min") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/disabled") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM",
                   "/root/props/orientation") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/resizable") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM",
                   "/root/props/scrollbar_placement") != nullptr);
  assert(findIssue(issues, "UI_JSON_TYPE", "/root/props/items") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_VALUE", "/root/props/item") != nullptr);
  assert(findIssue(issues, "UI_JSON_BINDING", "/root/props/key/expr") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_RANGE", "/root/props/overscan") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_RANGE",
                   "/root/props/item_extent") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_VALUE", "/root/props/anchor") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_ENUM", "/root/props/placement") != nullptr);
  assert(findIssue(issues, "KUI2_PROP_RANGE", "/root/props/delay_ms") != nullptr);
}

void testRecursiveThemeComposition() {
  const std::unordered_map<std::string, std::string> sources = {
      {"ui/base",
       R"JSON({
         format: 'karma.ui.theme', version: 2,
         variables: {accent: '#ff0000'},
         styles: {
           base: {
             layout: {width: 80, height: 20},
             appearance: {box: {background_color: {var: 'accent'}}},
           },
         },
       })JSON"},
      {"ui/middle",
       R"JSON({
         format: 'karma.ui.theme', version: 2,
         imports: [{asset: 'ui/base'}],
         styles: {derived: {extends: 'base', layout: {height: 40}}},
       })JSON"},
      {"ui/root",
       R"JSON({
         format: 'karma.ui.theme', version: 2,
         imports: [{asset: 'ui/middle'}],
         variables: {accent: '#00ff00'},
         styles: {derived: {layout: {width: 120}}},
       })JSON"},
  };
  const karma::ui::native::ThemeSourceResolver resolver =
      [&](std::string_view key) -> std::optional<karma::ui::native::ThemeSource> {
    const auto found = sources.find(std::string(key));
    if (found == sources.end()) return std::nullopt;
    return karma::ui::native::ThemeSource{
        .source = found->second,
        .content_hash = karma::assets::hashString(found->second),
    };
  };

  std::size_t order = 0u;
  const ParsedTheme parsed = parseThemeGraph("ui/root", resolver, order);
  assert(parsed.diagnostics.empty());
  assert((parsed.source_keys ==
          std::vector<std::string>{"ui/base", "ui/middle", "ui/root"}));

  const StyleRule* derived = findRule(parsed, ".derived");
  assert(derived != nullptr);
  assert(derived->style_name == "derived");
  assert(derived->declarations.at("width") == "120");
  assert(derived->declarations.at("height") == "40");
  assert(derived->declarations.at("background-color") == "#00ff00");
}

void testImportOrderMissingAndCycles() {
  const std::unordered_map<std::string, std::string> sources = {
      {"ui/left",
       R"JSON({format:'karma.ui.theme',version:2,
                styles:{shared:{layout:{width:80}}}})JSON"},
      {"ui/right",
       R"JSON({format:'karma.ui.theme',version:2,
                styles:{shared:{layout:{width:120}}}})JSON"},
      {"ui/ordered",
       R"JSON({format:'karma.ui.theme',version:2,
                imports:[{asset:'ui/left'},{asset:'ui/right'}]})JSON"},
      {"ui/missing-root",
       "{\n"
       "  format: 'karma.ui.theme',\n"
       "  version: 2,\n"
       "  imports: [{asset: 'ui/not-found'}],\n"
       "}\n"},
      {"ui/cycle-a",
       R"JSON({format:'karma.ui.theme',version:2,
                imports:[{asset:'ui/cycle-b'}]})JSON"},
      {"ui/cycle-b",
       R"JSON({format:'karma.ui.theme',version:2,
                imports:[{asset:'ui/cycle-a'}]})JSON"},
  };
  const karma::ui::native::ThemeSourceResolver resolver =
      [&](std::string_view key) -> std::optional<karma::ui::native::ThemeSource> {
    const auto found = sources.find(std::string(key));
    if (found == sources.end()) return std::nullopt;
    return karma::ui::native::ThemeSource{.source = found->second};
  };

  std::size_t order = 0u;
  const ParsedTheme ordered = parseThemeGraph("ui/ordered", resolver, order);
  assert(ordered.diagnostics.empty());
  const StyleRule* shared = findRule(ordered, ".shared");
  assert(shared != nullptr);
  assert(shared->declarations.at("width") == "120");
  assert((ordered.source_keys == std::vector<std::string>{
                                     "ui/left", "ui/right", "ui/ordered"}));

  order = 0u;
  const ParsedTheme missing =
      parseThemeGraph("ui/missing-root", resolver, order);
  assert(hasDiagnostic(missing, "KSTYLE2_IMPORT_MISSING"));
  assert((missing.missing_source_keys ==
          std::vector<std::string>{"ui/not-found"}));
  const auto missing_diagnostic = std::find_if(
      missing.diagnostics.begin(), missing.diagnostics.end(),
      [](const karma::ui::Diagnostic& diagnostic) {
        return diagnostic.code == "KSTYLE2_IMPORT_MISSING";
      });
  assert(missing_diagnostic != missing.diagnostics.end());
  assert(missing_diagnostic->asset_key == "ui/missing-root");
  assert(missing_diagnostic->line == 4u);
  assert(missing_diagnostic->column > 0u);

  order = 0u;
  const ParsedTheme cycle = parseThemeGraph("ui/cycle-a", resolver, order);
  assert(hasDiagnostic(cycle, "KSTYLE2_IMPORT_CYCLE"));
  assert(cycle.rules.empty());
}

const karma::ui::AccessibilityNode* accessible(
    const karma::ui::System& system,
    karma::ui::ElementHandle element) {
  const auto& nodes = system.accessibilityTree().nodes;
  const auto found = std::find_if(nodes.begin(), nodes.end(),
                                  [&](const auto& node) {
                                    return node.element == element;
                                  });
  return found == nodes.end() ? nullptr : &*found;
}

void testNodeStyleListOrderWins() {
  karma::assets::AssetRegistry assets;
  const std::string theme = R"JSON({
    "format": "karma.ui.theme",
    "version": 2,
    "styles": {
      "a-narrow": {"layout": {"width": 60, "height": 24}},
      "z-wide": {"layout": {"width": 140, "height": 24}}
    }
  })JSON";
  assert(assets.registerUiThemeAsset(
      "ui/order-theme",
      {.canonical_json_utf8 = theme,
       .content_hash = karma::assets::hashString(theme)}));

  const std::string document = R"JSON({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{"asset": "ui/order-theme"}],
    "root": {
      "type": "body",
      "layout": {"mode": "column", "gap": 4},
      "children": [
        {"type": "panel", "id": "later-narrow",
         "styles": ["z-wide", "a-narrow"]},
        {"type": "panel", "id": "later-wide",
         "styles": ["a-narrow", "z-wide"]}
      ]
    }
  })JSON";
  assert(assets.registerUiDocumentAsset(
      "ui/order-document",
      {.canonical_json_utf8 = document,
       .dependencies = {{karma::assets::UiAssetDependencyKind::UiTheme,
                         "ui/order-theme"}},
       .content_hash = karma::assets::hashString(document)}));

  karma::ui::UiSystemConfig config;
  config.enabled = true;
  config.hot_reload = false;
  karma::ui::System system(assets, nullptr, config);
  const auto opened = system.open("ui/order-document");
  assert(opened);
  assert(opened.diagnostics.empty());

  karma::rendering::UIDrawData draw_data;
  karma::ui::detail::SystemTestAccess::buildFrame(system, draw_data);
  const auto narrow = system.findById(opened.document, "later-narrow");
  const auto wide = system.findById(opened.document, "later-wide");
  assert(narrow && wide);
  const auto* narrow_node = accessible(system, narrow);
  const auto* wide_node = accessible(system, wide);
  assert(narrow_node != nullptr && wide_node != nullptr);
  assert(std::abs(narrow_node->bounds.width - 60.0f) < 0.01f);
  assert(std::abs(wide_node->bounds.width - 140.0f) < 0.01f);
}

void testCheckedInSchemasAreStrictJson() {
  const std::filesystem::path schema_dir = KARMA_UI_SCHEMA_DIR;
  for (const std::string_view filename : {
           "common.v2.schema.json", "kui.v2.schema.json",
           "kstyle.v2.schema.json"}) {
    std::ifstream stream(schema_dir / filename, std::ios::binary);
    assert(stream);
    const nlohmann::json schema = nlohmann::json::parse(stream);
    assert(schema.value("$schema", std::string{}) ==
           "https://json-schema.org/draft/2020-12/schema");
    assert(schema.contains("$id"));
  }
}

void testShowcaseAuthoringFilesStayValid() {
  const std::filesystem::path showcase_dir = KARMA_UI_SHOWCASE_DIR;
  for (const auto& [filename, kind] : {
           std::pair{"base.kstyle.json5", UiJsonKind::Theme},
           std::pair{"showcase.kstyle.json5", UiJsonKind::Theme},
           std::pair{"showcase.kui.json5", UiJsonKind::Document},
       }) {
    std::ifstream stream(showcase_dir / filename, std::ios::binary);
    assert(stream);
    const std::string source((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    const auto parsed = parseJsonProfile(source);
    if (!parsed) {
      std::cerr << filename << ": " << parsed.error->message << '\n';
    }
    assert(parsed);
    const auto issues = validateUiJsonProfile(*parsed.document, kind);
    for (const UiJsonValidationIssue& issue : issues) {
      std::cerr << filename << ':' << issue.line << ':' << issue.column << ' '
                << issue.code << ' ' << issue.message << '\n';
    }
    assert(issues.empty());
  }
}

}  // namespace

int main() {
  testSelectorsCompileDuringThemeStaging();
  testValidationAndSourceLocations();
  testBorderImageRepeatAuthoringContract();
  testSkinningAndGameLayoutFieldsValidate();
  testCanvasAliasesAndStrictConstraints();
  testThemeRangesAndAdapterMappings();
  testInlineAppearanceActionsAndSemanticRolesAreTruthful();
  testRuntimeMappedPropsAreStrict();
  testRecursiveThemeComposition();
  testImportOrderMissingAndCycles();
  testNodeStyleListOrderWins();
  testCheckedInSchemasAreStrictJson();
  testShowcaseAuthoringFilesStayValid();
  std::cout << "ui authoring tests passed\n";
  return 0;
}
