"""Release-input regression tests; creates no tags in the SDK repositories."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from resolve_release import checked_out_commit, ci_transport_sha, release_inputs


class ReleaseTriggerTests(unittest.TestCase):
    def test_report_workflow_can_import_resolver_from_repository_root(self):
        root = Path(__file__).resolve().parents[1]
        output = subprocess.check_output([sys.executable, '-c',
            'from pathlib import Path; from validation.resolve_release import ci_transport_sha; '
            'print(ci_transport_sha(Path(".")))'], cwd=root, text=True).strip()
        self.assertEqual(output, ci_transport_sha(root))

    def test_tag_uses_its_version_and_the_frozen_ci_pin(self):
        result = release_inputs('push', 'refs/tags/v0.3.0', 'b' * 40, '9.9.9', 'a' * 40)
        self.assertEqual(result, {'COMM_SHA': 'a' * 40, 'RELEASE_VERSION': '0.3.0'})

    def test_pull_request_tests_source_version_with_frozen_dependency(self):
        result = release_inputs('pull_request', 'refs/pull/10/merge', '', '0.3.0', 'a' * 40)
        self.assertEqual(result, {'COMM_SHA': 'a' * 40, 'RELEASE_VERSION': '0.3.0'})

    def test_manual_dispatch_preserves_explicit_inputs(self):
        result = release_inputs('workflow_dispatch', 'refs/heads/master', 'b' * 40, '0.3.1', 'a' * 40)
        self.assertEqual(result, {'COMM_SHA': 'b' * 40, 'RELEASE_VERSION': '0.3.1'})

    def test_branch_push_and_non_release_tags_are_rejected(self):
        for ref in ['refs/heads/master', 'refs/tags/0.3.0', 'refs/tags/v0.3.0-rc1', 'refs/tags/v01.3.0']:
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                release_inputs('push', ref, '', '', 'a' * 40)

    def test_invalid_manual_inputs_cannot_be_written_to_runner_environment(self):
        for sha, version in [('master', '0.3.0'), ('a' * 40, '0.3.0\nOTHER=value'), ('a' * 40, 'v0.3.0')]:
            with self.subTest(sha=sha, version=version), self.assertRaises(ValueError):
                release_inputs('workflow_dispatch', 'refs/heads/master', sha, version)

    def test_tag_requires_a_full_dependency_pin(self):
        for pin in [None, '', 'master', 'a' * 7]:
            with self.subTest(pin=pin), self.assertRaises(ValueError):
                release_inputs('push', 'refs/tags/v0.3.0', '', '', pin)

    def test_ci_pin_drift_and_missing_pins_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            paths = [root / host / 'workflows/ci.yml' for host in ['.gitea', '.github']]
            for path in paths:
                path.parent.mkdir(parents=True)
                path.write_text('env:\n  COMM_REF: ' + 'a' * 40 + '\n')
            self.assertEqual(ci_transport_sha(root), 'a' * 40)
            paths[1].write_text('env:\n  COMM_REF: ' + 'b' * 40 + '\n')
            with self.assertRaisesRegex(ValueError, 'pins differ'):
                ci_transport_sha(root)
            paths[1].write_text('env:\n  COMM_REF: master\n')
            with self.assertRaisesRegex(ValueError, 'pin one full'):
                ci_transport_sha(root)

    def test_github_only_repository_resolves_dependency_pin(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / '.github/workflows/ci.yml'
            path.parent.mkdir(parents=True)
            path.write_text('env:\n  COMM_REF: ' + 'a' * 40 + '\n')
            self.assertEqual(ci_transport_sha(root), 'a' * 40)

    def test_annotated_tag_is_peeled_to_the_actual_hand_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            command = ['git', '-C', str(root), '-c', 'commit.gpgsign=false',
                       '-c', 'tag.gpgsign=false', '-c', 'user.name=Release test',
                       '-c', 'user.email=release-test@example.invalid']
            subprocess.run([*command, 'init', '-q'], check=True)
            subprocess.run([*command, 'commit', '--allow-empty', '-qm', 'Test release input'], check=True)
            commit = subprocess.check_output([*command, 'rev-parse', 'HEAD'], text=True).strip()
            subprocess.run([*command, 'tag', '-a', 'v0.3.0', '-m', 'Test annotated tag'], check=True)
            tag = subprocess.check_output([*command, 'rev-parse', 'refs/tags/v0.3.0'], text=True).strip()
            self.assertNotEqual(tag, commit)
            self.assertEqual(checked_out_commit(root, tag), commit)
            self.assertEqual(checked_out_commit(root, commit), commit)
            subprocess.run([*command, 'commit', '--allow-empty', '-qm', 'Different commit'], check=True)
            with self.assertRaisesRegex(ValueError, 'does not match'):
                checked_out_commit(root, tag)


if __name__ == '__main__':
    unittest.main()
