$ErrorActionPreference = 'Stop'
$labSource = Join-Path $PSScriptRoot 'firmware\partB\lab02_partB'
$labBundles = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles'
$labCmake = Join-Path $labBundles 'cmake\4.4.0+st.1\bin\cmake.exe'
$env:PATH = (Join-Path $labBundles 'gnu-tools-for-stm32\14.3.1+st.2\bin') + ';' + (Join-Path $labBundles 'ninja\1.13.2+st.1\bin') + ';' + $env:PATH

Push-Location -LiteralPath $labSource
try {
    & $labCmake --preset Debug
    if ($LASTEXITCODE -ne 0) { throw 'Part B configuration failed.' }
    & $labCmake --build --preset Debug --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Part B build failed.' }
}
finally {
    Pop-Location
}
