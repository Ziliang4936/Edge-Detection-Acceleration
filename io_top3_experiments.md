# I/O Top 3 Experiments Playbook (Milestone 5)

## Scope

Turn the next I/O optimizations into 3 runnable experiments with report-ready tables and conclusions.

## Common Setup

- **Platform**: Kria KV260, same clock/bitstream per comparison group.
- **Images**: `input/bird.pgm`, `input/mona_lisa.pgm` (same dataset for all runs).
- **Runs per case**: at least `N=5` (report mean and min/max).
- **Command log**: save each run output to `logs/*.txt`.
- **Build (host)**:

```bash
cd src/kria
make clean && make edge_detector_fpga
```

- **Run template**:

```bash
./edge_detector_fpga ../../input/bird.pgm ../../output/bird_edges_exp.pgm --low 20 --high 60
```

---

## Experiment 1: 4-pixel/word packing vs current batched

### Hypothesis

Packing 4 `uint8` pixels into one `uint32` should reduce transaction count to ~25% and cut FPGA write/read latency.

### A/B Definition

- **A (baseline)**: current batched path (`1 pixel -> 1 word`).
- **B (new)**: packed path (`4 pixels -> 1 word`) with matching unpack logic in FPGA-side protocol.

### What to measure

- `FPGA write (in)` us
- `FPGA read (out)` us
- `FPGA round-trip` us
- `Total elapsed` us
- Effective bandwidth (MB/s), computed from bytes/time

### Implementation files

- DUT (FPGA, HLS): `src/dut_packed.cpp` — parallel to `src/dut.cpp`, unpacks 4 pixels per input word and packs 4 pixels per output word.
- Host (Kria): `src/kria/host_packed.cpp` — parallel to `src/kria/host.cpp`, same timing output.
- Makefile: top-level `bitstream_packed` target builds into `build_packed/`.
- Host Makefile: `src/kria/Makefile` gained `edge_detector_fpga_packed` target.
- Runner: `scripts/run_exp1.sh` logs N runs per variant per image.

### Execution steps

1. On the build host (Vitis + Vivado):

```bash
cd Edge-Detection-Acceleration
make bitstream                 # baseline (1 pixel/word) -> build/bitstream.bit
make bitstream_packed          # packed   (4 pixels/word) -> build_packed/bitstream.bit
make deploy                    # copies baseline artifacts to kria_dir/
cp build_packed/bitstream.bit kria_dir/bitstream_packed.bit
```

2. On Kria (same machine or via rsync):

```bash
cd kria_dir
make edge_detector_fpga
make edge_detector_fpga_packed
```

3. Baseline run (A):

```bash
sudo fpgautil -b bitstream.bit
./scripts/run_exp1.sh 5        # stops halfway and prompts for bitstream swap
```

4. When the script pauses, load the packed bitstream and press Enter:

```bash
sudo fpgautil -b bitstream_packed.bit
```

5. Collect logs in `logs/exp1_YYYYMMDD_HHMMSS/` and fill the table below.

### Result table

| Image | Version | Run | Write us | Read us | Round-trip us | Total us |
|---|---|---:|---:|---:|---:|---:|
| bird | A | 1 |  |  |  |  |
| bird | B | 1 |  |  |  |  |
| mona | A | 1 |  |  |  |  |
| mona | B | 1 |  |  |  |  |

### Success criteria

- Output PGMs of A and B must be byte-identical (no regression in correctness).
- B should reduce `FPGA write (in)` bytes by ~4x and `FPGA round-trip` wall-clock latency by a meaningful amount (target: >= 1.5x on the same image + bitstream).

---

## Experiment 2: Driver burst vs current driver (host unchanged)

### Hypothesis

Keeping host binary unchanged and swapping in a burst-capable driver should reduce per-word overhead in kernel-space.

### A/B Definition

- **A (baseline driver)**: current `cifra_openbus.ko`.
- **B (burst driver)**: optimized driver with larger contiguous burst handling.
- **Host binary**: same exact executable for both A and B.

### What to measure

- Same timing fields as Experiment 1.
- Optional: CPU utilization during transfer.

### Execution

1. Build host once and keep binary checksum unchanged.
2. Load baseline driver A, run both images (`N=5` each), save logs.
3. Unload A, load burst driver B, run same command matrix, save logs.
4. Compare A vs B only on driver difference.

### Kernel module switch checklist

```bash
sudo rmmod cifra_openbus || true
sudo insmod ./cifra_openbus.ko
lsmod | grep cifra_openbus
dmesg | tail -n 30
```

### Result table

| Image | Driver | Run | Write us | Read us | Round-trip us | Total us |
|---|---|---:|---:|---:|---:|---:|
| bird | A | 1 |  |  |  |  |
| bird | B | 1 |  |  |  |  |
| mona | A | 1 |  |  |  |  |
| mona | B | 1 |  |  |  |  |

---

## Experiment 3: Double-buffer throughput (32/64 frames)

### Hypothesis

Double buffering overlaps host prep/transfer with FPGA compute; single-frame latency may not drop much, but throughput (FPS) should improve.

### A/B Definition

- **A**: current single-buffer pipeline.
- **B**: double-buffer pipeline.

### What to measure

- Total wall-clock for 32 frames and 64 frames.
- FPS = `frames / elapsed_seconds`.
- Mean per-frame time = `elapsed_ms / frames`.
- (Optional) p95 frame time if frame-level timestamps are available.

### Execution

1. Use one fixed image (`bird`), run A for 32 and 64 frames.
2. Use same conditions, run B for 32 and 64 frames.
3. Repeat each test 3 times.
4. Report mean FPS and variance.

### Throughput command template

```bash
time for i in $(seq 1 32); do \
  ./edge_detector_fpga ../../input/bird.pgm ../../output/bird_edges_db_$i.pgm --low 20 --high 60 >/tmp/run_$i.log; \
done
```

> Note: For strict double-buffer validation, use a dedicated multi-frame executable that keeps device handles open and schedules alternating buffers inside one process.

### Result table

| Version | Frames | Repeat | Elapsed s | FPS | Mean ms/frame |
|---|---:|---:|---:|---:|---:|
| A single-buffer | 32 | 1 |  |  |  |
| B double-buffer | 32 | 1 |  |  |  |
| A single-buffer | 64 | 1 |  |  |  |
| B double-buffer | 64 | 1 |  |  |  |

---

## Report-ready summary format

> "Under the same image set and threshold settings, experiment X improves `FPGA round-trip` by `Y%` and reduces total latency by `Z%` versus baseline. This indicates the dominant bottleneck is `____`, and the next priority is `____`."
