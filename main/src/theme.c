#include "theme.h"

const theme_t g_theme = {
    .bg          = C565(14, 15, 17),
    .surface     = C565(24, 26, 29),
    .surface_hi  = C565(34, 36, 40),
    .text        = C565(238, 231, 216),
    .text_dim    = C565(133, 132, 126),
    .accent      = C565(226, 166,  64),
    .accent_dim  = C565(120,  88,  38),
    .line        = C565(45,  46,  50),
    .focus       = C565(48,  42,  32),
    .focus_edge  = C565(226, 166,  64),
    .ok          = C565(106, 168, 118),
    .warn        = C565(201,  96,  80),
};
