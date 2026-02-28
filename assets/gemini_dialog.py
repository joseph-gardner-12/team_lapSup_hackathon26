import sys
import os
from google import genai
from dotenv import load_dotenv

load_dotenv()

CHARACTER_DATA = {
    "SHERIFF": {
        "name": "Sheriff Burbrick",
        "personality": "suspicious of outsiders, strict about the law but lazy."
    },
    "FARMER": {
        "name": "Stables Susan",
        "personality": "Really loves horses, obsessed with manure quality, talks to animals."
    },
    "OUTLAW": {
        "name": "Gunslinger Garry",
        "personality": "Like the arms dealer from Terraria but simpler. Obsessed with guns, slightly unhinged."
    },
    "BANDIT": {
        "name": "Buster the Bandit",
        "personality": "Overly flirtatious, but in a creepy old-timey way."
    },
    "GENTLEMAN": {
        "name": "Tommy Treasurer",
        "personality": "Mildly stereotypical banker, obsessed with gold coins and savings."
    },
    "BARKEEP": {
        "name": "Barry the Barkeep",
        "personality": "He knows what you did. Mysterious, wipes the counter constantly, speaks in riddles."
    },
    "CONDUCTOR": {
        "name": "Crash the Conductor",
        "personality": "Reckless, loves fast trains, always covered in soot."
    },
    "MINER": {
        "name": "Dynamite Dale",
        "personality": "Loves explosions, hard of hearing, constantly covered in dust."
    },
    "MUSICIAN": {
        "name": "Marley the Musician",
        "personality": "Laid back, plays the banjo, always humming a tune, thinks life is a song ."    },
    "KITTY": {
        "name": "Sheriff's Wife Kitty",
        "personality": "Strict, keeps the Sheriff in line, sharp-tongued but cares for the town."    }
}


def main():
    if len(sys.argv) < 3:
        print("Howdy! I seem to have lost my voice.")
        return

    npc_name = sys.argv[1]
    item_name = sys.argv[2]
    
    api_key = os.getenv("GEMINI_API_KEY")
    if not api_key:
        print("Error: GEMINI_API_KEY not found. Make sure your .env file is set up correctly.")
        return

    client = genai.Client(api_key=api_key)
    
    # Using the flash model for speed

    character_info = CHARACTER_DATA.get(npc_name.upper(), {"name": npc_name, "personality": "Unknown"})
    
    # We ask for a short response so it fits neatly in your Raylib UI box
    prompt = (f"You are a wild west character named {character_info['name']}. "
              f"Your personality is: {character_info['personality']}. "
              f"In one or two very short sentences, ask the player to find and bring you a {item_name}. "
              f"Speak in a heavy wild west dialect. Keep it under 25 words.")

    try:
        response = client.models.generate_content(
            model="gemini-2.5-flash",
            contents=prompt
        )
        print(response.text.strip().replace('\n', ' '))
    except Exception as e:
        print("Gosh darn it, my brain ain't working right now! (API Error)")
        print(str(e))

if __name__ == "__main__":
    main()
