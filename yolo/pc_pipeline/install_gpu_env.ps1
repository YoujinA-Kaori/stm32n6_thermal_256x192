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

Write-Host "=== Installing CUDA PyTorch into project .venv ===" -ForegroundColor Cyan
Write-Host "TEMP: $temp_dir"
Write-Host "CACHE: $cache_dir"
& $python -m pip install torch==2.7.1 torchvision==0.22.1 `
    --extra-index-url https://download.pytorch.org/whl/cu128 `
    --progress-bar on
if ($LASTEXITCODE -ne 0) {
    throw "CUDA PyTorch installation failed with exit code $LASTEXITCODE"
}

Write-Host "=== Installing Ultralytics ===" -ForegroundColor Cyan
& $python -m pip install ultralytics==8.3.157 dill==0.4.1 --progress-bar on
if ($LASTEXITCODE -ne 0) {
    throw "Ultralytics installation failed with exit code $LASTEXITCODE"
}

Write-Host "=== Verifying GPU environment ===" -ForegroundColor Cyan
& $python -c "import torch, ultralytics; print('torch:', torch.__version__); print('ultralytics:', ultralytics.__version__); print('CUDA available:', torch.cuda.is_available()); print('GPU:', torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'NONE')"
if ($LASTEXITCODE -ne 0) {
    throw "GPU environment verification failed with exit code $LASTEXITCODE"
}

Write-Host "=== Installation complete. Keep this window open for inspection. ===" -ForegroundColor Green
