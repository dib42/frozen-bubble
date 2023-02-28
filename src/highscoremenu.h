#ifndef HIGHSCOREMENU_H
#define HIGHSCOREMENU_H

#include "IMenu.h"
#include <SDL2/SDL.h>

class HighscoreMenu : public IMenu
{
public:
    HighscoreMenu(const SDL_Renderer *renderer);
    ~HighscoreMenu();
    void Render(const SDL_Renderer *renderer) const;
    MenuType handle_key_down_event(const SDL_Event &e);
private:
    SDL_Texture *background;
};

#endif // HIGHSCOREMENU_H
