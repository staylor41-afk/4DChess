param(
    [string]$UnityProjectPath = "C:\Users\Stayl\8DChessUnity",
    [string]$UnityEditorPath = "",
    [string]$OutputRoot = "C:\Users\Stayl\8DChessUnity\AutomationCaptures",
    [string]$LogPath = "",
    [string]$EditorLogSnapshotPath = "",
    [switch]$WaitForCompletion,
    [switch]$Headful = $true
)

$ErrorActionPreference = "Stop"

function Resolve-UnityEditorPath {
    param([string]$RequestedPath)

    if ($RequestedPath -and (Test-Path $RequestedPath)) {
        return $RequestedPath
    }

    $candidateRoots = @(
        "C:\Program Files\Unity\Hub\Editor",
        "C:\Program Files\Unity\Editor"
    )

    foreach ($root in $candidateRoots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $exe = Get-ChildItem -Path $root -Recurse -Filter "Unity.exe" -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1

        if ($exe) {
            return $exe.FullName
        }
    }

    throw "Could not locate Unity.exe. Pass -UnityEditorPath explicitly."
}

function Get-LatestCaptureDirectory {
    param([string]$Root)

    if (-not (Test-Path $Root)) {
        return $null
    }

    return Get-ChildItem -Path $Root -Directory |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Get-UnityEditorLogPath {
    $localAppData = [Environment]::GetFolderPath("LocalApplicationData")
    return Join-Path $localAppData "Unity\Editor\Editor.log"
}

function Test-UnityProjectLocked {
    param([string]$ProjectPath)

    $lockFile = Join-Path $ProjectPath "Temp\UnityLockfile"
    return Test-Path $lockFile
}

function Get-UnityProcesses {
    Get-Process -Name Unity,UnityHub -ErrorAction SilentlyContinue
}

$resolvedEditor = Resolve-UnityEditorPath -RequestedPath $UnityEditorPath

if (-not (Test-Path $UnityProjectPath)) {
    throw "Unity project path not found: $UnityProjectPath"
}

if (-not $LogPath) {
    $logRoot = Join-Path $UnityProjectPath "AutomationLogs"
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $LogPath = Join-Path $logRoot ("unity_capture_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log")
}

if (-not $EditorLogSnapshotPath) {
    $snapshotRoot = Join-Path $UnityProjectPath "AutomationLogs"
    New-Item -ItemType Directory -Force -Path $snapshotRoot | Out-Null
    $EditorLogSnapshotPath = Join-Path $snapshotRoot ("unity_editor_tail_" + (Get-Date -Format "yyyyMMdd_HHmmss") + ".log")
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

if (Test-UnityProjectLocked -ProjectPath $UnityProjectPath) {
    $unityProcesses = @(Get-UnityProcesses)
    if ($unityProcesses.Count -eq 0) {
        $staleLockFile = Join-Path $UnityProjectPath "Temp\UnityLockfile"
        Remove-Item -Path $staleLockFile -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 250
    }

    if (Test-UnityProjectLocked -ProjectPath $UnityProjectPath) {
        throw "Unity project appears to already be open (`Temp\\UnityLockfile` exists). Close the interactive Unity editor before running the unattended batchmode capture loop."
    }
}

$before = Get-LatestCaptureDirectory -Root $OutputRoot

$arguments = @(
    "-accept-apiupdate",
    "-stackTraceLogType", "Full",
    "-projectPath", "`"$UnityProjectPath`"",
    "-logFile", "`"$LogPath`"",
    "-executeMethod", $(if ($Headful) { "PlaymodeAutomationRunner.RunScreenshotSuiteHeadful" } else { "PlaymodeAutomationRunner.RunScreenshotSuiteBatchmode" })
)

if (-not $Headful) {
    $arguments = @("-batchmode", "-nographics") + $arguments
}

$argumentString = $arguments -join " "
Write-Output "Launching Unity capture suite..."
Write-Output "Editor: $resolvedEditor"
Write-Output "Project: $UnityProjectPath"
Write-Output "Log: $LogPath"
Write-Output ("Mode: " + $(if ($Headful) { "Headful" } else { "Batchmode" }))

$process = Start-Process -FilePath $resolvedEditor -ArgumentList $argumentString -PassThru -Wait:$WaitForCompletion.IsPresent

$editorLogPath = Get-UnityEditorLogPath
if (Test-Path $editorLogPath) {
    Get-Content -Path $editorLogPath -Tail 250 | Set-Content -Path $EditorLogSnapshotPath -Encoding UTF8
}

if ($WaitForCompletion) {
    if ($process.ExitCode -ne 0) {
        throw "Unity capture run exited with code $($process.ExitCode). See logs: $LogPath and $EditorLogSnapshotPath"
    }
}

Start-Sleep -Seconds 2
$after = Get-LatestCaptureDirectory -Root $OutputRoot

$captureDir = $null
if ($after -and (($before -eq $null) -or ($after.FullName -ne $before.FullName))) {
    $captureDir = $after.FullName
} elseif ($after) {
    $captureDir = $after.FullName
}

[pscustomobject]@{
    UnityEditorPath = $resolvedEditor
    UnityProjectPath = $UnityProjectPath
    LogPath = $LogPath
    EditorLogSnapshotPath = $EditorLogSnapshotPath
    CaptureDirectory = $captureDir
    ExitCode = if ($WaitForCompletion) { $process.ExitCode } else { $null }
}
