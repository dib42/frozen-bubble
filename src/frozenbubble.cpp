#include "frozenbubble.h"
#include <iostream>
#include "highscoremenu.h"
#include "mainmenu.h"
#include "oneplayermenu.h"
#include "twoplayermenu.h"

FrozenBubble::FrozenBubble() : IsGameQuit(false)
{
}

FrozenBubble::~FrozenBubble() {
    if(renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }

    if(window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

uint8_t FrozenBubble::RunForEver()
{
    window = SDL_CreateWindow("Frozen-Bubble", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              640, 480, 0);

    if(!window) {
        IsGameQuit = true;
        std::cout << "Failed to create window: " << SDL_GetError() << std::endl;
    }

    std::string icon_path = std::string(DATA_DIR) + "/gfx/pinguins/window_icon_penguin.bmp";
    SDL_Surface *icon = SDL_LoadBMP(icon_path.c_str());
    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);

    renderer = SDL_CreateRenderer(window, -1, 0);

    if(!renderer) {
        IsGameQuit = true;
        std::cout << "Failed to create renderer: " << SDL_GetError() << std::endl;
    }

    MainMenu main_menu(renderer);
    OnePlayerMenu one_player_menu(renderer);
    TwoPlayerMenu two_player_menu(renderer);
    HighscoreMenu highscore_menu(renderer);

    MenuType current_menu_type = MAIN_MENU;

    IMenu* menus[9] = {
        &main_menu,
        &one_player_menu,
        &two_player_menu,
        &main_menu, // FIXME: lan game
        &main_menu, // FIXME: net game
        &main_menu, // FIXME: editor
        &main_menu, // FIXME: graphics
        &main_menu, // FIXME: keys
        &highscore_menu
    };

    while(!IsGameQuit) {
        SDL_Event e;
        while (SDL_PollEvent (&e)) {
            switch(e.type) {
            case SDL_WINDOWEVENT:
                switch (e.window.event) {
                    case SDL_WINDOWEVENT_CLOSE:
                    {
                        IsGameQuit = true;
                        break;
                    }
                }
                break;
            case SDL_KEYDOWN:
                if((e.key.keysym.sym == SDLK_ESCAPE) && (current_menu_type == MAIN_MENU)) {
                    IsGameQuit = true;
                    continue;
                }
                current_menu_type = menus[current_menu_type]->handle_key_down_event(e);
                break;
            }
        }
        SDL_RenderClear(renderer);
        menus[current_menu_type]->Render(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }
    return 0;
}
