// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_XSHAPE_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_XSHAPE_H_

#include <xcb/shape.h>
#include <xcb/xcb.h>
#include "sommelier-ctx.h"  // NOLINT(build/include_directory)

void sl_handle_shape_notify(struct sl_context* ctx,
                            struct xcb_shape_notify_event_t* event);

void sl_shape_query(struct sl_context* ctx, xcb_window_t xwindow);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_XSHAPE_H_
