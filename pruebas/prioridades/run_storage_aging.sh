#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/storage"

cd "$ROOT/storage" || exit 1

CFG="../tests_configs/planificacion/storage_nofresh.cfg"

echo "[INFO] Ejecutando STORAGE con $CFG"
./bin/storage "$CFG"
