import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release_version', ROOT / 'scripts/release_version.py')
release_version = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release_version)


class ReleaseVersionTests(unittest.TestCase):
    def test_cli_accepts_source_and_rejects_a_mislabeled_release(self):
        version = release_version.read_version()
        command = ['python3', str(ROOT / 'scripts/release_version.py'), '--expect']
        accepted = subprocess.run(command + [version], capture_output=True, text=True)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertEqual(accepted.stdout.strip(), version)
        rejected = subprocess.run(command + ['0.0.1'], capture_output=True, text=True)
        self.assertNotEqual(rejected.returncode, 0)
        self.assertIn('differs from source', rejected.stderr)

    def test_cmake_rejects_both_cache_and_environment_drift(self):
        env = {key: value for key, value in os.environ.items() if key != 'MTCONTROLS_APP_VERSION'}
        module = str(ROOT / 'cmake/ReleaseVersion.cmake')
        for arguments, extra_env, succeeds in [([], {}, True),
                (['-DMTCONTROLS_APP_VERSION=0.0.1'], {}, False),
                ([], {'MTCONTROLS_APP_VERSION': '0.0.1'}, False)]:
            with self.subTest(arguments=arguments, extra_env=extra_env):
                result = subprocess.run(['cmake', *arguments, '-P', module],
                    env={**env, **extra_env}, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, succeeds, result.stderr)

    def test_native_identity_without_build_override_matches_canonical_version(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'version.cpp'
            executable = Path(directory) / 'version'
            source.write_text('#include "AppIdentity.h"\n#include <iostream>\nint main() { std::cout << playworship::appVersion; }\n')
            subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'src'), str(source), '-o', str(executable)], check=True)
            self.assertEqual(subprocess.check_output([str(executable)], text=True), release_version.read_version())

    def test_git_ref_uses_the_target_revision_instead_of_the_working_tree(self):
        original_root = release_version.ROOT
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'src').mkdir()
            header = root / 'src/AppVersion.h'
            header.write_text('#define PLAYWORSHIP_RELEASE_VERSION "1.0.0"\n')
            subprocess.run(['git', '-C', str(root), 'add', 'src/AppVersion.h'], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'user.name=Version test', '-c', 'user.email=version@example.invalid', 'commit', '-qm', 'fixture'], check=True)
            header.write_text('#define PLAYWORSHIP_RELEASE_VERSION "2.0.0"\n')
            try:
                release_version.ROOT = root
                self.assertEqual(release_version.read_version('HEAD'), '1.0.0')
                self.assertEqual(release_version.read_version(), '2.0.0')
            finally:
                release_version.ROOT = original_root

    def test_workflow_minor_release_and_legacy_tags_use_the_selected_source(self):
        if not shutil.which('ruby'):
            self.skipTest('Ruby YAML parser unavailable for workflow fixture')
        workflow_script = subprocess.check_output(['ruby', '-ryaml', '-e',
            'print YAML.load_file(ARGV[0])["jobs"]["prepare"]["steps"].find { |s| s["id"] == "resolve" }["run"]',
            str(ROOT / '.github/workflows/ci.yml')], text=True)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            repo, remote = base / 'source', base / 'remote.git'
            subprocess.run(['git', 'init', '-q', '--bare', str(remote)], check=True)
            subprocess.run(['git', 'init', '-q', '-b', 'main', str(repo)], check=True)
            def git(*arguments):
                return subprocess.run(['git', '-C', str(repo), *arguments], check=True, capture_output=True)
            git('config', 'user.name', 'Release test')
            git('config', 'user.email', 'release@example.invalid')
            (repo / 'README').write_text('legacy source')
            git('add', '.')
            git('commit', '-qm', 'legacy')
            git('tag', 'v2.3.6')
            (repo / 'src').mkdir()
            (repo / 'scripts').mkdir()
            (repo / 'src/AppVersion.h').write_text('#define PLAYWORSHIP_RELEASE_VERSION "2.4.0"\n')
            shutil.copy(ROOT / 'scripts/release_version.py', repo / 'scripts/release_version.py')
            git('add', '.')
            git('commit', '-qm', 'unified')
            git('remote', 'add', 'origin', str(remote))
            git('push', '-q', 'origin', 'main', '--tags')
            def resolve(ref, kind):
                output = base / 'outputs'
                output.write_text('')
                result = subprocess.run(['bash', '-c', workflow_script], cwd=repo,
                    env={**os.environ, 'REQUESTED_REF': ref, 'REQUESTED_RELEASE_KIND': kind,
                        'REQUESTED_PUBLISH': 'false', 'REQUESTED_RETAIN_ARTIFACTS': 'true',
                        'REQUESTED_GOOGLE_PLAY_ONLY': 'false', 'GITHUB_OUTPUT': str(output)},
                    capture_output=True, text=True)
                return result, dict(line.split('=', 1) for line in output.read_text().splitlines())
            accepted, output = resolve('main', 'minor')
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            self.assertEqual(output['semver'], '2.4.0')
            rejected, _ = resolve('main', 'patch')
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('differs from source', rejected.stderr)
            legacy, output = resolve('v2.3.6', 'minor')
            self.assertEqual(legacy.returncode, 0, legacy.stderr)
            self.assertEqual(output['semver'], '2.3.6')


if __name__ == '__main__':
    unittest.main()
