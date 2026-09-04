// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 ReconGrunt
#pragma once

#include <gui/view.h>

typedef struct FlockView FlockView;

/** Called when the user presses OK on a selected detection. */
typedef void (*FlockViewOkCallback)(void* context, int selected_index);

FlockView* flock_view_alloc(void);
void flock_view_free(FlockView* fv);
View* flock_view_get_view(FlockView* fv);

/** Set the owning ReconApp pointer (read for live data inside the draw callback). */
void flock_view_set_app(FlockView* fv, void* app);

/** Set the OK-press callback. */
void flock_view_set_ok_callback(FlockView* fv, FlockViewOkCallback cb, void* context);

/** Long-press OK on a row: the deliberate actions (confirm, rename, delete).
 *  Same signature as the OK callback and the same currency -- a TABLE index. */
void flock_view_set_hold_callback(FlockView* fv, FlockViewOkCallback cb, void* context);

/** Request a redraw (call from a GUI-thread tick; never hold app->mutex). */
void flock_view_refresh(FlockView* fv);

/** Reset selection/scroll to the top. */
void flock_view_reset(FlockView* fv);
