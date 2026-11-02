# src/modern/data/ -- the modern build's game-data edits

Some changes in the modern build (`make modern`, `NOLFSRC_MODERN`; see
`BUILDING.md`, "Variant builds") need game data changed as well as code. The
game data is not in this repository -- it comes from the retail game, which
the `rez/` submodule installs -- so a change is committed here as a recipe,
and `tools/moddata.py` applies it when the modern build is made. The results go into the game directory's `custom/` rez
directory, which the engine searches after the four `.rez` archives, so they
override the retail copies. The release build never sees any of it.

| Recipe | What it does |
|---|---|
| `<path>.patch` | a unified diff against the retail file at `<path>` (as the game loads it: the last `.rez` archive that holds it), e.g. `ATTRIBUTES/WEAPONS.txt.patch` |
| `files/<path>` | a new file, written for this project, installed at `<path>` |
| `<path>.from` | one line, `<archive> <path in it>`: that retail file, from that archive, installed at `<path>` -- for a retail file the Game of the Year archives override, wanted under another name (e.g. `INTERFACE/MAINMENU1B.dtx.from` containing `nolf2.rez INTERFACE/MAINMENU1.dtx`) |
| `<path>.bytes` | byte edits of a binary retail file, one per line: `<offset> <old hex> <new hex>` (offset in hex or decimal, negative to count from the end; `#` starts a comment); the old bytes must match, e.g. `INTERFACE/FONTS/HUD_CHOOSER.pcx.bytes` turns one palette entry black |
| `<path>.objects.json` | edits to version-66 world objects, matched by exact class/name: replace a string property after checking its old value, or remove an unused object; record sizes and the following render section offset are rebuilt |
| `<path>.flop` | the retail texture at `<path>` (a `.dtx`, DXT1 or 32-bit) mirrored left to right, exactly (the blocks reordered, nothing re-encoded); the file's text is a comment, e.g. `TEX/OBJECTS/OBJ14/OBJ0689.dtx.flop` (M-P4-13) |
| `gamedir/...` | the same kinds for a file the engine reads from the game directory itself rather than from the archives (`gamedir/defctrls.cfg.patch`); the retail original is kept beside it as `<file>.retail` and put back if the recipe goes |

The sound corrections remove optional weapon events with no retail recording,
trim the Germanic backup list to its nine recorded lines, reuse the available
wood vehicle sound and generic melee impacts, and provide the numbered door
and jungle ambience recordings under their misspelled world references. These
are data recipes so both native and modern builds use the same corrections.

`TEXTURES/Chrome.dtx.from` supplies the retail chrome map under the path used
by Hamburg's world properties, so Inge's reflective costume can render.
`SFX/BEAMS/SKINS/Spotlight_beam.dtx.from` supplies the shipped sphere-effect
skin under the mine detector's missing texture path, restoring its pulsing
mine-radius highlights. The similarly named `SpotlightBeam.dtx` is a dim
spotlight-cone texture and does not provide visible mine markers.
The club's stage trigger uses her existing `Idle` animation. Animal dialogue
placeholders named `x` are removed from the server sound lists.

The dock recipes repair a trailing space in the locked-lever sound and the
water-lapping filenames. They remove an obsolete door with no compiled world
model, an editor sprite whose source never shipped, and the unfinished final
line of `conv4` (neither `1142` nor the apparent intended `11042` was recorded).
The freighter recipe corrects two rotating fans' `BusySound` paths and removes
an obsolete door portal name that has no compiled portal. Sound playback and
preloading also normalize the old fan separator typo and trailing whitespace
so those corrections work with existing saves.

To make a patch, edit a copy of the file and let the tool write the diff
(after a `make modern`, so `out/nolf-modern-release/` exists):

```sh
python3 tools/moddata.py show out/nolf-modern-release ATTRIBUTES/WEAPONS.txt > /tmp/weapons.txt
# ... edit /tmp/weapons.txt ...
python3 tools/moddata.py diff out/nolf-modern-release ATTRIBUTES/WEAPONS.txt /tmp/weapons.txt \
    > src/modern/data/ATTRIBUTES/WEAPONS.txt.patch
```

Keep patches small -- the changed lines and the three lines of context the
tool gives them. A patch has to apply exactly, so a retail file that does not
match what it expects fails the build rather than producing something half
edited. Never commit a whole retail file, and only put files here that were
written for this project; `MODERN.md` says which change each recipe belongs
to.
