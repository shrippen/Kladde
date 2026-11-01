"""Every native library in the APK finds what it needs: in the APK or in Android itself.

androiddeployqt follows the dependencies of Qt's own libraries, not those of the Kirigami QML
plugins from the dependency prefix. A missing one still builds and fails only on the phone,
where Qt reports a garbled name ("Cannot load library ... Kirigamiplugin ... not found").
Run: python3 tests/check_apk_libs.py <apk> <ndk>   (scripts/build-android.sh does)
"""

import glob
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ABI_DIR = "lib/arm64-v8a/"
NDK_TRIPLE = "aarch64-linux-android"


def needed(readelf, lib):
    out = subprocess.run([readelf, "-d", str(lib)], capture_output=True, text=True, check=True).stdout
    # e.g. " 0x0000000000000001 (NEEDED)  Shared library: [libKirigami.so]"
    return [line.split("[", 1)[1].rstrip("]") for line in out.splitlines() if "(NEEDED)" in line]


def main(apk, ndk):
    prebuilt = Path(ndk) / "toolchains" / "llvm" / "prebuilt" / "linux-x86_64"
    readelf = prebuilt / "bin" / "llvm-readelf"
    system = {Path(p).name for p in glob.glob(str(prebuilt / "sysroot" / "usr" / "lib" / NDK_TRIPLE / "*" / "*.so"))}
    if not system:
        print(f"no NDK system libraries under {prebuilt}", file=sys.stderr)
        return 2

    problems = []
    with tempfile.TemporaryDirectory(dir=Path(apk).parent) as tmp, zipfile.ZipFile(apk) as z:
        libs = [n for n in z.namelist() if n.startswith(ABI_DIR) and n.endswith(".so")]
        packed = {Path(n).name for n in libs}
        for name in libs:
            path = Path(z.extract(name, tmp))
            for dep in needed(readelf, path):
                if dep not in packed and dep not in system:
                    problems.append(f"{Path(name).name} needs {dep}, not in the APK")

    for p in sorted(problems):
        print(p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
