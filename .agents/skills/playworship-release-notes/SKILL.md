---
name: playworship-release-notes
description: Audit and prepare PlayWorship release notes before an app upload or publication, with a landing summary, full help detail, and independently verified availability. Use for release preparation or corrections, not ordinary feature development or backend deployment.
---

# PlayWorship Release Notes

Use one reusable skill for every upload; create one editorial document per version,
not a new skill per release. Preparing notes does not authorize push, deploy,
workflow dispatch, store submission, or credential changes.

## Locate the sources

Verify remotes and worktree state before editing:

- App: `/Users/luisberna/dev/multitracks/multitracks/juce-controls-plugin`,
  `luisberna0324/adoracion-studio`.
- Landing: `/Users/luisberna/dev/landing-playworship`,
  `luisberna0324/landing-playworship`.
- Help: `/Users/luisberna/dev/playworship-help`,
  `luisberna0324/playworship-help`.

Do not substitute `/Users/luisberna/landing` or an old Downloads checkout.
Read app `AGENTS.md` and `docs/okf/release/ReleaseNotes.md` for the maintained
schema and commands. Read [the audit checklist](references/audit-checklist.md)
when auditing a new version or deciding what a feature claim means.

## Prepare a release

1. Pin the target SHA and identify the actual previous release tree for each
   channel. Apple may have an older baseline than desktop/Android. Compare final
   trees and inspect relevant code/tests/OKF contracts; do not use commit titles
   or generated PR notes as the complete changelog. Exclude reverted features.
2. Update only `release-notes/<version>.json` for editorial content: neutral
   Spanish, a short summary, up to three landing highlights, full grouped changes,
   and accurate platform scope. Separate newly introduced changes from inherited
   functionality. Keep internal QA logs, credentials and customer data out.
3. Update only `release-status/<version>.json` for availability, build numbers
   and its verified date. Confirm Actions/artifacts/GCS and Apple/Play states
   independently. Build/upload/processing/review/publication are different facts.
   Never infer approval or iOS availability from a successful desktop workflow.
4. Run the renderer/validators and applicable local tests from the app contract.
   Generate help snapshots with the sync script; never hand-edit generated JSON
   or add the new release to the legacy help array. The landing currently links
   from each platform download card to the help history, without fetching or
   repeating release content. Keep those links intact; do not reintroduce a
   standalone summary unless requested. The optional latest summary file remains
   generated for future consumers.
5. Report exact files, target SHA, tested platforms, unverified states and pending
   external steps. If publication was not requested, stop with local changes.

## Publish only within the requested scope

When an app release is authorized, use the existing manually dispatched `ci.yml`
with its source/version and explicit publication flags. The pipeline must validate
notes from the selected SHA before building and must generate GitHub Markdown,
JSON assets, the lightweight catalog and individual detail files from that input.
Do not move an existing tag to incorporate later notes.

When only a store state changed, use the documented `release_status_only` mode
for an existing release after authorization. It must not rebuild installers,
rewrite editorial notes, alter download/payment settings, or submit a store build.

Verify the resulting release assets, public JSON and intended site separately.
If a publication attempt fails, inspect the failed stage before retrying; do not
redispatch a full native matrix to repair a metadata-only problem. Stop for missing
source evidence, authorization, signing, or account access instead of guessing.
