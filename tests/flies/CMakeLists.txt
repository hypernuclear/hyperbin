qt_add_executable(tst_fly_sim
    tst_fly_sim.cpp
    ${CMAKE_SOURCE_DIR}/src/core/FlySim.cpp
    ${CMAKE_SOURCE_DIR}/src/app/Settings.cpp
)

target_link_libraries(tst_fly_sim PRIVATE test_common Qt6::Test Qt6::Core Qt6::Gui)

add_test(NAME Flies.countFullBinStaysWithinBounds
         COMMAND tst_fly_sim count_fullBin_staysWithinBounds)
add_test(NAME Flies.countNearlyEmptyBinStillDrawsOne
         COMMAND tst_fly_sim count_nearlyEmptyBin_stillDrawsOne)
add_test(NAME Flies.lifetimeFliesPersist
         COMMAND tst_fly_sim lifetime_fliesPersist)
add_test(NAME Flies.countLessRubbishShrinksTheSwarm
         COMMAND tst_fly_sim count_lessRubbish_shrinksTheSwarm)
add_test(NAME Flies.movementBothModesOccur
         COMMAND tst_fly_sim movement_bothModesOccur)
add_test(NAME Flies.fadeHappensClearOfTheBin
         COMMAND tst_fly_sim fade_happensClearOfTheBin)
add_test(NAME Flies.movementFliersDoNotStall
         COMMAND tst_fly_sim movement_fliersDoNotStall)
add_test(NAME Flies.depthNothingPassesUnderTheBin
         COMMAND tst_fly_sim depth_nothingPassesUnderTheBin)
add_test(NAME Flies.depthSomeFlightsPassInFront
         COMMAND tst_fly_sim depth_someFlightsPassInFront)
add_test(NAME Flies.cursorOnTheBinClearsAndRestoresTheSwarm
         COMMAND tst_fly_sim cursor_onTheBinClearsAndRestoresTheSwarm)
add_test(NAME Flies.cursorBesideTheBinStillAllowsLanding
         COMMAND tst_fly_sim cursor_besideTheBinStillAllowsLanding)
add_test(NAME Flies.startleIsABurstNotADeath
         COMMAND tst_fly_sim startle_isABurstNotADeath)
add_test(NAME Flies.landingPlaysTheWholeSequence
         COMMAND tst_fly_sim landing_playsTheWholeSequence)
add_test(NAME Flies.landingHappensOnTheArtwork
         COMMAND tst_fly_sim landing_happensOnTheArtworkNotTheBoundingBox)
add_test(NAME Flies.landingNeverBehindTheBin
         COMMAND tst_fly_sim landing_neverBehindTheBinAndNothingPopsIn)
add_test(NAME Flies.walkingCoversGround
         COMMAND tst_fly_sim walking_coversGround)
add_test(NAME Flies.containmentStaysInsideTheOverlay
         COMMAND tst_fly_sim containment_staysInsideTheOverlay)
add_test(NAME Flies.scalingSwarmGrowsWithTheIcon
         COMMAND tst_fly_sim scaling_swarmGrowsWithTheIcon)
add_test(NAME Flies.determinismSameSeedSameSwarm
         COMMAND tst_fly_sim determinism_sameSeedSameSwarm)
add_test(NAME Flies.idleEmptiedBinStopsRendering
         COMMAND tst_fly_sim idle_emptiedBinStopsRendering)

# Windows: put Qt's DLLs on PATH for these tests. Must be called here,
# in the same directory scope as the add_test calls above.
hyperbin_fix_test_path()
