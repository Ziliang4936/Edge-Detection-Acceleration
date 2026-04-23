#!/usr/bin/env bash
# Non-interactive Experiment 1 runner for Kria.
# Loads each bitstream via fpgautil (needs sudo), runs baseline and packed
# binaries over the same image set, captures all output for later analysis.
#
# Usage: bash run_exp1_auto.sh [N_RUNS]
#
# Assumes (on Kria, in ~/edge_project):
#   - bitstream_baseline.bit and bitstream_packed.bit exist
#   - edge_detector_fpga and edge_detector_fpga_packed exist
#   - input/bird.pgm and input/mona_lisa.pgm exist
#   - $SUDO_PASS is exported, OR sudo -n works without a password
#
# Result layout: logs/exp1_<timestamp>/{A_baseline,B_packed}_<image>_run<N>.txt

set -euo pipefail

N_RUNS=${1:-5}
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="logs/exp1_${TS}"
mkdir -p "${OUT_DIR}" output

SUDO_PASS=${SUDO_PASS:-ubuntu}

load_bitstream() {
    local bit=$1
    echo ">>> Loading bitstream: ${bit}"
    echo "${SUDO_PASS}" | sudo -S fpgautil -b "${bit}"
    # Small settle delay; fpgautil returns after FPGA is reconfigured but the
    # driver expects /dev nodes to re-appear cleanly.
    sleep 1
}

run_image() {
    local binary=$1
    local tag=$2
    local image=$3
    local out_pgm="output/${image}_${tag}.pgm"
    for i in $(seq 1 "${N_RUNS}"); do
        local log="${OUT_DIR}/${tag}_${image}_run${i}.txt"
        echo "    [${tag}] ${image} run ${i}"
        "./${binary}" "input/${image}.pgm" "${out_pgm}" --low 20 --high 60 \
            > "${log}" 2>&1
    done
}

echo "============================================================"
echo "Experiment 1: 4-pixel/word packing vs current batched"
echo "  Runs per case: ${N_RUNS}"
echo "  Output dir:    ${OUT_DIR}"
echo "============================================================"

echo ""
echo "=== A baseline (1 pixel / word) ==="
load_bitstream bitstream_baseline.bit
for img in bird mona_lisa; do
    run_image edge_detector_fpga A_baseline "${img}"
done

echo ""
echo "=== B packed (4 pixels / word) ==="
load_bitstream bitstream_packed.bit
for img in bird mona_lisa; do
    run_image edge_detector_fpga_packed B_packed "${img}"
done

echo ""
echo "=== Summary (FPGA round-trip, us) ==="
printf "%-12s %-12s %-5s %-15s %-15s\n" Variant Image Run RoundTrip Total
for f in "${OUT_DIR}"/*.txt; do
    rt=$(grep "FPGA round-trip:" "${f}" | awk '{print $3}')
    total=$(grep "Total elapsed:" "${f}" | awk '{print $3}')
    base=$(basename "${f}" .txt)
    variant=$(echo "${base}" | awk -F'_' '{print $1"_"$2}')
    image=$(echo "${base}" | awk -F'_' '{print $3}')
    run=$(echo "${base}" | awk -F'_' '{print $4}')
    printf "%-12s %-12s %-5s %-15s %-15s\n" "${variant}" "${image}" "${run}" "${rt:-NA}" "${total:-NA}"
done | tee "${OUT_DIR}/summary.txt"

echo ""
echo "Logs saved to: ${OUT_DIR}"
