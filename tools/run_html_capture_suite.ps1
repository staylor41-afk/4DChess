param(
    [string]$HtmlPath = "C:\Users\Stayl\Downloads\Chess Files\8d-chess-v45.23.html",
    [string]$OutputRoot = "C:\Users\Stayl\OneDrive\Documents\New project\html_reference_captures"
)

$ErrorActionPreference = "Stop"

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runDir = Join-Path $OutputRoot $timestamp
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$captureScript = Join-Path $PSScriptRoot "capture_html_reference.ps1"

$steps = @(
    @{ Mode = "3d"; View = "exterior"; Name = "01_3d_exterior.png" },
    @{ Mode = "3d"; View = "slices";   Name = "02_3d_slices.png" },
    @{ Mode = "4d"; View = "exterior"; Name = "03_4d_exterior.png" },
    @{ Mode = "4d"; View = "slices";   Name = "04_4d_slices.png" }
)

$manifest = Join-Path $runDir "manifest.txt"
"HTML reference capture suite" | Set-Content -Path $manifest -Encoding UTF8
"Source: $HtmlPath" | Add-Content -Path $manifest

foreach ($step in $steps) {
    $target = Join-Path $runDir $step.Name
    & $captureScript -HtmlPath $HtmlPath -Mode $step.Mode -View $step.View -OutputPath $target | Out-Null
    "Saved $($step.Name) | mode=$($step.Mode) | view=$($step.View)" | Add-Content -Path $manifest
}

Write-Output $runDir
