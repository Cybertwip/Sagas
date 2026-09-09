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
    worker('retarget',source,destination/'retargeted.fbx',working)
    return destination/'rigid_mesh.json'

if __name__=='__main__':
    print(convert(Path(sys.argv[1]),Path(sys.argv[2])))
