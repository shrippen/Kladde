#!/usr/bin/env bash
# The Kladde APK (arm64-v8a), after Plasmai's scripts/build-android.sh.
#   scripts/build-android.sh [debug|release]
# debug:   de.arianw.kladde.dev ("Kladde Dev"), installs next to a release.
# release: signed when KLADDE_KEYSTORE_PATH, KLADDE_KEYSTORE_PASSWORD, KLADDE_KEY_ALIAS and
#          KLADDE_KEY_PASSWORD are set, unsigned otherwise. Never with the demo (checked).
# Installs what is missing: NDK and SDK packages (sdkmanager), Qt via aqtinstall, the
# dependencies (scripts/build-deps-android.sh). Output: dist/android/kladde-<mode>.apk.
# KLADDE_VERBOSE=1 (CI) shows the whole build output.
set -euo pipefail
MODE="${1:-debug}"
case "$MODE" in debug|release) ;; *) echo "usage: $0 [debug|release]" >&2; exit 2 ;; esac

REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/android-sdk}}"
QT_VER="6.11.3"
QT_ANDROID="$HOME/Qt/$QT_VER/android_arm64_v8a"
QT_HOST="$HOME/Qt/$QT_VER/gcc_64"
DEPS="${KLADDE_ANDROID_DEPS:-$HOME/kf6-android}"
BUILD="$REPO/build-android"

info()  { echo "[kladde] $*"; }
error() { echo "[kladde] ERROR: $*" >&2; exit 1; }
show()  { if [ -n "${KLADDE_VERBOSE:-}" ]; then cat; else tail -"$1"; fi; }

# ── NDK r27c (the one Qt 6.11 is built with), platform and build tools ──
NDK_PATH=$(ls -d "$SDK_ROOT"/ndk/27.2.* 2>/dev/null | head -1 || true)
if [ -z "$NDK_PATH" ] || [ ! -d "$SDK_ROOT/platforms/android-36" ] || [ ! -d "$SDK_ROOT/build-tools/36.0.0" ]; then
    SDKMANAGER="$SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
    [ -x "$SDKMANAGER" ] || error "no sdkmanager in $SDK_ROOT (install the Android command line tools)"
    info "Installing NDK 27.2, android-36 and build-tools 36.0.0"
    (yes || true) | "$SDKMANAGER" --sdk_root="$SDK_ROOT" "ndk;27.2.12479018" "platforms;android-36" "build-tools;36.0.0" > /dev/null
    NDK_PATH=$(ls -d "$SDK_ROOT"/ndk/27.2.* | head -1)
fi
info "NDK: $NDK_PATH"

# ── Qt for Android and the host tools ──
if [ ! -d "$QT_ANDROID" ] || [ ! -d "$QT_HOST" ]; then
    command -v aqt > /dev/null || pip install --user --break-system-packages aqtinstall > /dev/null
    export PATH="$HOME/.local/bin:$PATH"
    info "Installing Qt $QT_VER (Android arm64 and host) with aqtinstall"
    # Kirigami needs ShaderTools, which is not in the base install.
    aqt install-qt all_os android "$QT_VER" android_arm64_v8a -m qtshadertools -O "$HOME/Qt" | show 2
    aqt install-qt linux desktop "$QT_VER" linux_gcc_64 -m qtshadertools -O "$HOME/Qt" | show 2
fi

# ── KF6 (Kirigami), QtKeychain, OpenSSL ──
ANDROID_SDK_ROOT="$SDK_ROOT" "$REPO/scripts/build-deps-android.sh" "$DEPS"

# ── App ──
info "Building Kladde ($MODE)"
rm -rf "$BUILD"
BUILD_TYPE=Debug; [ "$MODE" = release ] && BUILD_TYPE=Release
ANDROID_SDK_ROOT="$SDK_ROOT" cmake -S "$REPO" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-34 \
    -DCMAKE_PREFIX_PATH="$QT_ANDROID;$DEPS" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
    -DQT_HOST_PATH="$QT_HOST" \
    -DANDROID_SDK_ROOT="$SDK_ROOT" \
    -DECM_DIR="$DEPS/share/ECM/cmake" \
    -DKLADDE_ANDROID_DEPS="$DEPS" \
    -DKLADDE_TESTS=OFF -DKLADDE_DEMO=OFF \
    2>&1 | show 5
cmake --build "$BUILD" --target kladde 2>&1 | show 10
# androiddeployqt: collects the libraries and QML modules into the Gradle project and
# builds the debug APK.
cmake --build "$BUILD" --target kladde_make_apk 2>&1 | show 10

GRADLE_DIR=$(dirname "$(find "$BUILD" -path '*android-build*' -name gradlew -print -quit)")
[ -d "$GRADLE_DIR" ] || error "no Gradle project from androiddeployqt"
if [ "$MODE" = release ]; then
    if [ -n "${KLADDE_KEYSTORE_PATH:-}" ]; then
        info "Signing with $KLADDE_KEYSTORE_PATH"
        {
            echo "RELEASE_STORE_FILE=$KLADDE_KEYSTORE_PATH"
            echo "RELEASE_STORE_PASSWORD=${KLADDE_KEYSTORE_PASSWORD:?KLADDE_KEYSTORE_PASSWORD not set}"
            echo "RELEASE_KEY_ALIAS=${KLADDE_KEY_ALIAS:?KLADDE_KEY_ALIAS not set}"
            echo "RELEASE_KEY_PASSWORD=${KLADDE_KEY_PASSWORD:?KLADDE_KEY_PASSWORD not set}"
        } >> "$GRADLE_DIR/gradle.properties"
    else
        info "No KLADDE_KEYSTORE_PATH: the release APK stays unsigned"
    fi
    (cd "$GRADLE_DIR" && ./gradlew --no-daemon assembleRelease 2>&1 | show 10)
fi

APK=$(find "$GRADLE_DIR/build/outputs/apk/$MODE" -name '*.apk' -print -quit)
[ -n "$APK" ] || error "no APK in $GRADLE_DIR/build/outputs/apk/$MODE"

# The demo never ships: look into every file of the package.
CHECK="$BUILD/apk-check"
rm -rf "$CHECK" && mkdir -p "$CHECK" && (cd "$CHECK" && unzip -q "$APK")
find "$CHECK" -type f \( -name '*.so' -o -name '*.rcc' -o -name '*.json' \) -print0 \
    | xargs -0 -I{} cmake -DAPP={} -P "$REPO/tests/no_demo.cmake"
rm -rf "$CHECK"

mkdir -p "$REPO/dist/android"
OUT="$REPO/dist/android/kladde-$MODE.apk"
cp "$APK" "$OUT"
info "APK: $OUT"
info "Install: adb install -r $OUT"
