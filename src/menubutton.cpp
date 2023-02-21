#include "menubutton.h"
#include <SDL2/SDL_image.h>
#include <utility>

MenuButton::MenuButton(uint32_t x, uint32_t y, const std::string &name, const SDL_Renderer *renderer)
    : isActive(false)
{
    std::string icon_path = std::string(DATADIR) + "/gfx/menu/anims/" + name + "_0001.png";
    icon = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), icon_path.c_str());
    std::string backgroundActive_path = std::string(DATADIR) + "/gfx/menu/txt_" + name + "_over.png";
    backgroundActive= IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), backgroundActive_path.c_str());
    std::string background_path = std::string(DATADIR) + "/gfx/menu/txt_" + name + "_off.png";
    background= IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), background_path.c_str());
    rect.x = x;
    rect.y = y;
    rect.w = 202;
    rect.h = 46;
    icon_rect.x = 248;
    icon_rect.y = y + 8;
    icon_rect.w = 40;
    icon_rect.h = 30;
}

MenuButton::MenuButton(MenuButton &&src) noexcept
    : isActive(std::move(src.isActive)),
    icon(std::exchange(src.icon, nullptr)),
    icon_rect(std::move(src.icon_rect)),
    backgroundActive(std::exchange(src.backgroundActive, nullptr)),
    background(std::exchange(src.background, nullptr)),
    rect(std::move(src.rect))
{
}

MenuButton::~MenuButton()
{
    SDL_DestroyTexture(icon);
    SDL_DestroyTexture(background);
    SDL_DestroyTexture(backgroundActive);
}

void MenuButton::Render(const SDL_Renderer *renderer) const
{
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), isActive?backgroundActive:background, nullptr, &rect);
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), icon, nullptr, &icon_rect);
}

void MenuButton::Activate()
{
    isActive = true;
}

void MenuButton::Deactivate()
{
    isActive = false;
}
