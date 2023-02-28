#include "mainmenu.h"
#include <SDL2/SDL_image.h>

MainMenu::MainMenu(const SDL_Renderer *renderer)
    : active_button_index(0)
{
    std::string texts[] = {"1pgame", "2pgame", "langame", "netgame", "editor", "graphics", "keys", "highscores"};
    uint32_t y_start = 14;
    uint8_t i = 1;
    for(const std::string &text : texts) {
        buttons.push_back(MenuButton(89, y_start, text, renderer, MenuType(i)));
        y_start += 56;
        i++;
    }

    std::string background_path = std::string(DATA_DIR) + "/gfx/menu/back_start.png";
    background = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), background_path.c_str());
    std::string fb_logo_path = std::string(DATA_DIR) + "/gfx/menu/fblogo.png";
    fb_logo = IMG_LoadTexture(const_cast<SDL_Renderer*>(renderer), fb_logo_path.c_str());
    fb_logo_rect.x = 400;
    fb_logo_rect.y = 15;
    fb_logo_rect.w = 190;
    fb_logo_rect.h = 119;
    buttons[active_button_index].Activate();
}

MainMenu::~MainMenu() {
    SDL_DestroyTexture(background);
    SDL_DestroyTexture(fb_logo);
    buttons.clear();
}

void MainMenu::Render(const SDL_Renderer *renderer) const {
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), background, nullptr, nullptr);
    SDL_RenderCopy(const_cast<SDL_Renderer*>(renderer), fb_logo, nullptr, &fb_logo_rect);
    for (const MenuButton &button : buttons) {
        button.Render(renderer);
    }
}

MenuType MainMenu::handle_key_down_event(const SDL_Event &e)
{
    switch(e.key.keysym.sym) {
        case SDLK_UP:
            up();
            break;
        case SDLK_DOWN:
            down();
            break;
        case SDLK_RETURN:
            return buttons[active_button_index].get_menu();
    }
    return MAIN_MENU;
}

void MainMenu::down()
{
    buttons[active_button_index].Deactivate();
    if(active_button_index == (buttons.size() - 1)) {
        active_button_index = 0;
    } else {
        active_button_index++;
    }

    buttons[active_button_index].Activate();
}

void MainMenu::up()
{
    buttons[active_button_index].Deactivate();

    if(active_button_index == 0) {
        active_button_index = buttons.size() - 1;
    } else {
        active_button_index--;
    }

    buttons[active_button_index].Activate();
}