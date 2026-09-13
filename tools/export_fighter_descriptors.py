"""Turn extractor aggregates into versioned runtime tables and typed readers.

Called by each source extractor. Numeric values live only in assets/fighters;
headers retain schemas and lazy, validated table accessors.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def split_template(value):
    depth = 0
    for i, char in enumerate(value):
        depth += (char == '<') - (char == '>')
        if char == ',' and depth == 0:
            return value[:i].strip(), int(value[i+1:].strip())
    raise ValueError(value)

def array_type(value):
    return split_template(value[len('std::array<'):-1])

def export_header(path):
    path = Path(path)
    source = path.read_text()
    if 'DescriptorTable<' in source:
        return
    structures = {}
    def structure(match):
        name, body = match.groups()
        fields = []
        clean = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
        for declaration in clean.split(';'):
            declaration = declaration.strip()
            if not declaration:
                continue
            if declaration.startswith('std::array<'):
                end = declaration.rfind('>')+1
                kind, members = declaration[:end], declaration[end:].strip()
            else:
                kind, members = declaration.split(None, 1)
            for member in members.split(','):
                fields.append((kind, member.strip()))
        structures[name] = fields
        return match[0]+'\ninline void descriptor_read(std::istream& input,'+name+'& value) {\n'+''.join('    descriptor_read(input,value.'+member+');\n' for _, member in fields)+'}\n'
    source = re.sub(r'struct\s+(\w+)\s*\{([^{}]*)\};', structure, source)
    def columns(kind, prefix=''):
        if kind.startswith('std::array<'):
            inner, count = array_type(kind)
            return [field for index in range(count) for field in columns(inner,prefix+f'[{index}]')]
        if kind in structures:
            return [field for inner,member in structures[kind] for field in columns(inner,(prefix+'.' if prefix else '')+member)]
        if kind not in ('float','unsigned','int'):
            raise ValueError('Unsupported descriptor type: '+kind)
        return [prefix or 'value']
    directory = ROOT/'assets/fighters'
    directory.mkdir(parents=True, exist_ok=True)
    def write(name, labels, rows):
        (directory/(name+'.tsv')).write_text('SAGAS-DATA\t1\n'+'\t'.join(labels)+'\n'+'\n'.join('\t'.join(row) for row in rows)+'\n')
    def aggregate(match):
        block = match[1]
        if '{' not in block:
            kind, rest = block.split(None,1)
            result = []
            for assignment in rest.split(','):
                name, literal = (part.strip() for part in assignment.split('='))
                number = str(int(literal.rstrip('Uu'),0))
                write(name,['value'],[[number]])
                result.append('inline const DescriptorValue<'+kind+'> '+name+'{"'+name+'.tsv"};')
            return '\n'.join(result)
        begin = block.index('{')
        declaration, values = block[:begin], block[begin:]
        m = re.fullmatch(r'(.+?)\s+(\w+)((?:\[\d+\])*)\s*=?\s*', declaration.strip())
        if not m:
            raise ValueError(declaration)
        kind, name, dimensions = m.groups()
        if dimensions:
            dims = [int(n) for n in re.findall(r'\d+',dimensions)]
            count = dims[0]
            for dim in reversed(dims[1:]):
                kind = f'std::array<{kind},{dim}>'
        else:
            kind, count = array_type(kind)
        labels = columns(kind)
        values = re.sub(r'//[^\n]*|/\*.*?\*/', '', values, flags=re.S).replace('~0U','4294967295')
        numbers = re.findall(r'[-+]?(?:0x[\da-fA-F]+|\d+(?:\.\d*)?(?:[eE][-+]?\d+)?)[fFuU]?',values)
        numbers = [str(int(n.rstrip('uU'),0)) if '0x' in n else n.rstrip('fFuU') for n in numbers]
        if len(numbers) != count*len(labels):
            raise ValueError(f'{name}: expected {count} x {len(labels)} fields, got {len(numbers)}')
        write(name,labels,[numbers[i:i+len(labels)] for i in range(0,len(numbers),len(labels))])
        schema = '\\t'.join(labels)
        return f'inline const DescriptorTable<{kind}> {name}{{"{name}.tsv","{schema}",{count}}};'
    source = re.sub(r'inline constexpr\s+([^;]+);',aggregate,source)
    source = source.replace('#include <array>','#include <array>\n#include <sagas/FighterDescriptors.hpp>')
    path.write_text(source)

if __name__ == '__main__':
    import sys
    for arg in sys.argv[1:]:
        export_header(arg)
