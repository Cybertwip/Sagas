"""Recover the verified Remix 2.0.1 resource archive as native Sagas assets.

Uses the local Remix project's VPK decoder, without loading its Tk editor.
ROMs stay in build/. Extracted resources are editable in assets/remix/.
"""
import argparse
import ast
import csv
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

RELEASE_MD5 = "2b2d6b295106c54216b7fc7a2f14346e"

def vpk_decoder(source):
    path=source/"scripts/SSB.py"
    tree=ast.parse(path.read_text())
    definition=next(node for node in tree.body if isinstance(node,ast.ClassDef) and node.name=="VPK")
    module=ast.Module(body=[definition],type_ignores=[])
    namespace={}
    exec(compile(module,str(path),"exec"),namespace)
    return namespace["VPK"].dec_file

def table_info(rom):
    if rom[:4]!=bytes.fromhex("80371240"):raise ValueError("Expected big-endian N64 ROM")
    table=struct.unpack_from(">I",rom,0x41f08)[0]
    high,low=(struct.unpack_from(">I",rom,p)[0] for p in (0x527e8,0x527f8))
    if high>>26!=15 or low>>26!=9:raise ValueError("Unsupported resource-count instructions")
    count=((high&65535)<<16)+struct.unpack(">h",struct.pack(">H",low&65535))[0]
    if not 0<count<65536 or table+(count+1)*12>len(rom):raise ValueError("Invalid resource table")
    rows=[struct.unpack_from(">I4H",rom,table+i*12) for i in range(count+1)]
    base=table+(count+1)*12
    offsets=[row[0]&0x7fffffff for row in rows]
    if offsets!=sorted(offsets) or base+offsets[-1]>len(rom):raise ValueError("Resource offsets exceed ROM")
    return table,base,rows

def links_for(data,internal,external,dependencies,file_id,count):
    links=[];visited=set();dependency=0
    for head,is_external in ((internal,False),(external,True)):
        current=head
        while current!=65535:
            offset=current*4
            if offset in visited or offset+4>len(data):raise ValueError(f"{file_id}: invalid relocation chain at {offset}")
            visited.add(offset)
            current,target=struct.unpack_from(">HH",data,offset)
            target_file=file_id
            if is_external:
                if dependency*2+2>len(dependencies):raise ValueError(f"{file_id}: missing resource dependency")
                target_file=struct.unpack_from(">H",dependencies,dependency*2)[0];dependency+=1
                if target_file>=count:raise ValueError(f"{file_id}: invalid target file {target_file}")
            links.append((offset,target_file,target*4))
    return links

# Parent MAIN files keyed by the moveset file stored at MAIN+0.
# Remix clones keep the parent's pointer offsets but sometimes retarget the
# dependency list at those slots to a smaller projectile/info file.
MOVESET_TO_MAIN={
    202:203,208:209,212:213,216:217,220:221,224:225,228:229,232:233,235:236,238:239,242:243,246:247
}

def load_link_map(path):
    rows=[]
    with path.open() as f:
        reader=csv.DictReader(f,delimiter="\t")
        for row in reader:
            rows.append((int(row["location"]),int(row["target_file"]),int(row["target_offset"])))
    return rows

def write_link_map(path,rows):
    with path.open("w") as f:
        writer=csv.writer(f,delimiter="\t",lineterminator="\n")
        writer.writerow(["location","target_file","target_offset"]);writer.writerows(rows)

def repair_out_of_range_links(directory,sizes,all_links):
    """Fix extracted relocs whose word offsets land past the target file.

    Cloned MAIN files keep parent graphic offsets while the ROM dependency list
    names a smaller info file. Stage headers sometimes name the info table
    instead of the graphic file it points at. Remaining overflow nodes are
    display-list words that continued a reloc chain; those links are dropped.
    """
    by_owner={}
    for owner,location,target,offset in all_links:
        by_owner.setdefault(owner,[]).append((location,target,offset))
    parent_main={}
    for owner,rows in by_owner.items():
        at={location: (target,offset) for location,target,offset in rows}
        if 0 in at and at[0][0] in MOVESET_TO_MAIN:
            parent_main[owner]=MOVESET_TO_MAIN[at[0][0]]
    repaired=[]
    dangling=[]
    for owner,location,target,offset in all_links:
        limit=sizes[target] if target<len(sizes) else 0
        if offset<=limit:
            repaired.append((owner,location,target,offset));continue
        replacement=None
        parent=parent_main.get(owner)
        if parent is not None:
            for ploc,ptarget,poffset in by_owner.get(parent,()):
                if ploc==location and poffset==offset and ptarget<len(sizes) and offset<=sizes[ptarget]:
                    replacement=(ptarget,poffset);break
        if replacement is None and target<len(sizes):
            successors=sorted({row[1] for row in by_owner.get(target,()) if row[1]<len(sizes) and offset<=sizes[row[1]]})
            if len(successors)==1:replacement=(successors[0],offset)
        if replacement is None and offset%4==0 and target<len(sizes) and offset//4<=sizes[target]:
            replacement=(target,offset//4)
        if replacement is None:
            dangling.append(dict(file=owner,location=location,target_file=target,target_offset=offset,
                                 target_size=limit,action="dropped"))
            continue
        repaired.append((owner,location,*replacement))
        if replacement!=(target,offset):
            dangling.append(dict(file=owner,location=location,target_file=target,target_offset=offset,
                                 target_size=limit,action="retargeted",
                                 new_file=replacement[0],new_offset=replacement[1]))
    grouped={}
    for owner,location,target,offset in repaired:
        grouped.setdefault(owner,[]).append((location,target,offset))
    for owner,rows in grouped.items():
        if rows!=by_owner.get(owner):
            write_link_map(directory/f"{owner:04d}.links.tsv",rows)
    still_oor=[item for item in dangling if item.get("action")=="dropped"]
    print(json.dumps({"retargeted":sum(1 for item in dangling if item.get("action")=="retargeted"),
                      "dropped":len(still_oor)},indent=2))
    return repaired,still_oor

def repair_extracted_assets(output):
    directory=output/"reloc"
    sizes=[]
    with (directory/"manifest.tsv").open() as f:
        for row in csv.DictReader(f,delimiter="\t"):
            ident=int(row["id"])
            while len(sizes)<=ident:sizes.append(0)
            sizes[ident]=int(row["size"])
    all_links=[]
    for path in sorted(directory.glob("*.links.tsv")):
        if path.name=="manifest.tsv":continue
        try:owner=int(path.stem.split(".")[0])
        except ValueError:continue
        for location,target,offset in load_link_map(path):
            all_links.append((owner,location,target,offset))
    repaired,dangling=repair_out_of_range_links(directory,sizes,all_links)
    (output/"relocation_diagnostics.json").write_text(json.dumps(dangling,indent=2)+"\n")
    report_path=output/"extraction.json"
    if report_path.exists():
        report=json.loads(report_path.read_text())
        report["out_of_range_references"]=len(dangling)
        report["relocations"]=len(repaired)
        report_path.write_text(json.dumps(report,indent=2)+"\n")
    return dangling

def extract(rom_path,source,output):
    (output/".complete").unlink(missing_ok=True)
    rom=rom_path.read_bytes()
    digest=hashlib.md5(rom).hexdigest()
    if digest!=RELEASE_MD5:raise ValueError(f"ROM is not the verified Remix 2.0.1 release: MD5 {digest}")
    table,base,rows=table_info(rom);count=len(rows)-1
    decode=vpk_decoder(source)
    names={}
    for name,value in re.findall(r"constant\s+(\w+)\((0x[0-9a-fA-F]+)\)",(source/"src/File.asm").read_text()):
        names.setdefault(int(value,16),name)
    directory=output/"reloc";directory.mkdir(parents=True,exist_ok=True)
    manifest=[];all_links=[];sizes=[]
    for ident,(raw,internal,stored,external,unpacked) in enumerate(rows[:-1]):
        offset=raw&0x7fffffff;next_offset=rows[ident+1][0]&0x7fffffff
        stored*=4
        if offset+stored>next_offset:raise ValueError(f"{ident}: payload overlaps next resource")
        payload=rom[base+offset:base+offset+stored]
        if raw&0x80000000:
            if payload[:4]!=b"vpk0":raise ValueError(f"{ident}: missing VPK header")
            data=bytes(decode(payload))
        else:data=payload
        if raw&0x80000000 and len(data)!=struct.unpack_from(">I",payload,4)[0]:
            raise ValueError(f"{ident}: VPK decoded size mismatch")
        # Release 2.0.1 resource 5439 has a byte tail beyond its truncated
        # word count. The VPK byte-length is authoritative for decompression.
        if abs(len(data)-unpacked*4)>3:raise ValueError(f"{ident}: decoded size mismatch")
        dependencies=rom[base+offset+stored:base+next_offset]
        links=links_for(data,internal,external,dependencies,ident,count)
        (directory/f"{ident:04d}.bin").write_bytes(data)
        with (directory/f"{ident:04d}.links.tsv").open("w") as f:
            writer=csv.writer(f,delimiter="\t",lineterminator="\n")
            writer.writerow(["location","target_file","target_offset"]);writer.writerows(links)
        sizes.append(len(data));all_links.extend((ident,*link) for link in links)
        manifest.append([ident,names.get(ident,f"RemixResource{ident}"),len(data),hashlib.sha256(data).hexdigest()])
        if ident%500==0:print(f"Decoded {ident}/{count} resources",flush=True)
    all_links,dangling=repair_out_of_range_links(directory,sizes,all_links)
    (output/"relocation_diagnostics.json").write_text(json.dumps(dangling,indent=2)+"\n")
    with (directory/"manifest.tsv").open("w") as f:
        writer=csv.writer(f,delimiter="\t",lineterminator="\n")
        writer.writerow(["id","name","size","sha256"]);writer.writerows(manifest)
    report=dict(version="2.0.1",rom_md5=digest,rom_sha256=hashlib.sha256(rom).hexdigest(),
                archive_offset=table,resources=count,relocations=len(all_links),out_of_range_references=len(dangling),
                decoded_bytes=sum(sizes),decoder_sha256=hashlib.sha256((source/"scripts/SSB.py").read_bytes()).hexdigest())
    (output/"extraction.json").write_text(json.dumps(report,indent=2)+"\n")
    (output/".complete").write_text("Sagas Remix 2.0.1 resource archive v1\n")
    print(json.dumps(report,indent=2))
    return report

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    workspace=Path(__file__).resolve().parents[2]
    parser.add_argument("--source",type=Path,default=workspace/"smashremix")
    parser.add_argument("--rom",type=Path,default=workspace/"sagas/build/remix/smashremix.z64")
    parser.add_argument("--patch",type=Path)
    parser.add_argument("--base-rom",type=Path,default=workspace/"ssb-decomp-re/baserom.us.z64")
    parser.add_argument("--output",type=Path,default=workspace/"sagas/assets/remix")
    parser.add_argument("--overwrite",action="store_true",help="Explicitly replace existing extracted assets, discarding local edits")
    parser.add_argument("--repair",action="store_true",help="Retarget out-of-range relocations in an existing extraction without re-decoding the ROM")
    args=parser.parse_args()
    if args.repair:
        dangling=repair_extracted_assets(args.output)
        print(json.dumps({"remaining_out_of_range_references":len(dangling)},indent=2));raise SystemExit(0)
    if args.output.exists() and any(args.output.iterdir()) and not args.overwrite:
        parser.error("Asset output is not empty; choose another --output or use --overwrite to replace it")
    if args.patch:
        args.rom.parent.mkdir(parents=True,exist_ok=True)
        if args.rom.exists():raise SystemExit("ROM output already exists; use --rom to extract it or choose a new output path")
        subprocess.run(["xdelta3","-d","-s",str(args.base_rom),str(args.patch),str(args.rom)],check=True)
    extract(args.rom,args.source,args.output)
