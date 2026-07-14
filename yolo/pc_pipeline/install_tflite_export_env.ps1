$ErrorActionPreference = "Stop"

$pipeline_dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$python = Join-Path $pipeline_dir ".venv\Scripts\python.exe"
$temp_dir = Join-Path $pipeline_dir ".tmp"
$cache_dir = Join-Path $pipeline_dir ".pip-cache"

New-Item -ItemType Directory -Force -Path $temp_dir | Out-Null
New-Item -ItemType Directory -Force -Path $cache_dir | Out-Null
$env:TEMP = $temp_dir
$env:TMP = $temp_dir
$env:PIP_CACHE_DIR = $cache_dir

Write-Host "=== Installing pinned TFLite export dependencies ===" -ForegroundColor Cyan
& $python -m pip install `
    tensorflow==2.19.0 `
    tf_keras==2.19.0 `
    sng4onnx==1.0.4 `
    onnx_graphsurgeon==0.5.8 `
    ai-edge-litert==2.1.2 `
    onnx==1.17.0 `
    onnx2tf==1.26.3 `
    onnxslim==0.1.56 `
    onnxruntime-gpu `
    protobuf==5.29.5 `
    --extra-index-url https://pypi.ngc.nvidia.com `
    --progress-bar on
if ($LASTEXITCODE -ne 0) {
    throw "TFLite export dependency installation failed with exit code $LASTEXITCODE"
}

Write-Host "=== Verifying export dependencies ===" -ForegroundColor Cyan
& $python -c "import tensorflow as tf, onnx, onnx2tf; print('tensorflow:', tf.__version__); print('onnx:', onnx.__version__); print('onnx2tf:', onnx2tf.__version__)"
if ($LASTEXITCODE -ne 0) {
    throw "TFLite export dependency verification failed with exit code $LASTEXITCODE"
}

Write-Host "=== TFLite export environment ready ===" -ForegroundColor Green
