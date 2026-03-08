#!/bin/bash

BINARY="../build/phase3/phase3"
DATASET="../dataset/nyc_311_2020_2026.csv"
RUNS=10
LOG="benchmark_results.txt"

echo "Phase 3 Benchmark — $RUNS runs" | tee "$LOG"
echo "Binary : $BINARY"               | tee -a "$LOG"
echo "Dataset: $DATASET"              | tee -a "$LOG"
echo "Date   : $(date)"               | tee -a "$LOG"
echo "----------------------------------------" | tee -a "$LOG"

for i in $(seq 1 $RUNS); do
    echo ""                | tee -a "$LOG"
    echo "=== Run $i ===" | tee -a "$LOG"
    "$BINARY" "$DATASET"  | tee -a "$LOG"
    sleep 5
done

echo ""                                    | tee -a "$LOG"
echo "========================================" | tee -a "$LOG"
echo "Summary by thread count (ms)"         | tee -a "$LOG"
echo ""                                    | tee -a "$LOG"

for entry in "load:4" "searchByZip:8" "searchByDate:8" "searchByBoundingBox:8"; do
    op="${entry%%:*}"
    T="${entry##*:}"
    grep "\[${op}|T=${T}\]" "$LOG" | awk '{print $(NF-1)}' | awk -v op="$op" -v t="$T" '
    BEGIN { sum=0; min=1e18; max=0; n=0 }
    {
        val = $1+0
        sum += val; n++
        if (val < min) min = val
        if (val > max) max = val
    }
    END {
        if (n > 0) {
            avg = sum/n
            printf "    %-22s  T=%s  runs=%d  avg=%10.2f ms  min=%10.2f ms  max=%10.2f ms\n", op, t, n, avg, min, max
        }
    }' | tee -a "$LOG"
done
