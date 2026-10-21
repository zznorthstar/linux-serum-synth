import tempfile
import unittest
from pathlib import Path

from zygzxg.assets import index_content, references_from_state, resolve_asset, save_index, load_index
from zygzxg.corpus import save_report
from zygzxg.serum import PresetFormatError, SerumDocument, decode_bytes, encode_bytes


class SerumContainerTests(unittest.TestCase):
    def test_unknown_data_survives_decode_encode(self):
        document = SerumDocument(
            metadata={"fileType": "SerumPreset", "futureHeader": [1, 2]},
            state={"FutureModule0": {"plainParams": {"unknown": 0.7}, "opaque": b"xyz"}},
            container_flags=2,
            metadata_flags=17,
        )
        self.assertEqual(decode_bytes(encode_bytes(document)), document)

    def test_bad_frame_is_rejected(self):
        document = SerumDocument({"fileType": "SerumPreset"}, {"Global0": {}}, 2)
        self.assertRaises(PresetFormatError, decode_bytes, encode_bytes(document)[:-3])


class AssetIndexTests(unittest.TestCase):
    def test_unicode_asset_index_and_corpus_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset = root / "Tables" / "User" / "Tōne 音.wav"
            asset.parent.mkdir(parents=True)
            asset.write_bytes(b"RIFF")
            index = index_content(root)
            target = root / "index.json"
            save_index(index, target)
            self.assertEqual(load_index(target), index)
            self.assertIn("Tōne 音.wav", target.read_text(encoding="utf-8"))
            report = root / "report.json"
            save_report({"name": "Tōne 音"}, report)
            self.assertIn("Tōne 音", report.read_text(encoding="utf-8"))

    def test_exact_resolution_and_missing_asset(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            table = root / "Tables" / "User" / "My Table.wav"
            table.parent.mkdir(parents=True)
            table.write_bytes(b"RIFF")
            index = index_content(root)
            self.assertEqual(
                resolve_asset(index, "wavetable", "User/My Table.wav")["status"],
                "resolved",
            )
            self.assertEqual(
                resolve_asset(index, "wavetable", "User/Absent.wav")["status"],
                "missing",
            )
            self.assertRaises(ValueError, resolve_asset, index, "wavetable", "../elsewhere.wav")

    def test_multisample_children_are_referenced(self):
        state = {
            "Oscillator0": {
                "MultiSampleOsc0": {
                    "sfzPathRelative": "Factory/Winds/Horns.sfz",
                    "files": {"Horns Samples\\C4.flac": {"numFrames": 1234}},
                }
            }
        }
        refs = references_from_state(state)
        self.assertEqual(len(refs), 2)
        self.assertEqual(refs[0]["reference"], "Factory/Winds/Horns Samples/C4.flac")
        self.assertEqual(refs[1]["reference"], "Factory/Winds/Horns.sfz")

    def test_serum_virtual_root_conventions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in ("Tables/Analog/Basic.wav", "Multisamples/Factory/Keys/C4.flac"):
                target = root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(b"sample")
            index = index_content(root)
            self.assertEqual(resolve_asset(index, "wavetable", "/Analog/Basic.wav")["status"], "resolved")
            self.assertEqual(
                resolve_asset(index, "sample", "../Multisamples/Factory/Keys/C4.flac")["status"],
                "resolved",
            )

    def test_authoring_machine_path_rebases_to_local_samples(self):
        state = {
            "Oscillator0": {
                "MultiSampleOsc0": {
                    "sfzPathRelative": "Factory/Drums/Kit.sfz",
                    "files": {
                        "C:\\Users\\maker\\Serum 2 Presets\\Samples\\Factory Non-Tonal\\Drum\\Kick.flac": {}
                    },
                }
            }
        }
        child = references_from_state(state)[0]
        self.assertEqual(child["category"], "sample")
        self.assertEqual(child["reference"], "Factory Non-Tonal/Drum/Kick.flac")


if __name__ == "__main__":
    unittest.main()
