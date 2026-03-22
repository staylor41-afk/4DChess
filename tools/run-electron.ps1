$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$nodeHome = Join-Path $root 'tools\node-v24.14.0-win-x64'
$electronCmd = Join-Path $root 'node_modules\.bin\electron.cmd'

if (-not (Test-Path $nodeHome)) {
  throw "Portable Node runtime not found at $nodeHome"
}
if (-not (Test-Path $electronCmd)) {
  throw "Electron is not installed at $electronCmd"
}

$env:Path = "$nodeHome;$env:Path"
Push-Location $root
try {
  & $electronCmd .
}
finally {
  Pop-Location
}
