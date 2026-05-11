#!/usr/bin/env python3
"""Read-only Quake .map analysis for Q2 porting heuristics."""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

# User-specified: no direct Q2 equivalent (replace in port)
Q1_ONLY_CLASSNAMES = frozenset(
    {
        "monster_dog",
        "monster_ogre",
        "monster_knight",
        "monster_shambler",
        "monster_wizard",
        "monster_shalrath",
        "monster_tarbaby",
        "monster_zombie",
        "item_artifact_super_damage",
        "item_artifact_invisibility",
        "item_artifact_invulnerability",
        "item_artifact_envirosuit",
        "weapon_nailgun",
        "weapon_supernailgun",
        "weapon_thunderbolt",
        "weapon_lightning",  # same weapon as thunderbolt (FGD naming)
        "trigger_secret",
        "info_teleport_destination",
    }
)

# Q1 classnames that commonly map to Q2 with entity swap / minor tweaks
Q2_CLOSE_EQUIV = frozenset(
    {
        "monster_grunt",
        "monster_soldier",
        "monster_army",
        "monster_enforcer",
        "monster_demon",
        "monster_fish",
        "monster_boss",
        "monster_oldone",
        "weapon_shotgun",
        "weapon_supershotgun",
        "weapon_grenadelauncher",
        "weapon_rocketlauncher",
        "item_health",
        "item_armor1",
        "item_armor2",
        "item_armor3",
        "item_shells",
        "item_spikes",
        "item_rockets",
        "item_cells",
        "item_key1",
        "item_key2",
        "ammo_shells",
        "ammo_nails",
        "ammo_rockets",
        "ammo_cells",
        "worldspawn",
        "info_player_start",
        "info_player_deathmatch",
        "info_intermission",
        "info_null",
        "func_door",
        "func_door_secret",
        "func_button",
        "func_plat",
        "func_train",
        "func_wall",
        "func_illusionary",
        "func_water",
        "func_conveyor",
        "func_areaportal",
        "func_episodegate",
        "func_bossgate",
        "trigger_multiple",
        "trigger_once",
        "trigger_relay",
        "trigger_push",
        "trigger_hurt",
        "trigger_teleport",
        "trigger_monsterjump",
        "trigger_onlyregistered",
        "trigger_changelevel",
        "trigger_setskill",
        "light",
        "light_fluoro",
        "light_torch2",
        "path_corner",
        "misc_teleporttrain",
        "misc_explobox",
        "misc_fireball",
        "target_temp_entity",
        "target_speaker",
        "target_explosion",
        "target_changelevel",
        "target_secret",
        "target_help",
        "target_goal",
        "target_kill",
        "target_splash",
        "target_blaster",
        "target_spawner",
        "target_angrylight",
        "target_lightramp",
        "misc_teleporttrain",
        "func_group",
        "func_detail",
        "func_detail_illusionary",
        "func_detail_wall",
        "func_illusionary",
        "func_particlefield",
    }
)

ENTITY_MARK = re.compile(r"^// entity (\d+)\s*$")
BRUSH_MARK = re.compile(r"^// brush (\d+)\s*$")
CLASSNAME_RE = re.compile(r'"classname"\s+"([^"]*)"')
# Valve 220: three point groups then texture name
FACE_TEX_RE = re.compile(
    r"^\(\s*[^)]+\)\s*\(\s*[^)]+\)\s*\(\s*[^)]+\)\s+(\S+)"
)
# First point of a face (for bbox)
FACE_PT_RE = re.compile(
    r"^\(\s*(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s+(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s+(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*\)"
)

TRIGGER_VOLUME = re.compile(
    r"^trigger_(multiple|once|push|hurt|teleport|counter|cdtrack)$"
)


def extract_entities(text: str) -> list[str]:
    """Return raw inner text of each // entity block (including nested brush braces)."""
    lines = text.splitlines()
    entities: list[str] = []
    i = 0
    n = len(lines)
    while i < n:
        m = ENTITY_MARK.match(lines[i].strip())
        if not m:
            i += 1
            continue
        i += 1
        while i < n and not lines[i].strip().startswith("{"):
            i += 1
        if i >= n:
            break
        start = i
        depth = 0
        while i < n:
            line = lines[i]
            depth, _ = _line_brace_update(line, depth)
            if depth == 0:
                entities.append("\n".join(lines[start : i + 1]))
                i += 1
                break
            i += 1
        else:
            j = start + 1
            while j < n and not ENTITY_MARK.match(lines[j].strip()):
                j += 1
            i = j
            continue
    return entities


def _line_brace_update(line: str, depth: int) -> tuple[int, bool]:
    """Apply brace deltas on `line` starting at `depth`; return (new_depth, unused)."""
    stripped = line.lstrip()
    # Valve face lines can include `{texture` tokens; braces there are not structural.
    if stripped.startswith("("):
        return depth, False
    in_str = False
    escape = False
    for ch in line:
        if in_str:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
            continue
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
    return depth, False


def parse_entity_metrics(entity_blocks: list[str]) -> dict:
    classnames: list[str] = []
    func_groups = 0
    trigger_volumes = 0
    for block in entity_blocks:
        cm = CLASSNAME_RE.search(block)
        if not cm:
            continue
        c = cm.group(1).strip()
        classnames.append(c)
        if c == "func_group":
            func_groups += 1
        if TRIGGER_VOLUME.match(c):
            trigger_volumes += 1
    return {
        "classnames": classnames,
        "func_groups": func_groups,
        "trigger_volumes": trigger_volumes,
    }


def analyze_map(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    brush_count = sum(1 for ln in lines if BRUSH_MARK.match(ln.strip()))
    if brush_count == 0:
        # Fallback: consecutive face-line groups with 3+ planes each
        face_lines = [ln for ln in lines if ln.lstrip().startswith("(")]
        groups = 0
        run = 0
        for ln in lines:
            if ln.lstrip().startswith("("):
                run += 1
            else:
                if run >= 3:
                    groups += 1
                run = 0
        if run >= 3:
            groups += 1
        brush_count = groups

    entity_blocks = extract_entities(text)
    entity_total = len(entity_blocks)

    em = parse_entity_metrics(entity_blocks)
    classnames = em["classnames"]
    if len(classnames) != entity_total:
        # Some malformed blocks without classname — still count blocks
        pass
    cn_counter = Counter(classnames)
    unique_classnames = sorted(set(classnames))

    q1_only_counts: dict[str, int] = {}
    q1_only_total = 0
    for name, cnt in cn_counter.items():
        if name in Q1_ONLY_CLASSNAMES:
            q1_only_counts[name] = cnt
            q1_only_total += cnt

    q2_equiv_total = 0
    for name, cnt in cn_counter.items():
        if name in Q2_CLOSE_EQUIV and name not in Q1_ONLY_CLASSNAMES:
            q2_equiv_total += cnt

    textures: set[str] = set()
    xs: list[float] = []
    ys: list[float] = []
    zs: list[float] = []

    for ln in lines:
        s = ln.strip()
        if not s.startswith("("):
            continue
        fm = FACE_TEX_RE.match(s)
        if fm:
            textures.add(fm.group(1))
        pm = FACE_PT_RE.match(s)
        if pm:
            try:
                xs.append(float(pm.group(1)))
                ys.append(float(pm.group(2)))
                zs.append(float(pm.group(3)))
            except ValueError:
                pass

    tex_sorted = sorted(textures)

    def tex_base(t: str) -> str:
        return t.lstrip("*{").lower()

    def is_sky(t: str) -> bool:
        b = tex_base(t)
        u = t.lstrip("*{").upper()
        return b.startswith("sky") or u.startswith("ENV")

    def is_liquid(t: str) -> bool:
        b = tex_base(t)
        return b.startswith("water") or b.startswith("slime") or b.startswith("lava")

    def is_animated(t: str) -> bool:
        return bool(re.match(r"^\+\d", t))

    sky_tex = sorted({t for t in textures if is_sky(t)})
    liq_tex = sorted({t for t in textures if is_liquid(t)})
    anim_tex = sorted({t for t in textures if is_animated(t)})

    if xs:
        bbox = (
            (min(xs), max(xs)),
            (min(ys), max(ys)),
            (min(zs), max(zs)),
        )
    else:
        bbox = ((0.0, 0.0), (0.0, 0.0), (0.0, 0.0))

    # Estimated areas: func_group + volume triggers; floor at 1
    est_areas = max(1, em["func_groups"] + em["trigger_volumes"])
    if est_areas == 1 and brush_count > 800:
        est_areas = max(est_areas, min(50, 1 + brush_count // 400))

    ut = len(textures)
    # Difficulty score 1–10
    b_score = 0
    if brush_count < 600:
        b_score = 0
    elif brush_count < 2000:
        b_score = 1
    elif brush_count < 4500:
        b_score = 2
    elif brush_count < 8000:
        b_score = 3
    elif brush_count < 12000:
        b_score = 4
    else:
        b_score = 5

    e_score = min(3, q1_only_total // 8)
    t_score = 0
    if ut < 40:
        t_score = 0
    elif ut < 90:
        t_score = 1
    elif ut < 160:
        t_score = 2
    else:
        t_score = 3

    a_score = min(2, max(0, (est_areas - 20) // 25))
    flags = (1 if sky_tex else 0) + (1 if liq_tex else 0) + (1 if anim_tex else 0)
    raw = b_score + e_score + t_score + a_score + flags
    score = int(max(1, min(10, round(1 + raw * 0.85))))

    reason_parts = []
    if q1_only_total:
        reason_parts.append(f"{q1_only_total} Q1-only entities")
    if brush_count > 8000:
        reason_parts.append("very high brush count")
    elif brush_count > 5000:
        reason_parts.append("high brush count")
    if ut > 120:
        reason_parts.append("many unique textures")
    if sky_tex:
        reason_parts.append("sky materials")
    if liq_tex or anim_tex:
        reason_parts.append("liquid/animated faces")
    if est_areas > 40:
        reason_parts.append("many grouped/volume areas")
    if not reason_parts:
        reason = "Compact geometry and mostly mappable entities/textures."
    else:
        reason = "Main challenge: " + "; ".join(reason_parts) + "."

    return {
        "path": path,
        "brushes": brush_count,
        "entities": entity_total,
        "unique_classnames": unique_classnames,
        "classname_counts": cn_counter,
        "q1_only_counts": q1_only_counts,
        "q1_only_total": q1_only_total,
        "q2_equiv_total": q2_equiv_total,
        "textures": tex_sorted,
        "texture_count": ut,
        "sky_tex": sky_tex,
        "liq_tex": liq_tex,
        "anim_tex": anim_tex,
        "est_areas": est_areas,
        "bbox": bbox,
        "score": score,
        "reason": reason,
        "func_groups": em["func_groups"],
        "trigger_volumes": em["trigger_volumes"],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Batch-analyze Quake 1 .map files in a folder (read-only).")
    parser.add_argument(
        "folder",
        nargs="?",
        default=".",
        help="Directory containing .map files (default: current working directory)",
    )
    args = parser.parse_args()
    root = Path(args.folder).resolve()
    if not root.is_dir():
        print(f"Error: not a directory: {root}", file=sys.stderr)
        sys.exit(1)
    maps = sorted(root.glob("*.map"))
    results = []
    for p in maps:
        if p.name.startswith("_"):
            continue
        try:
            results.append(analyze_map(p))
        except Exception as ex:
            results.append(
                {
                    "path": p,
                    "error": str(ex),
                }
            )

    ranked = sorted(
        [r for r in results if "error" not in r],
        key=lambda r: (r["score"], r["brushes"], r["texture_count"]),
    )

    for r in ranked:
        print(format_report(r))
        print()

    print("## RANKED SUMMARY (easiest to hardest)\n")
    print(
        "| Rank | File | Brushes | Entities | Q1-Only | Textures | Score |\n"
        "|------|------|---------|----------|---------|----------|-------|"
    )
    for i, r in enumerate(ranked, 1):
        print(
            f"| {i} | {r['path'].name} | {r['brushes']} | {r['entities']} | "
            f"{r['q1_only_total']} | {r['texture_count']} | {r['score']}/10 |"
        )

    easiest = ranked[:3]
    print("\n### Top 3 easiest porting candidates\n")
    for r in easiest:
        print(
            f"- **{r['path'].name}** ({r['score']}/10): {r['brushes']} brushes, "
            f"{r['q1_only_total']} Q1-only ents, {r['texture_count']} textures -- "
            f"{r['reason']}"
        )


def format_report(r: dict) -> str:
    bbox = r["bbox"]
    bx = f"{bbox[0][0]:.0f}/{bbox[0][1]:.0f}"
    by = f"{bbox[1][0]:.0f}/{bbox[1][1]:.0f}"
    bz = f"{bbox[2][0]:.0f}/{bbox[2][1]:.0f}"

    q1_list = ", ".join(
        f"{k}: {r['q1_only_counts'][k]}" for k in sorted(r["q1_only_counts"])
    )
    if not q1_list:
        q1_list = "(none)"

    uc = ", ".join(r["unique_classnames"])
    liq_yes = "yes" if r["liq_tex"] else "no"
    liq_names = ", ".join(r["liq_tex"]) if r["liq_tex"] else "(none)"
    anim_yes = "yes" if r["anim_tex"] else "no"
    anim_names = ", ".join(r["anim_tex"]) if r["anim_tex"] else "(none)"
    sky_yes = "yes" if r["sky_tex"] else "no"
    sky_names = ", ".join(r["sky_tex"]) if r["sky_tex"] else "(none)"
    sep = " -- "
    tex_block = ", ".join(r["textures"])

    lines = [
        "---",
        f"FILE: {r['path'].name}",
        f"BRUSHES: {r['brushes']}",
        f"ENTITIES (total): {r['entities']}",
        f"UNIQUE CLASSNAMES: {uc}",
        f"Q2-CLOSE EQUIV ENTITIES (count): {r['q2_equiv_total']}",
        f"Q1-ONLY ENTITIES (need replacement): {q1_list}  [total: {r['q1_only_total']}]",
        f"TEXTURES (unique): {r['texture_count']}",
        f"FULL TEXTURE LIST: {tex_block}",
        f"LIQUID TEXTURES: {liq_yes}{sep}{liq_names}",
        f"ANIMATED TEXTURES: {anim_yes}{sep}{anim_names}",
        f"SKY TEXTURES: {sky_yes}{sep}{sky_names}",
        f"ESTIMATED AREAS/ROOMS: {r['est_areas']}",
        f"BOUNDING BOX: X({bx}) Y({by}) Z({bz})",
        f"DIFFICULTY SCORE: {r['score']}/10",
        f"REASON: {r['reason']}",
        "---",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    main()
