#!/usr/bin/env python3
"""
Re-análisis con parser correcto.
Cada row con phase='' sub='' title=X hadPrev=Y marca el FIN del load de X.
Las stages ANTES de ese row son de X. Las stages DESPUÉS son del siguiente load.
"""
import csv, os, glob, re
from collections import defaultdict

LOG_DIR = os.path.expanduser("~/Library/Logs/MTControls")

def parse_csv_v3(path):
    rows = []
    with open(path) as f:
        reader = csv.reader(f)
        next(reader)
        for r in reader:
            if len(r) < 5: continue
            try:
                d = int(r[3])
                rss = int(r[4]) if r[4] not in ('', '-1') else -1
            except (ValueError, IndexError):
                continue
            rows.append({
                'wall': r[0], 'phase': r[1], 'sub': r[2],
                'us': d, 'rss': rss,
                'extra': r[5] if len(r) > 5 else '',
            })

    # find end markers: phase='' sub='' title=X hadPrev=Y
    boundaries = []
    for i, row in enumerate(rows):
        if (row['phase'] == '' and row['sub'] == '' and
            'title=' in row['extra'] and 'hadPrev=' in row['extra']):
            m = re.search(r'title=(\S+)', row['extra'])
            title = m.group(1) if m else '?'
            had_prev = 'hadPrev=1' in row['extra']
            boundaries.append((i, title, had_prev, row['us'], row['rss']))

    # For boundary k, the load that ENDED is title_bk
    # its stages are rows[boundaries[k-1].idx+1 ... boundaries[k].idx-1]
    # (k=0 has stages from 0 to boundaries[0]-1)
    # the song that STARTED after boundary k is the NEXT load (with title boundaries[k+1].title)
    songs = []
    for k, (idx, title, had_prev, total_us, rss) in enumerate(boundaries):
        start = boundaries[k-1][0] + 1 if k > 0 else 0
        stages = rows[start:idx]  # these are the stages of THIS load
        songs.append({
            'title': title, 'total_us': total_us, 'had_prev': had_prev,
            'rss': rss, 'stages': stages,
            'source_file': os.path.basename(path),
        })
    return songs, rows


def aggregate_per_song(songs):
    for s in songs:
        s['proj_open_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'project.open')
        # processor.loadSongStems is the outermost timer in PluginProcessor.cpp
        s['proc_load_song_stems_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'processor.loadSongStems')
        # loadSongStems is the phase for the per-stem sub-stages (cache.lookup, etc)
        s['stems_total_us'] = (s['proc_load_song_stems_us'] +
                               sum(x['us'] for x in s['stages'] if x['phase'] == 'loadSongStems'))
        s['settings_restore_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'settings.restore')
        s['bungee_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'bungee.prepare')
        s['per_stem_createReader_us'] = sum(x['us'] for x in s['stages'] if 'createReader' in x['sub'])
        s['per_stem_prepareToPlay_us'] = sum(x['us'] for x in s['stages'] if 'prepareToPlay' in x['sub'])
        s['per_stem_cache_lookup_us'] = sum(x['us'] for x in s['stages'] if 'cache.lookup' in x['sub'])
        s['per_stem_waitForReady_us'] = sum(x['us'] for x in s['stages'] if 'waitForReady' in x['sub'] and x['sub'] != 'waitForReady.total')
        s['fade_out_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'fadeOut.wait')
        s['suspend_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'suspendProcessing.wait')
        s['wait_ready_total_us'] = sum(x['us'] for x in s['stages'] if x['sub'] == 'waitForReady.total')
        s['size_bytes'] = 0
        s['n_stems'] = 0
        s['n_stages'] = len(s['stages'])
        s['flac_count'] = sum(1 for x in s['stages'] if 'cache.lookup' in x['sub'] and 'sourceFlac=1' in x['extra'])
        s['cache_hit_count'] = sum(1 for x in s['stages'] if 'cache.lookup' in x['sub'] and 'hit=1' in x['extra'])
        for x in s['stages']:
            if x['sub'] == 'project.open' and 'size=' in x['extra']:
                try: s['size_bytes'] = int(x['extra'].split('size=')[1].split(' ')[0])
                except: pass
            if x['sub'] == 'processor.loadSongStems' and 'stems=' in x['extra']:
                try: s['n_stems'] = int(x['extra'].split('stems=')[1].split(' ')[0])
                except: pass
        measured = s['proj_open_us'] + s['stems_total_us'] + s['settings_restore_us']
        s['unmeasured_us'] = max(0, s['total_us'] - measured)
        if s['proj_open_us'] > 0 and s['size_bytes'] > 0:
            s['class'] = 'COLD-REBUILD'
        elif s['proj_open_us'] == 0 and s['stems_total_us'] > 0:
            s['class'] = 'WARM-HIT'
        else:
            s['class'] = 'WARM-PARTIAL' if s['stems_total_us'] > 0 else 'UNKNOWN'
    return songs

def percentile(xs, p):
    if not xs: return 0
    xs = sorted(xs)
    k = (len(xs) - 1) * p / 100
    f = int(k); c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)

def main():
    files = sorted(glob.glob(os.path.join(LOG_DIR, 'song_load_*.csv')))
    print(f"Found {len(files)} song_load_*.csv files\n")
    all_songs = []
    global_by_sub = defaultdict(list)
    for f in files:
        songs, rows = parse_csv_v3(f)
        all_songs.extend(songs)
        for r in rows:
            global_by_sub[r['sub']].append(r['us'])

    all_songs = aggregate_per_song(all_songs)

    print("="*130)
    print("1) DISTRIBUCIÓN POR SUB-STAGE (microsegundos)")
    print("="*130)
    interesting = ['project.open', 'project.open.isDecodableFlac', 'processor.loadSongStems', 'settings.restore', 'bungee.prepare',
                   'waitForReady.total', 'suspendProcessing.wait', 'fadeOut.wait']
    print(f"  {'sub_phase':<28} {'count':>6} {'avg':>10} {'p50':>10} {'p90':>10} {'p99':>10} {'max':>10}")
    for s in sorted(interesting, key=lambda x: -sum(global_by_sub.get(x, []))):
        xs = global_by_sub.get(s, [])
        if not xs: continue
        avg = sum(xs)/len(xs)
        print(f"  {s:<28} {len(xs):>6} {avg:>9.0f}us {percentile(xs,50):>9.0f}us {percentile(xs,90):>9.0f}us {percentile(xs,99):>9.0f}us {max(xs):>9.0f}us")
    per_stem_sub = defaultdict(list)
    for s in all_songs:
        for st in s['stages']:
            mm = re.match(r'stem#\d+\.(\S+)', st['sub'])
            if mm: per_stem_sub[mm.group(1)].append(st['us'])
    print(f"\n  {'per-stem':<28} {'count':>6} {'avg':>10} {'p50':>10} {'p90':>10} {'p99':>10} {'max':>10}")
    for s in sorted(per_stem_sub.keys(), key=lambda x: -sum(per_stem_sub.get(x, []))):
        xs = per_stem_sub[s]
        if not xs: continue
        avg = sum(xs)/len(xs)
        print(f"  {s:<28} {len(xs):>6} {avg:>9.0f}us {percentile(xs,50):>9.0f}us {percentile(xs,90):>9.0f}us {percentile(xs,99):>9.0f}us {max(xs):>9.0f}us")

    print("\n" + "="*130)
    print("2) TOP 15 CARGAS MÁS LENTAS")
    print("="*130)
    print(f"  {'title':<25} {'class':<14} {'total':>9} {'proj.open':>10} {'stems':>8} {'restore':>9} {'unmeas':>8} {'#':>3} {'size':>10} {'hadPrev':>7}")
    for s in sorted(all_songs, key=lambda x: -x['total_us'])[:15]:
        size_mb = f"{s['size_bytes']/1024/1024:.0f}MB" if s['size_bytes'] else '-'
        print(f"  {s['title'][:25]:<25} {s['class']:<14} {s['total_us']/1000:>7.0f}ms {s['proj_open_us']/1000:>8.0f}ms {s['stems_total_us']/1000:>6.0f}ms {s['settings_restore_us']/1000:>7.0f}ms {s['unmeasured_us']/1000:>6.0f}ms {s['n_stems']:>3} {size_mb:>10} {s['had_prev']!s:>7}")

    print("\n" + "="*130)
    print("3) COLD vs WARM")
    print("="*130)
    by_class = defaultdict(list)
    for s in all_songs:
        by_class[s['class']].append(s)
    print(f"  {'class':<14} {'count':>5} {'avg total':>10} {'p50 total':>10} {'p90 total':>10} {'avg proj.open':>13} {'avg size':>10}")
    for cls in sorted(by_class.keys()):
        ss = by_class[cls]
        totals = [s['total_us'] for s in ss]
        po = [s['proj_open_us'] for s in ss if s['proj_open_us'] > 0]
        sizes = [s['size_bytes'] for s in ss if s['size_bytes'] > 0]
        avg_po = sum(po)/len(po) if po else 0
        avg_sz = sum(sizes)/len(sizes)/1024/1024 if sizes else 0
        print(f"  {cls:<14} {len(ss):>5} {sum(totals)/len(totals)/1000:>8.0f}ms {percentile(totals,50)/1000:>8.0f}ms {percentile(totals,90)/1000:>8.0f}ms {avg_po/1000:>11.0f}ms {avg_sz:>8.0f}MB")

    print("\n" + "="*130)
    print("4) CORRELACIÓN TAMAÑO .pwproj vs project.open DURATION (COLD-REBUILD)")
    print("="*130)
    cold = [s for s in all_songs if s['class'] == 'COLD-REBUILD' and s['size_bytes'] > 0 and s['proj_open_us'] > 0]
    if cold:
        print(f"  {'title':<25} {'size (MB)':>10} {'proj.open (ms)':>15} {'MB/s':>10}")
        for s in sorted(cold, key=lambda x: x['proj_open_us'], reverse=True)[:15]:
            mb = s['size_bytes']/1024/1024
            ms = s['proj_open_us']/1000
            rate = mb / (ms/1000) if ms > 0 else 0
            print(f"  {s['title'][:25]:<25} {mb:>9.0f} {ms:>14.0f} {rate:>9.1f}")
        if len(cold) >= 3:
            xs = [s['size_bytes']/1024/1024 for s in cold]
            ys = [s['proj_open_us']/1000 for s in cold]
            n = len(xs); mx = sum(xs)/n; my = sum(ys)/n
            cov = sum((x-mx)*(y-my) for x,y in zip(xs,ys))/n
            varx = sum((x-mx)**2 for x in xs)/n
            vary = sum((y-my)**2 for y in ys)/n
            r = cov / (varx**0.5 * vary**0.5) if varx>0 and vary>0 else 0
            slope = cov/varx if varx>0 else 0
            intercept = my - slope*mx
            print(f"\n  Linear fit: proj.open_ms ≈ {slope:.2f} * size_MB + {intercept:.0f}ms  (r²={r**2:.2f})")
            rates = [(s['size_bytes']/1024/1024 / (s['proj_open_us']/1000), s) for s in cold if s['proj_open_us']>0]
            rates.sort()
            print(f"\n  Peor tasa de extract: {rates[0][0]:.1f} MB/s  ({rates[0][1]['title']}, {rates[0][1]['size_bytes']/1024/1024:.0f}MB en {rates[0][1]['proj_open_us']/1000:.0f}ms)")
            print(f"  Mejor tasa de extract: {rates[-1][0]:.1f} MB/s  ({rates[-1][1]['title']}, {rates[-1][1]['size_bytes']/1024/1024:.0f}MB en {rates[-1][1]['proj_open_us']/1000:.0f}ms)")

    print("\n" + "="*130)
    print("5) ANOMALÍAS: 'unmeasured_us' alto")
    print("="*130)
    suspicious = sorted([s for s in all_songs if s['unmeasured_us'] > 1000], key=lambda x: -x['unmeasured_us'])[:10]
    print(f"  {'title':<25} {'total':>9} {'measured':>10} {'unmeasured':>11} {'%unmeas':>9}")
    for s in suspicious:
        meas = s['total_us'] - s['unmeasured_us']
        print(f"  {s['title'][:25]:<25} {s['total_us']/1000:>7.0f}ms {meas/1000:>8.0f}ms {s['unmeasured_us']/1000:>9.0f}ms {(s['unmeasured_us']/s['total_us']*100 if s['total_us'] else 0):>7.0f}%")

    print("\n" + "="*130)
    print("6) settings.restore EN EL TIEMPO (¿la mejora del 16/08 se mantiene?)")
    print("="*130)
    by_file = defaultdict(list)
    for s in all_songs:
        by_file[s['source_file']].append(s['settings_restore_us'])
    print(f"  {'source_file':<35} {'count':>5} {'avg':>10} {'p50':>10} {'max':>10}")
    for f in sorted(by_file.keys()):
        xs = [x for x in by_file[f] if x > 0]
        if not xs: continue
        print(f"  {f:<35} {len(xs):>5} {sum(xs)/len(xs)/1000:>8.0f}ms {percentile(xs,50)/1000:>8.0f}ms {max(xs)/1000:>8.0f}ms")

if __name__ == '__main__':
    main()
