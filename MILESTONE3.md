# Milestone 3 — HLS Optimization & Host I/O Batching

## What I did

1. **Refactored `src/dut.cpp`** into a 5-stage `#pragma HLS DATAFLOW` pipeline
   (read → Gaussian → Sobel → NMS+threshold → write), replacing full-image BRAMs
   with small line buffers (5 rows for Gaussian, 3 rows for Sobel/NMS).
2. **Added a `dut_csim` target** to `Makefile` so HLS C-simulation can verify
   functional correctness against the software reference before each synthesis.
3. **Rebuilt the bitstream** for the DATAFLOW design on Kria (`xck26`) and
   deployed it via `fpgautil -b`.
4. **Re-batched host I/O** in `src/kria/host.cpp`: packed the 5 header words
   plus all pixels into a single `std::vector<uint32_t>` and pushed them through
   the CiFra OpenBus device in **one** `write()` syscall (and symmetrically one
   `read()` for the response). The previous path issued one syscall per pixel.
5. **Added `std::chrono` instrumentation** to `host.cpp` to break the
   end-to-end latency into: PGM read, TX packing, device open, FPGA write, FPGA
   read, hysteresis, PGM write.
6. **Rebuilt the driver `cifra_openbus.ko`** against the live Kria kernel
   (`6.8.0-1024-xilinx`) after discovering the pre-installed module was compiled
   for `-1023-xilinx` and no longer loaded.

## Results (Kria Cortex-A53 @ 1.33 GHz, bird.pgm 321×481, mona_lisa.pgm 250×360)

### bird.pgm — full pipeline, per-stage µs

| Stage                | CPU baseline | FPGA, per-word I/O | FPGA, batched I/O |
|----------------------|-------------:|-------------------:|------------------:|
| 1 Gaussian blur      |       19 758 |                  — |                 — |
| 2 Sobel gradients    |       39 610 |                  — |                 — |
| 3 NMS                |        5 334 |                  — |                 — |
| 4 Double threshold   |        2 063 |                  — |                 — |
| Stages 1–4 (FPGA)    |            — |        (in RT)     |        (in RT)    |
| FPGA write (host→IP) |            — |            267 219 |        **66 024** |
| FPGA read  (IP→host) |            — |            297 384 |       **104 166** |
| FPGA round-trip      |            — |            564 603 |       **170 191** |
| 5 Hysteresis (CPU)   |        3 817 |              4 198 |             5 043 |
| **Total**            |   **70 582** |        **572 507** |       **181 674** |

### mona_lisa.pgm — totals

| Metric               | CPU baseline | FPGA, per-word | FPGA, batched |
|----------------------|-------------:|---------------:|--------------:|
| FPGA round-trip (µs) |            — |        329 631 |   **99 193**  |
| Total (µs)           |   **40 759** |        333 993 |  **106 343**  |

### Speed-up summary

| Image | Path            | End-to-end (µs) | vs per-word baseline | vs ARM CPU |
|-------|-----------------|----------------:|---------------------:|-----------:|
| bird  | Per-word I/O    |         572 507 |                 1.0× |     0.12×  |
| bird  | **Batched I/O** |     **181 674** |            **3.15×** |    0.39×   |
| mona  | Per-word I/O    |         333 993 |                 1.0× |     0.12×  |
| mona  | **Batched I/O** |     **106 343** |            **3.14×** |    0.38×   |

### Effective CiFra OpenBus bandwidth

| Direction | Per-word | Batched |
|-----------|---------:|--------:|
| Host → IP |  2.3 MB/s | **9.4 MB/s** |
| IP → Host |  2.0 MB/s | **5.9 MB/s** |

### Syscall count (bird)

| Path     | Writes |  Reads  |
|----------|-------:|--------:|
| Per-word | 154 406|  154 404|
| Batched  |  **1** |   **1** |

## HLS synthesis: DATAFLOW + line buffers

| Metric (@100 MHz, `xck26`) | Original | DATAFLOW + LineBuf |
|----------------------------|---------:|-------------------:|
| Reported pipeline latency  | > 20 ms (Gaussian alone) | **6.89 ms** (full pipeline) |
| BRAM utilization           | High (two full-image buffers) | **−85 %** |
| DSP utilization            | Low      |       Higher      |
| HLS C-simulation           | ✓ pass (6/154 401 fp rounding diffs) | ✓ pass (same 6 diffs) |
| Hardware behaviour         | Works    | **Hangs — AXI-Stream tready stays low** |

The DATAFLOW bitstream passes C-simulation but hangs on real hardware (FPGA
refuses to accept even the first 32-bit word). The kept artefact is in
`src/dut.cpp` (original restored for the currently deployed bitstream); the
DATAFLOW version is preserved for future debugging.

## Bottleneck breakdown (bird, after batching)

```
total round-trip  170 ms
  ├─ FPGA compute (HLS report)     ~ 20 ms  (12 %)
  └─ CiFra OpenBus I/O            ~150 ms  (88 %)
```

Even after removing per-pixel syscall overhead, the CiFra driver still issues
one 32-bit AXI transaction per word internally, so effective bandwidth is
~9 MB/s — two orders of magnitude below the AXI-Stream theoretical limit
(400 MB/s @ 100 MHz × 32-bit). This is now the dominant bottleneck; further
kernel-side optimisation (DATAFLOW, 1-D separable Gaussian, etc.) offers
diminishing returns until the bus-transaction rate is attacked (e.g. mmap +
NEON stores, or packing 4 pixels per 32-bit word).

## Files touched

| File | Change |
|------|--------|
| `src/kria/host.cpp` | Batched `write_all` / `read_all`; end-to-end `std::chrono` timing. |
| `src/dut.cpp`       | DATAFLOW + line-buffer refactor (kept as artefact; deployed bitstream uses pre-existing design). |
| `Makefile`          | Added `dut_csim` target for HLS C-simulation. |
