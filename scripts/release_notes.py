#!/usr/bin/env python3
"""Generate editorial release files, a lightweight catalog and independent status."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAX_HISTORY = 12
MAX_DOCUMENT_BYTES = 64 * 1024
PLATFORMS = {'windows', 'macos', 'android', 'ios'}
CHANNELS = {'windows-installer': 'windows', 'macos-installer': 'macos',
            'android-apk': 'android', 'mac-app-store': 'macos', 'ios-app-store': 'ios'}


def version_key(value):
    if not isinstance(value, str) or not re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)', value):
        raise ValueError('Expected stable SemVer')
    return tuple(map(int, value.split('.')))


def text(value, maximum=4000):
    if not isinstance(value, str) or not value.strip() or len(value) > maximum:
        raise ValueError('Missing or oversized release text')
    return value


def platform_list(value):
    if not isinstance(value, list) or not value or any(not isinstance(p, str) or p not in PLATFORMS for p in value) or len(set(value)) != len(value):
        raise ValueError('Invalid platforms')
    return value


def bounded(value):
    if len(json.dumps(value).encode()) > MAX_DOCUMENT_BYTES:
        raise ValueError('Document exceeds size limit')
    return value


def validate(document, expected=None):
    if not isinstance(document, dict) or type(document.get('schemaVersion')) is not int or document['schemaVersion'] != 2:
        raise ValueError('Unsupported editorial schema')
    version_key(document.get('version'))
    if expected and document['version'] != expected:
        raise ValueError('Document version differs from pipeline version')
    if document.get('status') != 'released':
        raise ValueError('Document is a draft; approve it before publishing')
    datetime.strptime(document.get('publishedAt', ''), '%Y-%m-%dT%H:%M:%SZ')
    platform_list(document.get('platforms'))
    text(document.get('title'), 180)
    text(document.get('summary'), 600)
    highlights = document.get('highlights')
    if not isinstance(highlights, list) or not 1 <= len(highlights) <= 3:
        raise ValueError('Expected 1 to 3 short highlights')
    for item in highlights:
        text(item, 180)
    changes = document.get('changes')
    if not isinstance(changes, list) or not 1 <= len(changes) <= 12:
        raise ValueError('Invalid change groups')
    for group in changes:
        if not isinstance(group, dict):
            raise ValueError('Invalid change group')
        text(group.get('label'), 160)
        items = group.get('items')
        if not isinstance(items, list) or not 1 <= len(items) <= 30:
            raise ValueError('Invalid change items')
        for item in items:
            text(item)
    result = {key: document[key] for key in ['schemaVersion', 'version', 'status',
        'publishedAt', 'platforms', 'title', 'summary', 'highlights']}
    result['changes'] = [{'label': g['label'], 'items': g['items']} for g in changes]
    return bounded(result)


def validate_status(value, expected=None):
    if not isinstance(value, dict) or type(value.get('schemaVersion')) is not int or value['schemaVersion'] != 1:
        raise ValueError('Unsupported status schema')
    version_key(value.get('version'))
    if expected and value['version'] != expected:
        raise ValueError('Status version differs from requested release')
    datetime.strptime(value.get('verifiedOn', ''), '%Y-%m-%d')
    channels = value.get('distribution')
    if not isinstance(channels, list) or not 1 <= len(channels) <= len(CHANNELS):
        raise ValueError('Invalid distribution')
    seen, result = set(), []
    for channel in channels:
        if not isinstance(channel, dict):
            raise ValueError('Invalid channel')
        name = channel.get('channel')
        if not isinstance(name, str) or name not in CHANNELS or name in seen:
            raise ValueError('Unknown or duplicate channel')
        seen.add(name)
        if channel.get('status') not in ['available', 'pending_review', 'in_review', 'processing', 'unavailable']:
            raise ValueError('Invalid channel status')
        if 'build' in channel and (not isinstance(channel['build'], str) or not re.fullmatch(r'[1-9]\d*', channel['build'])):
            raise ValueError('Invalid build')
        if 'reviewSubmitted' in channel and not isinstance(channel['reviewSubmitted'], bool):
            raise ValueError('Invalid review flag')
        if channel['status'] in ['pending_review', 'in_review'] and channel.get('reviewSubmitted') is False:
            raise ValueError('Pending review requires a submitted review')
        if 'releaseType' in channel and channel['releaseType'] not in ['manual', 'automatic']:
            raise ValueError('Invalid release type')
        result.append({key: channel[key] for key in ['channel', 'status', 'build', 'reviewSubmitted', 'releaseType'] if key in channel})
    return bounded({'schemaVersion': 1, 'version': value['version'], 'verifiedOn': value['verifiedOn'], 'distribution': result})


def summary_for(document):
    doc = validate(document)
    return {**{key: doc[key] for key in ['version', 'publishedAt', 'platforms', 'title', 'summary', 'highlights']},
            'detailsFile': f"versions/{doc['version']}.json", 'statusFile': f"status/{doc['version']}.json"}


def validate_summary(value):
    if not isinstance(value, dict):
        raise ValueError('Invalid summary')
    version_key(value.get('version'))
    datetime.strptime(value.get('publishedAt', ''), '%Y-%m-%dT%H:%M:%SZ')
    platform_list(value.get('platforms'))
    text(value.get('title'), 180)
    text(value.get('summary'), 600)
    highlights = value.get('highlights')
    if not isinstance(highlights, list) or not 1 <= len(highlights) <= 3:
        raise ValueError('Invalid highlights')
    for item in highlights:
        text(item, 180)
    for key, folder in [('detailsFile', 'versions'), ('statusFile', 'status')]:
        if value.get(key) != f"{folder}/{value['version']}.json":
            raise ValueError('Invalid relative release path')
    return {key: value[key] for key in ['version', 'publishedAt', 'platforms', 'title', 'summary', 'highlights', 'detailsFile', 'statusFile']}


def make_index(document, previous=None):
    entries = {}
    if previous is not None:
        if not isinstance(previous, dict) or previous.get('schemaVersion') != 2 or not isinstance(previous.get('releases'), list) or len(previous['releases']) > MAX_HISTORY:
            raise ValueError('Invalid previous lightweight index')
        for item in previous['releases']:
            item = validate_summary(item)
            if item['version'] in entries:
                raise ValueError('Duplicate release')
            entries[item['version']] = item
    item = summary_for(document)
    entries[item['version']] = item
    ordered = sorted(entries.values(), key=lambda item: version_key(item['version']), reverse=True)
    return {'schemaVersion': 2, 'releases': ordered[:MAX_HISTORY]}


def render(document):
    doc = validate(document)
    lines = [f"# PlayWorship {doc['version']}", '', doc['title'], '', doc['summary'], '']
    for group in doc['changes']:
        lines += [f"## {group['label']}", ''] + [f'- {item}' for item in group['items']] + ['']
    return '\n'.join(lines)


def read_json(folder, version, ref=None, source_dir=None, allow_legacy=False):
    key = version_key(version)
    try:
        raw = subprocess.check_output(['git', '-C', str(ROOT), 'show', f'{ref}:{folder}/{version}.json'], stderr=subprocess.PIPE) if ref else ((source_dir or ROOT / folder) / f'{version}.json').read_bytes()
    except (FileNotFoundError, subprocess.CalledProcessError):
        if allow_legacy and key < (2, 4, 0):
            return None
        raise ValueError(f'Missing {folder}/{version}.json in selected source') from None
    if len(raw) > MAX_DOCUMENT_BYTES:
        raise ValueError('Document exceeds size limit')
    return json.loads(raw)


def load(version, ref=None, source_dir=None, allow_legacy=False):
    value = read_json('release-notes', version, ref, source_dir, allow_legacy)
    return validate(value, version) if value is not None else None


def write_outputs(document, status, output, previous=None):
    document, status = validate(document), validate_status(status, document['version'])
    if any(CHANNELS[c['channel']] not in document['platforms'] for c in status['distribution']):
        raise ValueError('Status includes a platform outside the editorial document')
    index = make_index(document, previous)
    write_json(output / 'release-notes.json', document)
    write_json(output / f"versions/{document['version']}.json", document)
    write_status(status, output)
    write_json(output / 'index.json', index)
    write_json(output / 'latest.json', {'schemaVersion': 2, 'release': index['releases'][0]})
    (output / 'notes.md').write_text(render(document), encoding='utf-8')


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + '\n')


def write_status(status, output):
    status = validate_status(status)
    write_json(output / 'release-status.json', status)
    write_json(output / f"status/{status['version']}.json", status)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--ref')
    parser.add_argument('--source-dir', type=Path)
    parser.add_argument('--status-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--previous-index', type=Path)
    parser.add_argument('--previous-status', type=Path)
    parser.add_argument('--status-only', action='store_true')
    parser.add_argument('--allow-legacy', action='store_true')
    args = parser.parse_args()
    try:
        document = None if args.status_only else load(args.version, args.ref, args.source_dir, args.allow_legacy)
        if document is None and not args.status_only:
            print('Historical release without a document; preserve legacy notes')
            return
        state = validate_status(read_json('release-status', args.version, args.ref, args.status_dir), args.version)
        if document and any(CHANNELS[c['channel']] not in document['platforms'] for c in state['distribution']):
            raise ValueError('Status includes a platform outside the editorial document')
        if args.previous_status:
            previous_status = validate_status(json.loads(args.previous_status.read_text()), args.version)
            if state['verifiedOn'] < previous_status['verifiedOn']:
                raise ValueError('Refusing an older availability snapshot')
        if args.output_dir:
            if args.status_only:
                write_status(state, args.output_dir)
            else:
                previous = json.loads(args.previous_index.read_text()) if args.previous_index else None
                write_outputs(document, state, args.output_dir, previous)
        print(f'Validated release {args.version}' + (' status only' if args.status_only else ' editorial and status'))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
