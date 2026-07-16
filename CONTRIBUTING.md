# Contributing to lantern

Thanks for wanting to help. The engine is deliberately small — read
[docs/ENGINE.md](docs/ENGINE.md) first; most of what you need to know fits
there.

## Ground rules

- **The spec is fixed.** 400×240, software rasterizer, the PICA200-style
  lighting model. PRs that add GPU backends, alternate resolutions, or
  general-purpose-engine features will be declined — the constraints are the
  product.
- **Keep the API small.** New `lt_*` calls need a game that demonstrably
  can't be built without them.
- **No new dependencies.** SDL2 (OS plumbing) and Lua (game scripting) are
  the entire dependency list, and it stays that way.
- **Determinism is a feature.** The renderer must produce identical output
  across machines — that's what makes screenshot testing possible. Anything
  that breaks bit-exactness (thread races in the raster path, platform math
  variance) is a bug.

## Workflow

1. Build: `cmake -B build && cmake --build build -j`
2. Run a game: `./build/lantern games/demo`
3. Verify before submitting: `LANTERN_SHOT=/tmp/shot ./build/lantern
   games/demo` renders 60 frames headlessly and writes the framebuffer as
   BMP — eyeball it, and check `games/stress` still holds 60 fps.
4. Fixed a bug? Append an entry to [docs/BUG_JOURNAL.md](docs/BUG_JOURNAL.md)
   **in the same commit** — 5 lines max: symptom, cause, fix, lesson.
5. Changed wick? Update [docs/WICK.md](docs/WICK.md), [CHANGELOG.md](CHANGELOG.md),
   and the public site at `../wick-site/` (reference page + a blog post when
   the change is a design decision or release). See wick-site README.

## License

By contributing you agree your contributions are licensed under the
repository's [zlib license](LICENSE).
