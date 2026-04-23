#!/usr/bin/env bash
# Experiment 1: 4-pixel/word packing vs current batched baseline.
#
# Usage (from Kria, inside a directory that contains both binaries and inputs):
#   ./scripts/run_exp1.sh [N_RUNS]
#
# Assumes:
#   - edge_detector_fpga        (baseline, 1 pixel / word)
#   - edge_detector_fpga_packed (packed, 4 pixels / word)
#   - input/bird.pgm, input/mona_lisa.pgm
#   - The currently loaded bitstream matches the binary being tested.
#
# IMPORTANT: Before running the packed section, load the packed bitstream via
#   sudo fpgautil -b build_packed/bitstream.bit
# and before running baseline, load the baseline bitstream accordingly.

set -euo pipefail

N_RUNS=${1:-5}
OUT_DIR="logs/exp1_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${OUT_DIR}"

IMAGES=("bird" "mona_lisa")

run_case() {
    local binary=$1
    local image=$2
    local tag=$3
    local out_image="output/${image}_${tag}.pgm"
    mkdir -p output

    for i in $(seq 1 "${N_RUNS}"); do
        local log="${OUT_DIR}/${tag}_${image}_run${i}.txt"
        echo "[${tag}] ${image} run ${i} -> ${log}"
        "${binary}" "input/${image}.pgm" "${out_image}" --low 20 --high 60 \
            > "${log}" 2>&1 || {
            echo "  FAILED, see ${log}"; exit 1;
        }
    done
}

echo "=== Baseline (A): edge_detector_fpga ==="
for img in "${IMAGES[@]}"; do
    run_case ./edge_detector_fpga "${img}" "A_baseline"
done

echo
echo "=== Packed   (B): edge_detector_fpga_packed ==="
echo "Make sure build_packed/bitstream.bit is loaded on FPGA before continuing."
read -r -p "Press Enter when packed bitstream is loaded..."

for img in "${IMAGES[@]}"; do
    run_case ./edge_detector_fpga_packed "${img}" "B_packed"
done

echo
echo "Done. Raw logs in ${OUT_DIR}"
echo
echo "Quick summary (FPGA round-trip us):"
printf "%-20s %-12s %-6s %-15s\n" "Variant" "Image" "Run" "RoundTrip(us)"
for f in "${OUT_DIR}"/*.txt; do
    rt=$(grep -E "FPGA round-trip:" "${f}" | awk '{print $3}')
    base=$(basename "${f}" .txt)
    variant=$(echo "${base}" | cut -d'_' -f1-2)
    image=$(echo "${base}" | cut -d'_' -f3)
    run=$(echo "${base}" | cut -d'_' -f4)
    printf "%-20s %-12s %-6s %-15s\n" "${variant}" "${image}" "${run}" "${rt:-NA}"
done
