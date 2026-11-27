# Installiert das Claude Touch Gadget auf diesem PC (nur für den angemeldeten Benutzer, ohne Admin-Rechte):
#   1. Node.js prüfen (bei Bedarf per winget installieren, nach Rückfrage)
#   2. Brücke nach %LOCALAPPDATA%\ClaudeTouchGadget\bridge kopieren und Pakete installieren
#   3. Mod "touch-dashboard" kopieren und in ~/.claude/settings.json eintragen
#   4. Autostart beim Anmelden einrichten und die Brücke gleich starten
# Aufruf: Doppelklick auf Installieren.cmd  (oder: powershell -ExecutionPolicy Bypass -File install.ps1)
param(
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ClaudeTouchGadget'),
  [string]$ClaudeHome = (Join-Path $env:USERPROFILE '.claude'),
  [switch]$NoAutostart,
  [switch]$NoStart
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Step($t) { Write-Host "`n== $t" -ForegroundColor Cyan }
function Ok($t) { Write-Host "   $t" -ForegroundColor Green }

Write-Host 'Claude Touch Gadget - Installation' -ForegroundColor White

# --- 1. Node.js ---------------------------------------------------------------
Step 'Node.js'
$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node) {
  Write-Host '   Node.js ist nicht installiert. Es wird für die Verbindung zum Gadget gebraucht.'
  $a = Read-Host '   Jetzt mit winget installieren (Node.js LTS)? [J/n]'
  if ($a -match '^(n|nein)$') { throw 'Abgebrochen: Node.js fehlt (https://nodejs.org).' }
  $ErrorActionPreference = 'Continue'
  winget install --id OpenJS.NodeJS.LTS -e --accept-package-agreements --accept-source-agreements
  $ErrorActionPreference = 'Stop'
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
  $node = Get-Command node -ErrorAction SilentlyContinue
  if (-not $node) { throw 'Node.js wurde nicht gefunden. Bitte das Fenster schliessen und die Installation neu starten.' }
}
$ver = (& $node.Source --version).TrimStart('v')
if ([version]$ver -lt [version]'18.0') { throw "Node.js $ver ist zu alt, mindestens 18 nötig." }
Ok "Node.js $ver ($($node.Source))"
$nodeExe = $node.Source
$npm = Join-Path (Split-Path $nodeExe) 'npm.cmd'
if (-not (Test-Path $npm)) { $npm = 'npm' }

# --- 2. Brücke ---------------------------------------------------------------
Step 'Brücke'
& (Join-Path $PSScriptRoot 'stop-bridge.ps1') -InstallDir $InstallDir
$bridge = Join-Path $InstallDir 'bridge'
New-Item -ItemType Directory -Force $bridge | Out-Null
foreach ($f in 'bridge.mjs', 'package.json', 'package-lock.json', 'settings-helper.mjs') {
  Copy-Item (Join-Path $repo "pc\$f") $bridge -Force
}
Push-Location $bridge
$ErrorActionPreference = 'Continue'  # npm schreibt Hinweise nach stderr
try {
  & $npm ci --omit=dev --no-fund --no-audit --loglevel=error
  if ($LASTEXITCODE) { throw 'npm ci fehlgeschlagen' }
} finally { Pop-Location; $ErrorActionPreference = 'Stop' }
Ok "installiert in $bridge"

# --- 3. Mod ------------------------------------------------------------------
Step 'Claude-Code-Mod'
$mod = Join-Path $InstallDir 'mod\touch-dashboard'
if (Test-Path $mod) { Remove-Item $mod -Recurse -Force }
New-Item -ItemType Directory -Force (Split-Path $mod) | Out-Null
Copy-Item (Join-Path $repo 'mod\touch-dashboard') (Split-Path $mod) -Recurse -Force
$settings = Join-Path $ClaudeHome 'settings.json'
& $nodeExe (Join-Path $bridge 'settings-helper.mjs') add $settings $mod
if ($LASTEXITCODE) { throw 'settings.json konnte nicht angepasst werden' }
Ok "Mod in $mod, eingetragen in $settings (Sicherung: settings.json.bak-touch-gadget)"

# --- 4. Autostart ------------------------------------------------------------
$vbs = Join-Path $InstallDir 'start-bridge.vbs'
$log = Join-Path $InstallDir 'bridge.log'
@"
' Startet die Brücke des Claude Touch Gadget unsichtbar; Ausgabe in bridge.log (wird ab 1 MB neu begonnen).
Set fso = CreateObject("Scripting.FileSystemObject")
logFile = "$log"
If fso.FileExists(logFile) Then
  If fso.GetFile(logFile).Size > 1048576 Then fso.DeleteFile logFile
End If
Set sh = CreateObject("WScript.Shell")
sh.CurrentDirectory = "$bridge"
sh.Run "cmd /c """"$nodeExe"" ""$bridge\bridge.mjs"" >> ""$log"" 2>&1""", 0, False
"@ | Set-Content -Path $vbs -Encoding Unicode

$startup = [Environment]::GetFolderPath('Startup')
$link = Join-Path $startup 'Claude Touch Gadget.lnk'
if (-not $NoAutostart) {
  Step 'Autostart'
  $ws = New-Object -ComObject WScript.Shell
  $sc = $ws.CreateShortcut($link)
  $sc.TargetPath = Join-Path $env:WINDIR 'System32\wscript.exe'
  $sc.Arguments = "`"$vbs`""
  $sc.WorkingDirectory = $InstallDir
  $sc.Description = 'Claude Touch Gadget - Brücke'
  $sc.Save()
  Ok "Verknüpfung im Autostart: $link"
}

if (-not $NoStart) {
  Step 'Brücke starten'
  Start-Process (Join-Path $env:WINDIR 'System32\wscript.exe') -ArgumentList "`"$vbs`""
  Ok "läuft im Hintergrund, Protokoll: $log"
}

Write-Host "`nFertig." -ForegroundColor Green
Write-Host @"

Nächste Schritte:
  1. Claude (Desktop-App bzw. Claude Code) einmal neu starten, damit der Mod geladen wird.
  2. Gadget per USB-Kabel anschliessen: Es zeigt nach wenigen Sekunden die Sessions.
  3. Für den Betrieb ohne Kabel: am Gadget auf den Titel tippen -> 'WLAN einrichten', danach
     'Mit diesem PC koppeln'. Wenn Windows nach der Firewall fragt: 'Private Netzwerke' erlauben.
"@
