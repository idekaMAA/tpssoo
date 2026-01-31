#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/master"
cd "$ROOT/master" || exit 1

CFG="../tests_configs/errores/master.cfg"

echo "[INFO] Ejecutando MASTER con config: $CFG"
./bin/master "$CFG"
