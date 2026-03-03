#!/usr/bin/env bash
# End-to-end test: run the full pipeline (preprocessor → cadvis SVG renderer)
# on all three test logs and verify the expected outputs are produced.
# Requires: pipenv environment installed, ./build/cadvis compiled.
# Exit code 0 = all checks passed, non-zero = failure.
set -euo pipefail
cd "$(dirname "$0")/.."

CADVIS=./build/cadvis
OUTDIR=$(mktemp -d)
trap 'rm -rf "$OUTDIR"' EXIT

echo "=== e2e: 4gp_log ==="
pipenv run python preprocess/preprocess.py \
    --log test_logs/4gp_log.jsonl \
    --output "$OUTDIR/4gp.cadvis"
"$CADVIS" --all --output-dir "$OUTDIR/4gp/" "$OUTDIR/4gp.cadvis"
# Verify expected SVG files exist
for comp in G1 G2 G3 G4 P; do
    [[ -f "$OUTDIR/4gp/$comp.svg" ]] || { echo "FAIL: missing $comp.svg"; exit 1; }
    grep -q "<svg" "$OUTDIR/4gp/$comp.svg"  || { echo "FAIL: $comp.svg is not valid SVG"; exit 1; }
done
echo "  OK: 5 SVGs produced"

echo "=== e2e: counter_log ==="
pipenv run python preprocess/preprocess.py \
    --log test_logs/counter_log.jsonl \
    --output "$OUTDIR/counter.cadvis"
"$CADVIS" --all --output-dir "$OUTDIR/counter/" "$OUTDIR/counter.cadvis"
[[ -f "$OUTDIR/counter/Counter.svg" ]] || { echo "FAIL: missing Counter.svg"; exit 1; }
echo "  OK: Counter.svg produced"

echo "=== e2e: job_tracker_log (with --map) ==="
pipenv run python preprocess/preprocess.py \
    --log test_logs/job_tracker_log.jsonl \
    --map examples/job_tracker_map.py \
    --output "$OUTDIR/jt.cadvis"
"$CADVIS" --all --output-dir "$OUTDIR/jt/" "$OUTDIR/jt.cadvis"
for comp in S1 S2 S3 S4; do
    [[ -f "$OUTDIR/jt/$comp.svg" ]] || { echo "FAIL: missing $comp.svg"; exit 1; }
done
echo "  OK: 4 scorer SVGs produced"

echo "=== e2e: --component flag ==="
"$CADVIS" --component G1 --output-dir "$OUTDIR/single/" "$OUTDIR/4gp.cadvis"
[[ -f "$OUTDIR/single/G1.svg" ]] || { echo "FAIL: missing G1.svg from --component"; exit 1; }
[[ ! -f "$OUTDIR/single/G2.svg" ]] || { echo "FAIL: G2.svg should not exist with --component G1"; exit 1; }
echo "  OK: --component flag works"

echo "=== All e2e checks passed ==="
