"""CLI for inspectable interoperability research."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from .assets import index_content, inventory, load_index, references_from_state, resolve_asset, save_index
from .corpus import audit_assets, save_report, scan
from .serum import count_explicit_parameters, populated_objects, read_preset


def main() -> None:
    parser = argparse.ArgumentParser(prog="zygzxg")
    commands = parser.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("inspect-preset", help="decode a Serum 2 preset and report structural data")
    inspect.add_argument("preset", type=Path)
    inspect.add_argument("--asset-index", type=Path)
    index = commands.add_parser("index-assets", help="index an extracted or installed Serum content folder")
    index.add_argument("root", type=Path)
    index.add_argument("--output", type=Path)
    resolve = commands.add_parser("resolve-asset", help="resolve an asset against an existing local index")
    resolve.add_argument("index", type=Path)
    resolve.add_argument("category", choices=("wavetable", "sample", "noise", "multisample", "impulse", "arp", "clip", "lfo_path", "lfo_shape", "preset"))
    resolve.add_argument("reference")
    corpus = commands.add_parser("scan-corpus", help="aggregate parameter vocabulary from Serum presets")
    corpus.add_argument("root", type=Path)
    corpus.add_argument("--output", type=Path)
    audit = commands.add_parser("audit-assets", help="check preset references against one local asset index")
    audit.add_argument("preset_root", type=Path)
    audit.add_argument("index", type=Path)
    args = parser.parse_args()
    if args.command == "inspect-preset":
        document = read_preset(args.preset)
        result = {
            "preset": str(args.preset),
            "metadata": document.metadata,
            "state_top_level_keys": len(document.state),
            "populated_top_level_objects": len(populated_objects(document.state)),
            "explicit_parameter_fields": count_explicit_parameters(document.state),
            "semantic_coverage": "unmeasured; no DSP mapping exists yet",
        }
        if args.asset_index:
            asset_index = load_index(args.asset_index)
            references = references_from_state(document.state)
            result["asset_references"] = [
                {**reference, **resolve_asset(asset_index, reference["category"], reference["reference"])}
                for reference in references
            ]
            result["asset_status_counts"] = {
                status: sum(item["status"] == status for item in result["asset_references"])
                for status in ("resolved", "missing", "ambiguous")
            }
    elif args.command == "index-assets":
        content = index_content(args.root)
        if args.output:
            save_index(content, args.output)
        result = inventory(content)
        result["index_path"] = str(args.output) if args.output else None
    elif args.command == "scan-corpus":
        result = scan(args.root)
        if args.output:
            save_report(result, args.output)
        result = {
            "preset_files": result["preset_files"],
            "decoded_files": result["decoded_files"],
            "parameter_paths": len(result["parameter_paths"]),
            "errors": result["errors"],
            "output": str(args.output) if args.output else None,
        }
    elif args.command == "audit-assets":
        result = audit_assets(args.preset_root, load_index(args.index))
    else:
        result = resolve_asset(load_index(args.index), args.category, args.reference)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
