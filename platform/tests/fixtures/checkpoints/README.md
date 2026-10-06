# Reviewed played checkpoints

These full JSON flash archives are an explicit exception to the project's
source-only rule, approved for regression fixtures. They contain the same save
data as the user's naturally played `.sav` checkpoints, not compiled game
executables. Encoding as JSON does not remove or anonymize save data.

`manifest.json` records hashes, expected locations/party/money/save counters and
the pinned upstream version. Collection build and RTC settings were not recorded;
they are marked unknown rather than inferred. Decoded reports were reviewed with
the game loader. Checkpoints cover the truck, clock, rival, starter rescue and
Oldale Town, including outside the Pokémon Center and inside beside its PC. Do not
hand-edit story flags or pretend these fixtures were generated from code.

Run `ctest --test-dir build -R checkpoint --output-on-failure`. CTest runs
`checkpoint-restore` automatically before tests requiring `played-checkpoints`.
The setup validates archives and their manifest and produces
`build/checkpoint-saves/<checkpoint-id>/pkmemerald.sav`. Each setup recreates those
disposable files, so previous runs cannot change the baseline. The committed JSON
archives are only read, never written. Game-load checks run on supported targets;
native builds also compare every reported field against the reviewed projection.

Future tests that mutate a save should copy the reconstructed baseline into a
private test directory first; tests may run concurrently. Use a fixed RTC value
for gameplay replay tests. This fixture setup checks loadability and decoded
state, not full playthrough behavior or deterministic frames.

Changing a fixture requires another normally played save, a matching inspector,
review of state differences, and an updated archive/manifest hash. Upstream save
layout changes may require collecting new checkpoints. Saves reconstructed during
CI are build outputs, never CI artifacts or cache contents; full JSON archives
are tracked fixture inputs. Local collection files and exports in `test-saves/`
remain ignored.
