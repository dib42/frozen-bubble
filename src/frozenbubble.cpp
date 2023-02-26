#include "frozenbubble.h"
#include "mainmenu.h"
#include <iostream>

FrozenBubble::FrozenBubble() : IsGameQuit(false) {}

FrozenBubble::~FrozenBubble() {
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }
}

uint8_t FrozenBubble::RunForEver() {
  window = SDL_CreateWindow("Frozen-Bubble", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, 640, 480, 0);

  if (!window) {
    IsGameQuit = true;
    std::cout << "Failed to create window: " << SDL_GetError() << std::endl;
  }

  std::string icon_path =
      std::string(DATA_DIR) + "/gfx/pinguins/window_icon_penguin.bmp";
  SDL_Surface *icon = SDL_LoadBMP(icon_path.c_str());
  SDL_SetWindowIcon(window, icon);
  SDL_FreeSurface(icon);

  renderer = SDL_CreateRenderer(window, -1, 0);

  if (!renderer) {
    IsGameQuit = true;
    std::cout << "Failed to create renderer: " << SDL_GetError() << std::endl;
  }

  MainMenu main_menu(renderer);

  while (!IsGameQuit) {
    // handle input
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_WINDOWEVENT:
        switch (e.window.event) {
        case SDL_WINDOWEVENT_CLOSE: {
          IsGameQuit = true;
          break;
        }
        }
        break;
      case SDL_KEYDOWN:
        switch (e.key.keysym.sym) {
        case SDLK_UP:
          main_menu.up();
          break;
        case SDLK_DOWN:
          main_menu.down();
          break;
        }
        break;
      }
    }
    // do magic
    // render
    SDL_RenderClear(renderer);
    main_menu.Render();
    SDL_RenderPresent(renderer);
  }
  return 0;
}
