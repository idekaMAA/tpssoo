#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Ejecutando Query Control - Prueba Storage"
cd "$ROOT/query_control" || exit 1

CFG="src/query_control.cfg"
S="../scripts"

echo "[INFO] [1/5] STORAGE_1 (prio 0)"
./bin/query_control "$CFG" "$S/STORAGE_1" 0
echo

echo "[INFO] [2/5] STORAGE_2 (prio 2)"
./bin/query_control "$CFG" "$S/STORAGE_2" 2
echo

echo "[INFO] [3/5] STORAGE_3 (prio 4)"
./bin/query_control "$CFG" "$S/STORAGE_3" 4
echo

echo "[INFO] [4/5] STORAGE_4 (prio 6)"
./bin/query_control "$CFG" "$S/STORAGE_4" 6
echo

echo "[INFO] Prueba Storage finalizada."
