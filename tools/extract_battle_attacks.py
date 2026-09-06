"""Extract jab-chain hitbox windows from the US cartridge motion sources."""
import argparse
import csv
import json
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
    voices={r['name']:r['idx'] for r in json.loads((decomp/'build/us/src/audio/fgm.ucd.json').read_text())['entries']}
    hit_table=(decomp/'src/ft/ftmain.c').read_text().split('dFTMainHitCollisionFGMs')[1].split('};')[0]
    hit_sounds=[voices[name] for name in re.findall(r'nSYAudio\w+',hit_table)]
    def commands(name,depth=0):
        if depth>16:raise ValueError('recursive motion script')
        for command,arg in scripts[name]:
            if command=='ftMotionCommandSubroutine':yield from commands(arg,depth+1)
            elif command in ('ftMotionCommandReturn','ftMotionCommandEnd','ftMotionCommandPauseScript'):break
            else:yield command,arg
    hits=[]; followups=[]
    for clip in sorted({v for k,v in ids.items() if re.fullmatch(r'FT(?:Mario|Fox|Donkey|Samus|Luigi|Link|Yoshi|Captain|Kirby|Pikachu|Purin|Ness)Anim(?:Jab[123]|JabLoop(?:Start|End)?|FSmash|USmash|DSmash)',k)}):
        active={};frame=0;followup=-1;epoch=0
        def close(i):
            if i in active:
                begin,args=active.pop(i)
                if frame>begin:hits.append((clip,begin,frame,*args))
        for command,arg in commands(mapping[clip]):
            if command=='ftMotionCommandWait':frame+=int(arg,0)
            elif command=='ftMotionCommandWaitAsync':frame=max(frame,int(arg,0))
            elif command=='ftMotionCommandMakeAttackColl':
                a=[int(v.strip(),0) for v in arg.split(',')]
                close(a[0]);active[a[0]]=(frame,[a[0],a[2],a[3],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[17],hit_sounds[a[16]*3+a[15]],epoch])
            elif command=='ftMotionCommandSetFlag1' and int(arg,0):followup=frame
            elif command in ('ftMotionCommandSetAttackCollDamage','ftMotionCommandSetAttackCollSize'):
                i,value=[int(v.strip(),0) for v in arg.split(',')]
                if i in active:
                    args=active[i][1].copy();close(i)
                    args[2 if command.endswith('Damage') else 3]=value
                    active[i]=(frame,args)
            elif command=='ftMotionCommandClearAttackCollAll':
                epoch+=1
                for i in list(active):close(i)
            elif command=='ftMotionCommandClearAttackColl':close(int(arg,0))
            elif command=='ftMotionCommandEnd':break
        followups.append((clip,followup))
        frame=max(frame,30)
        for i in list(active):close(i)
    return hits,followups

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--decomp',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    hits,followups=extract(a.decomp,a.manifest)
    text='// Generated from US jab scripts by tools/extract_battle_attacks.py.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct SourceHitbox { unsigned motion,begin,end,id,joint; int damage,radius,x,y,z,angle,growth,weight,base; unsigned fgm,epoch; };\n'
    text+=f'inline constexpr std::array<SourceHitbox,{len(hits)}> source_jab_hitboxes{{{{\n'
    text+=''.join('    {'+','.join(map(str,h))+'},\n' for h in hits)+'}};\n'
    text+='struct SourceJabFollowup { unsigned motion; int frame; };\n'
    text+=f'inline constexpr std::array<SourceJabFollowup,{len(followups)}> source_jab_followups{{{{\n'
    text+=''.join('    {'+str(c)+','+str(f)+'},\n' for c,f in followups)+'}};\n}\n'
    a.output.write_text(text)
