#include "highscoremenu.h"

#include <string>
#include <SDL2/SDL_image.h>

HighscoreMenu::HighscoreMenu(const SDL_Renderer *renderer)
{
    std::string background_path = std::string(DATA_DIR) + "/gfx/back_hiscores.png";
    background = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), background_path.c_str());
}

HighscoreMenu::~HighscoreMenu()
{
    SDL_DestroyTexture(background);
}

void HighscoreMenu::Render(const SDL_Renderer *renderer) const
{
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), background, NULL, NULL);
}

MenuType HighscoreMenu::handle_key_down_event(const SDL_Event &e)
{
    switch(e.key.keysym.sym) {
        case SDLK_ESCAPE:
            return MAIN_MENU;
    }
    return HIGHSCORES_MENU;
}
