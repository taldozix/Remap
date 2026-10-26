#!/usr/bin/env bash
# Copia data/ (fonte única do conteúdo em JSON) para unreal/Content/Data, preservando
# unreal/Content/Data/art (gerado por tools/art/blender/export_unreal.py).
# Uso: tools/sync_unreal_data.sh          copia
#      tools/sync_unreal_data.sh --check  só verifica (sai com 1 se houver diferença)
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${1:-}" == "--check" ]]; then
	if diff -rq data unreal/Content/Data | grep -v "^Only in unreal/Content/Data: art$"; then
		echo "unreal/Content/Data está desatualizado: rode tools/sync_unreal_data.sh" >&2
		exit 1
	fi
	echo "unreal/Content/Data em dia com data/"
	exit 0
fi
find unreal/Content/Data -mindepth 1 -maxdepth 1 ! -name art -exec rm -rf {} +
cp -a data/. unreal/Content/Data/
echo "data/ copiado para unreal/Content/Data"
