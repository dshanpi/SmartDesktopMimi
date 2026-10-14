"""Use real Git trees: a record cannot hide deleted, renamed or untracked changes."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'scripts/check_change_gates.py'
spec = importlib.util.spec_from_file_location('change_gates', SCRIPT)
gates = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gates)
REQ = 'REQ-MIMI-TRACE-021'
RECORD = 'docs/change-gates/' + REQ + '.md'
BATCH = 'docs/change-gates/batches/example.json'


class ChangeGatesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Gate Test')
        self.write('app.c', 'old\n')
        self.base = self.commit()

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.PIPE).decode().strip()

    def write(self, name, value):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(value)

    def commit(self):
        self.git('add', '--all')
        self.git('commit', '-qm', 'fixture')
        return self.git('rev-parse', 'HEAD')

    def batch(self, paths=None):
        self.write(RECORD, '# ' + REQ + '\nContext, process, invariants, actual evidence and next step.\n')
        data = dict(schema=1, summary='Gate tooling', context='Prevent undocumented changes', next_steps='Device acceptance remains separate', requirements=[dict(id=REQ, record=RECORD, scope='Host coverage checker', invariants='No firmware or secret changes', paths=paths or ['app.c', RECORD, BATCH], checks=[dict(name='Fixture', status='passed', scope='Host only', evidence='output/unit.log')])])
        self.write(BATCH, json.dumps(data))
        return data

    def result(self, base=None):
        return gates.check(self.root, base or self.base, 'HEAD')

    def invalid(self, data, message):
        self.write(BATCH, json.dumps(data)); self.commit()
        with self.assertRaisesRegex(gates.GateError, message): self.result()

    def test_complete_coverage(self):
        self.write('app.c', 'new\n'); self.batch(); head=self.commit()
        r=self.result()
        self.assertEqual(r['head_commit'],head)
        self.assertEqual(r['status'],'passed')
        self.assertFalse(r['release_qualified'])
        self.assertEqual(len(r['diff_sha256']),64)

    def test_zero_diff_is_not_an_acceptance_pass(self):
        self.assertEqual(self.result()['status'],'not_run')

    def test_missing_batch(self):
        self.write('app.c','new');self.commit()
        with self.assertRaisesRegex(gates.GateError,'without traceability'):self.result()

    def test_omitted_file(self):
        self.write('app.c','new');self.batch([RECORD,BATCH]);self.commit()
        with self.assertRaisesRegex(gates.GateError,'app.c'):self.result()

    def test_deleted_source_is_covered(self):
        (self.root/'app.c').unlink();self.batch();self.commit()
        self.assertIn('app.c',self.result()['changed_files'])

    def test_rename_both_sides_required(self):
        (self.root/'app.c').rename(self.root/'renamed.c');self.batch();self.commit()
        with self.assertRaisesRegex(gates.GateError,'renamed.c'):self.result()

    def test_document_is_required(self):
        self.batch();(self.root/RECORD).unlink();self.commit()
        with self.assertRaisesRegex(gates.GateError,'Detailed record'):self.result()

    def test_document_symlink_rejected(self):
        self.batch();(self.root/RECORD).unlink();(self.root/RECORD).symlink_to('/etc/passwd');self.commit()
        with self.assertRaisesRegex(gates.GateError,'regular tracked'):self.result()

    def test_batch_symlink_rejected(self):
        self.batch();(self.root/BATCH).unlink();(self.root/BATCH).symlink_to('/etc/passwd');self.commit()
        with self.assertRaisesRegex(gates.GateError,'regular tracked'):self.result()

    def test_historical_batch_cannot_be_edited_or_deleted(self):
        self.batch();base=self.commit()
        self.write(BATCH,'{}');self.commit()
        with self.assertRaisesRegex(gates.GateError,'immutable'):self.result(base)
        (self.root/BATCH).unlink();self.commit()
        with self.assertRaisesRegex(gates.GateError,'immutable'):self.result(base)

    def test_append_batch_can_reference_updated_existing_gate(self):
        self.batch();base=self.commit()
        self.write(RECORD,'# '+REQ+'\nAppend actual evidence, preserve history.\n')
        newer=BATCH.replace('example','second')
        d=json.loads((self.root/BATCH).read_text());d['requirements'][0]['paths']=[RECORD,newer]
        self.write(newer,json.dumps(d));self.commit()
        self.assertEqual(self.result(base)['batches'],[newer])

    def test_path_traversal_and_absolute_paths(self):
        for value in ('../escape','/etc/passwd','C:/escape','a\\b','a/../b','a//b','.'):
            with self.subTest(value=value):
                with self.assertRaises(gates.GateError):gates.path(value)

    def test_boolean_schema_rejected(self):
        d=self.batch();d['schema']=True;self.invalid(d,'schema')

    def test_unhashable_status_rejected(self):
        d=self.batch();d['requirements'][0]['checks'][0]['status']=[];self.invalid(d,'status')

    def test_empty_context(self):
        d=self.batch();d['context']=' ';self.invalid(d,'context')

    def test_unknown_status(self):
        d=self.batch();d['requirements'][0]['checks'][0]['status']='complete';self.invalid(d,'status')

    def test_failure_requires_reason(self):
        d=self.batch();d['requirements'][0]['checks'][0]['status']='failed';self.invalid(d,'reason')

    def test_failed_and_not_run_are_valid_records_not_release_passes(self):
        d=self.batch();d['requirements'][0]['checks'][0].update(status='not_run',reason='Need actual target');self.write(BATCH,json.dumps(d));self.commit()
        self.assertFalse(self.result()['release_qualified'])

    def test_pass_requires_evidence(self):
        d=self.batch();d['requirements'][0]['checks'][0]['evidence']='';self.invalid(d,'evidence')

    def test_batch_itself_requires_coverage(self):
        self.batch([RECORD]);self.commit()
        with self.assertRaisesRegex(gates.GateError,'example.json'):self.result()

    def test_duplicate_json_key(self):
        self.batch();self.write(BATCH,'{"schema":1,"schema":2}');self.commit()
        with self.assertRaisesRegex(gates.GateError,'Duplicate'):self.result()

    def test_nonexistent_or_reverse_baseline(self):
        self.batch();head=self.commit()
        with self.assertRaises(gates.GateError):gates.check(self.root,'not-a-ref','HEAD')
        with self.assertRaises(gates.GateError):gates.check(self.root,head,self.base)

    def test_gate_id_mismatch(self):
        d=self.batch();d['requirements'][0]['id']='REQ-MIMI-UI-016';self.invalid(d,'match requirement')

    def test_unchanged_gate_is_not_sufficient(self):
        self.batch();base=self.commit();self.write('app.c','new')
        newer=BATCH.replace('example','second');d=json.loads((self.root/BATCH).read_text());d['requirements'][0]['paths']=['app.c',newer];self.write(newer,json.dumps(d));self.commit()
        with self.assertRaisesRegex(gates.GateError,'must be updated'):self.result(base)


if __name__ == '__main__':
    unittest.main()
