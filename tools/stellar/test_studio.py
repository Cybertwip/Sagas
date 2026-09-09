from pathlib import Path
import json, tempfile, unittest
from unittest.mock import patch
from server import initial, import_project, import_directory, save_roster

class StudioTests(unittest.TestCase):
    def test_remove_slots_preserves_holes_and_packages(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);state=initial()
            package=root/'mods/characters/mia';package.mkdir(parents=True);(package/'character.json').write_text('{}')
            state['characters']=[c for c in state['characters'] if c['id'] not in {'mario','fox'}]
            save_roster(root,state)
            cells=[int(row.split('\t')[-1]) for row in (root/'mods/roster.tsv').read_text().splitlines()[1:]]
            self.assertEqual(cells,[0,2,3,4,5,6,7,8,10,11])
            self.assertTrue((package/'character.json').exists())
            self.assertEqual(json.loads((root/'mods/roster.json').read_text()),state)
    def test_invalid_roster_does_not_replace_saved_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);state=initial();save_roster(root,state)
            before=(root/'mods/roster.tsv').read_bytes()
            state['characters'][0]['portrait']='mods/../../outside.png'
            with self.assertRaises(ValueError): save_roster(root,state)
            self.assertEqual((root/'mods/roster.tsv').read_bytes(),before)
            state=initial();state['characters'][1]['cell']=0
            with self.assertRaises(ValueError): save_roster(root,state)
            self.assertEqual((root/'mods/roster.tsv').read_bytes(),before)
    def test_source_import_and_reimport_without_exports(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary);root=directory/'assets';source=directory/'Characters/Mia';source.mkdir(parents=True)
            (source/'Mia.fbx').write_bytes(b'model');(source/'Mia.png').write_bytes(b'portrait')
            project={'name':'Mia','base_character':'Mario','model_path':'Mia.fbx','portrait_path':'Mia.png'}
            (source/'stellar_project.json').write_text(json.dumps(project));before=(source/'stellar_project.json').read_bytes()
            def conversion(project,destination):
                destination.mkdir(parents=True,exist_ok=True);path=destination/'rigid_mesh.json';path.write_text('{}');return path
            def native(proxy):
                path=proxy.with_name('model.sgmesh');path.write_text('fixture');return path
            with patch('server.convert',side_effect=conversion),patch('server.export_native',side_effect=native):
                first=import_project(root,source);second=import_project(root,source)
                self.assertEqual(first,second);self.assertEqual(first['model_status'],'ready')
                self.assertTrue((root/first['portrait']).is_file())
                result=import_directory(root,source.parent);self.assertEqual(len(result['characters']),1);self.assertFalse(result['errors'])
            self.assertEqual((source/'stellar_project.json').read_bytes(),before)
            self.assertFalse(list(root.rglob('*.c')))

if __name__=='__main__': unittest.main()
