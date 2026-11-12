# Build one Lab 2 task and flash it to the NUCLEO-G491RE.
#
#   ./run-task.ps1 A1            build + flash A1
#   ./run-task.ps1 B2 -BuildOnly build only (build/B2/lab2.elf)
#
# Tasks: A1, A2, A3a (polling), A3b (polling + delay), A3c (interrupt), B1, B2, B3
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('A1', 'A2', 'A3a', 'A3b', 'A3c', 'B1', 'B2', 'B3')]
    [string]$Task,
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
$bundles = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'

# Newest copy of a tool in the STM32Cube bundles, else whatever is on PATH
function Find-Tool([string]$bundle, [string]$relative, [string]$fallback) {
    $root = Join-Path $bundles $bundle
    if (Test-Path -LiteralPath $root) {
        $hit = Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName $relative } |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if ($hit) { return $hit }
    }
    $cmd = Get-Command $fallback -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "Tool not found: $fallback (looked in $root and PATH)"
}

$cmake      = Find-Tool 'cmake'              'bin\cmake.exe'                'cmake'
$gcc        = Find-Tool 'gnu-tools-for-stm32' 'bin\arm-none-eabi-gcc.exe'   'arm-none-eabi-gcc'
$ninja      = Find-Tool 'ninja'              'bin\ninja.exe'                'ninja'
$env:PATH   = (Split-Path $gcc) + ';' + (Split-Path $ninja) + ';' + $env:PATH

Push-Location -LiteralPath $PSScriptRoot
try {
    Write-Host "=== Lab 2 task $Task : configure" -ForegroundColor Cyan
    & $cmake --preset $Task | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $Task" }

    Write-Host "=== Lab 2 task $Task : build" -ForegroundColor Cyan
    & $cmake --build --preset $Task | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $Task" }

    $elf = Join-Path $PSScriptRoot "build\$Task\lab2.elf"
    if ($BuildOnly) {
        Write-Host "Built $elf" -ForegroundColor Green
        return
    }

    # Only one program can use the ST-LINK: end a debug session that holds it
    $debug = Get-Process -Name 'ST-LINK_gdbserver', 'arm-none-eabi-gdb' -ErrorAction SilentlyContinue
    if ($debug) {
        Write-Host '=== Stopping the debug session that holds the ST-LINK' -ForegroundColor Yellow
        $debug | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 1
    }

    $programmer = Find-Tool 'programmer' 'bin\STM32_Programmer_CLI.exe' 'STM32_Programmer_CLI'
    Write-Host "=== Lab 2 task $Task : flash" -ForegroundColor Cyan
    & $programmer -c port=SWD mode=UR -w $elf -v -rst | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'Flash failed: check the USB cable of the ST-LINK, then run again.'
    }
    Write-Host "Task $Task is running on the board." -ForegroundColor Green
}
finally {
    Pop-Location
}
