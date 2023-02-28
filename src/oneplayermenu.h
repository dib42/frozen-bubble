#ifndef ONEPLAYERMENU_H
#define ONEPLAYERMENU_H

#include "IMenu.h"
#include "bubble.h"
#include <SDL2/SDL.h>
#include <vector>

class OnePlayerMenu final : public IMenu
{
public:
    OnePlayerMenu(const SDL_Renderer *renderer);
    ~OnePlayerMenu();
    void Render(const SDL_Renderer *renderer) const;
    MenuType handle_key_down_event(const SDL_Event &e);
private:
    SDL_Texture *background;
    SDL_Texture *preview_bubble;
    SDL_Texture *shooter;
    SDL_Rect next_bubble_rect;
    SDL_Rect preview_bubble_rect;
    SDL_Rect shooter_rect;
    std::vector<Bubble> bubbles;
    double canon_angle;
};

#endif // ONEPLAYERMENU_H
