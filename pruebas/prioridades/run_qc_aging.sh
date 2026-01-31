#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/query_control" || exit 1

CFG="src/query_control.cfg"
S="../scripts"

mkfifo /tmp/qc1_ready
mkfifo /tmp/qc2_ready
mkfifo /tmp/qc3_ready
mkfifo /tmp/qc4_ready

echo "[INFO] Lanzando AGING Queries en paralelo, pero con orden forzado"

( read < /tmp/qc1_ready ; ./bin/query_control "$CFG" "$S/AGING_1" 4 ) &
PID1=$!

( read < /tmp/qc2_ready ; ./bin/query_control "$CFG" "$S/AGING_2" 3 ) &
PID2=$!

( read < /tmp/qc3_ready ; ./bin/query_control "$CFG" "$S/AGING_3" 5 ) &
PID3=$!

( read < /tmp/qc4_ready ; ./bin/query_control "$CFG" "$S/AGING_4" 1 ) &
PID4=$!

echo "go" > /tmp/qc1_ready
sleep 0.05

echo "go" > /tmp/qc2_ready
sleep 0.05

echo "go" > /tmp/qc3_ready
sleep 0.05

echo "go" > /tmp/qc4_ready

wait

echo "[INFO] Aging finalizado"

