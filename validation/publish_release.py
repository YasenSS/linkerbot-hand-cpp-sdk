"""Create an explicitly requested draft from the already validated SDK assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.error
import urllib.request

from release_matrix import collect


def require_tag(get, tag, sha, version):
    if tag != 'v' + version or not re.fullmatch(r'v\d+\.\d+\.\d+', tag):
        raise ValueError('Draft creation requires the matching immutable version tag')
    target = get('/git/ref/tags/' + tag)['object']
    for _ in range(8):
        if target['type'] != 'tag':
            break
        target = get('/git/tags/' + target['sha'])['object']
    if target['type'] != 'commit' or target['sha'] != sha:
        raise ValueError('Version tag moved or does not identify the validated source')


def require_quality(get, sha):
    runs = get('/actions/runs?head_sha=' + sha + '&per_page=100')['workflow_runs']
    runs = [run for run in runs if run['head_sha'] == sha
            and run['path'] == '.github/workflows/cppreportcard.yml'
            and run['event'] in ('push', 'workflow_dispatch')]
    if not runs:
        raise ValueError('Run the A+ workflow on this exact source commit before drafting a release')
    latest = max(runs, key=lambda run: run['id'])
    if latest['status'] != 'completed' or latest['conclusion'] != 'success':
        raise ValueError('The latest exact-source A+ workflow must complete successfully')
    return latest['html_url']


def verify_assets(assets, targets, version, hand_sha, comm_sha):
    # Revalidate in a disposable copy. Published bytes must remain identical to
    # the tested collector output, including manifest/checksum serialization.
    original = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in assets.iterdir() if p.is_file()}
    with tempfile.TemporaryDirectory() as temporary:
        copied = Path(temporary) / 'assets'
        shutil.copytree(assets, copied)
        collect(copied, targets, version, hand_sha, comm_sha)
        checked = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in copied.iterdir() if p.is_file()}
    if len(original) != 26 or checked != original:
        raise ValueError('Release assets differ from the validated twelve-package set')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--targets', type=Path, default=Path(__file__).resolve().parents[1] / 'ci/package-targets.json')
    parser.add_argument('--version', required=True)
    parser.add_argument('--hand-sha', required=True)
    parser.add_argument('--comm-sha', required=True)
    args = parser.parse_args()
    if os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch' or os.environ.get('GITHUB_REF_TYPE') != 'tag':
        parser.error('Draft creation requires an explicit manual dispatch on a version tag')
    repository = os.environ['GITHUB_REPOSITORY']
    tag = os.environ['GITHUB_REF_NAME']
    base = os.environ.get('GITHUB_API_URL', 'https://api.github.com') + '/repos/' + repository
    headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'], 'Accept': 'application/vnd.github+json'}

    def get(path, allow_missing=False):
        try:
            with urllib.request.urlopen(urllib.request.Request(base + path, headers=headers), timeout=30) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if allow_missing and error.code == 404:
                return None
            raise

    require_tag(get, tag, args.hand_sha, args.version)
    quality_url = require_quality(get, args.hand_sha)
    verify_assets(args.assets, args.targets, args.version, args.hand_sha, args.comm_sha)
    if get('/releases/tags/' + tag, allow_missing=True) is not None:
        raise RuntimeError('Release already exists; refusing to replace any assets')
    require_tag(get, tag, args.hand_sha, args.version)
    with tempfile.TemporaryDirectory() as temporary:
        notes = Path(temporary) / 'notes.md'
        notes.write_text(
            'Eight native Ubuntu 20.04/22.04/24.04/26.04 x86_64/ARM64 Release SDKs and four '
            'Windows x64/ARM64 Debug/Release SDKs.\n\n'
            f'Hand source: `{args.hand_sha}`; transport source: `{args.comm_sha}`.\n\n'
            f'A+ quality gate: {quality_url}\n\n'
            'See release-manifest.json and SHA256SUMS.txt for build identity and checksums.\n', encoding='utf-8')
        subprocess.run(['gh', 'release', 'create', tag, *map(str, sorted(args.assets.iterdir())),
                        '--repo', repository, '--draft', '--verify-tag', '--title', tag,
                        '--notes-file', str(notes)], check=True)
        downloaded = Path(temporary) / 'downloaded'
        subprocess.run(['gh', 'release', 'download', tag, '--repo', repository,
                        '--dir', str(downloaded)], check=True)
        expected = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in args.assets.iterdir()}
        actual = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in downloaded.iterdir()}
        if actual != expected:
            raise RuntimeError('Uploaded draft bytes do not match validated assets')
    print('Draft created; all 26 uploaded assets match the validated bytes')


if __name__ == '__main__':
    main()
