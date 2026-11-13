"""Resolve tag/manual package inputs before checking out the transport dependency."""
import os
import re
import subprocess
from pathlib import Path

if __package__:
    from .release_package import project_version
else:
    from release_package import project_version


VERSION = r'(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)'
SHA = r'[0-9a-f]{40}'


def ci_transport_sha(source):
    pins = []
    for relative in ['.gitea/workflows/ci.yml', '.github/workflows/sanitizers.yml']:
        path = source / relative
        if relative.startswith('.gitea/') and not path.exists():
            continue
        contents = path.read_text(encoding='utf-8')
        matches = re.findall(r'^  COMM_REF:\s*[\'\"]?(' + SHA + r')[\'\"]?\s*(?:#.*)?$',
                             contents, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError(f'{relative} must pin one full transport SHA in env.COMM_REF')
        pins.append(matches[0])
    if len(set(pins)) != 1:
        raise ValueError('Gitea and GitHub CI transport pins differ')
    return pins[0]


def release_inputs(event, ref, requested_sha, requested_version, pinned_sha=None):
    if event == 'push':
        match = re.fullmatch(r'refs/tags/v(' + VERSION + r')', ref)
        if not match:
            raise ValueError('Automatic packaging requires a vMAJOR.MINOR.PATCH tag')
        sha, version = pinned_sha, match[1]
    elif event == 'pull_request':
        sha, version = pinned_sha, requested_version
    elif event == 'workflow_dispatch':
        sha, version = requested_sha, requested_version
    else:
        raise ValueError(f'Unsupported release event: {event}')
    if not isinstance(sha, str) or not re.fullmatch(SHA, sha):
        raise ValueError('Transport revision must be a full 40-character lowercase SHA')
    if not re.fullmatch(VERSION, version):
        raise ValueError('Release version must be MAJOR.MINOR.PATCH')
    return {'COMM_SHA': sha, 'RELEASE_VERSION': version}


def checked_out_commit(source, event_sha):
    if not re.fullmatch(SHA, event_sha):
        raise ValueError('Event revision must be a full commit or annotated-tag SHA')
    command = ['git', '-C', str(source), 'rev-parse']
    expected = subprocess.check_output([*command, event_sha + '^{commit}'], text=True).strip()
    actual = subprocess.check_output([*command, 'HEAD'], text=True).strip()
    if actual != expected:
        raise ValueError('Hand checkout does not match the event revision')
    return actual


def main():
    source = Path(__file__).resolve().parents[1]
    event = os.environ['GITHUB_EVENT_NAME']
    values = release_inputs(event, os.environ['GITHUB_REF'],
                            os.environ.get('REQUESTED_COMM_SHA', ''),
                            project_version(source) if event == 'pull_request' else os.environ.get('REQUESTED_VERSION', ''),
                            ci_transport_sha(source) if event in ('push', 'pull_request') else None)
    values['HAND_SHA'] = checked_out_commit(source, os.environ['GITHUB_SHA'])
    with Path(os.environ['GITHUB_ENV']).open('a', encoding='utf-8') as output:
        for name, value in values.items():
            output.write(f'{name}={value}\n')
    print(f"Release {values['RELEASE_VERSION']}: hand={values['HAND_SHA']}, "
          f"transport={values['COMM_SHA']} ({event})")


if __name__ == '__main__':
    main()
