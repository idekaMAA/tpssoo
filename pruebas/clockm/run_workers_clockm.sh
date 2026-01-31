#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Iniciando único WORKER (CLOCKM)"
cd "$ROOT/worker" || exit 1

CFG="../tests_configs/worker/worker_clock.cfg"

echo "[INFO] Lanzando WORKER (ID=1) con $CFG"
./bin/worker "$CFG" 1
