/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Filters/ColorInvertingFilter.h>

namespace Gfx {

Color ColorInvertingFilter::transform_color(Color color)
{
    return Color(255 - color.red(), 255 - color.green(), 255 - color.blue(), color.alpha());
}

}
