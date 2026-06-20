#!/usr/bin/env bash
set -euo pipefail

CSV="${1:-}"
if [[ -z "${CSV}" || ! -f "${CSV}" ]]; then
  echo "Usage: $0 /path/to/audio_YYYYMMDD_HHMMSS.csv" >&2
  exit 1
fi

awk -F, '
NR == 1 { next }
{
  block[++n] = $2 + 0.0
  budget = $3 + 0.0
  overruns += $4 + 0
  lock_ms += $5 + 0.0
  final_misses = $7 + 0
}
END {
  if (n == 0) {
    print "No audio blocks found."
    exit
  }
  for (i = 1; i <= n; ++i)
    for (j = i + 1; j <= n; ++j)
      if (block[i] > block[j]) {
        t = block[i]; block[i] = block[j]; block[j] = t
      }
  sum = 0
  for (i = 1; i <= n; ++i) sum += block[i]
  p95 = int((n * 95 + 99) / 100)
  if (p95 < 1) p95 = 1
  if (p95 > n) p95 = n
  printf("blocks=%d\n", n)
  printf("block_avg_ms=%.3f\n", sum / n)
  printf("block_p95_ms=%.3f\n", block[p95])
  printf("block_max_ms=%.3f\n", block[n])
  printf("last_budget_ms=%.3f\n", budget)
  printf("overruns=%d\n", overruns)
  printf("lock_wait_total_ms=%.3f\n", lock_ms)
  printf("streaming_misses=%d\n", final_misses)
}' "${CSV}"
