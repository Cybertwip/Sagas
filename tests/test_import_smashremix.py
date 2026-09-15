import importlib.util
import struct
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location("importer",Path(__file__).resolve().parents[1]/"tools/import_smashremix.py")
importer=importlib.util.module_from_spec(spec);spec.loader.exec_module(importer)

class MovesetTranslation(unittest.TestCase):
    def test_real_falco_jab(self):
        data=(Path(__file__).resolve().parents[2]/"smashremix/src/Falco/Moveset/JAB_1.bin").read_bytes()
        hits,events=importer.decode_moveset(data)
        self.assertEqual(len(hits),2)
        self.assertEqual(hits[0][:2],[3,5])
        self.assertEqual(hits[0][5],4) # Damage
        self.assertTrue(any(row[1]==22 for row in events)) # Jab continuation flag

    def test_signed_offsets(self):
        words=[3<<26,200<<16|65535,65534<<16|65533,361<<22|100<<12|3,50<<7,1<<26|2,6<<26,0]
        hits,_=importer.decode_moveset(struct.pack(">8I",*words))
        self.assertEqual(hits[0][7:10],[-1,-2,-3])

    def test_truncated_hitbox_is_not_partial_success(self):
        with self.assertRaises(ValueError):
            importer.decode_moveset(struct.pack(">I", 3<<26))
        with self.assertRaises(ValueError):
            importer.decode_moveset(struct.pack(">I", 33<<26))

    def test_falco_grab_decodes(self):
        data=(Path(__file__).resolve().parents[2]/"smashremix/src/Falco/Moveset/GRAB.bin").read_bytes()
        hits,events=importer.decode_moveset(data)
        self.assertEqual(len(hits),1)
        self.assertEqual(hits[0][:2],[6,7])
        self.assertGreater(hits[0][6],0) # size

    def test_finite_loop(self):
        words=[32<<26|3,1<<26|2,33<<26,0]
        _,events=importer.decode_moveset(struct.pack(">4I",*words))
        self.assertEqual(events[-1][0],6)

if __name__=="__main__":unittest.main()
