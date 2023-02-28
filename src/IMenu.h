#ifndef IMENU_H
#define IMENU_H

#include <SDL2/SDL.h>

enum MenuType {
    MAIN_MENU,
    ONE_PLAYER_MENU,
    TWO_PLAYER_MENU,
    LAN_GAME_MENU,
    NET_GAME_MENU,
    EDITOR_MENU,
    GRAPHICS_MENU,
    KEY_MENU,
    HIGHSCORES_MENU

};

class IMenu
{
    public:
        virtual ~IMenu() {}
        virtual void Render(const SDL_Renderer *renderer) const = 0;
        virtual MenuType handle_key_down_event(const SDL_Event &e) = 0;
};

#endif // IMENU_H
