#!/bin/sh

read -p "Gemini API Key: " gemini_api_key
read -p "ElevenLabs API Key: " elevenlabs_api_key

echo "Checking python dependencies"

if [ -x "$(command -v pacman)" ]; then
	sudo pacman -S python-dotenv python-google-api-core
else
	pip install python-dotenv genai

fi

mkdir build
cd build

touch .env
echo "GEMINI_API_KEY='${gemini_api_key}'" > .env
echo "ELEVENLABS_API_KEY='${elevenlabs_api_key}'" >> .env

cmake ..
cmake --build .
cp -r ../assets .

mv ./assets/gemini_dialog.py .

