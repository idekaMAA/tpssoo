#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Ejecutando Query Control (LRU)"
cd "$ROOT/query_control" || exit 1

CFG="src/query_control.cfg"
S="../scripts/MEMORIA_WORKER_2"

echo "[INFO] Ejecutando MEMORIA_WORKER (LRU)"
./bin/query_control "$CFG" "$S" 0

echo ""
echo "[INFO] Prueba LRU finalizada."
