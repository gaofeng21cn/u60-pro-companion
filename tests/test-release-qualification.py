import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('qualification', ROOT / 'scripts/verify-release-qualification.py')
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)


class ReleaseQualification(unittest.TestCase):
    def test_device_verdict_is_bound_to_original_archive_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            name = 'u60-pro-enhanced-v1.2.3-companion.tar.gz'
            (root / name).write_bytes(b'synthetic public candidate')
            candidate = {'schema_version': 'u60.release-candidate.v1', 'source_commit': 'a' * 40,
                         'source_tree': 'b' * 40, 'tag': 'v1.2.3-companion', 'archive': name,
                         'archive_sha256': hashlib.sha256((root / name).read_bytes()).hexdigest(), 'host_regressions': 'passed'}
            receipt = {**{k: candidate[k] for k in ('source_commit', 'source_tree', 'archive_sha256')},
                       'schema_version': 'u60.device-release-qualification.v1', 'firmware': 'B31',
                       'verdict': 'approved', 'checks': {k: True for k in qualification.CHECKS}}
            self.assertEqual(qualification.verify(candidate, receipt, root)['tag'], candidate['tag'])
            for update in ({'source_commit': 'c' * 40}, {'archive_sha256': 'd' * 64},
                           {'firmware': 'unknown'}, {'verdict': 'pending'}, {'private_device_id': 'synthetic'}):
                with self.assertRaises(ValueError): qualification.verify(candidate, {**receipt, **update}, root)
            for check in qualification.CHECKS:
                with self.assertRaises(ValueError):
                    qualification.verify(candidate, {**receipt, 'checks': {**receipt['checks'], check: False}}, root)
            (root / name).write_bytes(b'changed after device qualification')
            with self.assertRaises(ValueError): qualification.verify(candidate, receipt, root)


if __name__ == '__main__': unittest.main()
