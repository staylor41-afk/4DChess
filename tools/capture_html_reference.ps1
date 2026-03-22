param(
    [Parameter(Mandatory = $true)]
    [string]$HtmlPath,

    [Parameter(Mandatory = $true)]
    [string]$Mode,

    [Parameter(Mandatory = $true)]
    [ValidateSet("exterior", "slices")]
    [string]$View,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"

$edgePath = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
if (-not (Test-Path $edgePath)) {
    throw "Edge not found at $edgePath"
}

$source = Get-Content -Path $HtmlPath -Raw

$automationScript = @"
<script>
(function () {
  function runSetup() {
    try {
      if (typeof setGameMode === 'function') {
        setGameMode('$Mode');
      }
      if (typeof startGame === 'function') {
        startGame();
      }

      setTimeout(function () {
        if ('$View' === 'slices' && typeof toggleSlices === 'function' && !window.slicesOpen) {
          toggleSlices();
        }
        if ('$View' === 'exterior' && typeof toggleSlices === 'function' && window.slicesOpen) {
          toggleSlices();
        }
        if (typeof fitBoard === 'function') fitBoard();
        if (typeof renderAll === 'function') renderAll('view');
      }, 700);

      setTimeout(function () {
        if (typeof fitBoard === 'function') fitBoard();
        if (typeof renderAll === 'function') renderAll('view');
      }, 1800);
    } catch (err) {
      document.body.setAttribute('data-capture-error', String(err));
    }
  }

  if (document.readyState === 'complete') {
    setTimeout(runSetup, 100);
  } else {
    window.addEventListener('load', function () { setTimeout(runSetup, 100); });
  }
})();
</script>
"@

if ($source -match "</body>") {
    $instrumented = $source -replace "</body>", ($automationScript + "`r`n</body>")
} else {
    $instrumented = $source + "`r`n" + $automationScript
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "eightd_html_capture"
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
$tempFile = Join-Path $tempRoot ([System.IO.Path]::GetFileNameWithoutExtension($HtmlPath) + "_$Mode" + "_$View.html")
Set-Content -Path $tempFile -Value $instrumented -Encoding UTF8

New-Item -ItemType Directory -Force -Path ([System.IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
$fileUrl = "file:///" + ($tempFile -replace "\\", "/")

& $edgePath `
  --headless=new `
  --disable-gpu `
  --hide-scrollbars `
  --allow-file-access-from-files `
  --window-size=2560,1440 `
  --virtual-time-budget=9000 `
  "--screenshot=$OutputPath" `
  $fileUrl | Out-Null

if (-not (Test-Path $OutputPath)) {
    throw "Screenshot was not created: $OutputPath"
}

Write-Output $OutputPath
