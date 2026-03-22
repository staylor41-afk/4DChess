param(
    [string]$UnityProjectPath = "C:\Users\Stayl\8DChessUnity",
    [string]$HtmlPath = "C:\Users\Stayl\Downloads\Chess Files\8d-chess-v45.23.html",
    [string]$WorkspaceRoot = "C:\Users\Stayl\OneDrive\Documents\New project"
)

$ErrorActionPreference = "Stop"

$htmlScript = Join-Path $PSScriptRoot "run_html_capture_suite.ps1"
$unityScript = Join-Path $PSScriptRoot "run_unity_capture_suite.ps1"
$compareScript = Join-Path $PSScriptRoot "compare_capture_sets.ps1"

$summaryRoot = Join-Path $WorkspaceRoot "iteration_cycle_reports"
New-Item -ItemType Directory -Force -Path $summaryRoot | Out-Null
$reportDir = Join-Path $summaryRoot (Get-Date -Format "yyyyMMdd_HHmmss")
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null

$htmlResult = & $htmlScript -HtmlPath $HtmlPath

$unityResult = $null
$unityError = $null
try {
    $unityResult = & $unityScript -UnityProjectPath $UnityProjectPath -WaitForCompletion
} catch {
    $unityError = $_.Exception.Message
}

$compareResult = if ($unityResult -and $unityResult.CaptureDirectory) {
    & $compareScript -UnityCaptureDir $unityResult.CaptureDirectory
} else {
    & $compareScript
}

$summaryPath = Join-Path $reportDir "iteration_summary.md"
$summary = @(
    "# 8D Chess Iteration Cycle"
    ""
    "Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
    ""
    "## Artifacts"
    ""
    "- HTML capture run: $htmlResult"
    "- Unity capture directory: $(if ($unityResult) { $unityResult.CaptureDirectory } else { 'missing' })"
    "- Unity log: $(if ($unityResult) { $unityResult.LogPath } else { 'missing' })"
    "- Comparison report: $($compareResult.ReportPath)"
    ""
    "## Unity capture status"
    ""
    "- Status: $(if ($unityError) { 'failed' } else { 'ok' })"
    "- Details: $(if ($unityError) { $unityError } else { 'Unity batchmode capture completed.' })"
    ""
    "## Suggested next agent actions"
    ""
    "- Open the latest Unity and HTML capture pairs and compare composition, camera framing, shell material, piece placement, slice layout, and HUD chrome."
    "- Make the next highest-value patch toward HTML fidelity while preserving the faster C++ engine path."
    "- Re-run this cycle after the patch."
)

$summary | Set-Content -Path $summaryPath -Encoding UTF8

[pscustomobject]@{
    HtmlCaptureRun = $htmlResult
    UnityCaptureDir = $unityResult.CaptureDirectory
    UnityLogPath = $unityResult.LogPath
    ComparisonReport = $compareResult.ReportPath
    IterationSummary = $summaryPath
}
