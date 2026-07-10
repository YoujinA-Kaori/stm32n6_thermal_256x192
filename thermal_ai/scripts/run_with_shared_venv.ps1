param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Script,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ScriptArgs
)

$python_candidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:THERMAL_AI_PYTHON))
{
    $python_candidates += $env:THERMAL_AI_PYTHON
}
$python_candidates += (Join-Path $PSScriptRoot "..\..\.venv\Scripts\python.exe")
$python_candidates += "D:\PracticeProject\thermal_model_tflite\.venv\Scripts\python.exe"

$python = $python_candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($python))
{
    $python_command = Get-Command python -ErrorAction SilentlyContinue
    if ($null -ne $python_command)
    {
        $python = $python_command.Source
    }
}

if ([string]::IsNullOrWhiteSpace($python))
{
    throw "TensorFlow Python environment not found. Set THERMAL_AI_PYTHON to a valid python.exe."
}

$script_path = $Script
if (-not [System.IO.Path]::IsPathRooted($script_path))
{
    $candidate = Join-Path $PSScriptRoot $script_path
    if (Test-Path $candidate)
    {
        $script_path = $candidate
    }
}

& $python $script_path @ScriptArgs
exit $LASTEXITCODE
