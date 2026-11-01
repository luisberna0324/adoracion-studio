# Evidence and editorial checklist

## Compare the shipped trees

Resolve the final annotated tag to a commit, confirm the intended SHA, and read
`git diff <baseline>..<target>`. Group effective changes by product behavior,
not the size of a PR. Check changed feature flags and each platform's compile
conditions. Tests and documentation from an older SHA are historical evidence,
not a fresh run. Inspect their assertions to understand the contract.

For Apple, locate the previous archive's source SHA and feature flags if available.
A version-number-only delta on desktop may still deliver new functions relative
to an older iOS/Mac Store build. State which comparison a claim uses.

## Write for the two audiences

- Landing: a discreet help-history link in each platform download card, no
  separate release block. The generated summary/highlights remain available for
  future consumers; never call inherited functions exclusive to the latest release.
- Help: all grouped changes, platform limits, meaningful behavior and fixes.
  Keep implementation logs and provider identifiers in private evidence, not copy.
- Availability: a separate dated status document; store approval is mutable after
  the editorial release and must not require moving a tag or rebuilding binaries.

Recent examples to verify rather than generalize: accepting decimal BPM means
editable input, display and save/reopen preservation, not merely a double-valued
engine. Fader linking may equalize absolute volume without linking Gain/Mute/Solo.
A native mobile lyrics editor is not the desktop FreeShow cue map. A feature in
source is not proof it was enabled in a signed store archive.

## Validation and handoff

Use the current repository renderer and consumer sync commands from
`docs/okf/release/ReleaseNotes.md`; do not maintain another schema here.
Confirm that the index contains summaries only, detail is fetched on demand,
status is version-matched, malformed responses retain a dated fallback, and the
release view cannot change billing or download controls. Keep catalog size bounded.

Distinguish local build/test, real device QA, Actions completion, installer access,
store upload, processing, review and public release. Preserve unrelated local work.
A skill is guidance, not an automatic CI hook: workflow validation enforces the
required documents. Revalidate that guard when changing the release process.
