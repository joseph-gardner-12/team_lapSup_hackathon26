// =============================================================================
//  main.c  –  Merged: scene-switching + collision (ours) & 9 NPCs (theirs)
// =============================================================================
#include "raylib.h"
#include "raymath.h"
#include "scene_manager.h"   // scene switching, currentScene, currentBackground
#include "character.h"
#include "npc.h"
#include "item.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
    #define PYTHON_CMD "python"
#else
    #define POPEN popen
    #define PCLOSE pclose
    #define PYTHON_CMD "python3"
#endif

#define SCREEN_WIDTH  (1024)
#define SCREEN_HEIGHT (576)
#define WINDOW_TITLE  "Lego Western Town - AI NPCs!"

// Full dimensions of bg.png / collision.png – used for map-wide random spawning.
#define MAP_WIDTH  (4096)
#define MAP_HEIGHT (2288)

// --- Global UI State ---
bool isDialogOpen = false;
char activeDialogText[1024] = ""; // Changed to a buffer so we can write Gemini's response to it
const char* activeNPCName = "";
bool isWaitingForAI = false;
NPC* interactingNPC = NULL;
static Sound dialogVoiceSound = { 0 };
static bool isDialogVoiceLoaded = false;

void WrapText(char *text, int maxLineWidth, int fontSize);

bool IsBlockedByCollisionMask(Vector2 worldPos, const Color *maskPixels, int maskWidth, int maskHeight) {
    int x = (int)worldPos.x;
    int y = (int)worldPos.y;

    if (x < 0 || y < 0 || x >= maskWidth || y >= maskHeight) {
        return true;
    }

    Color pixel = maskPixels[y * maskWidth + x];
    return (pixel.a > 0 && pixel.r < 20 && pixel.g < 20 && pixel.b < 20);
}

// Returns true only if the candidate point AND 8 compass/diagonal samples at `margin`
// pixels distance are all unblocked – guaranteeing the spawn is at least ~margin px
// from any wall.
static bool HasClearance(Vector2 pos, int margin,
                         const Color *maskPixels, int maskWidth, int maskHeight)
{
    // Centre point itself
    if (IsBlockedByCollisionMask(pos, maskPixels, maskWidth, maskHeight)) return false;

    // 8 directions at the requested distance
    static const int dx[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
    static const int dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
    for (int i = 0; i < 8; i++) {
        Vector2 probe = { pos.x + dx[i] * margin, pos.y + dy[i] * margin };
        if (IsBlockedByCollisionMask(probe, maskPixels, maskWidth, maskHeight)) return false;
    }
    return true;
}

// Returns a random position inside [xMin,xMax] x [yMin,yMax] that is at least
// SPAWN_CLEARANCE pixels away from any blocked pixel. Tries up to 2000 times then
// falls back to a deterministic scan to guarantee a result.
#define SPAWN_CLEARANCE 30

Vector2 FindValidSpawnPosition(int xMin, int xMax, int yMin, int yMax,
                               const Color *maskPixels, int maskWidth, int maskHeight)
{
    if (maskPixels == NULL) {
        return (Vector2){ (float)((xMin + xMax) / 2), (float)((yMin + yMax) / 2) };
    }

    for (int attempt = 0; attempt < 2000; attempt++) {
        Vector2 candidate = {
            (float)(xMin + rand() % (xMax - xMin + 1)),
            (float)(yMin + rand() % (yMax - yMin + 1))
        };
        if (HasClearance(candidate, SPAWN_CLEARANCE, maskPixels, maskWidth, maskHeight)) {
            return candidate;
        }
    }

    // Random attempts failed – deterministic scan, step by 10 px for speed.
    printf("WARNING: FindValidSpawnPosition random phase failed in [%d-%d, %d-%d], scanning...\n",
           xMin, xMax, yMin, yMax);
    for (int y = yMin; y <= yMax; y += 10) {
        for (int x = xMin; x <= xMax; x += 10) {
            Vector2 candidate = { (float)x, (float)y };
            if (HasClearance(candidate, SPAWN_CLEARANCE, maskPixels, maskWidth, maskHeight)) {
                return candidate;
            }
        }
    }

    // Entire rectangle has no clear spot – return centre as last resort.
    printf("WARNING: No clear spawn found in [%d-%d, %d-%d]!\n", xMin, xMax, yMin, yMax);
    return (Vector2){ (float)((xMin + xMax) / 2), (float)((yMin + yMax) / 2) };
}

// Spawn anywhere on the map that passes the clearance check.
// The collision mask is the only constraint – no hardcoded centre points.
// Requires collisionMaskPixels/collisionMaskImage to be in scope (used inside main).
#define SPAWN_MAP() \
    FindValidSpawnPosition(0, MAP_WIDTH - 1, 0, MAP_HEIGHT - 1, \
                           collisionMaskPixels, collisionMaskImage.width, collisionMaskImage.height)

// --- Python Hook Function ---
void GenerateGeminiDialog(const char* npcName, const char* itemName, char* buffer, size_t bufferSize) {
    char command[512];
    
    // Format the command dynamically based on the operating system
    snprintf(command, sizeof(command), "%s gemini_dialog.py \"%s\" \"%s\"", PYTHON_CMD, npcName, itemName);

    // Open the pipe using our cross-platform macro
    FILE *fp = POPEN(command, "r");
    if (fp == NULL) {
        snprintf(buffer, bufferSize, "Error: Could not run Python script.");
        printf("ERROR: Python script failed to execute.\n"); 
        return;
    }

    buffer[0] = '\0'; // Clear buffer
    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        strncat(buffer, line, bufferSize - strlen(buffer) - 1);
    }
    
    // Close the pipe using our cross-platform macro
    PCLOSE(fp);

    // Debug logs
    printf("\n--- GEMINI DIALOG GENERATED ---\n");
    printf("NPC: %s\n", npcName);
    printf("Text: %s\n", buffer);
    printf("-------------------------------\n\n");
}

static void SpeakDialogWithElevenLabs(const char* npcName, const char* dialogText) {
    if (npcName == NULL || dialogText == NULL || dialogText[0] == '\0') {
        return;
    }

    FILE *textFile = fopen("dialog_text.tmp", "w");
    if (textFile == NULL) {
        printf("ERROR: Could not create temporary dialogue file for TTS.\n");
        return;
    }
    fputs(dialogText, textFile);
    fclose(textFile);

    char command[1024];
    snprintf(command, sizeof(command), "%s assets/elevenlabs_tts.py \"%s\" \"dialog_text.tmp\" \"dialog_tts.mp3\"", PYTHON_CMD, npcName);

    FILE *fp = POPEN(command, "r");
    if (fp == NULL) {
        printf("ERROR: Could not execute ElevenLabs TTS script.\n");
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp) != NULL) {
        printf("[ElevenLabs] %s", line);
    }

    int status = PCLOSE(fp);
    if (status != 0 || !FileExists("dialog_tts.mp3")) {
        printf("WARNING: ElevenLabs TTS generation failed or produced no audio file.\n");
        return;
    }

    if (isDialogVoiceLoaded) {
        StopSound(dialogVoiceSound);
        UnloadSound(dialogVoiceSound);
        isDialogVoiceLoaded = false;
    }

    dialogVoiceSound = LoadSound("dialog_tts.mp3");
    if (dialogVoiceSound.frameCount > 0) {
        isDialogVoiceLoaded = true;
        PlaySound(dialogVoiceSound);
    } else {
        printf("WARNING: Generated dialog_tts.mp3 could not be loaded as a sound.\n");
    }
}

static void OpenDialogForNPC(const char* npcName, const char* dialogText, bool readAloud) {
    activeNPCName = npcName;
    snprintf(activeDialogText, sizeof(activeDialogText), "%s", dialogText);
    WrapText(activeDialogText, 560, 20);
    isDialogOpen = true;

    if (readAloud) {
        SpeakDialogWithElevenLabs(npcName, dialogText);
    }
}

// --- Text Wrapping Helper ---
// Modifies a string in-place, replacing spaces with newlines to fit a maximum pixel width.
void WrapText(char *text, int maxLineWidth, int fontSize) {
    int length = strlen(text);
    int lineStart = 0;
    int lastSpace = -1;
    char temp[1024];

    for (int i = 0; i < length; i++) {
        if (text[i] == ' ') lastSpace = i;
        if (text[i] == '\n') { // Reset if Gemini happened to generate a newline
            lineStart = i + 1;
            continue;
        }

        // Copy current line into a temporary buffer to measure it
        int currentLength = i - lineStart + 1;
        if (currentLength >= sizeof(temp)) currentLength = sizeof(temp) - 1; // Safegaurd
        
        strncpy(temp, text + lineStart, currentLength);
        temp[currentLength] = '\0';

        // Check if the current chunk of text exceeds our box width
        if (MeasureText(temp, fontSize) > maxLineWidth) {
            if (lastSpace > lineStart) {
                text[lastSpace] = '\n';     // Replace the last space with a newline
                lineStart = lastSpace + 1;  // Update the start of the new line
                i = lineStart - 1;          // Backtrack loop to measure properly from the new line
            } else {
                // Fallback: Force a break if a single word is somehow wider than the whole box
                text[i] = '\n';
                lineStart = i + 1;
            }
        }
    }
}

// Function to handle the interaction logic
void InteractWithNPC(NPC* npc, Character* player) {
    activeNPCName = npc->name;
    
    if (npc->questCompleted) {
        snprintf(activeDialogText, sizeof(activeDialogText), "Much obliged for your help earlier, partner!");
    } 
    else if (player->heldItem != NULL && strcmp(player->heldItem, npc->questItem) == 0) {
        // Player has the item!
        snprintf(activeDialogText, sizeof(activeDialogText), "Well I'll be! You found my %s. Thank ye kindly!", npc->questItem);
        npc->questCompleted = true;
        player->heldItem = NULL; // Consume the item
    } 
    else {
        // Generate dynamic quest dialog using Gemini
        GenerateGeminiDialog(npc->name, npc->questItem, activeDialogText, sizeof(activeDialogText));
    }
    
    OpenDialogForNPC(activeNPCName, activeDialogText, true);
}

// Collision mask data for one scene (pixels + dimensions).
typedef struct { Color *pixels; int width, height; } MaskData;

// Returns a random valid spawn position inside the given scene's coordinate space.
static Vector2 SpawnInScene(SceneType scene, MaskData masks[4]) {
    MaskData *m = &masks[scene];
    return FindValidSpawnPosition(0, m->width - 1, 0, m->height - 1,
                                  m->pixels, m->width, m->height);
}

int main(void)
{
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, WINDOW_TITLE);
    InitAudioDevice(); 
    Music bgMusic = LoadMusicStream("assets/background.mp3"); // Change this to your actual file path!
    PlayMusicStream(bgMusic);
    SetTargetFPS(60);
    srand(time(NULL)); // Seed random number generator

    // Scene manager owns the background texture from here on
    InitScenes();
    Image collisionMaskImage = LoadImage("assets/collision.png");
    Color *collisionMaskPixels = NULL;

    if (collisionMaskImage.data != NULL) {
        collisionMaskPixels = LoadImageColors(collisionMaskImage);
        printf("INFO: Collision mask loaded: %dx%d\n", collisionMaskImage.width, collisionMaskImage.height);
    } else {
        printf("WARNING: Could not load assets/collision.png. Movement will ignore collision mask.\n");
    }

    // Load sub-scene collision masks
    Image barMaskImg     = LoadImage("assets/Scenes/the_bar_mask.png");
    Image stablesMaskImg = LoadImage("assets/Scenes/stables_mask.png");
    Image rangeMaskImg   = LoadImage("assets/Scenes/practice_rage_mask.png");

    // Pack all four masks into one array indexed by SceneType
    MaskData sceneMasks[4] = {
        [SCENE_MAIN_TOWN]      = { collisionMaskPixels,
                                   collisionMaskImage.width, collisionMaskImage.height },
        [SCENE_SALOON]         = { barMaskImg.data     ? LoadImageColors(barMaskImg)     : NULL,
                                   barMaskImg.width,     barMaskImg.height },
        [SCENE_STABLES]        = { stablesMaskImg.data ? LoadImageColors(stablesMaskImg) : NULL,
                                   stablesMaskImg.width, stablesMaskImg.height },
        [SCENE_SHOOTING_RANGE] = { rangeMaskImg.data   ? LoadImageColors(rangeMaskImg)   : NULL,
                                   rangeMaskImg.width,   rangeMaskImg.height },
    };

    // Player always starts in the main town on a valid open pixel
    Vector2 playerStart = SpawnInScene(SCENE_MAIN_TOWN, sceneMasks);
    Character player;
    InitCharacter(&player, playerStart, "assets/character.png");

    // --- NPC Initialization ---
    NPC sheriff, garry, dale, susan, kitty, buster, tommy, barry, marley;

    // NPCs are randomly scattered across ALL scenes. Each gets a random SceneType,
    // then FindValidSpawnPosition picks a clear pixel inside that scene's mask.
    #define INIT_NPC_RANDOM(npcPtr, nameStr, texPath) do {                        \
        (npcPtr)->scene = (SceneType)(rand() % 4);                                \
        InitNPC((npcPtr), SpawnInScene((npcPtr)->scene, sceneMasks), nameStr, texPath); \
    } while (0)

    INIT_NPC_RANDOM(&sheriff, "Sheriff Burbrick",   "assets/sheriff.png");
    sheriff.questItem = "Lost Badge";

    INIT_NPC_RANDOM(&garry,  "Gunslinger Gary",     "assets/gary.png");
    garry.questItem = "Lucky Horseshoe";

    INIT_NPC_RANDOM(&dale,   "Dynamite Dale",       "assets/dale.png");
    dale.questItem = "TNT Plunger";

    INIT_NPC_RANDOM(&susan,  "Stable Susan",        "assets/susan.png");
    susan.questItem = "Golden Saddle";

    INIT_NPC_RANDOM(&kitty,  "Kitty",               "assets/kitty.png");
    kitty.questItem = "Feather Boa";

    INIT_NPC_RANDOM(&buster, "Buster the Bandit",   "assets/buster.png");
    buster.questItem = "Stolen Loot";

    INIT_NPC_RANDOM(&tommy,  "Tommy Treasurer",     "assets/tommy.png");
    tommy.questItem = "Ledger";

    INIT_NPC_RANDOM(&barry,  "Barry the Barkeep",   "assets/barry.png");
    barry.questItem = "Special Whiskey";

    INIT_NPC_RANDOM(&marley, "Marley the Musician",  "assets/marley.png");
    marley.questItem = "Tuning Fork";

    // --- Initialize Items (9 total, one per NPC) ---
    // Each item is randomly assigned to a scene and spawned on a valid open pixel in it.
    #define INIT_ITEM(idx, itemName) do {                                         \
        SceneType _sc = (SceneType)(rand() % 4);                                  \
        items[idx] = (Item){ SpawnInScene(_sc, sceneMasks), itemName, true, _sc };\
    } while (0)

    Item items[9];
    INIT_ITEM(0, "Lost Badge");
    INIT_ITEM(1, "Lucky Horseshoe");
    INIT_ITEM(2, "TNT Plunger");
    INIT_ITEM(3, "Golden Saddle");
    INIT_ITEM(4, "Feather Boa");
    INIT_ITEM(5, "Stolen Loot");
    INIT_ITEM(6, "Ledger");
    INIT_ITEM(7, "Special Whiskey");
    INIT_ITEM(8, "Tuning Fork");

    Camera2D camera = { 0 };
    camera.zoom = 1.0f; 
    camera.offset = (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f };

    while (!WindowShouldClose())
    {
	UpdateMusicStream(bgMusic);
        Vector2 mouseScreenPos = GetMousePosition();
        Vector2 mouseWorldPos = GetScreenToWorld2D(mouseScreenPos, camera);

        // --- Input Logic ---
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (isDialogOpen) {
                Rectangle okBtn = { SCREEN_WIDTH / 2.0f - 50, SCREEN_HEIGHT / 2.0f + 60, 100, 40 };
                if (CheckCollisionPointRec(mouseScreenPos, okBtn)) {
                    StopDialogVoicePlayback();
                    isDialogOpen = false;
                }
            } else {
                // Only interact with NPCs present in the current scene
                NPC* clickedNPC = NULL;
                NPC *allNPCs[9] = { &sheriff, &garry, &dale, &susan, &kitty,
                                    &buster,  &tommy, &barry, &marley };
                for (int i = 0; i < 9 && clickedNPC == NULL; i++) {
                    if (allNPCs[i]->scene == currentScene &&
                        IsNPCClicked(allNPCs[i], mouseWorldPos))
                        clickedNPC = allNPCs[i];
                }

                if (clickedNPC != NULL) {
                    activeNPCName = clickedNPC->name;
                    isDialogOpen  = true;

                    if (clickedNPC->questCompleted) {
                        OpenDialogForNPC(clickedNPC->name,
                                         "Much obliged for your help earlier, partner!",
                                         true);
                    } else if (player.heldItem != NULL &&
                               strcmp(player.heldItem, clickedNPC->questItem) == 0) {
                        char foundItemDialog[256];
                        snprintf(foundItemDialog, sizeof(foundItemDialog),
                                 "Well I'll be! You found my %s. Thank ye kindly!", clickedNPC->questItem);
                        OpenDialogForNPC(clickedNPC->name, foundItemDialog, true);
                        clickedNPC->questCompleted = true;
                        player.heldItem = NULL;
                    } else {
                        snprintf(activeDialogText, sizeof(activeDialogText), "Hmm...");
                        isWaitingForAI = true;
                        interactingNPC = clickedNPC;
                    }
                } else {
                    // Move player – use the collision mask for whatever scene we're in
                    MaskData *curMask = &sceneMasks[currentScene];
                    bool canMove = (curMask->pixels == NULL) ||
                        !IsBlockedByCollisionMask(mouseWorldPos, curMask->pixels,
                                                  curMask->width, curMask->height);
                    if (canMove) {
                        player.targetPosition = mouseWorldPos;
                    }
                }
            }
        }

        // --- Update Logic ---
        if (!isDialogOpen) {
            UpdateCharacter(&player);

            // Check for a scene transition every frame
            CheckForSceneSwitch(player.position, &player);

            // Item pickup works in any scene – filter by current scene
            for (int i = 0; i < 9; i++) {
                if (items[i].scene == currentScene && items[i].active &&
                    Vector2Distance(player.position, items[i].position) < 20.0f) {
                    items[i].active = false;
                    player.heldItem = items[i].name;
                }
            }
        }

        // Camera Logic
        camera.target = player.position;

        // --- Drawing ---
        BeginDrawing();
        ClearBackground(DARKGRAY);

        BeginMode2D(camera);

        if (currentBackground.id != 0) {
            DrawTextureEx(currentBackground, (Vector2){0,0}, 0.0f, 1.0f, WHITE);
        }

        // Draw items and NPCs that belong to the current scene
        NPC *allNPCsDraw[9] = { &sheriff, &garry, &dale, &susan, &kitty,
                                &buster,  &tommy, &barry, &marley };
        for (int i = 0; i < 9; i++) {
            if (items[i].active && items[i].scene == currentScene) {
                DrawRectangle((int)items[i].position.x - 5, (int)items[i].position.y - 5, 10, 10, GOLD);
                DrawText(items[i].name, (int)items[i].position.x - 10, (int)items[i].position.y - 15, 10, RAYWHITE);
            }
            if (allNPCsDraw[i]->scene == currentScene) DrawNPC(allNPCsDraw[i]);
        }

        // Teleport zone markers (main town) or exit marker (sub-scenes)
        if (currentScene == SCENE_MAIN_TOWN) {
            DrawRectangleLinesEx((Rectangle){ 2383, 1812, 80, 60 }, 2, RED);
            DrawText("[Saloon]",  2386, 1820, 10, RED);
            DrawRectangleLinesEx((Rectangle){ 2903, 1208, 80, 60 }, 2, BLUE);
            DrawText("[Stables]", 2906, 1216, 10, BLUE);
            DrawRectangleLinesEx((Rectangle){ 3499, 1502, 80, 60 }, 2, GREEN);
            DrawText("[Range]",   3502, 1510, 10, GREEN);
        } else {
            Rectangle exitRect = { 0 };
            if      (currentScene == SCENE_SALOON)         exitRect = (Rectangle){  71, 523, 80, 60 };
            else if (currentScene == SCENE_STABLES)        exitRect = (Rectangle){ 194, 647, 80, 60 };
            else if (currentScene == SCENE_SHOOTING_RANGE) exitRect = (Rectangle){  65, 554, 80, 60 };
            DrawRectangleLinesEx(exitRect, 2, ORANGE);
            DrawText("[Exit]", (int)exitRect.x + 5, (int)exitRect.y + 22, 10, ORANGE);
        }

        DrawCharacter(&player);

        EndMode2D();

        // -- SCREEN SPACE DRAWING (UI) --
        if (isDialogOpen) {
            DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.6f));

            Rectangle dialogRec = { SCREEN_WIDTH / 2.0f - 300, SCREEN_HEIGHT / 2.0f - 100, 600, 250 };
            DrawRectangleRec(dialogRec, RAYWHITE);
            DrawRectangleLinesEx(dialogRec, 4, DARKGRAY);

            // NPC Name Header
            DrawText(activeNPCName, (int)dialogRec.x + 20, (int)dialogRec.y + 15, 24, DARKBLUE);
            DrawLine((int)dialogRec.x + 20, (int)dialogRec.y + 45, (int)dialogRec.x + 580, (int)dialogRec.y + 45, GRAY);

            // Render the AI Dialog (Using a slightly smaller font so it fits)
            DrawText(activeDialogText, (int)dialogRec.x + 20, (int)dialogRec.y + 60, 20, BLACK);

            Rectangle okBtn = { SCREEN_WIDTH / 2.0f - 50, SCREEN_HEIGHT / 2.0f + 60, 100, 40 };
            bool isHovering = CheckCollisionPointRec(mouseScreenPos, okBtn);
            DrawRectangleRec(okBtn, isHovering ? LIGHTGRAY : GRAY);
            DrawRectangleLinesEx(okBtn, 2, BLACK);
            DrawText("Okay!", (int)okBtn.x + 25, (int)okBtn.y + 10, 20, BLACK);
        }

        // Top-Left UI – scene name + held item, visible everywhere since NPCs/items span all scenes
        const char *uiSceneName = (currentScene == SCENE_MAIN_TOWN)      ? "Main Town"      :
                                  (currentScene == SCENE_SALOON)         ? "The Saloon"     :
                                  (currentScene == SCENE_STABLES)        ? "Stables"        :
                                  (currentScene == SCENE_SHOOTING_RANGE) ? "Practice Range" : "?";
        DrawRectangle(10, 10, 350, 55, Fade(BLACK, 0.7f));
        DrawText(TextFormat("Location: %s  |  Click NPC to talk", uiSceneName), 20, 15, 13, RAYWHITE);
        if (player.heldItem) {
            DrawText(TextFormat("Holding: %s", player.heldItem), 20, 36, 16, GOLD);
        } else {
            DrawText("Holding: Nothing", 20, 36, 16, LIGHTGRAY);
        }

        // Mouse world-coordinate tracker (bottom-right corner)
        DrawRectangle(SCREEN_WIDTH - 220, SCREEN_HEIGHT - 34, 210, 24, Fade(BLACK, 0.7f));
        DrawText(TextFormat("World: %.0f, %.0f", mouseWorldPos.x, mouseWorldPos.y),
                 SCREEN_WIDTH - 215, SCREEN_HEIGHT - 29, 16, LIME);

        EndDrawing();

        // AI call happens after drawing so the "Hmm..." frame renders first
        if (isWaitingForAI && interactingNPC != NULL) {
            GenerateGeminiDialog(interactingNPC->name, interactingNPC->questItem,
                                 activeDialogText, sizeof(activeDialogText));
            OpenDialogForNPC(interactingNPC->name, activeDialogText, true);
            isWaitingForAI = false;
            interactingNPC = NULL;
        }
    }

    UnloadNPC(&sheriff);
    UnloadNPC(&garry);
    UnloadNPC(&dale);
    UnloadNPC(&susan);
    UnloadNPC(&kitty);
    UnloadNPC(&buster);
    UnloadNPC(&tommy);
    UnloadNPC(&barry);
    UnloadNPC(&marley);
    UnloadCharacter(&player);
    UnloadCurrentSceneTextures(); // scene manager owns the background
    if (collisionMaskPixels != NULL) {
        UnloadImageColors(collisionMaskPixels);
    }
    if (collisionMaskImage.data != NULL) {
        UnloadImage(collisionMaskImage);
    }
    UnloadMusicStream(bgMusic);
    if (isDialogVoiceLoaded) {
        StopSound(dialogVoiceSound);
        UnloadSound(dialogVoiceSound);
    }
    CloseAudioDevice();
    // Free main-town mask
    if (collisionMaskPixels != NULL) UnloadImageColors(collisionMaskPixels);
    if (collisionMaskImage.data != NULL) UnloadImage(collisionMaskImage);
    // Free sub-scene masks
    if (sceneMasks[SCENE_SALOON].pixels)         UnloadImageColors(sceneMasks[SCENE_SALOON].pixels);
    if (sceneMasks[SCENE_STABLES].pixels)        UnloadImageColors(sceneMasks[SCENE_STABLES].pixels);
    if (sceneMasks[SCENE_SHOOTING_RANGE].pixels) UnloadImageColors(sceneMasks[SCENE_SHOOTING_RANGE].pixels);
    if (barMaskImg.data)     UnloadImage(barMaskImg);
    if (stablesMaskImg.data) UnloadImage(stablesMaskImg);
    if (rangeMaskImg.data)   UnloadImage(rangeMaskImg);
    CloseWindow();

    return 0;
}
