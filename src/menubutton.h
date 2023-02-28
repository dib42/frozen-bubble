#ifndef MENUBUTTON_H
#define MENUBUTTON_H

#include "IMenu.h"
#include <SDL2/SDL.h>
#include <string>

class MenuButton final
{
public:
    MenuButton(uint32_t x, uint32_t y, const std::string &name, const SDL_Renderer *renderer,
               MenuType menu_type);
    MenuButton(const MenuButton&) = delete;
    MenuButton & operator= ( const MenuButton & ) = delete;
    MenuButton(MenuButton&& src) noexcept;
    MenuButton & operator= ( MenuButton && ) = delete;
    ~MenuButton();
    void Render(const SDL_Renderer *renderer) const;
    void Activate();
    void Deactivate();
    MenuType get_menu();
private:
    bool isActive;
    MenuType menu_type;
    SDL_Texture *icon;
    SDL_Rect icon_rect;
    SDL_Texture *backgroundActive;
    SDL_Texture *background;
    SDL_Rect rect;
};

#endif // MENUBUTTON_H