"""Synthetic gate boundary tests; actual A133 bytes verified separately."""
import json, sys, tempfile, unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import check_service_programs as c

class ServiceProgramsTests(unittest.TestCase):
 def test_valid_pair_and_invalid_inputs(self):
  for mutation in ('none','missing','old-lock','wrong-phase','receipt-artifact','binary','script','duplicate','symlink'):
   with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temp:
    r=Path(temp);build=r/'build';platform=build/'platform';platform.mkdir(parents=True)
    (r/'docs/update').mkdir(parents=True);(r/'platform/runtime').mkdir(parents=True)
    script=r/'platform/runtime/aitvbox-appctl';script.write_text('fixture script')
    binary=platform/'aitvbox-appd';binary.write_bytes(b'\x7fELF\x02\x01'+b'\0'*12+b'\xb7\0'+b'fixture')
    artifacts={'aitvbox-appd':dict(bytes=binary.stat().st_size,sha256=c.sha(binary))}
    ref=dict(schema=1,source_lock_sha256=c.LOCK_SHA256,artifacts=artifacts,appctl_sha256=c.sha(script))
    (r/'docs/update/services-program-reference.json').write_text(json.dumps(ref))
    receipt=platform/'services-build-result.json';record=dict(status='passed',phase='compiled',source_lock_sha256=c.LOCK_SHA256,no_original_source_dependency=True,artifacts=artifacts)
    receipt.write_text(json.dumps(record))
    if mutation=='missing':receipt.unlink()
    elif mutation=='old-lock':record['source_lock_sha256']='old';receipt.write_text(json.dumps(record))
    elif mutation=='wrong-phase':record['phase']='preflight';receipt.write_text(json.dumps(record))
    elif mutation=='receipt-artifact':record['artifacts']={};receipt.write_text(json.dumps(record))
    elif mutation=='binary':binary.write_bytes(b'old')
    elif mutation=='script':script.write_text('old script')
    elif mutation=='duplicate':receipt.write_text('{"status":"passed","status":"failed"}')
    elif mutation=='symlink':real=platform/'real';binary.rename(real);binary.symlink_to(real)
    with patch.object(c,'ROOT',r):
     if mutation=='none':self.assertEqual(c.check(str(build),str(r))['status'],'passed')
     else:
      with self.assertRaises((ValueError,OSError)):c.check(str(build),str(r))
