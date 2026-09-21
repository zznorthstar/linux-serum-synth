"""Reference the user's local Serum content by path; never copy or bundle it."""

from __future__ import annotations

from collections import Counter
from dataclasses import asdict, dataclass
import json
from pathlib import Path, PurePosixPath
import posixpath
from typing import Any

CONTENT_DIRS = (
    "Presets", "Tables", "Samples", "Multisamples", "Impulses", "Clips",
    "Arp Patterns", "LFO Paths", "LFO Shapes", "Skins", "System", "FX Presets",
)
ASSET_DIRS = {
    "wavetable": ("Tables",),
    "sample": ("Samples",),
    "noise": ("Samples/Factory Non-Tonal/Noises", "Samples"),
    "multisample": ("Multisamples",),
    "impulse": ("Impulses",),
    "arp": ("Arp Patterns",),
    "clip": ("Clips",),
    "lfo_path": ("LFO Paths",),
    "lfo_shape": ("LFO Shapes",),
    "preset": ("Presets",),
}
REFERENCE_FIELDS = {
    "relativePathToWT": "wavetable",
    "relativePathToNoiseSample": "noise",
    "relativePathToIR": "impulse",
    "samplePathRelative": "sample",
    "sfzPathRelative": "multisample",
}


@dataclass(frozen=True)
class AssetEntry:
    path: str
    bytes: int


def index_content(root: str | Path) -> dict[str, Any]:
    base = Path(root).expanduser().resolve(strict=True)
    if not base.is_dir():
        raise ValueError("asset root must be a directory")
    entries: list[AssetEntry] = []
    for category in CONTENT_DIRS:
        directory = base / category
        if not directory.is_dir():
            continue
        for item in directory.rglob("*"):
            if item.is_symlink() or not item.is_file():
                continue
            entries.append(AssetEntry(item.relative_to(base).as_posix(), item.stat().st_size))
    entries.sort(key=lambda entry: entry.path.casefold())
    return {
        "format": "zygzxg.asset-index.v1",
        "source_root": str(base),
        "entries": [asdict(entry) for entry in entries],
    }


def inventory(index: dict[str, Any]) -> dict[str, Any]:
    counts = Counter(entry["path"].split("/", 1)[0] for entry in index["entries"])
    return {
        "source_root": index["source_root"],
        "file_count": len(index["entries"]),
        "total_bytes": sum(entry["bytes"] for entry in index["entries"]),
        "categories": dict(sorted(counts.items())),
    }


def save_index(index: dict[str, Any], destination: str | Path) -> None:
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(index, ensure_ascii=False, indent=2) + "\n")


def load_index(path: str | Path) -> dict[str, Any]:
    index = json.loads(Path(path).read_text())
    if index.get("format") != "zygzxg.asset-index.v1":
        raise ValueError("unsupported asset-index format")
    return index


class AssetResolver:
    """Reusable exact-path resolver for one private content index."""

    def __init__(self, index: dict[str, Any]):
        self.root = Path(index["source_root"]).resolve(strict=True)
        self.names: dict[str, list[str]] = {}
        for entry in index["entries"]:
            self.names.setdefault(entry["path"].casefold(), []).append(entry["path"])

    def resolve(self, category: str, reference: str) -> dict[str, Any]:
        if category not in ASSET_DIRS:
            raise ValueError(f"unknown asset category: {category}")
        reference = reference.replace("\\", "/")
        # Serum uses virtual category roots. A leading slash is not a Linux
        # absolute path here; ../ can refer to a sibling content directory.
        reference = reference.lstrip("/")
        relative = PurePosixPath(reference)
        if relative.is_absolute() or not relative.parts or any(":" in part for part in relative.parts):
            raise ValueError("asset reference must be a safe relative path")
        matches: list[str] = []
        valid_candidates = 0
        for directory in ASSET_DIRS[category]:
            candidate = posixpath.normpath(f"{directory}/{relative.as_posix()}")
            if candidate.startswith("../") or candidate == "..":
                continue
            if candidate.split("/", 1)[0] not in CONTENT_DIRS:
                continue
            valid_candidates += 1
            for found in self.names.get(candidate.casefold(), []):
                if found not in matches:
                    matches.append(found)
        if not valid_candidates:
            raise ValueError("asset reference escapes known content roots")
        if len(matches) == 1:
            path = (self.root / matches[0]).resolve(strict=True)
            if not path.is_relative_to(self.root):
                raise ValueError("asset path escaped the indexed root")
            return {"status": "resolved", "path": str(path), "reference": reference}
        return {"status": "ambiguous" if matches else "missing", "matches": matches, "reference": reference}


def resolve_asset(index: dict[str, Any], category: str, reference: str) -> dict[str, Any]:
    return AssetResolver(index).resolve(category, reference)


def references_from_state(state: dict[str, Any]) -> list[dict[str, str]]:
    """Enumerate known external references, including multisample child files."""
    found: list[dict[str, str]] = []

    def visit(value: Any, location: str) -> None:
        if isinstance(value, dict):
            sfz = value.get("sfzPathRelative")
            children = value.get("files")
            if isinstance(sfz, str) and isinstance(children, dict):
                parent = PurePosixPath(sfz.replace("\\", "/")).parent
                for filename in children:
                    if isinstance(filename, str):
                        child_name = filename.replace("\\", "/")
                        # Some factory-generated SFZ maps contain a Windows
                        # authoring-machine path. Rebase only a recognized
                        # content-root suffix into the user's indexed root.
                        if "/Samples/" in child_name:
                            child_category = "sample"
                            child_reference = child_name.rsplit("/Samples/", 1)[1]
                        elif "/Multisamples/" in child_name:
                            child_category = "multisample"
                            child_reference = child_name.rsplit("/Multisamples/", 1)[1]
                        else:
                            child_category = "multisample"
                            child_reference = str(parent / child_name)
                        found.append({
                            "category": child_category,
                            "reference": child_reference,
                            "state_path": f"{location}.files",
                        })
            for key, child in value.items():
                if key in REFERENCE_FIELDS and isinstance(child, str) and child:
                    if key == "sfzPathRelative" and isinstance(value.get("embedded_sfz"), str) and value["embedded_sfz"]:
                        continue
                    found.append({
                        "category": REFERENCE_FIELDS[key],
                        "reference": child,
                        "state_path": f"{location}.{key}",
                    })
                elif key != "files":
                    visit(child, f"{location}.{key}" if location else key)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                visit(child, f"{location}[{index}]")

    visit(state, "")
    return found
