#ifndef MAINMENU_H
#define MAINMENU_H

#include <SDL2/SDL.h>
#include "menubutton.h"
#include "IMenu.h"
#include <vector>

class MainMenu final : public IMenu
{
public:
    MainMenu(const SDL_Renderer *renderer);
    MainMenu(const MainMenu&) = delete;
    ~MainMenu();
    void Render(const SDL_Renderer *renderer) const;
    MenuType handle_key_down_event(const SDL_Event &e);

private:
    void up();
    void down();
    std::vector<MenuButton> buttons;
    SDL_Texture *background;
    SDL_Texture *fb_logo;
    SDL_Rect fb_logo_rect;
    uint8_t active_button_index;
};

#endif // MAINMENU_H
