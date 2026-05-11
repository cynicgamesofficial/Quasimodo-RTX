# entities.py - Entity conversion logic

def convert_entities(text: str) -> tuple[str, str]:
    # Mock conversion: just return the input and a fake report
    converted = text.replace("monster_ogre", "monster_soldier")
    report = "Converted monster_ogre to monster_soldier.\nNo other changes detected."
    return converted, report
