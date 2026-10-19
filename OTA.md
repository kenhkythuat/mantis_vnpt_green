# OTA Mantis STM32F103RET6

## 1. Cập nhật version

Sửa `VERSION_MANTIS` trong `Core/Inc/config.h`:

```c
#define VERSION_MANTIS "1.1"
```

## 2. Build firmware

Build cấu hình `Debug` bằng STM32CubeIDE.

File dùng để tạo gói OTA:

```text
Debug\MANTIS_STM32F103RET6.hex
```

## 3. Tạo gói OTA

Mở PowerShell tại thư mục project và chạy:

```powershell
python .\Tools\prepare_ota_manifest.py --firmware .\Debug\MANTIS_STM32F103RET6.hex --version 1.1
```

Giá trị `--version` phải giống `VERSION_MANTIS`.

Tool sẽ tự kiểm tra firmware, tính CRC32 và tạo:

```text
ota\mantis_v1.1.bin
ota\manifest.json
```

## 4. Đưa firmware lên GitHub

Đưa hai file sau lên thư mục `ota` của nhánh
`feature/connect_server_VNPT`:

1. `ota/mantis_v1.1.bin`
2. `ota/manifest.json`

Luôn đưa file `.bin` lên trước, sau đó mới cập nhật `manifest.json`.

Manifest được tải từ:

```text
https://raw.githubusercontent.com/kenhkythuat/mantis_vnpt_green/refs/heads/feature/connect_server_VNPT/ota/manifest.json
```

## 5. Gửi lệnh kiểm tra OTA

Topic:

```text
v1/devices/me/attributes
```

Payload:

```json
{"OTA":"ota_check"}
```

Log bắt đầu OTA:

```text
[OTA] MQTT request accepted and queued
[OTA] starting update check
[OTA] checking manifest
```

Thiết bị chỉ tải firmware khi version trong `manifest.json` lớn hơn
`VERSION_MANTIS` đang chạy.

## Cấu hình riêng cho từng thiết bị

Trước khi build bootloader cho từng thiết bị, sửa bốn giá trị `FACTORY_*`
trong `Core/Inc/config.h`:

```c
#define FACTORY_SERIAL_NUMBER "sw_000007"
#define FACTORY_MQTT_USER "..."
#define FACTORY_MQTT_PASS "..."
#define FACTORY_MQTT_CLIENT_ID "..."
```

Build bootloader:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\build_bootloader.ps1
```

Sau đó nạp `Bootloader\build\mantis_bootloader.hex` tại địa chỉ `0x08000000`.
Lần khởi động đầu tiên, bootloader sẽ ghi cấu hình vào trang Flash riêng
`0x08007800`. Bootloader chỉ ghi khi trang này chưa có cấu hình hợp lệ; các lần
khởi động và OTA sau đó không ghi đè giá trị đã lưu.

Application OTA chỉ đọc trang factory config. Bốn giá trị này không nằm trong
file OTA `.bin` và không bị thay đổi khi cập nhật firmware.

> Thiết bị đang dùng bootloader cũ phải được nạp bootloader mới một lần qua
> ST-Link trước khi cài application mới. Không OTA application mới trước bước
> này, vì trang factory config sẽ chưa được khởi tạo.

## Flash layout

| Vùng | Địa chỉ | Kích thước |
|---|---|---:|
| Bootloader code | `0x08000000` | 30 KiB |
| Factory device config | `0x08007800` | 2 KiB |
| Application | `0x08008000` | 238 KiB |
| OTA staging | `0x08043800` | 238 KiB |
| OTA metadata | `0x0807F000` | 2 KiB |
| Relay config | `0x0807F800` | 2 KiB |

Khi nạp lần đầu, nạp bootloader tại `0x08000000` và application tại
`0x08008000`.
