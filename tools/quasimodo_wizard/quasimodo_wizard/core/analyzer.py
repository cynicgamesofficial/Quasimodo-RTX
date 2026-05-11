# analyzer.py - Map analysis logic

def analyze_map(path: str) -> dict:
    # Mock analysis data for demonstration
    return {
        "brushes": 123,
        "entities": 45,
        "texture_count": 8,
        "q1_only_total": 2,
        "textures": ["brick", "metal", "sky1", "water", "lava", "door", "light", "grass"],
        "sky_tex": ["sky1"],
        "liq_tex": ["water", "lava"],
        "anim_tex": ["door"],
        "score": 85,
        "reason": "Map is mostly compatible. Some Q1-only entities detected."
    }
