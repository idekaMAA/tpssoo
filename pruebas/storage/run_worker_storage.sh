#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando WORKER - Prueba Storage"
cd "$ROOT/worker" || exit 1

CFG="../tests_configs/storage/worker.cfg"

echo "[INFO] Ejecutando ./bin/worker $CFG ID=1"
./bin/worker "$CFG" 1
