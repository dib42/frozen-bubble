#include "oneplayermenu.h"

#include <string>
#include <SDL2/SDL_image.h>

OnePlayerMenu::OnePlayerMenu(const SDL_Renderer *renderer)
{
    std::string background_path = std::string(DATA_DIR) + "/gfx/back_one_player.png";
    background = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), background_path.c_str());

    // load all bubble images - outsource
    std::string preview_bubble_path = std::string(DATA_DIR) + "/gfx/balls/bubble-3.gif";
    preview_bubble = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), preview_bubble_path.c_str());
    preview_bubble_rect.h = 32;
    preview_bubble_rect.w = 32;
    preview_bubble_rect.x = 302;
    preview_bubble_rect.y = 440;
    // load levelset bubbles

    next_bubble_rect.h = 32;
    next_bubble_rect.w = 32;
    next_bubble_rect.x = 302;
    next_bubble_rect.y = 390;

    //shooter.png
    std::string shooter_path = std::string(DATA_DIR) + "/gfx/shooter.png";
    shooter = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), shooter_path.c_str());
    shooter_rect.h = 100;
    shooter_rect.w = 100;
    shooter_rect.x = 268;
    shooter_rect.y = 356;
    // compressor_ext.png
    // compressor.png
    //pinguins/anime-shooter_p1_00*

    canon_angle = 0.0;
}

OnePlayerMenu::~OnePlayerMenu() {
    SDL_DestroyTexture(background);
    SDL_DestroyTexture(preview_bubble);
    SDL_DestroyTexture(shooter);
}

void OnePlayerMenu::Render(const SDL_Renderer *renderer) const
{
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), background, NULL, NULL);
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), preview_bubble, NULL, &next_bubble_rect);
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), preview_bubble, NULL, &preview_bubble_rect);

    SDL_RenderCopyEx(const_cast<SDL_Renderer*>(renderer), shooter, NULL, &shooter_rect,
                     canon_angle, NULL, SDL_FLIP_NONE);
}

MenuType OnePlayerMenu::handle_key_down_event(const SDL_Event &e)
{
    switch(e.key.keysym.sym) {
        case SDLK_ESCAPE:
            return MAIN_MENU;
        case SDLK_LEFT:
            canon_angle -= 5.0;
            break;
        case SDLK_RIGHT:
            canon_angle += 5.0;
            break;
    }
    return ONE_PLAYER_MENU;
}
