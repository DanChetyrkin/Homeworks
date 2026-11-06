$ErrorActionPreference = 'Stop'
$labBundles = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'
$labGnuBin = Join-Path $labBundles 'gnu-tools-for-stm32\14.3.1+st.2\bin'
$labNinjaBin = Join-Path $labBundles 'ninja\1.13.2+st.1\bin'
$labCmake = Join-Path $labBundles 'cmake\4.4.0+st.1\bin\cmake.exe'
$labSource = Join-Path $PSScriptRoot 'firmware\partA\lab02_partA'
$labPreviousPath = $env:PATH
foreach ($labRequiredPath in @($labGnuBin, $labNinjaBin, $labCmake)) {
    if (-not (Test-Path -LiteralPath $labRequiredPath)) {
        throw "Required STM32 tool is missing: $labRequiredPath"
    }
}
Push-Location -LiteralPath $labSource
try {
    $env:PATH = $labGnuBin + ';' + $labNinjaBin + ';' + $labPreviousPath
    & $labCmake --preset Debug
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & $labCmake --build --preset Debug --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}
finally {
    $env:PATH = $labPreviousPath
    Pop-Location
}
