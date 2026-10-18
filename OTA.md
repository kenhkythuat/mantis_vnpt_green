# Mantis staged OTA

## Flash map

The STM32F103RET6 has 512 KiB flash (`0x08000000..0x0807FFFF`) and 2 KiB
erase pages.

| Region | Start | End (inclusive) | Size |
|---|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08007FFF` | 32 KiB |
| Active application | `0x08008000` | `0x080437FF` | 238 KiB |
| Staging image | `0x08043800` | `0x0807EFFF` | 238 KiB |
| Pending metadata | `0x0807F000` | `0x0807F7FF` | 2 KiB |
| Relay configuration | `0x0807F800` | `0x0807FFFF` | 2 KiB |

All boundaries are erase-page aligned. The active and staging capacities are
equal so an image accepted for the active slot always fits staging.

## MQTT topics

Relay RPC continues to use:

```text
v1/devices/me/rpc/request/+
```

OTA uses a separate subscription:

```text
v1/devices/me/attributes
```

Trigger an update check on the attributes topic with:

```json
{"OTA":"ota_check"}
```

The device pauses TIM6 telemetry and starts the HTTPS manifest/download flow
from the main loop. Relay control remains `setRelays` on the RPC topic.

Every telemetry payload published to `v1/devices/me/telemetry` also contains
the compiled firmware version, for example `"_version":"1.0"`.

## Manifest

Configure `OTA_MANIFEST_URL` in `Core/Inc/config.h` before field testing. The
manifest schema is:

```json
{
  "version": "1.1.0",
  "device": "mantis-stm32f103ret6",
  "app_addr": "0x08008000",
  "size": 60000,
  "crc32": "0x12345678",
  "bin_url": "https://host/path/mantis_v1.1.0.bin"
}
```

The compiled `VERSION_MANTIS`, artifact filename, and manifest version must
match. Publish the binary before publishing the manifest.

## Build and factory programming

Build the application with `STM32F103RETX_FLASH.ld`. Build the bootloader with:

```powershell
.\Tools\build_bootloader.ps1 -ToolchainBin <GNU_ARM_BIN_DIRECTORY>
```

Generate a contiguous binary and manifest from the relocated application HEX:

```powershell
python .\Tools\prepare_ota_manifest.py `
  --firmware <APPLICATION_HEX> `
  --version 1.1.0 `
  --base-url https://host/path
```

For initial factory programming, program the bootloader at `0x08000000` and
the relocated application at `0x08008000`. Later updates are staged and
installed by the bootloader.

## Safety behavior

The application checks HTTP length, manifest fields, device ID, application
address, image size, streaming CRC32, and staging-flash CRC32. Pending metadata
is committed only after all checks pass. The bootloader validates metadata,
the staged CRC, stack pointer, Thumb reset vector, address ranges, installed
CRC, and final vector table before clearing pending metadata and jumping.

CRC32 detects transfer corruption but does not authenticate firmware. A
production deployment that must resist malicious images needs a signature and
anti-rollback policy in addition to this implementation.
