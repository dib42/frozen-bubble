#ifndef BUBBLE_H
#define BUBBLE_H

#include <cstdint>

enum class BubbleColor {
    BLACK  = 1,
    WHITE  = 2,
    BLUE   = 3,
    GREEN  = 4,
    YELLOW = 5,
    PINK   = 6,
    RED    = 7,
    ORANGE = 8
};

class Bubble
{
public:
    Bubble(BubbleColor color, uint8_t row, uint8_t column);
    uint8_t row;
    uint8_t column;
};

#endif // BUBBLE_H
