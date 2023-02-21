#ifndef FROZENBUBBLE_H
#define FROZENBUBBLE_H

#include <stdint.h>
#include <SDL2/SDL.h>
#include <string>

class FrozenBubble
{
public:
    FrozenBubble();
    ~FrozenBubble();
    uint8_t RunForEver();


private:
    bool IsGameQuit;
    SDL_Window *window;
    SDL_Renderer *renderer;
};

#endif // FROZENBUBBLE_H
