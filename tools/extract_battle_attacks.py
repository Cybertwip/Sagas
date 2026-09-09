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
    mapping={}; recovery_clips=set();special_clips=set()
    names=['Luigi','Mario','Donkey','Link','Samus','Captain','Ness','Yoshi','Kirby','Fox','Pikachu','Purin']
    source=(decomp/'src/ft/ftdata.c').read_text()
    for kind,name in enumerate(names):
        table=re.search(r'FTMotionDesc dFT'+name+r'MotionDescs\[\]\s*=\s*\{(.*?)\n\};',source,re.S).group(1)
        entries=re.findall(r'^\s*(?:\{\s*&ll(\w+)FileID|\{?\s*0x00000000)\s*,',table,re.M)
        common=(decomp/'src/ft/ftdef.h').read_text().split('typedef enum FTCommonMotion')[1].split('}')[0]
        common=re.sub(r'//[^\n]*|/\*.*?\*/','',common,flags=re.S)
        enum_values={};value=-1
        for entry in common.split(','):
         m=re.search(r'(nFTCommonMotion\w+)(?:\s*=\s*(-?\d+|nFTCommonMotion\w+))?',entry)
         if not m:continue
         value=(enum_values[m[2]] if m[2].startswith('nFT') else int(m[2])) if m[2] else value+1
         enum_values[m[1]]=value
        special_base=enum_values['nFTCommonMotionSpecialStart']
        special_clips.update(ids[n] for n in entries[special_base:] if n in ids)
        recovery_clips.update(ids[n] for n in entries[58:71] if n)
        recovery_clips.update(ids[entries[enum_values['nFTCommonMotion'+n]]] for n in ('GuardOn','GuardOff','EscapeF','EscapeB'))
        for clip,script,offset in re.findall(r'\{\s*&ll(\w+)FileID,\s*(\w+)(?:\s*\+\s*(0x[0-9A-Fa-f]+))?,',table):
            if clip in ids and script in scripts:
                if offset:
                    if not all(c=='ftMotionCommandGoto' for c,_ in scripts[script]):continue
                    key=script+'@'+offset;scripts[key]=scripts[script][int(offset,0)//8:];script=key
                mapping.setdefault((kind,ids[clip]),script)
    voices={r['name']:r['idx'] for r in json.loads((decomp/'build/us/src/audio/fgm.ucd.json').read_text())['entries']}
    hit_table=(decomp/'src/ft/ftmain.c').read_text().split('dFTMainHitCollisionFGMs')[1].split('};')[0]
    hit_sounds=[voices[name] for name in re.findall(r'nSYAudio\w+',hit_table)]
    def commands(name,depth=0):
        if depth>16:raise ValueError('recursive motion script: '+name)
        index=0;loops=[];budget=10000
        while index<len(scripts[name]) and budget:
            budget-=1;command,arg=scripts[name][index];index+=1
            if command=='ftMotionCommandSubroutine':yield from commands(arg,depth+1)
            elif command=='ftMotionCommandGoto':
                if arg==name:break
                yield from commands(arg,depth+1);break
            elif command=='ftMotionCommandLoopBegin':loops.append([index,int(arg,0)])
            elif command=='ftMotionCommandLoopEnd':
                loops[-1][1]-=1
                if loops[-1][1]:index=loops[-1][0]
                else:loops.pop()
            elif command in ('ftMotionCommandReturn','ftMotionCommandEnd','ftMotionCommandPauseScript'):break
            else:yield command,arg
    hits=[]; followups=[]; flags=[]; hit_status=[];special_flags=[]
    supported={v for k,v in ids.items() if re.fullmatch(r'FT(?:Mario|Fox|Donkey|Samus|Luigi|Link|Yoshi|Captain|Kirby|Pikachu|Purin|Ness)Anim(?:Jab[123]|JabLoop(?:Start|End)?|FSmash|USmash|DSmash|AttackAir[NFBUD]|DashAttack|Turn|[UD]Tilt|FTilt(?:High|MidHigh|MidLow|Low)?|Catch)',k)}
    supported.update(recovery_clips|special_clips)
    for kind,clip in sorted(key for key in mapping if key[1] in supported):
        active={};definitions={};frame=0;followup=-1;epoch=0;refresh_epochs={}
        def close(i):
            if i in active:
                begin,args=active.pop(i)
                definitions[i]=args.copy()
                if frame>begin:hits.append((clip,begin,frame,*args,kind))
        for command,arg in commands(mapping[kind,clip]):
            if command=='ftMotionCommandWait':frame+=int(arg,0)
            elif command=='ftMotionCommandWaitAsync':frame=max(frame,int(arg,0))
            elif command=='ftMotionCommandMakeAttackColl':
                a=[int(v.strip(),0) for v in arg.split(',')]
                previous=active.get(a[0])
                shared=next((args[-3] for _,args in active.values() if args[-2]==a[1]),None)
                if previous and previous[1][-2]==a[1]:record=previous[1][-3]
                elif shared is not None:record=shared
                else:epoch+=1;record=epoch
                close(a[0]);active[a[0]]=(frame,[a[0],a[2],a[3],a[6],a[7],a[8],a[9],(a[10]+512)%1024-512,a[11],a[12],a[17],hit_sounds[a[16]*3+a[15]],record,a[1],a[5]])
            elif command in ('ftMotionCommandSetFlag0','ftMotionCommandSetFlag2','ftMotionCommandSetFlag3'):
                special_flags.append((clip,frame,int(command[-1]),int(arg,0),kind))
            elif command=='ftMotionCommandSetHitStatusAll':
                hit_status.append((clip,frame,int(arg,0),kind))
            elif command=='ftMotionCommandSetFlag1':
                flags.append((clip,frame,int(arg,0),kind))
                if int(arg,0):followup=frame
            elif command in ('ftMotionCommandSetAttackCollDamage','ftMotionCommandSetAttackCollSize'):
                i,value=[int(v.strip(),0) for v in arg.split(',')]
                if i in active:
                    args=active[i][1].copy();close(i)
                    args[2 if command.endswith('Damage') else 3]=value
                    active[i]=(frame,args)
            elif command=='ftMotionCommandRefreshAttackCollID':
                i=int(arg,0)
                if i in active or i in definitions:
                    args=(active[i][1] if i in active else definitions[i]).copy();close(i)
                    key=(args[-2],frame)
                    if key not in refresh_epochs:epoch+=1;refresh_epochs[key]=epoch
                    args[-3]=refresh_epochs[key];active[i]=(frame,args)
            elif command=='ftMotionCommandClearAttackCollAll':
                epoch+=1
                for i in list(active):close(i)
            elif command=='ftMotionCommandClearAttackColl':close(int(arg,0))
            elif command=='ftMotionCommandEnd':break
        followups.append((clip,followup,kind))
        frame=max(frame,30)
        for i in list(active):close(i)
    return hits,followups,flags,hit_status,special_flags

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--decomp',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    hits,followups,flags,hit_status,special_flags=extract(a.decomp,a.manifest)
    text='// Generated from US jab scripts by tools/extract_battle_attacks.py.\n#pragma once\n#include <array>\nnamespace sagas {\nstruct SourceHitbox { unsigned motion,begin,end,id,joint; int damage,radius,x,y,z,angle,growth,weight,base; unsigned fgm,epoch,group,element,kind; };\n'
    text+=f'inline constexpr std::array<SourceHitbox,{len(hits)}> source_jab_hitboxes{{{{\n'
    text+=''.join('    {'+','.join(map(str,h))+'},\n' for h in hits)+'}};\n'
    text+='struct SourceJabFollowup { unsigned motion; int frame; unsigned kind; };\n'
    text+=f'inline constexpr std::array<SourceJabFollowup,{len(followups)}> source_jab_followups{{{{\n'
    text+=''.join('    {'+str(c)+','+str(f)+','+str(k)+'},\n' for c,f,k in followups)+'}};\n}\n'
    text=text[:-2]+'struct SourceMotionFlag { unsigned motion,frame; int value; unsigned kind; };\n'
    text+=f'inline constexpr std::array<SourceMotionFlag,{len(flags)}> source_motion_flags{{{{\n'
    text+=''.join('    {'+','.join(map(str,f))+'},\n' for f in flags)+'}};\n}\n'
    text=text[:-2]+'struct SourceHitStatus { unsigned motion,frame,status,kind; };\n'
    text+=f'inline constexpr std::array<SourceHitStatus,{len(hit_status)}> source_hit_status{{{{\n'
    text+=''.join('    {'+','.join(map(str,f))+'},\n' for f in hit_status)+'}};\n}\n'
    text=text[:-2]+'struct SourceSpecialFlag { unsigned motion,frame,flag,value,kind; };\n'
    text+=f'inline constexpr std::array<SourceSpecialFlag,{len(special_flags)}> source_special_flags{{{{\n'
    text+=''.join('    {'+','.join(map(str,f))+'},\n' for f in special_flags)+'}};\n}\n'
    a.output.write_text(text)
