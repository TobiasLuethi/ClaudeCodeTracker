# Beendet eine laufende Brücke des Claude Touch Gadget, erkannt am vollen Pfad in der Befehlszeile.
param([string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'ClaudeTouchGadget'))
$script = Join-Path $InstallDir 'bridge\bridge.mjs'
Get-CimInstance Win32_Process -Filter "Name = 'node.exe'" -ErrorAction SilentlyContinue |
  Where-Object { $_.CommandLine -and $_.CommandLine.IndexOf($script, [StringComparison]::OrdinalIgnoreCase) -ge 0 } |
  ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
