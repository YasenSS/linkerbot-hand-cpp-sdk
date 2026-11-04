"""Draft release guards: exact tag, latest quality run and unchanged asset bytes."""
import contextlib
import io
import os
import sys
import unittest
from unittest.mock import patch

import test_release_matrix as fixtures
from publish_release import main, require_quality, require_tag, verify_assets


class DraftReleaseTests(unittest.TestCase):
    def test_annotated_tag_is_peeled_and_moving_tag_is_rejected(self):
        sha = 'a' * 40
        responses = {'/git/ref/tags/v0.3.0': {'object': {'type': 'tag', 'sha': 'b' * 40}},
                     '/git/tags/' + 'b' * 40: {'object': {'type': 'commit', 'sha': sha}}}
        require_tag(responses.__getitem__, 'v0.3.0', sha, '0.3.0')
        with self.assertRaises(ValueError):
            require_tag(responses.__getitem__, 'v0.3.0', 'c' * 40, '0.3.0')
        with self.assertRaises(ValueError):
            require_tag(responses.__getitem__, 'main', sha, '0.3.0')

    def test_quality_must_be_latest_success_on_exact_source(self):
        sha = 'a' * 40
        run = {'id': 1, 'head_sha': sha, 'path': '.github/workflows/cppreportcard.yml',
               'event': 'push', 'status': 'completed', 'conclusion': 'success', 'html_url': 'https://example.test/run/1'}
        def get(rows):
            return lambda path: {'workflow_runs': rows}
        self.assertEqual(require_quality(get([run]), sha), run['html_url'])
        for rows in ([], [{**run, 'head_sha': 'b' * 40}], [{**run, 'event': 'pull_request'}],
                     [run, {**run, 'id': 2, 'conclusion': 'failure'}],
                     [{**run, 'status': 'in_progress', 'conclusion': None}]):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                require_quality(get(rows), sha)

    def test_complete_assets_are_rechecked_without_modifying_originals(self):
        case = fixtures.ReleaseMatrixTests()
        case.setUp()
        self.addCleanup(case.doCleanups)
        case.complete_set()
        case.collect()
        before = {p.name: p.read_bytes() for p in case.assets.iterdir()}
        verify_assets(case.assets, case.targets_path, '0.3.0', 'a' * 40, 'b' * 40)
        self.assertEqual(before, {p.name: p.read_bytes() for p in case.assets.iterdir()})
        (case.assets / 'SHA256SUMS.txt').unlink()
        with self.assertRaises(ValueError):
            verify_assets(case.assets, case.targets_path, '0.3.0', 'a' * 40, 'b' * 40)

    def test_branch_or_automatic_dispatch_cannot_publish(self):
        argv = ['publish_release.py', '--assets', 'unused', '--version', '0.3.0',
                '--hand-sha', 'a' * 40, '--comm-sha', 'b' * 40]
        for event, ref_type in [('push', 'tag'), ('workflow_dispatch', 'branch')]:
            with self.subTest(event=event, ref_type=ref_type), patch.object(sys, 'argv', argv), \
                    patch.dict(os.environ, {'GITHUB_EVENT_NAME': event, 'GITHUB_REF_TYPE': ref_type}), \
                    patch('urllib.request.urlopen') as http, patch('subprocess.run') as command, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    main()
                http.assert_not_called()
                command.assert_not_called()


if __name__ == '__main__':
    unittest.main()
