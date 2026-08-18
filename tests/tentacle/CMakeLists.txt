qt_add_executable(tst_tentacle_chain
    tst_tentacle_chain.cpp
    ${CMAKE_SOURCE_DIR}/src/core/TentacleChain.cpp
)

target_link_libraries(tst_tentacle_chain PRIVATE test_common Qt6::Test Qt6::Core Qt6::Gui)

add_test(NAME Tentacle.lengthArmDoesNotGrowOrShrink
         COMMAND tst_tentacle_chain length_armDoesNotGrowOrShrink)
add_test(NAME Tentacle.bendNoJointFoldsThroughItsNeighbour
         COMMAND tst_tentacle_chain bend_noJointFoldsThroughItsNeighbour)
add_test(NAME Tentacle.rootHoldArmLeavesAlongTheHole
         COMMAND tst_tentacle_chain rootHold_armLeavesAlongTheHole)
add_test(NAME Tentacle.rollDoesNotLurch
         COMMAND tst_tentacle_chain roll_doesNotLurch)
add_test(NAME Tentacle.waveDoesNotAccelerateOrChatter
         COMMAND tst_tentacle_chain wave_doesNotAccelerateOrChatter)
add_test(NAME Tentacle.settlingStillArmGoesStill
         COMMAND tst_tentacle_chain settling_stillArmGoesStill)
add_test(NAME Tentacle.pushOutsideArmGetsOutOfTheBin
         COMMAND tst_tentacle_chain pushOutside_armGetsOutOfTheBin)

# Windows: put Qt's DLLs on PATH for these tests. Must be called here,
# in the same directory scope as the add_test calls above.
hyperbin_fix_test_path()
