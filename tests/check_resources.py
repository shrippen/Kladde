"""What the app loads from its resources must be listed there; a missing file still builds
and fails only at run time (on Android without a console to see why).

- every QML and JS file in app/qml is in app/qml/kladde.qrc;
- every icon name the QML uses ("icons: [...]", "icon.name: ...", "source: ...") is one of
  the Breeze icons in app/icons/icons.qrc (Android has no icon theme of its own);
- every permission and package query of android/sync's manifest is in the app's: the APK
  compiles the sync from its sources, its manifest is not merged (seen on the phone,
  1.11.2026: app names shown as packages, the launcher query was missing).
Run: python3 tests/check_resources.py
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QML = ROOT / "app" / "qml"


def qrc_files(qrc):
    return {f.get("alias") or f.text for f in ET.parse(qrc).getroot().iter("file")}


ANDROID = "{http://schemas.android.com/apk/res/android}"


def manifest_entries(manifest):
    """Permissions and package queries, e.g. "uses-permission READ_STEPS", "queries intent MAIN LAUNCHER"."""
    root = ET.parse(manifest).getroot()
    out = {f"uses-permission {p.get(ANDROID + 'name')}" for p in root.iter("uses-permission")}
    for q in root.iter("queries"):
        for child in q:
            names = [e.get(ANDROID + "name") for e in child.iter() if e.get(ANDROID + "name")]
            out.add(f"queries {child.tag} {' '.join(names)}")
    return out


def main():
    problems = []

    listed = qrc_files(QML / "kladde.qrc")
    for f in sorted(QML.glob("*.qml")) + sorted(QML.glob("*.js")):
        if f.name not in listed:
            problems.append(f"{f.name} missing from app/qml/kladde.qrc")

    icons = {Path(f).stem for f in qrc_files(ROOT / "app" / "icons" / "icons.qrc") if f.endswith(".svg")}
    used = set()
    for f in QML.glob("*.qml"):
        text = f.read_text(encoding="utf-8")
        for block in re.findall(r"\bicons:\s*\[([^\]]*)\]", text):
            used.update(re.findall(r'"([\w-]+)"', block))
        used.update(re.findall(r'\bicon\.name:\s*"([\w-]+)"', text))
        used.update(re.findall(r'\b(?:iconName|source):\s*"([\w-]+)"', text))
    for name in sorted(used - icons):
        problems.append(f'icon "{name}" missing from app/icons (copy it from breeze-dark, add to icons.qrc)')

    app_manifest = manifest_entries(ROOT / "app" / "android" / "AndroidManifest.xml")
    for entry in sorted(manifest_entries(ROOT / "android" / "sync" / "src" / "main" / "AndroidManifest.xml") - app_manifest):
        problems.append(f"{entry} missing from app/android/AndroidManifest.xml (android/sync needs it)")

    for p in problems:
        print(p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
