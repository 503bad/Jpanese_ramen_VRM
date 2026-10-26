add_library(rvrm_core STATIC
  src/avatar.cpp
  src/cgltf_impl.c
  src/convert.cpp
  src/firstperson.cpp
  src/humanoid.cpp
  src/json.cpp
  src/license.cpp
  src/loader.cpp
  src/math.cpp
  src/retarget.cpp
  src/skeleton.cpp
  src/skinning.cpp
  src/springbone.cpp
)
target_include_directories(rvrm_core
  PUBLIC include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cgltf
)

if(RVRM_BUILD_TESTS AND NOT RVRM_MOD_BUILD)
  enable_testing()
  add_executable(rvrm_tests
    tests/test_main.cpp
    tests/synthetic_vrm.cpp
    tests/test_math_json.cpp
    tests/test_loader.cpp
    tests/test_pose.cpp
  )
  target_link_libraries(rvrm_tests PRIVATE rvrm_core)
  add_test(NAME rvrm_tests COMMAND rvrm_tests)
endif()

# MOD のビルド(mod/CMakeLists.txt)から使われるときも同じ条件でコンパイルする
target_compile_features(rvrm_core PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(rvrm_core PRIVATE /utf-8)
endif()
if(MSVC)
  # cgltf(サードパーティ)の CRT 警告は抑止する
  set_source_files_properties(src/cgltf_impl.c PROPERTIES COMPILE_OPTIONS "/W0;/D_CRT_SECURE_NO_WARNINGS")
endif()
