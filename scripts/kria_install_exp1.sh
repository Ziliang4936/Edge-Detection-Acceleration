#!/usr/bin/env bash
# Install Experiment 1 artifacts into ~/edge_project on Kria.
# Idempotent: keeps the existing baseline bitstream and host.cpp intact.
set -euo pipefail

PROJ=${HOME}/edge_project
STAGE=${HOME}/edge_project_staging

echo "[1/5] Backup current Makefile + host.cpp if not yet backed up"
cd "${PROJ}"
[ -f Makefile.exp1.bak ] || cp Makefile Makefile.exp1.bak
[ -f bitstream_baseline.bit ] || cp bitstream.bit bitstream_baseline.bit

echo "[2/5] Install packed bitstream"
cp "${STAGE}/bitstream.bit" "${PROJ}/bitstream_packed.bit"

echo "[3/5] Install packed host source + new Makefile with packed target"
cp "${STAGE}/host_packed.cpp" "${PROJ}/host_packed.cpp"
cp "${STAGE}/Makefile" "${PROJ}/Makefile"

echo "[4/5] Install run_exp1.sh"
mkdir -p "${PROJ}/scripts"
cp "${STAGE}/run_exp1.sh" "${PROJ}/scripts/run_exp1.sh"
chmod +x "${PROJ}/scripts/run_exp1.sh"

echo "[5/5] Build both host binaries"
cd "${PROJ}"
CXX=g++-13 make edge_detector_fpga
CXX=g++-13 make edge_detector_fpga_packed

echo ""
echo "=== SUMMARY ==="
ls -la "${PROJ}/bitstream.bit" "${PROJ}/bitstream_baseline.bit" "${PROJ}/bitstream_packed.bit" \
       "${PROJ}/edge_detector_fpga" "${PROJ}/edge_detector_fpga_packed" \
       "${PROJ}/scripts/run_exp1.sh"
echo "DONE"
