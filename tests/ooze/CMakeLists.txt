qt_add_executable(tst_ooze_sim
    tst_ooze_sim.cpp
    ${CMAKE_SOURCE_DIR}/src/core/OozeSim.cpp
)
target_link_libraries(tst_ooze_sim PRIVATE test_common Qt6::Test Qt6::Core Qt6::Gui)

# Gui, not just Core: the distance field is a QImage. It needs no display.
qt_add_executable(tst_distance_field
    tst_distance_field.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DistanceField.cpp
)
target_link_libraries(tst_distance_field PRIVATE test_common Qt6::Test Qt6::Core Qt6::Gui)

add_test(NAME Ooze.levelNearlyEmptyBinStaysClean
         COMMAND tst_ooze_sim level_nearlyEmptyBinStaysClean)
add_test(NAME Ooze.levelFullBinCreepsRatherThanSnapping
         COMMAND tst_ooze_sim level_fullBinCreepsRatherThanSnapping)
add_test(NAME Ooze.levelGrowsWithTheTrash
         COMMAND tst_ooze_sim level_growsWithTheTrash)
add_test(NAME Ooze.levelEmptyingRecedesCompletely
         COMMAND tst_ooze_sim level_emptyingRecedesCompletely)
add_test(NAME Ooze.marginsLeaveRoomForADrip
         COMMAND tst_ooze_sim margins_leaveRoomForADripAtFullStretch)

add_test(NAME DistanceField.buildProducesAField
         COMMAND tst_distance_field build_producesAField)
add_test(NAME DistanceField.signIsNegativeInsideAndPositiveOutside
         COMMAND tst_distance_field sign_isNegativeInsideAndPositiveOutside)
add_test(NAME DistanceField.gradientIsMonotonicAwayFromTheBin
         COMMAND tst_distance_field gradient_isMonotonicAwayFromTheBin)

# Windows: put Qt's DLLs on PATH for these tests. Must be called here,
# in the same directory scope as the add_test calls above.
hyperbin_fix_test_path()
