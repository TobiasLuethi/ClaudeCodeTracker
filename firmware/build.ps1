# Baut (und flasht optional) einen Sketch aus diesem Ordner.
#   .\build.ps1 dashboard            # nur kompilieren
#   .\build.ps1 dashboard -Upload    # kompilieren und auf COM3 flashen
#   .\build.ps1 dashboard -Upload -Port COM5
# Die USB-Brücke (pc\bridge.mjs) muss beim Flashen beendet sein, sonst ist der COM-Port belegt.
param(
  [Parameter(Mandatory = $true)][string]$Sketch,
  [switch]$Upload,
  [string]$Port = 'COM3'
)
$ErrorActionPreference = 'Continue'  # Compiler schreibt Hinweise nach stderr
$cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
if (-not (Test-Path $cli)) { $cli = 'arduino-cli' }
$here = $PSScriptRoot
$dir = Join-Path $here $Sketch
$fqbn = (Get-Content (Join-Path $here 'FQBN.txt')).Trim()
# lv_conf.h liegt im Sketch-Ordner; LVGL findet sie über den Include-Pfad
$inc = ($dir -replace '\\', '/')
$flags = "-DLV_CONF_INCLUDE_SIMPLE -I$inc"
$props = @('--build-property', "compiler.c.extra_flags=$flags", '--build-property', "compiler.cpp.extra_flags=$flags")
& $cli compile -b $fqbn @props $dir
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Upload) {
  & $cli upload -b $fqbn -p $Port $dir
  exit $LASTEXITCODE
}
