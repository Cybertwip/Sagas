"""Character roster packages for Stellar Studio."""
from pathlib import Path
import base64
import json
import re
import shutil
import subprocess
import tempfile
from stellar_shared import atomic_json
from converter import convert, export_native, export_audio

ROOT = Path(__file__).resolve().parents[2]
KINDS = ['Luigi', 'Mario', 'Donkey', 'Link', 'Samus', 'Captain', 'Ness', 'Yoshi', 'Kirby', 'Fox', 'Pikachu', 'Purin']
ALIASES = {'Donkey Kong': 'Donkey', 'Captain Falcon': 'Captain', 'Jigglypuff': 'Purin'}


def initial():
    return {'version': 2, 'columns': 6, 'characters': [{'id': n.lower(), 'name': n, 'base': n, 'builtin': True, 'enabled': True, 'cell': i} for i, n in enumerate(KINDS)]}


def logical(value):
    if value and (not value.startswith('mods/') or '..' in Path(value).parts or any(x in value for x in '\t\r\n')):
        raise ValueError('Invalid package asset path')
    return value


def save_roster(root, state):
    columns = int(state.get('columns', 6))
    if not 3 <= columns <= 12:
        raise ValueError('Columns must be between 3 and 12')
    characters = state.get('characters', [])
    if not 1 <= len(characters) <= 120:
        raise ValueError('Roster must contain 1–120 entries')
    seen = set()
    occupied = set()
    lines = [str(columns)]
    for i, c in enumerate(characters):
        if not re.fullmatch(r'[a-z0-9_-]+', c['id']) or c['id'] in seen:
            raise ValueError('Invalid or duplicate character ID')
        seen.add(c['id'])
        if c['base'] not in KINDS:
            raise ValueError('Unknown base fighter')
        if not c['name'].strip() or any(x in c['name'] for x in '\t\r\n'):
            raise ValueError('Invalid character name')
        portrait = logical(c.get('portrait', ''))
        model = logical(c.get('model', ''))
        cell = int(c.get('cell', i))
        if not 0 <= cell < 120:
            raise ValueError('Grid cells must be between 0 and 119')
        if c.get('enabled', True):
            if cell in occupied:
                raise ValueError('Two fighters occupy the same grid cell')
            occupied.add(cell)
            lines.append(f"{KINDS.index(c['base'])}\t{c['name']}\t{portrait}\t{model}\t{cell}")
    if len(lines) == 1:
        raise ValueError('Enable at least one roster entry')
    folder = root / 'mods'
    folder.mkdir(parents=True, exist_ok=True)
    atomic_json(folder / 'roster.json', state)
    temporary = folder / 'roster.tsv.tmp'
    temporary.write_text('\n'.join(lines) + '\n')
    temporary.replace(folder / 'roster.tsv')


def package_id(name):
    slug = re.sub('[^a-z0-9_-]+', '-', name.lower()).strip('-')
    if not slug:
        raise ValueError('Enter a character name')
    return slug


def import_project(root, source):
    root = Path(root).resolve()
    source = Path(source).expanduser().resolve()
    if source.is_dir():
        source = source / 'stellar_project.json'
    raw = json.loads(source.read_text())
    name = raw.get('name', source.parent.name)
    slug = package_id(name)
    base = ALIASES.get(raw.get('base_character', 'Mario'), raw.get('base_character', 'Mario'))
    if base not in KINDS:
        raise ValueError('Unsupported base fighter')
    folder = root / 'mods/characters' / slug
    folder.mkdir(parents=True, exist_ok=True)
    project_dir = folder / 'source'
    project_dir.mkdir(exist_ok=True)

    def copy_asset(value):
        if not value:
            return ''
        path = Path(value)
        path = path if path.is_absolute() else source.parent / path
        if not path.is_file():
            raise ValueError(f'Missing source asset: {path}')
        target = project_dir / path.name
        if path.resolve() != target.resolve():
            shutil.copy2(path, target)
        return str(target.resolve())

    raw['model_path'] = copy_asset(raw['model_path'])
    raw['portrait_path'] = copy_asset(raw.get('portrait_path', ''))
    for sound in raw.get('sounds', []):
        if sound.get('path') and not sound.get('inherited', False):
            sound['path'] = copy_asset(sound['path'])
    for stage in raw.get('stages', []):
        for key in ('model_path', 'collision_path', 'preview_path'):
            if stage.get(key):
                stage[key] = copy_asset(stage[key])
    project = project_dir / 'stellar_project.json'
    atomic_json(project, raw)
    proxy = convert(project, folder / 'converted')
    model = export_native(proxy)
    export_audio(project, folder / 'converted')
    character = {'id': slug, 'name': name, 'base': base, 'enabled': True, 'builtin': False, 'model_status': 'ready', 'model': str(model.relative_to(root)), 'project': str(project.relative_to(root)), 'source_project': str(source)}
    if raw.get('portrait_path'):
        character['portrait'] = str(Path(raw['portrait_path']).relative_to(root))
    atomic_json(folder / 'character.json', character)
    return character


def import_character(root, request):
    if request.get('path'):
        return import_project(root, request['path'])
    name = request.get('name', '').strip()
    base = request.get('base', 'Mario')
    slug = package_id(name)
    if base not in KINDS:
        raise ValueError('Unknown base fighter')
    files = request.get('files', [])
    if not files:
        raise ValueError('Choose a source project directory or files')
    folder = root / 'mods/characters' / slug / 'upload'
    folder.mkdir(parents=True, exist_ok=True)
    for item in files:
        relative = Path(item['name'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Invalid upload path')
        if relative.suffix.lower() not in {'.json', '.fbx', '.glb', '.gltf', '.bin', '.png', '.wav', '.jpg', '.jpeg'}:
            continue
        target = folder / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(base64.b64decode(item['data'], validate=True))
    projects = list(folder.rglob('stellar_project.json'))
    if projects:
        return import_project(root, projects[0])
    raise ValueError('Include stellar_project.json and its source model; loose assets cannot define a character')


def import_directory(root, path):
    directory = Path(path).expanduser().resolve()
    projects = [directory] if directory.is_file() else sorted(p for p in directory.rglob('stellar_project.json') if not any(x.lower() in {'stellarexport', 'converted', '.git'} for x in p.relative_to(directory).parts))
    if not projects:
        raise ValueError('No stellar_project.json files found')
    result = {'characters': [], 'errors': []}
    for project in projects:
        try:
            result['characters'].append(import_project(root, project))
        except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
            result['errors'].append({'project': str(project), 'error': str(error)})
    return result


def project_file(root, identifier):
    if not re.fullmatch('[a-z0-9_-]+', identifier):
        raise ValueError('Invalid package ID')
    return root / 'mods/characters' / identifier / 'source/stellar_project.json'


def preview(root, request):
    state = request['roster']
    output = root / 'mods/preview.png'
    with tempfile.TemporaryDirectory(prefix='sagas-preview-') as temporary:
        fixture = Path(temporary)
        for item in root.iterdir():
            if item.name != 'mods':
                (fixture / item.name).symlink_to(item.resolve(), target_is_directory=item.is_dir())
        (fixture / 'mods').mkdir()
        if (root / 'mods/characters').exists():
            (fixture / 'mods/characters').symlink_to((root / 'mods/characters').resolve(), target_is_directory=True)
        save_roster(fixture, state)
        command = [str(ROOT / 'build/sagas'), '--assets', str(fixture), '--select', '--headless', '--frames', '40', '--capture-only', '--capture', str(fixture / 'preview.png'), '--preview-cell', str(request.get('cell', 1))]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode or not (fixture / 'preview.png').is_file():
            raise ValueError('Preview failed: ' + result.stderr[-1600:])
        shutil.copy2(fixture / 'preview.png', output)
    return {'image': '/assets/mods/preview.png'}
