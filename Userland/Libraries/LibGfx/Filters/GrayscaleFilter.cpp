/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Filters/GrayscaleFilter.h>

namespace Gfx {

Color GrayscaleFilter::transform_color(Color color)
{
    auto components_average = (color.red() + color.green() + color.blue()) / 3;
    return Color(components_average, components_average, components_average, color.alpha());
}

}
