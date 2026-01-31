#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/worker"
cd "$ROOT/worker" || exit 1

CFG="../tests_configs/errores/worker.cfg"

echo "[INFO] Lanzando Worker (ID=1) con $CFG"
./bin/worker "$CFG" 1
