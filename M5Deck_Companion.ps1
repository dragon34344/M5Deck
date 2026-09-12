# =========================================================================
# M5StickC Plus 2 - Native Windows USB StreamDeck Companion (PowerShell)
# Запуск БЕЗ установки сторонних программ и БЕЗ Python!
# Автопоиск устройства: "USB-Enhanced-SERIAL CH9102"
# Работает на любой Windows 10 / Windows 11 «из коробки».
# Запуск: powershell -ExecutionPolicy Bypass -File .\streamdeck_usb.ps1
# =========================================================================

Add-Type -AssemblyName System.Windows.Forms

# Регистрация класса эмуляции мультимедийных клавиш Windows (Play/Pause, Vol+, Vol-)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class MediaKey {
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    public static void Press(byte vk) {
        keybd_event(vk, 0, 0, UIntPtr.Zero);
        keybd_event(vk, 0, 2, UIntPtr.Zero);
    }
}
"@ -ErrorAction SilentlyContinue

$targetDevice = "USB-Enhanced-SERIAL CH9102"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " M5StickC Plus 2 USB StreamDeck Companion (Windows)" -ForegroundColor Cyan
Write-Host " Searching for: '$targetDevice'..." -ForegroundColor Yellow
Write-Host " [Внимание] Port COM1 is completely excluded from operation" -ForegroundColor DarkGray
Write-Host "==================================================" -ForegroundColor Cyan

$portName = $null
$foundName = $null

# 1. Поиск PnP устройства с именем "USB-Enhanced-SERIAL CH9102" через Get-CimInstance / Get-WmiObject
try {
    $pnpDev = Get-CimInstance Win32_PnPEntity | Where-Object { 
        $_.Name -like "*$targetDevice*" -or $_.Caption -like "*$targetDevice*" -or $_.Description -like "*$targetDevice*"
    } | Select-Object -First 1

    if (-not $pnpDev) {
        $pnpDev = Get-WmiObject Win32_PnPEntity | Where-Object { 
            $_.Name -like "*$targetDevice*" -or $_.Caption -like "*$targetDevice*" -or $_.Description -like "*$targetDevice*"
        } | Select-Object -First 1
    }

    if ($pnpDev) {
        $foundName = if ($pnpDev.Caption) { $pnpDev.Caption } else { $pnpDev.Name }
        # Извлекаем COM-порт из имени, например: "USB-Enhanced-SERIAL CH9102 (COM5)"
        if ($foundName -match '((COMd+))') {
            $extractedPort = $matches[1]
            if ($extractedPort -ne "COM1") {
                $portName = $extractedPort
                Write-Host "[OK] target device found: $foundName" -ForegroundColor Green
                Write-Host "     Selected port: $portName" -ForegroundColor Green
            }
        }
    }
} catch {
    Write-Host "[Info] PnP search: $_" -ForegroundColor DarkGray
}

# 2. Если точное имя не совпало, ищем по чипу CH9102 или WCH (исключая COM1)
if (-not $portName) {
    try {
        $altDev = Get-CimInstance Win32_PnPEntity | Where-Object {
            ($_.Name -match 'CH9102|WCH' -or $_.Caption -match 'CH9102|WCH') -and ($_.Name -match '((COMd+))' -or $_.Caption -match '((COMd+))')
        } | Select-Object -First 1

        if ($altDev) {
            $foundName = if ($altDev.Caption) { $altDev.Caption } else { $altDev.Name }
            if ($foundName -match '((COMd+))') {
                $extractedPort = $matches[1]
                if ($extractedPort -ne "COM1") {
                    $portName = $extractedPort
                    Write-Host "[OK] Compatible device found: $foundName" -ForegroundColor Green
                    Write-Host "     Selected port: $portName" -ForegroundColor Green
                }
            }
        }
    } catch {}
}

# 3. Резервный вариант: если поиск по имени не нашел устройство, проверяем другие COM-порты (COM1 СТРОГО ИСКЛЮЧЕН!)
if (-not $portName) {
    # Получаем все порты, фильтруя COM1
    $allPorts = @([System.IO.Ports.SerialPort]::GetPortNames() | Where-Object { $_ -ne "COM1" })
    if ($allPorts.Count -eq 0) {
        Write-Host ""
        Write-Host "[ERROR] Device '$targetDevice' not found!" -ForegroundColor Red
        Write-Host "System port COM1 is blocked and excluded from use." -ForegroundColor Yellow
        Write-Host "1. Connect M5StickC Plus 2 to the PC with a USB-C cable." -ForegroundColor White
        Write-Host "2. Ensure the WCH CH9102 driver is installed (USB-Enhanced-SERIAL CH9102)." -ForegroundColor White
        Write-Host "   Windows Device Manager -> Ports (COM and LPT)." -ForegroundColor Gray
        exit 1
    }

    Write-Host "[!] Exact match '$targetDevice' not found." -ForegroundColor Yellow
    Write-Host "    Available COM ports (COM1 excluded): $($allPorts -join ', ')" -ForegroundColor Yellow
    Write-Host "    Using the first available port: $($allPorts[0])" -ForegroundColor Yellow
    $portName = $allPorts[0]
}

# Дополнительная строгая проверка: COM1 категорически запрещен
if (-not $portName -or $portName -eq "COM1") {
    Write-Host "[ERROR] Port COM1 is forbidden for use. M5StickC Plus 2 not found!" -ForegroundColor Red
    exit 1
}

Write-Host "--------------------------------------------------" -ForegroundColor Gray
Write-Host "Opening port $portName (115200 bps)..." -ForegroundColor Cyan

$port = New-Object System.IO.Ports.SerialPort $portName, 115200, None, 8, one
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.ReadTimeout = 500  # Таймаут чтения для плавной обработки прерываний (Ctrl+C)

try {
    $port.Open()
} catch {
    Write-Host "[ERROR] Failed to open port ${portName}: $_" -ForegroundColor Red
    Write-Host "Possible causes: Port is occupied by Arduino IDE, port monitor, or another application." -ForegroundColor Yellow
    exit 1
}

Write-Host "Ready! USB command listener is active." -ForegroundColor Green
Write-Host "Press buttons on M5StickC Plus 2 to control the PC." -ForegroundColor White
Write-Host "(To stop the script, press Ctrl + C)" -ForegroundColor DarkGray
Write-Host "--------------------------------------------------" -ForegroundColor Gray

$shell = New-Object -ComObject Shell.Application

try {
    while ($port.IsOpen) {
        try {
            $line = $port.ReadLine().Trim()
        }
        catch [System.TimeoutException] {
            continue
        }
        catch {
            break
        }

        if ($line) {
            Write-Host "[USB Event] $line" -ForegroundColor Green

            if ($line -eq "CMD:MINIMIZE") {
                Write-Host "  -> Minimizing all windows (Win+D)" -ForegroundColor Yellow
                $shell.MinimizeAll()
            }
            elseif ($line -eq "CMD:REBOOT") {
                Write-Host "  -> Rebooting the computer" -ForegroundColor Red
                shutdown /r /t 1
            }
            elseif ($line -eq "CMD:SHUTDOWN") {
                Write-Host "  -> Shutting down the computer" -ForegroundColor Red
                shutdown /s /t 1
            }
            elseif ($line -eq "CMD:MEDIA:PLAY_PAUSE") {
                Write-Host "  -> Медиа: Play / Pause" -ForegroundColor Cyan
                [MediaKey]::Press(0xB3)
            }
            elseif ($line -eq "CMD:MEDIA:VOL_UP") {
                Write-Host "  -> Media: Volume +" -ForegroundColor Cyan
                [MediaKey]::Press(0xAF)
            }
            elseif ($line -eq "CMD:MEDIA:VOL_DOWN") {
                Write-Host "  -> Media: Volume -" -ForegroundColor Cyan
                [MediaKey]::Press(0xAE)
            }
            elseif ($line -eq "CMD:MEDIA:MUTE") {
                Write-Host "  -> Media: Mute" -ForegroundColor Cyan
                [MediaKey]::Press(0xAD)
            }
            elseif ($line.StartsWith("CMD:RUN:")) {
                $app = $line.Substring(8).Trim()
                Write-Host "  -> Running application: $app" -ForegroundColor Cyan
                try {
                    if ($app -like "explorer.exe*") {
                        cmd.exe /c $app
                    }
                    else {
                        Start-Process $app
                    }
                }
                catch {
                    Write-Host "     [!] Error starting ${app}: $_" -ForegroundColor Red
                }
            }
        }
    }
}
finally {
    if ($port -and $port.IsOpen) {
        $port.Close()
    }
    Write-Host ""
    Write-Host "Port $portName closed. Work completed." -ForegroundColor Gray
}
