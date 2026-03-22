param(
    [string]$UnityCaptureRoot = "C:\Users\Stayl\8DChessUnity\AutomationCaptures",
    [string]$HtmlCaptureRoot = "C:\Users\Stayl\OneDrive\Documents\New project\html_reference_captures",
    [string]$OutputRoot = "C:\Users\Stayl\OneDrive\Documents\New project\comparison_reports",
    [string]$UnityCaptureDir = "",
    [string]$HtmlCaptureDir = ""
)

$ErrorActionPreference = "Stop"

function Get-LatestRunDirectory {
    param([string]$Root)

    if (-not (Test-Path $Root)) {
        return $null
    }

    Get-ChildItem -Path $Root -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Get-CaptureMap {
    param([string]$DirectoryPath)

    $map = @{}
    if (-not $DirectoryPath -or -not (Test-Path $DirectoryPath)) {
        return $map
    }

    Get-ChildItem -Path $DirectoryPath -File -Include *.png |
        ForEach-Object {
            $normalized = $_.BaseName -replace '^\d+_', ''
            $map[$normalized] = $_.FullName
        }

    return $map
}

if (-not $UnityCaptureDir) {
    $unityLatest = Get-LatestRunDirectory -Root $UnityCaptureRoot
    if ($unityLatest) {
        $UnityCaptureDir = $unityLatest.FullName
    }
}

if (-not $HtmlCaptureDir) {
    $htmlLatest = Get-LatestRunDirectory -Root $HtmlCaptureRoot
    if ($htmlLatest) {
        $HtmlCaptureDir = $htmlLatest.FullName
    }
}

if (-not $HtmlCaptureDir) {
    throw "Could not find an HTML capture directory."
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$reportDir = Join-Path $OutputRoot (Get-Date -Format "yyyyMMdd_HHmmss")
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$unityMap = if ($UnityCaptureDir) { Get-CaptureMap -DirectoryPath $UnityCaptureDir } else { @{} }
$htmlMap = Get-CaptureMap -DirectoryPath $HtmlCaptureDir

$allKeys = ($unityMap.Keys + $htmlMap.Keys) | Sort-Object -Unique
$reportPath = Join-Path $reportDir "comparison_report.md"

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# 8D Chess Visual Comparison Report")
$lines.Add("")
$lines.Add("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
$lines.Add("")
$lines.Add(("Unity captures: {0}" -f $(if ($UnityCaptureDir) { $UnityCaptureDir } else { "missing" })))
$lines.Add(("HTML captures: {0}" -f $HtmlCaptureDir))
$lines.Add("")

foreach ($key in $allKeys) {
    $unityPath = if ($unityMap.ContainsKey($key)) { $unityMap[$key] } else { $null }
    $htmlPath = if ($htmlMap.ContainsKey($key)) { $htmlMap[$key] } else { $null }

    $lines.Add("## $key")
    $lines.Add("")
    if ($unityPath) {
        $lines.Add(("- Unity: {0}" -f $unityPath))
    } else {
        $lines.Add("- Unity: missing")
    }
    if ($htmlPath) {
        $lines.Add(("- HTML: {0}" -f $htmlPath))
    } else {
        $lines.Add("- HTML: missing")
    }

    if ($unityPath -and $htmlPath) {
        $lines.Add("- Compare: composition, camera framing, exterior shell density, piece placement, slice layout, and HUD/chrome fidelity.")
    }
    $lines.Add("")
}

$lines | Set-Content -Path $reportPath -Encoding UTF8

[pscustomobject]@{
    UnityCaptureDir = $UnityCaptureDir
    HtmlCaptureDir = $HtmlCaptureDir
    ReportDir = $reportDir
    ReportPath = $reportPath
}
