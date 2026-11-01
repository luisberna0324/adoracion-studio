import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_notes', ROOT / 'scripts/release_notes.py')
notes = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(notes)


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.doc = json.loads((ROOT / 'release-notes/2.4.0.json').read_text())
        self.status = json.loads((ROOT / 'release-status/2.4.0.json').read_text())

    def test_outputs_separate_summary_details_and_mutable_status(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            notes.write_outputs(self.doc, self.status, out)
            doc = json.loads((out / 'versions/2.4.0.json').read_text())
            index = json.loads((out / 'index.json').read_text())
            latest = json.loads((out / 'latest.json').read_text())
            self.assertEqual(doc, self.doc)
            self.assertNotIn('distribution', doc)
            self.assertEqual(latest['release'], index['releases'][0])
            self.assertNotIn('changes', index['releases'][0])
            self.assertNotIn('distribution', index['releases'][0])
            self.assertEqual(json.loads((out / 'status/2.4.0.json').read_text()), self.status)
            for group in doc['changes']:
                for item in group['items']:
                    self.assertIn(item, (out / 'notes.md').read_text())
            self.assertNotIn('Pendiente de revision', (out / 'notes.md').read_text())

    def test_rejects_drafts_wrong_version_and_invalid_editorial_fields(self):
        with self.assertRaises(ValueError): notes.validate(self.doc, '2.4.1')
        for field, value in [('status', 'draft'), ('version', '../other'), ('publishedAt', 'yesterday'),
                             ('platforms', []), ('changes', []), ('highlights', []), ('highlights', ['x'] * 4), ('schemaVersion', 1)]:
            doc = copy.deepcopy(self.doc); doc[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError): notes.validate(doc)

    def test_status_validation_is_independent_and_version_bound(self):
        with self.assertRaises(ValueError): notes.validate_status(self.status, '2.4.1')
        for mutate in [lambda s: s.update(verifiedOn='2026-02-30'),
                       lambda s: s['distribution'][0].update(build=134),
                       lambda s: s['distribution'][0].update(channel='unknown'),
                       lambda s: s['distribution'][0].update(status='approved'),
                       lambda s: s['distribution'].append(s['distribution'][0])]:
            value = copy.deepcopy(self.status); mutate(value)
            with self.assertRaises(ValueError): notes.validate_status(value)
        value = copy.deepcopy(self.status)
        for c in value['distribution']: c['status'] = 'unavailable'
        self.assertEqual(notes.validate_status(value)['distribution'][0]['status'], 'unavailable')

    def test_only_allowlisted_fields_are_public(self):
        doc, state = copy.deepcopy(self.doc), copy.deepcopy(self.status)
        doc['internal'] = 'not-exported'; doc['changes'][0]['internal'] = 'not-exported'
        state['internal'] = 'not-exported'; state['distribution'][0]['internal'] = 'not-exported'
        self.assertNotIn('not-exported', json.dumps(notes.validate(doc)))
        self.assertNotIn('not-exported', json.dumps(notes.validate_status(state)))

    def test_history_is_lightweight_bounded_sorted_and_idempotent(self):
        previous = {'schemaVersion': 2, 'releases': []}
        for n in range(12):
            doc = copy.deepcopy(self.doc); doc['version'] = f'3.0.{n}'
            previous = notes.make_index(doc, previous)
        result = notes.make_index(self.doc, previous)
        self.assertEqual(len(result['releases']), 12)
        self.assertEqual(result['releases'][0]['version'], '3.0.11')
        self.assertEqual(result['releases'][-1]['version'], '3.0.0')
        self.assertTrue(all('changes' not in item for item in result['releases']))
        current = notes.make_index(self.doc)
        self.assertEqual(len(notes.make_index(self.doc, current)['releases']), 1)
        broken = copy.deepcopy(current); broken['releases'][0]['detailsFile'] = 'https://other.invalid/doc'
        with self.assertRaises(ValueError): notes.make_index(self.doc, broken)

    def test_selected_git_ref_does_not_read_local_editorial_or_status_edits(self):
        original = notes.ROOT
        try:
            with tempfile.TemporaryDirectory() as directory:
                repo = Path(directory)
                subprocess.run(['git', 'init', '-q', str(repo)], check=True)
                for folder, data in [('release-notes', self.doc), ('release-status', self.status)]:
                    (repo / folder).mkdir(); (repo / folder / '2.4.0.json').write_text(json.dumps(data))
                subprocess.run(['git', '-C', str(repo), 'add', '.'], check=True)
                subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Notes test', '-c', 'user.email=notes@example.invalid', 'commit', '-qm', 'fixture'], check=True)
                (repo / 'release-notes/2.4.0.json').write_text('{}')
                (repo / 'release-status/2.4.0.json').write_text('{}')
                notes.ROOT = repo
                self.assertEqual(notes.load('2.4.0', 'HEAD')['title'], self.doc['title'])
                self.assertEqual(notes.read_json('release-status', '2.4.0', 'HEAD')['distribution'], self.status['distribution'])
                with self.assertRaises(ValueError): notes.load('2.4.0')
                self.assertIsNone(notes.load('2.3.6', 'HEAD', allow_legacy=True))
                with self.assertRaises(ValueError): notes.load('2.4.1', 'HEAD', allow_legacy=True)
        finally: notes.ROOT = original

    def test_status_only_does_not_need_editorial_or_write_catalog_and_notes(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            subprocess.run([sys.executable, 'scripts/release_notes.py', '--version', '2.4.0', '--status-only', '--source-dir', str(out / 'missing'), '--output-dir', str(out / 'output')], cwd=ROOT, check=True, capture_output=True)
            files = sorted(str(p.relative_to(out / 'output')) for p in (out / 'output').rglob('*.json'))
            self.assertEqual(files, ['release-status.json', 'status/2.4.0.json'])
            self.assertFalse((out / 'output/notes.md').exists())

    def test_publisher_modes_preserve_history_and_stop_on_auth_or_stale_status(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory); fake = base / 'gcloud'
            fake.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
args = sys.argv[1:]
if args[:2] == ['storage', 'cat']:
    if os.environ.get('DENY'):
        print('403 forbidden', file=sys.stderr); sys.exit(1)
    if os.environ.get('MISSING'):
        print('404 not found', file=sys.stderr); sys.exit(1)
    key = 'PREVIOUS_STATUS' if '/status/' in args[-1] else 'PREVIOUS_INDEX'
    print(Path(os.environ[key]).read_text())
elif args[:2] == ['storage', 'cp']:
    with open(os.environ['UPLOAD_LOG'], 'a') as stream:
        stream.write(json.dumps({'destination': args[-1], 'data': json.loads(Path(args[-2]).read_text())}) + '\\n')
else: sys.exit(2)
'''); fake.chmod(0o755)
            newer = copy.deepcopy(self.doc); newer['version'] = '2.5.0'
            previous = base / 'index.json'; previous.write_text(json.dumps(notes.make_index(newer)))
            state = base / 'status.json'; state.write_text(json.dumps(self.status))
            log = base / 'uploads.jsonl'
            cases = [([], {}, 4), (['--status-only'], {}, 1), ([], {'MISSING': '1'}, 4),
                     ([], {'DENY': '1'}, 0), (['--status-only'], {'DENY': '1'}, 0)]
            for mode, flags, count in cases:
                log.write_text('')
                result = subprocess.run(['bash', 'scripts/publish_release_notes.sh', *mode], cwd=ROOT,
                    env={**os.environ, 'PATH': str(base) + os.pathsep + os.environ['PATH'], 'GCS_BUCKET': 'test.invalid',
                         'SEMVER': '2.4.0', 'PREVIOUS_INDEX': str(previous), 'PREVIOUS_STATUS': str(state), 'UPLOAD_LOG': str(log), **flags}, capture_output=True, text=True)
                uploads = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual(len(uploads), count, result.stderr)
                self.assertEqual(result.returncode == 0, count > 0)
                if mode and count: self.assertTrue(uploads[0]['destination'].endswith('/status/2.4.0.json'))
                if not mode and count and not flags:
                    self.assertEqual(uploads[-1]['data']['release']['version'], '2.5.0')
                    self.assertEqual(uploads[-2]['data']['releases'][0]['version'], '2.5.0')
            state.write_text(json.dumps({**self.status, 'verifiedOn': '2099-01-01'})); log.write_text('')
            result = subprocess.run(['bash', 'scripts/publish_release_notes.sh', '--status-only'], cwd=ROOT,
                env={**os.environ, 'PATH': str(base) + os.pathsep + os.environ['PATH'], 'GCS_BUCKET': 'test.invalid', 'SEMVER': '2.4.0',
                     'PREVIOUS_STATUS': str(state), 'UPLOAD_LOG': str(log)}, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(log.read_text(), '')

    def test_status_workflow_rejects_invalid_modes_before_any_mutation(self):
        if not shutil.which('ruby'): self.skipTest('Ruby YAML parser unavailable')
        script = subprocess.check_output(['ruby', '-ryaml', '-e', 'print YAML.load_file(ARGV[0])["jobs"]["publish-release-status"]["steps"].find { |s| s["name"] == "Validate authorized mode and existing public release" }["run"]', str(ROOT / '.github/workflows/ci.yml')], text=True)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory); gh = base / 'gh'; log = base / 'gh.log'
            gh.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$GH_LOG"\nprintf "%s\\n" "${DRAFT:-false}"\n'); gh.chmod(0o755)
            for extra, succeeds in [({}, True), ({'PUBLISH_RELEASE': 'false'}, False), ({'EXPORT_KEY': 'true'}, False),
                                    ({'GOOGLE_PLAY_ONLY': 'true'}, False), ({'SEMVER': '../other'}, False), ({'DRAFT': 'true'}, False)]:
                log.write_text('')
                result = subprocess.run(['bash', '-c', script], cwd=ROOT,
                    env={**os.environ, 'PATH': str(base) + os.pathsep + os.environ['PATH'], 'GH_LOG': str(log), 'RUNNER_TEMP': str(base),
                         'SEMVER': '2.4.0', 'PUBLISH_RELEASE': 'true', 'EXPORT_KEY': 'false', 'GOOGLE_PLAY_ONLY': 'false', **extra}, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stderr)
                self.assertNotIn('upload', log.read_text()); self.assertNotIn('create', log.read_text())


if __name__ == '__main__': unittest.main()
