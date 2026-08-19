qt_add_executable(tst_settings
    tst_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Settings.cpp
)

target_link_libraries(tst_settings PRIVATE test_common Qt6::Test Qt6::Core Qt6::Gui)

add_test(NAME Settings.densityFixedStepsAreThirds
         COMMAND tst_settings density_fixedStepsAreThirds)
add_test(NAME Settings.densityRelativeScalesWithTheThreshold
         COMMAND tst_settings density_relativeScalesWithTheThreshold)
add_test(NAME Settings.densityWithoutASizeDrawsNothing
         COMMAND tst_settings density_withoutASizeDrawsNothing)
add_test(NAME Settings.firstRunHappensOnceAndIsRemembered
         COMMAND tst_settings firstRun_happensOnceAndIsRemembered)
add_test(NAME Settings.persistenceSurvivesBeingKilled
         COMMAND tst_settings persistence_survivesBeingKilled)

# Windows: put Qt's DLLs on PATH for these tests. Must be called here,
# in the same directory scope as the add_test calls above.
hyperbin_fix_test_path()
