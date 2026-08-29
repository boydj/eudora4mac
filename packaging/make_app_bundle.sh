#!/usr/bin/env bash
# Assemble Eudora.app from a SwiftPM release build and zip it.
#
# Usage: packaging/make_app_bundle.sh [products-dir] [out-dir] [version]
#   products-dir  SwiftPM build products (default .build/release)
#   out-dir       where Eudora.app and the zip land (default dist)
#   version       release version, e.g. "6.2.4-macos.1" (default 0.0.0-dev).
#                 The full string names the zip; the Info.plist gets only its
#                 leading numeric MAJOR[.MINOR[.PATCH]] prefix, because
#                 CFBundleShortVersionString / CFBundleVersion must be a
#                 period-separated list of at most three integers.
#
# Run from the repository root, after `swift build -c release`.
#
# Signing: by default the bundle is ad-hoc signed (enough to run locally;
# right-click > Open the first time on another machine).  For real
# distribution set these environment variables and the script will sign with
# a Developer ID and notarize:
#
#   MACOS_SIGN_IDENTITY   e.g. "Developer ID Application: Your Name (TEAMID)"
#                         The matching cert+key must be in the login keychain.
#   MACOS_NOTARIZE=1      after signing, submit to Apple's notary service and
#                         staple the ticket.  Requires either:
#                           AC_NOTARY_PROFILE   a `notarytool store-credentials`
#                                               keychain profile name, or
#                           AC_API_KEY_ID + AC_API_ISSUER_ID + AC_API_KEY_PATH
#                                               an App Store Connect API key.
#
# All of these are secrets we cannot ship; the CI release job wires them from
# repository secrets when present and otherwise falls back to ad-hoc signing.

set -euo pipefail

PRODUCTS_DIR="${1:-.build/release}"
OUT_DIR="${2:-dist}"
VERSION="${3:-0.0.0-dev}"

# Info.plist version keys accept only up to three integers, so strip any
# pre-release suffix (e.g. "6.2.4-macos.1" -> "6.2.4"); the full VERSION
# still names the zip and the GitHub Release.
PLIST_VERSION="$(printf '%s' "$VERSION" | sed -E 's/^([0-9]+(\.[0-9]+){0,2}).*/\1/')"
[[ -z "$PLIST_VERSION" ]] && PLIST_VERSION="0.0.0"

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

# SwiftPM resource bundle (e.g. EudoraCore_EudoraApp.bundle) belongs in
# Contents/Resources.  Match by glob so a naming change doesn't silently drop
# it; note it plainly when there's none rather than skipping in silence.
shopt -s nullglob
resource_bundles=("$PRODUCTS_DIR"/*_EudoraApp.bundle "$PRODUCTS_DIR/$RESOURCE_BUNDLE")
copied_bundle=""
for b in "${resource_bundles[@]}"; do
    if [[ -d "$b" && "$b" != "$copied_bundle" ]]; then
        cp -R "$b" "$APP/Contents/Resources/"
        copied_bundle="$b"
        echo "bundled resources: $(basename "$b")"
    fi
done
shopt -u nullglob
if [[ -z "$copied_bundle" ]]; then
    echo "note: no SwiftPM resource bundle found in $PRODUCTS_DIR (the app" \
         "reads its icon from Contents/Resources directly, so this is only" \
         "a problem if a target adds resources it loads via Bundle.module)"
fi

# The classic icon, for Finder/Dock via CFBundleIconFile.
cp "$REPO_ROOT/swift/EudoraApp/Resources/Eudora.icns" \
   "$APP/Contents/Resources/Eudora.icns"

sed "s/@VERSION@/$PLIST_VERSION/g" "$REPO_ROOT/packaging/Info.plist.in" \
    > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# Signature.  With a Developer ID identity, hardened-runtime sign (required
# for notarization); otherwise ad-hoc sign so it at least runs locally.
if [[ -n "${MACOS_SIGN_IDENTITY:-}" ]]; then
    echo "signing with Developer ID: $MACOS_SIGN_IDENTITY"
    codesign --force --deep --options runtime --timestamp \
             --sign "$MACOS_SIGN_IDENTITY" "$APP"
else
    echo "ad-hoc signing (set MACOS_SIGN_IDENTITY for a distributable build)"
    codesign --force --deep --sign - "$APP"
fi

ZIP="$OUT_DIR/Eudora-$VERSION-macos-arm64.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

# Notarization (only meaningful with a real Developer ID signature).
if [[ "${MACOS_NOTARIZE:-}" == "1" && -n "${MACOS_SIGN_IDENTITY:-}" ]]; then
    echo "submitting $ZIP for notarization…"
    if [[ -n "${AC_NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "$ZIP" \
            --keychain-profile "$AC_NOTARY_PROFILE" --wait
    elif [[ -n "${AC_API_KEY_ID:-}" && -n "${AC_API_ISSUER_ID:-}" \
            && -n "${AC_API_KEY_PATH:-}" ]]; then
        xcrun notarytool submit "$ZIP" \
            --key "$AC_API_KEY_PATH" --key-id "$AC_API_KEY_ID" \
            --issuer "$AC_API_ISSUER_ID" --wait
    else
        echo "error: MACOS_NOTARIZE=1 but no notary credentials provided" >&2
        exit 1
    fi
    # Staple the ticket into the .app, then re-zip so the download carries it.
    xcrun stapler staple "$APP"
    rm -f "$ZIP"
    ditto -c -k --keepParent "$APP" "$ZIP"
    echo "notarized and stapled"
fi

echo "built $APP"
echo "zipped $ZIP"
