# Headless checks: the simulations only, never the renderer.
#
# Everything here runs without a display, which is why the sims live in
# src/core/ with no dependency on the scene graph — see docs/effects.md.
# One executable per area, every test function registered with CTest
# individually, so a failure names itself and a slow check can be run on
# its own.

# Two translation units on purpose. A static library is pulled in an object
# file at a time, so a fixture that needs FlySim must not share a file with
# one that does not — see common/bin_silhouette.cpp.
add_library(test_common STATIC
    common/bin_silhouette.cpp
    common/fly_fixtures.cpp
)

target_include_directories(test_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/app
)

target_link_libraries(test_common PUBLIC Qt6::Core Qt6::Gui Qt6::Test)

# On Windows the test binaries need Qt's DLLs on PATH. The app bundle gets
# them from windeployqt; these are bare executables in the build tree, and
# without this every one of them fails to start with 0xc0000135.
#
# Called from each area's own CMakeLists and NOT once from here, which does
# not work: test properties are directory-scoped, so set_tests_properties
# in this file cannot see a test that add_test created in a subdirectory —
# it fails outright with "Can not find test to add properties to".
function(hyperbin_fix_test_path)
    if(NOT WIN32)
        return()
    endif()
    cmake_path(GET Qt6_DIR PARENT_PATH _qt_cmake_dir)
    cmake_path(GET _qt_cmake_dir PARENT_PATH _qt_lib_dir)
    cmake_path(GET _qt_lib_dir PARENT_PATH _qt_prefix)
    get_property(_tests DIRECTORY PROPERTY TESTS)
    if(_tests)
        set_tests_properties(${_tests} PROPERTIES
            ENVIRONMENT "PATH=${_qt_prefix}/bin;$ENV{PATH}")
    endif()
endfunction()

add_subdirectory(flies)
add_subdirectory(ooze)
add_subdirectory(settings)
add_subdirectory(tentacle)

# `check` — build every test, then run them.
#
# Exists so the tests can be RUN from a UI that only knows how to build a
# target. VS Code's CMake Tools, Qt Creator and CLion all let you pick a
# build target and press go; none of them will run `ctest` for you as part
# of that. Selecting `check` as the build target turns the ordinary build
# button into a test run.
#
# --output-on-failure, because a green run should say nothing and a red one
# should say everything. USES_TERMINAL keeps CTest's progress live rather
# than arriving in a lump when it finishes.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            --build-config $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running the headless test suite"
    USES_TERMINAL
)

# add_dependencies, NOT the DEPENDS argument above.
#
# DEPENDS on add_custom_target means files and the outputs of
# add_custom_command — target names are not part of its contract. Naming
# the executables there did work, under both Makefiles and Ninja, which is
# exactly what makes it worth changing: it is undocumented behaviour that
# happened to be right, and the failure mode if a generator stops honouring
# it is `check` running CTest against binaries that were never built.
add_dependencies(check
    tst_fly_sim tst_ooze_sim tst_distance_field tst_settings
    tst_tentacle_chain
)
