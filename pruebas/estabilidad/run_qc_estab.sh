#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/query_control" || exit 1

CFG="src/query_control.cfg"
S="../scripts"

TOTAL=25   # cantidad de repeticiones por script

echo "[INFO] Iniciando Query Control - Estabilidad"
echo "[INFO] Se lanzarán $TOTAL instancias de AGING_1..4 (Total = $((TOTAL*4)))"
echo ""

START_TS=$(date +%s)
START_DATE=$(date +"%H:%M:%S")

echo "[INFO] Hora de inicio: $START_DATE"
echo ""

PIDS=()

for ((i=1; i<=TOTAL; i++)); do
    ./bin/query_control "$CFG" "$S/AGING_1" 20 & PIDS+=($!)
    ./bin/query_control "$CFG" "$S/AGING_2" 20 & PIDS+=($!)
    ./bin/query_control "$CFG" "$S/AGING_3" 20 & PIDS+=($!)
    ./bin/query_control "$CFG" "$S/AGING_4" 20 & PIDS+=($!)
done

echo "[INFO] Esperando finalización de ${#PIDS[@]} procesos QC..."
wait

END_TS=$(date +%s)
END_DATE=$(date +"%H:%M:%S")
TOTAL_TIME=$((END_TS - START_TS))

echo ""
echo "============================================"
echo "[INFO] Todas las Query Control terminaron."
echo "[INFO] Hora de inicio : $START_DATE"
echo "[INFO] Hora de fin    : $END_DATE"
echo "[INFO] Tiempo total   : ${TOTAL_TIME} segundos"
echo "[INFO] Total QC corridas: ${#PIDS[@]}"
echo "============================================"
