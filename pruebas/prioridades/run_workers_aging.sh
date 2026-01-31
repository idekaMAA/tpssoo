#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a: $ROOT/worker"

cd "$ROOT/worker" || exit 1

CFG1="../tests_configs/planificacion/worker1.cfg"
CFG2="../tests_configs/planificacion/worker2.cfg"

echo "[INFO] Lanzando WORKER 1 → ID=1"
./bin/worker "$CFG1" 1 & PID1=$!

echo "[INFO] Lanzando WORKER 2 → ID=2"
./bin/worker "$CFG2" 2 & PID2=$!

echo "[INFO] Workers activos: $PID1 (id1), $PID2 (id2)"
wait
