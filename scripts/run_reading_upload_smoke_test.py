#!/usr/bin/env python3
"""Exercise confirmed recursive uploads against an isolated local server and SD fixture."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=ROOT / '.pio/build/simulator/program')
    parser.add_argument('--mode', action='append', help='Run only the named scenarios (repeatable)')
    parser.add_argument('--touch', action='store_true', help='Include touch skip checks (requires a touch simulator)')
    args = parser.parse_args()
    requests = []
    probes = []
    mode = 'ok'
    failed_document = None

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            reply = {}
            if self.path == '/api/v1/stats/summary':
                probes.append(self.path)
                status = 404 if mode in ('auto-unsupported', 'current-auto-unsupported') else 200
                # A catch-all proxy page must not count as the stats API.
                data = b'<html>ok</html>' if mode == 'auto-html' else json.dumps({'devices': []}).encode()
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            if mode in ('current-equal', 'current-download', 'current-menu-cold', 'folder-mixed', 'folder-equal', 'folder-ask', 'folder-ask-cancel', 'folder-local-ahead'):
                expected = json.loads((fs / 'expected-progress.json').read_text())
                if mode == 'current-menu-cold':
                    expected['document'] = hashlib.md5(b'unread.epub').hexdigest()
                if self.path.endswith(expected['document']):
                    reply = expected
                    if mode == 'folder-local-ahead':
                        reply['percentage'] *= 0.1
                    if mode in ('current-download', 'folder-mixed', 'folder-ask', 'folder-ask-cancel'):
                        reply = json.loads((fs / 'remote-progress.json').read_text())
            data = json.dumps(reply).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_PUT(self):
            nonlocal failed_document
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            assert self.headers['x-auth-user'] == 'smoke'
            assert self.headers['x-auth-key'] == hashlib.md5(b'smoke-password').hexdigest()
            requests.append((self.path, body))
            status = 200
            reply = {'until': 1, 'accepted': 1, 'timestamp': 1, 'document': body.get('document', '')}
            if mode == 'auth':
                status = 401
            if mode == 'unsupported' and self.path.startswith('/api/v1/'):
                status = 404
            if mode == 'folder-global-failure' and self.path == '/api/v1/stats/global':
                status = 500
            failure_endpoint = ('/api/v1/stats/books' if mode == 'folder-skip-stats' else
                                '/api/v1/clippings/' if mode == 'folder-skip-clippings' else '/syncs/progress')
            if mode.startswith('folder-skip-') and 'invalid' not in mode and self.path.startswith(failure_endpoint):
                document = body.get('document') or (body.get('items') or [{}])[0].get('document') or self.path.rsplit('/', 1)[-1]
                if failed_document is None:
                    failed_document = document
                if document == failed_document or mode == 'folder-skip-all':
                    status = 500
            data = json.dumps(reply).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        modes = ['ok', 'folder-xtc', 'all-books', 'stats-entry', 'xtc-book', 'auto-probe', 'auto-unsupported', 'auto-html', 'disabled', 'cancel', 'auth', 'unsupported', 'folder-mixed', 'folder-equal', 'folder-local-ahead', 'folder-ask', 'folder-ask-cancel', 'folder-missing-cache', 'folder-skip-progress', 'folder-skip-stats', 'folder-skip-clippings', 'folder-skip-invalid', 'folder-skip-all', 'current-upload', 'current-auto', 'current-auto-unsupported', 'current-equal', 'current-download', 'current-menu', 'current-menu-cold', 'folder-no-progress', 'folder-legacy-progress', 'folder-zero-count', 'folder-past-end', 'folder-empty', 'folder-global-failure', 'folder-no-stats', 'folder-tracking-disabled']
        if args.touch:
            modes += ['folder-skip-progress-touch', 'folder-skip-invalid-touch']
        if args.mode:
            unknown = set(args.mode) - set(modes)
            if unknown:
                parser.error(f"Unknown scenarios: {', '.join(sorted(unknown))}")
            modes = args.mode
        for mode in modes:
            requests.clear()
            probes.clear()
            failed_document = None
            with tempfile.TemporaryDirectory(prefix='crossink-reading-upload-') as directory:
                fs = Path(directory) / 'fs_'
                for folder in ('read/sub', 'read/.hidden', 'read-sibling'):
                    (fs / folder).mkdir(parents=True)
                fixture = ROOT / 'test/epubs/test_reader_rendering_matrix.epub'
                for book in ('read/first.epub', 'read/sub/second.EPUB', 'read/unread.epub',
                             'read/.hidden/hidden.epub', 'read-sibling/outside.epub'):
                    shutil.copy2(fixture, fs / book)
                (fs / 'read/notes.txt').write_text('Not an EPUB')
                env = os.environ.copy()
                env.update(SDL_VIDEODRIVER='dummy', CROSSINK_SIMULATOR_SMOKE_TEST='1',
                           CROSSINK_READING_TEST_SERVER=f'http://127.0.0.1:{server.server_port}')
                for key in tuple(env):
                    if key.startswith('CROSSINK_STATS_TEST_') or key.startswith('CROSSINK_READING_TEST_') and key != 'CROSSINK_READING_TEST_SERVER':
                        del env[key]
                if mode.startswith('folder-skip-'):
                    env['CROSSINK_READING_TEST_SKIP_FAILED'] = '1'
                    if mode == 'folder-skip-clippings':
                        env['CROSSINK_READING_TEST_SKIP_CLIPPINGS'] = '1'
                    if 'invalid' in mode:
                        env['CROSSINK_READING_TEST_INVALID_BOOK'] = '1'
                    if mode.endswith('-touch'):
                        env['CROSSINK_READING_TEST_TOUCH'] = '1'
                    if mode == 'folder-skip-all':
                        env['CROSSINK_READING_TEST_ALL_FAIL'] = '1'
                if mode in ('folder-xtc', 'all-books', 'stats-entry', 'xtc-book'):
                    env['CROSSINK_READING_TEST_XTC'] = '1'
                if mode == 'stats-entry':
                    env['CROSSINK_READING_TEST_STATS_ENTRY'] = '1'
                if mode == 'xtc-book':
                    env['CROSSINK_READING_TEST_XTC_BOOK'] = '1'
                if mode in ('all-books', 'stats-entry'):
                    env['CROSSINK_READING_TEST_ALL'] = '1'
                if mode.startswith('auto-') or mode.startswith('current-auto'):
                    env['CROSSINK_READING_TEST_AUTO'] = '1'
                if mode in ('disabled', 'cancel'):
                    env['CROSSINK_READING_TEST_' + mode.upper()] = '1'
                if mode.startswith('folder-ask'):
                    env['CROSSINK_READING_TEST_ASK'] = '1'
                if mode == 'folder-ask-cancel':
                    env['CROSSINK_READING_TEST_ASK_CANCEL'] = '1'
                variants = {'folder-no-progress': 'missing', 'folder-legacy-progress': 'legacy4',
                            'folder-zero-count': 'zero', 'folder-past-end': 'past-end'}
                if mode in variants:
                    env['CROSSINK_READING_TEST_PROGRESS_VARIANT'] = variants[mode]
                if mode == 'folder-empty':
                    env['CROSSINK_READING_TEST_EMPTY_FOLDER'] = '1'
                if mode == 'folder-no-stats':
                    env['CROSSINK_READING_TEST_NO_STATS'] = '1'
                if mode == 'folder-tracking-disabled':
                    env['CROSSINK_READING_TEST_TRACKING_DISABLED'] = '1'
                if mode == 'folder-missing-cache':
                    env['CROSSINK_READING_TEST_MISSING_CACHE'] = '1'
                if mode == 'auth':
                    env['CROSSINK_READING_TEST_ERROR'] = '1'
                if mode.startswith('current-'):
                    env['CROSSINK_READING_TEST_CURRENT'] = '1'
                if mode.startswith('current-menu'):
                    env['CROSSINK_READING_TEST_MENU'] = '1'
                if mode == 'current-menu-cold':
                    env['CROSSINK_READING_TEST_COLD'] = '1'
                result = subprocess.run([str(args.program.resolve())], cwd=directory, env=env,
                                        capture_output=True, text=True, timeout=120)
                log = result.stdout + result.stderr
                if result.returncode or 'Stats upload transport smoke passed' not in log:
                    print(log[-12000:])
                    raise RuntimeError(f'{mode}: simulator failed ({result.returncode})')
                if mode in ('folder-mixed', 'folder-ask', 'folder-equal', 'folder-ask-cancel'):
                    cache = fs / (fs / 'first-cache.txt').read_text().lstrip('/')
                    saved = (cache / 'progress.bin').read_bytes()
                    page = int.from_bytes(saved[2:4], 'little')
                    assert (page > 2 if mode in ('folder-mixed', 'folder-ask') else page == 2), (mode, page)
                progress = [body for path, body in requests if path == '/syncs/progress']
                stats = [body for path, body in requests if path == '/api/v1/stats/books']
                clippings = [(path, body) for path, body in requests if path.startswith('/api/v1/clippings/')]
                globals_sent = [body for path, body in requests if path == '/api/v1/stats/global']
                if mode in variants:
                    assert not progress and len(stats) == 2 and len(clippings) == 1 and len(globals_sent) == 1, requests
                    assert 'Finished: synced=0 skipped=3 failed=0' in log, log[-10000:]
                    assert 'Extras: global=0 stats=2 statsFailed=0 clippings=1 clippingsFailed=0' in log
                elif mode == 'folder-empty':
                    assert not progress and not stats and not clippings and len(globals_sent) == 1, requests
                elif mode in ('folder-no-stats', 'folder-tracking-disabled'):
                    assert len(progress) == 2 and not stats and not globals_sent and len(clippings) == 1, requests
                    assert 'Extras: global=7 stats=0 statsFailed=0 clippings=1 clippingsFailed=0' in log
                elif mode == 'folder-global-failure':
                    assert len(progress) == 2 and len(stats) == 2 and len(clippings) == 1 and len(globals_sent) == 1, requests
                    assert 'Finished: synced=2 skipped=1 failed=0' in log
                    assert 'Extras: global=6 stats=2 statsFailed=0 clippings=1 clippingsFailed=0' in log
                elif mode.startswith('folder-skip-'):
                    failed = 2 if mode == 'folder-skip-all' else 1
                    progress_synced = 0 if mode == 'folder-skip-all' else 1 if ('invalid' in mode or 'progress' in mode) else 2
                    assert f'Finished: synced={progress_synced} skipped=1 failed={failed}' in log, log[-10000:]
                    expected = {hashlib.md5(name.encode()).hexdigest() for name in ('first.epub', 'second.EPUB')}
                    assert len(globals_sent) == 1 and len(stats) == 2, requests
                    if 'invalid' in mode:
                        assert {b['document'] for b in progress} == {hashlib.md5(b'second.EPUB').hexdigest()}, requests
                    else:
                        assert {b['document'] for b in progress} == expected and len(progress) == 2, requests
                    assert len(clippings) == (2 if mode == 'folder-skip-clippings' else 1), requests
                    if mode == 'folder-skip-stats':
                        assert 'Extras: global=0 stats=1 statsFailed=1 clippings=1 clippingsFailed=0' in log
                    if mode == 'folder-skip-clippings':
                        assert 'Extras: global=0 stats=2 statsFailed=0 clippings=1 clippingsFailed=1' in log
                elif mode == 'current-auto-unsupported':
                    # Single-book sync probes once after freeing the EPUB, then skips extras.
                    assert len(probes) == 1 and len(progress) == 1 and len(requests) == 1, (probes, requests)
                elif mode == 'current-menu-cold':
                    assert not progress and not stats and not clippings, requests
                    assert sum(path == '/api/v1/stats/global' for path, _ in requests) == 1
                elif mode.startswith('current-'):
                    assert len(progress) == (1 if mode in ('current-upload', 'current-auto', 'current-menu') else 0), requests
                    assert len(probes) == (1 if mode == 'current-auto' else 0), probes
                    assert len(stats) == 1 and len(clippings) == 1, requests
                    expected = hashlib.md5(b'first.epub').hexdigest()
                    assert all(body['document'] == expected for body in progress)
                    assert stats[0]['items'][0]['document'] == expected
                    assert clippings[0][0].endswith(expected)
                    assert sum(path == '/api/v1/stats/global' for path, _ in requests) == 1
                elif mode == 'cancel':
                    assert not requests, requests
                elif mode == 'folder-ask-cancel':
                    assert len(globals_sent) == 1 and len(requests) == 1, requests
                elif mode == 'auth':
                    assert len(globals_sent) == 1 and len(progress) == 1 and len(stats) == 1 and len(clippings) == 1, requests
                elif mode == 'xtc-book':
                    # Sync Book on an XTC: overall plus that book's stats, no position or clippings.
                    assert not progress and not clippings and len(globals_sent) == 1 and len(stats) == 1, requests
                    assert stats[0]['items'][0]['document'] == hashlib.md5(b'comic.xtc').hexdigest()
                    assert 'Finished: synced=1 skipped=0 failed=0' in log, log[-10000:]
                elif mode in ('folder-xtc', 'all-books', 'stats-entry'):
                    # XTC adds a stats upload without a position; Library scope also reaches
                    # books outside /read (outside.epub has no saved progress).
                    expected = {hashlib.md5(name.encode()).hexdigest() for name in ('first.epub', 'second.EPUB', 'comic.xtc')}
                    assert len(progress) == 2 and len(globals_sent) == 1 and len(clippings) == 1, requests
                    assert {b['items'][0]['document'] for b in stats} == expected and len(stats) == 3, requests
                    # The XTC counts as synced; unread.epub (and, for Library scope, the
                    # deleted outside.epub) are skipped.
                    skipped = 1 if mode == 'folder-xtc' else 2
                    assert f'Finished: synced=3 skipped={skipped} failed=0' in log, log[-10000:]
                    assert 'Extras: global=0 stats=3 statsFailed=0 clippings=1 clippingsFailed=0' in log, log[-10000:]
                elif mode in ('auto-unsupported', 'auto-html'):
                    # A KOSync-only server answers the probe with 404: progress syncs, extras never attempt.
                    assert len(probes) == 1 and len(progress) == 2 and len(requests) == 2, (probes, requests)
                    assert 'Finished: synced=2 skipped=1 failed=0' in log, log[-10000:]
                    assert 'Extras: global=7 stats=0 statsFailed=0 clippings=0 clippingsFailed=0' in log, log[-10000:]
                elif mode == 'unsupported':
                    # The first 404 marks the server progress-only: later books skip extras
                    # and the folder finishes instead of stopping on an "extra upload failed" error.
                    assert len(globals_sent) == 1 and len(progress) == 2 and not stats and not clippings, requests
                    assert 'Finished: synced=2 skipped=1 failed=0' in log, log[-10000:]
                    assert 'Extras: global=3 stats=0 statsFailed=0 clippings=0 clippingsFailed=0' in log, log[-10000:]
                else:
                    assert len(progress) == (1 if mode in ('folder-mixed', 'folder-equal', 'folder-ask') else 2), requests
                    assert all(0 < body['percentage'] < 1 and body['progress'] for body in progress)
                    expected = {hashlib.md5(name.encode()).hexdigest() for name in ('first.epub', 'second.EPUB')}
                    assert {body['document'] for body in progress} == (expected - {hashlib.md5(b'first.epub').hexdigest()} if mode in ('folder-mixed', 'folder-equal', 'folder-ask') else expected)
                    assert len(probes) == (1 if mode == 'auto-probe' else 0), probes
                    if mode == 'disabled':
                        assert len(requests) == 2
                    else:
                        assert len(stats) == 2 and {b['items'][0]['document'] for b in stats} == expected
                        assert sum(path == '/api/v1/stats/global' for path, _ in requests) == 1
                        assert len(clippings) == 1
                        item = clippings[0][1]['items'][0]
                        assert item['id'] == hashlib.sha256((str(item['created_at']) + item['text']).encode()).hexdigest()[:16]
                        assert item['text'] == 'Quoted "text" — café\nnext line'
                        assert item['layout_signature'] == 1234
                        assert all(key not in item for key in ('note', 'color', 'deleted', 'para'))
                        assert clippings[0][0].endswith(hashlib.md5(b'first.epub').hexdigest())
                print(f'PASS: {mode} reading sync activity, request scope and payloads')
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    main()
