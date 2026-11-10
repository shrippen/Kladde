# Release steps

Release steps, in this order (`X.Y.Z` the new version, `A.B.C` the previous one). A release is done only after step 9.

1. Version: `project(kladde VERSION X.Y.Z)` in `CMakeLists.txt`; the APK takes its version name and code (major·10000 + minor·100 + patch) from it.
2. Write the release log before the tag in `CHANGELOG.md`, the single source: a section `## [X.Y.Z] — YYYY-MM-DD` at the top (newest first) with `### English` and `### Deutsch`, each a lead (what the release is about), then the changes grouped as `#### New`, `Improved`, `Fixed` and `Note` (German: `Neu`, `Verbessert`, `Behoben`, `Hinweis`; breaking changes, steps the user must take), then the compare link (`https://github.com/shrippen/Kladde/compare/vA.B.C...vX.Y.Z`). Each point says what changed for the user and why, not which code moved. Check the headings with `scripts/release-notes.sh --check` (CI runs it on every push), read the notes with `scripts/release-notes.sh X.Y.Z`.
3. Landing page `docs/index.html` (Kante `.changelog`): the same text in English and German as a new entry at the top of `#changes`; from the fourth entry on, the oldest move into `details.changelog-more`. Bump the footer version (`vX.Y`, linked to `#changes`). Screenshots from the demo world, taken now for the tagged release, not with every change: `../shrippen.github.io/demo/tools/.venv/bin/python ../shrippen.github.io/demo/tools/screenshots.py kladde`, then `../shrippen.github.io/demo/tools/use-shots.py kladde`; for a visible change also `docs/shots/release-<version>-<name>-<lang>.webp`.
4. Open the PR on Gitea, review it as the skill `pr-review` says (a release is published: the owner confirms it in words), merge on Gitea.
5. Tag the merge commit and push the tag to Gitea only: `git tag vX.Y.Z <merge commit> && git push origin vX.Y.Z` (the mirror carries it to GitHub).
6. Gitea release, notes only, never files: `scripts/release-notes.sh X.Y.Z > notes.md`, then `gt release create vX.Y.Z --repo shrippen/Kladde --title "Kladde X.Y.Z" --notes-file notes.md --no-input`.
7. GitHub: the tag runs `.github/workflows/release.yml`. It stops when the tag and the project version differ, creates the GitHub release as a draft with the same notes (stops without the section), builds the release APK with `scripts/build-android.sh release` (which stops when demo code or data is in the package), signs it, attaches `Kladde-X.Y.Z.apk` and then publishes the release. Check the run (`gh run list -R shrippen/Kladde --workflow release.yml`) and the asset (`gh release view vX.Y.Z -R shrippen/Kladde`).
8. The landing page lives on GitHub Pages of the mirror (`main`, `/docs`): it updates with the merge.
9. Done when https://shrippen.github.io/Kladde/ shows X.Y.Z at the top of `#changes` and in the footer, and the GitHub release has the APK.

Signing: the release key is the owner's, never in a repository. GitHub secrets `KLADDE_KEYSTORE_B64` (the keystore, base64), `KLADDE_KEYSTORE_PASSWORD`, `KLADDE_KEY_ALIAS`, `KLADDE_KEY_PASSWORD`. Without the key a release stops; a lost key means installed copies can no longer update. Test the release build by hand: `release.yml` via workflow_dispatch without `tag`.

`scripts/release-notes.sh` is a copy of Kante's `tools/release-notes.sh`: change it only there, then sync with `../Kante/tools/sync-release-notes.sh "$PWD"`.
