#pragma once

#include "font.h"
#include <cstdint>


struct SimpleText { 
    FontAPIText text; uint32_t color; 
    const uint32_t* key() const { return reinterpret_cast<const uint32_t*>(&text); }
};

static const SimpleText TestTextA[] = {
    { U'零', 2, ts_float_to_fixed(60.0f), 0xff66ccff },
    { U'下', 2, ts_float_to_fixed(17.1f), 0xff000000 },
    { U'5' , 0, ts_float_to_fixed(14.1f), 0xffff0000 },
    { U'°' , 1, ts_float_to_fixed(14.1f), 0xff66ccff },
    { U'C' , 1, ts_float_to_fixed(14.1f), 0xff66ccff },
    { U'_' , 1, ts_float_to_fixed(14.1f), 0xff00ffff },
    { U'零', 2, ts_float_to_fixed(60.0f), 0xff66ccff },
    { U'上', 2, ts_float_to_fixed(17.1f), 0xff000000 },
    { U'5' , 0, ts_float_to_fixed(14.1f), 0xffff0000 },
    { U'°' , 1, ts_float_to_fixed(14.1f), 0xff66ccff },
    { U'C' , 1, ts_float_to_fixed(14.1f), 0xff66ccff },
};

