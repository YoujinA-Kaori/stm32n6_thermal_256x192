#!/bin/bash

set -e

ScriptPath="$0"
if command -v readlink >/dev/null 2>&1; then
    ResolvedPath="$(readlink -f "$ScriptPath" 2>/dev/null || true)"
fi

if [ -n "$ResolvedPath" ]; then
    ProjectDir="$(dirname "$ResolvedPath")"
else
    ProjectDir="$(cd "$(dirname "$ScriptPath")" && pwd)"
fi

for j in $(find "$ProjectDir" -name "*_Appli.bin"); do
    ProjectOut="$j"
done

if [ -z "$ProjectOut" ]; then
    echo "No *_Appli.bin file found under $ProjectDir" >&2
    exit 1
fi

OutputDir="$ProjectDir"/../../Binary
mkdir -p "$OutputDir"

arm-none-eabi-objcopy -I binary "$ProjectOut" --change-addresses 0x70100400 -O ihex "$OutputDir"/appli.hex
