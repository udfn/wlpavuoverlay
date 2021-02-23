#ifndef _NWL_CAIRO_H
#define _NWL_CAIRO_H
#include "nwl/surface.h"
#include <cairo.h>

typedef bool (*nwl_surface_cairo_render_t)(struct nwl_surface *surface, cairo_surface_t *cairo_surface);
void nwl_surface_renderer_cairo(struct nwl_surface *surface, nwl_surface_cairo_render_t renderfunc);

#endif
