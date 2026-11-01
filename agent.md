- When writing something intended for human consumption, (comment, commit message, reply to prompt) use as few words as possible. Pick every word meticulously to reduce the volume to a strict minimum. Be down to the point. Less is more.

- Avoid superlatives and praise. Stop telling me I am absolutely right. Give me the cold hard truth.

- Avoid magic numbers and strings by extracting recurring or meaningful values into descriptive constants (const) or enums. Keep self-explanatory, one-off values inline to avoid clutter. If a value comes from a spec (e.g. HTTP 200 OK), use a constant regardless.

- Reduce code indentation. Avoid Arrow Anti-Pattern. Leverage early return and continue.

- Keep function names short. Less than 30 characters.

- Use enums instead of booleans for function parameters.

- Let the reader of the code breathe. Add empty lines between logical blocks of code.

- Add a small, to the point, comment to explain *what* the block does and *why*. Use examples when possible. Propose ASCII drawings to explain complete systems.

- Treat member visibility changes as a breaking design shift. Keep all fields and functions private unless external access is strictly required by the design. Prompt the user for explicit approval before changing any access modifier from private to internal or public.

- Program to levels of abstraction. Lower-level mechanics (e.g., raw hardware I/O, sector parsing, direct socket streams) must be encapsulated in a dedicated driver/abstraction layer. Expose clean, high-level APIs to the rest of the application so calling code works with domain concepts, not raw implementation details.

- Don't touch blocks of code unrelated to the feature you implement. e.g. Don't add comments to a block of code if you did not create it or modify it. As much as possible try to minimize the number of changed lines when implementing a feature.

- Strictly adhere to the layered boundary hierarchy: each layer may only communicate with its immediate neighbor directly below it. Never "punch holes" through layers (e.g., controllers or UI components must never directly call database queries, raw hardware drivers, or low-level network clients; always route through the intermediate service/abstraction layer).

- Always use {}, even on a one-line "if" statement.

- Every visualization (chart, graph, heatmap) in a popup or page has axis labels or a legend; without them it is useless. Give it hover tooltips where possible. Tiles stay without axes and legends, so they read at a glance; their popup carries the details.

- Test integrations against a real instance where possible. HealthLog: the owner's instance `https://hl.arianw.de` with the `measurements:write` token in `token.txt` (repo root, ignored by Git; owner, 1.11.2026: free to test with it). Write only synthetic data dated far in the past (March 2001, ids starting `kladde-test-`), so it never shows in the owner's current charts; never print, log or commit the token. Without the token the test is skipped: in cloud environments and CI, tests against fakes are enough.

When you write a commit message, follow these 7 rules:
Rule 1: Separate the subject line from the body with a single blank line.
Rule 2: Limit the subject line to 50 characters (72 is the absolute hard limit).
Rule 3: Capitalize the first letter of the subject line.
Rule 4: Do not end the subject line with a period.
Rule 5: Use the imperative mood in the subject line (e.g., "Fix bug," "Add feature," 
        not "Fixed" or "Adds"). Test formula: It must complete the sentence: "If applied,
        this commit will [your subject line here]".
Rule 6: Wrap the body text manually at 72 characters to prevent Git formatting issues.
Rule 7: Use the body to explain what and why vs. how. Assume the code explains the how;
        the message must explain the context and reasoning. 

- If the prompt indicates that a bug is being fixed, don't write the fix right away. First write the test. Observe it failing. Then write the fix. And observe the test passing.
  - This holds for every bug, however it was found (a user report, a review, your own run),
    and for performance problems and crashes too: stutter, late frames, a hang, a panic, a
    leak. The test stays in the suite; a one-off script run is not a test.
  - Name the failure in the commit or PR: the test and what it printed before the fix.

## Kladde – Projekt-Wissen

## Zweck
Bringt Gesundheitsdaten der Pebble in die eigene HealthLog-Instanz: auf Android aus
Health Connect, später auf Plasma Mobile direkt von der Uhr (libpebble3-Daemon über D-Bus).
Anforderungen: `docs/lastenheft.md` (Ids LF-xx, NF-xx in Commits und PRs nennen),
Plan mit Häkchen: `docs/plan.md`, Quellen: `docs/referenzen.md`. Aktuell halten.

## Schichten (nur zum direkten Nachbarn darunter)
```
app/        Kirigami-Oberfläche (QML mit Kante), Connection und Co. als QML-Objekte
  ↓
core/       Abbildung, Batches, Warteschlange, HealthLogClient   (nur Qt Core + Network)
  ↑
Quellen     Health Connect (Kotlin, Android) | Pebble-Daemon (D-Bus, Linux)
            liefern nur das Austauschformat (core/hcjson.h), nie direkt an HealthLog
```
- QML spricht nie HTTP; alles über Objekte aus `app/`, die `core/` nutzen.
- `core/` bleibt ohne Qt GUI, damit er in Tests, `kladde-push` und im Android-Hintergrundjob läuft.

## Konventionen
- C++20, Qt 6. Vor jedem Commit: `cmake --build build && ctest --test-dir build`.
- HealthLog ist PolyForm Noncommercial: lesen und verlinken ja, Code übernehmen nein. Die
  Pebble-App (coredevices/mobileapp) ist GPL-3.0 wie Kladde.
- Echte Gesundheitsdaten (Exporte der Probe) nie committen; Fixtures nur synthetisch oder
  gekürzt und ohne Rückschluss auf Personen.
- `app/qml/Kante` nur über `tools/sync-kante.sh`, nie von Hand.
- Temporäre Dateien und Build-Verzeichnisse unter `~/.cache/claude/kladde-*`.

## Gelernte Fehler
- HealthLogs Batch-Endpunkt spricht HealthKit: Werte in Apples Einheit (`count/min`, `m`,
  Schlaf als Minuten mit `sleepStage` nach `HKCategoryValueSleepAnalysis`). Tagessummen
  (`stats:<id>:<Tag>`) überschreiben: immer den ganzen Tag schicken, nie ein Delta.
- Ein `measurements:write`-Token darf keine `source` nennen (422) und erreicht `/api/auth/me`
  nicht (403); die Zeitzone kommt dann vom Gerät.
- Gitea-Runner (act) löst `gradle/actions@v4` nicht auf („unsupported object type“); die CI
  läuft ohnehin auf GitHub.
- Idempotency-Key nie aus dem Body ableiten: HealthLog beantwortet einen bekannten Key 24 h aus dem
  Speicher, eine Tagessumme, die auf einen früheren Wert zurückfällt, käme nie an. Key je Batch neu,
  beim erneuten Senden derselbe.
- Nur 422 ist eine endgültige Ablehnung. Andere Status (404, 308, 400 …) halten die Warteschlange an,
  sonst leert eine falsche URL sie.
- Ein Datensatz über Mitternacht gehört zu dem Tag, zu dem der Mapper ihn zählt (Schritte: Starttag,
  Herzfrequenz: die Tage der Blöcke von Start bis Ende, ein Block gehört zum Tag seines Beginns), nicht zu dem Tag, für den er gelesen wurde.
- Die Warteschlange geht streng der Reihe nach: Tagessummen und Herzfrequenz-Blöcke überschreiben auf
  dem Server, ein neuerer Batch darf einen älteren im Backoff nicht überholen. Ein ersetzter Batch
  behält seinen Platz.
- Eine 200 ist nicht immer ganz erledigt: Einträge mit `status: failed` bleiben in der Warteschlange.

## Release rule

Release steps, in this order (`X.Y.Z` the new version, `A.B.C` the previous one). A release is done only after step 9.

1. Version: `project(kladde VERSION X.Y.Z)` in `CMakeLists.txt`; the APK takes its version name and code (major·10000 + minor·100 + patch) from it.
2. Write the release log before the tag in `CHANGELOG.md`, the single source: a section `## [X.Y.Z] — YYYY-MM-DD` at the top (newest first) with `### English` and `### Deutsch`, each a lead (what the release is about), then the changes grouped as `#### New`, `Improved`, `Fixed` and `Note` (German: `Neu`, `Verbessert`, `Behoben`, `Hinweis`; breaking changes, steps the user must take), then the compare link (`https://github.com/shrippen/Kladde/compare/vA.B.C...vX.Y.Z`). Each point says what changed for the user and why, not which code moved. Check the headings with `scripts/release-notes.sh --check` (CI runs it on every push), read the notes with `scripts/release-notes.sh X.Y.Z`.
3. Landing page `docs/index.html` (Kante `.changelog`): the same text in English and German as a new entry at the top of `#changes`; from the fourth entry on, the oldest move into `details.changelog-more`. Bump the footer version (`vX.Y`, linked to `#changes`). Screenshots from the demo world, taken now for the tagged release, not with every change: `../shrippen.github.io/demo/tools/.venv/bin/python ../shrippen.github.io/demo/tools/screenshots.py kladde`, then `../shrippen.github.io/demo/tools/use-shots.py kladde`; for a visible change also `docs/shots/release-<version>-<name>-<lang>.webp`.
4. Open the PR on Gitea, review it as the Repository rule says (a release is published: the owner confirms it in words), merge on Gitea.
5. Tag the merge commit and push the tag to Gitea only: `git tag vX.Y.Z <merge commit> && git push origin vX.Y.Z` (the mirror carries it to GitHub).
6. Gitea release, notes only, never files: `scripts/release-notes.sh X.Y.Z > notes.md`, then `tea release create --repo shrippen/Kladde --tag vX.Y.Z --title "Kladde X.Y.Z" --note-file notes.md`.
7. GitHub: the tag runs `.github/workflows/release.yml`. It stops when the tag and the project version differ, creates the GitHub release as a draft with the same notes (stops without the section), builds the release APK with `scripts/build-android.sh release` (which stops when demo code or data is in the package), signs it, attaches `Kladde-X.Y.Z.apk` and then publishes the release. Check the run (`gh run list -R shrippen/Kladde --workflow release.yml`) and the asset (`gh release view vX.Y.Z -R shrippen/Kladde`).
8. The landing page lives on GitHub Pages of the mirror (`main`, `/docs`): it updates with the merge.
9. Done when https://shrippen.github.io/Kladde/ shows X.Y.Z at the top of `#changes` and in the footer, and the GitHub release has the APK.

Signing: the release key is the owner's, never in a repository. GitHub secrets `KLADDE_KEYSTORE_B64` (the keystore, base64), `KLADDE_KEYSTORE_PASSWORD`, `KLADDE_KEY_ALIAS`, `KLADDE_KEY_PASSWORD`. Without the key a release stops; a lost key means installed copies can no longer update. Test the release build by hand: `release.yml` via workflow_dispatch without `tag`.

`scripts/release-notes.sh` is a copy of Kante's `tools/release-notes.sh`: change it only there, then sync with `../Kante/tools/sync-release-notes.sh "$PWD"`.

## GUI rule

- Every GUI of this project is generated from Kante, not inspired by it: landing pages,
  web apps, Qt Quick / Kirigami apps, Plasma widgets, dialogs, e-mail and print layouts.
  Source: https://github.com/shrippen/Kante (checkout `../Kante`).
  Web: link `https://shrippen.github.io/v1/shrippen.css` and `shrippen.js`, or vendor them
  unchanged. Apps: copy `qml/Kante` (and `KantePlasma` for Plasma widgets) unchanged.
- Use Kante's tokens, roles, components, classes, QML components and motion as they are.
  No own colours, fonts, sizes, radii, cuts, shadows, animation timings, no own copy or
  variant of a component that Kante has. Raw values (`#hex`, `px` for controls) are a bug;
  use roles (`--primary`, `--focus`, `--warn`, `KanteStyle.*`).
- A missing element is added to Kante first (CSS or QML, docs, catalogue), then used here.
  Never solve it locally in this project and never wait with a "temporary" copy.
- Exception: Kimai plugins take their GUI from Knust (`kimai/knust/` in shrippen/Kante), the
  Kante spinoff that adapts Kante to Kimai's look. The same rule applies to Knust: use it
  as it is, and add missing elements to Knust.
- A project without a GUI (library, CLI, scripts) has nothing to do here.
- Rule text: https://github.com/shrippen/Kante/blob/main/AGENT-RULE.md

## Repository rule

- This repository lives on Gitea (`git.arianw.de`). GitHub is only a push mirror of it.
- CI runs on the GitHub mirror (`.github/workflows/ci.yml`, since 1.11.2026) and reports back to Gitea as the commit status `github / test`. Gitea runs no Actions here (no `.gitea/workflows/`, Actions off in the repository settings).
- Changes arrive as pull requests: work on a branch, open a PR, merge it on Gitea (the mirror follows).
- Never merge a PR, push to `main` (or any default branch), push tags or publish releases on GitHub. A merge there is overwritten by the next Gitea push.
- Never force-push a branch that someone else's PR depends on.
- Review (all shrippen repositories, owner 18.10. and 22.10.2026): every PR is reviewed before it is merged. The owner does not read code, so this is the second reader.
  - PR-Agent first (owner, 22.10.2026): where `.gitea/workflows/pr-agent.yml` is set up, the session comments `/review` on the PR for each round with new code or a new base (the first one included; a rebase, a merge of the base or a change of the base branch, see below) and waits for PR-Agent's comment. That comment reviews the whole diff and covers the commit that was the PR's head when `/review` was posted. PR-Agent labels its findings as `REVIEW.md` says; a finding without a label counts as **Wichtig**. A run cancelled by a newer `/review` on the same PR (the concurrency group holds the PR number) is replaced, not failed.
  - If PR-Agent fails (the run fails or ends without a review comment, reports a key or cost limit, or no run is created within 10 minutes), the session posts `/review` once more. If that fails too, a new Claude agent reviews the round as below, and the session says in the PR why. Where PR-Agent is not set up, the new Claude agent always reviews.
  - The Claude agent: the session that wrote the PR starts a new agent (Agent tool, no shared context) and gives it only the PR's title and body, the diff against its base (`git diff <base>...HEAD`), the repository and `REVIEW.md`. It does not tell it what it thinks of its own change. The agent checks every finding it would post against the code (file, line, the input that goes wrong) and drops what it cannot show. It reports as `REVIEW.md` says: count first, labels **Wichtig**, **Kleinigkeit**, **Bestand**. The session posts the report unchanged as a PR comment on Gitea. Above it goes the heading `## Claude-Review (neuer Agent) zu <commit>`, below it the footer `---` and `_Generated by [Claude Code](https://claude.ai/code)_`.
  - Each **Wichtig** is fixed, or answered in the PR with a reason.
  - PR-Agent sees no earlier round. So when an earlier round had a **Wichtig** (open or closed), after PR-Agent's comment (or with answers only, alone) a new Claude agent closes the round: it gets the PR's title and body, the repository, `REVIEW.md`, the open **Wichtig** with the answers, PR-Agent's findings of this round, the closed **Wichtig** and how they were closed, and the new commits (`git diff <last reviewed commit>..HEAD`; without new commits, the code at HEAD). It says for each open **Wichtig** whether it is fixed or its answer is accepted, and whether a **Wichtig** PR-Agent raises again is one already closed (fixed, answer accepted or decided by the owner; it stays closed unless the agent shows it is not fixed). Its count holds every **Wichtig** still open, PR-Agent's new ones included. It posts like a review, heading `## Claude-Review (offene Wichtig) zu <commit>`. An answer it does not accept goes to the owner, who decides in words. With the fallback, one new agent does both: the review of the new commits and the open **Wichtig**.
  - After a rebase, a merge of the base or a change of the PR's base branch a new round is due, also without a new commit, against the new base; a Claude agent of that round (closing or fallback) gets the whole diff against the new base (`git diff <base>...HEAD`) in place of the new commits. Every round counts, also one with answers only: after three rounds that still leave a **Wichtig** open, stop and rethink the approach with the owner.
  - Merge only when the last round covers the PR's latest commit against its current base branch (the branch by name; the base moving on alone needs no new round) and leaves no **Wichtig** open (its closing agent's count, or PR-Agent's comment when no earlier round had a **Wichtig**), and CI is green on the PR's latest commit: every CI check that runs on each push (such as `github / test` from the GitHub mirror) shows success there. No status there yet means wait, not merge; if it never comes, check the mirror or ask the owner. PR-Agent's status does not count here. Where CI runs only for some paths, the repository rule says which commits need it. Without CI, the local checks count (here the build and `ctest`).
  - Before the merge, the owner confirms in words, not by reading code, a PR that changes CI or release workflows (`.gitea/`, `.github/`, `packaging/`), this review rule (`agent.md`, `REVIEW.md`), secrets or what is published (a release, a tag, a new public page, site or package; rebuilding what is already published does not count).
  - `REVIEW.md` in the repository root says what failure looks like here, what to check in particular, what to skip and how to report. A repository without one gets it in its next PR.
- PR-Agent's status is not a CI check. The owner may post `/review` at any time (PR-Agent takes no other command here); the merge waits for that run and handles its findings like a round's. Owner and session post from the same account, so a run that fails is handled the same whoever posted `/review`: as above, once more, then the fallback.
