# DVD Region Tools

PS3 homebrew (PSL1GHT / NPDRM) that puts DVD region handling and DVD dumping in
one place:

1. **Region settings** — read and rewrite the DVD / Blu-ray / PS3 region stored
   in `/dev_flash2/etc/xRegistry.sys`.
2. **Disc tools** — dump the DVD in the console's own drive to
   `/dev_hdd0/DVDISO`, either as a 1:1 ISO image or as a `VIDEO_TS` folder copy.
3. **Region patch** — optionally clear the prohibited-region byte in
   `VIDEO_TS.IFO` so the dump plays on any player.

---

## What it will not do

These are hardware and disc-protocol limits, not missing features:

* **RPC-2 / RCE discs cannot be freed.** A disc carrying RPC-2 reads a player's
  region out of its PGC bytecode while it plays. Zeroing the mask in
  `VIDEO_TS.IFO` does not touch that code, so those discs still refuse to play
  outside their region. The app detects the `0xFF` mask case and says so.
* **This does not reprogram the drive.** Writing the registry does not change the
  Blu-ray drive's EEPROM or the console target ID. On some consoles the DVD code
  is ignored no matter what the registry says. That is a well-known limitation
  shared with webMAN.
* **A registry change only takes effect after a restart.** GameOS caches the
  settings; the app tells you when to power-cycle.

## Requirements per feature

| Feature | Stock OFW | CFW / HEN / HAN |
| --- | --- | --- |
| Read/backup/restore `xRegistry.sys` | no | yes |
| Change the region | no | yes |
| 1:1 ISO dump (raw `sys_storage_*`) | no | yes |
| `VIDEO_TS` folder copy | yes | yes |
| Region patch on the copy | yes | yes |

`sys_storage_open` (syscall 600) and `sys_storage_get_device_info` (609) are
root-only on stock firmware. There is no homebrew way around that without a
modified hypervisor, which is why the folder copy exists as an equal-footing
alternative.

## Building

Requirements: [ps3dev/ps3toolchain](https://github.com/ps3dev/ps3toolchain),
[ps3dev/PSL1GHT](https://github.com/ps3dev/PSL1GHT), and
[ps3dev/ps3libraries](https://github.com/ps3dev/ps3libraries) for SDL.

> Use **ps3dev/PSL1GHT**. The HACKERCHANNEL fork has a different header layout
> (`include/` instead of `ppu/include/`, `rules/ppu.mk` instead of `ppu_rules`,
> `sys/sysfs.h` instead of `lv2/sysfs.h`) and will not build this project.

```sh
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
make            # -> build/DVDREGION.self
make pkg        # -> DVDREGION.pkg  (needs your own category + console ID to sign)
make clean
```

`make run` works too when a `ps3load` listener is running on the console.

## Host compile check (no toolchain needed)

`ps3toolchain` only builds on Linux or macOS, so on a Windows or bare machine
the sources can still be type-checked against stub declarations of the PSL1GHT
and SDL APIs:

```powershell
tools\check\check.bat          # MSVC
```

```sh
tools/check/check.sh           # gcc or clang
```

This type-checks all nine translation units at `/W4` plus MSVC's `/analyze`.
It does **not** generate PowerPC code and proves nothing about hardware
behaviour — it only catches signature mismatches, missing includes, buffer
overflows and dead code. It currently passes with no diagnostics.

The stubs live in `tools/check/stubs/` and mirror the real PSL1GHT prototypes;
keep them in sync if you change a signature.

## Building with GitHub Actions

`.github/workflows/build.yml` builds both, with no local toolchain:

* `hostcheck` — plain `ubuntu-latest` + gcc, runs the stub type-check above.
  Seconds, and it gates the real build.
* `ps3` — runs inside [`hldtux/ps3dev:latest`](https://hub.docker.com/r/hldtux/ps3dev),
  which already contains ps3toolchain, PSL1GHT (`$PSL1GHT=/usr/local/ps3dev`)
  and SDL 1.3 in `$PS3DEV/portlibs/ppu`.

Push to `main` or `master`, open a PR, or hit **Run workflow**. The artifact is
`DVDREGION`, containing:

| File | What it is |
| --- | --- |
| `build/DVDREGION.elf` | stripped PowerPC ELF, for inspection |
| `DVDREGION.self` | NPDRM self |
| `DVDREGION.gnpdrm.pkg` | NPDRM pkg |
| `DVDRGN01000/` | `EBOOT.BIN` + `PARAM.SFO` + `ICON0.PNG`, sideloadable |

Two ways to install:

* **NPDRM pkg** on CFW/HEN. It is *not* signed with a console ID, so a debug or
  dummy PSN account is enough. No `.dec`, no retail signing.
* **Sideload the folder** on any CFW/HEN/HAN console: copy `DVDRGN01000` to
  `/dev_hdd0/game/DVDRGN01000/` and it appears in the XMB.

The workflow writes both sets of instructions into the run summary and fails
loudly with a specific message if the image turns out not to have SDL 1.3.

`make pkg` is marked `continue-on-error` deliberately: the `.self` is the
artifact that matters most and it must not be lost to a packaging hiccup.

### Build gotchas, all four hit during the first real build

These are not obvious from the PSL1GHT sources and each one produces a
misleading error message. They are commented at the relevant lines in the
`Makefile`.

1. **`OFILES` must be set before `include $(PSL1GHT)/ppu_rules`.**
   `base_rules` declares the link rule as `%.elf: $(OFILES)`. Make expands a
   rule's prerequisite list when it *reads the rule*, not when it runs the
   recipe, so an `OFILES` assigned after the include leaves the rule with no
   prerequisites at all. Make then compiles nothing and links nothing:
   `undefined reference to 'main'`.

2. **`LD` must be assigned explicitly.** `base_rules` uses
   `export CC := $(PREFIX)gcc` for the compiler but `export LD ?= $(PREFIX)gcc`
   for the linker. GNU make already has a built-in `LD`, `?=` never overrides an
   existing value, so `LD` stays the host `ld` — which rejects *every*
   PowerPC archive with a wall of `skipping incompatible ... libm.a`. The
   misleading part is that the same message appears for newlib's own `libm.a`.
   PSL1GHT's own samples work around this with `export LD := $(CC)`.

3. **`LIBPATHS` is never assigned by PSL1GHT.** The link recipe is
   `$(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS)`, so without it every PSL1GHT
   library reports as missing.

4. **There is no `libfs` and no `libpad` in the image.** The `sysFs*`
   filesystem API is in `libsysfs.a`; the `ioPad*` entry points are in
   `libio.a`, which is what PSL1GHT's `samples/input/padtest` links. Both were
   confirmed by grepping every shipped archive for the symbol names rather than
   trusting library names.

Also note SDL installs as `portlibs/ppu/include/SDL/SDL.h`, so
`-I$(PORTLIBS)/include/SDL` is needed for `#include <SDL.h>`.

## Using it

| Button | Action |
| --- | --- |
| `L1` / `R1` | move between the five pages |
| `UP` / `DOWN` | move inside a list (auto-repeats when held) |
| `LEFT` / `RIGHT`, `X` | change the highlighted value |
| `START` | apply the region settings |
| `TRIANGLE` | rescan the disc / take a registry backup |
| `O` | back, or quit from the home page |

The **Registry backup** page also carries a *Restart the console* entry, which
is what you need after applying a region. It only works on a modified
hypervisor (CFW/HEN/HAN); if the app is still running afterwards, the request
was refused and you need to power-cycle by hand.

### Region page

* **Preset tab** — the 14 region presets from the `xai_plugin` region table.
  Each one writes a consistent triple (PS3 country code, DVD region, Blu-ray
  region), which is what you almost always want.
  Blu-ray region **C is `0x04`**, not `0x03`; that trips people up constantly.
* **Manual tab** — set the DVD region, the Blu-ray region and the TV system
  independently.

`START` copies the untouched registry to
`/dev_hdd0/DVDRGN01000/xRegistry.sys.bak` first, then writes both the primary
and the mirrored flash copy.

### Disc page

Pick a mode, a destination (`/dev_hdd0` plus any mounted `/dev_usbNNN`) and
whether to clear the region mask, then press `X` on *Start*.

* **1:1 ISO image** — `sys_storage_read` in 64 KB chunks, with close/open
  recovery when the drive drops out. The disc size comes from ATAPI `READ TOC`,
  falling back to device info and finally to a read probe.
  Written as a single file on the internal HDD, split at `0xFFFF0000` on FAT32.
* **VIDEO_TS folder** — a file-tree copy out of `/dev_bdvd`. Works on stock
  firmware, needs no raw drive access, and is what webMAN/MultiMAN do
  themselves.

The region mask byte is at offset `0x22` of `VIDEO_TS.IFO` (the `VMGM_CAT`
prohibited-region field). It is a bit-per-region mask where a set bit means
*prohibited*, so `0x00` is region free. In the 1:1 path the app notices the
`DVDVIDEO-VMG` signature while streaming and patches the byte in place at the
end, so there is no second pass over 4.7 GB.

## Playing the result

ISOs land in `/dev_hdd0/DVDISO`, which Showtime, MultiMAN, IRISMAN and webMAN
all index automatically. A copied `VIDEO_TS` folder can be mounted as a DVD
folder from a file manager.

## Recovering from a bad change

Open the **Registry backup** page:

* *Create backup now* — only writes the file if it is not there yet, so the
  pristine copy is never overwritten by accident.
* *Restore from backup* — validates the `BC AD AD BC` header first, then
  writes it back over both flash copies.

If the console is too unhappy to boot the app, copy the saved file to
`/dev_flash2/etc/xRegistry.sys` over FTP.

## How the registry file is parsed

`xRegistry.sys` is 256 KB in two 64 KB halves (`0x10000` bytes each, only the
first two are populated):

```
0x00000  header, 16 bytes: BC AD AD BC | 00 00 00 90 | 00 00 00 02 | BC AD AD BC
0x00010  key table    u16 id, u16 len, u8 type, char key[len], u8 0x00
0x0FFF0  fixed block  4D26 007A 4D26 0062 ... (not part of the entries)
0x10000  value table  u16 flags, u16 keyref, u16 id, u16 len, u8 type,
                       u8 value[len], u8 0x00
```

`keyref` is the absolute offset of the owning key entry minus `0x10`. The key
table terminates on `AABB CCDD EE` and the value table on `AABB CCDD EE00`.

Both markers also occur *inside* the tables as break points, so
`source/xreg.c` walks the tables structurally and then falls back to a
byte-exact rescan that locates the key string directly and derives the entry
offset from it. Two keys that are never written are left alone entirely, and
string-typed entries are never rewritten in place — a corrupted registry is
much worse than a missing feature.

Relevant keys:

| Key | Value |
| --- | --- |
| `/setting/bddvd/dvdRegionCode` | `1`..`6` (0 = default) |
| `/setting/bddvd/bdRegionCode` | `1` = A, `2` = B, `4` = C (0 = default) |
| `/setting/bddvd/dvdTvSystem` | `0` NTSC, `1` PAL, `2` PAL 60Hz, `3` NTSC-J |
| `/setting/system/region` | `0x00` default, `0x83`..`0x8F` country code |

The `dvdTvSystem` sense is inconsistent between the PS3 wiki and `xai_plugin`,
so the Manual tab shows the raw values rather than guessing, and the Preset tab
never touches it.

## Source layout

| File | Contents |
| --- | --- |
| `source/main.c` | screens, navigation, dialogs, progress UI |
| `source/xreg.c` | xRegistry.sys parser and writer, region tables |
| `source/disc.c` | disc detection, `VIDEO_TS.IFO` parsing, size detection |
| `source/storage.c` | LV2 `sys_storage_*` and ATAPI `READ TOC` |
| `source/rip.c` | 1:1 sector dump, folder copy, region patch |
| `source/gfx.c` | SDL surface, 5x7 bitmap font, widgets |
| `source/input.c` | libpad wrapper with press edges and auto-repeat |
| `source/util.c` | endian helpers, strings, filesystem helpers, formatting |
| `tools/check/` | stub headers plus host compile check scripts |

## Credits

Region tables and the xRegistry layout follow the work of Mysis, stoker25 and
the PS3 developer wiki, plus `nikolaevich23/xai_plugin_hen` and
`aldostools/webMAN-MOD`, both GPL-3.0. DVD sector geometry follows ECMA TR-71
(2048-byte logical blocks, UDF bridge with ISO9660 compatibility).