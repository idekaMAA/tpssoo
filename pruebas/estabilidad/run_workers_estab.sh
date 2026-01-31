#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT/worker" || exit 1

echo "[INFO] Iniciando Workers para Estabilidad"
echo "[INFO] ROOT = $ROOT"

# ================================
# 1. Workers 1 y 2
# ================================

echo "[INFO] Lanzando Workers 1 y 2..."

./bin/worker "../tests_configs/estabilidad/worker1.cfg" 1 & PID1=$!
./bin/worker "../tests_configs/estabilidad/worker2.cfg" 2 & PID2=$!

echo "[INFO] Workers 1 y 2 activos → $PID1, $PID2"
echo "[INFO] Esperando 35 segundos antes de iniciar Workers 3 y 4..."
sleep 35

# ================================
# 2. Workers 3 y 4
# ================================

echo "[INFO] Lanzando Workers 3 y 4..."

./bin/worker "../tests_configs/estabilidad/worker3.cfg" 3 & PID3=$!
./bin/worker "../tests_configs/estabilidad/worker4.cfg" 4 & PID4=$!

echo "[INFO] Workers 3 y 4 activos → $PID3, $PID4"
echo "[INFO] Esperando 35 segundos antes de finalizar Workers 1 y 2..."
sleep 35

# ================================
# 3. Finalizar Workers 1 y 2
# ================================

echo "[INFO] Terminando Workers 1 y 2 (PID $PID1 y $PID2)..."
kill -9 $PID1 2>/dev/null
kill -9 $PID2 2>/dev/null
echo "[INFO] Workers 1 y 2 finalizados."
echo "[INFO] Esperando 35 segundos antes de iniciar Workers 5 y 6..."
sleep 35

# ================================
# 4. Workers 5 y 6
# ================================

echo "[INFO] Lanzando Workers 5 y 6..."

./bin/worker "../tests_configs/estabilidad/worker5.cfg" 5 & PID5=$!
./bin/worker "../tests_configs/estabilidad/worker6.cfg" 6 & PID6=$!

echo "[INFO] Workers 5 y 6 activos → $PID5, $PID6"

echo "[INFO] Todos los Workers fueron lanzados según la secuencia de Estabilidad."
wait
