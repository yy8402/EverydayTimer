#!/usr/bin/env python3
"""Offline USB manager for Note4C Timer. Requires pyserial for device access."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import time

MAX_BYTES = 8 * 1024 * 1024
MIN_EPOCH = 1704067200  # 2024-01-01 UTC
MAX_EPOCH = 4102444800  # 2100-01-01 UTC (exclusive)


class ManagerError(Exception):
    """An actionable device, input, or protocol failure."""


def parse_json(data):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    def bad_constant(value):
        raise ValueError('non-finite JSON number: ' + value)
    return json.loads(data, object_pairs_hook=pairs, parse_constant=bad_constant)


def validate_actions(value):
    if not isinstance(value, dict) or set(value) != {'schema_version', 'actions'}:
        raise ManagerError('Actions must contain only schema_version and actions.')
    if type(value['schema_version']) is not int or value['schema_version'] != 1:
        raise ManagerError('Unsupported actions schema_version (expected 1).')
    actions = value['actions']
    if not isinstance(actions, list) or not 1 <= len(actions) <= 32:
        raise ManagerError('Provide 1 to 32 actions.')
    ids = set()
    for action in actions:
        if not isinstance(action, dict) or set(action) != {'id', 'name'}:
            raise ManagerError('Each action must contain only id and name.')
        ident, name = action['id'], action['name']
        if not isinstance(ident, str) or not re.fullmatch(r'[a-z0-9_-]{1,32}', ident):
            raise ManagerError('Action IDs must be 1..32 lowercase letters, digits, _ or -.')
        if ident in ids:
            raise ManagerError('Duplicate action ID: ' + ident)
        ids.add(ident)
        if not isinstance(name, str) or not 1 <= len(name) <= 24 or len(name.encode('utf-8')) > 72 or not all(32 <= ord(c) <= 126 or 0x4E00 <= ord(c) <= 0x9FFF for c in name) or not name.strip():
            raise ManagerError('Action names must be 1..24 ASCII/basic Chinese characters (at most 72 UTF-8 bytes), not blank.')
    return value


def load_actions(path):
    with open(path, 'rb') as source:
        data = source.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ManagerError('Actions file exceeds 8 MiB.')
    try:
        return validate_actions(parse_json(data.decode('utf-8')))
    except (ValueError, UnicodeError) as exc:
        raise ManagerError('Invalid actions JSON: ' + str(exc)) from exc


def atomic_output(path, value):
    """Replace destination only after the complete JSON is serialized and fsynced."""
    data = (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')
    if len(data) > MAX_BYTES:
        # Compact formatting keeps a valid large response within the export bound.
        data = (json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False) + '\n').encode('utf-8')
    if len(data) > MAX_BYTES:
        raise ManagerError('Export exceeds 8 MiB; destination was not changed.')
    path = Path(path)
    temp = None
    try:
        with tempfile.NamedTemporaryFile(mode='wb', prefix='.' + path.name + '.', suffix='.tmp', dir=path.parent, delete=False) as target:
            temp = target.name
            target.write(data)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temp, path)
        temp = None
    finally:
        if temp is not None:
            os.unlink(temp)


class Device:
    def __init__(self, serial_port, timeout=15.0):
        self.serial = serial_port
        self.timeout = timeout
        self.buffer = bytearray()
        self.next_id = 1

    def close(self):
        self.serial.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def request(self, command, **fields):
        request_id = self.next_id
        self.next_id += 1
        payload = (json.dumps(dict(fields, id=request_id, cmd=command), separators=(',', ':'), allow_nan=False) + '\n').encode('utf-8')
        if self.serial.write(payload) != len(payload):
            raise ManagerError('Incomplete USB write; command outcome is unknown. Check status before retrying.')
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            if b'\n' not in self.buffer:
                chunk = self.serial.read(min(max(getattr(self.serial, 'in_waiting', 0), 1), 65536))
                self.buffer.extend(chunk)
                if len(self.buffer) > MAX_BYTES:
                    raise ManagerError('USB response exceeds 8 MiB.')
                if b'\n' not in self.buffer:
                    continue
            line, _, tail = self.buffer.partition(b'\n')
            self.buffer = bytearray(tail)
            line = line.strip()
            if not line or not line.startswith((b'{', b'[')):
                continue  # ROM boot diagnostics and other plain-text logs
            try:
                response = parse_json(line.decode('utf-8'))
            except (ValueError, UnicodeError) as exc:
                raise ManagerError('Malformed USB JSON response: ' + str(exc)) from exc
            if not isinstance(response, dict):
                raise ManagerError('USB response must be an object.')
            if type(response.get('id')) is not int:
                raise ManagerError('USB response has no integer request ID.')
            if response['id'] != request_id:
                continue
            if type(response.get('ok')) is not bool:
                raise ManagerError('USB response has no boolean ok field.')
            if not response['ok']:
                if not isinstance(response.get('error'), str):
                    raise ManagerError('USB error response has no error message.')
                raise ManagerError('Device error: ' + response['error'])
            if 'result' not in response:
                raise ManagerError('USB success response has no result.')
            return response['result']
        raise ManagerError('USB response timed out; command outcome may be unknown. Check status before retrying.')


def candidate_ports(ports):
    return [p for p in ports if p.vid == 0x303A or 'usbmodem' in p.device.lower() or 'ttyacm' in p.device.lower()]


def choose_port(explicit, ports):
    if explicit:
        return explicit
    candidates = candidate_ports(ports)
    if len(candidates) != 1:
        raise ManagerError('Specify --port explicitly: found %d candidate USB ports. Use ports to list them.' % len(candidates))
    return candidates[0].device


def parse_utc_offset(text):
    if not re.fullmatch(r'[+-][0-9]{2}:[0-9]{2}', text):
        raise argparse.ArgumentTypeError('UTC offset must use signed HH:MM, e.g. +08:00 or -05:00.')
    hours, minutes = map(int, text[1:].split(':'))
    value = (hours * 60 + minutes) * (-1 if text[0] == '-' else 1)
    if minutes >= 60 or not -720 <= value <= 840:
        raise argparse.ArgumentTypeError('UTC offset must be between -12:00 and +14:00.')
    return value


def local_utc_offset():
    offset = datetime.now().astimezone().utcoffset()
    if offset is None or offset.total_seconds() % 60:
        raise ManagerError('Cannot determine a whole-minute local UTC offset; specify --utc-offset.')
    minutes = int(offset.total_seconds() / 60)
    if not -720 <= minutes <= 840:
        raise ManagerError('Computer UTC offset must be between -12:00 and +14:00.')
    return minutes


def offset_label(minutes):
    return 'UTC%s%02d:%02d' % ('-' if minutes < 0 else '+', abs(minutes) // 60, abs(minutes) % 60)


def display_result(result, applied_offset=None):
    # Preserve every device status/error field, adding a human-readable offset.
    if isinstance(result, dict):
        result = dict(result)
        if applied_offset is not None:
            result.setdefault('utc_offset_minutes', applied_offset)
        minutes = result.get('utc_offset_minutes')
        if type(minutes) is int and -720 <= minutes <= 840:
            result['utc_offset'] = offset_label(minutes)
    elif applied_offset is not None:
        result = {'result': result, 'utc_offset_minutes': applied_offset,
                  'utc_offset': offset_label(applied_offset)}
    return result


def parser():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--port', help='USB serial port, e.g. /dev/cu.usbmodem1101')
    cli.add_argument('--timeout', type=float, default=15, help='Response timeout in seconds (default: 15)')
    commands = cli.add_subparsers(dest='command', required=True)
    for name in ('ports', 'status', 'start', 'stop'):
        commands.add_parser(name)
    commands.add_parser('time-sync').add_argument('--utc-offset', type=parse_utc_offset, help='Local display offset; default: current Mac offset')
    commands.add_parser('timezone').add_argument('--utc-offset', type=parse_utc_offset, required=True, help='Fixed local display offset, e.g. +08:00')
    actions = commands.add_parser('actions').add_subparsers(dest='action_command', required=True)
    actions.add_parser('get').add_argument('--output', required=True, type=Path)
    actions.add_parser('put').add_argument('file', type=Path)
    commands.add_parser('clear-history').add_argument('--confirm', required=True, choices=['CLEAR'])
    commands.add_parser('history').add_argument('--output', required=True, type=Path)
    return cli


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    # argparse otherwise treats a negative HH:MM value as another option.
    for index in range(len(argv) - 2, -1, -1):
        if argv[index] == '--utc-offset' and re.fullmatch(r'[+-][0-9]{2}:[0-9]{2}', argv[index + 1]):
            argv[index:index + 2] = ['--utc-offset=' + argv[index + 1]]
    args = parser().parse_args(argv)
    try:
        if not 0 < args.timeout <= 300:
            raise ManagerError('--timeout must be greater than zero and at most 300 seconds.')
        applied_offset = None
        if args.command == 'time-sync':
            applied_offset = args.utc_offset if args.utc_offset is not None else local_utc_offset()
        elif args.command == 'timezone':
            applied_offset = args.utc_offset
        # Validate local input before opening (and possibly resetting) a device.
        actions = load_actions(args.file) if args.command == 'actions' and args.action_command == 'put' else None
        try:
            import serial
            from serial.tools import list_ports
        except ImportError as exc:
            raise ManagerError('Install pyserial: python3 -m pip install -r tools/requirements.txt') from exc
        ports = list(list_ports.comports())
        if args.command == 'ports':
            for port in ports:
                print('%s\t%s' % (port.device, port.description))
            return 0
        port = choose_port(args.port, ports)
        # Set DTR/RTS before opening, avoiding intentional boot/reset toggles.
        connection = serial.Serial(port=None, baudrate=115200, timeout=min(0.25, args.timeout), write_timeout=args.timeout)
        connection.dtr = False
        connection.rts = False
        connection.port = port
        try:
            connection.open()
            with Device(connection, args.timeout) as device:
                if args.command == 'actions':
                    result = device.request('set_actions', actions=actions) if args.action_command == 'put' else validate_actions(device.request('get_actions'))
                elif args.command == 'clear-history':
                    result = device.request('clear_history', confirm=args.confirm)
                elif args.command == 'history':
                    result = device.request('get_history')
                    if not isinstance(result, dict) or type(result.get('schema_version')) is not int or result['schema_version'] != 1 or not isinstance(result.get('records'), list):
                        raise ManagerError('Invalid history schema returned by device.')
                elif args.command == 'time-sync':
                    epoch = int(time.time())
                    if not MIN_EPOCH <= epoch < MAX_EPOCH:
                        raise ManagerError('Computer clock must be within 2024..2099 UTC.')
                    result = device.request('set_time', epoch=epoch, utc_offset_minutes=applied_offset)
                elif args.command == 'timezone':
                    result = device.request('set_utc_offset', utc_offset_minutes=applied_offset)
                else:
                    result = device.request(args.command)
        finally:
            if connection.is_open:
                connection.close()
        if getattr(args, 'output', None):
            atomic_output(args.output, result)
            print('Saved ' + str(args.output))
        else:
            print(json.dumps(display_result(result, applied_offset), ensure_ascii=False, indent=2, allow_nan=False))
        return 0
    except (ManagerError, OSError, ValueError) as exc:
        print('Error: ' + str(exc), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('Interrupted; check device status before retrying a command.', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
