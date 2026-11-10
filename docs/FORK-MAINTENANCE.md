# Personal fork workflow

- Personal fork: https://github.com/qiyuey/SavageWindRecomp
- Development and default branch: `test`, tracking `origin/test`.
- `origin`: personal fork; default push destination.
- `upstream`: original project; integration source `upstream/test`.

The 2026-11-10 cleanup consolidated the fork changes into one commit above upstream `5618291`.
Keep future changes in focused commits by default. When another squash is explicitly requested,
save the prior tip and any pending work locally, review the diff against freshly fetched upstream,
and push to `origin/test` with an explicit force-with-lease against the fetched fork tip.
For ordinary updates, merge upstream into `test`. Push only when publication is requested.

## Build dependencies

The Windows and Linux builds use the custom SDL3 revision pinned in `tools/build_ci_sdl.sh`.
It needs libusb for Switch 2 Pro USB frequency output. Record both repository revisions and
build options when publishing a binary. The macOS build uses SDL's conventional two-motor API.

SW Update still points to the original project's release feed. Installing an update from that
feed replaces the fork build with the upstream release.

Keep game dumps, firmware, keys, saves, generated game code, extracted resources, derived catalogs, local logs and build artifacts outside version control.

The fork retains the versions of the named-place metadata tracked by `upstream/test` when merging upstream:

- `runtime/control_places_data.inc`
- `tools/places_startpos.json`
- `tools/places_measured.json`

Keep local extracts and locally regenerated copies of these tables outside version control.
