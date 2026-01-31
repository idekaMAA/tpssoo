#!/bin/bash

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

echo "[INFO] Entrando a Query Control"
cd "$ROOT/query_control" || exit 1

CFG="src/query_control.cfg"
S="../scripts"

echo "[INFO] Ejecutando PRUEBA DE ERRORES en orden fijo"
echo ""

# 1) ESCRITURA_ARCHIVO_COMMITED
echo "[1/4] Ejecutando: ESCRITURA_ARCHIVO_COMMITED"
./bin/query_control "$CFG" "$S/ESCRITURA_ARCHIVO_COMMITED" 1
echo ""

# 2) FILE_EXISTENTE
echo "[2/4] Ejecutando: FILE_EXISTENTE"
./bin/query_control "$CFG" "$S/FILE_EXISTENTE" 1
echo ""

# 3) LECTURA_FUERA_DEL_LIMITE
echo "[3/4] Ejecutando: LECTURA_FUERA_DEL_LIMITE"
./bin/query_control "$CFG" "$S/LECTURA_FUERA_DEL_LIMITE" 1
echo ""

# 4) TAG_EXISTENTE
echo "[4/4] Ejecutando: TAG_EXISTENTE"
./bin/query_control "$CFG" "$S/TAG_EXISTENTE" 1
echo ""

echo "[INFO] Prueba de ERRORES finalizada."
