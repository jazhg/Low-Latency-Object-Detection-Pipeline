#!/usr/bin/env sh
set -eu

cmake --preset release
cmake --build --preset release --target ring_benchmark
mkdir -p results
./build/release/ring_benchmark "${1:-10000000}" > results/ring_throughput.csv
