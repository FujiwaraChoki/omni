# Wooting 60HE v2: HID configuration protocol (for omni)

Status: research spec, written 2026-11-17. No hardware was touched. Every byte layout
below comes from a cited source. Things that are inferred, or that were only verified
on a sibling board (60HE+ or 80HE) and not on the 60HE v2, are marked **[INFERRED]**
or **[60HE+ only]**.

## 0. Sources and how much to trust them

| Tag | Source | What it is | Trust |
|---|---|---|---|
| **W** | Wootility 5.4.2 web bundle `https://wootility.io/assets/index-BaoALhfv.js` (sha256 `987ba72a09a78b1f…`, version constant `ak="5.4.2"`), downloaded to `/home/loki/Work/omni-research/wootility-web/`. | Wooting's own configurator: the code that actually drives the keyboard. It is obfuscated. I decoded its string table statically with my own scripts (`omni-research/tools/deob.py`, `tools/fold.py`, both read-only and never executing the bundle). The output is `wootility-web/index.deob2.js`. Citations name the class or function symbol (for example "W `class tY.getKeyboardProfile`"). `grep` the deob file for the quoted string to find it. | **Highest.** This is the reference implementation, and the protobuf schemas are the real ones with field names. |
| **S** | `WootingKb/wooting-rgb-sdk` @ `7e091a0` (2026-10-26), cloned to `omni-research/wooting-rgb-sdk` | Wooting's official open-source RGB SDK (C) | High for the RGB/WootDev subset |
| **PR75** | https://github.com/WootingKb/wooting-rgb-sdk/pull/75 (merged as `451f40d`) | A bug report and fix tested **on a physical 60HE V2** (fw 2.14.1, ISO, `31e3:1342`, usage page `0xff55`, Arch Linux, hidapi-hidraw) | High. It is the only 60HE V2 hardware evidence I found. |
| **A** | `WootingKb/wooting-analog-sdk` @ `be35a62` | Official analog SDK (Rust) | High for the analog interface |
| **E** | `eng1n88r/wooting` @ `7d4c6ad` (Rust daemon/CLI/GTK app, Linux), `docs/protocol.md` + `docs/captures/README.md` | Community reverse engineering with Wootility v5.4 WebHID captures on a **60HE+** (fw 2.14.1/2.14.2, `31e3:1322`) | Medium-high. Captures agree with W, but some names and interpretations are wrong (corrected below). |
| **O** | `OpenMouse-Project/keyboard-protocol` @ `3f9158f`, `docs/wooting-support.md` | Community captures plus Wootility 5.4.2 analysis on a **60HE+** (fw 2.13.0) | Medium-high, consistent with W |
| **L** | `meeuw/libwootility` @ `b235fac` | Older community command enum (60HE ARM) | Low (names only) |

There is **no official documentation of the settings protocol**. dev.wooting.io and the SDKs cover
only RGB (WootDev) and analog reads. Wooting's PastaJ36 gist
(https://gist.github.com/PastaJ36/04b82556bde3cd49183e913c833988ec) describes only the legacy AVR
framing. Everything about actuation, rapid trigger and profiles comes from W, corroborated by E and O.

---

## 1. Device identification, interfaces, framing

### 1.1 VID / PID

| Item | Value | Source |
|---|---|---|
| VID | `0x31E3` | S `wooting-usb.c:34`; W `O0=12771` |
| 60HE v2 base PID | `0x1340` | S `wooting-usb.c:54`; W device table `tT={'id':oe.WOOTING_60HE_V2,…,'pid':4928}`; O `wooting-support.md:28` |
| Runtime PIDs | `0x1340`, `0x1341`, `0x1342` = base OR'ed with 0, 1, 2 | S `wooting-usb.c:38-42,335-340,398-401` (`PID_ALT_CHECK`); W `UE=[0,1,2]`, `WE()` builds `pid|0..2` |
| Meaning of the low nibble | Equals DeviceConfig `gamepadMode`: `0` = Xbox/X360, `1` = HID ("Classic") gamepad, `2` = no gamepad | **[INFERRED]** from W enum `bt={X360:0,HidGamepad:1,None:2}`. E `protocol.md:92,186` saw `…20`=Xbox, `…21`=Classic, `…22`=off on a 60HE+. PR75's 60HE V2 enumerated as `0x1342` (gamepad off). |
| Model matching | Mask the PID with `0xFFF0` | A `plugin/wooting.rs:33`, `device.rs:242-246`; W `GE=65520` |
| Bootloader PID | `base | 0x000F` = **`0x134F`**, usage page `0xFFFF` | W `qE(u){return u|15}`, `T7()`, `Nl=65535`, `XE()` |

**Do not open the bootloader device.** See §8.

### 1.2 HID interfaces (usage pages)

| Usage page | Purpose | Source |
|---|---|---|
| **`0xFF55`** | **Config interface, "V3 / multi-report" protocol. This is the one omni uses.** | S `wooting-usb.c:60,426-442`; W `m9=65365`, `D0` ctor sets `_protocolRevision=3`, `magicWord=G3` when `collections[0].usagePage===0xFF55`; PR75 confirms `0xff55` on the 60HE V2 |
| `0x1337` | Legacy config interface (AVR boards, older firmware). Not used by the 60HE v2. | S `wooting-usb.c:59`; W `D7=4919` |
| `0xFF53` | Analog interface v2 (10-bit values, 4 bytes per key) | A `plugin/wooting.rs:31`; W `TC=65363`, `unpackAnalogInterfaceV2` |
| `0xFF54` | Analog interface v1 (8-bit) | A `plugin/wooting.rs:30`; W `SC=65364` |
| `0xFFFF` | Bootloader | W `Nl=65535` |

On Linux, pick the hidraw node by VID/PID (`/sys/class/hidraw/hidrawN/device/uevent`, `HID_ID=0003:000031E3:0000134x`).
Then parse `/sys/class/hidraw/hidrawN/device/report_descriptor` and look for `06 55 FF` (Usage Page 0xFF55).
On a 60HE+ that was USB interface 2 (E `protocol.md:15-17`). **[INFERRED]** that the 60HE v2 uses the same interface number, so match on the usage page rather than the interface number.

### 1.3 Report IDs and sizes on the 0xFF55 interface

The report descriptor declares numbered input **and** output reports with these payload sizes (sizes exclude the report-ID byte):

| Report ID | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Payload bytes | 32 | 62 | 254 | 510 | 1022 | 2046 |

There is also one **feature report, ID 1, 7 payload bytes**.

Sources: W `function ej(rev,id)` (rev 3: 1→32, 2→62, 3→254, 4→510, 5→1022, 6→2046) and `D0.writeBuffer`. E `protocol.md:16-17` read the descriptor on a 60HE+. PR75 saw a 33-byte (1+32) numbered response from a 60HE V2.

### 1.4 Magic word

* ARM/V3 interface: magic `0xDAD1`, sent **little-endian**, so the first two payload bytes are **`D1 DA`**. W `G3=56017`; `Or(magic,2)` is LE (W `function Or`).
* Legacy: `0xDAD0` (`D0 DA`, W `ZE=56016`). Bootloader: `0xDBDF` (`DF DB`, W `zQ=56287`).

### 1.5 Commands (host → device): FEATURE report, ID 1

A command is always a feature report. In hidraw terms that is `ioctl(fd, HIDIOCSFEATURE(8), buf)`:

```
buf[0]   = 0x01                report ID
buf[1]   = 0xD1                magic lo
buf[2]   = 0xDA                magic hi
buf[3]   = cmd                 command id (table §1.8)
buf[4..7]= param               uint32 LITTLE-endian
```

Sources: W `class a4.buffer` (7 bytes: magic(2) + cmd + `Or(param,4)`), and `D0.sendCommand` uses `sendFeatureReport(rev===3?1:0, buf)`. S `wooting-usb.c:684-701` builds the same layout. Note the SDK names the bytes `p3,p2,p1,p0`, which is just reversed naming of the same LE value.

**Profile parameter encoding.** Wootility's `Gt(profileId, param1=0, param2=0)` becomes (W `WQ`, `qQ`):

```
param = (namespace<<8 | index) & 0xFFFF      // or 0xFFFF if profileId is undefined
      | (param1 & 0xFF) << 16
      | (param2 & 0xFF) << 24
=> buf[4]=index, buf[5]=namespace, buf[6]=param1, buf[7]=param2
```

`namespace`: `0` = onboard ("Default"), `1` = linked (W `oE={onboard:0,linked:1}`).
O `wooting-support.md:240` observed that `0xFFFF` returns the **active** profile on a 60HE+. **[60HE+ only]**

### 1.6 Responses (device → host): INPUT report on the same interface

After a command, `read()` the hidraw fd. The response arrives as a numbered **input** report. It is not a GET_FEATURE. W `D0.sendCommandWithResponse` → `getInputReport`. PR75 confirms that the 60HE V2 answers with one numbered input report (33 bytes) and that reading until 2046 bytes times out.

```
r[0]     report ID (1..6; the small ones are typical)
r[1..2]  D1 DA
r[3]     command id echo   (0xFF on very old firmware = "no echo")
r[4]     status
r[5..6]  body length, uint16 LE
r[7..]   body (length as above; the rest is padding)
```

Source: W `class BQ` (header length 6 for `G3`, body length = `rr(buf.slice(4,6))`; W's buffer excludes the report ID). Example from a 60HE+ GetVersion: `01 D1 DA 01 88 03 00 02 0E 01 …` means status OK, length 3, firmware 2.14.1 (E `packet.rs:336-341`).

Status codes (W enum `Ab`): `0x88` SUCCESS, `0x66` UNKNOWN, `0x77` BUSY, `0x99` OVERFLOW, `0xAA` UNSUPPORTED, `0xFF` ERROR. Wootility throws on anything other than `0x88`.

There is **no CRC** on V3 frames. The legacy `0xDAD0` frames had a CRC16-CCITT trailer (S `wooting-usb.c:75-91,590-593`; W `NQ`/`pg`).

Other input reports can arrive on the same interface. Wootility filters them as follows (W `D0.newInputReport`):
* `r[1]==0xEE`: async **event**. `r[2..3]` is the length (LE), then a `profiles.Event` protobuf (for example `profileChanged{profileIndex, profileNamespace}`). Events are enabled with cmd 76 (§1.8).
* `r[1]==0xDD`: debug log line (`profiles.DebugLog`).
* A response whose `cmd` does not match is ignored, and Wootility keeps waiting.

Wootility's timeout is 4 s (W `DC=4000`). It serializes every transfer through one queue (W `FQ`). omni must do the same: one outstanding command at a time, matched on `r[3]`.

### 1.7 Writes (host → device): OUTPUT report, sized by payload

Structured writes are **output reports** (`write(fd, buf, size+1)`). Choose the **smallest report ID whose size fits** the frame and zero-pad to exactly that size (W `D0.writeBuffer`). The frame format (W `class Lt.getBuffer`) is:

```
buf[0]   report ID (1..6, smallest that fits)
buf[1..2] D1 DA
buf[3]   report type   ("output command", table §1.9; NOT the same numbering as feature cmds)
then, by body type:
  profile          : flags(1) | len u16 LE | protobuf
  profileExtended  : flags(1) | param1(1) | additional u16 LE | len u16 LE | protobuf
  withLength       : len u16 LE | bytes
  raw              : bytes
zero padding to the report size
```

**Flags byte** (W `function Nb`):

```
bit 0     save    (1 = write to flash slot; 0 = apply to the active profile in RAM only)
bits 1..5 profile index (0..31)
bits 6..7 namespace (0 onboard, 1 linked)
```

So: `0x00` = live/RAM, `0x01` = save to onboard slot 0, `0x03` = slot 1, `0x05` = slot 2, `0x07` = slot 3.
E `report.rs:170-213` and O `wooting-support.md:139,241` captured the same thing.

Feature-command IDs and output-report types are **separate number spaces**. For example, feature `0x15` is KeysOff but output `0x15` is the per-key actuation write (O `wooting-support.md:130-134`).

Output writes get **no acknowledgement**. O `wooting-support.md:148-151` saw no reply. Verify with a read-back (§4.4).

### 1.8 Feature command IDs (ARM firmware), from W `class tY`

| cmd | hex | Wootility method | Param | Response body |
|---|---|---|---|---|
| 0 | 00 | ping | 0 | `[0xFF]` on success |
| 1 | 01 | getVersion | 0 | `[major, minor, patch, optional ASCII prerelease…]` |
| 2 | 02 | **resetToBootloader** | flags | none (device drops). **BLACKLIST** |
| 3 | 03 | getSerial (param 2), getMcuSerial (1), legacy (0), getFlashIDs (3) | 0..3 | param 2: `profiles.SerialNumber` protobuf |
| 8 | 08 | saveRgbProfile | Gt(slot) | commits RAM colors and RGB core to the slot |
| 11 | 0B | getCurrentKeyboardProfileIndex | 0 | `[index, namespace]` |
| 19 | 13 | getDeviceConfig | 0 | 7 raw bytes (§6.4) |
| 20 | 14 | getAnalogValues | 0 | `profiles.MatrixBytes` (8-bit travel per key) |
| 21 | 15 | **disableKeys** (KeysOff) | 0 | Keyboard stops typing. Avoid. |
| 22 | 16 | enableKeys (KeysOn) | 0 | |
| 23 | 17 | activateProfile | Gt(slot) | |
| 25 | 19 | **doSoftReset** | 0 | Device reboots and re-enumerates |
| 32 | 20 | wootDevResetAll / colorServiceReset | 0 | Hands the LEDs back to the firmware |
| 33 | 21 | wootDevInit / colorServiceInit | 0 | Enters direct-RGB mode |
| 35, 36 | 23, 24 | getRgbProfileColors part 1 / 2 | Gt(slot) | `profiles.PartialColors` (rows 0-2 / 3-5) |
| 38 | 26 | reloadProfile | 0 | Reloads the active profile from flash and discards RAM edits |
| 39 | 27 | getKeyboardProfile | Gt(slot) | `profiles.KeyboardProfile` |
| 40, 41 | 28, 29 | getGamepadMapping / getGamepadProfile | Gt(slot) | |
| 42 | 2A | saveKeyboardProfile | Gt(slot) | Defined in W but not used by its save path |
| 43 | 2B | **resetSettings** (factory reset; L calls it RESET_FLASH) | 0 | **BLACKLIST** |
| 44 | 2C | setRawScanning | 0/1 | diagnostic. Avoid. |
| 45, 46 | 2D, 2E | start / stop gamepad detection | 0 | |
| 48 | 30 | getKeymapLayer | Gt(slot, layer) | `profiles.KeyMapping` |
| 49 | 31 | getActuationProfile (per-key actuation) | Gt(slot) | `profiles.KeyMMPropertyProfile` |
| 50 | 32 | getRgbProfileCore | Gt(slot) | `profiles.RgbProfileCore` |
| 51 | 33 | getGlobalSettings | 0 | `profiles.GlobalSettings` |
| 52 | 34 | getAKCProfile | Gt(slot, startPoint) | `profiles.PartialAKCProfile` |
| 53 | 35 | saveAKCProfile | Gt(slot) | |
| 54 | 36 | getRapidTriggerProfile (legacy "partial" per-key RT) | Gt(slot) | `profiles.KeyPropertyProfile`. Empty on ARM (O `:87-90,242`) |
| 55 | 37 | getProfileMetadata | Gt(slot) | `profiles.ProfileMetadata` |
| 56 | 38 | isFlashChipConnected | 0 | `[1]` |
| 57 | 39 | getRgbLayer | packed | |
| 59 | 3B | getRGBBins | 0 | factory LED bins |
| 61 | 3D | **getFullRapidTriggerProfile** (per-key RT) | Gt(slot) | `profiles.RapidTriggerProfile` |
| 62 | 3E | getProfileCount | namespace | `[count]` (fw >= 2.10.0-alpha.1, W `e9`) |
| 63 | 3F | **deleteProfile** | Gt(slot) | **BLACKLIST** |
| 64 | 40 | **createProfile** | Gt(slot) | **BLACKLIST** for v1 |
| 66-71 | 42-47 | LED-bar (lightbar) profile commands | | 80HE only |
| 72 | 48 | getProfileSaveTracker | Gt(slot) | uint32 LE save counter (fw >= 2.11) |
| 73 | 49 | heartbeat | 0 | `profiles.Heartbeat{currentProfileIndex, isWootDevActive, isRgbSleeping, currentProfileNamespace}` (fw >= 2.11, W `AT`) |
| 74 | 4A | getKeyboardColors | 0 | uint16 LE RGB565 per matrix cell (rows x cols), currently *displayed* colors |
| 76 | 4C | eventSubscription | `id | (enable?0x100:0)` | |
| 79 | 4F | getDeviceCert (0) / getIntermediateCert (1) | | genuineness check (host side) |
| 80 | 50 | challenge result | 0 | |
| 81 | 51 | getHs | 0 | `[flag]` |
| 82 | 52 | getSensorConfigProfile (0) / default (1) | | `profiles.SensorConfigProfile` (switch types per key) |
| 83 | 53 | getSupportedSwitches | startIndex | `profiles.SupportedSwitches` (paged) |
| 85 | 55 | getLinkedProfileId | 0 | `[index, namespace]` |
| 86, 87 | 56, 57 | linked-app count / entries | | |

From S (WootDev; not present in W's ARM class but part of the official SDK for every `v2_interface` board, which includes the 60HE V2, S `wooting-usb.c:227-235`):
`30` WootDevSingleColor, `31` WootDevResetColor (S `wooting-usb.h:92-97`, `wooting-rgb-sdk.c:128-173`).

The retired legacy IDs (4-7, 9, 10, 12-18, 24, 26-28, 34, 37) come from L `libwootility_feature.py:4-63` and E `command.rs:28-88`. Do not send them.

### 1.9 Output report types (ARM), from W `class tY`

| type | hex | Body | Wootility method | Payload |
|---|---|---|---|---|
| 7 | 07 | raw (7 bytes) | saveDeviceConfig | DeviceConfig. **BLACKLIST** (layout and gamepad bytes; followed by soft reset) |
| 11 | 0B | raw | wootDevRawReport | 6x21 RGB565 LE (§3.1) |
| 12 | 0C | withLength | **saveSerial** | **BLACKLIST** |
| 14, 15 | 0E, 0F | profile | sendRgbProfileColors | `profiles.PartialColors` rows 0-2 / 3-5 |
| 17 | 11 | profile | sendKeyboardProfile | `profiles.KeyboardProfile` (§4, §5) |
| 18, 19 | 12, 13 | profile | gamepad mapping / gamepad profile | |
| 20 | 14 | profileExtended (param1 = layer) | saveKeymapLayer | `profiles.KeyMapping` |
| 21 | 15 | profile | setActuationProfile | `profiles.KeyMMPropertyProfile` (per-key actuation) |
| 22 | 16 | profile | sendRgbProfileCore | `profiles.RgbProfileCore` |
| 23 | 17 | profile | setGlobalSettings | `profiles.GlobalSettings` |
| 24 | 18 | profile | setAKCProfile | `profiles.PartialAKCProfile` (10 items per chunk) |
| 25 | 19 | profile | setRapidTriggerProfile (legacy partial) | `profiles.KeyPropertyProfile` |
| 26 | 1A | profile | saveProfileMetadata | `profiles.ProfileMetadata` (name <= 30 UTF-8 bytes, W `Xo=30`) |
| 27 | 1B | raw | setRgbLayer | RGB effect layer bitmaps (E/O saw 4x510-byte chunks) |
| 28 | 1C | withLength | **setRGBBin** | **BLACKLIST** (factory calibration) |
| 29 | 1D | profile | **setFullRapidTriggerProfile** | `profiles.RapidTriggerProfile` (§5) |
| 30-35 | 1E-23 | | LED-bar (80HE) | |
| 38 | 26 | withLength | executeChallenge | **BLACKLIST** |
| 39 | 27 | withLength | **setHs** (writes a flag plus magic `0x7A45465E`) | **BLACKLIST** |
| 40 | 28 | profile | setSensorConfigProfile | switch type and spacer per key. **BLACKLIST** for v1 |
| 43 | 2B | withLength | wiggleOverride | unknown. **BLACKLIST** |
| 44 | 2C | profileExtended | linked-app profiles | |

E calls some of these differently. E's "0x15 PerKey" = actuation profile, "0x1D RtKeys" = full RT profile, "0x28 default profile" = SensorConfigProfile write, "0x1B blob" = RGB layer (E `protocol.md:90-106`). The W names above are authoritative.

### 1.10 Protobuf encoding notes

The schemas are generated by protobuf-ts (proto3), package `profiles`. Wootility's generated writer **always emits non-`optional` scalar fields, even when the value is 0 or false**. For example, the KBase writer unconditionally writes fields 1, 2, 3 and 5 (W `KeyboardProfile.KBase.internalBinaryWrite`). E `protocol.md:87-88` reports that firmware rejects messages that leave them out. Mirror that: write every non-optional scalar in field-number order, and write optional ones only when set. Reads must tolerate both packed and unpacked repeated fields (O `wooting-support.md:254-255`).

---

## 2. Key matrix and per-key indexing

* The matrix is addressed as a **6 x 21** grid (rows 0-5, cols 0-20). This is the RGB/WootDev buffer shape (S `wooting-usb.h:83-86`; W `mi=21`).
* The 60HE v2 uses **rows 1-5 and columns 0-13**. Row 0 is unused. W device `tT.matrix = {rows:{start:1,count:5}, columns:14}`. S `set_meta_wooting_60hev2` uses `max_columns=14` (`wooting-usb.c:227-235`).
* **Matrix ID** (the per-key index in the settings protobufs, the advanced keys and the WootDev single-color command) is one byte: `id = (row & 7) << 5 | (col & 31)`. `0xFF` (row 7, col 31) means "none". W `l2()` / `Ba()`; S `KeyboardMatrixID` bitfield (`wooting-usb.h:74-77`).
* Supported physical layouts: ANSI (0), ISO (1), ANSI split-spacebar (3), ISO split-spacebar (4). JIS is not offered (W `tT.supportedLayoutTypes`). The keyboard reports its layout in DeviceConfig byte 5 (§6.4).

### 2.1 Matrix → key (main-layer default), 60HE v2

Built from the W default keymap `xE` (60HE family main layer, rows x cols) and the W layout geometry `qS = {...S5, ...Sg}` (which keys exist per layout; `kn(dev, layout, 'analogkeys')`). Cross-checked against E's on-device keymap dump (E `protocol.md:159-167`).

| row\col | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **1** | Esc | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 0 | - _ | = + | Backspace |
| **2** | Tab | Q | W | E | R | T | Y | U | I | O | P | [ { | ] } | \ \| **(ANSI only)** |
| **3** | Caps | A | S | D | F | G | H | J | K | L | ; : | ' " | ISO # ~ **(ISO only)** | Enter |
| **4** | LShift | ISO \ \| **(ISO only)** | Z | X | C | V | B | N | M | , | . | / | (none) | RShift |
| **5** | LCtrl | LWin | LAlt | (none) | Space* | Space* | Space* | Space* | Space* | (none) | RAlt | Menu | RCtrl | Fn |

Matrix IDs: row 1 starts at `0x20` (Esc), row 2 at `0x40` (Tab), row 3 at `0x60` (Caps, so A = `0x61`), row 4 at `0x80`, row 5 at `0xA0` (Fn = `0xAD`).

Notes:
* ANSI: `(2,13)` is `\`. `(3,12)` and `(4,1)` do not exist. The Enter key is `(3,13)`.
* ISO: `(2,13)` does not exist. `(3,12)` is the ISO `#~` key (default code "Backslash"). `(4,1)` is ISO `\|` (W code "ExtraIso"). Enter is `(3,13)` (2u tall). LShift `(4,0)` is 1.25u.
* *Spacebar, ANSI/ISO (W `Sg`):* the single analog switch is **`(5,6)`**. `(5,4)`, `(5,5)`, `(5,7)` and `(5,8)` are **LED-only** cells (5 spacebar LEDs). The keymap fills `(5,4)..(5,8)` with Space (W `xC`). Settings and per-key actuation apply to `(5,6)`.
* *Split spacebar (layouts 3 and 4):* `(5,4)` = left space (2.75u, analog), `(5,6)` = middle 1.25u key (default **Fn**, W `xC`: `H3(layout) → V[5][6]=FnKey`), `(5,8)` = right space (2.25u). `(5,5)` and `(5,7)` do not exist.
* Fn layer defaults for the 60HE v2 are W `LE` (arrows on `(2,2)`/`(3,1..3)`, F-keys on row 1, and so on). They are not needed for omni v1.

### 2.2 How per-key settings are indexed

* Per-key actuation (`KeyMMPropertyProfile`) uses either a **full** matrix (`rows[r].values[c]`, 6 rows x up-to-21 columns, 0 = no override) or a **partial** list (`entries[] = value<<16 | matrixId`, 14-bit form). W `GZ`, `WZ`.
* Per-key rapid trigger (`RapidTriggerProfile`) uses groups of `keys[] = matrixId` (§5).
* RGB colors use `rows[r].values[c]` (6 x 21).

---

## 3. Per-key RGB

### 3.1 Live / direct colors ("WootDev", volatile, nothing goes to flash)

1. `cmd 33` WootDevInit, param 0. W `wootDevInit`; S sends it when it opens a device (`wooting-usb.c:489`).
2. Full frame: **output report**, type `0x0B`, body raw:
   `D1 DA 0B` + 6x21 little-endian `uint16` RGB565, row-major (`[r*21+c]`), 252 bytes. That makes 255 bytes, so use **report ID 4** (510-byte payload) and zero-pad.
   W `colorServiceReport`: `DataView.setUint16(2*(r*21+c), D1(color), true)`, then `wootDevRawReport` → `Lt(magic, 11, F6(bytes))`.
   (S `wooting_usb_send_buffer_v3`, `wooting-usb.c:656-682`, sends report ID **5** with a 2046-byte buffer. That works on the 60HE V2 according to PR75's color/reset test, but it is not the smallest fit. Follow W.)
3. Single key: `cmd 30` with `buf[4]=blue, buf[5]=green, buf[6]=red, buf[7]=matrixId` (S `wooting-rgb-sdk.c:135-138` + `wooting-usb.c:693-697`; E `packet.rs:318-326`). **[INFERRED for 60HE v2]**: this is the SDK path for all `v2_interface` boards, but W never uses it on ARM.
4. Reset one key: `cmd 31` with `buf[7]=matrixId` (S `wooting-rgb-sdk.c:170-173`).
5. **Restore the keyboard's own effects:** `cmd 32` WootDevResetAll. PR75 verified on a 60HE V2 that this "restored its original lighting profile". S `wooting_rgb_reset_rgb` (`wooting-rgb-sdk.c:106-108`) does the same.

Color encoding is **RGB565**: `(r&0xF8)<<8 | (g&0xFC)<<3 | (b&0xF8)>>3` (S `wooting-rgb-sdk.c:83-91`; W `D1()`). Decode as `r=(v&0xF800)>>8`, `g=(v&0x07E0)>>3`, `b=(v&0x1F)<<3`, then add `+7/+3/+7` when non-zero (W `B1`/`cf`).
Wootility does **not** gamma-correct on ARM. The `gammaFilter` in S is applied only to the legacy V1 buffers (`wooting-rgb-sdk.c:308-346`).

Read the currently displayed colors with `cmd 74` (W `getKeyboardColors`: a uint16 LE RGB565 array of `rows*cols`). Heartbeat `cmd 73` reports `isWootDevActive`.
**[UNKNOWN]** whether the firmware drops out of WootDev mode on its own after a timeout. Always send `cmd 32` on exit and on crash/SIGTERM.

### 3.2 Saved profile colors (persist)

Wootility's save sequence (W `saveRgbProfile(slot, profile)` → `sendRgbProfile` → device `saveRgbProfile`):

1. Optionally send RgbProfileCore: output `0x16`, flags `0x00` (RAM).
2. Send colors: output `0x0E` with `PartialColors{rows: rows[0..2]}` and output `0x0F` with `rows[3..5]`, **flags `0x00`** (W `sendRgbProfileColors(rows,false)`). `ColorRow.values` = packed `uint32` RGB565 with 21 columns per row. Wootility's color arrays are 6x21 (W `eD`), and firmware reads come back as 3 rows x 21 (O `wooting-support.md:340-345`).
3. `cmd 8` SaveRgbProfile with `Gt(slot)` copies the RAM colors (and core) into that flash slot.

Sending only step 3 copies whatever is currently in RAM into the slot (E `protocol.md:112-114`). Writing 0x0E/0x0F with flags 0 changes the live profile colors immediately (W keeps RAM in sync on every edit).

Read back with `cmd 35` + `cmd 36` (`Gt(slot)`). E reports that reads return the flash copy (`protocol.md:108-110`). **[60HE+ only]**

Schemas (W):
* `PartialColors { repeated ColorRow rows = 1; }`
* `ColorRow { repeated uint32 values = 1 [packed]; }`
* `RgbProfileCore { Base base = 1; oneof effect { EffectLegacy effectLegacy = 3; RgbEffects effectsV2 = 4; } }`
* `RgbProfileCore.Base { uint32 Brightness = 1; uint32 CapsLockActiveColor = 2; uint32 FnLockActiveColor = 3; optional uint32 ScrollLockActiveColor = 4; optional uint32 NumLockActiveColor = 5; }` (the indicator colors are RGB565)
* `RgbEffects { repeated RgbEffectLayer layers = 1; }`. Layer oneof: 1 wave, 2 scan, 3 breath, 4 cycle, 5 key_ripple, 6 key_trail, 7 analog_touch, 8 analog_jelly, 9 analog_bomb, 10 analog_mixing, 11 rapid_trigger, 12 snake, 13 heatmap, 14 noise, 15 spiral. The 60HE v2 allows 4 layers (W `tT.rgbEffectLayersCount=4`).
* For a "static per-key colors" look, O observed that ARM "Static" = base + an `analog_bomb{1:127}` layer (O `wooting-support.md:302-312`). E observed "no layers" (E `protocol.md:99`). **[UNRESOLVED]** Safest: read the core, keep its effect list as-is, and only change colors.

---

## 4. Actuation point

### 4.1 Value encoding ("ProtoAnalogValue")

From W `Lg`, `Ji`, `Jt`, `r9`:

* Internal value `v` is in `0..16383` (14 bits). `v = round(mm / fullScale_mm * 16383)`. JS `Math.round` after `toFixed(4)`, so halves round up.
* **Wire format, firmware >= 2.13.0-alpha.0 (14-bit, "Percent_14bit"):** `wire = 0x4000 | v`.
* Firmware < 2.13 ("Percent_8bit"): `wire = round(v*255/16383)`. This does not apply to a current 60HE v2. PR75's unit ran 2.14.1.
* Decoding (W `Ji`): `wire <= 255` → 8-bit: `v = round(wire*16383/255)`. `(wire>>14)==1` → `v = wire & 0x3FFF`.
* `fullScale_mm` is **the key's switch range max**. Wootility converts with the per-key switch range (`Jt(..., {range})`, W `L4`, `dI`, `hm`). For the 60HE v2's stock **Wooting Lekker Tikken** (W `tT.defaultSwitchType = WootingLekkerTikken = 1`), the switch table gives `min 0.1 mm, max 4.0 mm, step 0.05 mm` (W `QD[type 1] = {stepsPerMM:20,min:2,max:80}` → `CC()`). The generic Hall-effect fallback is also 0..4.0 mm (W `pT`).
  **So for a stock 60HE v2: `wire = 0x4000 | round(mm/4.0*16383)`.**
  **[INFERRED]** If the user changed switch types with the Switch Selector (`cmd 82` SensorConfigProfile), the full scale can differ (for example 3.3 mm for type 2, Gateron Jade). Read `cmd 82`, and refuse or warn if any key is not type 1.
* Verified captures (60HE+): 1.0 mm = `0x5000`, 1.6 mm = `0x5999`, 2.0 mm = `0x6000`, 4.0 mm = `0x7FFF`, 2.5 mm = `0x67FF`, 0.3 mm = `0x44CD` (E `captures/README.md:30-41,95-97`; O `wooting-support.md:138`).

| mm | wire | varint bytes |
|---|---|---|
| 0.1 | 0x419A | 9A 83 01 |
| 0.2 | 0x4333 | B3 86 01 |
| 0.4 | 0x4666 | E6 8C 01 |
| 1.0 | 0x5000 | 80 A0 01 |
| 1.5 | 0x5800 | 80 B0 01 |
| 2.0 | 0x6000 | 80 C0 01 |
| 4.0 | 0x7FFF | FF FF 01 |

**UI range for the 60HE v2: 0.1-4.0 mm in 0.05 mm steps** (switch table above. When no switch info is available, Wootility falls back to 0.1-4.0 mm in 0.1 steps, W `YD`/`gT(pT)`).

### 4.2 Global (per-profile) actuation: `KeyboardProfile.base.ActuationPoint`

Schema (W, with exact field names):

```proto
message KeyboardProfile { KBase base = 1; }
message KeyboardProfile.KBase {
  uint32 ActuationPoint = 1;                          // ProtoAnalogValue
  bool   isDigitalOn = 2;                             // "enable keyboard keys" (gamepad); keep as read, normally true
  bool   enableRapidTrigger = 3;                      // §5
  optional uint32 rapidTriggerSensitivity = 4;        // ProtoAnalogValue, press ("primary")
  bool   tachyonModeEnabled = 5;
  optional DKSTickRate tickRate = 6;                  // 0 NoTick, 1 Tick60, 2 Tick30
  optional bool   rapidTriggerStrictActuationRange = 7;   // true = normal RT, false = CONTINUOUS RT
  optional uint32 rapidTriggerSecondarySensitivity = 8;   // release sensitivity (split mode); absent/0 = same as press
  optional TachyonMode tachyonMode = 9;               // 0 Disabled, 1 Enabled (60HE v2: no special modes)
}
```

* Read: `cmd 39` (getKeyboardProfile) with `Gt(slot)`.
* Write live: output `0x11`, flags `0x00`. Write to flash: flags `(slot<<1)|1` (W `sendKeyboardProfile(p, save, profileId)`).
* Always send the **whole** KBase. Do read-modify-write and preserve fields 2, 5, 6 and 9.

Worked example (my encoding, checked against the capture shapes in E `captures/README.md:32`): actuation 1.5 mm, RT on, press 0.3 mm, tick 2, non-continuous, live (RAM):

```
report ID 1 (33 bytes total):
01 D1 DA 11 00 16 00
   0A 14  08 80 B0 01  10 01  18 01  20 CD 89 01  28 00  30 02  38 01  48 00
   (zero pad to 33)
```

Use the same bytes with flags `01` to persist to slot 0.

### 4.3 Per-key actuation overrides: output `0x15` / `cmd 49`

```proto
message KeyMMPropertyProfile {
  oneof form {
    MatrixBytes                          full           = 1;  // 8-bit fw only
    PartialKeyPropertiesProfile          partial        = 2;  // 8-bit fw only: entries = value8<<8 | matrixId
    MatrixMMEncoded                      fullEncoded    = 3;  // rows[r].values[c] = wire14 (0 = no override)
    PartialKeyMMEncodedPropertiesProfile partialEncoded = 4;  // entries = wire14<<16 | matrixId
  }
}
message MatrixMMEncoded { repeated MatrixMMRow rows = 1; }
message MatrixMMRow     { repeated uint32 values = 1 [packed]; }
message PartialKeyMMEncodedPropertiesProfile { repeated uint32 entries = 1; }  // unpacked on write
```

* Wootility uses `partialEncoded` when fewer than 50 keys have overrides and `fullEncoded` otherwise (W `GZ`, `cy=50`). Keys without an override follow `ActuationPoint`. An empty message or empty body means "no overrides".
* Write: output `0x15` with flags. Read: `cmd 49` `Gt(slot)`. The decoder must accept all four forms (W `WZ`).
* Example, key A `(3,1)`=0x61 at 2.5 mm, live: `01 D1 DA 15 00 08 00 22 06 08 E1 80 FC BF 06`. This equals E's capture `captures/README.md:95-97`.
  O saved two keys at 0.30 mm with flags `01`: `d1 da 15 01 0e 00 22 0c 08 a0 80 b4 a6 04 08 ad 81 b4 a6 04` (O `wooting-support.md:138`).
* Clear all overrides: `01 D1 DA 15 <flags> 00 00` (empty message, O `wooting-support.md:139`).
* Requires fw >= 2.2.0 (W `Jo`). Wootility caps nothing here, but the matrix is bounded.

### 4.4 Read-back and persistence

* A flags-`(slot<<1)|1` write updates flash. O saved a base actuation that way, sent `cmd 38` ReloadProfile, read it back, unplugged and replugged, and read it back again: it persisted (O `wooting-support.md:185-200`). **[60HE+ only]**
* Reads with an explicit slot return the **flash** copy, so a RAM-only (flags 0) write does not show up in a read-back (E `protocol.md:108-110`). **[60HE+ only]**
* Recommended omni sequence to apply and persist on the active slot `s`: write with flags 0 (instant effect), write the same body with flags `(s<<1)|1`, read back `Gt(s)` and compare. Optionally send `cmd 38` to make sure RAM matches flash.

---

## 5. Rapid Trigger

### 5.1 Global (profile-level) RT, inside KBase (§4.2)

* `enableRapidTrigger` (3): RT on or off for keys that have no per-key group.
* `rapidTriggerSensitivity` (4): press ("activate") sensitivity, ProtoAnalogValue in mm.
* `rapidTriggerSecondarySensitivity` (8): release ("deactivate") sensitivity. Present only in split mode. Absent or 0 means symmetric (W `getKeyboardProfile`: `secondary!==0 ? Ji(...) : null`).
* `rapidTriggerStrictActuationRange` (7): `true` = RT only works past the actuation point (normal). `false` = **continuous RT** (E `captures/README.md:106-107`; W `VD`: `strictActuationRange = !continuousMode`).
* **Ranges** for a Hall-effect board with Lekker Tikken (W `Bu` applied to the switch range 0.1-4.0/0.05): press and release **0.1-2.35 mm in 0.05 mm steps**. Defaults: 0.4 mm press in single mode; in split mode 0.2 mm press and 0.55 mm release.
  The generic fallback without switch info is 0.15-2.35 mm (W `Pu`).

### 5.2 Per-key RT: `RapidTriggerProfile` (output `0x1D`, read `cmd 61`)

This is the "full per-key RT" path. Wootility uses it when fw >= 2.10.0-alpha.0 on an ARM board (W `Hg`), which covers every 60HE v2.

```proto
message RapidTriggerProfile      { repeated RapidTriggerConfigGroup groups = 1; }
message RapidTriggerConfigGroup  {
  repeated uint32 keys = 1;            // matrix IDs (Wootility writes unpacked; firmware returns packed)
  oneof value {
    RapidTriggerConfig config  = 2;    // RT ON with these per-key settings
    bool               enabled = 3;    // true = RT ON using the KBase sensitivities; false = RT OFF
  }
}
message RapidTriggerConfig {
  uint32 primarySensitivity = 1;            // press, ProtoAnalogValue
  optional uint32 secondarySensitivity = 2; // release (split)
  optional bool   continuousMode = 3;       // true = continuous; omit for normal (W HD)
}
```

Semantics are read from W `SY.getRapidTriggerProfile` / `setRapidTriggerProfile`, and how the firmware applies them is **[INFERRED]** from Wootility's model:
* A key that is in no group follows KBase (`enableRapidTrigger` plus the KBase sensitivities).
* A key in an `enabled:true` group has RT on with the KBase sensitivities. A key in an `enabled:false` group has RT off. A key in a `config` group has RT on with that config.
* Wootility's writer moves the largest `config` group into KBase (sensitivities plus `enableRapidTrigger=true`). The remaining distinct settings become groups of **at most 10 keys** each, with **at most 30 groups** (10 on fw < 2.13.0-beta.7) (W `mT`/`FT`). An empty profile is sent as an empty body.
* Write live: output `0x1D` flags 0. Persist: flags `(slot<<1)|1`. Wootility always sends `0x1D` before `0x11` (E `captures/README.md:28,103-104`).
* Live round trip on a 60HE+ (O `wooting-support.md:257-282`): read `d1 da 3d 88 0f 00 0a 0d 0a 03 20 ad 01 12 06 08 b3 86 01 18 01` means one group with keys {0x20 Esc, 0xAD Fn}, 0.2 mm, continuous. A write with flags `01` read back byte-identical.

Example (my encoding): W, A, S, D (`0x42,0x61,0x62,0x63`) at press 0.15 / release 0.30, normal mode. Esc RT off. Live. The frame is 33 bytes, which is more than 32, so it needs **report ID 2** (63 bytes total):

```
02 D1 DA 1D 00 1A 00
   0A 12  08 42 08 61 08 62 08 63  12 08 08 E6 84 01 10 CD 89 01
   0A 04  08 20 18 00
   (zero pad)
```

The legacy partial path (output `0x19` / `cmd 54`, `KeyPropertyProfile` with values 2=on, 1=off, 0=follow base, fw 2.5.3-2.10) is **not used on ARM** (O `wooting-support.md:87-90`). Ignore it.

Tachyon mode (KBase 5/9) caps brightness (E `captures/README.md:39`). Leave it untouched unless the user asks.

---

## 6. Profiles, device info, persistence

### 6.1 Slots

* Onboard namespace: **4 slots**, indices 0-3 (W `Ob=4`, `a0=4`). The linked namespace (app-linked) has 10 slots (W `a2=10`). Ignore it in v1.
* Count: `cmd 62` with param = namespace (`0`) → `[count]`. fw >= 2.10.0-alpha.1, otherwise assume 4 (W `getProfileCount`, `e9`). E's alternative is to probe `cmd 55` until the body is empty (`protocol.md:146-151`).
* Name, color and icon: `cmd 55` `Gt(slot)` → `ProfileMetadata{ string name=1; RGB24 color=2 {r=1,g=2,b=3}; optional string icon=3 }`.

### 6.2 Read the current profile

* `cmd 11` → body `[index, namespace]` (W `getCurrentKeyboardProfileIndex`).
* Or `cmd 73` heartbeat → `Heartbeat{currentProfileIndex=1, isWootDevActive=2, isRgbSleeping=3, currentProfileNamespace=4}`, fw >= 2.11 (W `heartbeat`, `AT`). This is cheap enough to poll every second or so. Wootility polls 73/74 continuously (E `captures/README.md:6-8`).
* Or subscribe to events with `cmd 76` and watch for `0xEE` input reports carrying `Event.profileChanged` (W `eventSubscription`, `newInputReport`).

### 6.3 Select the active profile

`cmd 23` ActivateProfile with `Gt(slot)` (= `buf[4]=slot, buf[5]=0`). W `activateProfile`.
After activating, Wootility re-reads the slot's settings and pushes its own copy into RAM (E `captures/README.md:10-25`).
E and O use `cmd 33` → `cmd 23` → wait 100 ms → `cmd 38` → verify with `cmd 11` (E `protocol.md:150-151`; O `wooting-support.md:398-401`).
**[INFERRED]** that `cmd 23` alone loads flash into RAM. Use the E/O sequence, minus `cmd 33`, which would put the LEDs into WootDev mode.

### 6.4 Save and persist semantics (summary)

| What | Live | Persist |
|---|---|---|
| KBase (actuation, global RT) | output `0x11` flags 0 | output `0x11` flags `(slot<<1)|1` |
| Per-key actuation | `0x15` flags 0 | `0x15` flags `(slot<<1)|1` |
| Per-key RT | `0x1D` flags 0 | `0x1D` flags `(slot<<1)|1` |
| Profile colors / RGB core | `0x0E`/`0x0F`/`0x16` flags 0 | then **`cmd 8` (slot)**. Do not set the save flag on these: their flags carry no slot in Wootility. |
| Direct RGB | raw `0x0B` / `cmd 30` | never persists. `cmd 32` restores. |
| Discard RAM edits | `cmd 38` ReloadProfile | |

Nothing is persisted automatically. RAM edits survive until a profile switch, `cmd 38`, or a power cycle. **[INFERRED]** for the power-cycle case.
`cmd 72` returns a per-slot save counter that changes on each save (fw >= 2.11), which helps detect edits made by Wootility running at the same time.

### 6.5 DeviceConfig (`cmd 19`, 7 bytes, read-only for omni)

From W `class Q2`:
* `b0` bit 4 = NKRO disabled (6KRO "compatibility" mode). `b0` bit 3 = force USB full speed.
* `b4` = calibration status (0 = none).
* `b5` = **layout** (0 ANSI, 1 ISO, 2 JIS, 3 ANSI split, 4 ISO split). Wootility falls back to ISO if the value is invalid.
* `b6` bits 1-2 = gamepad mode (0 Xbox, 1 HID, 2 none). `b6` bits 3-4 = gamepad poll rate (0 = 250 Hz, 1 = 1 kHz, 2 = 8 kHz).

In a raw hidraw read the layout is at `r[7+5] = r[12]`.
Do **not** copy S `wooting_usb_get_layout` (`wooting-usb.c:275-296`). It reads `buff[10]`, the right offset for the old single-byte-length V2 header but wrong for multi-report devices. **[INFERRED]** SDK bug.

---

## 7. Firmware version and device info

* `cmd 1` → `[major, minor, patch]` plus an optional ASCII prerelease suffix (W `getVersion`). Example: `01 D1 DA 01 88 03 00 02 0E 01` = 2.14.1.
* `cmd 3` param 2 → `SerialNumber{SupplierNumber=1, Year=2, WeekNumber=3, ProductNumber=4, RevisionNumber=5, ProductId=6, Stage=7, optional Variant=9, UniquePCBDesign=10, MinorRevision=11}` (W `getSerial`). Param 1 = MCU serial. Param 3 = flash chip IDs `[manufacturer, device, serial…]`.
* `cmd 82` / `cmd 83`: switch configuration and supported switches (to validate the actuation scale, §4.1).
* `cmd 51` → `GlobalSettings{RgbSleepTimeout rgbSleepTimeout=1 {dimMins, offMins, optional turnOff}; optional invertNumLock=2; optional debounceFilter=3; optional deviceName=4}`.

Feature gates in Wootility that omni should mirror. Refuse writes below these firmware versions:
* 14-bit values: fw >= 2.13.0-alpha.0.
* Full per-key RT: fw >= 2.10.0-alpha.0.
* Per-key actuation: fw >= 2.2.0.
* Heartbeat: fw >= 2.11.

---

## 8. Dangerous operations: use a whitelist, not a blacklist

Recommended omni whitelist. Feature cmds: `0,1,3,8,11,19,20,23,32,33,30,31,35,36,38,39,49,50,51,55,61,62,72,73,74,76,82,83`. Output types: `0x0B,0x0E,0x0F,0x11,0x15,0x16,0x1D`.

Never send:

| Kind | ID | Why |
|---|---|---|
| feature | **2** ResetToBootloader | Enters firmware-update mode (param bits: 1 highSpeed, 2 forceNormalMode, 4 disableRgb; W `XQ`) |
| feature | **43** ResetSettings | Factory reset: wipes every profile (W `loadDefaultSettings`) |
| feature | **63** DeleteProfile, **64** CreateProfile | Changes profile slots |
| feature | **25** SoftReset | Reboots and re-enumerates the device (harmless but disruptive) |
| feature | **21** KeysOff | The keyboard stops typing until `cmd 22` or a replug |
| feature | 44 SetRawScanning, 45/46 gamepad detection, 79/80/81 cert/challenge/hs | Diagnostic or authentication |
| feature | retired legacy IDs (4-7, 9, 10, 12-18, 24, 26-28, 34, 37) | Undefined on ARM |
| output | **0x07** DeviceConfig | Layout, NKRO and gamepad bytes. A malformed write "silently corrupts the config" (E `protocol.md:187-189`) |
| output | **0x0C** saveSerial, **0x1C** setRGBBin, **0x26** challenge, **0x27** setHs (magic `0x7A45465E`), **0x28** SensorConfigProfile, **0x2B** wiggleOverride | Factory, authentication or calibration data |
| output | 0x14 keymap, 0x18 AKC, 0x1B RGB layers, 0x12/0x13 gamepad, 0x2C linked apps | Out of scope. They can make keys unusable. |
| device | anything with usage page `0xFFFF` / PID `0x134F` / magic `DF DB` | Bootloader: cmd 2 start upload, 3 write block, 4 CRC, 5 reboot, **6 unlock and erase (param `0xFFAAFFBB`)**, **7 resetFlash** (W `class YQ`) |

The 60HE v2 is flagged `requiresVerification:true` in W. That is a **host-side** genuineness check: Wootility fetches the device certificate (cmd 79) and sends an RSA challenge (output 0x26 / cmd 80). Nothing I found shows that firmware gates config commands on it. PR75 ran the RGB/WootDev commands on a 60HE V2 without it. **[UNVERIFIED]** for settings writes on the 60HE v2.

---

## 9. Gaps, confidence, recommended path

**Confidence overall: medium-high.** The wire framing, command and report IDs, protobuf schemas, value encodings and version gates come straight from Wooting's own configurator code (W). They agree byte-for-byte with two independent capture sets on a 60HE+ (E, O). The 60HE v2-specific facts are its PID, its 0xFF55 interface, 33-byte responses, fw 2.14.1, and WootDev RGB plus reset. Those come from W's device table and PR75's real 60HE V2. W treats the 60HE v2 with the same `tY` ARM class as the 60HE+ (W `compatibleDevices: [60HE_V2, 60HE_ARM, 60HE_PLUS]`).

Open items, to check on the real board with a read-only probe first:
1. **No settings write has been observed on a 60HE v2.** Every actuation and RT capture is from a 60HE+. Verify with a read (`cmd 39`, `cmd 49`, `cmd 61`), then a reversible live write (flags 0) plus `cmd 38` to undo.
2. The actuation full scale when non-stock switch types are configured (§4.1). Check `cmd 82`.
3. Whether reads with `0xFFFF` or the active slot return RAM or flash (E says flash for explicit slots).
4. Whether the PID low nibble always equals the gamepad mode (§1.1).
5. Whether `cmd 23` alone reloads RAM (§6.3), and whether WootDev times out on its own (§3.1).
6. ARM "static" RGB core contents (§3.2). Preserve the effect list rather than synthesizing one.
7. Interface number and hidraw ordering on the v2. Match on the usage page.

**Recommended path:** treat W as the specification. Model omni's encoder on W's `tY` class (method by method, as tabulated above) and its `SY`/`ZY` controllers (`setKeyboardProfile`, `setRapidTriggerProfile`, `GZ`). Use E (`crates/wooting-proto`, Rust, Linux hidraw, MIT/compatible licence to be checked before copying) and O as independent cross-checks and as sources of known-good byte strings for unit tests. Generate omni's protobuf messages from the schemas in §3-§7 with the exact field numbers. The Wootility field names can be recovered for any other message with `python3 -I omni-research/tools/schemas.py omni-research/wootility-web/index.deob2.js profiles.<Name>`.

Reproduce the decoding (no code from the bundle is executed):
```
python3 -I omni-research/tools/deob.py  wootility-web/index-BaoALhfv.js wootility-web/index.deob.js
python3 -I omni-research/tools/fold.py  wootility-web/index.deob.js     wootility-web/index.deob2.js
python3 -I omni-research/tools/defn.py  wootility-web/index.deob2.js 1500 <symbol>   # print a definition
```
