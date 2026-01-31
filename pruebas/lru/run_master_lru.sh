#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando MASTER (LRU)"
cd "$ROOT/master" || exit 1

CFG="../tests_configs/worker/master.cfg"

echo "[INFO] Ejecutando MASTER con $CFG"
./bin/master "$CFG"
