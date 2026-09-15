"""Import HD fighter models (FBX) into scene hd_models descriptors."""
from pathlib import Path
import shutil
from stellar_roster import KINDS, ALIASES, package_id
from stellar_scenes import update_table_row
from stellar_shared import atomic_json

WORKSPACE = Path(__file__).resolve().parents[3]


def resolve_fbx(path):
    candidate = Path(path).expanduser()
    if candidate.is_file():
        return candidate.resolve()
    relative = WORKSPACE / path
    if relative.is_file():
        return relative.resolve()
    named = WORKSPACE / 'HD' / Path(path).name
    if named.is_file():
        return named.resolve()
    raise ValueError(f'Missing HD model: {path}')


def import_hd(asset_root, request, convert=None, export_native=None):
    fbx = resolve_fbx(request.get('path', 'HD/Mia.fbx'))
    key = (request.get('key') or fbx.stem).strip().upper()
    if not key:
        raise ValueError('HD model key is required')
    parent = request.get('parent', 'Luigi')
    parent = ALIASES.get(parent, parent)
    if parent not in KINDS:
        raise ValueError('Unknown parent fighter')
    slug = package_id(key)
    dest = Path(asset_root) / 'mods' / 'hd' / slug
    source_dir = dest / 'source'
    source_dir.mkdir(parents=True, exist_ok=True)
    copied = source_dir / fbx.name
    if fbx.resolve() != copied.resolve():
        shutil.copy2(fbx, copied)
    portrait = '-'
    for image in list(fbx.parent.glob(fbx.stem + '.*')) + list(source_dir.glob('*.png')):
        if image.suffix.lower() in {'.png', '.jpg', '.jpeg'}:
            target = source_dir / image.name
            if image.resolve() != target.resolve():
                shutil.copy2(image, target)
            portrait = str(target.relative_to(asset_root))
            break
    project = {
        'name': key.title() if key.isupper() else key,
        'base_character': parent,
        'model_path': str(copied.resolve()),
    }
    if portrait != '-':
        project['portrait_path'] = str((Path(asset_root) / portrait).resolve())
    project_path = source_dir / 'stellar_project.json'
    atomic_json(project_path, project)
    model = '-'
    converter = convert
    exporter = export_native
    if converter is None or exporter is None:
        from converter import convert as default_convert, export_native as default_export
        converter = converter or default_convert
        exporter = exporter or default_export
    proxy = converter(project_path, dest / 'converted')
    mesh = exporter(proxy)
    model = str(Path(mesh).resolve().relative_to(Path(asset_root).resolve()))
    fbx_label = f'HD/{fbx.name}'
    update_table_row(asset_root, 'hd_models', 'key', key, {
        'key': key,
        'parent': parent,
        'fbx': fbx_label,
        'model': model,
        'portrait': portrait,
    })
    character = {
        'id': slug,
        'name': project['name'],
        'base': parent,
        'enabled': True,
        'builtin': False,
        'model_status': 'ready',
        'model': model,
        'portrait': '' if portrait == '-' else portrait,
        'hd': True,
        'fbx': fbx_label,
    }
    atomic_json(dest / 'character.json', character)
    return character
