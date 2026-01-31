#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando STORAGE (CLOCKM)"
cd "$ROOT/storage" || exit 1

CFG="../tests_configs/worker/storage.cfg"

echo "[INFO] Ejecutando STORAGE con $CFG"
./bin/storage "$CFG"
