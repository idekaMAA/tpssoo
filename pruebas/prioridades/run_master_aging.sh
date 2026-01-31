#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/master"

cd "$ROOT/master" || exit 1

CFG="../tests_configs/planificacion/master_prio.cfg"

echo "[INFO] Ejecutando MASTER con $CFG"
./bin/master "$CFG"
