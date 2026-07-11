if (NOT DEFINED KARMA_SCENE_BAKE OR KARMA_SCENE_BAKE STREQUAL "")
  message(FATAL_ERROR "KARMA_SCENE_BAKE must point at the karma_scene_bake executable")
endif()

if (NOT DEFINED KARMA_SCENE_BAKE_WORK_DIR OR KARMA_SCENE_BAKE_WORK_DIR STREQUAL "")
  set(KARMA_SCENE_BAKE_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

set(fixture_dir "${KARMA_SCENE_BAKE_WORK_DIR}/scene_bake_cli_check")
file(REMOVE_RECURSE "${fixture_dir}")
file(MAKE_DIRECTORY "${fixture_dir}/bakes")

set(scene_path "${fixture_dir}/cli.kscene.json")
set(output_path "${fixture_dir}/cli.kscenebake.json")
set(nav_cache_path "${fixture_dir}/bakes/main.knav")
set(asset_package_path "${fixture_dir}/assets.package.json")
set(asset_cache_path "${fixture_dir}/bakes/asset_cache/cli_assets")

function(write_scene path scene_name)
  file(WRITE "${path}" "{
  \"version\": 1,
  \"name\": \"${scene_name}\",
  \"asset_packages\": [
    {\"id\": \"cli_assets\", \"path\": \"assets.package.json\", \"baked_cache\": \"bakes/asset_cache/cli_assets\"}
  ],
  \"entities\": [
    {\"id\": \"root\", \"name\": \"Root\"}
  ],
  \"static\": [
    {\"id\": \"root_static\", \"entity\": \"root\", \"transform\": true, \"render\": false}
  ],
  \"bakes\": [
    {\"id\": \"main\", \"path\": \"bakes/main.kbake.json\", \"static\": [\"root_static\"], \"nav_cache\": [\"bakes/main.knav\"]}
  ]
}
")
endfunction()

write_scene("${scene_path}" "CLI Bake")
file(WRITE "${nav_cache_path}" "nav-cache-v1")
file(WRITE "${fixture_dir}/env.hdr" "env")
file(WRITE "${asset_package_path}" "{
  \"version\": 1,
  \"assets\": [
    {\"type\": \"environment_map\", \"key\": \"tests/scene_bake_cli/env\", \"path\": \"env.hdr\"}
  ]
}
")

execute_process(
  COMMAND "${KARMA_SCENE_BAKE}" --bake-packages --output "${output_path}" "${scene_path}"
  RESULT_VARIABLE bake_result
  OUTPUT_VARIABLE bake_stdout
  ERROR_VARIABLE bake_stderr
)
if (NOT bake_result EQUAL 0)
  message(FATAL_ERROR "karma_scene_bake failed (${bake_result}): ${bake_stdout}${bake_stderr}")
endif()
if (NOT EXISTS "${asset_cache_path}/baked.package.json")
  message(FATAL_ERROR "karma_scene_bake --bake-packages did not write baked.package.json")
endif()

execute_process(
  COMMAND "${KARMA_SCENE_BAKE}" --check --bake-packages --output "${output_path}" "${scene_path}"
  RESULT_VARIABLE fresh_check_result
  OUTPUT_VARIABLE fresh_check_stdout
  ERROR_VARIABLE fresh_check_stderr
)
if (NOT fresh_check_result EQUAL 0)
  message(FATAL_ERROR "karma_scene_bake --check rejected fresh bake (${fresh_check_result}): ${fresh_check_stdout}${fresh_check_stderr}")
endif()

set(authored_output_path "${fixture_dir}/bakes/main.kbake.json")
execute_process(
  COMMAND "${KARMA_SCENE_BAKE}" "${scene_path}"
  RESULT_VARIABLE authored_bake_result
  OUTPUT_VARIABLE authored_bake_stdout
  ERROR_VARIABLE authored_bake_stderr
)
if (NOT authored_bake_result EQUAL 0 OR NOT EXISTS "${authored_output_path}")
  message(FATAL_ERROR "karma_scene_bake did not use the authored bake path (${authored_bake_result}): ${authored_bake_stdout}${authored_bake_stderr}")
endif()
file(SHA256 "${authored_output_path}" authored_hash_before_check)
execute_process(
  COMMAND "${KARMA_SCENE_BAKE}" --check "${scene_path}"
  RESULT_VARIABLE authored_check_result
  OUTPUT_VARIABLE authored_check_stdout
  ERROR_VARIABLE authored_check_stderr
)
if (NOT authored_check_result EQUAL 0)
  message(FATAL_ERROR "karma_scene_bake --check rejected the authored output path (${authored_check_result}): ${authored_check_stdout}${authored_check_stderr}")
endif()
file(SHA256 "${authored_output_path}" authored_hash_after_check)
if (NOT authored_hash_before_check STREQUAL authored_hash_after_check)
  message(FATAL_ERROR "karma_scene_bake --check modified the manifest")
endif()

write_scene("${scene_path}" "CLI Bake Changed")

execute_process(
  COMMAND "${KARMA_SCENE_BAKE}" --check --output "${output_path}" "${scene_path}"
  RESULT_VARIABLE stale_check_result
  OUTPUT_VARIABLE stale_check_stdout
  ERROR_VARIABLE stale_check_stderr
)
if (NOT stale_check_result EQUAL 2)
  message(FATAL_ERROR "karma_scene_bake --check should reject stale bake with exit 2, got ${stale_check_result}: ${stale_check_stdout}${stale_check_stderr}")
endif()
