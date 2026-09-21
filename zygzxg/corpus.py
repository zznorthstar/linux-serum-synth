"""Aggregate structure and parameter vocabulary from local preset fixtures.

The report contains identifier names and aggregate counts, never preset/asset data.
"""

from __future__ import annotations

from collections import Counter, defaultdict
import json
from pathlib import Path
import re
from typing import Any

from .assets import AssetResolver, references_from_state
from .serum import PresetFormatError, read_preset

INDEX_PATTERN = re.compile(
    r"(Oscillator|WTOsc|NoiseOsc|SampleOsc|MultiSampleOsc|SpectralOsc|"
    r"GranularOsc|SubOsc|FXRack|ArpClip|Arp|MidiClip|ModSlot|RoutingSlot|"
    r"Global|Env|LFO|Macro|VoiceFilter|VoicePanel|PitchQuantizer|"
    r"RetriggerState|ClipPlayer|LFOPointModBus)(\d+)"
)


def _normalize(path: str) -> str:
    return INDEX_PATTERN.sub(lambda match: match.group(1) + "#", path)


def _walk(
    value: Any, path: str, params: Counter, enums: dict[str, set[str]],
    assets: Counter, structure: Counter,
) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            child_path = f"{path}.{key}" if path else key
            structure[_normalize(child_path)] += 1
            if key == "plainParams" and isinstance(child, dict):
                for parameter, setting in child.items():
                    name = _normalize(f"{path}.{parameter}")
                    params[name] += 1
                    structure[_normalize(child_path + "." + parameter)] += 1
                    if isinstance(setting, str):
                        enums[name].add(setting)
            elif (key.startswith("relativePathTo") or key.endswith("PathRelative")) and isinstance(child, str):
                assets[_normalize(child_path)] += 1
            elif key == "files" and isinstance(child, dict):
                structure[_normalize(child_path + "[]")] += len(child)
            else:
                _walk(child, child_path, params, enums, assets, structure)
    elif isinstance(value, list):
        for child in value:
            _walk(child, path + "[]", params, enums, assets, structure)


def scan(root: str | Path) -> dict[str, Any]:
    base = Path(root)
    files = sorted(base.rglob("*.SerumPreset"))
    params: Counter[str] = Counter()
    module_names: Counter[str] = Counter()
    fx_types: Counter[str] = Counter()
    assets: Counter[str] = Counter()
    structure: Counter[str] = Counter()
    enums: dict[str, set[str]] = defaultdict(set)
    errors: list[str] = []
    for path in files:
        try:
            state = read_preset(path).state
        except (OSError, PresetFormatError) as exc:
            errors.append(f"{path.relative_to(base)}: {exc}")
            continue
        for key, value in state.items():
            module_names[_normalize(key)] += 1
            _walk(value, key, params, enums, assets, structure)
            if key.startswith("FXRack") and isinstance(value, dict):
                for effect in value.get("FX", []):
                    if isinstance(effect, dict):
                        for child_key in effect:
                            if child_key.startswith("FX"):
                                fx_types[child_key] += 1
    return {
        "format": "zygzxg.serum-corpus.v1",
        "preset_files": len(files),
        "decoded_files": len(files) - len(errors),
        "errors": errors,
        "module_keys": dict(sorted(module_names.items())),
        "parameter_paths": dict(sorted(params.items())),
        "string_parameter_values": {key: sorted(value) for key, value in sorted(enums.items())},
        "fx_modules": dict(sorted(fx_types.items())),
        "asset_reference_fields": dict(sorted(assets.items())),
        "structure_paths": dict(sorted(structure.items())),
    }


def save_report(report: dict[str, Any], path: str | Path) -> None:
    Path(path).write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")


def audit_assets(preset_root: str | Path, index: dict[str, Any]) -> dict[str, Any]:
    root = Path(preset_root)
    files = [root] if root.is_file() else sorted(root.rglob("*.SerumPreset"))
    resolver = AssetResolver(index)
    statuses: Counter[str] = Counter()
    categories: Counter[str] = Counter()
    presets_with_missing = 0
    examples: list[dict[str, str]] = []
    for path in files:
        document = read_preset(path)
        missing_here = False
        for reference in references_from_state(document.state):
            categories[reference["category"]] += 1
            try:
                outcome = resolver.resolve(reference["category"], reference["reference"])
                status = outcome["status"]
            except (OSError, ValueError):
                status = "invalid"
            statuses[status] += 1
            if status != "resolved":
                missing_here = True
                if len(examples) < 20:
                    examples.append({
                        "preset": str(path),
                        "category": reference["category"],
                        "reference": reference["reference"],
                        "status": status,
                    })
        presets_with_missing += missing_here
    return {
        "presets": len(files),
        "references": sum(statuses.values()),
        "status_counts": dict(sorted(statuses.items())),
        "category_counts": dict(sorted(categories.items())),
        "presets_with_unresolved": presets_with_missing,
        "first_unresolved": examples,
    }
