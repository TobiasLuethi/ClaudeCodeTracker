# Startet die USB-Brücke zum Claude Touch Gadget.
# Zum Flashen neuer Firmware vorher beenden (Strg+C), sonst ist der COM-Port belegt.
Set-Location $PSScriptRoot
if (-not (Test-Path node_modules)) { npm install --no-fund --no-audit }
node bridge.mjs
