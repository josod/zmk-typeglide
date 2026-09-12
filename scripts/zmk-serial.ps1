param(
    [Parameter(Mandatory=$true)]
    [string]$PortName,

    [int]$BaudRate = 9600,

    [int]$ReconnectDelay = 1,

    [string]$LogFile = "zmk-serial.log"
)

Write-Host "ZMK serial monitor: $PortName @ $BaudRate" -ForegroundColor Cyan
Write-Host "Logging to: $LogFile" -ForegroundColor Cyan
Write-Host "Press Ctrl+C to exit." -ForegroundColor DarkGray
Write-Host ""

while ($true) {
    $port = $null

    try {
        Write-Host "Connecting to $PortName..." -ForegroundColor Cyan

        $port = New-Object System.IO.Ports.SerialPort `
            $PortName, $BaudRate, None, 8, One

        $port.ReadTimeout = 1000
        $port.Open()

        Write-Host "Connected." -ForegroundColor Green
        Write-Host ""

        while ($port.IsOpen) {
            try {
                $line = $port.ReadLine()

                # Show on screen
                Write-Host $line

                # Append to log file
                $line | Out-File -FilePath $LogFile -Append -Encoding utf8
            }
            catch [TimeoutException] {
                # Keep waiting.
            }
        }
    }
    catch {
        Write-Host "COM port unavailable. Retrying..." -ForegroundColor Yellow
    }
    finally {
        if ($port) {
            try {
                if ($port.IsOpen) {
                    $port.Close()
                }
            } catch {}

            $port.Dispose()
        }
    }

    Start-Sleep -Seconds $ReconnectDelay
}
