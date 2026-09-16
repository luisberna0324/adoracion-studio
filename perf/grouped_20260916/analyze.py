#!/usr/bin/env python3
"""
Compare two AudioDiagnostics captures.

The baseline is the per-stem ALABA capture from 2026-09-15 (`162701`),
which exercises the legacy per-stem Bungee loop with 19 stems at
tempo ratio 0.984252. The grouped capture is the same scenario after
the routing-grouped path of StemMixEngine is enabled.

Run with no args to use the bundled baseline (`baseline_alaba_125/`)
plus the most recent capture under `grouped_alaba_125/`. Pass folders
explicitly to compare other captures.

Outputs:
  - summary.txt  : high-level before/after deltas.
  - decode_render.csv : per-block stage_decode_render_us histogram.
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def pct(values, q):
    if not values:
        return 0.0
    s = sorted(values)
    idx = max(0, min(len(s) - 1, int(q / 100 * len(s))))
    return s[idx]


def block_stats(audio_csv: Path):
    """Return (avg, p90, p95, p99, max) for block_ms, overruns, streaming_misses."""
    with audio_csv.open() as f:
        rd = csv.DictReader(f)
        rows = [r for r in rd if r.get("is_playing") == "1"]

    bm = [float(r["block_ms"]) for r in rows]
    over = sum(1 for r in rows if r["overrun"] == "1")
    return {
        "n": len(rows),
        "block_ms": {
            "avg": statistics.mean(bm) if bm else 0.0,
            "p90": pct(bm, 90),
            "p95": pct(bm, 95),
            "p99": pct(bm, 99),
            "max": max(bm) if bm else 0.0,
        },
        "overruns_pct": (over * 100 / len(rows)) if rows else 0.0,
        "overruns": over,
        "streaming_misses_total": sum(int(r["streaming_misses"]) for r in rows),
        "stage_read_next": {
            "avg": statistics.mean([float(r["stage_read_next_us"]) for r in rows]) if rows else 0.0,
            "p99": pct([float(r["stage_read_next_us"]) for r in rows], 99),
        },
        "stage_decode_render": {
            "avg": statistics.mean([float(r["stage_decode_render_us"]) for r in rows]) if rows else 0.0,
            "p99": pct([float(r["stage_decode_render_us"]) for r in rows], 99),
        },
    }


def bungee_stats(stems_csv: Path):
    """Compute distinct blocks where tempo is active and how many
    Bungee voices were running per block (key metric for grouped path).
    """
    with stems_csv.open() as f:
        rd = csv.DictReader(f)
        rows = list(rd)

    tempo_rows = [r for r in rows if r.get("tempo_active") == "1"]
    grouped_rows = [r for r in rows if r.get("routing_grouped_eligible") == "1"]
    per_block = defaultdict(list)
    has_grouped_flag = "routing_grouped_eligible" in (rd.fieldnames or [])

    for r in tempo_rows:
        per_block[r["wall_ms"]].append(int(r["bungee_us"]))

    bungee_counts = [sum(1 for v in vs if v > 0) for vs in per_block.values()]
    bungee_durs = [v for vs in per_block.values() for v in vs if v > 0]
    ratios = sorted({r["tempo_ratio"] for r in tempo_rows})
    grouped_blocks = sum(1 for r in grouped_rows if int(r["bungee_count"]) > 0)

    return {
        "tempo_active_blocks": len(per_block),
        "bungee_per_block": {
            "avg": statistics.mean(bungee_counts) if bungee_counts else 0.0,
            "max": max(bungee_counts) if bungee_counts else 0,
            "min": min(bungee_counts) if bungee_counts else 0,
        },
        "bungee_per_stem_us": {
            "avg": statistics.mean(bungee_durs) if bungee_durs else 0.0,
            "max": max(bungee_durs) if bungee_durs else 0,
        },
        "tempo_ratio": ratios,
        "routing_grouped_supported": has_grouped_flag,
        "routing_grouped_eligible_blocks": grouped_blocks,
    }


def gaps_summary(gaps_csv: Path):
    if not gaps_csv.exists():
        return {"count": 0, "max_delta_ms": 0.0}
    with gaps_csv.open() as f:
        rd = csv.DictReader(f)
        rows = list(rd)
    deltas = [float(r["delta_ms"]) for r in rows]
    return {
        "count": len(rows),
        "max_delta_ms": max(deltas) if deltas else 0.0,
    }


def fmt(v, width=10):
    if isinstance(v, float):
        return f"{v:.3f}".rjust(width)
    return str(v).rjust(width)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=None)
    ap.add_argument("--candidate", default=None)
    args = ap.parse_args()

    here = Path(__file__).resolve().parent
    baseline_dir = Path(args.baseline) if args.baseline else here / "baseline_alaba_125"
    candidates = sorted([p.parent for p in here.glob("grouped_alaba_125/audio_stems_*.csv")])
    candidate_dirs = [Path(args.candidate)] if args.candidate else candidates
    if not candidate_dirs:
        candidate_dirs = [here / "grouped_alaba_125"]
    elif isinstance(candidate_dirs, list) and len(candidate_dirs) == 0:
        candidate_dirs = [here / "grouped_alaba_125"]

    outputs = []
    for cand_dir in candidate_dirs:
        cand_dir = Path(cand_dir)
        if not cand_dir.exists():
            print(f"# Skipping missing candidate {cand_dir}", file=sys.stderr)
            continue
        cand_audio = next(iter(sorted(cand_dir.glob("audio_*.csv"))), None)
        if cand_audio and not cand_audio.name.startswith("audio_stems"):
            # glob("audio_*.csv") matches both `audio_*` and `audio_stems_*`;
            # pick the strictly block-level one when possible.
            for cand in sorted(cand_dir.glob("audio_*.csv")):
                if cand.name.startswith("audio_stems_") or cand.name.startswith("audio_gaps_"):
                    continue
                cand_audio = cand
                break
        cand_stems = next(iter(cand_dir.glob("audio_stems_*.csv")), None)
        cand_gaps = next(iter(cand_dir.glob("audio_gaps_*.csv")), None)
        if not cand_audio or not cand_stems:
            print(f"# Skipping {cand_dir}: missing audio/*.csv or audio_stems/*.csv", file=sys.stderr)
            continue

        base_audio = None
        for cand in sorted(baseline_dir.glob("audio_*.csv")):
            if cand.name.startswith("audio_stems_") or cand.name.startswith("audio_gaps_"):
                continue
            base_audio = cand
            break
        base_stems = next(iter(baseline_dir.glob("audio_stems_*.csv")), None)
        base_gaps = next(iter(baseline_dir.glob("audio_gaps_*.csv")), None)

        base = {
            "block": block_stats(base_audio),
            "bungee": bungee_stats(base_stems),
            "gaps": gaps_summary(base_gaps),
        }
        cand = {
            "block": block_stats(cand_audio),
            "bungee": bungee_stats(cand_stems),
            "gaps": gaps_summary(cand_gaps),
        }

        lines = []
        lines.append(f"# Grouped-routing comparison")
        lines.append(f"baseline = {baseline_dir}")
        lines.append(f"candidate = {cand_dir}")
        lines.append("")
        lines.append("## Tempo scenario")
        lines.append(f"baseline tempo_ratio = {base['bungee']['tempo_ratio']}")
        lines.append(f"candidate tempo_ratio = {cand['bungee']['tempo_ratio']}")
        lines.append(
            f"baseline routing_grouped_eligible_blocks = "
            f"{base['bungee'].get('routing_grouped_eligible_blocks', '-')}"
            f" (flag present in CSV: {base['bungee']['routing_grouped_supported']})"
        )
        lines.append(
            f"candidate routing_grouped_eligible_blocks = "
            f"{cand['bungee'].get('routing_grouped_eligible_blocks', '-')}"
        )
        lines.append("")
        lines.append("## Callback block_ms (audio_*.csv)")
        for k in ("avg", "p90", "p95", "p99", "max"):
            v_b = base["block"]["block_ms"][k]
            v_c = cand["block"]["block_ms"][k]
            lines.append(f"  {k:>4}: baseline={fmt(v_b)}ms  candidate={fmt(v_c)}ms")
        lines.append(
            f"  overrun%: baseline={fmt(base['block']['overruns_pct'])} "
            f"candidate={fmt(cand['block']['overruns_pct'])}"
        )
        lines.append(
            f"  streaming_misses total: baseline={base['block']['streaming_misses_total']:,} "
            f"candidate={cand['block']['streaming_misses_total']:,}"
        )
        lines.append("")
        lines.append("## read_next stage us")
        for k in ("avg", "p99"):
            v_b = base["block"]["stage_read_next"][k]
            v_c = cand["block"]["stage_read_next"][k]
            lines.append(f"  {k:>4}: baseline={fmt(v_b)}us  candidate={fmt(v_c)}us")
        lines.append("")
        lines.append("## decode_render stage us")
        for k in ("avg", "p99"):
            v_b = base["block"]["stage_decode_render"][k]
            v_c = cand["block"]["stage_decode_render"][k]
            lines.append(f"  {k:>4}: baseline={fmt(v_b)}us  candidate={fmt(v_c)}us")
        lines.append("")
        lines.append("## Bungee per block (from audio_stems_*.csv)")
        bb = base["bungee"]["bungee_per_block"]
        cb = cand["bungee"]["bungee_per_block"]
        for k in ("avg", "min", "max"):
            lines.append(f"  {k:>4}: baseline={fmt(bb[k])}  candidate={fmt(cb[k])}")
        lines.append(
            f"  candidate grouping: {cand['bungee']['routing_grouped_eligible_blocks']}/"
            f"{cand['bungee']['tempo_active_blocks']} tempo-on blocks ran grouped"
        )
        lines.append("")
        lines.append("## Gaps (audio_gaps_*.csv)")
        lines.append(f"  count: baseline={base['gaps']['count']}  candidate={cand['gaps']['count']}")
        lines.append(
            f"  max delta_ms: baseline={fmt(base['gaps']['max_delta_ms'])} "
            f"candidate={fmt(cand['gaps']['max_delta_ms'])}"
        )

        out = cand_dir / "summary.txt"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(lines) + "\n")
        outputs.append(out)
        print("\n".join(lines))
        print()
        print(f"-> wrote {out}")

    if not outputs:
        print(
            "# No candidate directory had the required CSVs yet. "
            "Capture a new AudioDiagnostics run on the device into "
            "perf/grouped_20260916/grouped_alaba_125/ and re-run.",
            file=sys.stderr,
        )
        sys.exit(2)


if __name__ == "__main__":
    main()
