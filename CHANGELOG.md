# Changelog — lantern & wick

All notable changes to the engine and its language. Format inspired by
[Keep a Changelog](https://keepachangelog.com/); versioning is informal
while the stack is pre-1.0 (`engine=0.7` in `.lant` manifests).

## [0.7] — 2026-07-14

### wick language

- **`record` types** (flat, no methods): named fields of `num` / `bool` /
  `str`, construction with named fields, field get/set, `list<record>`.
  Admitted because KORA's kitchen props needed five parallel lists.
- Feature ledger updated: records **admitted**; string indexing still
  deferred (world baking remains the clean workaround).
- CI language suite: 12 checks (semantics + must-fail-to-compile +
  showcase_wick self-play), including record construct/get/set and
  must-fail cases for unknown fields / bad field types.

### Security & packaging

- **Path sandbox** for every game-relative load (`load_texture`,
  `load_mesh`, `load_sound`) in both wick and Lua hosts: refuses `..`,
  absolute paths, and hidden segments. Shared helper in
  `src/path_sandbox.cpp`.
- **Package-mode screenshots** confined to the extract directory
  (basename only) — store games cannot write arbitrary host paths.
- **Nested `.lant` names**: packages may contain `assets/tileset.bmp`
  (still no `..`). `lantern_pack` walks recursively; extract creates
  parent directories. Flat games (Lantern Night) pack unchanged.
- Durable tests: `tests/path_sandbox_test.sh`, existing
  `tests/package_test.sh` (folder↔package byte-identical frames).

### KORA (flagship wick game)

- Kitchen (scene 3) enterable; props use `list<Prop>` records.
- `KORA_SCENE=N` forces scene **and** skips title/prologue (CI path).
- `game.info` for store packaging; kitchen folder frame byte-identical
  to kitchen-from-`.lant`.

### Docs

- `docs/WICK.md`, `docs/PACKAGE.md`, bug journals updated in the same
  change set. Public site: [wick.famemu.aulenor.com](https://wick.famemu.aulenor.com/)
  and the [wick blog](https://wick.famemu.aulenor.com/docs/blog/).

## [0.6] and earlier

See git history for the initial software rasterizer, iOS host, Lua + wick
hosts, Lantern Night dual build, and first `.lant` packer.
