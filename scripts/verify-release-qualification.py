#!/usr/bin/env python3
"""Bind an owner's device verdict to one immutable public release candidate."""
import hashlib
import json
from pathlib import Path
import re
import sys

CHECKS = {'installed_bytes', 'application_start', 'private_state_preserved', 'network_preserved', 'upgrade_and_recovery'}


def verify(candidate, qualification, directory):
    fields = {'schema_version', 'source_commit', 'source_tree', 'archive_sha256', 'firmware', 'checks', 'verdict'}
    if (set(qualification) != fields or qualification['schema_version'] != 'u60.device-release-qualification.v1'
        or qualification['firmware'] not in ('B28', 'B31') or qualification['verdict'] != 'approved'
        or set(qualification['checks']) != CHECKS or any(x is not True for x in qualification['checks'].values())):
        raise ValueError('Device qualification is incomplete')
    if candidate.get('schema_version') != 'u60.release-candidate.v1' or candidate.get('host_regressions') != 'passed':
        raise ValueError('Candidate host validation is incomplete')
    for key in ('source_commit', 'source_tree', 'archive_sha256'):
        if not re.fullmatch('[a-f0-9]{' + ('64' if key == 'archive_sha256' else '40') + '}', str(candidate.get(key, ''))):
            raise ValueError('Invalid candidate identity')
        if qualification[key] != candidate[key]:
            raise ValueError('Device qualification selects different bytes')
    if not re.fullmatch(r'u60-pro-enhanced-v[0-9]+\.[0-9]+\.[0-9]+-companion\.tar\.gz', candidate['archive']):
        raise ValueError('Invalid archive name')
    if candidate['archive'] != 'u60-pro-enhanced-' + candidate['tag'] + '.tar.gz':
        raise ValueError('Candidate tag and archive disagree')
    path = Path(directory) / candidate['archive']
    if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != candidate['archive_sha256']:
        raise ValueError('Candidate archive digest differs')
    return {'tag': candidate['tag'], 'source_commit': candidate['source_commit'], 'firmware': qualification['firmware'],
            'archive': candidate['archive'], 'archive_sha256': candidate['archive_sha256']}


if __name__ == '__main__':
    try:
        root = Path(sys.argv[1])
        candidate = json.loads((root / 'candidate.json').read_text())
        qualification = json.loads(Path(sys.argv[2]).read_text())
        print(json.dumps(verify(candidate, qualification, root)))
    except Exception:
        raise SystemExit('U60 release qualification rejected; retain the original candidate and inspect its identity and checks.')
