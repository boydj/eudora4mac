#!/usr/bin/env bash
# Assemble Eudora.app from a SwiftPM release build and zip it.
#
# Usage: packaging/make_app_bundle.sh [products-dir] [out-dir] [version]
#   products-dir  SwiftPM build products (default .build/release)
#   out-dir       where Eudora.app and the zip land (default dist)
#   version       CFBundleShortVersionString (default 0.0.0-dev)
#
# Run from the repository root, after `swift build -c release`.
# The bundle is ad-hoc signed; distributing outside your own machines
# properly needs a Developer ID certificate + notarization (slot a real
# `codesign --sign "Developer ID Application: …"` and `notarytool submit`
# in below where the ad-hoc signing happens).

set -euo pipefail

PRODUCTS_DIR="${1:-.build/release}"
OUT_DIR="${2:-dist}"
VERSION="${3:-0.0.0-dev}"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$OUT_DIR/Eudora.app"
RESOURCE_BUNDLE="EudoraCore_EudoraApp.bundle"

if [[ ! -x "$PRODUCTS_DIR/EudoraApp" ]]; then
    echo "error: $PRODUCTS_DIR/EudoraApp not found — run 'swift build -c release' first" >&2
    exit 1
fi

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

cp "$PRODUCTS_DIR/EudoraApp" "$APP/Contents/MacOS/EudoraApp"

# SwiftPM's Bundle.module accessor looks in Bundle.main.resourceURL, so
# the package resource bundle belongs in Contents/Resources.
if [[ -d "$PRODUCTS_DIR/$RESOURCE_BUNDLE" ]]; then
    cp -R "$PRODUCTS_DIR/$RESOURCE_BUNDLE" "$APP/Contents/Resources/"
fi

# The classic icon, for Finder/Dock via CFBundleIconFile.
cp "$REPO_ROOT/swift/EudoraApp/Resources/Eudora.icns" \
   "$APP/Contents/Resources/Eudora.icns"

sed "s/@VERSION@/$VERSION/g" "$REPO_ROOT/packaging/Info.plist.in" \
    > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# Ad-hoc signature: enough to run locally (right-click > Open the first
# time on another machine, since it is not notarized).
codesign --force --deep --sign - "$APP"

ZIP="$OUT_DIR/Eudora-$VERSION-macos-arm64.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

echo "built $APP"
echo "zipped $ZIP"
