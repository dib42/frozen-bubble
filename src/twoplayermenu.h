#ifndef TWOPLAYERMENU_H
#define TWOPLAYERMENU_H

#include "IMenu.h"
#include <SDL2/SDL.h>

class TwoPlayerMenu : public IMenu
{
public:
    TwoPlayerMenu(const SDL_Renderer *renderer);
    ~TwoPlayerMenu();
    void Render(const SDL_Renderer *renderer) const;
    MenuType handle_key_down_event(const SDL_Event &e);
private:
    SDL_Texture *background;
};

#endif // TWOPLAYERMENU_H
