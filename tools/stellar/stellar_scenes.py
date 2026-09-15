"""Scene descriptor tables for the fighting editor suite."""
from pathlib import Path
from stellar_tsv import read_tsv, write_tsv

SAGAS_ROOT = Path(__file__).resolve().parents[2]
SOURCE_SCENES = SAGAS_ROOT / 'assets' / 'scenes'
TABLES = (
    'ui_sprites',
    'scene_layout',
    'css_slots',
    'css_kinds',
    'menu_items',
    'menu_screens',
    'menu_icons',
    'menu_gallery',
    'hd_models',
)


def scene_roots(asset_root):
    roots = [SOURCE_SCENES]
    runtime = Path(asset_root) / 'scenes'
    if runtime.resolve() != SOURCE_SCENES.resolve():
        roots.append(runtime)
    return roots


def load_scenes(asset_root):
    tables = {}
    for name in TABLES:
        path = Path(asset_root) / 'scenes' / f'{name}.tsv'
        if not path.is_file():
            path = SOURCE_SCENES / f'{name}.tsv'
        tables[name] = read_tsv(path)
    return {'tables': tables}


def save_scenes(asset_root, payload):
    tables = payload.get('tables', payload)
    if not isinstance(tables, dict):
        raise ValueError('Expected a tables object')
    written = []
    for name, table in tables.items():
        if name not in TABLES:
            raise ValueError(f'Unknown scene table: {name}')
        header = table['header']
        rows = table['rows']
        if not header:
            raise ValueError(f'{name} is missing a header')
        for root in scene_roots(asset_root):
            write_tsv(root / f'{name}.tsv', header, rows)
        written.append(name)
    return {'saved': written}


def update_table_row(asset_root, name, key_column, key, values):
    if name not in TABLES:
        raise ValueError(f'Unknown scene table: {name}')
    current = load_scenes(asset_root)['tables'][name]
    header = current['header']
    rows = current['rows']
    found = False
    for row in rows:
        if str(row.get(key_column, '')) == str(key):
            row.update(values)
            found = True
            break
    if not found:
        row = {column: values.get(column, '-') for column in header}
        row[key_column] = key
        row.update(values)
        rows.append(row)
    save_scenes(asset_root, {name: {'header': header, 'rows': rows}})
    return {'updated': name, 'key': key}
