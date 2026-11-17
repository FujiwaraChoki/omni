# SteelSeries Apex Pro TKL Gen 3 (wired, USB 1038:1642): HID protocol spec

Compiled 2026-11-17 by reading source only. No hidraw device was opened and no binary was run.

## Evidence legend and path abbreviations

`ORGB/` = `.../scratchpad/src/openrgb/Controllers/SteelSeriesController/`
`SSGG/` = `.../scratchpad/src/ssgg/` (code under `src/`, notes under `docs/development/` and `.claude/rules/`)

Every claim carries one of these tags:

| Tag | Meaning |
|-----|---------|
| **[1642-ORGB]** | OpenRGB has code for `0x1642` specifically. Neither repo has a hardware test record for `0x1642`. |
| **[1642-SSGG]** | ssgg has code or a capability row for `0x1642` specifically. ssgg marks every such row untested. |
| **[HW-1628]** | ssgg confirmed it on real hardware, but on the Apex Pro TKL **2023** (`0x1628`, fw 1.19.7, Windows) only. |
| **[SPEC]** | Taken from GG's decrypted device spec as quoted in ssgg docs/code. No hardware test. |
| **[STATIC]** | ssgg's Ghidra reading of the `0x1628` 1.19.7 firmware. For `0x1642` 1.19.7, ssgg compared the dispatch tables only, and they match exactly (`SSGG/docs/development/firmware-notes.md:297-307`). Handler bodies were not compared. |
| **[INFERRED-1642]** | My inference for `0x1642`. Neither repo codes or states it for this PID. |

**No byte sequence in this document has been confirmed on a `0x1642` in either repo.** The best evidence for this
board comes from three places:
- `0x1628`, which runs the same base spec (`apex_pro_tkl_2024` includes `apex_pro_tkl_2022`, `SSGG/.claude/rules/experimental-protocol.md:34`).
- The firmware table comparison above.
- OpenRGB's `0x1642` code path.

---

## 1. Transport, report types, sizes, report-ID conventions

### 1.1 Interface

- Use interface 1, usage page `0xFFC0`. OpenRGB registers the Gen 3 wired PID on interface 1
  (`ORGB/SteelSeriesControllerDetect.cpp:555`, PID constant `ORGB/SteelSeriesDevices.h:102`). ssgg's control usage
  page is `0xFFC0` (`SSGG/src/devices/mod.rs:404`). GG sent all host-to-device traffic to `wIndex = 1` on `0x1628`
  (`SSGG/docs/development/apex-pro-tkl-2023-gg-capture.md:10-16`). [1642-ORGB] for the interface number.
- Report descriptor, decoded from the `0x1628` firmware: vendor `0xFFC0` with report ID 0, a 64-byte input, a 64-byte
  output and a **644-byte feature** report. A second vendor collection `0xFFC1` has a 64-byte input report only
  (`firmware-notes.md:103-117`). This matches what you measured on your `0x1642`. GG's spec gives `0x1642` a
  `feature-report-size` of 645 (report-ID byte included) (`SSGG/src/devices/keyboards/model_caps.rs:111-114`, test
  row `:225`). [1642-SSGG]
- Interface 4 (`0xFFC1`) carries unsolicited board reports. On `0x1628`, changing actuation in the OLED menu sent
  `0f <h> <l> <32-byte HID bitmask>`, and changing release mode sent `0d <mode> <bitmask>`, both on EP `0x85`
  (`apex-pro-tkl-2023-gg-capture.md:413-415`). That EP `0x85` is your interface 4 is [INFERRED-1642]. Section 3.6
  explains why these reports are useful.

### 1.2 The two request channels

| Channel | Linux hidraw call | Buffer you pass | On the wire | How the answer arrives |
|---------|-------------------|-----------------|-------------|-------------------------|
| **Output report** (GG `HIDIO`) | `write(fd, buf, 65)` | `buf[0]=0x00` (report ID), `buf[1]=cmd`, args, zero-padded to 65 | 64 bytes (`SET_REPORT wValue 0x0200`, or interrupt OUT) | 64-byte input report on interface 1 (EP `0x82`): `read(fd, buf, 64)` |
| **Feature report** (GG `HIDFEATURE`) | `ioctl(fd, HIDIOCSFEATURE(len), buf)` | `buf[0]=0x00`, `buf[1]=cmd`, ..., zero-padded | `len-1` bytes (`SET_REPORT wValue 0x0300`) | If the command answers, read it with `ioctl(fd, HIDIOCGFEATURE(len), buf)`, setting `buf[0]=0` first |

Sources:
- 65-byte output buffers: `SSGG/src/devices/hid_reports.rs:15` (`KEYBOARD_REPORT_SIZE = 65`) and
  `ORGB/SteelSeriesApexBaseController.h:20-21` (`PACKET_OUT_SIZE = 64 + 1`).
- Output reports go out with `hid_write`: `ORGB/.../SteelSeriesApexController.cpp:109,294,346,381`.
- Transport names come from GG's spec: `SSGG/docs/development/gg-keyboard-apis.md:39-52, 79-93`.

**Reply layout (all answers).** Byte 0 echoes the request's command byte. Byte 1 is the status: `0x00` means OK,
`0xFF` means no handler for that tag, and the `0x40` handler returns `0x02` on a length error. Handler output starts
at byte 2 (`firmware-notes.md:133-135, 274-277`) [STATIC].
- On Linux with no report IDs, `read()` on hidraw returns the 64 bytes **without** a report-ID prefix, so
  `ibuf[0]` is the echoed command. OpenRGB relies on this for `0x1642`:
  - `ibuf[0]==0xF5` (`SteelSeriesApexController.cpp:384`)
  - `ibuf[0]==0xA3 && ibuf[1]==0x00` (`:302`)
  - `ibuf[0]==0x90` (`ORGB/SteelSeriesApexBaseController.cpp:122`)
- ssgg accepts both with and without a leading `00` (`SSGG/src/devices/keyboards/key_action.rs:393-403`,
  `apex_pro_tkl_2023.rs:522-530`).
- A feature read returns `[0x00 report ID][echo][status][data...]`. ssgg's header lengths count the report-ID byte:
  `83` → 3, `B6` → 5, `B7` → 7 (`hid_reports.rs:1145, 1230, 1393`). The check is at `apex_pro_tkl_2023.rs:465-470`.

**Command dispatch** [STATIC] (`firmware-notes.md:143-148`):
- `tag = cmd & 0x7F`. **Bit 7 set selects the tag's read handler.**
- So `B6` reads `36`, `F5` reads `75`, `B2` reads `32`, `83` reads `03`, and so on.
- `F5` (read region) and `75` (**write region to flash**) differ by one bit. Guard against bit-flip bugs.

### 1.3 Feature report length: the sources disagree

| Who | Frame | Buffer length (incl. report ID) | Wire bytes on Linux usbhid | Cite |
|-----|-------|---------------------------------|----------------------------|------|
| OpenRGB, `0x1642` | `40` direct | 643 | 642 | `ORGB/SteelSeriesApexBaseController.h:50`, `SteelSeriesApexController.cpp:46-50,69,194` |
| OpenRGB, `0x1642` | `4B` init | 65 | 64 | `SteelSeriesApexController.cpp:262-267` |
| ssgg, `0x1642` | `40` direct, `4B` init | 643 | 642 | `SSGG/src/devices/keyboards/apex_pro_tkl_2023.rs:30,71-78`, `hid_reports.rs:1597` |
| ssgg, all 645-byte boards | `38 6x`, `36`, `B6`, `B7`, `83 01`, `03 01`, `1F 81`, `38 83` | 645 | 644 | `hid_reports.rs:652-653` |
| GG on `0x1628` | everything | 645 | 644 | `apex-pro-tkl-2023-gg-capture.md:13-19`, `:274-278` (Windows requires an exact caps-length match) |

[STATIC]: the firmware accepts a `SET_REPORT` with `wLength` **up to** 644 (`firmware-notes.md:123-125`). The `0x40`
handler only requires `len >= count*4 + 1` (`:274-277`). Shorter frames are therefore accepted.
`GET_REPORT` is capped at 644, and longer requests stall (`:130-131`).

**Recommendation** [INFERRED-1642]:
- Send every feature report as a **645-byte buffer** (`00` + 644). That matches the descriptor and GG exactly.
- 643 is what both community tools send to `0x1642` for lighting. Use it as the fallback if 645 misbehaves.

**GET_FEATURE on Linux.** ssgg found on an Apex 7 that the buffer length goes to the ioctl verbatim, and that the
wire-sized length answered while length + 1 never completed (`hid_reports.rs:1763-1771`). It also warns that "Linux
hidraw may drop the leading report-id byte on feature reads" (`.claude/rules/experimental-protocol.md:46`).
For `0x1642` [INFERRED-1642]:
1. Try `HIDIOCGFEATURE(645)` first.
2. If it times out, use 644.
3. Locate the reply by its echo byte rather than a fixed offset.

### 1.4 Timing

- 13 ms between consecutive commands (`hid_reports.rs:1394-1395`, GG's `time-between-commands-onboard-override`).
- 50 ms after `4B` (`apex_pro_tkl_2023.rs:665-666`).
- Wait up to 5000 ms for the reply to erase, commit or load (`apex_pro_tkl_2023.rs:386-387`).
- Drain stale input reports before a request whose answer you need (`apex_pro_tkl_2023.rs:959-967`). Accept only a
  reply whose echo byte matches (`:1124-1137`).
- OpenRGB uses 2 ms timeouts for the firmware and region reads and 100 ms for brightness
  (`SteelSeriesApexBaseController.h:22`, `SteelSeriesApexController.cpp:296,382`).

### 1.5 Command summary for 0x1642

| Bytes after report ID | Channel | Purpose | Answer | Evidence |
|-----------------------|---------|---------|--------|----------|
| `4B` | feature | Gen 3 "init" before direct frames | none known; firmware has no `4B` handler (see 2.1) | [1642-ORGB] [1642-SSGG] |
| `40 <n> (usage R G B)*n` | feature | direct per-key RGB | none | [1642-ORGB] [1642-SSGG] [HW-1628] |
| `41` | output | leave direct mode, return to on-board lighting | none | [1642-ORGB] [HW-1628] |
| `23 <0..10>` / `A3` | output | on-board illumination brightness set / read | `A3 00 <lvl>` | [1642-ORGB] (generic Apex path) |
| `90 00` / `90 01` | output | firmware version / build id | `90 00 <ASCII>` | [1642-ORGB] [HW-1628] |
| `F5` | output | region read | `F5 <err> <region>` | **[1642-ORGB]** (`cpp:377`) [HW-1628] |
| `F2` | output | layout read | `F2 <err> <layout>` | [1642-SSGG] [HW-1628] |
| `38 61 <n> <layer> (hid h l)*n` | feature | actuation (hall thresholds) | none | [HW-1628] layer 0; ssgg **refuses** it on `0x1642` |
| `38 62 <n> (hid mode)*n` | feature | release mode (Rapid Trigger / Key Guard) | none | [SPEC] (frame seen from GG on 1628) |
| `38 65 <n> (hid val)*n` | feature | Rapid Trigger sensitivity | none | [SPEC] |
| `38 66 <0/1>` | output | Rapid Tap enable | `38 00 66` | [SPEC] (frame seen from GG on 1628) |
| `38 67 <n> (...)` | output | Rapid Tap pairs | ? | [SPEC] |
| `B8 61 <n> <layer> (hid 0 0)*n` | feature, then GET_FEATURE | actuation read-back | thresholds | [STATIC] only |
| `B2 <slot>` then `34 00` | output | activate on-board profile slot 0-4 | `B2 00 ...` | [SPEC], captured from GG on 1628 |
| `83 01 <fs> F4 01 <off u32>` | feature, then GET_FEATURE | read a profile slot chunk | `00 83 00 <500 B>` | [HW-1628]; ssgg allows it on `0x1642` |
| `B6 <layer> <n> (hid 0 0 0 0 0)*n` | feature, then GET_FEATURE | key-mapping read | 5-byte header + 6 B/key | [HW-1628]; ssgg allows it on `0x1642` |
| `B7 <off u16> <len u16>` | feature, then GET_FEATURE | macro data read | 7-byte header + data | [HW-1628]; ssgg allows it on `0x1642` |
| `1F 82` | output | OLED back to firmware | none | [HW-1628] |

---

## 2. Per-key RGB

### 2.1 Init (`4B`)

- **OpenRGB** maps `0x1642` to `APEX_GEN3` (`ORGB/.../SteelSeriesApexController.cpp:52-62`, row `:59`). For
  `kbd_quirk >= APEX_GEN3` it sends a 65-byte **feature** report `00 4B 00…` once at construction and again when
  leaving on-board mode (`:219-275`, specifically `:260-269`; re-init at `:116-119`). The comment there:
  "seems to be required for some models and not others, regardless there is no harm in sending it" (`:255-259`).
  [1642-ORGB]
- **ssgg** `GEN3_FRAME` (used for `0x1642`) sets `init: true` (`apex_pro_tkl_2023.rs:71-78`, model row
  `SSGG/src/devices/keyboards/models.rs:167`). It sends a 643-byte feature report `00 4B` followed by zeros once,
  before the first frame, then sleeps 50 ms (`apex_pro_tkl_2023.rs:657-670`; command at `hid_reports.rs:1590-1620`).
  [1642-SSGG]
- GG was never seen sending `4B` to `0x1628` (`apex-pro-tkl-2023-gg-capture.md:41`, `:383`;
  `experimental-protocol.md:20`).
- [STATIC] Tag `0x4B` does not appear in the `0x1628` top-level dispatch table (`firmware-notes.md:163-186`; "Only
  tags absent from every table are absent", `:286`), and the `0x1642` table is identical (`:299`). The firmware
  would answer it `4b ff`, which is a no-op. **[INFERRED-1642]: harmless and probably unnecessary.** Sending it once,
  as both tools do, costs nothing.

### 2.2 Direct frame (`40`), feature report

```
buf[0]           = 0x00            report ID
buf[1]           = 0x40            direct_write (wired); 0x61 is the wireless variant, not for 0x1642
buf[2]           = n               number of entries (OpenRGB/ssgg send 112 = 0x70 for 0x1642)
buf[3+4*i+0]     = HID usage of LED i
buf[3+4*i+1..3]  = R, G, B (0..255, absolute, no gamma)
rest             = 0x00 up to the report length (see 1.3)
```

- OpenRGB build: `SteelSeriesApexController.cpp:128-200`. Packet ID `0x40` for the wired Gen 3 PID is at `:148-165`
  (only the wireless PIDs get `0x61`). Header and entries: `:176-189`. It sends a feature report at `:194`.
  [1642-ORGB]
- ssgg build: `hid_reports.rs:1514-1588`. `0x1642` uses `0x40`, 643 bytes, and the 112-key `TKL_KEYS` table
  (`apex_pro_tkl_2023.rs:36-47, 71-78`; tests `:1517-1520, :2126-2131`). The frame header for an all-red 112-key
  frame is `00 40 70 04 FF 00 00 …` (`:2143`). The code comment claims the wired-only Gen 3 model uses `0x40` and
  the wireless Gen 3 uses `0x61`, "verified on" the wireless board (`:648-652`). [1642-SSGG]
- GG on `0x1628` always sent `n = 0x54` (84) entries in its own order and streamed about 19 frames/s
  (`apex-pro-tkl-2023-gg-capture.md:25-40`). [HW-1628]
- GG's spec lighting layout for `0x1642` has 85 keys: the `0x1628` set plus `0xFB`
  (`model_caps.rs:93-101`, test `:294-302`). It is a US layout list: no `0x32`, `0x64` or `0x87-0x8B`
  (`model_caps.rs:46-49`). [1642-SSGG]
- [STATIC] The `0x40` write handler reads byte 1 as the count:
  - `count == 0xFF`: fill every key with the 24-bit colour at count+1..+3. ssgg does not use this and it is untested.
  - Otherwise: apply `count` 4-byte entries. The data length must be at least `count*4+1`, or the status is 2.
  - `C0` (bit 7 set) takes 5-byte entries and is undecoded.

  (`firmware-notes.md:274-278`)
- The frame is a full replacement in both tools. For a single-key change, resend all keys from a host-side colour
  cache. ssgg keeps a cache on disk for exactly this reason (`apex_pro_tkl_2023.rs:761-777`).
- Unknown or absent usages: OpenRGB says the firmware "will pick the keys applicable to it, ignoring the rest"
  (`ORGB/SteelSeriesApexRegions.h:22-33`). That is OpenRGB's general claim, not something verified on `0x1642`.
  [INFERRED-1642] Sending the full 112-entry table is safe and covers ANSI, ISO and JIS.
- Streaming: ssgg confirmed 25 fps on `0x1628` (`experimental-protocol.md:18`). [HW-1628] On `0x1628`, with GG
  closed, the board keeps the last frame until told otherwise (`apex-pro-tkl-2023-gg-capture.md:36-38`).
- **Do not** send `09` (APPLY) after a frame. Gen 3 firmware has no `09` handler (`firmware-notes.md:286`), it
  "reverts direct lighting" in older notes (`apex_pro_tkl_2023.rs:1174-1176, 1218-1224`), and on other families GG
  names it `save_to_flash` (`SSGG/src/devices/keyboards/mod.rs:696-697`).

### 2.3 LED IDs: they are HID usages

The LED "id" in a frame is the USB HID keyboard usage of the key (`hid_reports.rs:1514-1515`,
`apex-pro-tkl-2023-gg-capture.md:33`). The two vendor usages:
- `0xF0` is the SteelSeries/Fn key. OpenRGB names it `RIGHT_FUNCTION`.
- `0xFB` is the extra LED. OpenRGB names it Media Play/Pause and places it at row 0, column 17 on TKL
  (`ORGB/SteelSeriesApexRegions.h:169, :251`). ssgg calls it the logo LED in `TKL_KEYS` (`apex_pro_tkl_2023.rs:44-46`)
  and found it to be the logo on the `0x1640` (`experimental-protocol.md:25`). ssgg's capability table calls it "the
  play/pause key `0xFB` over the numpad" for the 2024 TKLs (`model_caps.rs:93-94`). Which physical LED `0xFB` is on
  your `0x1642` is unresolved.

**OpenRGB's full table for `0x1642`.** LED index = position in OpenRGB `keys[]` (`SteelSeriesApexController.cpp:23-37`)
and `led_names[]` (`SteelSeriesApexRegions.h:56-170`). These are also the frame order OpenRGB sends.

Positions are `row`/`col` in OpenRGB's 6×22 matrix (`Regions.h:35-48`) after the patches:
- ANSI = `apex_tkl_us_region_patch` (`:247-269`)
- ISO = TKL patch + `apex_iso_region_patch` (`:236-241`)
- JIS = TKL patch + `apex_jp_region_patch` (`:222-234`)

A `-` means OpenRGB places no LED there for that layout. The table is generated from the source arrays; the
generator matched both arrays at 112 entries.

The "in GG 85-key 0x1642 layout" column is ssgg's `TKL_2024_LIGHTING_KEYS` (`model_caps.rs:95-101`). The "analog"
column is ssgg's `TKL_ANALOG_KEYS` (`model_caps.rs:77-82`).

| idx | usage | OpenRGB name (`KEY_...`) | ANSI pos | ISO pos | JIS pos | in GG 85-key 0x1642 layout | analog (OmniPoint) |
|----:|------:|---------------|---------|--------|--------|:---:|:---:|
| 0 | `0x04` | EN_A | r3c2 | r3c2 | r3c2 | yes | yes |
| 1 | `0x05` | EN_B | r4c6 | r4c6 | r4c6 | yes | yes |
| 2 | `0x06` | EN_C | r4c4 | r4c4 | r4c4 | yes | yes |
| 3 | `0x07` | EN_D | r3c4 | r3c4 | r3c4 | yes | yes |
| 4 | `0x08` | EN_E | r2c4 | r2c4 | r2c4 | yes | yes |
| 5 | `0x09` | EN_F | r3c5 | r3c5 | r3c5 | yes | yes |
| 6 | `0x0A` | EN_G | r3c6 | r3c6 | r3c6 | yes | yes |
| 7 | `0x0B` | EN_H | r3c7 | r3c7 | r3c7 | yes | yes |
| 8 | `0x0C` | EN_I | r2c9 | r2c9 | r2c9 | yes | yes |
| 9 | `0x0D` | EN_J | r3c8 | r3c8 | r3c8 | yes | yes |
| 10 | `0x0E` | EN_K | r3c9 | r3c9 | r3c9 | yes | yes |
| 11 | `0x0F` | EN_L | r3c10 | r3c10 | r3c10 | yes | yes |
| 12 | `0x10` | EN_M | r4c8 | r4c8 | r4c8 | yes | yes |
| 13 | `0x11` | EN_N | r4c7 | r4c7 | r4c7 | yes | yes |
| 14 | `0x12` | EN_O | r2c10 | r2c10 | r2c10 | yes | yes |
| 15 | `0x13` | EN_P | r2c11 | r2c11 | r2c11 | yes | yes |
| 16 | `0x14` | EN_Q | r2c2 | r2c2 | r2c2 | yes | yes |
| 17 | `0x15` | EN_R | r2c5 | r2c5 | r2c5 | yes | yes |
| 18 | `0x16` | EN_S | r3c3 | r3c3 | r3c3 | yes | yes |
| 19 | `0x17` | EN_T | r2c6 | r2c6 | r2c6 | yes | yes |
| 20 | `0x18` | EN_U | r2c8 | r2c8 | r2c8 | yes | yes |
| 21 | `0x19` | EN_V | r4c5 | r4c5 | r4c5 | yes | yes |
| 22 | `0x1A` | EN_W | r2c3 | r2c3 | r2c3 | yes | yes |
| 23 | `0x1B` | EN_X | r4c3 | r4c3 | r4c3 | yes | yes |
| 24 | `0x1C` | EN_Y | r2c7 | r2c7 | r2c7 | yes | yes |
| 25 | `0x1D` | EN_Z | r4c2 | r4c2 | r4c2 | yes | yes |
| 26 | `0x1E` | EN_1 | r1c1 | r1c1 | r1c1 | yes | yes |
| 27 | `0x1F` | EN_2 | r1c2 | r1c2 | r1c2 | yes | yes |
| 28 | `0x20` | EN_3 | r1c3 | r1c3 | r1c3 | yes | yes |
| 29 | `0x21` | EN_4 | r1c4 | r1c4 | r1c4 | yes | yes |
| 30 | `0x22` | EN_5 | r1c5 | r1c5 | r1c5 | yes | yes |
| 31 | `0x23` | EN_6 | r1c6 | r1c6 | r1c6 | yes | yes |
| 32 | `0x24` | EN_7 | r1c7 | r1c7 | r1c7 | yes | yes |
| 33 | `0x25` | EN_8 | r1c8 | r1c8 | r1c8 | yes | yes |
| 34 | `0x26` | EN_9 | r1c9 | r1c9 | r1c9 | yes | yes |
| 35 | `0x27` | EN_0 | r1c10 | r1c10 | r1c10 | yes | yes |
| 36 | `0x28` | EN_ANSI_ENTER | r3c13 | r2c14 | r2c14 | yes | yes |
| 37 | `0x29` | EN_ESCAPE | r0c0 | r0c0 | r0c0 | yes |  |
| 38 | `0x2A` | EN_BACKSPACE | r1c13 | r1c13 | r1c14 | yes | yes |
| 39 | `0x2B` | EN_TAB | r2c0 | r2c0 | r2c0 | yes | yes |
| 40 | `0x2C` | EN_SPACE | r5c7 | r5c7 | r5c7 | yes | yes |
| 41 | `0x2D` | EN_MINUS | r1c11 | r1c11 | r1c11 | yes | yes |
| 42 | `0x2E` | EN_EQUALS | r1c12 | r1c12 | r1c12 | yes | yes |
| 43 | `0x2F` | EN_LEFT_BRACKET | r2c12 | r2c12 | r2c12 | yes | yes |
| 44 | `0x30` | EN_RIGHT_BRACKET | r2c13 | r2c13 | r2c13 | yes | yes |
| 45 | `0x32` | EN_POUND | - | r3c13 | r3c13 |  | yes |
| 46 | `0x33` | EN_SEMICOLON | r3c11 | r3c11 | r3c11 | yes | yes |
| 47 | `0x34` | EN_QUOTE | r3c12 | r3c12 | r3c12 | yes | yes |
| 48 | `0x35` | EN_BACK_TICK | r1c0 | r1c0 | r1c0 | yes | yes |
| 49 | `0x36` | EN_COMMA | r4c9 | r4c9 | r4c9 | yes | yes |
| 50 | `0x37` | EN_PERIOD | r4c10 | r4c10 | r4c10 | yes | yes |
| 51 | `0x38` | EN_FORWARD_SLASH | r4c11 | r4c11 | r4c11 | yes | yes |
| 52 | `0x39` | EN_CAPS_LOCK | r3c0 | r3c0 | r3c0 | yes | yes |
| 53 | `0x3A` | EN_F1 | r0c2 | r0c2 | r0c2 | yes |  |
| 54 | `0x3B` | EN_F2 | r0c3 | r0c3 | r0c3 | yes |  |
| 55 | `0x3C` | EN_F3 | r0c4 | r0c4 | r0c4 | yes |  |
| 56 | `0x3D` | EN_F4 | r0c5 | r0c5 | r0c5 | yes |  |
| 57 | `0x3E` | EN_F5 | r0c7 | r0c7 | r0c7 | yes |  |
| 58 | `0x3F` | EN_F6 | r0c8 | r0c8 | r0c8 | yes |  |
| 59 | `0x40` | EN_F7 | r0c9 | r0c9 | r0c9 | yes |  |
| 60 | `0x41` | EN_F8 | r0c10 | r0c10 | r0c10 | yes |  |
| 61 | `0x42` | EN_F9 | r0c11 | r0c11 | r0c11 | yes |  |
| 62 | `0x43` | EN_F10 | r0c12 | r0c12 | r0c12 | yes |  |
| 63 | `0x44` | EN_F11 | r0c13 | r0c13 | r0c13 | yes |  |
| 64 | `0x45` | EN_F12 | r0c14 | r0c14 | r0c14 | yes |  |
| 65 | `0x46` | EN_PRINT_SCREEN | - | - | - |  |  |
| 66 | `0x47` | EN_SCROLL_LOCK | - | - | - |  |  |
| 67 | `0x48` | EN_PAUSE_BREAK | - | - | - |  |  |
| 68 | `0x49` | EN_INSERT | r1c15 | r1c15 | r1c15 | yes |  |
| 69 | `0x4A` | EN_HOME | r1c16 | r1c16 | r1c16 | yes |  |
| 70 | `0x4B` | EN_PAGE_UP | r1c17 | r1c17 | r1c17 | yes |  |
| 71 | `0x4C` | EN_DELETE | r2c15 | r2c15 | r2c15 | yes |  |
| 72 | `0x4D` | EN_END | r2c16 | r2c16 | r2c16 | yes |  |
| 73 | `0x4E` | EN_PAGE_DOWN | r2c17 | r2c17 | r2c17 | yes |  |
| 74 | `0x4F` | EN_RIGHT_ARROW | r5c17 | r5c17 | r5c17 | yes |  |
| 75 | `0x50` | EN_LEFT_ARROW | r5c15 | r5c15 | r5c15 | yes |  |
| 76 | `0x51` | EN_DOWN_ARROW | r5c16 | r5c16 | r5c16 | yes |  |
| 77 | `0x52` | EN_UP_ARROW | r4c16 | r4c16 | r4c16 | yes |  |
| 78 | `0x64` | EN_ISO_BACK_SLASH | - | r4c1 | - |  | yes |
| 79 | `0xE0` | EN_LEFT_CONTROL | r5c0 | r5c0 | r5c0 | yes | yes |
| 80 | `0xE1` | EN_LEFT_SHIFT | r4c0 | r4c0 | r4c0 | yes | yes |
| 81 | `0xE2` | EN_LEFT_ALT | r5c2 | r5c2 | r5c2 | yes | yes |
| 82 | `0xE3` | EN_LEFT_WINDOWS | r5c1 | r5c1 | r5c1 | yes | yes |
| 83 | `0xE4` | EN_RIGHT_CONTROL | r5c14 | r5c14 | r5c14 | yes | yes |
| 84 | `0xE5` | EN_RIGHT_SHIFT | r4c12 | r4c12 | r4c13 | yes | yes |
| 85 | `0xE6` | EN_RIGHT_ALT | r5c11 | r5c11 | r5c12 | yes | yes |
| 86 | `0xE7` | EN_RIGHT_WINDOWS | r5c12 | r5c12 | - | yes | yes |
| 87 | `0xF0` | EN_RIGHT_FUNCTION | r5c13 | r5c13 | r5c13 | yes | yes |
| 88 | `0x31` | EN_ANSI_BACK_SLASH | r2c14 | - | - | yes | yes |
| 89 | `0x87` | JP_RO | - | - | r4c12 |  | yes |
| 90 | `0x88` | JP_KANA | - | - | r5c11 |  | yes |
| 91 | `0x89` | JP_YEN | - | - | r1c13 |  | yes |
| 92 | `0x8A` | JP_HENKAN | - | - | r5c10 |  | yes |
| 93 | `0x8B` | JP_MUHENKAN | - | - | r5c3 |  | yes |
| 94 | `0x53` | EN_NUMPAD_LOCK | - | - | - |  |  |
| 95 | `0x54` | EN_NUMPAD_DIVIDE | - | - | - |  |  |
| 96 | `0x55` | EN_NUMPAD_TIMES | - | - | - |  |  |
| 97 | `0x56` | EN_NUMPAD_MINUS | - | - | - |  |  |
| 98 | `0x57` | EN_NUMPAD_PLUS | - | - | - |  |  |
| 99 | `0x58` | EN_NUMPAD_ENTER | - | - | - |  |  |
| 100 | `0x59` | EN_NUMPAD_1 | - | - | - |  |  |
| 101 | `0x5A` | EN_NUMPAD_2 | - | - | - |  |  |
| 102 | `0x5B` | EN_NUMPAD_3 | - | - | - |  |  |
| 103 | `0x5C` | EN_NUMPAD_4 | - | - | - |  |  |
| 104 | `0x5D` | EN_NUMPAD_5 | - | - | - |  |  |
| 105 | `0x5E` | EN_NUMPAD_6 | - | - | - |  |  |
| 106 | `0x5F` | EN_NUMPAD_7 | - | - | - |  |  |
| 107 | `0x60` | EN_NUMPAD_8 | - | - | - |  |  |
| 108 | `0x61` | EN_NUMPAD_9 | - | - | - |  |  |
| 109 | `0x62` | EN_NUMPAD_0 | - | - | - |  |  |
| 110 | `0x63` | EN_NUMPAD_PERIOD | - | - | - |  |  |
| 111 | `0xFB` | EN_MEDIA_PLAY_PAUSE | r0c17 | r0c17 | r0c17 | yes |  |

Frame order for a C++ `static const uint8_t kLedUsage[112]` (identical to OpenRGB `keys[]`):

```
0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x32, 0x33, 0x34,
0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x64, 0xE0,
0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xF0, 0x31, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x53, 0x54,
0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0xFB
```

### 2.4 ANSI / ISO / JIS differences

The frame does not change with layout, because every usage is always sent. Only the GUI placement differs.
OpenRGB's patches (`Regions.h:222-241`):
- **ISO**:
  - ISO Enter: `0x28` moves to r2c14, where ANSI `\` (`0x31`) was.
  - `#~` (`0x32`) is added at r3c13.
  - `<>` / ISO `\` (`0x64`) is added at r4c1.
  - ANSI `\` `0x31` is not placed.
- **JIS**: adds `0x89` Yen (r1c13), `0x87` Ro (r4c12), `0x8B` Muhenkan (r5c3), `0x8A` Henkan (r5c10) and `0x88` Kana
  (r5c11). It moves Backspace `0x2A` to r1c14, Enter `0x28` to r2c14, `0x32` to r3c13 and RShift `0xE5` to r4c13. Key
  labels are renamed through `apex_jp_keyname_lookup` (`:329-339`). It also moves RAlt `0xE6` to r5c12.
- **TKL patch (all regions)**:
  - Drops PrtSc/ScrLk/Pause (`0x46-0x48`) and puts `0xFB` at r0c17 (`:249-251`).
  - Drops the numpad.
  - This matches GG's 85-key list, which has no `0x46-0x48` (`model_caps.rs:95-101`).

How OpenRGB picks the layout for `0x1642` (the special handling at `SteelSeriesApexController.cpp:377-410`) [1642-ORGB]:
1. Send output `00 F5`. Read 64 bytes with a 2 ms timeout.
2. If `ibuf[0]==0xF5`, map `ibuf[2]` to a SKU:
   - `0x01` → `64740` US
   - `0x03` → `64741` UK
   - `0x04` → `64743` (OpenRGB's list says German, `Regions.h:508`)
   - `0x06` → `64744` French
   - `0x0A` → `64742` Nordic
   - `0x0D` → `64745` JP
   - anything else → falls through to `"64865"` (`:419`)
3. Pick the patch for that SKU from `patch_lookup` (`Regions.h:371-377`). The RGB controller uses the first five
   characters of the "serial" as the SKU (`ORGB/SteelSeriesApexController/RGBController_SteelSeriesApex.cpp:105,123,137`).
4. **OpenRGB gaps**: `64741` (UK) and `64744` (FR) have no `patch_lookup` entry, so those boards get the unpatched
   full-size ANSI matrix (`Regions.h:444-461`).

ssgg names the region ids 1 us, 3 uk, 4 germany, 6 france, 10 nordic, 13 japan and 20 turkish
(`hid_reports.rs:1330-1342`). The layout query `F2` returns 0 US, 1 European or 3 Japan (`hid_reports.rs:1283-1286`).

### 2.5 Leaving direct mode, live vs persistent

- **Leave direct**: output report `00 41` (65 bytes).
  - OpenRGB sends it on destruction and when switching to its "Onboard" mode (`SteelSeriesApexController.cpp:96-126`).
    `reset_cmd = APEX_2023_PACKET_ID_ONBOARD` is set for Gen 3 at `:264`. [1642-ORGB]
  - GG spec `clear_direct_write` uses `HIDIO` (`gg-keyboard-apis.md:82`). Confirmed on `0x1628`
    (`experimental-protocol.md:52`). [HW-1628]
  - The firmware tag `41` has a write handler, and the `0x1642` tables are identical (`firmware-notes.md:181, 299`).
  - ssgg refuses `41` on `0x1642` only because it is unverified there (`model_caps.rs:57-59, 123`).
- **Persistence**: direct frames are **live only**. Neither repo has a command that stores per-key colours to
  on-board memory.
  - GG renders even the lock-key colour on the host (`apex-pro-tkl-2023-gg-capture.md:443`).
  - The 12,500-byte profile blob has no per-key colour table. Its only lighting fields are lock and meta key
    brightness and colour at offsets 28-75 (`apex-pro-tkl-2023-gg-capture.md:465-470`;
    `SSGG/src/devices/keyboards/apex_pro_tkl_2023_profile_codec.rs:35-47`).
  - The on-board effects (REACTIVE, BREATHE, ...) exist only as OLED-menu strings (`firmware-notes.md:93-97`).
    Their storage is undecoded.
  - Whether the last direct frame survives a USB replug is not stated anywhere. [INFERRED-1642]: assume it does not.

---

## 3. Actuation point (OmniPoint)

### 3.1 Command: `38 61`, feature report, all analog keys in one frame

```
buf[0]=0x00  buf[1]=0x38  buf[2]=0x61  buf[3]=count (0x44 = 68)  buf[4]=layer
buf[5+3*i .. 7+3*i] = hid_i, h_i, l_i        for i in 0..67, hid ascending
zero padding to the report length (645)
```

- Layer `0x00` is the primary actuation (`hall_thresholds`). Layer `0x02` is the second actuation point
  (`second_actuation_thresholds`) (`hid_reports.rs:718-726`; spec `gg-keyboard-apis.md:105-106`).
  [STATIC] Layers 0 and 1 share one table, and layer 2 has its own (`firmware-notes.md:193-195`).
- `h` is the actuation raw value and `l` is the reset (release) raw value (`hid_reports.rs:898`,
  `apex-pro-tkl-2023-gg-capture.md:63-64, 78-80`).
- The serializer is `hid_reports.rs:944-950, 982-993`. The frame build and order (68 analog keys sorted by usage)
  is `apex_pro_tkl_2023.rs:180-185, 236-260`.
- The GG wire frame for "all keys 0.1 mm" on `0x1628` was `00 38 61 44 00 04 05 05 05 05 05 06 ...`. For 4.0 mm it
  was the same with `e0 db` (`apex-pro-tkl-2023-gg-capture.md:407-410`). [HW-1628]
- On `0x1628` the board sends **no reply** to `38 61` (`:418-419`). ssgg still polls up to 3×100 ms for a `0x38`
  input report (`apex_pro_tkl_2023.rs:883-898`).
- **Per-key versus global.** The values are per key, but every frame carries all 68 keys. ssgg refuses any frame
  that does not list each analog key exactly once (`apex_pro_tkl_2023.rs:246-253`). GG also sent all 68 for a
  single-key change (`apex-pro-tkl-2023-gg-capture.md:440`). The comment at `hid_reports.rs:894-895` reads: "the
  count byte is a spec constant, so a frame cannot address one key alone". A "global" setting is the same `(h,l)`
  pair repeated. Whether the firmware accepts a smaller `count` is unknown.
- **0x1642 status**: ssgg **refuses** live `38 6x` on `0x1642` (`model_caps.rs:50-52, 121`;
  `apex_pro_tkl_2023.rs:869-874`). The stated reason: "the 2024 boards have their own raw hall-threshold tables,
  `0x1628`'s would set the wrong travel" (`experimental-protocol.md:36`; `docs/development/devices.md:78-79`).
  [STATIC] The `0x1642` class-`38` sub-tag table (`60 83 61 62 63 65 64 66 67`) is identical to `0x1628`'s
  (`firmware-notes.md:155, 299`), so the frame **format** is very likely the same. Only the **values** are in question.

### 3.2 Units: the `(h, l)` table (`0x1628`, GG's own)

The values are non-linear raw hall-sensor values, indexed by tenths of a millimetre (1..40 = 0.1..4.0 mm). Sources:
- `SSGG/src/devices/keyboards/apex_pro_tkl_2023_profile.rs:19-64`, lookup `:100-112`.
- Origin: the spec's `hall-thresholds-to-actuation-alist` plus GG's UI map. On `0x1628` it was cross-checked
  against the board's own OLED sync reports (`apex-pro-tkl-2023-gg-capture.md:74-77, 413-415`).

The default is 1.8 mm = `(47, 42)`. **[HW-1628] only. Do not assume it for `0x1642`** (see 3.1 and 7).

| mm | h (actuation) | l (reset) | mm | h | l |
|---:|---:|---:|---:|---:|---:|
| 0.1 | 5 (0x05) | 5 (0x05) | 2.1 | 62 (0x3E) | 57 (0x39) |
| 0.2 | 5 (0x05) | 4 (0x04) | 2.2 | 68 (0x44) | 62 (0x3E) |
| 0.3 | 6 (0x06) | 5 (0x05) | 2.3 | 74 (0x4A) | 68 (0x44) |
| 0.4 | 8 (0x08) | 6 (0x06) | 2.4 | 81 (0x51) | 74 (0x4A) |
| 0.5 | 10 (0x0A) | 8 (0x08) | 2.5 | 88 (0x58) | 81 (0x51) |
| 0.6 | 12 (0x0C) | 10 (0x0A) | 2.6 | 95 (0x5F) | 87 (0x57) |
| 0.7 | 14 (0x0E) | 12 (0x0C) | 2.7 | 104 (0x68) | 95 (0x5F) |
| 0.8 | 16 (0x10) | 14 (0x0E) | 2.8 | 114 (0x72) | 104 (0x68) |
| 0.9 | 18 (0x12) | 16 (0x10) | 2.9 | 124 (0x7C) | 114 (0x72) |
| 1.0 | 21 (0x15) | 18 (0x12) | 3.0 | 134 (0x86) | 123 (0x7B) |
| 1.1 | 24 (0x18) | 21 (0x15) | 3.1 | 147 (0x93) | 134 (0x86) |
| 1.2 | 27 (0x1B) | 24 (0x18) | 3.2 | 161 (0xA1) | 147 (0x93) |
| 1.3 | 30 (0x1E) | 27 (0x1B) | 3.3 | 175 (0xAF) | 161 (0xA1) |
| 1.4 | 33 (0x21) | 29 (0x1D) | 3.4 | 189 (0xBD) | 174 (0xAE) |
| 1.5 | 36 (0x24) | 33 (0x21) | 3.5 | 197 (0xC5) | 189 (0xBD) |
| 1.6 | 39 (0x27) | 36 (0x24) | 3.6 | 206 (0xCE) | 197 (0xC5) |
| 1.7 | 43 (0x2B) | 40 (0x28) | 3.7 | 215 (0xD7) | 206 (0xCE) |
| 1.8 | 47 (0x2F) | 42 (0x2A) | 3.8 | 224 (0xE0) | 215 (0xD7) |
| 1.9 | 52 (0x34) | 47 (0x2F) | 3.9 | 224 (0xE0) | 217 (0xD9) |
| 2.0 | 57 (0x39) | 52 (0x34) | 4.0 | 224 (0xE0) | 219 (0xDB) |

The sources disagree on whether `0x1642` really differs:
- ssgg's capability table says the 2024 boards "carry their own tables" (`model_caps.rs:50-52`).
- ssgg's own capture note says the `hall-thresholds-to-actuation-alist` it recovered from the 2024 Cyber device file
  "is a different table entirely: raw hall value → a 1-40 'actuation level' index for the Prism reactive-lighting
  effect, not mm depth" (`apex-pro-tkl-2023-gg-capture.md:169-172`).
- Neither repo contains the `0x1642` values.

### 3.3 Key index for actuation

- The `38 61` frame addresses keys by **HID usage**, the same id space as the LED frame. It does **not** use the LED
  index, and it does not use a scan index.
- Only the 68 analog keys can be set (`model_caps.rs:75-82`, which ssgg also applies to `0x1642` at `:116`):

```
04-27, 28, 2A, 2B, 2C, 2D, 2E, 2F, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
64, 87, 88, 89, 8A, 8B, E0, E1, E2, E3, E4, E5, E6, E7, F0
```

  (`0x04-0x27` is a..z plus 1..0.) Esc (`0x29`), F1-F12, the navigation cluster, the arrows and `0xFB` are **not**
  analog (`apex-pro-tkl-2023-gg-capture.md:97`). The absent ISO/JIS usages are present in the table. ssgg sends all 68
  whatever the physical layout.
- The **on-board profile blob** uses a third ordering, the "slot", in physical row order:
  - `WIRED_2023_PROFILE_SLOTS` (`apex_pro_tkl_2023.rs:118-142`)
  - `DEVICE_KEY_HID` (`apex_pro_tkl_2023_profile_codec.rs:73-79`)
  - The slot map table is `apex-pro-tkl-2023-gg-capture.md:82-97`.
  - Blob offsets: actuation `1084+slot`, reset `1154+slot`, second-layer `1306/1376+slot` (`codec.rs:50-55`).
  - That is the `0x1628` blob. ssgg says the `0x1642` blob is "not interpreted" (`model_caps.rs:53-56`).

### 3.4 Read-back

1. `B8 61 <count> <layer> (hid, 0, 0) × count`. The firmware fills each entry's two bytes from the RAM profile.
   This is [STATIC] only, from handler `0x080177ec` (`firmware-notes.md:193-196`).
   - Inferred transport: the request is a feature report, so the answer comes back with `GET_FEATURE`
     (`firmware-notes.md:136-139`).
   - It is **untested on any board**. Expected reply: `[00] B8 00 61 <count> <layer> (hid h l)*count`. That layout
     is [INFERRED-1642], extrapolated from the echo/status rule.
2. Read the on-board slot with `83 01` (section 5.2) and decode bytes `1084..1223`. This is [HW-1628]. ssgg allows
   the read on `0x1642` (`model_caps.rs:122`), but the `0x1628` decoding may not apply.
3. GG itself never read actuation live. Its UI uses its own cache (`apex-pro-tkl-2023-gg-capture.md:146-155`).

### 3.5 Things not to use for actuation

- `2D <tenths>` (ssgg `ActuationCommand`, `hid_reports.rs:2739-2818`) is a guess. It was a confirmed no-op on
  `0x1628` (`experimental-protocol.md:42`), and there is no tag `2D` in the Gen 3 table (`firmware-notes.md:163-186`).
- `31/32/33/34/35 47 ...` are the Gen 1 analog frames (Apex Pro `0x1610`/`0x1614`/`0x1640`), not this board
  (`gg-keyboard-apis.md:201-217`). On Gen 3, `32 <slot>` **saves the profile to flash** and `35` is the meta key.

### 3.6 Safe way to get `0x1642`'s real `(h,l)` table

[INFERRED-1642] This needs no host writes:
1. Listen on interface 4 (`0xFFC1`, input only).
2. Step the actuation in the keyboard's own OLED menu.
3. Record each `0f <h> <l> <bitmask>` report (`apex-pro-tkl-2023-gg-capture.md:413-415`).
4. Repeat for every 0.1 mm step to build the 40-entry table.

### 3.7 Persistence

[STATIC], from the `36` key-map handler, which is the analysed analogue (`firmware-notes.md:207-228`):
- Live writes go to the RAM profile.
- The firmware auto-saves the whole RAM profile to the active slot after on-board menu, macro or OLED edits. While
  `34 01` (volatile) is set it does not.
- `32 <slot>` saves explicitly.
- Whether `38 61` itself triggers a save was not decoded. GG follows every live `38 61` with a full profile rewrite
  (`apex-pro-tkl-2023-gg-capture.md:440`). Power-cycle survival is untested (`experimental-protocol.md:43`).

---

## 4. Rapid Trigger (and Key Guard, Rapid Tap)

### 4.1 Enable/disable: release mode `38 62`, per key, feature report

```
00 38 62 <count=0x44> (hid, mode) × 68        padded to 645
mode: 0x00 off, 0x02 Rapid Trigger, 0x03 Key Guard (protection), 0x04 unknown
```

- Sources:
  - `hid_reports.rs:900-901, 915, 951-955, 1011-1017`
  - Blob field meanings: `apex-pro-tkl-2023-gg-capture.md:66, 478`
  - CLI: `SSGG/src/cli.rs:860-867`
- GG frames seen on `0x1628`:
  - `00 38 62 44 04 02 ...` (RT on for all keys) and `... 04 00 ...` (off) (`apex-pro-tkl-2023-gg-capture.md:410`).
  - For W only: `1A 02` then `1A 00` inside a full 68-key frame, followed by a profile rewrite (`:441`).
  - These are GG captures. A `38 62` sent from ssgg is not confirmed (`experimental-protocol.md:43`).
- Per key. "Global" means all 68 keys get the same mode.

### 4.2 Sensitivity: `38 65`, per key, feature report

```
00 38 65 <count=0x44> (hid, value) × 68       padded to 645
```

- `hid_reports.rs:902, 951-955`; spec `gg-keyboard-apis.md:108`. **Never captured from GG.** [SPEC]
- **There is only one value per key.** No separate press and release sensitivity appears in the frame, the spec
  list or the blob. The blob field is `adaptive_dist_10[70]` at offset 1516, "rapid-trigger sensitivity, 0.1 mm"
  (`apex-pro-tkl-2023-gg-capture.md:479`; `codec.rs:57`).
- Unit: the blob field name says tenths of a millimetre. ssgg itself says "raw unit" / "the unit of the sensitivity
  is unknown" (`cli.rs:869`, `hid_reports.rs:902`). Live and default value: 2, i.e. 0.2 mm (`gg-capture.md:67, 163`).
  **The valid range is not in either repo.**

### 4.3 Key Guard parameters

`protection_dur_ms` (u32 at blob offset 1586, value 500) and `protection_dist_10[70]` (offset 1590, `0x14` = 2.0 mm)
exist only in the blob. No live command is known for wired Gen 3 (`apex-pro-tkl-2023-gg-capture.md:68, 480-481`).
The wireless base has a `protection_mode` op, which does not apply here (`gg-keyboard-apis.md:135`).

### 4.4 Rapid Tap (two-key snap)

- Enable: output report `00 38 66 <0|1>`. Answered `38 00 66` on `0x1628` (`apex-pro-tkl-2023-gg-capture.md:131,
  338-339`; `hid_reports.rs:904, 956`).
- Pairs: output report `00 38 67 <count> (index, hid1, hid2, mode | 0x80 if report_both, 0, 0) × count`.
  - Mode 0-3 has unknown meaning. ssgg allows 5 pairs, and the spec struct holds 10 (`hid_reports.rs:880-913,
    957-964`; `gg-keyboard-apis.md:110`).
  - GG did not use `38 67` to add a pair. It stored the pair in the blob at offset 2650 (`gg-capture.md:442`).

### 4.5 Read-back

[STATIC] Sub-tags `62` to `67` all have read handlers (`firmware-notes.md:188-191`), so `B8 62`, `B8 65` and so on
should exist. They are untested and the reply layout is undecoded. The board reports OLED-menu release-mode
changes on interface 4 as `0d <mode> <bitmask>` (`gg-capture.md:415`).

### 4.6 0x1642 status

ssgg refuses all `38 6x` on `0x1642` (section 3.1). `62`, `65`, `66` and `67` carry no hall values, so the
"own hall table" concern does not apply to them. [INFERRED-1642] Same frames as `0x1628`.

---

## 5. Brightness, profiles, reads, firmware version

### 5.1 Brightness

- OpenRGB generic Apex path, which also runs for `0x1642` [1642-ORGB]:
  - Set: output `00 23 <0..10>` (`SteelSeriesApexController.cpp:326-349`; range `SteelSeriesApexBaseController.h:66-71`).
  - Read: output `00 A3`, answered `A3 00 <level>`. The level is accepted only if `<= 10`
    (`SteelSeriesApexController.cpp:283-314`).
  - OpenRGB offers brightness only if the read answers with status 0 (`:80-87`). It applies to **on-board lighting
    only**, not to direct frames (`:277-281`; `RGBController_SteelSeriesApex.cpp:69-80`).
- [STATIC] Tag `23` has both a read and a write handler in the Gen 3 table, but the handlers are undecoded
  (`firmware-notes.md:170`).
- GG's wired spec defines no brightness api (`gg-keyboard-apis.md:79-95`). GG and ssgg scale direct-frame colours
  in software (`apex-pro-tkl-2023-gg-capture.md:39-40`; `apex_pro_tkl_2023.rs:740-754`).
- Do not confuse this with the Apex 3 TKL `00 23 <0..16>` (`gg-keyboard-apis.md:253`), which is a different board.

### 5.2 On-board profiles (5 slots, 0-4; file-system id = `0x80 + slot`)

| Operation | Frames | Evidence |
|-----------|--------|----------|
| Activate slot | output `00 B2 <slot>` → wait for input `B2 00 …` (GG saw `b2 00 03 00`) → output `00 34 00` | `apex_pro_tkl_2023.rs:1017-1030`; `hid_reports.rs:819-878`; GG: `1F 82, B2 n, 34 00, 90 00` (`gg-capture.md:445`). [SPEC]/GG capture; ssgg allows it on `0x1628` only |
| Volatile flag | `34 01` stops auto-save to the active slot; `34 00` re-enables it; `B4` reads it | [STATIC] `firmware-notes.md:176, 224-228` |
| Save RAM profile to slot | `32 <slot>` (erase plus write of the slot) | [STATIC] `firmware-notes.md:174`; nothing sends it. **Writes flash** |
| Read slot | 25 × feature `00 83 01 <fs> F4 01 <offset u32 LE>` (offsets 0, 500, … 12000), each followed by GET_FEATURE → `00 83 00 <500 bytes>` | `hid_reports.rs:1373-1437`; `apex_pro_tkl_2023.rs:408-428, 920-950`. [HW-1628]; ssgg allows it on `0x1642` |
| Slot info | `84 01 <fs>` → 8-byte reply (two u32), undecoded | [STATIC] `firmware-notes.md:168` |
| Write slot | output `00 02 01 <fs>` (erase) → wait for `02 00`; 25 × feature `00 03 01 <fs> F4 01 <offset u16 LE> 00 00 <500 B>` 13 ms apart; output `00 B1 <slot>` → wait for `B1 00 …` | `hid_reports.rs:652-716, 750-817`; `apex_pro_tkl_2023.rs:978-1015`; `gg-capture.md:448-457`. **Destructive. ssgg allows it on `0x1628` only** (`:985`) |
| Blob integrity | 12,500 bytes; CRC at offset 12320 (u32 LE) = STM32 CRC-32 (poly `0x04C11DB7`, init `0xFFFFFFFF`, no reflection, no final XOR, data taken as LE 32-bit words fed MSB first); bytes 12324.. are `FF` | `apex_pro_tkl_2023_profile.rs:14-17, 66-98`; `gg-capture.md:109-119, 488-489` |

- OpenRGB's `0x89 <profile>` (`SteelSeriesApexBaseController.h:52`, `SteelSeriesApexController.cpp:205-217`) is the
  **Gen 1** `load_profile` (1-based, `gg-keyboard-apis.md:186`). OpenRGB never calls it. On Gen 3, `0x89` is a
  read of tag `09`, which has no handler. [INFERRED-1642] No-op. Use `B2`.
- The `0x1642` blob may differ from `0x1628`'s: "device schema 8 from firmware 1.19.0"
  (`experimental-protocol.md:35`; `model_caps.rs:53-56`).

### 5.3 Firmware version

- Output `00 90 00` gets the input answer `90 00 "1.19.7" 00…` (`gg-capture.md:130`; spec `gg-keyboard-apis.md:86`:
  `00 90 <err> <version string, 62 bytes>`).
- `90 01` returns a build-id string (`firmware-notes.md:169`).
- Parsing:
  - OpenRGB (`SteelSeriesApexBaseController.cpp:93-127`) removes all NULs, drops the first char when
    `ibuf[0]==0x90`, then keeps `X.Y.Z`.
  - ssgg finds the `0x90` byte, skips status, reads ASCII, and treats a string starting with `@` as an error
    (`apex_pro_tkl_2023.rs:522-530, 1139-1153`).
- GG ships firmware 1.19.7 for `0x1642` (`firmware-notes.md:21`). OpenRGB's ≥1.19.7 Gen 3 heuristic covers only
  `0x1614`/`0x1634`/`0x1620` (`SteelSeriesApexController.cpp:221-253`), because `0x1642` is always Gen 3.

### 5.4 Other read commands (input-report answers unless noted)

| Request | Answer | Notes |
|---------|--------|-------|
| `00 F5` | `F5 <err> <region>` | region ids at 2.4. [1642-ORGB] [HW-1628] |
| `00 F2` | `F2 <err> <layout>` | 0 US, 1 European, 3 Japan. ssgg assumes err 0 = OK (`apex_pro_tkl_2023.rs:487-499`) |
| `B6 <layer> <n> (hid 0 0 0 0 0)*n` (feature) | GET_FEATURE: `00 B6 00 ?? ?? (hid fn kc0 kc1 kc2 kc3)*n` | 5-byte header, the 2 bytes after status are undecoded; layer 0 base and 1 meta (91 keys), 2 second-actuation (68). Function bytes: `gg-capture.md:491-492`. `hid_reports.rs:1131-1173`; `key_action.rs:18-27, 309-345` |
| `B7 <off u16> <len u16>` (feature, in 4-byte events, 150 per chunk) | GET_FEATURE: `00 B7 00 <off> <len> <data>` | 16 chunks = 9600 bytes (`hid_reports.rs:1211-1271`; `firmware-notes.md:230-233`) |
| `B4` | volatile flag | [STATIC] |
| `B8 6x …` | analog tables | [STATIC], see 3.4 |

OLED, for completeness:
- `1F 82` (output) hands the OLED back to the firmware.
- `1F 81 <640 B>` (feature) is a direct image. The `0x1642` spec packs the image **column-wise**, unlike `0x1628`'s
  row-major format, and the format is undecoded (`model_caps.rs:17-27, 118`).

---

## 6. Opcodes to never send (blocklist)

ssgg labels these dangerous, or the firmware notes show they write flash or are undecoded. The dispatcher ignores
bit 7 when it picks the tag, so block by `cmd & 0x7F` **and** by the exact byte.

| Bytes | Why | Source |
|-------|-----|--------|
| Anything to PID **`0x1643`** | bootloader of `0x1642`, which only answers the firmware-update transport | `gg-keyboard-apis.md:14-15, 399` |
| Firmware update (`SSFWFIZZ` transport) | Used for Apex Pro TKL 2022/2024 with "pid, bootloader pid, fs id, fs file". For `0x1628`: fs id `0x01`, fs file `0x02`. The opcodes are **not in either repo** | `gg-keyboard-apis.md:114, 331-345`; `gg-capture.md:214-216` |
| `02 01 <fs>`, `03 01 <fs>` with fs **outside `0x80-0x84`** | [INFERRED-1642] could touch the firmware file system (fs `0x01`/`0x02` above). ssgg rejects any fs outside `0x80-0x84` | `hid_reports.rs:728-737` |
| `02 01 <0x80+n>` | erase: "Destroys the slot's stored profile until the chunks and the commit follow" | `hid_reports.rs:750-754`; `apex_pro_tkl_2023.rs:978-996` |
| `03 01 …`, `B1 <slot>` | profile chunk write and commit (flash) | `hid_reports.rs:655-660, 787-790` |
| `32 <slot>` | saves the RAM profile to a flash slot | `firmware-notes.md:174` |
| `75 <id>` | region write, "**Writes flash**" (one bit away from `F5`) | `hid_reports.rs:185-187, 1322-1324`; `firmware-notes.md:185`; `experimental-protocol.md:51` |
| `71`, `72`, `77` (writes) | reach on-chip flash (a 336-byte settings page at flash `0x8800`, 2 KB page erase). `72` is the write side of `F2` | `firmware-notes.md:182-183, 219-221` |
| `36` with count `0xFF` | restores the default key-map tables from ROM | `firmware-notes.md:211` |
| `36` / `37` writes in general | live remap and macro write. `37` first resets the recorder. Write paths are only allowed on `0x1628` | `firmware-notes.md:208-211, 233`; `model_caps.rs:60-63` |
| `09` | APPLY, which is `save_to_flash` on other Apex families. No handler on Gen 3 | `keyboards/mod.rs:696-697`; `firmware-notes.md:286` |
| `01`, `1E`, `20`, `24 xx` (`02`, `86`), `26`, `27`, `30`, `33`, `39-3E`, `73 xx` (`50 51 52 53 80 11`), `74`, `76` | present in the dispatch table, purpose **not decoded**. The firmware also has FACTORY RESET and RECALIBRATE (OLED menu strings) with no known HID opcode, so assume they may hide here | `firmware-notes.md:163-187, 93-97` |
| `90`, `91`, `92` | ssgg's actuation fuzzer skips them as "Firmware update start (very dangerous)" / "Firmware update". **Note:** `90 00`/`90 01` is the harmless version read (confirmed on `0x1628` and Apex 7). Never send `91`/`92` | `SSGG/src/bin/discover_actuation.rs:46-58`; `experimental-protocol.md:62` |
| `02` (any) | listed by the same fuzzer as "Firmware related?" | `discover_actuation.rs:54` |
| arbitrary bytes / fuzzing | "can potentially brick your device" | `SSGG/src/devices/fuzz.rs:41-47`; `.claude/skills/fuzz-runner/SKILL.md:8` |
| `40 FF …` (fill), `C0 …` | untested and undecoded handler forms | `firmware-notes.md:274-278` |

**Suggested allowlist for the C++ tool** (everything else rejected):
- Writes: `40`, `41`, `4B`, `38 61/62/65/66/67`, `B2 n` followed by `34 00`, `1F 82`, and optionally `23` with `A3`.
- Reads: `90 00/01`, `F2`, `F5`, `83 01 <0x80-0x84>`, `B6`, `B7`, `B4`, `B8 6x`.

---

## 7. Known gaps and uncertainties

1. **Nothing has been verified on `0x1642` hardware** in either repo:
   - OpenRGB has code but no test record.
   - ssgg's `GEN3_FRAME` says "No test record in this repo" (`apex_pro_tkl_2023.rs:71-72`).
   - Every `0x1642` row in ssgg is untested (`model_caps.rs:6-7`).
2. **Actuation values for `0x1642`.** ssgg says the raw `(h,l)` table differs from `0x1628`'s and refuses to send
   it. Its own capture note suggests the "other" table is a lighting table. The real `0x1642` values are in neither
   repo. Calibrate them with the read-only method in 3.6 before writing `38 61`.
3. **Feature report length**: OpenRGB and ssgg send 643 to `0x1642`, while the descriptor, GG and the spec use 645
   (1.3). GET_FEATURE length on Linux is unsettled (644 vs 645).
4. `4B` init: both tools send it, but the firmware table has no handler for it. It is probably a no-op.
5. `41` release: confirmed on `0x1628` only. ssgg refuses it on `0x1642`.
6. Rapid Trigger sensitivity:
   - The unit (probably 0.1 mm) and the valid range are unknown.
   - There is no separate press and release sensitivity in this protocol.
   - Release mode 4 has unknown meaning.
   - Rapid Tap pair `mode` 0-3 has unknown meaning.
7. Read-backs `B8 6x` are static analysis only. Their reply layout is inferred.
8. Persistence of live `38 6x`, `36` and direct frames across a power cycle is untested. The firmware auto-save
   behaviour is static analysis.
9. `0x1642` profile blob layout (schema) is not confirmed to match `0x1628`'s offsets.
10. `0xFB` identity: logo, play/pause key or media LED. OpenRGB and ssgg disagree.
11. Whether the `40` handler silently skips usages the board lacks. OpenRGB asserts it does, but that is not decoded
    for Gen 3.
12. OpenRGB layout bugs for UK (`0x03`) and FR (`0x06`) region codes. The German SKU (`0x04`) reuses the UK key-name
    lookup.
13. Brightness `23`/`A3` on Gen 3: the handlers exist, but their meaning and range (0-10 in OpenRGB) are not
    decoded. GG does not use them.
14. The unsolicited `0f`/`0d` reports on EP `0x85` are assumed to come from interface 4 (`0xFFC1`).
15. The OLED image format on `0x1642` (column-packed) is undecoded.
16. Firmware-update, factory-reset and recalibration opcodes are unknown, so treat every undecoded tag as unsafe.
