"""Extract projectile contact attributes from the local US WPAttributes."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
names=['Luigi','Mario','Fox','Samus','Link','Ness','Pikachu']
rows=[]
for name in names:
    path=next((root/'ssb-decomp-re/src/relocData').glob('*_'+name+'Special1.c'))
    active=[True];lines=[]
    for line in path.read_text().splitlines():
        if line.startswith('#if'):active.append(active[-1] and 'REGION_US' in line)
        elif line.startswith('#else'):active[-1]=active[-2] and not active[-1]
        elif line.startswith('#endif'):active.pop()
        elif all(active):lines.append(line)
    src=re.search(r'WPAttributes\s+\w+\s*=\s*\{(.*?)\n\};','\n'.join(lines),re.S).group(1)
    vals={k:int(v) for v,k in re.findall(r'^\s*(-?\d+),\s*/\*\s*(\w+)',src,re.M)}
    rows.append([vals[k] for k in ['size','damage','angle','knockback_scale','knockback_weight','knockback_base','element','sfx']])
# Thunder trail attributes remain a packed word array in PikachuMain.
source=(root/'ssb-decomp-re/src/relocData/243_PikachuMain.c').read_text()
block=source.split('(u32)&dPikachuModel_ThunderTrailDObjDesc')[-1].split('};')[0]
words=[int(x,16) for x in re.findall(r'^\s*(0x[0-9A-Fa-f]+)',block,re.M)]
size_angle,combat,flags,base=words[-4:]
rows.append([size_angle>>16,(combat>>14)&255,(size_angle>>6)&1023,combat>>22,combat&1023,base>>22,(combat>>10)&15,(flags>>11)&1023])
names.append('Pikachu Thunder trail')
rows.append([200,7,361,20,0,10,2,23]);names.append('Pikachu grounded Thunder Jolt (US)')
out=root/'sagas/include/sagas/WeaponSourceData.hpp'
out.write_text('// Generated from US WPAttributes by extract_weapon_data.py.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct WeaponSourceData { int size,damage,angle,growth,weight,base,element,sfx; };\ninline constexpr std::array<WeaponSourceData,9> weapon_source_data{{\n'+''.join('    {'+','.join(map(str,row))+'}, // '+name+'\n' for name,row in zip(names,rows))+'}};\n}\n')
