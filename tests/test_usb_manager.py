import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('note4c', Path(__file__).resolve().parents[1] / 'tools/note4c.py')
note4c = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(note4c)


class FakeSerial:
    def __init__(self, chunks):
        self.chunks = list(chunks)
        self.written = b''
        self.closed = False

    @property
    def in_waiting(self):
        return len(self.chunks[0]) if self.chunks else 0

    def read(self, size):
        return self.chunks.pop(0) if self.chunks else b''

    def write(self, data):
        self.written += data
        return len(data)

    def close(self):
        self.closed = True


class TestProtocol(unittest.TestCase):
    def test_fragmented_boot_and_matching_id(self):
        serial = FakeSerial([b'ESP-ROM: boot\r\n', b'{"id":99,"ok":true,"result":0}\n{"id":1,', b'"ok":true,"result":{"data_errors":["bad history"]}}\r\n'])
        with note4c.Device(serial) as device:
            self.assertEqual(device.request('status'), {'data_errors': ['bad history']})
        self.assertTrue(serial.closed)
        self.assertEqual(json.loads(serial.written), {'id': 1, 'cmd': 'status'})

    def test_buffered_multiple_responses(self):
        serial = FakeSerial([b'{"id":1,"ok":true,"result":1}\n{"id":2,"ok":true,"result":2}\n'])
        device = note4c.Device(serial)
        self.assertEqual(device.request('status'), 1)
        self.assertEqual(device.request('status'), 2)

    def test_invalid_responses(self):
        for response in (b'{broken}\n', b'[]\n', b'{"id":1,"ok":1,"result":0}\n', b'{"id":1,"ok":true}\n', b'{"id":true,"ok":true,"result":0}\n', b'{"id":1,"ok":false}\n', b'{"id":1,"id":1,"ok":true,"result":0}\n', b'{"id":1,"ok":true,"result":NaN}\n'):
            with self.subTest(response=response), self.assertRaises(note4c.ManagerError):
                note4c.Device(FakeSerial([response])).request('status')

    def test_error_preserved(self):
        with self.assertRaisesRegex(note4c.ManagerError, 'corrupt history'):
            note4c.Device(FakeSerial([b'{"id":1,"ok":false,"error":"corrupt history"}\n'])).request('get_history')

    def test_timeout_closes(self):
        serial = FakeSerial([b'boot\n'])
        with self.assertRaisesRegex(note4c.ManagerError, 'timed out'):
            with note4c.Device(serial, timeout=0.001) as device:
                device.request('status')
        self.assertTrue(serial.closed)

    def test_oversized_response(self):
        with patch.object(note4c, 'MAX_BYTES', 32), self.assertRaisesRegex(note4c.ManagerError, 'exceeds'):
            note4c.Device(FakeSerial([b'x' * 33])).request('status')

    def test_partial_write(self):
        serial = FakeSerial([])
        serial.write = lambda data: 1
        with self.assertRaisesRegex(note4c.ManagerError, 'Incomplete USB write'):
            note4c.Device(serial).request('start')


class TestActions(unittest.TestCase):
    def valid(self):
        return {'schema_version': 1, 'actions': [{'id': 'brush_teeth', 'name': 'Brush teeth'}]}

    def test_valid(self):
        self.assertEqual(note4c.validate_actions(self.valid()), self.valid())

    def test_chinese_names(self):
        value = self.valid()
        value['actions'][0]['name'] = '刷牙'
        self.assertEqual(note4c.validate_actions(value), value)
        value['actions'][0]['name'] = '中' * 24
        self.assertEqual(note4c.validate_actions(value), value)
        for name in ('中' * 25, '刷牙😀', 'e\u0301', '\u3400', '\U00020000'):
            value['actions'][0]['name'] = name
            with self.assertRaises(note4c.ManagerError):
                note4c.validate_actions(value)

    def test_invalid(self):
        cases = [None, {}, {'schema_version': True, 'actions': []}]
        for ident, name in [('Caps', 'Name'), ('x' * 33, 'Name'), ('x', ''), ('x', ' '), ('x', 'é'), ('x', 'a' * 25), ('x', 'bad\nname')]:
            cases.append({'schema_version': 1, 'actions': [{'id': ident, 'name': name}]})
        cases += [{'schema_version': 1, 'actions': []}, {'schema_version': 1, 'actions': self.valid()['actions'] * 2}]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(note4c.ManagerError):
                note4c.validate_actions(value)

    def test_malformed_file(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'actions.json'
            for data in (b'{bad', b'\xff', b'{"schema_version":1,"schema_version":1,"actions":[]}'):
                path.write_bytes(data)
                with self.assertRaisesRegex(note4c.ManagerError, 'Invalid actions JSON'):
                    note4c.load_actions(path)

    def test_bad_file_before_serial_import(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'actions.json'
            path.write_text('{bad')
            with patch('sys.stderr', new_callable=io.StringIO) as err:
                self.assertEqual(note4c.main(['actions', 'put', str(path)]), 1)
                self.assertIn('Invalid actions JSON', err.getvalue())


class TestOutputAndPorts(unittest.TestCase):
    def test_atomic_json_replace(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'history.json'
            path.write_text('old')
            result = {'schema_version': 1, 'records': []}
            note4c.atomic_output(path, result)
            self.assertEqual(json.loads(path.read_text()), result)
            self.assertEqual(list(Path(temp).iterdir()), [path])

    def test_failed_replace_preserves_destination_and_cleans_temp(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'history.json'
            path.write_text('old')
            with patch.object(note4c.os, 'replace', side_effect=OSError('disk error')):
                with self.assertRaises(OSError):
                    note4c.atomic_output(path, {'records': []})
            self.assertEqual(path.read_text(), 'old')
            self.assertEqual(list(Path(temp).iterdir()), [path])

    def test_oversize_preserves_destination(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'history.json'
            path.write_text('old')
            with patch.object(note4c, 'MAX_BYTES', 16), self.assertRaises(note4c.ManagerError):
                note4c.atomic_output(path, {'records': ['x' * 40]})
            self.assertEqual(path.read_text(), 'old')

    def test_explicit_and_unique_port(self):
        from types import SimpleNamespace
        port = SimpleNamespace(device='/dev/cu.usbmodem123', vid=0x303A)
        self.assertEqual(note4c.choose_port(None, [port]), port.device)
        self.assertEqual(note4c.choose_port('/dev/custom', []), '/dev/custom')
        for ports in ([], [port, port]):
            with self.assertRaises(note4c.ManagerError):
                note4c.choose_port(None, ports)


class TestCli(unittest.TestCase):
    def connection(self, result):
        from types import SimpleNamespace
        serial = FakeSerial([(json.dumps({'id': 1, 'ok': True, 'result': result}) + '\n').encode()])
        serial.is_open = False
        def open_port():
            serial.is_open = True
        def close_port():
            serial.closed = True
            serial.is_open = False
        serial.open = open_port
        serial.close = close_port
        package = SimpleNamespace(Serial=lambda **kwargs: serial)
        ports = SimpleNamespace(comports=lambda: [])
        modules = {'serial': package, 'serial.tools': SimpleNamespace(list_ports=ports)}
        return serial, modules

    def test_status_keeps_data_errors_and_closes(self):
        serial, modules = self.connection({'data_errors': ['history damaged']})
        with patch.dict('sys.modules', modules), patch('sys.stdout', new_callable=io.StringIO) as out:
            self.assertEqual(note4c.main(['--port', '/dev/example', 'status']), 0)
            self.assertIn('history damaged', out.getvalue())
        self.assertTrue(serial.closed)
        self.assertFalse(serial.dtr)
        self.assertFalse(serial.rts)

    def test_time_sync_utc_epoch(self):
        serial, modules = self.connection({'updated': True})
        with patch.dict('sys.modules', modules), patch.object(note4c.time, 'time', return_value=1800000000), patch('sys.stdout', new_callable=io.StringIO):
            self.assertEqual(note4c.main(['--port', '/dev/example', 'time-sync']), 0)
        self.assertEqual(json.loads(serial.written)['epoch'], 1800000000)
        self.assertEqual(json.loads(serial.written)['cmd'], 'set_time')
        self.assertIn('utc_offset_minutes', json.loads(serial.written))

    def test_time_sync_defaults_to_mac_offset(self):
        from datetime import timedelta
        serial, modules = self.connection({'updated': True})
        with patch.dict('sys.modules', modules), patch.object(note4c, 'datetime') as clock, patch.object(note4c.time, 'time', return_value=1800000000), patch('sys.stdout', new_callable=io.StringIO) as out:
            clock.now.return_value.astimezone.return_value.utcoffset.return_value = timedelta(hours=-3, minutes=-30)
            self.assertEqual(note4c.main(['--port', '/dev/example', 'time-sync']), 0)
            payload = json.loads(serial.written)
            self.assertEqual(payload['utc_offset_minutes'], -210)
            self.assertEqual(payload['epoch'], 1800000000)
            self.assertIn('UTC-03:30', out.getvalue())

    def test_time_sync_override_keeps_utc_epoch(self):
        serial, modules = self.connection({'updated': True})
        with patch.dict('sys.modules', modules), patch.object(note4c.time, 'time', return_value=1800000000), patch.object(note4c, 'local_utc_offset', side_effect=AssertionError('must not use local offset')), patch('sys.stdout', new_callable=io.StringIO) as out:
            self.assertEqual(note4c.main(['--port', '/dev/example', 'time-sync', '--utc-offset', '+08:00']), 0)
            self.assertEqual(json.loads(serial.written), {'id': 1, 'cmd': 'set_time', 'epoch': 1800000000, 'utc_offset_minutes': 480})
            self.assertIn('UTC+08:00', out.getvalue())

    def test_timezone_changes_offset_only(self):
        serial, modules = self.connection({'utc_offset_minutes': -720})
        with patch.dict('sys.modules', modules), patch('sys.stdout', new_callable=io.StringIO) as out:
            self.assertEqual(note4c.main(['--port', '/dev/example', 'timezone', '--utc-offset', '-12:00']), 0)
            self.assertEqual(json.loads(serial.written), {'id': 1, 'cmd': 'set_utc_offset', 'utc_offset_minutes': -720})
            self.assertIn('UTC-12:00', out.getvalue())

    def test_offset_validation(self):
        import argparse
        for text, expected in (('+14:00', 840), ('-12:00', -720), ('+00:00', 0), ('-00:00', 0), ('+05:45', 345)):
            self.assertEqual(note4c.parse_utc_offset(text), expected)
        for text in ('08:00', '+8:00', '+08:0', '+14:01', '-12:01', '+00:60', '+25:00', '+08:00x', 'UTC+08:00', '+08:00\n'):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                note4c.parse_utc_offset(text)

    def test_invalid_offset_before_serial(self):
        with patch('sys.stderr', new_callable=io.StringIO), self.assertRaises(SystemExit) as error:
            note4c.main(['timezone', '--utc-offset', '-13:00'])
        self.assertEqual(error.exception.code, 2)

    def test_status_labels_offset_and_preserves_fields(self):
        result = note4c.display_result({'utc_offset_minutes': 480, 'epoch': 1800000000, 'data_errors': ['bad history']})
        self.assertEqual(result['utc_offset'], 'UTC+08:00')
        self.assertEqual(result['epoch'], 1800000000)
        self.assertEqual(result['data_errors'], ['bad history'])

    def test_history_output(self):
        result = {'schema_version': 1, 'records': [{'duration_ms': 100}]}
        serial, modules = self.connection(result)
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / 'history.json'
            with patch.dict('sys.modules', modules), patch('sys.stdout', new_callable=io.StringIO):
                self.assertEqual(note4c.main(['--port', '/dev/example', 'history', '--output', str(target)]), 0)
            self.assertEqual(json.loads(target.read_text()), result)
        self.assertEqual(json.loads(serial.written)['cmd'], 'get_history')

    def test_invalid_history_leaves_file(self):
        serial, modules = self.connection({'schema_version': 1, 'records': None})
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / 'history.json'
            target.write_text('old')
            with patch.dict('sys.modules', modules), patch('sys.stderr', new_callable=io.StringIO):
                self.assertEqual(note4c.main(['--port', '/dev/example', 'history', '--output', str(target)]), 1)
            self.assertEqual(target.read_text(), 'old')
        self.assertTrue(serial.closed)


if __name__ == '__main__':
    unittest.main()
