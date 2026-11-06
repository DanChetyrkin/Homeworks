param(
    [Parameter(Mandatory)]
    [ValidateSet('A2', 'A3a', 'A3b', 'A3c', 'B1', 'B2', 'B3')]
    [string]$Mode,
    [switch]$VerifyEcho,
    [ValidateSet('Reads', 'Exceptions', 'Noise')]
    [string]$B2Check = 'Reads'
)

$ErrorActionPreference = 'Stop'
$labImages = @{
    A2  = 'lab02_A2.elf'
    A3a = 'lab02_A3a_polling.elf'
    A3b = 'lab02_A3b_delay.elf'
    A3c = 'lab02_A3c_irq.elf'
    B1  = 'lab02_B1.elf'
    B2  = 'lab02_B2.elf'
    B3  = 'lab02_B3.elf'
}
if ($VerifyEcho -and $Mode -notlike 'A3?') { throw 'Use -VerifyEcho with an A3 mode.' }
if ($PSBoundParameters.ContainsKey('B2Check') -and $Mode -ne 'B2') { throw 'Use -B2Check with B2.' }
$labPart = if ($Mode -like 'B?') { 'partB' } else { 'partA' }
$labImage = Join-Path $PSScriptRoot ("firmware\$labPart\" + $labImages[$Mode])
$labProgrammer = Join-Path $env:LOCALAPPDATA 'stm32cube\bundles\programmer\2.23.0\bin\STM32_Programmer_CLI.exe'
if (-not (Test-Path -LiteralPath $labImage)) { throw "Firmware file missing: $labImage" }
if (-not (Test-Path -LiteralPath $labProgrammer)) { throw "STM32CubeProgrammer missing: $labProgrammer" }

function Read-LabSerial {
    param([System.IO.Ports.SerialPort]$Serial, [int]$Milliseconds)
    $labBuffer = [System.Text.StringBuilder]::new()
    $labTimer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($labTimer.ElapsedMilliseconds -lt $Milliseconds) {
        [void]$labBuffer.Append($Serial.ReadExisting())
        Start-Sleep -Milliseconds 15
    }
    [void]$labBuffer.Append($Serial.ReadExisting())
    $labBuffer.ToString()
}

$labProbeText = (& $labProgrammer -l stlink) -join "`n"
$labProbes = [regex]::Matches($labProbeText, 'ST-LINK SN\s*:\s*([0-9A-F]+)')
if ($labProbes.Count -ne 1 -or $labProbeText -notmatch 'Board Name\s*:\s*NUCLEO-G491RE') {
    throw 'Connect a single NUCLEO-G491RE board through its ST-LINK USB port.'
}
$labConnection = 'sn=' + $labProbes[0].Groups[1].Value
$labSerial = $null

try {
    if ($VerifyEcho -or $labPart -eq 'partB') {
        $labDevices = @(Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%(COM%)'" | Where-Object { $_.PNPDeviceID -match '^USB\\VID_0483&PID_374E' })
        if ($labDevices.Count -ne 1 -or $labDevices[0].Name -notmatch '\((COM\d+)\)') {
            throw 'Could not identify the ST-LINK virtual serial port.'
        }
        $labPort = $Matches[1]
    }
    if ($VerifyEcho) {
        $labSerial = [System.IO.Ports.SerialPort]::new($labPort, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
        $labSerial.WriteTimeout = 1000
        $labSerial.Open()
        $labSerial.DiscardInBuffer()
    }

    $labFlash = & $labProgrammer -c port=SWD $labConnection mode=UR -w $labImage -v -rst
    if ($LASTEXITCODE -ne 0 -or ($labFlash -join "`n") -notmatch 'Download verified successfully') {
        $labFlash | Write-Output
        throw "Flash verification failed for $Mode."
    }
    Write-Output ("{0}: flash verified, {1}, SHA256 {2}" -f $Mode, $labImages[$Mode], (Get-FileHash -LiteralPath $labImage).Hash)

    if ($labPart -eq 'partB') {
        $labPython = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'
        $labModbus = Join-Path $PSScriptRoot 'firmware\partB\lab02_modbus.py'
        $labModbusArguments = @('--port', $labPort)
        if ($Mode -eq 'B1') { $labModbusArguments += '--write-only' }
        elseif ($B2Check -eq 'Exceptions') { $labModbusArguments += '--exceptions' }
        elseif ($B2Check -eq 'Noise') { $labModbusArguments += @('--errors', '0.3', '--seed', '7') }
        $labOriginalPythonPath = $env:PYTHONPATH
        try {
            $env:PYTHONPATH = Join-Path $PSScriptRoot 'firmware\partB\python_deps'
            & $labPython $labModbus @labModbusArguments
            if ($LASTEXITCODE -ne 0) { throw "$Mode Modbus verification failed." }
        }
        finally {
            $env:PYTHONPATH = $labOriginalPythonPath
        }
    }

    if ($VerifyEcho) {
        $labStartup = Read-LabSerial -Serial $labSerial -Milliseconds 800
        if ($labStartup -ne "Lab 2 ready.`r`n") { throw "Unexpected startup text: '$labStartup'" }
        Write-Output ("{0} 115200 8N1: {1}" -f $labSerial.PortName, $labStartup.TrimEnd())
        $labRequest = '0123456789'
        $labBursts = if ($Mode -eq 'A3b') { 2 } else { 1 }
        for ($labBurst = 1; $labBurst -le $labBursts; $labBurst++) {
            $labSerial.Write($labRequest)
            $labReply = Read-LabSerial -Serial $labSerial -Milliseconds 750
            Write-Output ("Burst {0}: sent '{1}', received '{2}' ({3}/10)" -f $labBurst, $labRequest, $labReply, $labReply.Length)
            if ($Mode -eq 'A3b') {
                if ($labReply.Length -ge 10 -or $labReply -notmatch '^0?1?2?3?4?5?6?7?8?9?$') {
                    throw 'Expected lost characters during delayed polling.'
                }
            }
            elseif ($labReply -ne $labRequest) {
                throw 'Expected a full ordered echo.'
            }
        }

        if ($Mode -eq 'A3b') {
            # Read ISR through SWD without resetting or reading RDR.
            $labRegisterText = (& $labProgrammer -c port=SWD $labConnection mode=HOTPLUG -r32 0x4000801C 0x4) -join "`n"
            $labIsr = [regex]::Match($labRegisterText, '(?im)^\s*(?:0x)?4000801c\s*:\s*(?:0x)?([0-9a-f]{8})')
            if ($LASTEXITCODE -eq 0 -and $labIsr.Success) {
                $labStatus = [Convert]::ToUInt32($labIsr.Groups[1].Value, 16)
                Write-Output ("LPUART1 ISR 0x{0:X8}, RXNE={1}, ORE={2}" -f $labStatus, (($labStatus -shr 5) -band 1), (($labStatus -shr 3) -band 1))
            }
            else {
                Write-Output 'ISR capture unavailable. Read RXNE and ORE in debugger Watch after the polling bursts.'
                $labRegisterText | Write-Output
            }
        }
    }
}
finally {
    if ($labSerial) { $labSerial.Dispose() }
}
