param([string]$ToolchainBin = "")

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "Bootloader\build"

if ([string]::IsNullOrWhiteSpace($ToolchainBin)) {
    $gccCommand = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
    if ($null -eq $gccCommand) {
        throw "arm-none-eabi-gcc.exe not found; pass -ToolchainBin."
    }
    $ToolchainBin = Split-Path -Parent $gccCommand.Source
}

$gcc = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
$objcopy = Join-Path $ToolchainBin "arm-none-eabi-objcopy.exe"
$sizeTool = Join-Path $ToolchainBin "arm-none-eabi-size.exe"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$common = @(
    "-mcpu=cortex-m3", "-mthumb", "-Os", "-ffunction-sections",
    "-fdata-sections", "-Wall", "-Wextra", "-DSTM32F103xE",
    "-I$repoRoot\Core\Inc",
    "-I$repoRoot\Drivers\CMSIS\Device\ST\STM32F1xx\Include",
    "-I$repoRoot\Drivers\CMSIS\Include"
)

& $gcc @common -c "$repoRoot\Bootloader\bootloader_main.c" -o "$buildDir\bootloader_main.o"
if ($LASTEXITCODE -ne 0) { throw "Bootloader compile failed" }
& $gcc @common -c "$repoRoot\Bootloader\system_bootloader.c" -o "$buildDir\system_bootloader.o"
if ($LASTEXITCODE -ne 0) { throw "Bootloader system compile failed" }
& $gcc @common -x assembler-with-cpp -c "$repoRoot\Core\Startup\startup_stm32f103retx.s" -o "$buildDir\startup.o"
if ($LASTEXITCODE -ne 0) { throw "Bootloader startup compile failed" }

$linkArgs = @(
    "-mcpu=cortex-m3", "-mthumb",
    "-T$repoRoot\Bootloader\STM32F103RETX_BOOTLOADER.ld",
    "-Wl,--gc-sections", "-Wl,-Map=$buildDir\mantis_bootloader.map",
    "-Wl,--no-warn-rwx-segments", "-nostdlib",
    "$buildDir\startup.o", "$buildDir\system_bootloader.o",
    "$buildDir\bootloader_main.o", "-lgcc", "-o", "$buildDir\mantis_bootloader.elf"
)
& $gcc @linkArgs
if ($LASTEXITCODE -ne 0) { throw "Bootloader link failed" }
& $objcopy -O ihex "$buildDir\mantis_bootloader.elf" "$buildDir\mantis_bootloader.hex"
if ($LASTEXITCODE -ne 0) { throw "Bootloader HEX conversion failed" }
& $sizeTool "$buildDir\mantis_bootloader.elf"
Write-Host "Bootloader HEX: $buildDir\mantis_bootloader.hex"
