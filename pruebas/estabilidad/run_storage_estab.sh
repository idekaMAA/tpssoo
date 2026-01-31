#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando STORAGE - Estabilidad"
cd "$ROOT/storage" || exit 1

CFG="../tests_configs/estabilidad/storage.cfg"

echo "[INFO] Ejecutando ./bin/storage $CFG"
./bin/storage "$CFG"
