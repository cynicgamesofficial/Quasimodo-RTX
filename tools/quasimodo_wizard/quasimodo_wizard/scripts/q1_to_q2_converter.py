#!/usr/bin/env python3
"""
Quake 1 → Quake 2 .map entity classname converter (Phase 1 + Phase 2 swap tables).

Only replaces values on lines that are exclusively a Valve-style
``"classname" "..."`` pair; brush geometry and all other keys are untouched.
Parsing approach aligns with ``_analyze_maps_q2_port.py`` (quoted strings,
face lines irrelevant here because we only match classname key lines).

Phase 1 is applied before Phase 2 when both could match (no overlapping keys).
Python 3.8+; stdlib only.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Phase 1 swap table (source classname → target classname)
# ---------------------------------------------------------------------------
PHASE1_ENTITY_SWAP: Dict[str, str] = {
    # Monsters
    "monster_dog": "monster_infantry",
    "monster_ogre": "monster_gunner",
    "monster_shambler": "monster_tank",
    "monster_knight": "monster_gladiator",
    "monster_hell_knight": "monster_gladiator",
    "monster_wizard": "monster_flyer",
    "monster_shalrath": "monster_boss",
    "monster_enforcer": "monster_soldier",
    "monster_army": "monster_soldier",
    "monster_demon1": "monster_berserker",
    "monster_tarbaby": "monster_parasite",
    "monster_zombie": "monster_infantry",
    # Weapons
    "weapon_supernailgun": "weapon_hyperblaster",
    "weapon_nailgun": "weapon_machinegun",
    "weapon_lightning": "weapon_railgun",
    "weapon_thunderbolt": "weapon_railgun",
    # weapon_supershotgun, grenadelauncher, rocketlauncher: unchanged in Q2 — omit from table
    # Items
    "item_artifact_super_damage": "item_quad",
    "item_artifact_invisibility": "item_invisibility",
    "item_artifact_invulnerability": "item_invulnerability",
    "item_artifact_envirosuit": "item_envirosuit",
    "item_armor1": "item_armor_jacket",
    "item_armor2": "item_armor_combat",
    "item_armorInv": "item_armor_body",
    "item_spikes": "item_ammo_bullets",
    "item_cells": "item_ammo_cells",
    "item_rockets": "item_ammo_rockets",
    "item_shells": "item_ammo_shells",
    # Triggers / logic
    "trigger_secret": "trigger_once",
}

# ---------------------------------------------------------------------------
# Phase 2 swap table (geometry, triggers, audio, lights, targets, misc)
# Identity mappings omitted — those classnames are listed in Q2_NATIVE only.
# ---------------------------------------------------------------------------
PHASE2_ENTITY_SWAP: Dict[str, str] = {
    # Geometry / func
    "func_illusionary": "func_wall",
    "func_door_secret": "func_door",
    "func_void": "func_wall",
    # Player / info
    "info_player_coop": "info_player_deathmatch",
    "info_null": "info_notnull",
    "info_player_start_test": "info_player_start",
    # Triggers
    "trigger_monsterjump": "trigger_push",
    "trigger_timer": "trigger_once",
    "trigger_once_box": "trigger_once",
    "trigger_monsterjump_box": "trigger_push",
    "trigger_teleport_box": "trigger_teleport",
    "trigger_fogblend": "trigger_once",
    # Items
    "item_backpack": "item_pack",
    "item_sigil": "item_key1",
    "air_bubbles": "func_wall",
    # Ambients / sound
    "ambient_comp_hum": "target_speaker",
    "ambient_drone": "target_speaker",
    "ambient_drip": "target_speaker",
    "ambient_flouro_buzz": "target_speaker",
    "ambient_light_buzz": "target_speaker",
    "ambient_suck_wind": "target_speaker",
    "ambient_sound": "target_speaker",
    # Lights
    "light_flame_large_yellow": "light",
    "light_flame_small_yellow": "light",
    "light_globe": "light",
    "light_fluoro": "light",
    "light_fluorospark": "light",
    "light_candle": "light",
    # Targets / logic
    "target_autosave": "target_changelevel",
    "target_screenshake": "target_explosion",
    "target_sound": "target_speaker",
    "target_lock": "trigger_once",
    "target_state": "trigger_once",
    "target_items": "target_spawner",
    "target_multiprint": "target_string",
    "target_secret": "trigger_once",
    "target_setskill": "worldspawn",
    "target_meat_fireworks": "target_explosion",
    "target_telefog": "func_wall",
    # Traps / misc
    "trap_spikeshooter": "func_wall",
    "trap_shooter": "func_wall",
    "trap_lightning": "func_wall",
    "misc_explobox2": "misc_explobox",
    "misc_teleporttrain": "func_train",
    "misc_particlefield": "func_wall",
    "misc_particlefield_box": "func_wall",
    "monster_oldone": "monster_boss",
    "monster_fish": "monster_parasite",
    "info_teleport_target": "info_teleport_destination",
}

# Phase 2 sources that need manual follow-up; shown as ⚠ on the report line.
PHASE2_SWAP_FLAGS: Dict[str, str] = {
    "func_illusionary": "check spawnflags",
    "func_door_secret": "check spawnflags",
    "func_void": "consider deleting - no visual purpose",
    "trigger_timer": "check wait key",
    "trigger_fogblend": "visual effect lost",
    "item_sigil": "visual only - no Q2 equiv",
    "air_bubbles": "visual effect lost",
    "ambient_comp_hum": 'needs "noise" key set manually',
    "ambient_drone": 'needs "noise" key set manually',
    "ambient_drip": 'needs "noise" key set manually',
    "ambient_flouro_buzz": 'needs "noise" key set manually',
    "ambient_light_buzz": 'needs "noise" key set manually',
    "ambient_suck_wind": 'needs "noise" key set manually',
    "ambient_sound": 'needs "noise" key set manually',
    "target_autosave": "behavior differs",
    "target_screenshake": "visual differs",
    "target_sound": 'needs "noise" key set manually',
    "target_lock": "lock logic lost - manual review",
    "target_state": "state logic lost - manual review",
    "target_items": "verify Q2 spawner syntax",
    "target_multiprint": "verify Q2 string entity",
    "target_secret": "secret count lost",
    "target_setskill": 'set "skill" key on worldspawn',
    "target_meat_fireworks": "visual only",
    "target_telefog": "visual effect lost",
    "trap_spikeshooter": "no Q2 equivalent - remove",
    "trap_shooter": "no Q2 equivalent - remove",
    "trap_lightning": "no Q2 equivalent - remove",
    "misc_teleporttrain": "verify path_corner chain",
    "misc_particlefield": "visual effect lost",
    "misc_particlefield_box": "visual effect lost",
}

# Kept as-is; each occurrence logs a review warning (Q2 teleporter wiring).
TELEPORT_DEST_CLASSNAME = "info_teleport_destination"

# Known Q2-native classnames: do not report as “unmapped / needs review”.
Q2_NATIVE_CLASSNAMES: Set[str] = {
    "worldspawn",
    "func_door",
    "func_wall",
    "func_button",
    "func_bobbing",
    "func_detail",
    "func_detail_wall",
    "func_detail_fence",
    "func_group",
    "func_detail_illusionary",
    "light",
    "info_player_start",
    "info_player_deathmatch",
    "info_intermission",
    "path_corner",
    "trigger_once",
    "trigger_multiple",
    "trigger_hurt",
    "trigger_push",
    "trigger_counter",
    "trigger_relay",
    "trigger_teleport",
    "trigger_changelevel",
    "item_health",
    "item_key1",
    "item_key2",
    "weapon_shotgun",
    "weapon_supershotgun",
    "weapon_machinegun",
    "weapon_chaingun",
    "weapon_grenadelauncher",
    "weapon_rocketlauncher",
    "weapon_hyperblaster",
    "weapon_railgun",
    "weapon_bfg",
    "monster_infantry",
    "monster_soldier",
    "monster_gunner",
    "monster_tank",
    "monster_medic",
    "monster_flyer",
    "monster_gladiator",
    "monster_berserker",
    "monster_parasite",
    "monster_boss",
    "monster_berserk",
    # Phase 2: Q2-native / safe as-is on input (no swap row)
    "func_plat",
    "func_train",
    "info_notnull",
    "misc_explobox",
    "misc_model",
    "target_explosion",
    "target_speaker",
    "target_changelevel",
    "target_spawner",
    "target_string",
    "item_pack",
    "misc_explobox2",
    "func_explobox",
    # Post-swap targets from Phase 1 (also valid in output / Q2)
    "item_quad",
    "item_invisibility",
    "item_invulnerability",
    "item_envirosuit",
    "item_armor_jacket",
    "item_armor_combat",
    "item_armor_body",
    "item_ammo_bullets",
    "item_ammo_cells",
    "item_ammo_rockets",
    "item_ammo_shells",
    "info_teleport_destination",
}

# Whole line: optional indent, "classname", whitespace, quoted value, rest (e.g. trailing spaces).
CLASSNAME_LINE_RE = re.compile(r'^(\s*"classname"\s+)("[^"]*")(.*)$')


def collect_classnames_from_lines(lines: Iterable[str]) -> List[str]:
    """
    Extract classname values from lines that match the standard ``"classname"`` pair pattern.

    Returns one entry per matching line (order preserved).
    """
    found: List[str] = []
    for line in lines:
        m = CLASSNAME_LINE_RE.match(line)
        if not m:
            continue
        inner = m.group(2)[1:-1]
        found.append(inner)
    return found


def transform_classname_line(
    line: str,
    phase1_counter: Counter,
    phase2_counter: Counter,
) -> Tuple[str, bool]:
    """
    If ``line`` is a classname key line, apply Phase 1 then Phase 2 swap rules.

    Phase 1 takes precedence if a classname existed in both (not expected).
    Teleport destinations are left unchanged; caller counts them separately.

    Returns ``(new_line, changed)``.
    """
    m = CLASSNAME_LINE_RE.match(line)
    if not m:
        return line, False
    prefix, quoted, tail = m.group(1), m.group(2), m.group(3)
    inner = quoted[1:-1]

    if inner == TELEPORT_DEST_CLASSNAME:
        return line, False

    if inner in PHASE1_ENTITY_SWAP:
        new_inner = PHASE1_ENTITY_SWAP[inner]
        phase1_counter[(inner, new_inner)] += 1
    elif inner in PHASE2_ENTITY_SWAP:
        new_inner = PHASE2_ENTITY_SWAP[inner]
        phase2_counter[(inner, new_inner)] += 1
    else:
        return line, False

    new_line = prefix + '"' + new_inner + '"' + tail
    return new_line, True


def convert_map_text(
    text: str,
) -> Tuple[str, Counter, Counter, int, int]:
    """
    Apply classname swaps to full map text (line-based).

    Returns ``(new_text, phase1_counts, phase2_counts, teleport_hits, lines_changed)``.
    """
    lines = text.splitlines(keepends=True)
    phase1_counter: Counter = Counter()
    phase2_counter: Counter = Counter()
    teleport_hits = 0
    lines_changed = 0
    out_chunks: List[str] = []

    for line in lines:
        m = CLASSNAME_LINE_RE.match(line.rstrip("\r\n"))
        if m:
            inner = m.group(2)[1:-1]
            if inner == TELEPORT_DEST_CLASSNAME:
                teleport_hits += 1
                out_chunks.append(line)
                continue

        newline = "\r\n" if line.endswith("\r\n") else ("\n" if line.endswith("\n") else "")
        core = line[:-len(newline)] if newline else line
        new_core, changed = transform_classname_line(
            core, phase1_counter, phase2_counter
        )
        if changed:
            lines_changed += 1
        out_chunks.append(new_core + newline)

    new_text = "".join(out_chunks)
    return new_text, phase1_counter, phase2_counter, teleport_hits, lines_changed


def list_unmapped_input_classnames(classnames: List[str]) -> List[str]:
    """
    Return sorted unique classnames that are not Phase 1/2 swap sources, not the
    teleport destination (handled separately), and not in ``Q2_NATIVE_CLASSNAMES``.
    """
    swap_sources = set(PHASE1_ENTITY_SWAP.keys()) | set(PHASE2_ENTITY_SWAP.keys())
    unmapped: Set[str] = set()
    for cn in classnames:
        if cn in swap_sources or cn == TELEPORT_DEST_CLASSNAME:
            continue
        if cn in Q2_NATIVE_CLASSNAMES:
            continue
        unmapped.add(cn)
    return sorted(unmapped)


def _format_phase_block(title: str, underline: str, counter: Counter, phase2: bool) -> List[str]:
    """Format one phase section of the swap report."""
    lines_out: List[str] = [title, underline]
    if not counter:
        lines_out.append("    (none)")
        return lines_out
    arrow = "->"
    for (src, dst), count in sorted(counter.items(), key=lambda x: (x[0][0], x[0][1])):
        flag_txt = ""
        if phase2 and src in PHASE2_SWAP_FLAGS:
            flag_txt = f"  [!] {PHASE2_SWAP_FLAGS[src]}"
        lines_out.append(
            f"    {src:<32} {arrow} {dst:<22} : {count}{flag_txt}"
        )
    return lines_out


def _flagged_checklist_lines(phase2_counter: Counter) -> List[str]:
    """Build FLAGGED section lines (consolidated checklist for flagged Phase 2 swaps)."""
    rows: List[str] = []
    for (src, dst), count in sorted(
        phase2_counter.items(), key=lambda x: (x[0][0], x[0][1])
    ):
        if src not in PHASE2_SWAP_FLAGS:
            continue
        msg = PHASE2_SWAP_FLAGS[src]
        rows.append(
            f"    {src} -> {dst} : {count}  [!] {msg}"
        )
    return rows


def count_flagged_swaps(phase2_counter: Counter) -> int:
    """Return total instance count of Phase 2 swaps that carry a follow-up flag."""
    total = 0
    for (src, _dst), c in phase2_counter.items():
        if src in PHASE2_SWAP_FLAGS:
            total += c
    return total


def format_swap_report(
    phase1_counter: Counter,
    phase2_counter: Counter,
    teleport_hits: int,
    unmapped: List[str],
    output_path: Optional[Path],
    dry_run: bool,
) -> str:
    """Build the human-readable multi-section swap report."""
    p1_total = sum(phase1_counter.values())
    p2_total = sum(phase2_counter.values())
    total_swaps = p1_total + p2_total
    flags_raised = count_flagged_swaps(phase2_counter)

    lines_out: List[str] = ["SWAP REPORT", ""]

    lines_out.extend(
        _format_phase_block(
            "PHASE 1 SWAPS (monsters / weapons / items)",
            "------------------------------------------",
            phase1_counter,
            phase2=False,
        )
    )
    lines_out.append("")
    lines_out.extend(
        _format_phase_block(
            "PHASE 2 SWAPS (geometry / triggers / audio / misc)",
            "---------------------------------------------------",
            phase2_counter,
            phase2=True,
        )
    )
    lines_out.append("")
    lines_out.append("FLAGGED (manual follow-up needed)")
    lines_out.append("----------------------------------")
    flagged = _flagged_checklist_lines(phase2_counter)
    if flagged:
        lines_out.extend(flagged)
    else:
        lines_out.append("    (none)")

    lines_out.append("")
    lines_out.append("TOTALS")
    lines_out.append("------")
    lines_out.append(f"    Phase 1 swaps     : {p1_total}")
    lines_out.append(f"    Phase 2 swaps     : {p2_total}")
    lines_out.append(f"    Total swaps       : {total_swaps}")
    lines_out.append(f"    Flags raised      : {flags_raised}")
    if teleport_hits:
        lines_out.append(
            f"    Teleport review   : {TELEPORT_DEST_CLASSNAME} kept {teleport_hits}x "
            "(check Q2 teleporter wiring)"
        )
    if unmapped:
        lines_out.append(
            "    Unmapped remaining: " + ", ".join(unmapped)
        )
        lines_out.append(
            "    WARNING - not covered by Phase 1/2 and not Q2-native (see list above)"
        )
    else:
        lines_out.append("    Unmapped remaining: (none)")

    if output_path is not None:
        label = "Would write         :" if dry_run else "Output            :"
        lines_out.append(f"    {label} {output_path}")

    review = bool(unmapped or teleport_hits or flags_raised)
    status = "REVIEW NEEDED" if review else "OK"
    if dry_run:
        status = "DRY-RUN"
    lines_out.append(f"    Status            : {status}")
    return "\n".join(lines_out)


def backup_if_exists(path: Path) -> None:
    """
    If ``path`` exists, copy it to ``path`` + ``.bak`` (overwrite old .bak).
    """
    if not path.is_file():
        return
    bak = path.with_name(path.name + ".bak")
    shutil.copy2(path, bak)


def validate_input_map(path: Path) -> None:
    """Raise ``SystemExit`` if path is missing or not a ``.map`` file."""
    if not path.exists():
        sys.exit(f"Error: input does not exist: {path}")
    if not path.is_file():
        sys.exit(f"Error: input is not a file: {path}")
    if path.suffix.lower() != ".map":
        sys.exit(f"Error: input must be a .map file: {path}")


def convert_file(
    input_path: Path,
    output_path: Path,
    *,
    dry_run: bool,
    write_report: bool,
) -> int:
    """
    Convert one map file. Returns process exit code (0 = success).

    Creates a ``.bak`` of the output if the output already exists and this is
    not a dry run. Never writes to ``input_path``.
    """
    validate_input_map(input_path)
    outp = output_path.resolve()
    inp = input_path.resolve()
    if outp == inp:
        sys.exit("Error: output path must differ from input (refusing to overwrite source).")

    text = input_path.read_text(encoding="utf-8", errors="replace")
    classnames_before = collect_classnames_from_lines(text.splitlines())
    new_text, phase1_c, phase2_c, teleport_hits, _ = convert_map_text(text)
    unmapped = list_unmapped_input_classnames(classnames_before)

    report_body = format_swap_report(
        phase1_c,
        phase2_c,
        teleport_hits,
        unmapped,
        output_path,
        dry_run,
    )
    print(report_body)

    if dry_run:
        return 0

    backup_if_exists(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(new_text, encoding="utf-8", newline="")

    if write_report:
        # Pair report with source map stem (e.g. foo.map -> foo_swap_report.txt, not foo_q2_...)
        report_path = input_path.with_name(input_path.stem + "_swap_report.txt")
        backup_if_exists(report_path)
        report_path.write_text(report_body + "\n", encoding="utf-8")

    return 0


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """Parse command-line arguments."""
    p = argparse.ArgumentParser(
        description=(
            "Convert Quake 1 .map entity classnames toward Quake 2 using Phase 1 and "
            "Phase 2 swap tables. Only 'classname' key lines are modified; brushes are untouched."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python q1_to_q2_converter.py mymap.map\n"
            "  python q1_to_q2_converter.py mymap.map out.map\n"
            "  python q1_to_q2_converter.py mymap.map --dry-run\n"
            "  python q1_to_q2_converter.py ./maps --batch --report\n"
        ),
    )
    p.add_argument(
        "input",
        nargs="?",
        help="Input .map file, or folder path when using --batch",
    )
    p.add_argument(
        "output",
        nargs="?",
        help="Optional output .map path (default: <name>_q2.map next to input)",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="Print swap report only; do not write output or backups",
    )
    p.add_argument(
        "--batch",
        action="store_true",
        help=(
            "Treat positional input as a folder: convert each *.map to <stem>_q2.map "
            "(skips names ending in _q2)"
        ),
    )
    p.add_argument(
        "--report",
        action="store_true",
        help="Also write <input_stem>_swap_report.txt beside the maps (e.g. foo_swap_report.txt)",
    )
    return p.parse_args(argv)


def run_batch(folder: Path, dry_run: bool, write_report: bool) -> int:
    """
    Convert all ``*.map`` files in ``folder``; skip outputs named ``*_q2.map``.
    """
    if not folder.is_dir():
        sys.exit(f"Error: --batch path is not a directory: {folder}")
    maps = sorted(folder.glob("*.map"))
    code = 0
    for mpath in maps:
        if mpath.stem.endswith("_q2"):
            continue
        out = mpath.with_name(mpath.stem + "_q2.map")
        print(f"\n=== {mpath.name} -> {out.name} ===\n")
        convert_file(mpath, out, dry_run=dry_run, write_report=write_report)
    if not maps:
        print("(no .map files in folder)")
    return code


def main(argv: Optional[List[str]] = None) -> int:
    """
    Parse arguments and run single-file conversion or batch folder conversion.

    Returns a process exit code (0 on success).
    """
    args = parse_args(argv)
    dry_run: bool = args.dry_run
    write_report: bool = args.report

    if args.batch:
        if not args.input:
            sys.exit("Error: provide folder path as first argument when using --batch")
        if args.output:
            sys.exit("Error: output path is not used with --batch (each file gets <stem>_q2.map)")
        return run_batch(Path(args.input), dry_run=dry_run, write_report=write_report)

    if not args.input:
        sys.exit("Error: missing input .map path (see --help)")

    inp = Path(args.input)
    if args.output:
        out = Path(args.output)
    else:
        out = inp.with_name(inp.stem + "_q2.map")

    return convert_file(inp, out, dry_run=dry_run, write_report=write_report)


if __name__ == "__main__":
    raise SystemExit(main())
