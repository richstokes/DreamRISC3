# Vendored mkdcdisc

Upstream: https://gitlab.com/simulant/mkdcdisc
Revision: a663882c4ac0e2123c6229af6dd596cee7a66dc1 (0.0.4, MIT, see LICENSE and THIRD-PARTY-NOTICES.md)

Local changes from upstream:

- `tests/` (13MB of audio and ELF fixtures), `.gitlab-ci.yml`, `.gitlab-ci/` and `.semgrepignore` are not included
- `meson.build` has `subdir('tests')` commented out to match

To bump: `git archive <rev>` from upstream over this directory with the same excludes, reapply the meson.build line, update the revision here.

Built by `make mkdcdisc` into `build/mkdcdisc/mkdcdisc`. Needs meson, ninja, pkg-config and libisofs (`brew install meson ninja pkg-config libisofs`).
