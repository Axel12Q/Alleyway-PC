import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
ROM=ROOT.parent/"Local-Privado/roms/Alleyway (USA).gb"


class AudioSequenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.probe=ROOT/"build/audio-sequence-probe.exe"
        cls.probe.parent.mkdir(exist_ok=True)
        subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-Wall","-Wextra","-Werror",
                        "-I",str(ROOT/"src"),str(ROOT/"tests/audio_sequence_probe.c"),"-o",str(cls.probe)],
                       check=True,capture_output=True)

    def sequence(self,path,effect,frames=160):
        text=subprocess.check_output([str(self.probe),str(path),str(effect),str(frames)],text=True)
        return [list(map(int,line.split())) for line in text.splitlines()]

    def test_software_effects_stop_in_production(self):
        # Sweeps use code, not ROM tables. This catches the previously endless
        # NR12=F0/NR14=87 static wall tone without commercial fixtures.
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"synthetic.bin"
            path.write_bytes(bytes(32768))
            for effect in (9,10,11,12):
                with self.subTest(effect=effect):
                    seq=self.sequence(path,effect)
                    self.assertTrue(any(row[9] for row in seq))
                    self.assertEqual(seq[-1][0],0)
                    self.assertEqual(seq[-1][9],0,"DAC must be switched off")

    @unittest.skipUnless(ROM.is_file(),"private reference ROM absent")
    def test_all_primary_sfx_register_sequences_against_original(self):
        from tools.capture_audio_reference import capture
        for effect in range(1,13):
            with self.subTest(effect=effect):
                original=capture(ROM,effect)
                self.assertTrue(original)
                self.assertEqual(original,self.sequence(ROM,effect,len(original)))

    def test_unsupported_rom_is_rejected_without_game_window(self):
        subprocess.run([sys.executable,str(ROOT/"tools/build.py")],check=True,capture_output=True)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"wrong.gb"
            path.write_bytes(bytes(32768))
            proc=subprocess.run([str(ROOT/"build/alleyway-lab.exe"),"--check-rom",str(path)],
                                capture_output=True,text=True,timeout=5)
            self.assertEqual(proc.returncode,1)
            self.assertFalse(json.loads(proc.stdout)["supported"])


if __name__=="__main__":
    unittest.main()
