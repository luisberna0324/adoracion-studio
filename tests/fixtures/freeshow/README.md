# FreeShow real validation fixture

`independent-marks.show` contains original synthetic Spanish text for technical
validation. Import it into an isolated FreeShow 1.6.6 profile with show ID
`pw-synthetic-validation` (FreeShow import may assign another ID; use that ID).
Enable the REST API locally on port 5506. Do not expose API ports publicly.

Principal: Intro, Coro (two screens), literal Coro2, Coro repeated (two screens),
Final. Alternativo rearranges the same stable group and slide IDs.

Build the desktop tests using the normal project configuration, then run:

```sh
MTControlsFreeShowProtocolTests --real pw-synthetic-validation 5506
```

The test uses the real REST client to fetch the catalogue, select each absolute
destination in both arrangements and confirm `get_output`, then clear lyrics.
It changes the isolated presenter's output. Unit tests use an injectable
transport and require no running FreeShow. Processor tests render their own
small audio fixture; their optional `--real` mode also exercises the real
presenter. GUI tests require an OS display (Xvfb works on Linux).

Use the app's explicit **Probar en FreeShow** action for manual projection;
selection and dragging alone must leave the presenter's output unchanged.
