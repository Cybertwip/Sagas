import base64
from pathlib import Path
import tempfile
import unittest
from server import initial, import_character, save_roster

class StudioTests(unittest.TestCase):
    def test_import_and_roster(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            imported=import_character(root,{'name':'Test Fighter','base':'Fox','files':[{'name':'model.fbx','data':base64.b64encode(b'fixture model').decode()}]})
            self.assertEqual((root/'mods/characters/test-fighter/model.fbx').read_bytes(),b'fixture model')
            state=initial();state['characters'].append(imported);state['columns']=7
            save_roster(root,state)
            rows=(root/'mods/roster.tsv').read_text().splitlines()
            self.assertEqual(rows[0],'7');self.assertEqual(len(rows),14)
            self.assertEqual(rows[-1],'9\tTest Fighter\t')
            self.assertFalse(list(root.rglob('*.c')))
            with self.assertRaises(ValueError): import_character(root,{'name':'Test Fighter','base':'Fox','files':[]})
    def test_invalid_roster_does_not_replace_saved_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);state=initial();save_roster(root,state)
            before=(root/'mods/roster.tsv').read_bytes()
            state['characters'][0]['portrait']='mods/../../outside.png'
            with self.assertRaises(ValueError):save_roster(root,state)
            self.assertEqual((root/'mods/roster.tsv').read_bytes(),before)
if __name__=='__main__':unittest.main()
