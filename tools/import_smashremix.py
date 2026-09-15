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
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from remix_moveset import decode_moveset, parse_throw_desc, signed  # noqa: E402,F401

PARENTS = dict(MARIO=1, FOX=9, DONKEY=2, SAMUS=4, LUIGI=0, LINK=3,
               YOSHI=7, CAPTAIN=5, KIRBY=8, PIKACHU=10, JIGGLY=11, NESS=6)
PARENTS.update(DK=2, JIGGLYPUFF=11)

def clean(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)

def constants(text, prefix):
    return {prefix+name:int(value,0) for name,value in
            re.findall(r"\bconstant\s+(\w+)\(\s*(0x[\da-fA-F]+|\d+)\s*\)", clean(text))}

def number(expression, symbols):
    expression=expression.strip()
    if expression in symbols:return symbols[expression]
    return int(expression,0)

def table(directory,name,columns,rows):
    path=directory/(name+".tsv")
    with path.open("w",newline="") as f:
        f.write("SAGAS-DATA\t1\n")
        writer=csv.writer(f,delimiter="\t",lineterminator="\n")
        writer.writerow(columns);writer.writerows(rows)

def import_tree(root,output,resources=None):
    output.mkdir(parents=True,exist_ok=True)
    resource_manifest=None
    if resources:
        if not (resources/".complete").exists():raise ValueError("Resource extraction is incomplete")
        resource_manifest=json.loads((resources/"extraction.json").read_text())
        if resource_manifest.get("version")!="2.0.1":raise ValueError("Unsupported resource release")
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
            missing=[v for v in files if v and not (
                (resources/"reloc"/f"{v:04d}.bin").exists() if resources else
                (root/"build/original"/f"{v:04X}.bin").exists())]
            record["missing_resources"]=missing
            roster.append([ident,name,PARENTS[parent],number(args[11],symbols),
                           number(args[12],symbols),number(args[13],symbols),number(args[14],symbols),*files])
        except (ValueError,KeyError) as e:record["unresolved"]=str(e)
        fighters.append(record)
    ids={f["key"]:f["id"] for f in fighters}
    ids.update({k:v for k,v in zip(
        ["MARIO","FOX","DONKEY","SAMUS","LUIGI","LINK","YOSHI","CAPTAIN","KIRBY","PIKACHU","JIGGLY","NESS"],range(12))})
    actions=[];callbacks=[];scripts=[];hit_rows=[];event_rows=[];script_ids={}
    throw_rows=[];throw_files={}
    paths=sorted(src.rglob("*.asm"))

    def resolve_file(base, relative_name):
        target=base/relative_name
        if target.exists():
            return target
        return next((p for p in base.rglob("*") if p.as_posix().lower()==target.as_posix().lower()), target)

    for path in paths:
        text=clean(path.read_text(errors="replace"));relative=path.relative_to(root).as_posix()
        inserts={}
        for m in re.finditer(r'\binsert\s+(\w+)\s*,\s*"([^"]+)"',text):
            inserts[m[1]]=resolve_file(path.parent, m[2])
        # Label scripts: GRAB:; Moveset.THROW_DATA(X); insert "moveset/GRAB.bin"
        for m in re.finditer(r"^\s*(\w+)\s*:", text, re.M):
            name=m[1]
            if name in inserts:
                continue
            chunk=re.split(r"\n\s*(?:\w+\s*:|Character\.)", text[m.end():], 1)[0]
            unnamed=re.findall(r'\binsert\s+"([^"]+)"', chunk)
            named=re.findall(r'\binsert\s+(\w+)\s*,\s*"([^"]+)"', chunk)
            throw=re.search(r"Moveset\.THROW_DATA\((\w+)\)", chunk)
            if unnamed:
                inserts[name]=resolve_file(path.parent, unnamed[0])
            elif named:
                inserts[name]=resolve_file(path.parent, named[0][1])
            if throw:
                throw_src=inserts.get(throw[1])
                if throw_src and throw_src.exists():
                    throw_files[name]=throw_src
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
            throw_src=throw_files.get(args[3])
            if throw_src and throw_src.exists() and action in (0xa9, 0xaa):
                desc=parse_throw_desc(throw_src.read_bytes())
                if desc:
                    release=0
                    if script_id>=0:
                        for event in event_rows:
                            if event[0]==script_id and event[2]==23 and (event[3]&0x3ffffff)==1:
                                release=event[1];break
                    throw_rows.append([ids[args[0]], action, release or desc[0], *desc[1:6]])
    fgm_names=constants((src/"FGM.asm").read_text().split("scope names")[-1].split("scope css")[0],"")
    announce_rows=[]
    css_text=clean((src/"CharacterSelect.asm").read_text())
    for m in re.finditer(r"add_to_css\(\s*Character\.id\.(\w+)\s*,\s*FGM\.announcer\.names\.(\w+)", css_text):
        fighter, name=m[1], m[2]
        if fighter not in ids or name not in fgm_names:
            continue
        announce_rows.append([fighter, fgm_names[name]])
    table(output,"remix_roster",["id","key","parent","attribute_offset","extra_actions","jab3","copy",*[f"files[{i}]" for i in range(9)]],roster)
    table(output,"remix_actions",["fighter","action","animation","flags","script"],
          [[ids[r["fighter"]],r["action"],r["animation"],r["flags"],r["script_id"]] for r in actions])
    table(output,"remix_hitboxes",["script","begin","end","id","group","joint","damage","size","x","y","z","angle","growth","weight","base","element","ground_air","shield_damage","sound_kind","sound_level","scaled"],hit_rows)
    table(output,"remix_events",["script","frame","opcode","word_count",*[f"words[{i}]" for i in range(5)]],event_rows)
    table(output,"remix_scripts",["id","decoded"],[[r["id"],int(r["status"]=="decoded")] for r in scripts])
    table(output,"remix_announce",["key","fgm"],announce_rows)
    table(output,"remix_throws",["fighter","action","frame","damage","angle","growth","weight","base"],throw_rows)
    report=dict(format=1,resource_archive=resource_manifest,fighters=fighters,actions=actions,callbacks=callbacks,scripts=scripts,
                unresolved=issues,summary=dict(fighters=len(fighters),actions=len(actions),
                    callback_declarations=len(callbacks),decoded_scripts=sum(r["status"]=="decoded" for r in scripts),
                    scripts=len(scripts),hitbox_windows=len(hit_rows),events=len(event_rows),
                    announcer_names=len(announce_rows),throws=len(throw_rows),
                    unresolved_declarations=len(issues),missing_resource_references=sum(len(f.get("missing_resources",[])) for f in fighters)),
                limitations=["Declarations are source-level, not an assembled patch: conditional edits and ordering need validation.",
                             "Decoded script does not imply its fighter callbacks or assets are ported.",
                             "No imported fighters are enabled for character select by this initial port."])
    (output/"remix_import_report.json").write_text(json.dumps(report,indent=2)+"\n")
    return report

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--source",type=Path,default=Path(__file__).resolve().parents[2]/"smashremix")
    parser.add_argument("--output",type=Path,default=Path(__file__).resolve().parents[1]/"assets/fighters")
    parser.add_argument("--resources",type=Path,help="Editable extracted Remix asset root (defaults to assets/remix)")
    args=parser.parse_args()
    if args.resources is None:
        extracted=Path(__file__).resolve().parents[1]/"assets/remix"
        if (extracted/".complete").exists():args.resources=extracted
    print(json.dumps(import_tree(args.source,args.output,args.resources)["summary"],indent=2))
