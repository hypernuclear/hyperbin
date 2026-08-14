#!/usr/bin/env bash
#
# Sign a built hyperbin.app and wrap it in a DMG.
#
# Run by CI on a tag, and runnable by hand for testing the update path
# end to end without waiting on a release. Notarization is separate (see
# RELEASING.md) — this stops at a signed, stapled-ready bundle.
#
#   scripts/package-macos.sh build 0.1.0 arm64
#
# Arguments:
#   $1  build directory containing hyperbin.app   (default: build)
#   $2  version string used in the DMG's filename (default: from CMake)
#   $3  architecture tag for the filename          (default: uname -m)
#
# Signing identity comes from CODESIGN_IDENTITY, or the first
# "Developer ID Application" in the keychain. Unset and absent means the
# bundle is left ad-hoc signed: fine for a local look, useless for
# distribution, and said out loud rather than assumed.
set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$BUILD_DIR/hyperbin.app"

VERSION="${2:-}"
if [ -z "$VERSION" ]; then
    VERSION=$(sed -n 's/^project(hyperbin VERSION \([0-9.]*\).*/\1/p' \
                  "$REPO_ROOT/CMakeLists.txt")
fi
ARCH="${3:-$(uname -m)}"
DMG="$BUILD_DIR/hyperbin-${VERSION}-macos-${ARCH}.dmg"

[ -d "$APP" ] || { echo "no bundle at $APP — build first" >&2; exit 1; }

# --- is the bundle actually current? ---------------------------------------
#
# This script signs and packages. It does NOT compile, and that has
# already shipped a wrong DMG once: the icon was regenerated, the bundle
# still held the previous .icns, and the disk image looked perfectly
# valid while carrying a stale asset. Nothing downstream catches it —
# the signature is over whatever is there.
#
# So refuse to package a bundle older than its own inputs. CI always
# builds immediately before this, so it only ever fires locally, which is
# exactly where the mistake is possible.
# Compared against the newest file anywhere in the bundle, not against
# the executable: changing a resource copies a new file in without
# relinking, so anchoring on the binary reports every resource edit as
# stale forever, even straight after a successful build.
# `awk NR==1` rather than `head -1`: head exits after the first line, the
# writer upstream takes SIGPIPE, and under `set -o pipefail` that is a
# 141 that `set -e` turns into a silent abort of the whole script. awk
# drains the stream, so the pipeline exits 0.
newest() {
    find "$@" -type f -exec stat -f '%m %N' {} + 2>/dev/null | sort -rn | awk 'NR==1'
}
APP_NEWEST=$(newest "$APP")
SRC_NEWEST=$(newest "$REPO_ROOT/src" "$REPO_ROOT/qml" "$REPO_ROOT/resources" \
                    "$REPO_ROOT/shaders")
if [ "${SRC_NEWEST%% *}" -gt "${APP_NEWEST%% *}" ] 2>/dev/null; then
    echo "ERROR: $APP is older than ${SRC_NEWEST#* }" >&2
    echo "       This script packages; it does not build." >&2
    echo "       Run:  cmake --build $BUILD_DIR" >&2
    exit 1
fi

# --- clear stale mounts ----------------------------------------------------
#
# create-dmg drives Finder with AppleScript that addresses the volume by
# NAME. With /Volumes/hyperbin already taken, the new image mounts as
# "hyperbin 1", the script talks to the wrong volume, and the run hangs
# until something kills it. A killed run then leaves its own scratch
# image attached — with the backing file already unlinked — so the next
# one inherits the mess.
for vol in /Volumes/hyperbin /Volumes/hyperbin\ *; do
    [ -d "$vol" ] || continue
    echo "detaching stale volume: $vol"
    hdiutil detach "$vol" -force -quiet 2>/dev/null || true
done
# Orphaned create-dmg scratch mounts — but only ones backed by an image
# in THIS build directory. Never somebody else's disk image.
hdiutil info | awk '
    /^image-path/  { p = $0; sub(/^image-path[ \t]*:[ \t]*/, "", p) }
    /^\/dev\/disk/ { if ($NF ~ "^/Volumes/" && p ~ "/rw\\..*hyperbin.*\\.dmg$") print $NF }
' | while IFS= read -r m; do
    echo "detaching orphaned scratch mount: $m"
    hdiutil detach "$m" -force -quiet 2>/dev/null || true
done

# --- Qt --------------------------------------------------------------------
# macdeployqt copies the Qt frameworks and QML modules in. It runs before
# signing, because it rewrites the very binaries that get signed.
if [ -n "${QT_ROOT_DIR:-}" ] && [ -x "$QT_ROOT_DIR/bin/macdeployqt" ]; then
    MACDEPLOYQT="$QT_ROOT_DIR/bin/macdeployqt"
else
    MACDEPLOYQT="$(command -v macdeployqt || true)"
fi
[ -n "$MACDEPLOYQT" ] || { echo "macdeployqt not found" >&2; exit 1; }
"$MACDEPLOYQT" "$APP" -qmldir="$REPO_ROOT/qml" -verbose=1

# --- signing ---------------------------------------------------------------
IDENTITY="${CODESIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
    IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null \
               | grep "Developer ID Application" | awk 'NR==1' \
               | grep -o '"[^"]*"' | tr -d '"' || true)
fi

if [ -n "$IDENTITY" ]; then
    echo "Signing with: $IDENTITY"
    # Inside out. codesign seals what it finds, so anything signed after
    # its container invalidates that container's seal — and the failure
    # shows up at launch on someone else's machine, not here.
    find "$APP" \( -name "*.dylib" -o -name "*.so" \) -print0 \
        | xargs -0 -I{} codesign --force --options runtime --timestamp \
                                 --sign "$IDENTITY" {}
    # Frameworks are signed through Versions/<X>, never through the
    # framework directory itself.
    #
    # A versioned framework's top level is a set of symlinks into the
    # current version, and codesign cannot tell that shape apart from an
    # app bundle — it stops with "bundle format is ambiguous (could be
    # app or framework)". Naming the real version directory removes the
    # ambiguity, and signing it is what signs the framework.
    for fw in "$APP"/Contents/Frameworks/*.framework; do
        [ -d "$fw" ] || continue
        # Sparkle carries its own executables: two XPC services, the
        # Autoupdate helper, and the progress app. Each is a bundle in
        # its own right and has to be sealed before the thing containing
        # it. -maxdepth keeps this off the symlinked duplicates at the
        # framework root, which would otherwise be signed twice and
        # break the seal made the first time.
        for ver in "$fw"/Versions/*; do
            [ -d "$ver" ] && [ ! -L "$ver" ] || continue
            find "$ver" -maxdepth 2 \
                 \( -name "*.xpc" -o -name "*.app" -o -name "Autoupdate" \) \
                 -print0 \
                | xargs -0 -r -I{} codesign --force --options runtime \
                                            --timestamp --sign "$IDENTITY" {}
            codesign --force --options runtime --timestamp \
                     --sign "$IDENTITY" "$ver"
        done
    done
    codesign --force --options runtime --timestamp \
             --entitlements "$REPO_ROOT/hyperbin.entitlements" \
             --sign "$IDENTITY" "$APP"
    codesign --verify --deep --strict --verbose=2 "$APP"
else
    echo "WARNING: no Developer ID identity found. The bundle stays" >&2
    echo "         ad-hoc signed — Gatekeeper will refuse it elsewhere." >&2
fi

# --- DMG -------------------------------------------------------------------
command -v create-dmg >/dev/null || brew install create-dmg

STAGE="$BUILD_DIR/dmg_staging"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"

# create-dmg returns 2 when it produced the image but could not apply the
# window layout — which happens on a headless runner with no Finder. The
# image is still good, so the file's existence is the real test.
create-dmg \
    --volname "hyperbin" \
    --window-pos 200 120 \
    --window-size 660 400 \
    --icon-size 110 \
    --icon "hyperbin.app" 165 185 \
    --hide-extension "hyperbin.app" \
    --app-drop-link 495 185 \
    --background "$REPO_ROOT/packaging/macos/dmg-background.tiff" \
    --no-internet-enable \
    "$DMG" "$STAGE" || true

rm -rf "$STAGE"
[ -f "$DMG" ] || { echo "DMG was not created" >&2; exit 1; }
echo "$DMG"
