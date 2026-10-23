# Elite's legacy Oculus selection

## Status

- 2026-10-23 (EDVRonFOOT): профиль перенесён на `2026.10.23.334925` (заявка `openspec/changes/edvr-port-334925`).
  Все 11 блоков отпечатков доказаны тождественными блокам 334473, см. раздел «Перенос профиля на 334925» в конце.
  Следующий шаг: запуск `.23` (строка `oculus route: native=1 profile=1 ... installed=1`), затем прогулка `.22`.
- 2026-10-20 (EDVRonFOOT): профиль переносится с ревизии 332841 на `2026.10.13.334473`, заявка
  `openspec/changes/edvr-oculus-profile-334473`. Все 11 блоков отпечатков доказаны тождественными старым, см.
  раздел «Перенос профиля на 334473» в конце. Следующий шаг: полёт на `.9`, строка `oculus route: native=1
  profile=1 ... installed=1`.
- Без профиля под текущую ревизию DLL не загружается в Elite (`0xC0000142`, игра закрывается без окна). Так
  было на `.8` после обновления игры.

The release goal is to migrate every EDVR user to the native OpenXR backend,
including Meta and older Oculus users whom Elite currently routes through
LibOVR. Windows selects the OpenXR runtime. SteamVR remains supported as an
OpenXR runtime; forwarding to native OpenVR, OpenComposite and Elite's direct
LibOVR backend are not target transports.

Bypassing Elite's LibOVR preference is required migration work and must be
automatic in the native release. It is not a headset-specific opt-in or a
permanent choice between EDVR backends. Separate qualification builds and an
explicit rollback artifact are temporary migration tools. An upgrade must
install the complete native pair and preserve user settings; rollback restores
the previous installation deliberately rather than silently selecting a legacy
backend after OpenXR failure.

System OpenXR runtime selection and Elite's choice between LibOVR and OpenVR
are separate decisions. Changing the former does not force the latter. The
game-facing OpenVR API remains the compatibility entry into EDVR; the runtime
transport behind it is OpenXR.

## Static investigation result

The installed Frontier executable has a hard-coded backend selection loop:
LibOVR first, OpenVR second, then a third unsupported backend kind. The loop
stops on successful initialization and advances on failure. Static disassembly
now establishes the LibOVR load-failure path into that OpenVR fallback. This is
executable evidence, not a controlled launch with suppression enabled.

The inspection used the installed Odyssey executable, SHA-256
`e6be8bbe04e6a7ae226d4318945af7f367de13dc5a007a261964d9ba8144e988`. Imports,
call sites, virtual method mappings, cleanup and branch continuations were
checked with the PE reader and MSVC disassembler. Detailed offsets and
disassembly remain in the ignored `build/libovr-selection-static` archive.
These findings apply to this executable revision; future revisions need
validation. Twenty-four static import, instruction and virtual-table checks
passed against the unchanged executable, and an independent Luna review
confirmed the selection loop and load-failure cleanup. These checks do not
execute the game or qualify a loader hook.

### Entry and fallback

The installed Frontier executable's five OpenVR delay imports are
`VR_ShutdownInternal`, `VR_GetGenericInterface`, `VR_IsInterfaceVersionValid`,
`VR_GetInitToken` and `VR_InitInternal`. EDVR's facade implements this entry
path. Replacing `Openvr/win64/openvr_api.dll` only controls calls that reach
OpenVR; it cannot itself stop an earlier LibOVR choice.

The executable has no normal or delay LibOVR DLL import. Instead, its
compiled-in SDK shim constructs the LibOVR runtime filename, searches for it,
and calls the executable's regular `KERNEL32.dll!LoadLibraryW` import. On
success it resolves `ovr_Initialize` and the other SDK functions with
`GetProcAddress`.

The inspected failure chain is:

1. Both runtime search attempts fail to load a module.
2. The SDK loader returns `-3001`; the initialization wrapper propagates it.
3. Elite's Oculus backend returns false before creating an Oculus session. Its
   error-query wrapper handles an unloaded SDK, and shutdown checks the
   unresolved SDK function pointer before calling it.
4. The selection loop advances. The factory cleans up the failed backend,
   constructs OpenVR and invokes its initializer.
5. The OpenVR initializer loads `openvr/win64/openvr_api.dll` and calls the
   OpenVR entry points implemented by EDVR's native facade.

This establishes a plausible refusal point with an existing fallback. It does
not establish that a particular headset will initialize successfully through
the Windows-selected OpenXR runtime, or that a future hook executes early
enough.

Ruled out: pointing `LIBOVR_DLL_DIR` at a nonexistent directory is not a
reliable disable mechanism, because the shim subsequently tries its normal
search path. The inspected selector constructs its priority list directly; no
runtime-selection option is consulted in that function. The executable-string
and local configuration search did not establish a supported force-OpenVR
setting. This is not proof that no other setting exists elsewhere.

### Earlier EDVR interception

Elite imports `d3d11.dll!D3D11CreateDevice` normally, so the graphics proxy has
a process-attach entry before the game's executable entry point. The native
OpenVR facade arrives only if Elite reaches OpenVR, making it too late to
prevent the preceding Oculus choice.

The existing [early input gate](../src/d3d11/input_gate.cpp) demonstrates a
small executable-only IAT patch at graphics process attach. The [IAT
helper](../src/common/iat_hook.cpp) can target the confirmed regular loader
import and preserve its previous target. Device attachment is not a guaranteed
early interception point. Normal graphics initialization runs at the first D3D
export call; its ordering relative to the Oculus probe still requires a launch
trace.

The native graphics build now carries an immutable startup capability. The
development installer selects `build/edvr_openxr_graphics.dll` and installs it
as `d3d11.dll`, paired with `edvr_openxr_runtime.dll` as the game-facing
`Openvr/win64/openvr_api.dll`. The local OpenXR configuration is still read
later by the native module; early routing does not depend on reading that file.
`build/d3d11.dll` remains a separate qualification/rollback artifact without
the routing capability. This does not introduce a permanent user choice between
shipping backends.

Do not move configuration parsing, proxy-chain loading or normal logging into
`DllMain` to obtain that identity. The existing early configuration reader also
allocates and performs file I/O, so it is not a suitable primitive for a strict
loader-lock-safe hook.

The existing [runtime detector](../src/d3d11/vr_runtime.cpp) recognizes loaded
`LibOVRRT` modules; suppression is separately owned by the [startup
route](../src/d3d11/oculus_route.cpp). Runtime module presence alone cannot
establish which entry path Elite used.

## Implemented routing and qualification

The native graphics DLL installs an executable-only `LoadLibraryW` IAT wrapper
at process attach, before ordinary graphics initialization. It refuses only
calls from the audited SDK return RVA `0x4e70bc` with the exact
case-insensitive basename `LibOVRRT64_1.dll`. This covers both search attempts
through the same SDK call site. Other names and callers continue through the
previous IAT target with its result and last-error semantics preserved.

The [mapped executable profile](../src/common/elite_oculus_profile.h) validates
PE identity, imports, the IAT slot, relative calls, relocated virtual-table
entries and eleven complete code-block fingerprints before exchanging the slot.
The [installer profile](../tools/elite_oculus.py) additionally requires the
exact executable SHA-256. Unknown revisions are rejected before install; if the
game changes afterward, startup leaves its IAT alone and reports an unsupported
profile. That condition is a compatibility gap, not successful migration. New
game revisions require a newly audited profile.

The immutable `edvrNativeStartupRouting` data export distinguishes the native
graphics artifact without loading it. [Static pair
validation](../tools/openxr_pe.py) requires the marker's exact ABI and
read-only, non-executable placement, plus the native graphics provider exports.
Both direct and receipt-based installs check the pair and executable before
writing. Older receipts remain usable for explicit rollback.

Startup and the wrapper use fixed atomic storage with no configuration reads,
logging, allocation, waits or runtime loading. Original-target and routing
identity publication precede the IAT exchange, so a concurrent first probe is
filtered immediately. Installation and removal preserve another hook owner's
slot. Normal log initialization, owned Present calls and shutdown drain bounded
changed observations into the existing graphics log under `edvr_logs`.

The `oculus route:` line reports capability, profile recognition, installation,
calls before/after log readiness, the qualified caller RVA, rejected/forwarded
counts and failures. An initial line appears even when no probe was seen. The
first actual rejection remains reportable after ordinary snapshot exhaustion.
The CPU-only `edvrQueryOculusRouting` export permits startup inspection without
initializing graphics. A refused probe followed by native `VR_InitInternal` and
the selected runtime is the required live evidence; desktop tests alone cannot
establish successful migration.

Qualification must cover:

- Offline wrapper tests for caller/path/package filtering, forwarding,
  last-error preservation, concurrent publication, unknown executable behavior,
  and uninstall/rollback.
- Upgrade from each existing route (SteamVR/OpenVR, OpenComposite and direct
  LibOVR), with the SDK absent and present. Every upgraded installation must
  reach native OpenXR automatically and preserve settings.
- Explicit rollback restores the previous DLL/configuration arrangement. OpenXR
  initialization failure must not silently select a legacy runtime path.
- A connected Meta/Oculus headset with its compatible Windows-selected OpenXR
  runtime: record Elite's refused probe, subsequent native `VR_InitInternal`,
  selected runtime, rendering, recenter and clean shutdown.
- Runtime-originated Oculus component loads continuing normally; absence of
  every `LibOVRRT` module is not the success criterion.
- Quest/VDXR, Pimax/PiOpenXR and SteamVR OpenXR regression coverage, plus
  missing-runtime/startup-failure behavior.

Windows' selected runtime must actually support the connected headset; forcing
the entry path alone cannot establish compatibility for every older Oculus
device. The static investigation did not launch the game. Implementation
qualification is recorded below; runtime settings and system Oculus libraries
are not modified by this route.

## Desktop and Frontier checkpoint

Air Link retest on 2026-09-14 failed: the correctly installed
`v0.16.2-93-g6f99b03-dirty` graphics log at 09:26:03 reports `native=1
profile=0 installed=0 calls=0 failures=1`. The session subsequently reports
LibOVR loaded with no OpenVR module. No new native OpenXR log exists. Ruled
out: stale installation or a refused probe failing to advance the selector,
because the pair/config hashes match and the wrapper never armed. The offline
mapping fixture is insufficient evidence for live profile recognition; Windows'
mapped image metadata must also be exercised.

The rejection was reproduced without running the game. The file's preferred
`ImageBase` is `0x140000000`, but Windows maps this executable with its header
and relocated virtual tables referring to the randomized base. The old header
check required the file value even for a live mapping. A `SEC_IMAGE_NO_EXECUTE`
view remapped at its encoded base reproduces that state: the header, allocation
and virtual-table pointers agree at `0x7ff75ce30000` on this boot, while the
old constant comparison necessarily fails.

The mapped validator now accepts either the audited file's preferred base or
the actual mapped allocation base. Unrelated encoded bases still fail; all
revision, import, code-block and virtual-table proofs remain required. The file
validator continues to require the exact SHA-256 and preferred file base. The
regression includes both manually relocated memory and the guarded convenience
API applied to a real non-executable Windows image mapping. This corrects the
profile-recognition defect; an Air Link retry must still verify that the armed
route reaches native OpenXR.

### Original implementation qualification (before the failed Air Link flight)

The original full absolute-path `build.bat` run passed with all 534 source
hashes unchanged. It includes the existing ABI, lifecycle, transport, feature
and configuration gates plus 20 loader-route assertions, 17 standalone mapped
profile checks, 15 Python profile checks and 30 PE checks. Actual graphics DLL
startup inspection passed 17 checks for each of the native and baseline
artifacts. Installer self-tests cover mismatched pairs and unknown profiles
before writes, zero-write dry runs and compatibility with older receipts.

The actual Frontier executable passed the exact file validator and 43 offline
mapped-image checks. Those checks accept two distinct relocated allocations,
reject changes to every audited code block and all three virtual-table
pointers, reject changed PE identity and a partial import terminator, and
confirm the restored image passes again. No game code or imports are executed
by this fixture. Its optional command is `build/elite_oculus_test.exe --game
<absolute EliteDangerous64.exe path>`.

The new native graphics artifact also passed actual output checks for TAA and
DLSS (169 each, 18 treated eyes per mode), plus sharpening on/off (58/22).
These verify provider engagement and output, not headset image quality or
comparative performance.

That checkpoint installed and hash-verified `v0.16.2-93-g6f99b03-dirty` in
Frontier:

- Native runtime SHA-256:
  `98111f0c45ba256dab7175c6ed6edc3429e9c2325b4445fdcc7b85e086a6dd64`.
- Native graphics SHA-256:
  `02c7abb28f6f38f20245f1686f18d4f55fbb2756eb927e60134a37cd9facf563`.
- Local startup configuration SHA-256:
  `3da530c6e9c7e82b99a9f10caf0f6ea8b467719b36837aa4e2cbb37bd924f7b2`.
- Windows runtime discovery remains `runtime=system`; `edvr.ini` and
  `openvr_api_orig.dll` hashes are unchanged.

The ignored `build/openxr-libovr-routing-20260914/qualification.json` records
the source and artifact hashes, build log, install preview and verification.
The earlier [supported feature retest](openxr-feature-parity-2026-09-14.md)
remains pending and can be combined with this flight. The user launches
Frontier manually; no launcher automation, game launch or runtime-setting
change was performed for this checkpoint.

### Corrected Air Link checkpoint

The corrected pair is installed and hash-verified as
`v0.16.2-94-g663b49d-dirty`. Its full build passes with 537 source hashes
unchanged, 21 standalone profile checks, 47 checks against the actual Frontier
executable in manually relocated memory, and three checks against a Windows
`SEC_IMAGE_NO_EXECUTE` mapping. The mapped allocation, encoded header base and
virtual tables agree at `0x7ff75ce30000`. Both actual graphics variants pass 18
startup checks, including failure-stage reporting for unrelated executables.
Actual TAA/DLSS/sharpening checks also pass.

The [resolution checkpoint](openxr-resolution-2026-09-14.md) records the
installed runtime, graphics and startup-config hashes. The matching archive is
`build/openxr-airlink-resolution-20260914/qualification.json`; settings and the
original OpenVR DLL were preserved, and runtime selection remains `system`.
These checks validate profile recognition without executing game code;
successful Air Link routing still requires the next flight.

For the corrected build's next flight, use a Meta/Oculus headset with its
compatible Windows-selected OpenXR runtime while the legacy SDK is available.
Confirm that VR and F8 appear, startup faces the splash screen, head tracking
and recenter work, and exit is normal. The graphics log must show `native=1
profile=1 profile_failure=0 installed=1` and a refused Elite probe; the native
log must then identify successful initialization and the selected OpenXR
runtime. `rejected=0` does not prove the bypass worked. Check these through
`tools/edvr_log.py --target frontier --expect-build 663b49d`; the archived DLL
hashes distinguish this build from another dirty build at the same commit.

Then cover Quest/VDXR, Pimax/PiOpenXR and SteamVR OpenXR as available,
combining normal gameplay and the feature checklist. Live fallback and headset
behavior are still unqualified. Promoting the native pair into the release
installer, upgrading all prior backend arrangements and release-wide rollback
remain subsequent migration work; the current release installer still carries
the previous transport.

## Перенос профиля на 334473 (2026-10-20, EDVRonFOOT)

Новая ревизия: `2026.10.13.334473`, SHA-256 `6891f70a021fa9f3a0e00458c284f48d1226e68ae7fc02ad48081aed47857e93`,
PE stamp `0x6ACD513D`, SizeOfImage `0x640C000`. SizeOfHeaders `0x400`, 7 разделов и база `0x140000000` не
изменились. `.text`: RVA `0x1000`, span `0x4DB4C00`. Exe 332841 на диске больше нет: лаунчер его заменил.

Сдвиги областей кода, найденные по байтовым точкам и прямым вызовам:
- селектор (`0x8D5xxx`): +0x160;
- область LibOVR SDK (`0x4E0xxx…0x4E8xxx`): +0x100;
- ячейка `LoadLibraryW`: `0x4DB32E0` → `0x4DB62E0`, единственная регулярная в KERNEL32;
- указатели таблиц (vtable) нашлись по своим перемещённым значениям, каждый ровно один раз и в таблице перемещений:
  `0x4E243F8` → `0x4E27438`, `0x4E24400` → `0x4E27440`, `0x4E245C0` → `0x4E27600`.

Все 11 байтовых точек стоят на новых местах без изменений. Четыре из них уникальны во всём `.text`:
`0x8D549C`, `0x4E720E`, `0x4E8356`, `0x4E87BB`. Все 5 прямых вызовов ведут в сдвинутые цели. Вызов загрузчика
`0x4E71B6` (`FF 15` → ячейка) с продолжением `48 8B CB 48 8B F0` в `0x4E71BC`. Импорта LibOVR нет, в том числе
отложенного.

Отпечатки. Блок считается доказанным, если старый FNV-1a 332841 восстанавливается из новых байт после того, как
каждое относительное смещение (`rel32` вызовы и переходы, `rip`-адресация) переписано на «старая цель − старый
адрес следующей инструкции», где старая цель = новая цель − сдвиг её области. Сдвиги подбирались, но доказательство
даёт только точное совпадение 64-битного хэша. Итог проверяется одним общим набором сдвигов на 167 целей:

| 332841 | 334473 | длина | новый FNV-1a | итог |
|---|---|---|---|---|
| `0x8D52A0` | `0x8D5400` | `0xF3` | `0x17629A1FD3828894` | доказан |
| `0x4E4AA0` | `0x4E4BA0` | `0x2C7` | `0x98376EDD435B9D9C` | доказан |
| `0x4E4630` | `0x4E4730` | `0x23C` | `0x22A172BD9B250448` | доказан |
| `0x4E4870` | `0x4E4970` | `0x230` | `0x21CB0709FCF55236` | доказан |
| `0x4E8530` | `0x4E8630` | `0x155` | `0x2A9E2DBBAEDDE2BB` | доказан |
| `0x4E70D0` | `0x4E71D0` | `0x984` | `0x5D9DFCB146397878` | доказан |
| `0x4E6D40` | `0x4E6E40` | `0x390` | `0xC724029334737635` | доказан |
| `0x4E0E60` | `0x4E0F60` | `0xC7` | `0xFD358E07FA8A2868` | доказан |
| `0x4E8230` | `0x4E8330` | `0x3A` | `0x46C9815E81B0DF4A` | доказан |
| `0x4E5CA0` | `0x4E5DA0` | `0x65` | `0xA54979C6EB0CC47D` | доказан |
| `0x4E86B0` | `0x4E87B0` | `0x20` | `0x61126C9627CB6C61` | доказан |

Сдвиги данных, на которые ссылаются эти функции:
- `.rdata` около `0x4E27xxx` и `0x50CB0xx`: +0x3040;
- импорт `0x4DB62xx`: +0x3000;
- `0x4DE165C`: +0x3048;
- `.data` `0x5EFF5xx` и `0x5F1Dxxx`: +0x3040;
- `.data` `0x5EA99B8` (cookie): +0x3050;
- `.data` `0x5E73348`: +0x3000.

Внешние функции:
- `0x489xxxx`, `0x48B83E4`: +0x2B40;
- `0x53A6F0`, `0x5495C0`: +0x160.

DIR64-перемещений внутри блоков нет, как и в 332841: отпечатки не зависят от базы ASLR. Длины функций по `.pdata`
совпадают со старыми длинами блоков (`0xF3`, `0x23C`, `0x3A`, `0x65`, `0x20`), у `0x4E71D0` и `0x4E6E40` блок на
байт длиннее функции (`0x983`, `0x38F`). Три блока (`0x4E4BA0`, `0x4E4970`, `0x4E8630`) длиннее первого фрагмента
функции в `.pdata` — так было и в старом профиле.

Скрипты только читали exe. Они лежали в скретчпаде сессии (`port_oculus.py` — точки, вызовы, указатели,
перемещения; `prove_fp2.py` — доказательство отпечатков) и в репозиторий не входят. Метод выше достаточно повторить
для следующей ревизии.

## Перенос профиля на 334925 (2026-10-23, EDVRonFOOT)

Новая ревизия: `2026.10.23.334925`, SHA-256 `ed5a8f8cd10f9a80d3bda5badd7ba1d4fcfc15996c8c7e018852cb170e264b63`,
PE stamp `0x6ADA8048`, SizeOfImage `0x640C000`, SizeOfHeaders `0x400`, 7 разделов, база `0x140000000` — как в 334473.
`.text`: RVA `0x1000`, span `0x4DB4A00` (был `0x4DB4C00`); порог `0x4DB4000` в профиле не менялся и выполняется.
Exe 334473 на диске нет; «старыми» байтами служат отпечатки его профиля (`_FINGERPRINTS`), метод тот же, что в
записи о 334473.

Сдвиги областей, найденные по байтовым точкам, прямым вызовам и указателям:
- селектор (`0x8D5xxx`) и область LibOVR SDK (`0x4E0xxx…0x4E8xxx`): -0x60. Четыре точки, уникальные во всём файле
  (`0x8D549C`, `0x4E720E`, `0x4E8356`, `0x4E87BB`), нашлись каждая ровно по одному разу и все со сдвигом -0x60;
- ячейка `LoadLibraryW`: `0x4DB62E0`, не сдвинулась (по таблице импорта единственная регулярная в KERNEL32);
- указатели таблиц (vtable) не сдвинулись: `0x4E27438`, `0x4E27440`, `0x4E27600` нашлись по перемещённым значениям
  (новые функции), каждый ровно один раз, и все три стоят в таблице перемещений (DIR64);
- данные, на которые ссылаются блоки (`0x4DB62xx`, `0x4DE165C`, `0x4E27xxx`, `0x50CB0xx`, `.data` `0x5E73348`,
  `0x5EA99B8`, `0x5EFF5xx`, `0x5F1Dxxx`): 0;
- внешние функции: `0x53A6F0`, `0x5495C0`: -0x60; `0x489B6xx…0x489D0xx`, `0x48B83E4`: -0xA0.

Все 11 байтовых точек стоят на новых местах (старый RVA - 0x60) без изменений. Все 5 прямых вызовов ведут в сдвинутые
цели. Вызов загрузчика `0x4E7156` (`FF 15` на ячейку `0x4DB62E0`), продолжение `48 8B CB 48 8B F0` в `0x4E715C`.
Импорта LibOVR нет, в том числе отложенного.

Отпечатки. Блок доказан, если старый FNV-1a (из профиля 334473) восстанавливается из новых байт после того, как
каждое относительное смещение (`rel32` вызовы и переходы, `rip`-адресация) переписано на «старая цель - старый адрес
следующей инструкции», где старая цель = новая цель - сдвиг её области. Итог проверен одним общим набором сдвигов
на 167 целей (все 11 блоков):

| 334473 | 334925 | длина | новый FNV-1a | итог |
|---|---|---|---|---|
| `0x8D5400` | `0x8D53A0` | `0xF3` | `0xFF6BF29E3788FC3C` | доказан |
| `0x4E4BA0` | `0x4E4B40` | `0x2C7` | `0x60262CD96F885F66` | доказан |
| `0x4E4730` | `0x4E46D0` | `0x23C` | `0xE72D36967A10BFA8` | доказан |
| `0x4E4970` | `0x4E4910` | `0x230` | `0x7B3B4C4070FB007A` | доказан |
| `0x4E8630` | `0x4E85D0` | `0x155` | `0x1BEB5B69C7BF932E` | доказан |
| `0x4E71D0` | `0x4E7170` | `0x984` | `0x66B422313A80E9AE` | доказан |
| `0x4E6E40` | `0x4E6DE0` | `0x390` | `0xB9F0AF721320C54D` | доказан |
| `0x4E0F60` | `0x4E0F00` | `0xC7` | `0xAEE1539FB5BE2968` | доказан |
| `0x4E8330` | `0x4E82D0` | `0x3A` | `0xB69AB25FEC7A1F6A` | доказан |
| `0x4E5DA0` | `0x4E5D40` | `0x65` | `0x9F6C59F05E0A6390` | доказан |
| `0x4E87B0` | `0x4E8750` | `0x20` | `0x72BD38A6D3FB9681` | доказан |

Перемещений DIR64 внутри блоков нет: отпечатки не зависят от базы ASLR. Длины функций по `.pdata`: `0xF3`, `0x23C`,
`0x3A`, `0x65`, `0x20` совпадают с длиной блока; у `0x4E7170` и `0x4E6DE0` блок на байт длиннее функции (`0x983`,
`0x38F`); блоки `0x4E4B40`, `0x4E4910`, `0x4E85D0`, `0x4E0F00` длиннее первого фрагмента функции в `.pdata`
(`0x176`, `0x13C`, `0xF4`, `0x43`) — так было и в старом профиле.

Проверки после правки: `elite_oculus.py --self-test` — `21 checks, 0 failures`; `elite_oculus.py <exe>` принимает
профиль `elite-odyssey-ed5a8f8c-libovr-r1`; стенд `elite_oculus_test --self-test` — `21 checks, 0 failures`,
`--game <exe>` — `47 checks, 0 failures`, `--sec-image <exe>` — `3 checks, 0 failures`.

Скрипт только читал exe: `prove_334925.py` (доработка `prove_fp2.py`) в скретчпаде сессии, в репозиторий не входит.
