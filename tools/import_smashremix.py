"""Translate Smash Remix declarations and moveset binaries into Sagas data.

This is deliberately not a MIPS assembler. Unknown expressions, callbacks and
control flow stay explicit in the coverage report; they never become playable
parent-character fallbacks.
"""
import argparse
import csv
import hashlib
import json
import re
import struct
from pathlib import Path

PARENTS = dict(MARIO=1, FOX=9, DONKEY=2, SAMUS=4, LUIGI=0, LINK=3,
               YOSHI=7, CAPTAIN=5, KIRBY=8, PIKACHU=10, JIGGLY=11, NESS=6)
PARENTS.update(DK=2, JIGGLYPUFF=11)
MULTIWORD = {3:5,4:5,7:2,12:2,13:2,31:4,34:2,36:2,38:4,39:4,46:2}

def clean(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)

def constants(text, prefix):
    return {prefix+name:int(value,0) for name,value in
            re.findall(r"\bconstant\s+(\w+)\(\s*(0x[\da-fA-F]+|\d+)\s*\)", clean(text))}

def number(expression, symbols):
    expression=expression.strip()
    if expression in symbols:return symbols[expression]
    return int(expression,0)

def signed(value,bits):
    return value-(1<<bits) if value&(1<<(bits-1)) else value

def decode_moveset(data):
    """Decode a self-contained binary into timed commands and hitbox windows.

    Reject pointer/control-flow dependencies as a whole, retaining the reason.
    Never publish a partially decoded attack as a complete translation.
    """
    if len(data)%4:raise ValueError("unaligned moveset")
    words=struct.unpack(">"+str(len(data)//4)+"I",data)
    frame=0;pc=0;active={};hits=[];events=[];loops=[];budget=10000
    def close(aid):
        if aid in active:
            begin,values=active.pop(aid)
            if frame>begin:hits.append([begin,frame,*values])
    while pc<len(words):
        budget-=1
        if budget<=0:raise ValueError("moveset instruction budget exceeded")
        word=words[pc];op=word>>26;count=MULTIWORD.get(op,1)
        if op>51:raise ValueError(f"Remix extended opcode {op} at {pc*4}")
        if pc+count>len(words):raise ValueError(f"truncated opcode {op} at {pc*4}")
        args=words[pc:pc+count];pc+=count
        events.append([frame,op,count,*args,*([0]*(5-count))])
        if op==0:
            for aid in list(active):close(aid)
            return hits,events
        if op in (12,13,34,35,36,37,46):
            raise ValueError(f"requires linked script/pointer opcode {op} at {(pc-count)*4}")
        if op==1:frame+=word&0x3ffffff
        elif op==2:frame=max(frame,word&0x3ffffff)
        elif op in (3,4):
            aid=(word>>23)&7;close(aid)
            # aid, group, joint, damage, radius, offset, knockback, element,
            # collision masks, shield damage, sound bank/level, scaled-position.
            active[aid]=(frame,[aid,(word>>20)&7,signed((word>>13)&127,7),
                (word>>5)&255,args[1]>>16,signed(args[1]&65535,16),
                signed(args[2]>>16,16),signed(args[2]&65535,16),
                (args[3]>>22)&1023,(args[3]>>12)&1023,(args[3]>>2)&1023,
                (args[4]>>7)&1023,word&15,args[3]&3,signed(args[4]>>24,8),
                (args[4]>>17)&15,(args[4]>>21)&7,int(op==4)])
        elif op==5:close(word&0x3ffffff)
        elif op==6:
            for aid in list(active):close(aid)
        elif op in (7,8,9,10,11):
            aid=(word&0x3ffffff) if op==11 else (word>>23)&7
            if aid in active:
                values=active[aid][1].copy();close(aid)
                if op==7:values[5:8]=[signed((word>>7)&65535,16),signed(args[1]>>16,16),signed(args[1]&65535,16)]
                elif op==8:values[3]=(word>>15)&255
                elif op==9:values[4]=(word>>7)&65535
                elif op==10:values[16]=(word>>20)&7
                active[aid]=(frame,values)
        elif op==32:
            iterations=word&0x3ffffff
            if not 0<iterations<=1000:raise ValueError("invalid loop count")
            loops.append([pc,iterations])
        elif op==33:
            if not loops:raise ValueError("unmatched loop end")
            loops[-1][1]-=1
            if loops[-1][1]:pc=loops[-1][0]
            else:loops.pop()
    raise ValueError("requires ASM continuation: binary has no END")

def table(directory,name,columns,rows):
    path=directory/(name+".tsv")
    with path.open("w",newline="") as f:
        f.write("SAGAS-DATA\t1\n")
        writer=csv.writer(f,delimiter="\t",lineterminator="\n")
        writer.writerow(columns);writer.writerows(rows)

def import_tree(root,output):
    output.mkdir(parents=True,exist_ok=True)
    src=root/"src"
    symbols=constants((src/"File.asm").read_text(),"File.")
    # Only the unscoped common Action constants can be used without a fighter scope.
    symbols.update(constants((src/"Action.asm").read_text().split("scope MARIO")[0],"Action."))
    symbols.update({"OS.TRUE":1,"OS.FALSE":0})
    character_source=(src/"Character.asm").read_text()
    fighters=[];roster=[];issues=[];next_id=27
    for match in re.finditer(r"^\s*define_character\(([^\n]+)\)",clean(character_source),re.M):
        args=[v.strip() for v in match[1].split(",")]
        if len(args)==1:continue
        if len(args)!=21:raise ValueError("unexpected define_character schema: "+match[1])
        name,parent=args[:2]
        ident=next_id;next_id+=1
        if name=="RANDOM":next_id+=1  # Source reserves slot 0x1C after RANDOM.
        record=dict(id=ident,key=name,parent=parent,source="src/Character.asm",
                    files=args[2:11],attribute_offset=args[11],extra_actions=args[12],
                    stages=args[15:19],sound_type=args[19],variant=args[20],
                    playable=False)
        try:
            files=[number(v,symbols) for v in args[2:11]]
            record["resolved_files"]=files
            missing=[v for v in files if v and v>=0x854 and not (root/"build/original"/f"{v:04X}.bin").exists()]
            record["missing_resources"]=missing
            roster.append([ident,name,PARENTS[parent],number(args[11],symbols),
                           number(args[12],symbols),number(args[13],symbols),number(args[14],symbols),*files])
        except (ValueError,KeyError) as e:record["unresolved"]=str(e)
        fighters.append(record)
    ids={f["key"]:f["id"] for f in fighters}
    ids.update({k:v for k,v in zip(
        ["MARIO","FOX","DONKEY","SAMUS","LUIGI","LINK","YOSHI","CAPTAIN","KIRBY","PIKACHU","JIGGLY","NESS"],range(12))})
    actions=[];callbacks=[];scripts=[];hit_rows=[];event_rows=[];script_ids={}
    paths=sorted(src.rglob("*.asm"))
    for path in paths:
        text=clean(path.read_text(errors="replace"));relative=path.relative_to(root).as_posix()
        inserts={}
        for m in re.finditer(r'\binsert\s+(\w+)\s*,\s*"([^"]+)"',text):
            target=path.parent/m[2]
            if not target.exists():
                # Bass paths are case-insensitive on the source authoring platform.
                target=next((p for p in path.parent.rglob("*") if p.as_posix().lower()==target.as_posix().lower()),target)
            inserts[m[1]]=target
        for m in re.finditer(r"Character\.(edit_action_parameters|edit_action|add_new_action|add_new_action_params)\(([^\n]+?)\)",text):
            macro=m[1];args=[a.strip() for a in m[2].split(",")]
            if not args or args[0] not in ids:continue
            row=dict(fighter=args[0],macro=macro,args=args[1:],source=relative,
                     line=text.count("\n",0,m.start())+1)
            if macro!="edit_action_parameters":
                callbacks.append(row);continue
            if len(args)!=5:row["error"]="unsupported argument count";issues.append(row);continue
            try:action=number(args[1],symbols);animation=number(args[2],symbols);flags=number(args[4],symbols)
            except ValueError:row["error"]="unresolved action/animation/flag expression";issues.append(row);continue
            script_id=-1 if args[3]=="-1" else -2
            target=inserts.get(args[3])
            if target and target.exists():
                key=target.relative_to(root).as_posix()
                if key not in script_ids:
                    index=len(scripts);script_ids[key]=index
                    data=target.read_bytes()
                    script=dict(id=index,path=key,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
                    try:
                        hits,events=decode_moveset(data)
                        script.update(status="decoded",hitboxes=len(hits),events=len(events))
                        hit_rows.extend([index,*hit] for hit in hits)
                        event_rows.extend([index,*event] for event in events)
                    except ValueError as e:script.update(status="requires_port",reason=str(e))
                    scripts.append(script)
                script_id=script_ids[key]
            row.update(action=action,animation=animation,flags=flags,script_id=script_id)
            actions.append(row)
    table(output,"remix_roster",["id","key","parent","attribute_offset","extra_actions","jab3","copy",*[f"files[{i}]" for i in range(9)]],roster)
    table(output,"remix_actions",["fighter","action","animation","flags","script"],
          [[ids[r["fighter"]],r["action"],r["animation"],r["flags"],r["script_id"]] for r in actions])
    table(output,"remix_hitboxes",["script","begin","end","id","group","joint","damage","size","x","y","z","angle","growth","weight","base","element","ground_air","shield_damage","sound_kind","sound_level","scaled"],hit_rows)
    table(output,"remix_events",["script","frame","opcode","word_count",*[f"words[{i}]" for i in range(5)]],event_rows)
    table(output,"remix_scripts",["id","decoded"],[[r["id"],int(r["status"]=="decoded")] for r in scripts])
    report=dict(format=1,fighters=fighters,actions=actions,callbacks=callbacks,scripts=scripts,
                unresolved=issues,summary=dict(fighters=len(fighters),actions=len(actions),
                    callback_declarations=len(callbacks),decoded_scripts=sum(r["status"]=="decoded" for r in scripts),
                    scripts=len(scripts),hitbox_windows=len(hit_rows),events=len(event_rows),
                    unresolved_declarations=len(issues)),
                limitations=["Declarations are source-level, not an assembled patch: conditional edits and ordering need validation.",
                             "Decoded script does not imply its fighter callbacks or assets are ported.",
                             "No imported fighters are enabled for character select by this initial port."])
    (output/"remix_import_report.json").write_text(json.dumps(report,indent=2)+"\n")
    return report

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,default=Path(__file__).resolve().parents[2]/"smashremix")
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[1]/"assets/fighters")
    args=parser.parse_args()
    print(json.dumps(import_tree(args.source,args.output)["summary"],indent=2))
