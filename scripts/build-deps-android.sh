#!/usr/bin/env bash
# What the Android build needs besides Qt, cross-compiled for arm64-v8a into one prefix:
# extra-cmake-modules, KCoreAddons and Kirigami (KF6), QtKeychain (static) and OpenSSL
# (libssl_3.so / libcrypto_3.so, the names Qt loads on Android). After Plasmai's
# scripts/build-kf6-android.sh; the KF6 part is the same, so both can share $HOME/kf6-android.
#
# Usage: scripts/build-deps-android.sh [prefix]   (default $HOME/kf6-android)
# Skips what is there in the right version; -f as first argument builds everything again.
# Needs the NDK and Qt for Android (scripts/build-android.sh installs both).
set -euo pipefail

FORCE=0
if [ "${1:-}" = "-f" ]; then FORCE=1; shift; fi
PREFIX="${1:-$HOME/kf6-android}"

KF6_VERSION="6.30.0"
QTKEYCHAIN_TAG="0.15.0"
OPENSSL_VERSION="3.5.8"   # LTS until 2030-04; raise for security releases
QT_VER="6.11.3"
SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/android-sdk}"
QT_ANDROID="$HOME/Qt/$QT_VER/android_arm64_v8a"
QT_HOST="$HOME/Qt/$QT_VER/gcc_64"
API=28   # the dependencies run from Android 9; the app itself needs 14 (app/CMakeLists.txt)

info()  { echo "[deps] $*"; }
error() { echo "[deps] ERROR: $*" >&2; exit 1; }

NDK_PATH=$(ls -d "$SDK_ROOT"/ndk/27.2.* 2>/dev/null | head -1)
[ -d "$NDK_PATH" ] || error "Android NDK 27.2 not found under $SDK_ROOT/ndk (scripts/build-android.sh installs it)"
[ -d "$QT_ANDROID" ] && [ -d "$QT_HOST" ] || error "Qt $QT_VER for Android not found under \$HOME/Qt (scripts/build-android.sh installs it)"

mkdir -p "${TMPDIR:-$HOME/.cache}"
WORK="$(mktemp -d -p "${TMPDIR:-$HOME/.cache}")"
trap 'rm -rf "$WORK"' EXIT

kf6_ok() {
    grep -qs "\"$KF6_VERSION\"" "$PREFIX/lib/cmake/KF6Kirigami/KF6KirigamiConfigVersion.cmake" \
        && [ -f "$PREFIX/lib/cmake/Qt6Keychain/Qt6KeychainConfig.cmake" ]
}
openssl_ok() {
    grep -qsx "$OPENSSL_VERSION" "$PREFIX/openssl/VERSION" \
        && [ -f "$PREFIX/openssl/libssl_3.so" ] && [ -f "$PREFIX/openssl/libcrypto_3.so" ]
}

if [ "$FORCE" -eq 1 ] || ! kf6_ok; then
    # Another KF6 (built against another Qt) must not mix with this one.
    rm -rf "${PREFIX:?}"
    mkdir -p "$PREFIX"
    CROSS_ARGS=(
        -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake"
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM="android-$API"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_PREFIX_PATH="$QT_ANDROID;$PREFIX"
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
        -DQT_HOST_PATH="$QT_HOST"
        -DECM_DIR="$PREFIX/share/ECM/cmake"
        -DBUILD_TESTING=OFF -DBUILD_QCH=OFF
    )
    clone() { git clone --quiet -c advice.detachedHead=false --depth 1 --branch "v$KF6_VERSION" "https://invent.kde.org/frameworks/$1.git" "$WORK/$1"; }
    info "Cloning extra-cmake-modules, kcoreaddons, kirigami $KF6_VERSION and QtKeychain $QTKEYCHAIN_TAG"
    clone extra-cmake-modules & clone kcoreaddons & clone kirigami &
    git clone --quiet -c advice.detachedHead=false --depth 1 --branch "$QTKEYCHAIN_TAG" https://github.com/frankosterfeld/qtkeychain.git "$WORK/qtkeychain" &
    wait

    cmake -B "$WORK/extra-cmake-modules/build" -S "$WORK/extra-cmake-modules" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
        -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF > /dev/null
    cmake --build "$WORK/extra-cmake-modules/build" > /dev/null
    cmake --install "$WORK/extra-cmake-modules/build" > /dev/null

    for m in kcoreaddons kirigami; do
        info "Building $m"
        cmake -B "$WORK/$m/build" -S "$WORK/$m" "${CROSS_ARGS[@]}" -DBUILD_EXAMPLES=OFF -DDESKTOP_ENABLED=OFF > /dev/null
        cmake --build "$WORK/$m/build" -j"$(nproc)" > /dev/null
        cmake --install "$WORK/$m/build" > /dev/null
    done

    # Static: linked into the app library, nothing extra for androiddeployqt to find.
    info "Building QtKeychain"
    cmake -B "$WORK/qtkeychain/build" -S "$WORK/qtkeychain" "${CROSS_ARGS[@]}" \
        -DBUILD_WITH_QT6=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_TRANSLATIONS=OFF -DBUILD_TEST_APPLICATION=OFF > /dev/null
    cmake --build "$WORK/qtkeychain/build" -j"$(nproc)" > /dev/null
    cmake --install "$WORK/qtkeychain/build" > /dev/null
else
    info "KF6 $KF6_VERSION and QtKeychain already in $PREFIX"
fi

if [ "$FORCE" -eq 1 ] || ! openssl_ok; then
    info "Building OpenSSL $OPENSSL_VERSION"
    # OpenSSL's Makefile template takes Time::Piece for one date. Fedora splits it into its
    # own package (perl-Time-Piece); without it, a stand-in with just what the template uses.
    if ! perl -MTime::Piece -e1 2> /dev/null; then
        mkdir -p "$WORK/perl/Time"
        cat > "$WORK/perl/Time/Piece.pm" <<'PERL'
package Time::Piece;
use POSIX ();
my %mon = (Jan => 0, Feb => 1, Mar => 2, Apr => 3, May => 4, Jun => 5,
           Jul => 6, Aug => 7, Sep => 8, Oct => 9, Nov => 10, Dec => 11);
sub import {
    my $caller = caller;
    no strict 'refs';
    *{"${caller}::localtime"} = sub { bless [CORE::localtime(@_ ? $_[0] : time)], __PACKAGE__ };
}
sub strptime {
    my ($class, $s) = @_;   # only "%d %b %Y"
    my ($d, $m, $y) = $s =~ /^(\d+)\s+(\w{3})\s+(\d{4})$/ or die "unsupported date: $s";
    bless [0, 0, 0, $d, $mon{$m}, $y - 1900], $class;
}
sub strftime { my ($self, $f) = @_; POSIX::strftime($f, @$self) }
1;
PERL
        export PERL5LIB="$WORK/perl${PERL5LIB:+:$PERL5LIB}"
    fi
    git clone --quiet -c advice.detachedHead=false --depth 1 --branch "openssl-$OPENSSL_VERSION" https://github.com/openssl/openssl.git "$WORK/openssl"
    # A target named android-*-<arch> keeps OpenSSL's NDK setup; only the file suffix changes.
    cat > "$WORK/openssl-qt.conf" <<'CONF'
my %targets = (
    "android-qt-arm64" => {
        inherit_from     => [ "android-arm64" ],
        shared_extension => "_3.so",
    },
);
CONF
    (
        cd "$WORK/openssl"
        export ANDROID_NDK_ROOT="$NDK_PATH"
        export PATH="$NDK_PATH/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
        ./Configure --config="$WORK/openssl-qt.conf" shared no-tests android-qt-arm64 -D__ANDROID_API__=$API
        make -j"$(nproc)" build_generated
        make -j"$(nproc)" libcrypto_3.so
        # Without it libssl takes libcrypto.a in statically instead of needing libcrypto_3.so.
        ln -sf libcrypto_3.so libcrypto.so
        make -j"$(nproc)" build_libs
    ) > "$WORK/openssl.log" 2>&1 || { tail -n 60 "$WORK/openssl.log"; error "OpenSSL failed"; }
    mkdir -p "$PREFIX/openssl"
    cp "$WORK/openssl/libssl_3.so" "$WORK/openssl/libcrypto_3.so" "$PREFIX/openssl/"
    echo "$OPENSSL_VERSION" > "$PREFIX/openssl/VERSION"
else
    info "OpenSSL $OPENSSL_VERSION already in $PREFIX"
fi
info "ready: $PREFIX"
