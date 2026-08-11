cmake_minimum_required(VERSION 3.16)

# Match Hypershot's floor so the same toolchain builds both.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(hyperbin VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(hyperbin
    src/main.cpp
    src/core/FlySim.cpp
    src/core/FlySim.h
    src/core/PowerPolicy.cpp
    src/core/PowerPolicy.h
    src/platform/TrashTarget.cpp
    src/platform/TrashTarget.h
    src/platform/StubTrashTarget.cpp
    src/platform/StubTrashTarget.h
    $<$<PLATFORM_ID:Darwin>:src/platform/DockTrashTarget.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacOverlay.mm>
    src/render/FlyItem.cpp
    src/render/FlyItem.h
)

qt_add_qml_module(hyperbin
    URI hyperbin
    VERSION 1.0
    QML_FILES qml/Main.qml
)

# qmltyperegistrations.cpp includes the QML_ELEMENT headers by basename,
# so their directories have to be on the include path, not just src/.
target_include_directories(hyperbin PRIVATE
    src
    src/core
    src/render
    src/platform
)
target_link_libraries(hyperbin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)

# Headless swarm check. No GUI, so it runs on CI and under the debugger
# without a display.
add_executable(simtest tests/simtest.cpp src/core/FlySim.cpp)
target_include_directories(simtest PRIVATE src/core)
target_link_libraries(simtest PRIVATE Qt6::Core)

# Overlay app: no Dock icon, no menu bar. LSUIElement keeps it a
# background agent driven from the menu-bar item.
if(APPLE)
    set_target_properties(hyperbin PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.hypernuclear.hyperbin
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
    target_link_libraries(hyperbin PRIVATE
        "-framework AppKit"
        "-framework ApplicationServices"
    )

    # Sign with a STABLE identity, even for local dev builds.
    #
    # Without this the bundle is ad-hoc/linker-signed, and TCC identifies
    # ad-hoc apps by their code hash — which changes on every rebuild. The
    # result is that every rebuild looks like a brand new app to macOS and
    # silently drops the Accessibility grant, so the app re-prompts
    # forever. Signing with the Developer ID makes TCC key on team +
    # bundle id, which survives rebuilds.
    set(HYPERBIN_CODESIGN_IDENTITY
        "Developer ID Application: Hypernuclear LLC (67J6LGVJDP)"
        CACHE STRING "codesign identity; empty to skip signing")
    if(HYPERBIN_CODESIGN_IDENTITY)
        add_custom_command(TARGET hyperbin POST_BUILD
            COMMAND codesign --force --sign ${HYPERBIN_CODESIGN_IDENTITY}
                    --timestamp=none $<TARGET_BUNDLE_DIR:hyperbin>
            COMMENT "Signing so TCC permissions survive rebuilds"
            VERBATIM)
    endif()
    set_target_properties(hyperbin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in)
elseif(WIN32)
    set_target_properties(hyperbin PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(hyperbin PRIVATE shell32 user32)
endif()
