#!/usr/bin/env bash
# Roda os testes unitários e o teste de aceitação headless.
# Uso: GODOT=/caminho/godot tools/run_tests.sh [unit|acceptance|all]
set -euo pipefail
cd "$(dirname "$0")/.."
GODOT="${GODOT:-godot}"
MODE="${1:-all}"
"$GODOT" --headless --path . --import >/dev/null 2>&1 || true
if [[ "$MODE" == "unit" || "$MODE" == "all" ]]; then
  "$GODOT" --headless --path . res://tests/TestRunner.tscn
fi
if [[ "$MODE" == "acceptance" || "$MODE" == "all" ]]; then
  "$GODOT" --headless --path . res://tests/AcceptanceTest.tscn
fi
