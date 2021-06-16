/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGfx/Bitmap.h>
#include <LibGfx/Filters/Filter.h>
#include <LibGfx/Rect.h>

namespace Gfx {

class ColorChangingFilter : public Filter {
public:
    virtual ~ColorChangingFilter() { }

    virtual void apply(Bitmap&, const IntRect&, const Bitmap&, const IntRect&, const Parameters&) override;

    virtual Color transform_color(Color pixel) = 0;

protected:
    ColorChangingFilter() { }
};

}
