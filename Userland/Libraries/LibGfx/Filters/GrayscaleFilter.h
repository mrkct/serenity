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

class GrayscaleFilter : public ColorChangingFilter {
public:
    virtual ~GrayscaleFilter() { }

    virtual const char* class_name() const override { return "GrayscaleFilter"; };

    virtual Color transform_color(Color) override;

    GrayscaleFilter() { }
};

}
