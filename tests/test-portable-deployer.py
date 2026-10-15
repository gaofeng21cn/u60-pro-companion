#!/usr/bin/env python3
import hashlib,os,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Deployer(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.r=pathlib.Path(self.tmp.name);self.pkg=self.r/'package';self.pkg.mkdir()
  shutil.copy2(ROOT/'scripts/portable/deploy-from-computer.py',self.pkg/'deploy-from-computer.py')
  (self.pkg/'RELEASE-ID').write_text('u60-pro-B31-20260921-140827\n')
  (self.pkg/'TARGET-IDENTITY-SHA256').write_text(hashlib.sha256(b'u60-imei-v1:123456789012345').hexdigest()+'\n')
  (self.pkg/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(self.pkg.iterdir()) if p.name!='SHA256SUMS'))
  self.adb=self.r/'adb';self.adb.write_text('''#!/usr/bin/env python3
import os,pathlib,sys
with open(os.environ['CALLS'],'a') as f:f.write(sys.argv[1]+'\\n')
args=sys.argv[1:]
if args[0]=='devices':
 print('List of devices attached\\nONE\\tdevice')
 if os.environ.get('MULTIPLE'):print('TWO\\tdevice')
elif 'push' in args:
 with open(os.environ['PUSHES'],'a') as f:f.write('push\\n')
elif 'exec-out' in args:
 script=args[-1]
 if 'get_zwrt_common_info' in script:print(os.environ.get('FW','BD_CNMU5250V1.0.0B31')+'\\n__U60_RC__=0')
 elif 'device_info' in script:print(os.environ.get('IMEI','123456789012345')+'\\n__U60_RC__=0')
 elif os.environ.get('FAIL_RC'):print('failed but adb reports zero\\n__U60_RC__=1')
 else:print('checked\\n__U60_RC__=0')
else:sys.exit(2)
''');self.adb.chmod(0o700)
  self.env=dict(os.environ,CALLS=str(self.r/'calls'),PUSHES=str(self.r/'pushes'))
 def tearDown(self):self.tmp.cleanup()
 def run_it(self,**env):return subprocess.run(['python3',str(self.pkg/'deploy-from-computer.py'),'--adb',str(self.adb),'check'],env=dict(self.env,**env),capture_output=True,text=True,timeout=10)
 def run_action(self,action,**env):
  return subprocess.run(['python3',str(self.pkg/'deploy-from-computer.py'),'--adb',str(self.adb),action],
                        env=dict(self.env,**env),capture_output=True,text=True,timeout=10)
 def test_restore_boot_uploads_the_reviewed_script_before_running_it(self):
  # restore-boot runs its script from the target package directory; the device
  # must receive this reviewed copy first instead of running a stale one.
  r=self.run_action('restore-boot')
  self.assertEqual(r.returncode,0,r.stderr)
  self.assertTrue((self.r/'pushes').exists(),'restore-boot must upload the bundle first')
 def test_clean_bundle_targets_one_device_with_exec_out(self):
  r=self.run_it();self.assertEqual(r.returncode,0,r.stderr);self.assertTrue((self.r/'pushes').exists())
 def test_b28_bundle_retains_support(self):
  (self.pkg/'RELEASE-ID').write_text('u60-pro-B28-20260921-140827\n')
  (self.pkg/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in sorted(self.pkg.iterdir()) if p.name!='SHA256SUMS'))
  r=self.run_it(FW='BD_FLYMODEMMU5250V1.0.0B28')
  self.assertEqual(r.returncode,0,r.stderr);self.assertTrue((self.r/'pushes').exists())
 def test_unlisted_private_file_is_never_uploaded(self):
  (self.pkg/'config.yaml').write_text('SYNTHETIC-PRIVATE-FILE');r=self.run_it();self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'calls').exists())
 def test_multiple_devices_need_selection(self):
  r=self.run_it(MULTIPLE='1');self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'pushes').exists())
 def test_wrong_firmware_refuses_upload(self):
  r=self.run_it(FW='B27');self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'pushes').exists())
 def test_other_device_refuses_upload(self):
  r=self.run_it(IMEI='999999999999999');self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'pushes').exists())
 def test_remote_rc_failure_is_not_hidden_by_adb_success(self):
  r=self.run_it(FAIL_RC='1');self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'pushes').exists())
 def test_tampered_manifest_member_refused(self):
  (self.pkg/'RELEASE-ID').write_text('tampered');r=self.run_it();self.assertNotEqual(r.returncode,0);self.assertFalse((self.r/'calls').exists())
if __name__=='__main__':unittest.main()
