"""Convert original Stellar FBX projects to native Sagas packages."""
from pathlib import Path
import json, subprocess, sys
from stellar_project import StellarProject
from stellar_blender import auto_map_bones

BLENDER = Path('/Applications/Blender.app/Contents/MacOS/Blender')

def worker(operation, source, output, project):
    output.parent.mkdir(parents=True, exist_ok=True)
    command=[str(BLENDER), '--background', '--factory-startup', '--python', str(Path(__file__).with_name('stellar_blender.py')), '--', operation, str(source), str(output), str(project)]
    with (output.parent/f'{operation}.log').open('w') as log:
        result=subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    if result.returncode or not output.exists():
        raise ValueError(f'Blender {operation} failed. See {output.parent / (operation + ".log")}')

def convert(project_path, destination):
    project_path=Path(project_path).resolve(); destination=Path(destination).resolve()
    raw=json.loads(project_path.read_text())
    source=Path(raw['model_path'])
    if not source.is_absolute(): source=project_path.parent/source
    if not source.is_file(): raise ValueError(f'Model does not exist: {source}')
    project=StellarProject.load(project_path)
    working=destination/'stellar_project.json'
    destination.mkdir(parents=True,exist_ok=True)
    project.model_path=str(source.resolve());project.save(working)
    manifest=destination/'manifest.json'
    worker('inspect',source,manifest,working)
    project.source_manifest=json.loads(manifest.read_text())
    auto_map_bones(project);project.save(working)
    worker('retarget',source,destination/'retargeted.blend',working)
    return destination/'rigid_mesh.json'

if __name__=='__main__':
    print(convert(Path(sys.argv[1]),Path(sys.argv[2])))

def export_native(proxy_path):
    raw=json.loads(Path(proxy_path).read_text())
    from stellar_retarget import prepare_rigid_for_base
    project=StellarProject.load(Path(proxy_path).with_name('stellar_project.json'))
    raw=prepare_rigid_for_base(project,raw)
    lines=['SGMESH1',str(len(raw['bind_joints']))]
    for joint,point in raw['bind_joints'].items(): lines.append(' '.join(map(str,[joint,*point])))
    tex=raw.get('texture') or {};pixels=tex.get('pixels',[])
    lines.append(f"{tex.get('width',0)} {tex.get('height',0)}")
    lines.append(' '.join(map(str,pixels)))
    vertices=[v for tri in raw['geometry'] for v in tri['vertices']]
    lines.append(str(len(vertices)))
    for v in vertices:
        skin=v['skin'];a=skin[0];b=skin[1] if len(skin)>1 else a
        lines.append(' '.join(map(str,[a['joint'],a['weight'],*a['position'],b['joint'],*b['position'],*v['uv'],*v['color']])))
    path=Path(proxy_path).with_name('model.sgmesh');path.write_text('\n'.join(lines)+'\n');return path

def export_audio(project_path,destination):
    import re, struct
    from types import SimpleNamespace
    from stellar_audio import process_sound, read_wav_mono
    project_path=Path(project_path);destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    raw=json.loads(project_path.read_text());base={'Donkey Kong':'Donkey','Captain Falcon':'Captain','Jigglypuff':'Purin'}.get(raw.get('base_character'),raw.get('base_character','Mario'))
    catalog=Path(__file__).resolve().parents[3]/'remix/build/generated/audio_fgm_catalog.inc'
    entries={};symbols={}
    if catalog.exists():
        for identifier,symbol in re.findall(r'\{\s*(\d+),\s*"([^"]+)"',catalog.read_text()):
            symbols[symbol]=int(identifier)
            if symbol.startswith((f'nSYAudioFGM{base}',f'nSYAudioVoice{base}',f'nSYAudioVoicePublic{base}')):
                entries[int(identifier)]=[float(raw.get('global_pitch_semitones',0))+float(raw.get('inherited_pitch_overrides',{}).get(symbol,0)),'']
    for i,sound in enumerate(raw.get('sounds',[])):
        identifier=int(sound.get('fgm_id',-1));symbol=sound.get('game_voice_id','')
        if identifier<0: identifier=symbols.get(symbol,-1)
        if sound.get('kind','').lower()=='announcer': identifier=symbols.get('nSYAudioVoiceAnnounce'+base,-1)
        if identifier<0: continue
        if sound.get('inherited'):
            entries[identifier]=[float(raw.get('global_pitch_semitones',0))+float(sound.get('pitch_semitones',0)),''];continue
        if not sound.get('path'): continue
        source=Path(sound['path']);source=source if source.is_absolute() else project_path.parent/source
        settings={'trim_start':0,'trim_end':0,'gain_db':0,'pitch_semitones':0,'kind':'voice'};settings.update(sound)
        wav=destination/f'sound-{i}.wav';process_sound(source,wav,SimpleNamespace(**settings),float(raw.get('global_pitch_semitones',0)))
        rate,samples=read_wav_mono(wav);pcm=[max(-32768,min(32767,round(sample*32767))) for sample in samples]
        filename=f'sound-{i}.sgpcm';(destination/filename).write_bytes(b'SGPC'+struct.pack('<III',rate,1,len(pcm))+struct.pack('<'+'h'*len(pcm),*pcm))
        entries[identifier]=[0,filename]
    (destination/'audio.tsv').write_text(''.join(f'{key}\t{pitch}\t{path}\n' for key,(pitch,path) in sorted(entries.items())))
