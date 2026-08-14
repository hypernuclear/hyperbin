cmake_minimum_required(VERSION 3.16)

# macOS 14 is the floor. SMAppService (login items) needs 13+, and
# nothing here wants the LSSharedFileList fallback that supporting
# older versions would drag back in.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(hyperbin VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt without requiring the caller to know where it lives.
#
# CMAKE_PREFIX_PATH may arrive empty rather than unset — the `dev` preset
# sets it from $env{QT_ROOT}, and an editor that doesn't export QT_ROOT
# expands that to "", which silently defeats the search. So treat empty
# as "not specified" and fall back to the standard install location,
# newest version first.
if(NOT CMAKE_PREFIX_PATH)
    if(DEFINED ENV{QT_ROOT})
        set(CMAKE_PREFIX_PATH "$ENV{QT_ROOT}")
    else()
        if(APPLE)
            file(GLOB _qt_dirs "$ENV{HOME}/Qt/6.*/macos")
        elseif(WIN32)
            # Windows ships one Qt build per ABI, and a cross-ABI Qt links no
            # better than a missing one — so match whatever compiler was
            # selected rather than assuming MSVC. (Only reached when nothing
            # supplied a Qt: a Qt Creator kit always passes CMAKE_PREFIX_PATH,
            # so this is the bare `cmake -S . -B build` path.) The online
            # installer defaults to C:/Qt; a per-user install lands under the
            # profile directory instead.
            if(MSVC)
                set(_qt_abi "msvc*")
            else()
                set(_qt_abi "*mingw*")
            endif()
            file(GLOB _qt_dirs "C:/Qt/6.*/${_qt_abi}" "$ENV{USERPROFILE}/Qt/6.*/${_qt_abi}")
            # Both "msvc2022_64" and "msvc2022_arm64" match the glob above, and
            # a descending sort prefers the arm64 one. Keep only the target arch.
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
                list(FILTER _qt_dirs INCLUDE REGEX "arm64$")
            else()
                list(FILTER _qt_dirs EXCLUDE REGEX "arm64$")
            endif()
        else()
            file(GLOB _qt_dirs "$ENV{HOME}/Qt/6.*/gcc_64")
        endif()
        if(_qt_dirs)
            list(SORT _qt_dirs COMPARE NATURAL ORDER DESCENDING)
            list(GET _qt_dirs 0 _qt_found)
            set(CMAKE_PREFIX_PATH "${_qt_found}")
            message(STATUS "Qt not specified; auto-detected ${_qt_found}")
        endif()
    endif()
endif()

find_package(Qt6 6.11 QUIET COMPONENTS Core Gui Widgets Svg Qml Quick QuickControls2 Quick3D ShaderTools)
if(NOT Qt6_FOUND)
    if(WIN32)
        message(FATAL_ERROR
            "Qt 6.11+ not found.\n"
            "Point the build at it in any one of these ways:\n"
            "  set QT_ROOT=C:\\Qt\\6.11.1\\msvc2022_64\n"
            "  cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64\n"
            "In Qt Creator, pick a kit that already has a Qt version bound to\n"
            "it (Desktop Qt 6.11.1 MSVC2022 64bit) — the presets are macOS/Linux\n"
            "only on purpose, see CMakePresets.json.")
    else()
        message(FATAL_ERROR
            "Qt 6.11+ not found.\n"
            "Point the build at it in any one of these ways:\n"
            "  export QT_ROOT=~/Qt/6.11.1/macos\n"
            "  cmake --preset dev -DCMAKE_PREFIX_PATH=~/Qt/6.11.1/macos\n"
            "  create CMakeUserPresets.json with a preset inheriting 'dev'\n"
            "In VS Code, select the 'local' preset rather than 'dev' if you\n"
            "have a CMakeUserPresets.json.")
    endif()
endif()
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Svg Qml Quick QuickControls2 Quick3D ShaderTools)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(hyperbin
    src/main.cpp
    src/app/Settings.cpp
    src/app/Settings.h
    src/app/TrayMenu.cpp
    src/app/TrayMenu.h
    src/core/FlySim.cpp
    src/core/FlySim.h
    src/core/PowerPolicy.cpp
    src/core/PowerPolicy.h
    src/platform/TrashTarget.cpp
    src/platform/TrashTarget.h
    src/platform/StubTrashTarget.cpp
    src/platform/StubTrashTarget.h
    src/platform/LaunchAtLogin.h
    $<$<PLATFORM_ID:Darwin>:src/platform/LaunchAtLoginMac.mm>
    $<$<PLATFORM_ID:Windows>:src/platform/LaunchAtLoginWin.cpp>
    $<$<NOT:$<OR:$<PLATFORM_ID:Darwin>,$<PLATFORM_ID:Windows>>>:src/platform/LaunchAtLoginStub.cpp>
    $<$<PLATFORM_ID:Darwin>:src/platform/DockTrashTarget.mm>
    $<$<PLATFORM_ID:Darwin>:src/platform/MacOverlay.mm>
    $<$<PLATFORM_ID:Windows>:src/platform/RecycleBinTarget.cpp>
    $<$<PLATFORM_ID:Windows>:src/platform/RecycleBinTarget.h>
    $<$<PLATFORM_ID:Windows>:src/platform/WinOverlay.cpp>
    $<$<PLATFORM_ID:Windows>:src/platform/WinOverlay.h>
    $<$<PLATFORM_ID:Windows>:src/platform/WinScreen.cpp>
    $<$<PLATFORM_ID:Windows>:src/platform/WinScreen.h>
    src/core/Effect.h
    src/core/EffectRegistry.cpp
    src/core/EffectRegistry.h
    src/core/DistanceField.cpp
    src/core/DistanceField.h
    src/core/OozeSim.cpp
    src/core/OozeSim.h
    src/effects/FliesEffect.cpp
    src/effects/FliesEffect.h
    src/effects/OozeEffect.cpp
    src/effects/OozeEffect.h
    src/effects/OozeGeometry.cpp
    src/effects/OozeGeometry.h
    src/effects/OozeShape.cpp
    src/effects/OozeShape.h
    src/render/EffectItem.cpp
    src/render/EffectItem.h
    src/render/FlyMaskMaterial.cpp
    src/render/FlyMaskMaterial.h
)

# The fly material samples the bin's silhouette to erase flies behind it.
qt_add_shaders(hyperbin "hyperbin_shaders"
    PREFIX "/shaders"
    BASE shaders
    FILES
        shaders/flymask.vert
        shaders/flymask.frag
)
qt_add_qml_module(hyperbin
    URI hyperbin
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/BinGlyph.qml
        qml/OozeVisual.qml
        qml/Permissions.qml
        qml/Splash.qml
        qml/HyperbinLockup.qml
        qml/HyperbinWordmark.qml
        qml/HypernuclearWordmark.qml
)

# qmltyperegistrations.cpp includes the QML_ELEMENT headers by basename,
# so their directories have to be on the include path, not just src/.
# The version reaches the UI as a compile definition rather than being
target_compile_definitions(hyperbin PRIVATE HYPERBIN_VERSION="${PROJECT_VERSION}")
target_include_directories(hyperbin PRIVATE
    src
    src/app
    src/core
    src/effects
    src/render
    src/platform
)
target_link_libraries(hyperbin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
    Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D
)
qt_add_resources(hyperbin "hyperbin_icons"
    PREFIX "/icons"
    BASE resources
    FILES
        resources/hyperbin.svg
        resources/env.hdr
        resources/splash.jpg
        # A static SemiBold instance cut from Inter's variable font and
        # subset to Latin, by scripts/cut_inter.py. Bundled rather than
        # asked for by name: this ships as a direct download, and a font
        # that silently falls back to the system UI face on every machine
        # but the one it was designed on is not a brand.
        resources/Inter-SemiBold.ttf
)
qt_add_resources(hyperbin "hyperbin_shaders3d"
    PREFIX "/shaders3d"
    BASE shaders
    FILES
        shaders/ooze3d.vert
        shaders/ooze3d.frag
)

# Headless swarm check. No GUI, so it runs on CI and under the debugger
# without a display.
add_executable(simtest tests/simtest.cpp src/core/FlySim.cpp src/core/OozeSim.cpp
               src/core/DistanceField.cpp src/app/Settings.cpp)
target_include_directories(simtest PRIVATE src/core src/app)
# Gui, not just Core: the distance field is a QImage. It needs no display
target_link_libraries(simtest PRIVATE Qt6::Core Qt6::Gui)

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
        # SMAppService — registers the app in System Settings > Login Items.
        "-framework ServiceManagement"
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
    # shell32/ole32/uuid: the desktop shell view and the Recycle Bin's own
    # artwork. gdi32: turning that artwork's HBITMAP into a QImage.
    # dwmapi: DWMWA_CLOAKED, which is how a window that is "visible" but
    # not really on screen is told apart from one that covers the bin.
    # shcore: GetDpiForMonitor, which is how physical shell coordinates
    # become Qt's device-independent ones on a mixed-DPI desktop.
    target_link_libraries(hyperbin PRIVATE
        shell32 user32 ole32 oleaut32 uuid shlwapi gdi32 dwmapi advapi32 shcore)
endif()
