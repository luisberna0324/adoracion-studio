#!/usr/bin/env bash
set -euo pipefail

CSV="${1:-}"
if [[ -z "${CSV}" || ! -f "${CSV}" ]]; then
  echo "Usage: $0 /path/to/audio_YYYYMMDD_HHMMSS.csv" >&2
  exit 1
fi

SORTED_BLOCKS="$(mktemp)"
trap 'rm -f "${SORTED_BLOCKS}"' EXIT

awk -F, 'NR > 1 { print $2 + 0.0 }' "${CSV}" |
  LC_ALL=C sort -n > "${SORTED_BLOCKS}"

awk -F, -v sorted_blocks="${SORTED_BLOCKS}" '
NR == 1 { next }
{
  ++n
  sum += $2 + 0.0
  budget = $3 + 0.0
  overruns += $4 + 0
  lock_ms += $5 + 0.0
  misses = $7 + 0
  if (seen) {
    misses_delta += misses >= previous_misses \
      ? misses - previous_misses : misses
  }
  previous_misses = misses
  final_misses = misses
  if (NF >= 9)
    missing_frames = $9 + 0
  if (seen) {
    missing_frames_delta += missing_frames >= previous_missing_frames \
      ? missing_frames - previous_missing_frames : missing_frames
  }
  previous_missing_frames = missing_frames
  final_missing_frames = missing_frames
  if (NF >= 10)
    reset_misses = $10 + 0
  if (seen) {
    reset_misses_delta += reset_misses >= previous_reset_misses \
      ? reset_misses - previous_reset_misses : reset_misses
  }
  previous_reset_misses = reset_misses
  final_reset_misses = reset_misses
  if (NF >= 11)
    service_failures = $11 + 0
  if (seen) {
    service_failures_delta += service_failures >= previous_service_failures \
      ? service_failures - previous_service_failures : service_failures
  }
  previous_service_failures = service_failures
  final_service_failures = service_failures
  seen = 1
}
END {
  if (n == 0) {
    print "No audio blocks found."
    exit
  }
  p95 = int((n * 95 + 99) / 100)
  if (p95 < 1) p95 = 1
  if (p95 > n) p95 = n
  sorted_count = 0
  while ((getline sorted_value < sorted_blocks) > 0) {
    ++sorted_count
    if (sorted_count == p95)
      p95_value = sorted_value + 0.0
    max_value = sorted_value + 0.0
  }
  close(sorted_blocks)
  printf("blocks=%.0f\n", n)
  printf("block_avg_ms=%.3f\n", sum / n)
  printf("block_p95_ms=%.3f\n", p95_value)
  printf("block_max_ms=%.3f\n", max_value)
  printf("last_budget_ms=%.3f\n", budget)
  printf("overruns=%.0f\n", overruns)
  printf("lock_wait_total_ms=%.3f\n", lock_ms)
  printf("streaming_misses_delta=%.0f\n", misses_delta)
  printf("streaming_missing_frames_delta=%.0f\n", missing_frames_delta)
  printf("time_processor_reset_misses_delta=%.0f\n", reset_misses_delta)
  printf("time_processor_service_failures_delta=%.0f\n",
         service_failures_delta)
  printf("streaming_misses_last_cumulative=%.0f\n", final_misses)
  printf("streaming_missing_frames_last_cumulative=%.0f\n",
         final_missing_frames)
  printf("time_processor_reset_misses_last_cumulative=%.0f\n",
         final_reset_misses)
  printf("time_processor_service_failures_last_cumulative=%.0f\n",
         final_service_failures)
}' "${CSV}"
