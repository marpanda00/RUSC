# Same as backend/scripts/download-regatta-assets.ps1 — run from repo root.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $here "..\..\backend\scripts\download-regatta-assets.ps1")
