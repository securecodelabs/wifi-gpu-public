"""Bounded HC22000 stdin, file parity and checkpoint regressions; synthetic only."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    fixture = (Path(__file__).parent / 'fixtures/all-types.hc22000').read_bytes()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='wifi-gpu-stdin-') as directory:
        root = Path(directory)

        def run(options, data=b'', code=0):
            nonlocal checks
            result = subprocess.run([str(exe), *map(str, options)], check=False, input=data,
                                    capture_output=True, cwd=root, timeout=30)
            assert result.returncode == code, (options, result.returncode, result.stdout, result.stderr)
            checks += 1
            return result

        hashes = root / 'source.hc22000'
        hashes.write_bytes(fixture)
        piped = run(['inspect', '--hashes', '-'], fixture)
        regular = run(['inspect', '--hashes', hashes])
        assert piped.stdout == regular.stdout and b'records=37' in piped.stdout

        base = ['recover', '--cpu', '--threads', '2', '--mask', 'labpass42']
        run([*base, '--hashes', '-', '--output', 'stdin.json'], fixture)
        run([*base, '--hashes', hashes, '--output', 'file.json'])
        stream_result = json.loads((root / 'stdin.json').read_text())
        file_result = json.loads((root / 'file.json').read_text())
        assert stream_result['recovered'] == 37
        assert stream_result['job_id'] == file_result['job_id']
        assert stream_result['matches'] == file_result['matches']
        assert all(m['verified'] and m['password_hex'] == b'labpass42'.hex()
                   for m in stream_result['matches'])

        one = next(line for line in fixture.splitlines() if line.startswith(b'WPA*'))
        run(['inspect', '--hashes', '-'], b'\xef\xbb\xbf# comment\r\n\r\n' + one + b'\r\n' + one)
        dedup = run(['inspect', '--hashes', '-'], one + b'\n' + one + b'\n')
        assert b'records=1 duplicates=1' in dedup.stdout
        for invalid in (b'', b'\n# empty\n', b'not-a-hash\n', b'x' * 16387,
                        one + b'\n\x1anot-a-hash\n', b'\xff\xfeW\x00P\x00A\x00',
                        b'\n' * 1000001):
            run(['inspect', '--hashes', '-'], invalid, code=1)
        # A valid prefix must not start recovery when the remaining stream is invalid.
        run([*base, '--hashes', '-', '--output', 'invalid.json'],
            one + b'\nnot-a-hash\n', code=1)
        assert not (root / 'invalid.json').exists()

        # Large comments still count against the aggregate input limit.
        run(['inspect', '--hashes', '-'], (b'#' + b'x' * 16383 + b'\n') * 4097, code=1)

        # Raw CRLF bytes must hash exactly as a regular file does on Windows too.
        crlf = one + b'\r\n'
        hashes.write_bytes(crlf)
        run([*base, '--hashes', '-', '--output', 'crlf-stdin.json'], crlf)
        run([*base, '--hashes', hashes, '--output', 'crlf-file.json'])
        assert json.loads((root / 'crlf-stdin.json').read_text())['job_id'] == json.loads(
            (root / 'crlf-file.json').read_text())['job_id']

        search = ['recover', '--cpu', '--threads', '2', '--hashes', '-', '--mask', 'labpass4?d']
        run([*search, '--session', 'stdin.session', '--limit', '1'], one + b'\n', code=4)
        run([*search, '--resume', 'stdin.session'], one + b'\n# changed\n', code=1)
        run([*search, '--resume', 'stdin.session', '--output', 'resumed.json'], one + b'\n')
        assert json.loads((root / 'resumed.json').read_text())['recovered'] == 1

        # '-' is special only as the exact --hashes argument; ./- remains a file.
        (root / '-').write_bytes(one + b'\n')
        run(['inspect', '--hashes', './-'])
        words = root / 'words.txt'
        words.write_text('labpass42\n', encoding='ascii')
        run(['recover', '--cpu', '--hashes', '-', '--wordlist', words,
             '--output', words, '--overwrite'], one + b'\n', code=1)
        assert words.read_text() == 'labpass42\n'
    print(f'PASS: {checks} stdin, limits, file-parity and resume checks')


if __name__ == '__main__':
    main()
