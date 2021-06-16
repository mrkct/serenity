/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Filters/ColorChangingFilter.h>

namespace Gfx {

void ColorChangingFilter::apply(Bitmap& target, const IntRect& target_rect, const Bitmap& source, const IntRect& source_rect, const Parameters&)
{
    for (int x = 0; x < source_rect.width(); x++) {
        for (int y = 0; y < source_rect.height(); y++) {
            auto pixel = source.get_pixel(source_rect.x() + x, source_rect.y() + y);
            target.set_pixel(target_rect.x() + x, target_rect.y() + y, transform_color(pixel));
        }
    }
}

}
