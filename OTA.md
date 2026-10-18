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

## Flash layout

| Vùng | Địa chỉ | Kích thước |
|---|---|---:|
| Bootloader | `0x08000000` | 32 KiB |
| Application | `0x08008000` | 238 KiB |
| OTA staging | `0x08043800` | 238 KiB |
| OTA metadata | `0x0807F000` | 2 KiB |
| Relay config | `0x0807F800` | 2 KiB |

Khi nạp lần đầu, nạp bootloader tại `0x08000000` và application tại
`0x08008000`.
