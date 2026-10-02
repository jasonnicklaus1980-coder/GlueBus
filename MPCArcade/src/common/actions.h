// MPC Arcade: the actions a pad, button or Q-Link can perform, shared by padbridge and the library app.
// Keys are MAME's default keyboard bindings, so no MAME input configuration is needed.
#pragma once
#include <linux/input-event-codes.h>
#include <string.h>

enum ActGroup { G_NONE, G_DIR, G_BUTTON, G_SYSTEM, G_P2, G_KNOB, G_EXIT };

typedef struct
{
    const char* name;      // as written in profile files
    const char* label;     // short label for the pad grid
    const char* desc;      // longer description for pickers
    int group;
    int key, key2;         // key2: second key of a sequence (save / load state slot), 0 = none
    int mod;               // modifier held with `key` (e.g. KEY_LEFTSHIFT for save state)
    int turbo;             // 1 = repeats while held
} Action;

static const Action kActions[] = {
    { "NONE", "", "Nothing", G_NONE, 0, 0, 0, 0 },
    { "UP", "\xe2\x96\xb2", "Joystick up", G_DIR, KEY_UP, 0, 0, 0 },
    { "DOWN", "\xe2\x96\xbc", "Joystick down", G_DIR, KEY_DOWN, 0, 0, 0 },
    { "LEFT", "\xe2\x97\x80", "Joystick left", G_DIR, KEY_LEFT, 0, 0, 0 },
    { "RIGHT", "\xe2\x96\xb6", "Joystick right", G_DIR, KEY_RIGHT, 0, 0, 0 },
    { "B1", "B1", "Button 1 (fire / punch)", G_BUTTON, KEY_LEFTCTRL, 0, 0, 0 },
    { "B2", "B2", "Button 2", G_BUTTON, KEY_LEFTALT, 0, 0, 0 },
    { "B3", "B3", "Button 3", G_BUTTON, KEY_SPACE, 0, 0, 0 },
    { "B4", "B4", "Button 4 (kick)", G_BUTTON, KEY_LEFTSHIFT, 0, 0, 0 },
    { "B5", "B5", "Button 5", G_BUTTON, KEY_Z, 0, 0, 0 },
    { "B6", "B6", "Button 6", G_BUTTON, KEY_X, 0, 0, 0 },
    { "B1_TURBO", "B1 T", "Button 1, auto-fire", G_BUTTON, KEY_LEFTCTRL, 0, 0, 1 },
    { "B2_TURBO", "B2 T", "Button 2, auto-fire", G_BUTTON, KEY_LEFTALT, 0, 0, 1 },
    { "R_UP", "R\xe2\x96\xb2", "Right stick up (twin-stick)", G_DIR, KEY_I, 0, 0, 0 },
    { "R_DOWN", "R\xe2\x96\xbc", "Right stick down", G_DIR, KEY_K, 0, 0, 0 },
    { "R_LEFT", "R\xe2\x97\x80", "Right stick left", G_DIR, KEY_J, 0, 0, 0 },
    { "R_RIGHT", "R\xe2\x96\xb6", "Right stick right", G_DIR, KEY_L, 0, 0, 0 },
    { "COIN", "COIN", "Insert coin", G_SYSTEM, KEY_5, 0, 0, 0 },
    { "START", "START", "1 player start", G_SYSTEM, KEY_1, 0, 0, 0 },
    { "P2_UP", "2\xe2\x96\xb2", "Player 2 up", G_P2, KEY_R, 0, 0, 0 },
    { "P2_DOWN", "2\xe2\x96\xbc", "Player 2 down", G_P2, KEY_F, 0, 0, 0 },
    { "P2_LEFT", "2\xe2\x97\x80", "Player 2 left", G_P2, KEY_D, 0, 0, 0 },
    { "P2_RIGHT", "2\xe2\x96\xb6", "Player 2 right", G_P2, KEY_G, 0, 0, 0 },
    { "P2_B1", "2 B1", "Player 2 button 1", G_P2, KEY_A, 0, 0, 0 },
    { "P2_B2", "2 B2", "Player 2 button 2", G_P2, KEY_S, 0, 0, 0 },
    { "P2_B3", "2 B3", "Player 2 button 3", G_P2, KEY_Q, 0, 0, 0 },
    { "P2_B4", "2 B4", "Player 2 button 4", G_P2, KEY_W, 0, 0, 0 },
    { "P2_COIN", "2 COIN", "Player 2 coin", G_P2, KEY_6, 0, 0, 0 },
    { "P2_START", "2 START", "2 players start", G_P2, KEY_2, 0, 0, 0 },
    { "PAUSE", "PAUSE", "Pause / resume", G_SYSTEM, KEY_P, 0, 0, 0 },
    { "MENU", "MENU", "MAME menu (Tab) / game options", G_SYSTEM, KEY_TAB, 0, 0, 0 },
    { "SELECT", "OK", "Select (Enter)", G_SYSTEM, KEY_ENTER, 0, 0, 0 },
    { "SAVE", "SAVE", "Save state (slot 1)", G_SYSTEM, KEY_F7, KEY_1, KEY_LEFTSHIFT, 0 },
    { "LOAD", "LOAD", "Load state (slot 1)", G_SYSTEM, KEY_F7, KEY_1, 0, 0 },
    { "SNAP", "SNAP", "Screenshot", G_SYSTEM, KEY_F12, 0, 0, 0 },
    { "EXIT", "EXIT", "Exit game (hold)", G_EXIT, KEY_ESC, 0, 0, 0 },
    { "KNOB_X", "STEER", "Knob: steering / dial / paddle (mouse X)", G_KNOB, 0, 0, 0, 0 },
    { "KNOB_Y", "KNOB Y", "Knob: vertical axis (mouse Y)", G_KNOB, 0, 0, 0, 0 },
    { "SCROLL", "SCROLL", "Knob: up / down steps (menus)", G_KNOB, 0, 0, 0, 0 },
};
enum { NUM_ACTIONS = sizeof kActions / sizeof kActions[0] };

static inline int actionIndex (const char* name)
{
    for (int i = 0; i < NUM_ACTIONS; ++i) if (strcmp (kActions[i].name, name) == 0) return i;
    return -1;
}

// Built-in pad layouts. Pad numbers are the MPC's: pad 1 bottom-left, pad 4 bottom-right, pad 13 top-left.
// Each entry: { pad, action }. Q-Link 1 is listed as pad 101 (it is a knob).
typedef struct { const char* id; const char* name; const char* desc; const char* map[20][2]; } Template;
static const Template kTemplates[] = {
    { "classic", "Classic", "Pads 1-4 joystick, 5-10 buttons, top row coin / start / pause / exit",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD5", "B1" }, { "PAD6", "B2" },
        { "PAD7", "B3" }, { "PAD8", "B4" }, { "PAD9", "B5" }, { "PAD10", "B6" }, { "PAD11", "SAVE" }, { "PAD12", "LOAD" },
        { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" }, { "QLINK1", "KNOB_X" } } },
    { "arrows", "Arrow keys", "Inverted-T joystick on pads 1-3 + 6, buttons on the right",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "RIGHT" }, { "PAD6", "UP" }, { "PAD4", "B1" }, { "PAD8", "B2" },
        { "PAD12", "B3" }, { "PAD7", "B4" }, { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD15", "PAUSE" },
        { "PAD16", "EXIT" }, { "QLINK1", "KNOB_X" } } },
    { "fighting", "Fighting", "Punches on pads 5-7, kicks on 9-11 (Street Fighter layout)",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD5", "B1" }, { "PAD6", "B2" },
        { "PAD7", "B3" }, { "PAD9", "B4" }, { "PAD10", "B5" }, { "PAD11", "B6" }, { "PAD8", "SAVE" }, { "PAD12", "LOAD" },
        { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "beatemup", "Beat 'em up", "Attack, jump, special on pads 5-7",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD5", "B1" }, { "PAD6", "B2" },
        { "PAD7", "B3" }, { "PAD8", "B4" }, { "PAD11", "SAVE" }, { "PAD12", "LOAD" }, { "PAD13", "COIN" },
        { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "platform", "Platform", "Jump and fire on the two pads above the joystick",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD6", "B1" }, { "PAD7", "B2" },
        { "PAD5", "B3" }, { "PAD8", "B4" }, { "PAD11", "SAVE" }, { "PAD12", "LOAD" }, { "PAD13", "COIN" },
        { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "shmup", "Shoot 'em up", "Auto-fire on pad 5, normal fire 7, bomb 6",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD5", "B1_TURBO" },
        { "PAD6", "B2" }, { "PAD7", "B1" }, { "PAD8", "B3" }, { "PAD11", "SAVE" }, { "PAD12", "LOAD" },
        { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "racing", "Racing", "Q-Link 1 steers, pad 5 gas, pad 6 brake / shift",
      { { "QLINK1", "KNOB_X" }, { "PAD1", "LEFT" }, { "PAD4", "RIGHT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" },
        { "PAD5", "B1" }, { "PAD6", "B2" }, { "PAD7", "B3" }, { "PAD13", "COIN" }, { "PAD14", "START" },
        { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "twinstick", "Twin-stick", "Move on pads 1-4, shoot in 4 directions on pads 9-12",
      { { "PAD1", "LEFT" }, { "PAD2", "DOWN" }, { "PAD3", "UP" }, { "PAD4", "RIGHT" }, { "PAD9", "R_LEFT" },
        { "PAD10", "R_DOWN" }, { "PAD11", "R_UP" }, { "PAD12", "R_RIGHT" }, { "PAD5", "B1" }, { "PAD6", "B2" },
        { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD15", "PAUSE" }, { "PAD16", "EXIT" } } },
    { "twoplayer", "2 players", "Left half player 1, right half player 2 (exit: touch corner or a button)",
      { { "PAD1", "LEFT" }, { "PAD2", "RIGHT" }, { "PAD5", "DOWN" }, { "PAD6", "UP" }, { "PAD9", "B1" }, { "PAD10", "B2" },
        { "PAD13", "COIN" }, { "PAD14", "START" }, { "PAD3", "P2_LEFT" }, { "PAD4", "P2_RIGHT" }, { "PAD7", "P2_DOWN" },
        { "PAD8", "P2_UP" }, { "PAD11", "P2_B1" }, { "PAD12", "P2_B2" }, { "PAD15", "P2_COIN" }, { "PAD16", "P2_START" } } },
};
enum { NUM_TEMPLATES = sizeof kTemplates / sizeof kTemplates[0] };

// Hardware controls that can be learned besides the 16 pads (names shown in the learn wizard).
static const char* const kHwNames[] = {
    "Q-Link 1", "Q-Link 2", "Q-Link 3", "Q-Link 4", "Data wheel", "Wheel push", "Play", "Play Start", "Stop", "Rec",
    "Overdub", "Main", "Browse", "Menu", "Shift", "Erase", "Tap Tempo", "Undo", "Note Repeat", "Full Level", "16 Level",
    "Pad Bank A", "Pad Bank B", "Pad Bank C", "Pad Bank D", "+", "-", "Footswitch 1", "Footswitch 2", "Other 1", "Other 2",
};
enum { NUM_HW_NAMES = sizeof kHwNames / sizeof kHwNames[0] };
