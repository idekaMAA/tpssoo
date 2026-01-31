#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando MASTER - Estabilidad"
cd "$ROOT/master" || exit 1

CFG="../tests_configs/estabilidad/master.cfg"

echo "[INFO] Ejecutando ./bin/master $CFG"
./bin/master "$CFG"
