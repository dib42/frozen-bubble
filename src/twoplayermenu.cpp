#include "twoplayermenu.h"

#include <string>
#include <SDL2/SDL_image.h>

TwoPlayerMenu::TwoPlayerMenu(const SDL_Renderer *renderer)
{
    std::string background_path = std::string(DATA_DIR) + "/gfx/backgrnd.png";
    background = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), background_path.c_str());
}

TwoPlayerMenu::~TwoPlayerMenu()
{
    SDL_DestroyTexture(background);
}

void TwoPlayerMenu::Render(const SDL_Renderer *renderer) const
{
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), background, NULL, NULL);
}

MenuType TwoPlayerMenu::handle_key_down_event(const SDL_Event &e)
{
    switch(e.key.keysym.sym) {
        case SDLK_ESCAPE:
            return MAIN_MENU;
    }
    return TWO_PLAYER_MENU;
}
