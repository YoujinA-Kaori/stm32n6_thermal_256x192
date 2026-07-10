param(
    [string]$CubeProgrammerPath
)

$ErrorActionPreference = 'Stop'

$project_dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$project_out = Get-ChildItem -Path $project_dir -Recurse -Filter '*_Appli.bin' |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $project_out) {
    throw "No *_Appli.bin file found under $project_dir"
}

$repo_root = Split-Path -Parent (Split-Path -Parent $project_dir)
$output_dir = Join-Path $repo_root 'Binary'
$output_hex = Join-Path $output_dir 'appli.hex'

New-Item -ItemType Directory -Path $output_dir -Force | Out-Null

& arm-none-eabi-objcopy -I binary $project_out.FullName --change-addresses 0x70100400 -O ihex $output_hex

if ($LASTEXITCODE -ne 0) {
    throw "arm-none-eabi-objcopy failed with exit code $LASTEXITCODE"
}
