cmake_minimum_required(VERSION 3.16)

# macOS 14 is the floor. SMAppService (login items) needs 13+, and
# nothing here wants the LSSharedFileList fallback that supporting
# older versions would drag back in.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(hyperbin VERSION 1.0.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

# Auto-updater. Off for a build that must not phone home — a store
# submission, or a distro package where the package manager owns updates.
option(HYPERBIN_AUTO_UPDATE "Include the auto-updater (Sparkle / WinSparkle)" ON)

# --- versions ---------------------------------------------------------------
#
# Two numbers, and they do different jobs.
#
# PROJECT_VERSION is the marketing version: what the splash shows, what a
# release is called. APP_VERSION_BUILD is a monotonic commit count, and it
# is the one the updaters actually compare. The marketing version cannot
# do that job: CFBundleVersion has to be numeric, and two builds of the
# same 0.1.0 would look identical to Sparkle, so a rebuild would never be
# offered as an update. A commit count only ever goes up.
#
# CI passes the real value with -DAPP_VERSION_BUILD (computed on the
# tagged commit); a local build computes the same thing here. Note the
# CI checkout must be full-depth — `rev-list --count` on a shallow clone
# returns 1, which would make every release look older than the last.
find_package(Git QUIET)
if(NOT APP_VERSION_BUILD AND GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE APP_VERSION_BUILD
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()
if(NOT APP_VERSION_BUILD)
    set(APP_VERSION_BUILD "0")
endif()

# What the user sees. CI passes the tag's version; a local build says so,
# because "0.1.0" on a working copy that is nine commits ahead of the tag
# is a lie that only ever surfaces in a bug report.
if(NOT APP_VERSION_DISPLAY)
    set(APP_VERSION_DISPLAY "${PROJECT_VERSION}-dev")
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

# --- auto-update ------------------------------------------------------------
#
# One appcast format, one signing key, two runtimes. The public key is
# public by construction — it ships inside every binary either way, and
# its whole job is to be checked against a signature made with the
# private half, which lives only in a GitHub secret. Do not put the
# private key here, or anywhere else in this repo.
#
# Stable channel only. The R2 layout leaves `latest-beta/` free so a beta
# feed can be added later without moving the stable one.
#
# Deliberately NOT a cache variable. `set(... CACHE ...)` does not
# overwrite an entry that already exists, so once a build directory had
# been configured with an empty key, adding the real one here changed
# nothing — the stale empty value won, the plist shipped an empty
# SUPublicEDKey, and the only symptom was an appcast that quietly came
# out unsigned. Assigning it every configure removes that failure.
#
# -D still wins: a value passed on the command line lands in the cache,
# which makes the variable truthy, and this leaves it alone.
if(NOT HYPERBIN_EDDSA_PUBKEY)
    set(HYPERBIN_EDDSA_PUBKEY "py1N5FJ/CxmiAkDmKJuIc6b2znpkxwb6qo3s+YC3Clg=")
endif()
set(HYPERBIN_CDN_URL "https://hyperbin-builds.hypernuclear.com" CACHE STRING
    "Base URL that release artifacts are published under")
set(HYPERBIN_APPCAST_URL "${HYPERBIN_CDN_URL}/latest/mac/appcast.xml"
    CACHE STRING "Sparkle appcast feed URL (macOS)")
set(HYPERBIN_APPCAST_URL_WIN "${HYPERBIN_CDN_URL}/latest/win/appcast.xml"
    CACHE STRING "WinSparkle appcast feed URL")

# An ed25519 public key is 32 bytes, which is 44 base64 characters. This
# catches a truncated or half-selected paste, which would otherwise sail
# through configure and surface as an unsigned appcast at release time.
string(LENGTH "${HYPERBIN_EDDSA_PUBKEY}" _hb_key_len)
if(HYPERBIN_AUTO_UPDATE AND NOT _hb_key_len EQUAL 44)
    message(FATAL_ERROR
        "HYPERBIN_EDDSA_PUBKEY is ${_hb_key_len} characters; an ed25519 public "
        "key is 44. Check the value in CMakeLists.txt against "
        "`generate_keys --account hyperbin -p`. See docs/distribution.md.")
endif()

# --- analytics --------------------------------------------------------------
#
# Opt-in, and off unless an app key was compiled in. The key is not a
# credential — it identifies which Countly app a request belongs to and
# ships inside every binary — but this repo is public, and a key sitting
# in a public tree is an invitation to write junk into the dashboard. So
# CI passes it from a secret and local builds simply have no backend.
option(HYPERBIN_ANALYTICS "Include opt-in usage analytics (Countly)" ON)
set(HYPERBIN_COUNTLY_APP_KEY "" CACHE STRING "Countly app key; CI passes this")
set(HYPERBIN_COUNTLY_SERVER "https://analytics.hypernuclear.com" CACHE STRING
    "Countly server URL")

if(HYPERBIN_ANALYTICS)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/countly-sdk-cpp/CMakeLists.txt")
        message(FATAL_ERROR
            "third_party/countly-sdk-cpp is empty. Run:\n"
            "  git submodule update --init --recursive\n"
            "or configure with -DHYPERBIN_ANALYTICS=OFF.")
    endif()
    # Qt already provides HTTP and SHA-256, and this app links both. The
    # SDK's own defaults would drag libcurl and OpenSSL into a menu-bar
    # app for no gain.
    set(COUNTLY_USE_CUSTOM_HTTP   ON  CACHE BOOL "" FORCE)
    set(COUNTLY_USE_CUSTOM_SHA256 ON  CACHE BOOL "" FORCE)
    set(COUNTLY_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
    set(COUNTLY_BUILD_SAMPLE      OFF CACHE BOOL "" FORCE)
    # The SDK declares cmake_minimum_required(VERSION 3.0), which CMake 4
    # refuses outright. Can't patch a submodule, so raise the floor it is
    # allowed to ask for.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    set(_hb_shared_saved ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/countly-sdk-cpp EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${_hb_shared_saved} CACHE BOOL "" FORCE)
endif()

set(HYPERBIN_UPDATE_SOURCES src/update/AppUpdater.h)
if(HYPERBIN_AUTO_UPDATE AND APPLE)
    include(FetchContent)
    set(SPARKLE_VERSION "2.9.0")
    FetchContent_Declare(sparkle
        URL "https://github.com/sparkle-project/Sparkle/releases/download/${SPARKLE_VERSION}/Sparkle-${SPARKLE_VERSION}.tar.xz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sparkle)
    set(SPARKLE_FRAMEWORK_DIR "${sparkle_SOURCE_DIR}")
    list(APPEND HYPERBIN_UPDATE_SOURCES
        src/update/SparkleUpdater.h
        src/update/SparkleUpdater.mm)
elseif(HYPERBIN_AUTO_UPDATE AND WIN32)
    include(FetchContent)
    set(WINSPARKLE_VERSION "0.9.2")
    FetchContent_Declare(winsparkle
        URL "https://github.com/vslavik/winsparkle/releases/download/v${WINSPARKLE_VERSION}/WinSparkle-${WINSPARKLE_VERSION}.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(winsparkle)
    list(APPEND HYPERBIN_UPDATE_SOURCES
        src/update/WinSparkleUpdater.h
        src/update/WinSparkleUpdater.cpp)
else()
    list(APPEND HYPERBIN_UPDATE_SOURCES src/update/NoUpdater.cpp)
endif()

set(HYPERBIN_ANALYTICS_SOURCES src/analytics/Analytics.h)
if(HYPERBIN_ANALYTICS)
    list(APPEND HYPERBIN_ANALYTICS_SOURCES
        src/analytics/CountlyAnalytics.h
        src/analytics/CountlyAnalytics.cpp
        src/analytics/MachineId.h
        src/analytics/MachineId.cpp)
else()
    list(APPEND HYPERBIN_ANALYTICS_SOURCES src/analytics/NoAnalytics.cpp)
endif()

qt_add_executable(hyperbin
    ${HYPERBIN_UPDATE_SOURCES}
    ${HYPERBIN_ANALYTICS_SOURCES}
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
    src/core/BinMouth.cpp
    src/core/OozeSim.cpp
    src/core/TentacleChain.cpp
    src/core/OozeSim.h
    src/effects/FliesEffect.cpp
    src/effects/FliesEffect.h
    src/effects/IconTexture.cpp
    src/effects/SpineTexture.cpp
    src/effects/OozeEffect.cpp
    src/effects/TentacleEffect.cpp
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
        qml/OozeEyes.qml
        qml/TentacleVisual.qml
        qml/Permissions.qml
        qml/Splash.qml
        qml/HyperbinLockup.qml
        qml/HyperbinWordmark.qml
        qml/HypernuclearWordmark.qml
)

# qmltyperegistrations.cpp includes the QML_ELEMENT headers by basename,
# so their directories have to be on the include path, not just src/.
# The version reaches the UI as a compile definition rather than being
# read back out of the bundle: on Windows there is no bundle to read.
target_compile_definitions(hyperbin PRIVATE
    HYPERBIN_VERSION="${APP_VERSION_DISPLAY}"
    HYPERBIN_VERSION_BUILD="${APP_VERSION_BUILD}")
if(HYPERBIN_AUTO_UPDATE)
    # macOS reads both of these from Info.plist; Windows has no plist, so
    # WinSparkle is told at runtime and needs them compiled in.
    target_compile_definitions(hyperbin PRIVATE
        HYPERBIN_AUTO_UPDATE
        HYPERBIN_APPCAST_URL="${HYPERBIN_APPCAST_URL_WIN}"
        HYPERBIN_EDDSA_PUBKEY="${HYPERBIN_EDDSA_PUBKEY}")
endif()
if(HYPERBIN_ANALYTICS)
    target_link_libraries(hyperbin PRIVATE countly Qt6::Network)
    target_compile_definitions(hyperbin PRIVATE
        HYPERBIN_COUNTLY_APP_KEY="${HYPERBIN_COUNTLY_APP_KEY}"
        HYPERBIN_COUNTLY_SERVER="${HYPERBIN_COUNTLY_SERVER}")
endif()
target_include_directories(hyperbin PRIVATE
    src
    src/app
    src/core
    src/effects
    src/render
    src/platform
    src/update
    src/analytics
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
        # The eyeballs in the goo. Converted from a glTF with balsam — see
        # "Bringing in a model" in docs/effects.md for the command and what
        # to keep. The source model's cornea is a second,
        # transparent mesh — left out on purpose, because a transparent
        # PBR shell costs a full extra pass to produce a highlight that
        # is sub-pixel at the size this is drawn.
        resources/meshes/eye.mesh
        resources/maps/eye_albedo.jpg
        resources/maps/eye_normal.png
        resources/meshes/tentacle.mesh
        resources/maps/tentacle_albedo.jpg
        resources/maps/tentacle_normal.jpg
)
qt_add_resources(hyperbin "hyperbin_shaders3d"
    PREFIX "/shaders3d"
    BASE shaders
    FILES
        shaders/ooze3d.vert
        shaders/ooze3d.frag
        shaders/eye3d.vert
        shaders/eye3d.frag
        shaders/tentacle.vert
        shaders/tentacle.frag
)

# Headless swarm check. No GUI, so it runs on CI and under the debugger
# without a display.
add_executable(simtest tests/simtest.cpp src/core/FlySim.cpp src/core/OozeSim.cpp
    src/core/TentacleChain.cpp
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
        # Sparkle compares CFBundleVersion, so it gets the build number.
        # CFBundleShortVersionString is the one people read.
        MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION_BUILD}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE hyperbin.icns
    )
    # LSUIElement keeps this out of the Dock, so the icon is for Finder,
    # the DMG, and the update dialog Sparkle puts up — all places where a
    # generic bundle icon looks like something went wrong.
    target_sources(hyperbin PRIVATE resources/hyperbin.icns)
    set_source_files_properties(resources/hyperbin.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_link_libraries(hyperbin PRIVATE
        "-framework AppKit"
        "-framework ApplicationServices"
        # SMAppService — registers the app in System Settings > Login Items.
        "-framework ServiceManagement"
    )

    if(HYPERBIN_AUTO_UPDATE)
        target_compile_options(hyperbin PRIVATE
            "-F${SPARKLE_FRAMEWORK_DIR}")
        target_link_libraries(hyperbin PRIVATE
            "-F${SPARKLE_FRAMEWORK_DIR}" "-framework Sparkle")
        set_source_files_properties(src/update/SparkleUpdater.mm PROPERTIES
            COMPILE_FLAGS "-fobjc-arc")
        # Sparkle ships as a framework and has to travel inside the
        # bundle, with the loader pointed at it. Copied at build time so
        # a locally built .app updates like a shipped one; without this
        # it launches on the developer's machine and nowhere else.
        #
        # ditto, not `cmake -E copy_directory`: a versioned framework is
        # a Versions/ directory plus a set of symlinks into it, and
        # copy_directory follows those symlinks and writes real copies.
        # The result still launches, so it looks fine — and then
        # codesign refuses the bundle with "bundle format is ambiguous
        # (could be app or framework)", because a framework whose
        # Versions/Current is a real directory is not a shape it
        # recognises. ditto preserves the links.
        add_custom_command(TARGET hyperbin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_BUNDLE_CONTENT_DIR:hyperbin>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "$<TARGET_BUNDLE_CONTENT_DIR:hyperbin>/Frameworks/Sparkle.framework"
            COMMAND ditto
                    "${SPARKLE_FRAMEWORK_DIR}/Sparkle.framework"
                    "$<TARGET_BUNDLE_CONTENT_DIR:hyperbin>/Frameworks/Sparkle.framework"
            COMMENT "Copying Sparkle.framework into the bundle"
            VERBATIM)
        set_target_properties(hyperbin PROPERTIES
            BUILD_RPATH "@executable_path/../Frameworks"
            INSTALL_RPATH "@executable_path/../Frameworks")
    endif()

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
    # Windows takes the executable's icon from a resource compiled in,
    # not from a file beside it.
    target_sources(hyperbin PRIVATE packaging/windows/hyperbin.rc)
    # shell32/ole32/uuid: the desktop shell view and the Recycle Bin's own
    # artwork. gdi32: turning that artwork's HBITMAP into a QImage.
    # dwmapi: DWMWA_CLOAKED, which is how a window that is "visible" but
    # not really on screen is told apart from one that covers the bin.
    # shcore: GetDpiForMonitor, which is how physical shell coordinates
    # become Qt's device-independent ones on a mixed-DPI desktop.
    target_link_libraries(hyperbin PRIVATE
        shell32 user32 ole32 oleaut32 uuid shlwapi gdi32 dwmapi advapi32 shcore)

    if(HYPERBIN_AUTO_UPDATE)
        # WinSparkle ships prebuilt, one directory per architecture.
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
                set(WINSPARKLE_ARCH "arm64")
            else()
                set(WINSPARKLE_ARCH "x64")
            endif()
        else()
            set(WINSPARKLE_ARCH "x86")
        endif()
        target_include_directories(hyperbin PRIVATE
            "${winsparkle_SOURCE_DIR}/include")
        target_link_directories(hyperbin PRIVATE
            "${winsparkle_SOURCE_DIR}/${WINSPARKLE_ARCH}/Release")
        target_link_libraries(hyperbin PRIVATE WinSparkle)
        # The DLL has to sit beside the exe both in the build tree and in
        # whatever the installer stages, or the app dies at load time
        # with no message a user could act on.
        add_custom_command(TARGET hyperbin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${winsparkle_SOURCE_DIR}/${WINSPARKLE_ARCH}/Release/WinSparkle.dll"
                    "$<TARGET_FILE_DIR:hyperbin>"
            COMMENT "Copying WinSparkle.dll beside the executable"
            VERBATIM)
    endif()
endif()
