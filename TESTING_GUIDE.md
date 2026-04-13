# Testing Guide (Local / Lab / Kira)

This is a short, practical guide for running baseline tests for `edge_detector`.

## 1) Baseline Definition

Baseline in current code (`src/main.cpp`) reports average time over 100 iterations for:

- Stage 3: Non-Maximum Suppression
- Stage 4: Double Threshold
- Stage 5: Hysteresis

## 2) Local Build and Run

### Windows (PowerShell)

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\edge_detector.exe input\bird.pgm output\bird_edges_test.pgm --low 40 --high 100 --kernel 7 --sigma 1.4
```

### Linux / WSL (bash)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/edge_detector input/bird.pgm output/bird_edges_test.pgm --low 40 --high 100 --kernel 7 --sigma 1.4
```

Expected output includes:

- `Baseline timing (stages 3-5, avg over 100 iterations)`
- per-stage timing in microseconds
- `Edge detection complete.`

## 3) Quick Baseline Procedure

Use a `Release` build and run each case at least 3 times.

Recommended cases:

- `input/bird.pgm`
- `input/mona_lisa.pgm`

Use fixed parameters for fair comparison:

- `--low 40 --high 100 --kernel 7 --sigma 1.4`

## 4) Lab Machine (Minimal Steps)

```bash
scp -r Edge-Detection-Acceleration <netid>@<lab_host>:~/
ssh <netid>@<lab_host>
cd ~/Edge-Detection-Acceleration
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/edge_detector input/bird.pgm output/bird_edges_lab.pgm --low 40 --high 100 --kernel 7 --sigma 1.4
```

Optional log capture:

```bash
mkdir -p logs
./build/edge_detector input/bird.pgm output/bird_edges_lab.pgm --low 40 --high 100 --kernel 7 --sigma 1.4 | tee logs/bird_lab_run1.txt
```

## 5) Kira (Minimal Steps)

```bash
rsync -av Edge-Detection-Acceleration/ <netid>@kira:~/Edge-Detection-Acceleration/
ssh <netid>@kira
cd ~/Edge-Detection-Acceleration
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/edge_detector input/bird.pgm output/bird_edges_kira.pgm --low 40 --high 100 --kernel 7 --sigma 1.4
```

If your environment requires a scheduler, submit the same run command in a job script.

## 6) Record Template

| Machine | Image | Run | NMS (us) | Thresh (us) | Hysteresis (us) | Total (us) |
|---|---|---:|---:|---:|---:|---:|
| Lab | bird | 1 | ... | ... | ... | ... |
| Lab | bird | 2 | ... | ... | ... | ... |
| Kira | bird | 1 | ... | ... | ... | ... |

## 7) Notes

- Keep parameters identical across machines.
- Compare averages, not a single run.
- Verify output images are generated before comparing timing.
