#!/usr/bin/env python3
"""Local Stellar authoring service backed by Sagas data packages."""
from pathlib import Path
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import unquote, urlsplit, parse_qs
import argparse, json, mimetypes, subprocess
from stellar_shared import atomic_json
from converter import export_audio
from stellar_roster import (
    initial, import_character, import_directory, import_project, preview, project_file, save_roster,
)
from stellar_scenes import load_scenes, save_scenes
from stellar_hd import import_hd

ROOT = Path(__file__).resolve().parents[2]
STATIC = {
    '/': ('index.html', 'text/html; charset=utf-8'),
    '/studio.css': ('studio.css', 'text/css; charset=utf-8'),
    '/studio.js': ('studio.js', 'text/javascript; charset=utf-8'),
    '/scenes.js': ('scenes.js', 'text/javascript; charset=utf-8'),
}


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, data, kind='application/json'):
        payload = json.dumps(data).encode() if kind == 'application/json' else data
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        url = urlsplit(self.path)
        root = self.server.asset_root
        try:
            if url.path in STATIC:
                name, kind = STATIC[url.path]
                return self.reply(200, Path(__file__).with_name(name).read_bytes(), kind)
            if url.path == '/api/packages':
                return self.reply(200, [json.loads(p.read_text()) for p in sorted((root / 'mods/characters').glob('*/character.json'))] + [json.loads(p.read_text()) for p in sorted((root / 'mods/hd').glob('*/character.json'))])
            if url.path == '/api/roster':
                file = root / 'mods/roster.json'
                return self.reply(200, json.loads(file.read_text()) if file.exists() else initial())
            if url.path == '/api/project':
                return self.reply(200, json.loads(project_file(root, parse_qs(url.query)['id'][0]).read_text()))
            if url.path == '/api/scenes':
                return self.reply(200, load_scenes(root))
            if url.path.startswith('/assets/'):
                path = (root / unquote(url.path[len('/assets/'):])).resolve()
                if not path.is_relative_to(root.resolve()):
                    raise ValueError('Invalid asset path')
                return self.reply(200, path.read_bytes(), mimetypes.guess_type(path)[0] or 'application/octet-stream')
            self.reply(404, {'error': 'Not found'})
        except (ValueError, KeyError, OSError) as error:
            self.reply(400, {'error': str(error)})

    def do_POST(self):
        origin = self.headers.get('Origin')
        if origin and origin != f'http://{self.headers.get("Host")}':
            return self.reply(403, {'error': 'Origin rejected'})
        try:
            length = int(self.headers.get('Content-Length', 0))
            if not 0 < length <= 300 * 1024 * 1024:
                raise ValueError('Request too large or empty')
            request = json.loads(self.rfile.read(length))
            root = self.server.asset_root
            if self.path == '/api/roster':
                save_roster(root, request)
                result = {'saved': True}
            elif self.path == '/api/import':
                result = import_character(root, request)
            elif self.path == '/api/upload-directory':
                import tempfile
                from pathlib import Path as P
                import base64
                with tempfile.TemporaryDirectory(prefix='sagas-upload-') as temporary:
                    folder = P(temporary)
                    for item in request['files']:
                        relative = P(item['name'])
                        if relative.is_absolute() or '..' in relative.parts:
                            raise ValueError('Invalid upload path')
                        if any(part.lower() in {'stellarexport', 'converted', '.git'} for part in relative.parts):
                            continue
                        target = folder / relative
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(base64.b64decode(item['data'], validate=True))
                    result = import_directory(root, folder)
            elif self.path == '/api/import-directory':
                result = import_directory(root, request['path'])
            elif self.path == '/api/preview':
                result = preview(root, request)
            elif self.path == '/api/scenes':
                result = save_scenes(root, request)
            elif self.path == '/api/hd/import':
                result = import_hd(root, request)
            elif self.path == '/api/project':
                path = project_file(root, request['id'])
                raw = request['project']
                if not isinstance(raw, dict) or not raw.get('model_path'):
                    raise ValueError('Invalid project')
                atomic_json(path, raw)
                export_audio(path, path.parent.parent / 'converted')
                result = {'saved': True}
                if request.get('convert'):
                    result = import_project(root, path)
            else:
                return self.reply(404, {'error': 'Not found'})
            self.reply(200, result)
        except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
            self.reply(400, {'error': str(error)})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, default=ROOT / 'build/assets')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.asset_root = args.assets.resolve()
    print(f'Sagas Studio: http://127.0.0.1:{args.port}', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
