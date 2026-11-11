#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./run_phone_comparison.sh SOURCE [DURATION_SECONDS] [REPETITIONS]

Examples:
  ./run_phone_comparison.sh camera:0 30 5
  ./run_phone_comparison.sh camera:1 30 5

Use the camera:N source that corresponds to the phone's Continuity Camera.
The script alternates FIFO/latest order and saves raw, reproducible results.
EOF
}

if [[ $# -lt 1 || $# -gt 3 ]]; then
  usage >&2
  exit 2
fi

source_name=$1
duration_seconds=${2:-30}
repetitions=${3:-5}
binary=${LLVP_BINARY:-build/vision/bin/low_latency_detector}
model=${LLVP_MODEL:-models/ssdlite320.pt}
ops=${LLVP_OPS:-.venv/lib/python3.14/site-packages/torchvision/_C_stable.so}
timestamp=$(date -u +%Y%m%dT%H%M%SZ)
output=${LLVP_OUTPUT:-results/phone_comparison_${timestamp}.txt}

if [[ ! -x "$binary" ]]; then
  echo "error: detector executable not found: $binary" >&2
  echo "build the vision preset first (see README.md)" >&2
  exit 1
fi
if [[ ! -f "$model" ]]; then
  echo "error: model not found: $model" >&2
  exit 1
fi
if [[ ! -f "$ops" ]]; then
  echo "error: torchvision operations library not found: $ops" >&2
  exit 1
fi
if ! [[ "$duration_seconds" =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v value="$duration_seconds" 'BEGIN { exit !(value > 0) }'; then
  echo "error: duration must be a positive number" >&2
  exit 2
fi
if ! [[ "$repetitions" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: repetitions must be a positive integer" >&2
  exit 2
fi

mkdir -p "$(dirname "$output")"
{
  echo "# Low-latency phone camera comparison"
  echo "# utc_started=$timestamp source=$source_name duration_seconds=$duration_seconds repetitions=$repetitions"
  echo "# Keep the camera scene, lighting, model, hardware, and display setting fixed."
} > "$output"

run_trial() {
  local repetition=$1
  local mode=$2
  echo "Running repetition $repetition/$repetitions: $mode" >&2
  local result
  result=$("$binary" \
    --source "$source_name" \
    --model "$model" \
    --ops "$ops" \
    --duration-seconds "$duration_seconds" \
    "--$mode")
  printf 'repetition=%s %s\n' "$repetition" "$result" | tee -a "$output"
}

for ((repetition = 1; repetition <= repetitions; ++repetition)); do
  if ((repetition % 2 == 1)); then
    run_trial "$repetition" fifo
    run_trial "$repetition" latest
  else
    run_trial "$repetition" latest
    run_trial "$repetition" fifo
  fi
done

echo "Saved comparison to $output"
