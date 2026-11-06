"""Exercise flash ordering with a fake esptool. Never access actual serial ports."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/flash_mac.sh'


class TestFlashHelper(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'tools').mkdir()
        (self.root / 'dist').mkdir()
        shutil.copy2(SCRIPT, self.root / 'tools/flash_mac.sh')
        lines = []
        for name in ('bootloader.bin', 'partition-table.bin', 'note4c_timer.bin'):
            data = b'fake-test-binary-' + name.encode()
            (self.root / 'dist' / name).write_bytes(data)
            lines.append(hashlib.sha256(data).hexdigest() + '  ' + name)
        (self.root / 'dist/SHA256SUMS').write_text('\n'.join(lines) + '\n')
        self.fake = self.root / 'fake'
        self.fake.mkdir()
        (self.fake / 'esptool.py').write_text('''
__version__ = '4.9.0'
if __name__ == '__main__':
    import json, os, sys
    from pathlib import Path
    with open(os.environ['FAKE_LOG'], 'a') as f:
        f.write(json.dumps(sys.argv[1:]) + '\\n')
    if 'read_flash' in sys.argv:
        with open(sys.argv[-1], 'wb') as f:
            f.truncate(10 if os.environ.get('SHORT_BACKUP') else 0x1000000)
''')
        self.log = self.root / 'commands.jsonl'
        self.env = dict(os.environ, PYTHONPATH=str(self.fake), FAKE_LOG=str(self.log))

    def run_helper(self, mode='--update', answer='FLASH\n'):
        return subprocess.run(['bash', str(self.root / 'tools/flash_mac.sh'), '/dev/fake port', mode], input=answer, text=True, capture_output=True, env=self.env, timeout=30)

    def commands(self):
        import json
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_update_backups_before_write_and_never_erases(self):
        result = self.run_helper()
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertEqual([c[10] for c in commands], ['read_flash', 'write_flash', 'verify_flash'])
        self.assertEqual([c[6:10] for c in commands], [['--before', 'default_reset', '--after', 'no_reset'], ['--before', 'default_reset', '--after', 'no_reset'], ['--before', 'default_reset', '--after', 'hard_reset']])
        self.assertTrue(all(c[3] == '/dev/fake port' for c in commands))
        backups = list((self.root / 'backups').glob('*/full-flash-16MiB.bin'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].stat().st_size, 0x1000000)
        self.assertTrue(backups[0].with_suffix('.bin.sha256').exists())

    def test_install_erases_only_after_backup(self):
        result = self.run_helper('--install')
        self.assertEqual(result.returncode, 0, result.stderr)
        commands = self.commands()
        self.assertEqual([c[10] for c in commands], ['read_flash', 'erase_region', 'write_flash', 'verify_flash'])
        self.assertEqual([c[9] for c in commands], ['no_reset', 'no_reset', 'no_reset', 'hard_reset'])
        self.assertTrue(all(c[6:9] == ['--before', 'default_reset', '--after'] for c in commands))
        self.assertEqual(commands[1][11:], ['0x300000', '0xd00000'])
        self.assertEqual(commands[2][11::2], ['0x0', '0x8000', '0x10000'])

    def test_cancel_retains_backup_and_never_writes(self):
        result = self.run_helper('--install', 'no\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([c[10] for c in self.commands()], ['read_flash'])
        self.assertIn('Cancelled', result.stdout)
        self.assertIn('remains in download mode', result.stdout)
        self.assertEqual(self.commands()[0][9], 'no_reset')

    def test_short_backup_refuses_write(self):
        self.env['SHORT_BACKUP'] = '1'
        result = self.run_helper('--install')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual([c[10] for c in self.commands()], ['read_flash'])
        self.assertIn('Incomplete backup', result.stderr)

    def test_corrupt_artifact_prevents_device_access(self):
        (self.root / 'dist/bootloader.bin').write_bytes(b'tampered')
        result = self.run_helper('--install')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.commands(), [])
        self.assertIn('Checksum mismatch', result.stderr)

    def test_mode_required(self):
        result = self.run_helper('--erase')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.commands(), [])


if __name__ == '__main__':
    unittest.main()
