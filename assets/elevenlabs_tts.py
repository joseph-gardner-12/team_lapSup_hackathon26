import json
import os
import sys
import urllib.error
import urllib.request
from dotenv import load_dotenv

load_dotenv()

VOICE_ID_BY_NPC = {
    "Sheriff Burbrick": "TxGEqnHWrfWFTfGW9XjX",  # Josh
    "Gunslinger Gary": "ErXwobaYiN019PkySvjV",  # Antoni
    "Dynamite Dale": "VR6AewLTigWG4xSOukaG",  # Arnold
    "Stable Susan": "MF3mGyEYCl7XYWbV9V6O",  # Elli
    "Kitty": "EXAVITQu4vr4xnSDxMaL",  # Bella
    "Buster the Bandit": "AZnzlk1XvdvUeBnXmlld",  # Domi
    "Tommy Treasurer": "pNInz6obpgDQGcFmaJgB",  # Adam
    "Barry the Barkeep": "onwK4e9ZLuTAKqWW03F9",  # Daniel
    "Marley the Musician": "XB0fDUnXU5powFXDhCwa",  # Charlotte
}

DEFAULT_VOICE_ID = "21m00Tcm4TlvDq8ikWAM"  # Rachel


def read_dialog_text(path: str) -> str:
    with open(path, "r", encoding="utf-8") as dialog_file:
        return dialog_file.read().strip()


def synthesize_speech(api_key: str, voice_id: str, text: str, output_path: str) -> None:
    url = f"https://api.elevenlabs.io/v1/text-to-speech/{voice_id}"
    payload = {
        "text": text,
        "model_id": "eleven_multilingual_v2",
        "voice_settings": {
            "stability": 0.4,
            "similarity_boost": 0.8,
            "style": 0.25,
            "use_speaker_boost": True,
        },
    }

    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url=url,
        data=body,
        headers={
            "xi-api-key": api_key,
            "Accept": "audio/mpeg",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    with urllib.request.urlopen(request, timeout=30) as response:
        audio_data = response.read()

    with open(output_path, "wb") as audio_file:
        audio_file.write(audio_data)


def main() -> int:
    if len(sys.argv) < 4:
        print("Usage: elevenlabs_tts.py <npc_name> <dialog_text_file> <output_audio_file>")
        return 1

    npc_name = sys.argv[1]
    dialog_text_path = sys.argv[2]
    output_audio_path = sys.argv[3]

    if not os.path.exists(dialog_text_path):
        print(f"ERROR: Dialog text file not found: {dialog_text_path}")
        return 1

    dialog_text = read_dialog_text(dialog_text_path)
    if not dialog_text:
        print("ERROR: Dialog text is empty.")
        return 1

    api_key = os.getenv("ELEVENLABS_API_KEY")
    if not api_key:
        print("ERROR: ELEVENLABS_API_KEY not set. Add it to your environment or .env file.")
        return 1

    voice_id = VOICE_ID_BY_NPC.get(npc_name, DEFAULT_VOICE_ID)

    try:
        synthesize_speech(api_key, voice_id, dialog_text, output_audio_path)
        print(f"Generated speech for '{npc_name}' using voice '{voice_id}'.")
        return 0
    except urllib.error.HTTPError as error:
        details = error.read().decode("utf-8", errors="ignore")
        print(f"ERROR: ElevenLabs API HTTP {error.code}: {details}")
    except Exception as error:
        print(f"ERROR: Failed to generate speech: {error}")

    return 1


if __name__ == "__main__":
    raise SystemExit(main())
