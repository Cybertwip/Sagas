#!/usr/bin/env python3
"""Sagas character-package and roster editor. No Remix overlay generation."""
from pathlib import Path
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import argparse, base64, json, re, shutil, sys
from stellar_shared import atomic_json

ROOT = Path(__file__).resolve().parents[2]
KINDS = ['Luigi','Mario','Donkey','Link','Samus','Captain','Ness','Yoshi','Kirby','Fox','Pikachu','Purin']

def initial():
    return {'version':1,'columns':6,'characters':[{'id':n.lower(),'name':n,'base':n,'builtin':True,'enabled':True} for n in KINDS]}

def save_roster(root, state):
    columns=int(state.get('columns',6))
    if not 3<=columns<=12: raise ValueError('Columns must be between 3 and 12')
    characters=state.get('characters',[])
    if not 1<=len(characters)<=120: raise ValueError('Roster must contain 1–120 entries')
    seen=set(); lines=[str(columns)]
    for c in characters:
        if not re.fullmatch(r'[a-z0-9_-]+',c['id']) or c['id'] in seen: raise ValueError('Invalid or duplicate character ID')
        seen.add(c['id'])
        if c['base'] not in KINDS: raise ValueError('Unknown base fighter')
        if any(x in c['name'] for x in '\t\r\n'): raise ValueError('Name cannot contain tabs or newlines')
        portrait=c.get('portrait','')
        if portrait and (not portrait.startswith('mods/') or '..' in Path(portrait).parts or any(x in portrait for x in '\t\r\n')): raise ValueError('Invalid portrait path')
        if c.get('enabled',True): lines.append(f"{KINDS.index(c['base'])}\t{c['name']}\t{portrait}")
    if len(lines)==1: raise ValueError('Enable at least one roster entry')
    folder=root/'mods';folder.mkdir(parents=True,exist_ok=True)
    atomic_json(folder/'roster.json',state)
    temporary=folder/'roster.tsv.tmp';temporary.write_text('\n'.join(lines)+'\n');temporary.replace(folder/'roster.tsv')

def import_character(root, request):
    name=request.get('name','').strip();base=request.get('base','Mario')
    slug=re.sub('[^a-z0-9_-]+','-',name.lower()).strip('-')
    if not slug or base not in KINDS: raise ValueError('Enter a name and valid base fighter')
    folder=root/'mods'/'characters'/slug
    if folder.exists(): raise ValueError('A package with this name already exists')
    files=request.get('files',[])
    if not files: raise ValueError('Choose a Stellar project, model, portrait, or audio file')
    decoded=[]
    for item in files:
        filename=Path(item['name']).name
        if Path(filename).suffix.lower() not in {'.json','.fbx','.glb','.gltf','.bin','.png','.wav'}: raise ValueError('Unsupported asset type')
        decoded.append((filename,base64.b64decode(item['data'],validate=True)))
    folder.mkdir(parents=True)
    character={'id':slug,'name':name,'base':base,'enabled':True,'builtin':False,'model_status':'conversion_pending'}
    for filename,data in decoded:
        (folder/filename).write_bytes(data)
        if filename.lower().endswith('.png') and 'portrait' not in character: character['portrait']=f'mods/characters/{slug}/{filename}'
    atomic_json(folder/'character.json',character)
    return character

class Handler(BaseHTTPRequestHandler):
    def reply(self,status,data,kind='application/json'):
        payload=json.dumps(data).encode() if kind=='application/json' else data
        self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    def do_GET(self):
        if self.path=='/': return self.reply(200,Path(__file__).with_name('index.html').read_bytes(),'text/html; charset=utf-8')
        if self.path=='/api/roster':
            file=self.server.asset_root/'mods'/'roster.json'
            return self.reply(200,json.loads(file.read_text()) if file.exists() else initial())
        self.reply(404,{'error':'Not found'})
    def do_POST(self):
        # Reject requests from unrelated browser origins; all edits stay local.
        origin=self.headers.get('Origin')
        if origin and origin!=f'http://{self.headers.get("Host")}': return self.reply(403,{'error':'Origin rejected'})
        try:
            length=int(self.headers.get('Content-Length',0))
            if not 0<length<=100*1024*1024: raise ValueError('Request too large or empty')
            request=json.loads(self.rfile.read(length))
            if self.path=='/api/roster': save_roster(self.server.asset_root,request);result={'saved':True}
            elif self.path=='/api/import': result=import_character(self.server.asset_root,request)
            else: return self.reply(404,{'error':'Not found'})
            self.reply(200,result)
        except (ValueError,KeyError,OSError) as error: self.reply(400,{'error':str(error)})

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--assets',type=Path,default=ROOT/'build'/'assets');parser.add_argument('--port',type=int,default=8765);args=parser.parse_args()
    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler);server.asset_root=args.assets.resolve()
    print(f'Sagas Studio: http://127.0.0.1:{args.port}',flush=True)
    server.serve_forever()
if __name__=='__main__': main()
