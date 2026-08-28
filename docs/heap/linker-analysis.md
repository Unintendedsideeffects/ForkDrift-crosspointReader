# ESP32-C3 linker and memory-layout analysis

## Scope, evidence, and confidence

This is a read-only analysis of the supplied `default` firmware artifacts and the checked-in configuration/source. No build was run. The supplied `firmware.elf` and `firmware.map` were available and inspected at the start of the analysis, but disappeared concurrently before this report was written; they were not recreated. Addresses, sizes, and map-line references below are therefore from the captured artifact snapshot.

Confidence labels used below:

- **Verified**: directly present in the inspected ELF/map, `sdkconfig`, linker script, or source.
- **Inferred**: follows from verified layout/runtime facts, but is not itself encoded in the static map.
- **Unknown until measured**: depends on runtime workload, allocator state, or a rebuilt vendor library.

The central conclusion is that the apparent missing ~20 KiB is not an unaccounted linker reservation. The raw heap regions reconcile exactly. The difference between those regions and `ESP.getHeapSize()` is allocator metadata and light-poisoning overhead, and it varies with the number and topology of live heap blocks.

## 1. Exact RAM reconciliation

### 1.1 Linker-owned DRAM

The PlatformIO summary calls the nominal application RAM capacity 327,680 bytes (`0x50000`). The inspected ELF/map shows:

| Consumer | Address/range | Bytes | Evidence | Confidence |
|---|---:|---:|---|---|
| IRAM's shared DRAM alias | `0x3fc80000..0x3fc92a00` | 76,288 | `.dram0.dummy = 0x12a00`; map `62061`; `.iram0.text` begins at `0x40380000`, map `60362` | Verified |
| Initialized DRAM | `0x3fc92a00..0x3fc975ac` | 19,372 | `.dram0.data = 0x4bac`; map `62065` | Verified |
| Data-to-BSS alignment | `0x3fc975ac..0x3fc975b0` | 4 | section addresses | Verified |
| Zero-initialized DRAM | `0x3fc975b0..0x3fcb1ef8` | 108,872 | `.dram0.bss = 0x1a948`; map `63486` | Verified |
| Heap-start alignment | `0x3fcb1ef8..0x3fcb1f00` | 8 | `.dram0.heap_start` and `_heap_start`; map `132004-132008` | Verified |

Thus the application image occupies:

```text
76,288 + 19,372 + 4 + 108,872 + 8 = 204,544 bytes
327,680 - 204,544 = 123,136 bytes
```

That `123,136` value is what remains **inside the nominal 320 KiB accounting window**, but it is not the complete heap-capable memory on this chip.

### 1.2 Why the linker region is not the PlatformIO 320 KiB number

The linker script defines the shared IRAM/DRAM region with length `0x4e710` (321,296 bytes), ending at `0x3fcce710`/`0x403ce710` (`memory.ld:38,49-52`; map `59473-59480`). That is deliberately different from PlatformIO's generic `0x50000` percentage denominator. The ESP32-C3's actual DRAM address space continues through `0x3fce0000`; the SoC header defines the physical range `0x3fc80000..0x3fce0000`, or 393,216 bytes (`soc.h:225-240`).

The heap-region table stored in flash as `soc_memory_regions` confirms how IDF describes that physical memory (`firmware.map:115449-115451`, symbol at `0x3c577a9c`):

| Region | Start | Size | Type |
|---|---:|---:|---|
| 0 | `0x3fc80000` | `0x20000` | DRAM / IRAM alias |
| 1 | `0x3fca0000` | `0x20000` | DRAM / IRAM alias |
| 2 | `0x3fcc0000` | `0x1c710` | DRAM / IRAM alias |
| 3 | `0x3fcdc710` | `0x38f0` | startup stack / DRAM |
| 4 | `0x50000000` | `0x2000` | RTC RAM |

IDF subtracts these reserved ranges before registering heap regions (`heap_memory_layout.h:79-89`):

- IRAM code alias: `0x3fc80000..0x3fc92a00`
- initialized data/BSS/alignment: `0x3fc92a00..0x3fcb1f00`
- RTC-retained data: `0x50000000..0x500015c0`
- final RTC retention descriptor: `0x50001ff0..0x50002000`

The corresponding descriptors are visible in the map at `120075-120081`.

### 1.3 Raw heap regions: exact arithmetic

After startup, the heap-capable regions are:

```text
normal DRAM:           0x3fcb1f00..0x3fcdc710 = 0x2a810 = 174,096
reclaimed startup RAM: 0x3fcdc710..0x3fce0000 = 0x038f0 =  14,576
remaining RTC RAM:     0x500015c0..0x50001ff0 = 0x00a30 =   2,608
                                                           -------
raw registered space                                         191,280
```

The startup-stack range is not permanently lost: IDF explicitly enables that region with `heap_caps_enable_nonos_stack_heaps()` after non-OS startup (`esp_heap_caps_init.h:32-40`; map symbols `89917-89919`). RTC RAM is eligible because `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y` (`sdkconfig:1023`).

An equivalent reconciliation from the nominal PlatformIO number is:

```text
327,680 nominal RAM
- 76,288 IRAM mirror reservation
- 19,372 .data
-108,872 .bss
-     12 alignment
+ 65,536 upper physical DRAM outside the nominal 320 KiB window
+  2,608 remaining RTC RAM
=191,280 raw heap-region bytes
```

The often-derived `199,436` value (`327,680 - .data - .bss`) is misleading because it fails to subtract the 76,288-byte IRAM alias and alignment, and fails to add the upper 64 KiB and eligible RTC remainder.

### 1.4 The remaining 11.8-16.0 KiB is allocator overhead

Arduino's `ESP.getHeapSize()` does not sum raw region descriptors. `Esp.cpp:133-138` calls `heap_caps_get_info(MALLOC_CAP_INTERNAL)` and returns `total_free_bytes + total_allocated_bytes`. That deliberately omits allocator control structures, block headers, alignment gaps, and poisoning canaries.

The supplied runtime observations reconcile as follows:

| State | Raw regions | `ESP.getHeapSize()` | Hidden allocator bytes | Allocated blocks | Free blocks |
|---|---:|---:|---:|---:|---:|
| Reading | 191,280 | 179,464 | 11,816 | 382 | 23 |
| Home | 191,280 | 175,272 | 16,008 | 641 | 9 |

`CONFIG_HEAP_POISONING_LIGHT=y` (`sdkconfig:1251`). With roughly 16 bytes of non-payload accounting per live allocated block, the observations decompose consistently:

```text
Reading: 382 * 16 =  6,112; 11,816 - 6,112 = 5,704 fixed/free-block overhead
Home:    641 * 16 = 10,256; 16,008 -10,256 = 5,752 fixed/free-block overhead
```

The exact `191,280` raw total is **verified**. The approximately 16 bytes per allocated block and approximately 5.7 KiB remainder are **inferred** from the observed block counts; the static map cannot encode the runtime TLSF block topology. The key proof is the 4,192-byte change in reported heap size alongside 259 additional allocated blocks: `4,192 / 259 = 16.19` bytes per block. No hidden 20 KiB linker consumer is needed to explain either observation.

Directly from the misleading `199,436` subtotal:

```text
199,436 - 76,288 - 12 + 65,536 + 2,608 - 11,816 = 179,464
199,436 - 76,288 - 12 + 65,536 + 2,608 - 16,008 = 175,272
```

## 2. Largest static RAM consumers

### 2.1 Ranked symbols

These are the largest individually named static consumers in the inspected image. `.data` consumes both RAM and a flash load image; `.bss` consumes RAM but no flash payload. All entries below are mutable unless explicitly noted.

| Rank | Symbol | Section | Bytes | Object / residency assessment | Evidence |
|---:|---|---|---:|---|---|
| 1 | `display` | `.bss` | 52,312 | `libhal.a(HalDisplay.cpp.o)`; includes the 52,272-byte e-ink framebuffer plus object state. Required mutable pixel storage. | map `63762-63763` |
| 2 | `network::sharedBufferedHttpUploadSession()::session` | `.bss` | 6,344 | `BufferedHttpUpload.cpp.o`; mutable upload buffer, paths, filename, and error state. | map `63626`; `BufferedHttpUpload.h:22-59` |
| 3 | `g_cnxMgr` | `.bss` | 3,800 | `libnet80211.a(wl_cnx.o)`; live Wi-Fi connection state. | map `64105` |
| 4 | `ftm_initiator` | `.bss` | 2,776 | `libnet80211.a(ieee80211_ftm.o)`; Wi-Fi Fine Timing Measurement state, with no application references found. | map `63982` |
| 5 | `xIsrStack` | `.bss` | 2,096 | FreeRTOS ISR stack; size matches `CONFIG_FREERTOS_ISR_STACKSIZE=2096`. | map `63878`; `sdkconfig:1218` |
| 6 | `s_serialOtaSession` | `.bss` | 1,544 | `src/main.cpp.o`; mutable serial-OTA session state. | map `63603` |
| 7 | `BidiUtils::bidiLineScratch` | `.bss` | 1,536 | `MiniBidi/BidiUtils.cpp.o`; mutable line-layout scratch. | map `63771` |
| 8 | `packet.8427` | `.bss` | 1,460 | `libmdns.a(mdns.c.o)`; mutable mDNS packet buffer. | map `63914` |
| 9 | `fontDecompressor` | `.data` | 1,424 | `src/main.cpp.o`; mutable decompressor object, also has a flash initialization image. | map `62391-62393` |
| 10 | `ctype_w` | `.bss` | 1,248 | `libstdc++.a(locale_init.o)`; runtime locale table/state. | map `64369` |
| 11 | `dns_table` | `.bss` | 1,184 | `liblwip.a(dns.c.o)`; DNS resolver state/table. | map `63859` |
| 12 | `s_wifi_nvs` | `.bss` | 1,180 | `libnet80211.a(nvs.o)`; Wi-Fi NVS state. | map `64057-64059` |
| 13 | `s_coredump_stack` | `.bss` | 1,124 | ESP core-dump stack; config payload is 1,024 bytes plus state/alignment. | map `63924`; `sdkconfig:1126` |
| 14 | `CrossPointSettings::instance` | `.data` | 988 | Mutable persisted settings singleton with flash load image. | map `62380` |
| 15 | `TxRxCxt` | `.data` | 960 | `libpp.a` Wi-Fi transmit/receive context. | map `62918` |
| 16 | `gWpaSm` | `.bss` | 844 | WPA state-machine storage. | symbol/map near `66120` |
| 17 | `ble_att_svr_prep_entry_mem` | `.bss` | 768 | NimBLE prepared-write storage. | symbol in `.dram0.bss` |

The correct first target is not the framebuffer: it is genuinely mutable, and there is no PSRAM. The upload session is the largest application-controlled non-framebuffer object and contains a mechanically reducible 4,096-byte transfer buffer.

### 2.2 `.noinit` and RTC RAM

Normal DRAM `.noinit` is empty in this image (`firmware.map:63479`), so there is no hidden normal-RAM consumer there.

RTC memory is separate. `.rtc.text` is 20 bytes, `.rtc.data` is 4 bytes, and `.rtc_noinit` is 5,536 bytes. The main `.rtc_noinit` consumers are:

| Symbol/function | Bytes | Evidence |
|---|---:|---|
| RTC logging ring (`logMessages`) | 4,096 | `Logging.cpp:18-23`: 16 lines x 256 bytes; RTC map range beginning `0x50000028` |
| Panic stack | 1,152 | `HalSystem.cpp:13`: 32 frames; RTC map range |
| Panic message | 256 | RTC map range |
| Reboot/log magic, indices, target, clock state | remainder | map `.rtc_noinit`, `60273-60358` |

Reducing RTC-retained diagnostics expands the eligible RTC heap remainder rather than normal DRAM. It is still useful because `MALLOC_CAP_INTERNAL` includes that heap, but the operational tradeoff is reduced post-crash evidence.

### 2.3 Read-only candidates currently consuming DRAM

Only one simple, low-risk const-placement opportunity was verified:

- `timezoneOffsetOptions()::kCities` is a 108-byte pointer array in `.data`. The source declares `static const char* kCities[]` (`SettingsList.h:239-268`), which makes the pointed-to characters const but leaves the pointers mutable. Change it to `static constexpr const char* kCities[]` (or `static const char* const kCities[]`). Expected result: 108 bytes move from DRAM to flash, with approximately zero net firmware-image growth because `.data` initializers already occupy flash. Verify the rebuilt map.

Several registries are stored as mutable `.bss` arrays and populated during startup: Home actions (192 B), web routes (288 B), settings actions (288 B), reader registry (320 B), and lifecycle registry (640 B), about 1,728 bytes plus counters. They are logically immutable after registration but are not a safe one-line `const` change. A compile-time table refactor could move roughly 1.7 KiB to rodata; expect up to roughly 1.7 KiB of flash residency, potentially offset by removal of registration code. This is a medium/high-risk architectural change and must be validated against initialization order and optional features.

Do not move `PANIC_REASON_UNKNOWN` merely because it looks constant. Its 23-byte `DRAM_ATTR` placement supports panic/cache-disabled code and is intentional.

## 3. IRAM audit

### 3.1 Exact IRAM cost and DRAM coupling

`.iram0.text` contains 75,770 bytes (`0x127fa`) at `0x40380000` (`firmware.map:60362`). The linker then pads through `0x40392a00`, making the shared-DRAM reservation `.dram0.dummy = 76,288` bytes. The 518-byte gap consists of prefetch/security/alignment padding, including final 512-byte memory-protection alignment (`firmware.map:131978`).

This matters for optimization: removing less than or equal to the current 518 bytes of IRAM may produce **zero** additional heap. A successful IRAM change should be judged by the rebuilt `.dram0.dummy` boundary, not just by a smaller sum of IRAM input sections.

Largest IRAM object contributions include:

| Object family | Approx. IRAM bytes | Why present |
|---|---:|---|
| FreeRTOS tasks | 6,270 | scheduler/task primitives needed in critical and ISR paths |
| Ring buffer | 4,758 | normal and ISR-safe ring-buffer APIs currently placed in IRAM |
| Bluetooth controller `lld_con` | 3,818 | prebuilt controller timing path |
| FreeRTOS queue | 3,778 | queues/semaphores, including ISR users |
| TLSF heap | 3,452 | internal allocator/critical paths |
| SPI flash generic | 2,776 | cache-disabled flash operations |
| ESP flash API | 2,524 | flash/OTA operations |
| Bluetooth `lld_adv` | 2,468 | prebuilt controller timing path |
| SPI flash HAL IRAM | 2,070 | cache-disabled flash operations |
| Bluetooth `lld_scan` | 1,922 | prebuilt controller timing path |
| RTC sleep | 1,844 | sleep/wake control |
| RMT | 1,722 | timing/ISR path |
| RTC clock | 1,702 | low-level clock transition path |
| PHY register code | 1,550 | radio control |
| SPI flash GPSPI | 1,400 | flash access while cache is unavailable |
| Flash mmap | 1,356 | cache/mapping management |
| Heap capabilities | 1,312 | allocator critical path |

These are predominantly framework/IDF libraries rather than project annotations.

### 3.2 Application and Arduino IRAM attributes

Only two project-owned IRAM functions were found:

- `__wrap_panic_abort`, 58 bytes
- `__wrap_panic_print_backtrace`, 106 bytes

Both are in `HalSystem.cpp:27-71` and deliberately execute during panic/cache-unsafe conditions. Their combined 164 bytes should remain in IRAM.

Arduino HWCDC's SOF tick hook is another 76-byte IRAM function (`HWCDC.cpp:47-78`); it is a tick/USB timing path and should remain. `CONFIG_ARDUINO_ISR_IRAM` is not set (`sdkconfig:240`), so the build is not indiscriminately forcing ordinary Arduino functions into IRAM.

No clearly unnecessary application `IRAM_ATTR` was identified.

### 3.3 Configurations that can return IRAM to heap

1. **Place non-ISR ring-buffer functions in flash.** `CONFIG_RINGBUF_PLACE_FUNCTIONS_INTO_FLASH` is unset, as is the separate ISR option (`sdkconfig:1009-1010`). Enable only the non-ISR option. Estimated move: about 2.5-3.5 KiB from IRAM to flash, with a similar heap gain once the 512-byte boundary changes. The exact split is unknown until a rebuilt map. Keep ISR ring-buffer functions in IRAM because HWCDC calls `xRingbufferReceiveUpToFromISR`, `vRingbufferReturnItemFromISR`, and `xRingbufferSendFromISR` (`HWCDC.cpp:85-180`). Validate USB serial, serial OTA, and flash/OTA concurrency.

2. **Release-only heap-poisoning tradeoff.** Disable `CONFIG_HEAP_POISONING_LIGHT` and enable the disabled mode. `multi_heap_poisoning` contributes about 564 IRAM bytes, which is just above the current 518-byte padding and is therefore likely to reduce `.dram0.dummy` by 1,024 bytes. Removing per-allocation canaries would additionally recover an inferred ~3.1 KiB in the 382-block Reading state and ~5.1 KiB in the 641-block Home state. Total likely gain is ~4.1-6.1 KiB. This sacrifices valuable corruption detection and is appropriate only as an explicitly chosen release profile, not as an unconditional change.

3. **Do not enable the broad FreeRTOS-to-flash option.** `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH` is unset (`sdkconfig:1231`). It could move more than 10 KiB, but queue/task functions are called from interrupts and cache-disabled paths; enabling it globally is unsafe without a call-path proof. Likewise, `CONFIG_FREERTOS_PLACE_SNAPSHOT_FUNS_INTO_FLASH` would move only about 274 bytes and undermines panic/core-dump behavior while likely staying within current alignment padding.

4. **Do not replace the patched flash driver with the ROM implementation merely to save IRAM.** `CONFIG_SPI_FLASH_ROM_DRIVER_PATCH=y` and the ROM implementation is unset (`sdkconfig:1687-1688`). The patch layer protects flash/OTA operations and chip/ROM errata. Given a 16 MiB flash layout and dual OTA slots, the operational risk is disproportionate.

`CONFIG_ESP_EVENT_POST_FROM_IRAM_ISR=y` (`sdkconfig:879-880`) also has a real user: HWCDC posts events from its ISR. Moving that event path to flash would require proving that the USB ISR cannot execute while cache is disabled; that proof is absent.

### 3.4 ROM functions and newlib nano

The image already resolves basic libc routines such as `memset`, `memcpy`, `memcmp`, `strcpy`, `strncpy`, `strcmp`, and `strlen` to ESP32-C3 ROM addresses (`0x40000354` onward; `esp32c3.rom.newlib.ld:16-80`). There is no additional RAM win from trying to remap them.

`CONFIG_NEWLIB_NANO_FORMAT` is unset (`sdkconfig:1633`). Enabling it would bind more printf-family formatting to the ROM nano implementation (`esp32c3.rom.newlib-nano.ld:16-27`) and can reduce flash text, but it is not a meaningful static-DRAM or heap optimization. It also changes formatting capabilities/compatibility. Treat it only as a flash-headroom option after format-regression tests.

## 4. Garbage collection and linker settings

The existing build flags already include `-Os`, `-ffunction-sections`, `-fdata-sections`, and `-Wl,--gc-sections` (`platformio.ini:59-63`). The map's `Discarded input sections` begins at line `2483` and proves that section GC is active. The captured discarded total included about 27,102 bytes of text, 4,071 bytes of rodata, and 919 bytes of RAM-class input sections (`data`, `bss`, `sdata`, and `sbss`).

Therefore:

- Adding duplicate GC flags saves zero bytes.
- A custom linker fragment is not justified by the evidence.
- Remaining vendor archive/IRAM `KEEP` sections must not be stripped without a startup/ISR/cache-safety proof.
- The next map should be compared at `.dram0.dummy`, `.dram0.data`, `.dram0.bss`, `.rtc_noinit`, and the discarded-section table, not merely at PlatformIO's summary percentage.

## 5. Wi-Fi, lwIP, and task-stack audit

### 5.1 Wi-Fi buffers and unused features

The generated SDK config specifies 8 static RX buffers, 32 dynamic RX, 32 dynamic TX, 5 static management RX buffers, CSI, AMPDU TX/RX, and FTM initiator/responder (`sdkconfig:1078-1110`). However, Arduino's `WiFiGeneric.cpp:672-681` overrides static RX to 4 when `useStaticBuffers` is false, and the application never enables static-buffer mode. Changing only `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8` therefore has no runtime effect in the current Arduino path.

Useful changes are:

- **Disable FTM in the framework build.** No application FTM references were found, while `ftm_initiator` alone is a verified 2,776-byte BSS object and associated FTM state adds roughly another 76 bytes. Disable `CONFIG_ESP_WIFI_FTM_ENABLE`, initiator, and responder. Expected static gain: at least ~2.85 KiB; flash should also decrease. Because net80211 is precompiled, application `-D` flags alone may not remove these symbols—the Arduino/IDF library configuration must actually be rebuilt, then verified in the map.

- **Consider 4 -> 3 Arduino static RX buffers.** Patch the effective `cfg.static_rx_buf_num` value, not the ignored generated value. Expected gain is approximately one Wi-Fi receive buffer, around 1.6 KiB; exact allocator/header cost is runtime-dependent. Risk: reduced burst tolerance and radio throughput/reliability. Stress TLS downloads, reconnects, AP scanning, and simultaneous UI/network work.

- Switching the five management RX buffers from static to dynamic may improve idle heap by several KiB, but the exact buffer size and allocation behavior were not recoverable from the application map. It also increases fragmentation/burst-failure risk. This remains **unknown until an A/B framework build and runtime measurement**.

- Lowering dynamic RX/TX counts caps peak allocation; it does not necessarily increase idle heap because those buffers are allocated on demand. Do not present their configured count reduction as a static-RAM saving.

- CSI has only a small visible static object (approximately 8 bytes); disabling it may reduce flash but is not a material heap optimization.

Never switch the current dynamic TX policy to 32 static TX buffers: that could reserve tens of KiB and make the memory problem worse.

### 5.2 lwIP

The relevant settings are 16 sockets, 16 active/listening TCP PCBs, 16 UDP PCBs, 32-entry TCP/IP mailbox, 5,760-byte TCP send/window sizes, 6-entry per-connection receive mailboxes, and a 2,560-byte TCP/IP task stack (`sdkconfig:1305,1324,1354-1381,1392`). `lwipopts.h:98,105` enables libc allocation for lwIP memory and memp objects, so most PCB, pbuf, and mailbox capacity is heap-on-demand rather than a static pool.

Concrete implications:

- Reducing max sockets 16 -> 8 should save only about 288 static bytes: half of the verified 384-byte `sockets` array and 192-byte VFS `s_fd_table`. It also constrains TLS/websocket/server concurrency. Low reward.
- Reducing the TCP/IP mailbox 32 -> 16 saves only roughly 64 bytes of queue payload plus allocator overhead. Do it only after queue-watermark evidence.
- Reducing TCP window/send sizes chiefly reduces dynamic memory while connections are active and directly lowers throughput; it does not improve a no-connection idle baseline.
- Reducing the 2,560-byte TCP/IP stack to 2,304 would recover 256 runtime heap bytes, but only after high-water-mark measurement under DNS, TLS, upload, reconnect, and OTA workloads.
- `dns_table` is a verified 1,184-byte static consumer, but patching lwIP's DNS table size is not justified without DNS-concurrency evidence.

### 5.3 Persistent and transient task stacks

- **Arduino loop task:** 8,192 bytes, persistent (`sdkconfig:230`; `cores/esp32/main.cpp:36-38,71`). Add an application override of weak `getArduinoLoopTaskStackSize()` returning 6,144 to recover exactly 2,048 runtime heap bytes. First instrument `uxTaskGetStackHighWaterMark()` during full ePub parsing/rendering, image decoding, TLS, upload, serial OTA, and error paths. A stack overflow is catastrophic, so this is medium/high risk.

- **Arduino network event task:** hardcoded 4,096-byte stack and 32-entry pointer queue (`WiFiGeneric.cpp:564,578`). A framework patch to 3,072 recovers exactly 1,024 runtime heap bytes. Event callbacks can execute application logic, so high-water-mark evidence is mandatory.

- **IDF main task:** configured at 4,096 (`sdkconfig:1038`) but is deleted after `app_main` returns. Reducing it improves only transient boot headroom, not steady-state heap; low priority.

- **IDF system event task:** persistent 2,048-byte stack and 32-entry queue (`sdkconfig:1036-1037`). Do not shrink it without high-water and queue-watermark instrumentation; likely gain is only hundreds of bytes.

- **HWCDC event loop:** its 2,048-byte task is created only when `Serial.onEvent` is registered (`HWCDC.cpp:92-108`). No registration was found, so it is not currently a heap consumer. The generic Arduino serial-event stack setting applies to `HardwareSerial`, not this HWCDC path.

### 5.4 USB serial buffer

The build uses native USB CDC (`ARDUINO_USB_MODE=1`, CDC on boot; `platformio.ini:31-32`). When USB is connected at boot, `Logging.h:107-118` raises HWCDC RX capacity to 8,192 bytes before `begin`; setup enables it in `main.cpp:621-629`. Lowering this to 4,096 would recover exactly 4,096 runtime heap bytes while USB is active, but the source explicitly associates the large buffer with 2 KiB serial-OTA chunks and long e-ink blocking periods. This is a high-value, high-risk change: validate full serial OTA, sustained log input, and input during display refresh. A better longer-term design is to allocate extra buffering only for the serial-OTA session or make the parser backpressure-aware.

## 6. Ranked change plan

Flash context: the 6,553,600-byte OTA slot currently holds a 6,024,666-byte image (91.9%), leaving approximately 528,934 bytes. Small IRAM-to-flash moves fit numerically, but every rebuilt image still needs partition-size and OTA validation.

| Rank | Exact change | Expected gain | Flash effect | Risk / proof required | Map/config proof |
|---:|---|---:|---:|---|---|
| 1 | Rebuild framework with Wi-Fi FTM, initiator, and responder disabled | >=2,852 B static DRAM | decreases | Low if FTM is truly unused; verify symbols vanish and Wi-Fi behavior | `ftm_initiator` 2,776 B at map `63982`; FTM enabled at `sdkconfig:1098-1100`; no app references |
| 2 | `BufferedHttpUpload::kBufferSize` 4,096 -> 2,048 | exactly 2,048 B BSS | zero | Low/medium: twice as many SD writes; run upload/resume/error tests | 6,344 B session at map `63626`; buffer at `BufferedHttpUpload.h:24` |
| 3 | RTC log lines 16 -> 8 | exactly 2,048 B RTC heap capacity | zero | Medium: half post-crash log history | `Logging.cpp:18-23`, `.rtc_noinit` map `60273-60358` |
| 4 | Enable non-ISR ring-buffer functions in flash only | estimated 2.5-3.5 KiB heap after alignment | +2.5-3.5 KiB | Medium: validate USB/serial OTA and flash concurrency; never move ISR variants | ringbuf 4,758 B IRAM; config unset `sdkconfig:1009-1010` |
| 5 | Reduce RTC panic depth 32 -> 16 | exactly 576 B RTC heap capacity | zero | Medium: shorter backtrace | `HalSystem.cpp:13`; 1,152 B panic stack |
| 6 | Release profile only: disable light heap poisoning | estimated 4.1-6.1 KiB total | neutral/decreases | High: loses overrun/use-after-free signal; compare `.dram0.dummy` and runtime blocks | `sdkconfig:1251`; ~564 B poison IRAM; current 518 B padding |
| 7 | Override loop stack 8,192 -> 6,144 | exactly 2,048 B runtime heap | negligible | Medium/high: require workload high-water marks plus margin | `sdkconfig:230`; Arduino `main.cpp:36-38,71` |
| 8 | Reduce USB RX 8,192 -> 4,096 | exactly 4,096 B when USB active | zero | High: serial OTA and display-blocking input loss | `Logging.h:107-118`; `main.cpp:621-629` |
| 9 | Reduce Arduino network event task 4,096 -> 3,072 | exactly 1,024 B runtime heap | negligible | Medium/high: callback stack high-water mark | `WiFiGeneric.cpp:564,578` |
| 10 | Effective Wi-Fi static RX 4 -> 3 | approximately 1.6 KiB while Wi-Fi active | zero | Medium/high: packet bursts/reliability; full radio stress | Arduino override at `WiFiGeneric.cpp:672-681` |
| 11 | Refactor five startup registries into constexpr tables | approximately 1,728 B BSS | up to +1,728 B, likely partly offset | Medium/high refactor and initialization-order risk | registry arrays in `.dram0.bss` |
| 12 | Make `kCities` pointer array itself const | exactly 108 B `.data` | approximately zero net | Low | `SettingsList.h:239-268`; 108 B `.data` symbol |
| 13 | Sockets 16 -> 8, only if concurrency permits | approximately 288 B static | zero | Medium product constraint | `sdkconfig:1305`; `sockets` and `s_fd_table` map symbols |

The safest first bundle is ranks 1-3 plus 5 and 12: approximately 7.6 KiB of additional internal capacity with little/no flash growth and clearly bounded behavior changes. Add rank 4 after USB/OTA testing for roughly 10-11 KiB total. Stack and buffer reductions should follow instrumentation rather than assumptions.

Every operator-applied change should be accepted only if a rebuilt map/ELF proves the expected section movement and device tests cover authenticated network fetch, ePub/image render, sleep/wake, USB serial OTA, network OTA, and recovery/error paths.

## 7. What not to do

- **Do not move the framebuffer to flash.** It is mutable working memory. There is no PSRAM, and flash cannot serve as a writable framebuffer.
- **Do not heap-allocate the complete 6,344-byte upload session.** The observed largest free block can be around 5.6 KiB; converting known-good BSS to a large contiguous runtime allocation would introduce a fragmentation-dependent startup failure. Shrink the embedded transfer buffer instead.
- **Do not move panic strings/wrappers, flash drivers, ISR ring-buffer functions, broad FreeRTOS primitives, or event-post-from-ISR paths to flash.** Those paths can run while the flash cache is disabled. A flash-resident call then faults precisely during panic, OTA, or recovery.
- **Do not select `CONFIG_SPI_FLASH_ROM_IMPL` simply for memory.** The patched driver is part of flash-chip/ROM-errata safety. Any experiment would require exhaustive erase/write, both OTA slots, rollback, power-loss, and 16 MiB addressing tests.
- **Do not disable the second OTA slot or shrink OTA partitions to manufacture RAM.** Partition-table flash capacity and internal DRAM are independent. It would not recover one byte of heap and would damage rollback safety.
- **Do not add duplicate section-GC flags or an unproven linker script.** GC is already active and demonstrably discarding sections.
- **Do not assume generated `sdkconfig` values override Arduino runtime policy or prebuilt libraries.** Wi-Fi RX is overridden by `WiFiGeneric.cpp`, while removing FTM/IRAM vendor objects requires rebuilding the actual framework libraries.
- **Do not count lower dynamic Wi-Fi/lwIP maxima as idle static savings.** They primarily cap peak heap use and may instead turn bursts into allocation/network failures.
- **Do not reduce stacks without high-water marks from worst-case code paths.** Stack reductions return runtime heap, not `.bss`; the linker map cannot prove their safety.
- **Do not enable 32 static Wi-Fi TX buffers.** That changes on-demand allocation into a large reservation and can consume tens of KiB.
- **Do not disable core dumps, RTC logs, panic stack capture, or heap poisoning without explicitly accepting the loss of diagnostics.** Memory pressure is already fragmentation-sensitive; removing all corruption evidence can make the resulting failures much harder to diagnose.
- **Do not judge success solely by the PlatformIO RAM percentage.** The authoritative indicators are `.dram0.dummy`, `.dram0.data`, `.dram0.bss`, registered heap-region boundaries, `ESP.getHeapSize()`, free heap, largest free block, block counts, and task high-water marks.

## Bottom line

The linker's static allocation is internally consistent. The raw internal-capable heap is 191,280 bytes. `ESP.getHeapSize()` reports 179,464 or 175,272 because TLSF bookkeeping and light-poisoning overhead are excluded and grow with the number of live blocks. The highest-confidence reductions are unused Wi-Fi FTM state, the upload transfer buffer, and RTC diagnostic depth; meaningful IRAM recovery is available from non-ISR ring-buffer placement, while broad FreeRTOS/flash-driver moves are unsafe. The firmware has enough flash headroom for a few-KiB IRAM-to-flash trade, but OTA/cache safety and largest-contiguous-block behavior—not nominal total RAM—must remain the acceptance criteria.
