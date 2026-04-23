# Milestone 5 — Experiment 1: 4-pixel/word Packing (2026-04-23)

Author: Ziliang
Status: C-sim **PASS** / HLS synthesis **PASS** / Vivado bitstream **PASS** / Kria hardware **HANG** (deferred)
Companion docs: [`io_top3_experiments.md`](io_top3_experiments.md), [`report_4.21_ZW.md`](report_4.21_ZW.md)

---

## 1. Goal

Shrink the CiFra OpenBus I/O bottleneck identified in `report_4.21_ZW.md` by packing **4 `uint8` pixels per 32-bit bus word**. Target: ~4x fewer AXI-Lite word transactions on the write and read paths, without changing the algorithm.

Theoretical gain (bird.pgm, 321x481):

| Path | Baseline words | Packed words | Word reduction |
|---|---|---|---|
| Input  (5 hdr + pixels)   | 5 + 154,401 = 154,406 | 5 + 38,961 = 38,966 | 3.96x |
| Output (3 hdr + pixels)   | 3 + 154,401 = 154,404 | 3 + 38,961 = 38,964 | 3.96x |

---

## 2. What was built

### 2.1 Host side (`src/kria/host_packed.cpp`)
- Per-row packing on TX: for each row, emit `ceil(width/4)` words, each holding up to 4 consecutive pixels (LSB is the earliest pixel, tail bytes zero-padded when width is not a multiple of 4).
- Symmetric per-row unpacking on RX.
- Instrumented `read_all()` to log `got / total` bytes on timeout (crucial for HW debugging).
- New Makefile target `edge_detector_fpga_packed` in `src/kria/Makefile`.

### 2.2 FPGA side (`src/dut_packed.cpp`)
- `read_image_packed()`: outer loop on `y`, inner pipelined loop on `w` in `[0, words_per_row)`, unpacks each 32-bit word into 4 bytes with bounds guards.
- `write_thresholded_stream_packed()`: uses an explicit `static Pixel row_buf[kMaxImageWidth]` + separate `fill_row` / `pack_row` loops (avoids lambdas / captured conditionals that first caused HLS scheduling trouble).
- Function `dut()` remains **non-DATAFLOW** (same structure as the working baseline): `read_header -> read_image_packed -> gaussian_blur_hw -> write_thresholded_stream_packed`.

### 2.3 C-simulation testbench (`src/dut_packed_csim.cpp`)
- Drives `dut()` in software with the exact packed wire protocol.
- Unpacks the FPGA's output and diffs it byte-by-byte against a pure-CPU Canny reference.
- **Pass**: word counts match exactly (input 38,966 / output 38,964) and the output image is bit-identical to the CPU reference modulo expected float rounding.

### 2.4 Build infrastructure
- `artifacts/build_dut.tcl`: accepts `DUT_SRC_NAME` env var so the same TCL can synthesize `dut.cpp` or `dut_packed.cpp`.
- `Makefile` (top-level): added phony target `bitstream_packed` that runs HLS + Vivado on `dut_packed.cpp` into an isolated `build_packed/` directory (does not clobber the baseline `build/`).

### 2.5 Automation scripts (`scripts/`)
- `lab_install_exp1.sh` — idempotent installer on the lab server: strips CRLF, backs up originals, stages patched Makefile / TCL / source files.
- `kria_install_exp1.sh` — deploys both baseline and packed bitstreams + both host binaries into `~/edge_project/` on the Kria board.
- `run_exp1_auto.sh` — non-interactive A/B runner: loads baseline bitstream, runs baseline, loads packed bitstream, runs packed, captures all stdout/stderr with timestamps.
- `kria_probe_cifra.sh` — driver sanity probe (confirms device nodes and kernel module state).

---

## 3. Results

### 3.1 C-simulation
```
== dut_packed C-sim ==
input words pushed : 38966
output words read  : 38964
pixel diff (max)   : 0   (byte-for-byte match vs CPU reference)
PASS
```

### 3.2 HLS synthesis (Vitis HLS 2025.1, `dut_packed.cpp`)
- No scheduling errors, no deadlock monitors triggered.
- Read / write / compute modules produced: `dut_dut_Pipeline_read_rows_read_words.v`, `dut_dut_Pipeline_pack_row.v`, etc.
- Fmax ~131 MHz (comparable to baseline's 135.86 MHz).

### 3.3 Vivado bitstream
- 0 errors / 0 warnings, `build_packed/bitstream.bit` produced.
- Deployed to Kria as `~/edge_project/bitstream_packed.bit`.

### 3.4 Kria hardware run — **FAILED (deadlock)**
With the packed bitstream loaded and `edge_detector_fpga_packed input/bird.pgm ...` executed:

```
[host] write_all(155864 bytes) OK
[host] read stalled after 0 / 155856 bytes
[host] errno=110 Connection timed out
```

Immediately swapping back to the baseline bitstream and running the unmodified `edge_detector_fpga` still passes — ruling out driver corruption or board damage.

### 3.5 Root-cause analysis so far
- Host successfully completes the full 155,864-byte write, meaning the DUT **is** draining its input FIFO (otherwise host would block after ~8,192 words, the FIFO depth implied by the 13-bit `occupancy` counter in `CifraOpenBus.v`).
- Host sees **zero** output words, meaning the DUT never raises `strmOut_TVALID`.
- Driver source (`/home/ubuntu/driver/cifra_openbus.c`) polls with a 5-s per-word timeout, which matches the observed host error (`ETIMEDOUT` after ~5 s).
- Inspecting `build_packed/hw_dut/dut.v` vs `build/hw_dut/dut.v`:
  - Baseline generates `dut_input_r_RAM_AUTO_1R1W.v` (an explicit input line buffer) plus `dut_Pipeline_read_rows_read_cols.v`.
  - Packed generates a different internal binding: no separate `input_r` BRAM, and a new dual-bank 17-bit-addressed storage (`dut_stream_ap_uint_32_0_stream_ap_uint_32_0_input*`) fed by `dut_Pipeline_read_rows_read_words.v`.
  - Shapes differ enough that the runtime `strmOut` handshake appears to stall on silicon even though C-sim is clean.

**Hypothesis**: a classic HLS pipeline handshake bug that only manifests in RTL — typically catchable by **HLS C/RTL cosim** (`-cosim`) or a Vivado ILA on `strmOut_TVALID` / `strmOut_TREADY`. Neither was run in this session.

---

## 4. Deliverables in this commit

```
src/
  dut_packed.cpp                   (new, 345 lines)
  dut_packed_csim.cpp              (new, C-sim testbench)
  kria/
    host_packed.cpp                (new, per-row packing host)
    Makefile                       (add edge_detector_fpga_packed target)
Makefile                           (add bitstream_packed target)
artifacts/
  build_dut.tcl                    (accept DUT_SRC_NAME env var)
scripts/
  lab_install_exp1.sh              (lab-side idempotent installer)
  kria_install_exp1.sh             (Kria-side idempotent installer)
  run_exp1_auto.sh                 (non-interactive A/B runner)
  kria_probe_cifra.sh              (driver sanity probe)
  run_exp1.sh                      (older interactive variant)
io_top3_experiments.md             (experiment catalogue + Exp 1 run steps)
report_4.23_packing.md             (this file)
```

---

## 5. Reproducibility — one-command path

On the lab server (`cs730-test8`), after `git pull`:
```bash
cd ~/Edge-Detection-Acceleration
bash scripts/lab_install_exp1.sh        # stage patched Makefile / TCL / sources
make bitstream                          # rebuild baseline (if needed)
make bitstream_packed                   # build packed bitstream
```

On the Kria board (`kria-4`, from lab via `ssh -i ~/.ssh/kria_key ubuntu@10.210.1.138`):
```bash
bash ~/kria_install_exp1.sh             # copies both bitstreams + both host binaries
cd ~/edge_project
bash run_exp1_auto.sh                   # runs A (baseline) then B (packed), logs everything
```

---

## 6. Next steps

Short-term (pick one):

1. **Ship Exp 3 (double-buffer throughput)** — host-side only, reuses the working baseline bitstream, produces a clean FPS metric for the Milestone 5 report.
2. **Unblock Exp 1** — run `vitis_hls` cosim on `dut_packed.cpp` to reproduce the deadlock in simulation, then fix (likely candidates: add `ARRAY_PARTITION` on `row_buf`, or split `write_thresholded_stream_packed` into a DATAFLOW region with explicit FIFO depth).
3. **Kernel-side burst** (Exp 2) — batch AXI-Lite word pushes in `cifra_openbus.c` to amortize the polling overhead while keeping the user-facing protocol unchanged.

Medium-term:
- Integrate Vivado ILA on `strmOut_TVALID` / `strmOut_TREADY` to catch this class of bug in-silicon next time.
- Once packing works on HW: re-measure end-to-end latency against baseline and update `io_top3_experiments.md` with numbers.
