#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/storage"
cd "$ROOT/storage" || exit 1

CFG="../tests_configs/errores/storage.cfg"

echo "[INFO] Ejecutando STORAGE con config: $CFG"
./bin/storage "$CFG"
