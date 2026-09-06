"""Extract initial jab hitbox windows from the US cartridge motion sources."""
import argparse
import csv
import re
from pathlib import Path


def extract(decomp, manifest):
    ids={r['name']:int(r['id']) for r in csv.DictReader(manifest.open(),delimiter='\t')}
    scripts={}
    for path in (decomp/'src/relocData').glob('*MainMotion.c'):
        lines=[]; active=[True]
        for line in path.read_text().splitlines():
            if line.startswith('#if'): active.append(active[-1] and 'REGION_US' in line)
            elif line.startswith('#else') and len(active)>1: active[-1]=active[-2] and not active[-1]
            elif line.startswith('#endif') and len(active)>1: active.pop()
            elif all(active):lines.append(line)
        source=re.sub(r'/\*.*?\*/|//[^\n]*','','\n'.join(lines),flags=re.S)
        for name,body in re.findall(r'(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};',source,re.S):
            scripts[name]=re.findall(r'(ftMotion\w+)\(([^()]*)\)',body)
    mapping={}
    for clip,script in re.findall(r'\{\s*&ll(\w+)FileID,\s*(\w+),',(decomp/'src/ft/ftdata.c').read_text()):
        if clip in ids and script in scripts:mapping.setdefault(ids[clip],script)
    hits=[]
    for clip in sorted({v for k,v in ids.items() if re.fullmatch(r'FT(?:Mario|Fox|Donkey|Samus|Luigi|Link|Yoshi|Captain|Kirby|Pikachu|Purin|Ness)AnimJab1',k)}):
        active={};frame=0
        def close(i):
            if i in active:
                begin,args=active.pop(i)
                if frame>begin:hits.append((clip,begin,frame,*args))
        for command,arg in scripts[mapping[clip]]:
            if command=='ftMotionCommandWait':frame+=int(arg,0)
            elif command=='ftMotionCommandWaitAsync':frame=max(frame,int(arg,0))
            elif command=='ftMotionCommandMakeAttackColl':
                a=[int(v.strip(),0) for v in arg.split(',')]
                close(a[0]);active[a[0]]=(frame,[a[0],a[2],a[3],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[17]])
            elif command=='ftMotionCommandClearAttackCollAll':
                for i in list(active):close(i)
            elif command=='ftMotionCommandClearAttackColl':close(int(arg,0))
            elif command=='ftMotionCommandEnd':break
        frame=max(frame,30)
        for i in list(active):close(i)
    return hits

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--decomp',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    hits=extract(a.decomp,a.manifest)
    text='// Generated from US jab scripts by tools/extract_battle_attacks.py.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct SourceHitbox { unsigned motion,begin,end,id,joint; int damage,radius,x,y,z,angle,growth,weight,base; };\n'
    text+=f'inline constexpr std::array<SourceHitbox,{len(hits)}> source_jab_hitboxes{{{{\n'
    text+=''.join('    {'+','.join(map(str,h))+'},\n' for h in hits)+'}};\n}\n'
    a.output.write_text(text)
