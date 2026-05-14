#!/usr/bin/env bash
set -euo pipefail

# Profile PlayWorship on macOS while it runs.
# Captures periodic snapshots to CSV + optional stack samples.
#
# Usage:
#   scripts/profile_playworship_macos.sh
#   scripts/profile_playworship_macos.sh --name PlayWorship --interval 1 --duration 180 --out ./perf
#   scripts/profile_playworship_macos.sh --pid 12345 --sample-every 30

APP_NAME="PlayWorship"
PID=""
INTERVAL=1
DURATION=120
OUT_DIR="./perf"
SAMPLE_EVERY=30

while [[ $# -gt 0 ]]; do
  case "$1" in
    --name)
      APP_NAME="${2:-}"
      shift 2
      ;;
    --pid)
      PID="${2:-}"
      shift 2
      ;;
    --interval)
      INTERVAL="${2:-1}"
      shift 2
      ;;
    --duration)
      DURATION="${2:-120}"
      shift 2
      ;;
    --out)
      OUT_DIR="${2:-./perf}"
      shift 2
      ;;
    --sample-every)
      SAMPLE_EVERY="${2:-30}"
      shift 2
      ;;
    -h|--help)
      sed -n '1,30p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown arg: $1"
      exit 1
      ;;
  esac
done

if [[ -z "${PID}" ]]; then
  PID="$(pgrep -x "${APP_NAME}" | head -n1 || true)"
fi

if [[ -z "${PID}" ]]; then
  echo "Could not find process. Start ${APP_NAME} or pass --pid <PID>."
  exit 1
fi

if ! ps -p "${PID}" > /dev/null 2>&1; then
  echo "PID ${PID} is not running."
  exit 1
fi

TS="$(date +"%Y%m%d_%H%M%S")"
RUN_DIR="${OUT_DIR}/profile_${APP_NAME}_${TS}"
mkdir -p "${RUN_DIR}/samples"

CSV="${RUN_DIR}/metrics.csv"
LOG="${RUN_DIR}/run.log"

echo "pid=${PID}" | tee "${LOG}"
echo "app=${APP_NAME}" | tee -a "${LOG}"
echo "interval_sec=${INTERVAL}" | tee -a "${LOG}"
echo "duration_sec=${DURATION}" | tee -a "${LOG}"
echo "sample_every_sec=${SAMPLE_EVERY}" | tee -a "${LOG}"
echo "output=${RUN_DIR}" | tee -a "${LOG}"

echo "timestamp,elapsed_sec,cpu_pct,mem_mb,threads,wakeups,power_score" > "${CSV}"

START_EPOCH="$(date +%s)"
NEXT_SAMPLE_AT=$((START_EPOCH + SAMPLE_EVERY))

echo "Profiling started. Press Ctrl+C to stop early." | tee -a "${LOG}"

for ((elapsed=0; elapsed<=DURATION; elapsed+=INTERVAL)); do
  if ! ps -p "${PID}" > /dev/null 2>&1; then
    echo "Process exited at t=${elapsed}s" | tee -a "${LOG}"
    break
  fi

  NOW="$(date +"%Y-%m-%d %H:%M:%S")"

  # %cpu rss threads (rss in KB). Fallback for different macOS ps fields.
  PS_LINE="$(ps -p "${PID}" -o %cpu= -o rss= -o nlwp= 2>/dev/null | awk 'NR==1{print $1, $2, $3}' || true)"
  if [[ -z "${PS_LINE}" ]]; then
    PS_LINE="$(ps -p "${PID}" -o %cpu= -o rss= 2>/dev/null | awk 'NR==1{print $1, $2}' || true)"
    read -r CPU RSS <<<"${PS_LINE}"
    THREADS="na"
  else
    read -r CPU RSS THREADS <<<"${PS_LINE}"
  fi
  CPU="${CPU:-0}"
  RSS="${RSS:-0}"
  MEM_MB="$(awk -v rss="${RSS}" 'BEGIN { printf "%.2f", rss/1024.0 }')"

  # top output varies across macOS versions; parse wakeups defensively.
  TOP_LINE="$(top -pid "${PID}" -l 1 -stats pid,cpu,wq -n 0 2>/dev/null | awk -v pid="${PID}" '$1==pid {print $0}' || true)"
  WAKEUPS="$(echo "${TOP_LINE}" | awk '{print $3}' || true)"
  POWER=""
  WAKEUPS="${WAKEUPS:-0}"
  POWER="${POWER:-na}"

  echo "${NOW},${elapsed},${CPU:-0},${MEM_MB},${THREADS:-0},${WAKEUPS},${POWER}" >> "${CSV}"

  EPOCH_NOW="$(date +%s)"
  if [[ "${SAMPLE_EVERY}" -gt 0 && "${EPOCH_NOW}" -ge "${NEXT_SAMPLE_AT}" ]]; then
    SAMPLE_FILE="${RUN_DIR}/samples/sample_t${elapsed}s.txt"
    # 3s call stack sample. Needs no extra deps.
    sample "${PID}" 3 -file "${SAMPLE_FILE}" >/dev/null 2>&1 || true
    NEXT_SAMPLE_AT=$((EPOCH_NOW + SAMPLE_EVERY))
  fi

  sleep "${INTERVAL}"
done

SUMMARY="${RUN_DIR}/summary.txt"
awk -F, '
NR==1 { next }
{
  cpu[++n]=$3+0.0;
  mem[m++]=$4+0.0;
}
END {
  if (n==0) { print "No samples captured."; exit 0; }
  # sort cpu
  for (i=1;i<=n;i++) for (j=i+1;j<=n;j++) if (cpu[i] > cpu[j]) { t=cpu[i]; cpu[i]=cpu[j]; cpu[j]=t; }
  csum=0; cmax=cpu[n];
  for (i=1;i<=n;i++) csum+=cpu[i];
  cavg=csum/n;
  p95i=int((n*95+99)/100); if (p95i<1) p95i=1; if (p95i>n) p95i=n;
  cp95=cpu[p95i];

  # mem stats
  msum=0; mmax=0;
  for (i=0;i<m;i++) { msum+=mem[i]; if (mem[i]>mmax) mmax=mem[i]; }
  mavg=msum/m;

  printf("samples=%d\ncpu_avg=%.2f%%\ncpu_p95=%.2f%%\ncpu_max=%.2f%%\nmem_avg=%.2fMB\nmem_max=%.2fMB\n",
         n, cavg, cp95, cmax, mavg, mmax);
}' "${CSV}" > "${SUMMARY}"

STACK_SUMMARY="${RUN_DIR}/stack_summary.txt"
{
  echo "Top paint stack evidence:"
  rg -n "paintEntireComponent|paintComponentAndChildren|paintWithinParentContext|processBlock|WaveformBar|timerCallback" \
    "${RUN_DIR}/samples"/*.txt -S 2>/dev/null | head -n 120 || true
} > "${STACK_SUMMARY}"

echo "Done." | tee -a "${LOG}"
echo "CSV: ${CSV}" | tee -a "${LOG}"
echo "Samples: ${RUN_DIR}/samples" | tee -a "${LOG}"
echo "Summary: ${SUMMARY}" | tee -a "${LOG}"
echo "Stack summary: ${STACK_SUMMARY}" | tee -a "${LOG}"
