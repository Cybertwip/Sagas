"""Read and write SAGAS-DATA TSV tables used by scene and fighter descriptors."""
from pathlib import Path


def read_tsv(path):
    path = Path(path)
    lines = path.read_text().splitlines()
    if not lines or lines[0] != 'SAGAS-DATA\t1':
        raise ValueError(f'Unsupported descriptor version: {path}')
    if len(lines) < 2:
        raise ValueError(f'Missing descriptor header: {path}')
    header = lines[1].split('\t')
    rows = []
    for line in lines[2:]:
        if not line or line.startswith('#'):
            continue
        fields = line.split('\t')
        if len(fields) != len(header):
            raise ValueError(f'{path}: expected {len(header)} columns, got {len(fields)}')
        rows.append(dict(zip(header, fields)))
    return {'header': header, 'rows': rows}


def write_tsv(path, header, rows):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ['SAGAS-DATA\t1', '\t'.join(header)]
    for row in rows:
        if isinstance(row, dict):
            lines.append('\t'.join('' if row.get(column) is None else str(row[column]) for column in header))
        else:
            lines.append('\t'.join(str(value) for value in row))
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text('\n'.join(lines) + '\n')
    temporary.replace(path)
