#include "controller_labels.h"
#include "config.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static void check(bool ok, const char *name)
{
    if (!ok) { fprintf(stderr, "FAIL: %s (%s)\n", name, SDL_GetError()); exit(1); }
    checks++;
}

static SDL_GameController *virtual_pad(const char *type, bool by_label, int *index)
{
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = 6; d.nbuttons = 16;
    d.name = type;
    *index = SDL_JoystickAttachVirtualEx(&d);
    check(*index >= 0, "attach virtual controller");
    char guid[33], map[512];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(*index), guid, sizeof guid);
    /* Physical raw buttons 0/1/2/3 are south/east/west/north. */
    bool swap = !strcmp(type, "switchpro") && by_label;
    snprintf(map, sizeof map, "%s,Label test,a:b%d,b:b%d,x:b%d,y:b%d,back:b4,start:b6,"
             "leftx:a0,lefty:a1,lefttrigger:a4,righttrigger:a5,type:%s,",
             guid, swap ? 1 : 0, swap ? 0 : 1, swap ? 3 : 2, swap ? 2 : 3, type);
    check(SDL_GameControllerAddMapping(map) >= 0, "install explicit virtual mapping");
    SDL_GameController *pad = SDL_GameControllerOpen(*index);
    check(pad != NULL, "open virtual controller");
    return pad;
}

int main(int argc, char **argv)
{
    check(argc == 2, "usage: test_controller_labels SCRATCH_INI");
    check(SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0, "initialise SDL");
    check(pad_family_for_type(SDL_CONTROLLER_TYPE_XBOXONE) == PAD_FAMILY_XBOX, "detect Xbox");
    check(pad_family_for_type(SDL_CONTROLLER_TYPE_PS5) == PAD_FAMILY_PLAYSTATION, "detect PlayStation");
    check(pad_family_for_type(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) == PAD_FAMILY_SWITCH, "detect Switch");
    check(pad_family_for_type(SDL_CONTROLLER_TYPE_UNKNOWN) == PAD_FAMILY_XBOX, "unknown falls back to Xbox");
    const char *const xbox[] = {"A", "B", "X", "Y"};
    const char *const positions[] = {"South", "East", "West", "North"};
    for (int b = 0; b < 4; b++) {
        check(!strcmp(pad_button_name(NULL, PAD_FAMILY_AUTO, b), xbox[b]), "no controller uses Xbox-style names");
        check(!strcmp(pad_button_name(NULL, PAD_FAMILY_GENERIC, b), positions[b]), "positions remain an explicit option");
    }
    pad_prompt_t initial_prompt;
    pad_prompt_init(&initial_prompt);
    char initial_footer[96];
    pad_prompt_adjust_footer(&initial_prompt, PAD_FAMILY_AUTO, initial_footer, sizeof initial_footer);
    check(!strcmp(initial_footer, "LEFT/RIGHT CHANGE   ESC BACK"), "graphics keeps keyboard adjustment and back hints");
    check(!strcmp(pad_button_name(NULL, PAD_FAMILY_GENERIC, SDL_CONTROLLER_BUTTON_A), "South"), "unknown south");
    check(!strcmp(pad_button_name(NULL, PAD_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_A), "Cross"), "PS south");
    check(!strcmp(pad_button_name(NULL, PAD_FAMILY_PLAYSTATION, PAD_RT), "R2"), "PS trigger");
    check(!strcmp(pad_button_name(NULL, PAD_FAMILY_SWITCH, PAD_RT), "ZR"), "Switch trigger");
    check(!strcmp(pad_button_name(NULL, PAD_FAMILY_XBOX, PAD_NONE), "-"), "unbound button");

    for (int labels = 0; labels < 2; labels++) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, labels ? "1" : "0");
        int index;
        SDL_GameController *pad = virtual_pad("switchpro", labels != 0, &index);
        check(SDL_GameControllerGetType(pad) == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO, "mapping identifies Switch");
        const char *const expected[2][4] = {{"B", "A", "Y", "X"}, {"A", "B", "X", "Y"}};
        for (int b = 0; b < 4; b++)
            check(!strcmp(pad_button_name(pad, PAD_FAMILY_AUTO, b), expected[labels][b]), "Switch label matches SDL mode");
        check(!strcmp(pad_button_name(pad, PAD_FAMILY_GENERIC, SDL_CONTROLLER_BUTTON_A), labels ? "East" : "South"),
              "positional override normalises Nintendo letter-based input");
        check(!strcmp(pad_button_name(pad, PAD_FAMILY_PLAYSTATION, SDL_CONTROLLER_BUTTON_A), labels ? "Circle" : "Cross"),
              "manual family labels the physical position, not the SDL letter");
        SDL_Joystick *joy = SDL_GameControllerGetJoystick(pad);
        check(SDL_JoystickSetVirtualButton(joy, 0, 1) == 0, "press physical south");
        SDL_JoystickUpdate();
        int south = labels ? SDL_CONTROLLER_BUTTON_B : SDL_CONTROLLER_BUTTON_A;
        check(SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)south) != 0, "physical south activates mapped action");
        check(!strcmp(pad_button_name(pad, PAD_FAMILY_AUTO, south), "B"), "physical south is labelled B in both modes");
        check(SDL_JoystickSetVirtualButton(joy, 0, 0) == 0, "release physical south");
        SDL_JoystickUpdate();

        pad_prompt_t prompt;
        pad_prompt_init(&prompt);
        SDL_Event ev;
        memset(&ev, 0, sizeof ev);
        ev.type = SDL_CONTROLLERBUTTONDOWN;
        ev.cbutton.which = SDL_JoystickInstanceID(joy);
        ev.cbutton.button = SDL_CONTROLLER_BUTTON_A;
        pad_prompt_event(&prompt, &ev);
        char footer[96];
        pad_prompt_footer(&prompt, PAD_FAMILY_AUTO, "SELECT", "BACK", footer, sizeof footer);
        check(!strcmp(footer, labels ? "A SELECT   B BACK" : "B SELECT   A BACK"), "menu describes the actual select/back actions");
        pad_prompt_adjust_footer(&prompt, PAD_FAMILY_AUTO, footer, sizeof footer);
        check(!strcmp(footer, labels ? "LEFT/RIGHT CHANGE   B BACK" : "LEFT/RIGHT CHANGE   A BACK"),
              "graphics keeps the adjustment hint and physical Switch back button");
        ev.type = SDL_KEYDOWN;
        pad_prompt_event(&prompt, &ev);
        pad_prompt_footer(&prompt, PAD_FAMILY_AUTO, "SELECT", "BACK", footer, sizeof footer);
        check(!strcmp(footer, "ENTER SELECT   ESC BACK"), "keyboard switches prompts without rebinding");
        check(prompt.last_pad == SDL_JoystickInstanceID(joy), "binding menu retains last pad context");
        ev.type = SDL_CONTROLLERAXISMOTION;
        ev.caxis.which = SDL_JoystickInstanceID(joy);
        ev.caxis.axis = SDL_CONTROLLER_AXIS_LEFTX;
        ev.caxis.value = 50;
        pad_prompt_event(&prompt, &ev);
        check(!prompt.using_pad, "stick noise cannot steal keyboard prompts");
        ev.caxis.value = 22000;
        pad_prompt_event(&prompt, &ev);
        check(prompt.using_pad, "deliberate stick movement selects controller prompts");
        ev.type = SDL_CONTROLLERDEVICEREMOVED;
        ev.cdevice.which = SDL_JoystickInstanceID(joy);
        pad_prompt_event(&prompt, &ev);
        check(!prompt.using_pad && prompt.last_pad == -1, "disconnect drops stale controller context");
        SDL_GameControllerClose(pad);
        check(SDL_JoystickDetachVirtual(index) == 0, "detach virtual controller");
    }
    int index;
    SDL_GameController *pad = virtual_pad("xboxone", false, &index);
    check(pad_label_family(pad, PAD_FAMILY_AUTO) == PAD_FAMILY_XBOX, "auto family uses actual controller");
    check(!strcmp(pad_button_name(pad, PAD_FAMILY_SWITCH, SDL_CONTROLLER_BUTTON_A), "B"),
          "manual Switch on XInput keeps the physical south label");
    check(pad_label_family(pad, 999) == PAD_FAMILY_XBOX, "invalid override falls back to detection");
    check(!strcmp(pad_button_name(pad, PAD_FAMILY_AUTO, SDL_CONTROLLER_BUTTON_A), "A"), "Xbox south remains A");
    SDL_GameControllerClose(pad);
    check(SDL_JoystickDetachVirtual(index) == 0, "detach Xbox");

    pad = virtual_pad("unknown", false, &index);
    check(pad_label_family(pad, PAD_FAMILY_AUTO) == PAD_FAMILY_XBOX, "unidentified USB pad uses Xbox-style fallback");
    for (int b = 0; b < 4; b++) {
        check(!strcmp(pad_button_name(pad, PAD_FAMILY_AUTO, b), xbox[b]), "generic-pad action labels stay A/B/X/Y");
        check(!strcmp(pad_button_name(pad, PAD_FAMILY_GENERIC, b), positions[b]), "generic-pad positions can still be selected manually");
    }
    SDL_GameControllerClose(pad);
    check(SDL_JoystickDetachVirtual(index) == 0, "detach unidentified pad");

    game_select(&game_ki1);
    FILE *ini = fopen(argv[1], "w");
    check(ini != NULL, "open legacy config");
    fputs("[gamepad]\nhigh_quick = y\n", ini);
    check(fclose(ini) == 0, "write legacy config");
    config_load(argv[1]);
    check(cfg.pad_family[0] == PAD_FAMILY_AUTO && cfg.pad_family[1] == PAD_FAMILY_AUTO,
          "legacy configs default to automatic labels for both players");
    check(cfg.pad[BTN_HQ] == SDL_CONTROLLER_BUTTON_Y, "legacy action binding stays unchanged");
    cfg.pad_family[0] = PAD_FAMILY_SWITCH;
    cfg.pad_family[1] = PAD_FAMILY_PLAYSTATION;
    check(config_save(), "save independent label overrides");
    config_load(argv[1]);
    check(cfg.pad_family[0] == PAD_FAMILY_SWITCH && cfg.pad_family[1] == PAD_FAMILY_PLAYSTATION,
          "both players keep their own family override after reload");
    check(cfg.pad[BTN_HQ] == SDL_CONTROLLER_BUTTON_Y, "saving labels cannot rewrite the action binding");
    check(remove(argv[1]) == 0, "remove config fixture");
    SDL_Quit();
    printf("controller label tests: PASS (%d checks)\n", checks);
    return 0;
}
