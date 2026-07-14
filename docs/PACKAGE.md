# .lant — the lantern game package

One file, whole game. A `.lant` is lantern's own archive format — no zip,
no tar, ~150 lines we fully control (`src/package.cpp`), CRC-checked, and
safe to run by construction. It is the delivery format of the lantern
store.

## Use it

```sh
# package a game folder
./build/lantern_pack games/mygame mygame.lant

# play a package (extracts to a temp dir, then runs normally)
./build/lantern mygame.lant
```

## Store packages are wick-only. Here is why.

A wick game is pure data. The wick VM's only exits are the typed `lt.*`
natives — draw, sound, input, and per-game save slots. There is no file
API, no network, no shell, no native code. A malicious `.lant` therefore
has nothing to reach: the WORST a store game can do is render rude
pixels. That guarantee is what makes a curated store cheap to review and
safe to use, and it cannot be offered for Lua games (Lua ships `os` and
`io` — fine for local development, not for distributing strangers' code).
`lantern_pack` refuses folders without `main.wick`.

## game.info

The manifest, plain `key=value` lines, first entry in every package:

```
id=com.aulenor.lantern-night  # reverse-DNS, unique in the store
title=Lantern Night
version=1.0.0                 # semver
author=the lantern engine authors
entry=main.wick               # always main.wick for store packages
engine=0.7                    # minimum engine version
```

If the folder has no `game.info`, `lantern_pack` synthesizes a local one
and warns — fine for testing, not accepted by the store.

## Format (all integers little-endian)

```
8    magic "LANTPKG1"
u32  file count                       (1..1024)
per file:
  u16  name length, name bytes        (utf-8, FLAT — no '/', no '..',
                                       no leading '.', max 255 bytes)
  u32  data length, data bytes        (max 64 MB/file, 256 MB/package)
u32  CRC-32 (IEEE) of every byte before it
```

Packages are flat because game folders are flat. The reader validates
names, sizes, and the checksum before a single byte is extracted;
`tests/package_test.sh` proves a packed game renders byte-identical to
its folder and that a corrupted package is refused.
