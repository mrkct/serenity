/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGfx/Bitmap.h>
#include <LibGfx/Filters/ColorChangingFilter.h>
#include <LibGfx/Rect.h>

namespace Gfx {

class ColorInvertingFilter : public ColorChangingFilter {
public:
    virtual ~ColorInvertingFilter() { }

    virtual const char* class_name() const override { return "ColorInvertingFilter"; };

    virtual Color transform_color(Color) override;

    ColorInvertingFilter() { }
};

}
