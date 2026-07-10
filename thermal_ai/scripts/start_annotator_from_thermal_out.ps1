param(
    [string]$SessionName = ("session_{0}_a" -f (Get-Date -Format "yyyyMMdd")),
    [string]$InputDir = "thermal_out",
    [string]$RawRootDir = "thermal_ai_dataset/raw",
    [int]$Scale = 4
)

function Resolve-ProjectPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$PathValue
    )

    if ([System.IO.Path]::IsPathRooted($PathValue))
    {
        return $PathValue
    }

    return [System.IO.Path]::GetFullPath((Join-Path $script:ProjectRoot $PathValue))
}

$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptRoot "..\.."))
$AnnotatorScript = Join-Path $ScriptRoot "annotate_centerpoints.py"

if ([string]::IsNullOrWhiteSpace($InputDir))
{
    $InputDir = "thermal_out"
}

if ([string]::IsNullOrWhiteSpace($RawRootDir))
{
    $RawRootDir = "thermal_ai_dataset/raw"
}

$InputPath = Resolve-ProjectPath $InputDir
$RawRootPath = Resolve-ProjectPath $RawRootDir

$PythonCandidates = @(
    "C:\Users\26218\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe",
    "C:\Users\26218\AppData\Local\Programs\Python\Python310\python.exe"
)

$PythonExe = $null
foreach ($Candidate in $PythonCandidates)
{
    if (Test-Path $Candidate)
    {
        $PythonExe = $Candidate
        break
    }
}

if ($null -eq $PythonExe)
{
    $PythonExe = "python"
}

if (-not (Test-Path $InputPath))
{
    Write-Host "Input directory not found: $InputPath" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $RawRootPath))
{
    New-Item -ItemType Directory -Path $RawRootPath -Force | Out-Null
}

Write-Host "Launching annotator..." -ForegroundColor Cyan
Write-Host "  Python:  $PythonExe"
Write-Host "  Input:   $InputPath"
Write-Host "  RawRoot: $RawRootPath"
Write-Host "  Session: $SessionName"
Write-Host ""

& $PythonExe $AnnotatorScript `
    --input $InputPath `
    --raw-root $RawRootPath `
    --session-name $SessionName `
    --scale $Scale

exit $LASTEXITCODE
