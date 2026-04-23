#!/usr/bin/env bash
# Idempotent installer run on the lab host to apply Experiment 1 changes.
# Usage: bash ~/lab_install_exp1.sh
set -euo pipefail

PROJ=${HOME}/Edge-Detection-Acceleration
cd "${PROJ}"

strip_crlf() {
    local f=$1
    if [ -f "${f}" ]; then
        tr -d '\r' < "${f}" > "${f}.lf"
        mv "${f}.lf" "${f}"
    fi
}

FILES=(
    "${PROJ}/src/dut_packed.cpp"
    "${PROJ}/src/kria/host_packed.cpp"
    "${PROJ}/scripts/run_exp1.sh"
    /tmp/build_dut_new.tcl
    /tmp/Makefile_new
    /tmp/kria_Makefile_new
)

echo "[1/4] Strip CRLF from staged files"
for f in "${FILES[@]}"; do strip_crlf "${f}"; done

echo "[2/4] Back up originals then install patched Makefile / tcl / kria Makefile"
[ -f Makefile.exp1.bak ] || cp Makefile Makefile.exp1.bak
[ -f artifacts/build_dut.tcl.exp1.bak ] || cp artifacts/build_dut.tcl artifacts/build_dut.tcl.exp1.bak
[ -f src/kria/Makefile.exp1.bak ] || cp src/kria/Makefile src/kria/Makefile.exp1.bak

cp /tmp/Makefile_new Makefile
cp /tmp/build_dut_new.tcl artifacts/build_dut.tcl
cp /tmp/kria_Makefile_new src/kria/Makefile

echo "[3/4] Make run_exp1.sh executable"
chmod +x "${PROJ}/scripts/run_exp1.sh"

echo "[4/4] Verification"
file Makefile artifacts/build_dut.tcl src/kria/Makefile src/dut_packed.cpp src/kria/host_packed.cpp scripts/run_exp1.sh
echo "--- Makefile targets ---"
grep -E '^[a-zA-Z_].*:' Makefile | head -n 20
echo "--- kria Makefile targets ---"
grep -E '^[a-zA-Z_].*:' src/kria/Makefile
echo "--- build_dut.tcl DUT_SRC_NAME ---"
grep -n 'DUT_SRC_NAME\|dut_src_name' artifacts/build_dut.tcl

echo "DONE"
