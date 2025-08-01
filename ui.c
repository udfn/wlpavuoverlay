#define NK_IMPLEMENTATION
#define NK_ZERO_COMMAND_MEMORY
// Silencing some warnings..
#define NK_SIN
#define NK_COS
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <cairo.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <nwl/surface.h>
#include <nwl/seat.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include "ui.h"
#include "nuklear.h"
#include "xdg-shell.h"
#include "audio.h"

struct wlpavuo_ui {
	struct nk_context *context;
	void *draw_last;
	void *draw_buf;
	int pointer_x;
	int pointer_y;
	uint32_t width,height;
	struct nk_user_font font;
	struct nk_user_font fontsmol;
	char last_pointer_buttons;
	struct nk_rect rect;
	struct nk_color color_table[NK_COLOR_COUNT];
	nk_bool active;
	bool inited;
	struct {
		int32_t pointer_x, pointer_y;
		int32_t pointer_down_x, pointer_down_y;
		char pointer_focus;
		char pointer_buttons;
		uint32_t pointer_serial;
		int scroll_hori;
		int scroll_vert;
		struct nwl_seat *last_input;
		int adjust_vol;
		int selected;
		char mute_selected;
		char scroll_to_selected;
		char num_shifts;
		char adjusting_volume;
	} input;
	const struct wlpavuo_audio_impl *backend;
	int num_items;
	int scale;
	int evfd; // eventfd used for redrawing
};

void cairo_set_nk_color(cairo_t *c, struct nk_color color) {
	cairo_set_source_rgba(c, (double)color.r/255, (double)color.g/255, (double)color.b/255, (double)color.a/255);
}

void wlpavuo_ui_render(struct nk_context *ctx, cairo_t *c) {
	const struct nk_command *cmd = 0;
	nk_foreach(cmd, ctx) {
	switch (cmd->type) {
		case NK_COMMAND_SCISSOR: {
			struct nk_command_scissor *scissor = (struct nk_command_scissor*)cmd;
			cairo_reset_clip(c);
			cairo_rectangle(c, scissor->x,scissor->y,scissor->w,scissor->h);
			cairo_clip(c);
			break;
		}
		case NK_COMMAND_CURVE: {
			struct nk_command_curve *curve = (struct nk_command_curve*)cmd;
			cairo_set_nk_color(c, curve->color);
			cairo_set_line_width(c, curve->line_thickness);
			cairo_move_to(c, curve->begin.x,curve->begin.y);
			cairo_curve_to(c, curve->ctrl[0].x, curve->ctrl[0].y, curve->ctrl[1].x, curve->ctrl[1].y, curve->end.x, curve->end.y);
			cairo_stroke(c);
			break;
		}
		case NK_COMMAND_LINE: {
			struct nk_command_line *line = (struct nk_command_line*)cmd;
			cairo_set_nk_color(c, line->color);
			cairo_set_line_width(c, line->line_thickness);
			cairo_move_to(c, line->begin.x, line->begin.y);
			cairo_line_to(c, line->end.x, line->end.y);
			cairo_stroke(c);
			break;
		}
		case NK_COMMAND_RECT_FILLED: {
			struct nk_command_rect_filled *rect = (struct nk_command_rect_filled*)cmd;
			cairo_set_nk_color(c, rect->color);
			cairo_rectangle(c, rect->x,rect->y, rect->w, rect->h);
			cairo_fill(c);
			break;
		}
		case NK_COMMAND_RECT: {
			struct nk_command_rect *rect = (struct nk_command_rect*)cmd;
			cairo_set_nk_color(c, rect->color);
			cairo_set_line_width(c, rect->line_thickness);
			cairo_rectangle(c, rect->x,rect->y, rect->w, rect->h);
			cairo_stroke(c);
			break;
		}
		case NK_COMMAND_CIRCLE_FILLED: {
			struct nk_command_circle_filled *circle = (struct nk_command_circle_filled*)cmd;
			cairo_set_nk_color(c, circle->color);
			short radi = circle->w/2;
			cairo_arc(c,circle->x+radi,circle->y+radi,radi,0,3.141*2);
			cairo_fill(c);
			break;
		}
		case NK_COMMAND_TEXT: {
			struct nk_command_text *text = (struct nk_command_text*)cmd;
			cairo_set_nk_color(c, text->foreground);
			cairo_select_font_face (c, "sans",
			text->font->height < 13 ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size (c, text->height);
			cairo_move_to(c, text->x, text->y+(text->height));
			cairo_show_text(c, text->string);
			break;
		}
		case NK_COMMAND_TRIANGLE_FILLED: {
			struct nk_command_triangle_filled *tri = (struct nk_command_triangle_filled*)cmd;
			cairo_set_nk_color(c, tri->color);
			cairo_new_path(c);
			cairo_line_to(c, tri->a.x,tri->a.y);
			cairo_line_to(c, tri->b.x, tri->b.y);
			cairo_line_to(c, tri->c.x, tri->c.y);
			cairo_close_path(c);
			cairo_fill(c);
			break;
		}
		default:
			printf("don't know how to draw %i\n",cmd->type);
			break; //ooops!
	}}

}

float calc_text_length(nk_handle handle, float height, const char *text, int len) {
	UNUSED(len);
	cairo_text_extents_t extents;
	cairo_set_font_size(handle.ptr, height);
	cairo_select_font_face (handle.ptr, "sans",CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_text_extents(handle.ptr, text, &extents);
	return extents.width;
}

static void copy_wlpavuo_input_to_nk(struct wlpavuo_ui *ui, struct nk_context *ctx) {
	nk_input_begin(ctx);
	if (ui->input.pointer_buttons & NWL_MOUSE_LEFT) {
		nk_input_button(ctx, NK_BUTTON_LEFT, ui->input.pointer_down_x,ui->input.pointer_down_y, 1);
	} else {
		nk_input_button(ctx, NK_BUTTON_LEFT, ui->input.pointer_x,ui->input.pointer_y, 0);
	}
	if (ui->input.pointer_buttons & NWL_MOUSE_RIGHT) {
		nk_input_button(ctx, NK_BUTTON_RIGHT, ui->input.pointer_down_x, ui->input.pointer_down_y, 1);
	} else {
		nk_input_button(ctx, NK_BUTTON_RIGHT, ui->input.pointer_x,ui->input.pointer_y, 0);
	}
	if (ui->input.pointer_focus) {
		nk_input_motion(ctx, ui->input.pointer_x, ui->input.pointer_y);
	} else {
		// bit of a hack so pointer hover doesn't get "stuck"
		nk_input_motion(ctx, -1,-1);
	}
	if (ui->input.scroll_vert != 0 || ui->input.scroll_hori != 0) {
		nk_input_scroll(ctx,nk_vec2(ui->input.scroll_hori,-ui->input.scroll_vert));
		ui->input.scroll_vert = 0;
		ui->input.scroll_hori = 0;
	}
	nk_input_end(ctx);
}

static void set_nk_color(struct nk_color *color,nk_byte r,nk_byte g, nk_byte b, nk_byte a) {
	color->a = a;
	color->r = r;
	color->g = g;
	color->b = b;
}

#define WINDOW_RESIZE_BORDER 4
#define WINDOW_CORNER_RESIZE 25
static void check_window_move(struct wlpavuo_surface *wlpsurface, struct nk_context *ctx) {
	struct nwl_surface *surface = &wlpsurface->main_surface;
	if (!(surface->states & NWL_SURFACE_STATE_CSD)) {
		return;
	}
	struct nk_window *mainwin = nk_window_find(ctx, "mainwin");
	if (mainwin->flags & (NK_WINDOW_CLOSED|NK_WINDOW_HIDDEN)) {
		nwl_surface_destroy_later(surface);
		return;
	}
	if (!surface->wl.xdg_surface) {
		return;
	}
	struct wlpavuo_ui *ui = wlpsurface->ui;
	if (mainwin && ui->input.pointer_buttons & NWL_MOUSE_LEFT) {
		int left_mouse_click_in_cursor;
		// check resize...
		uint32_t edges = 0;
		int32_t surf_height = surface->height;
		int32_t surf_width = surface->width;
		int resize_border_scaled = WINDOW_RESIZE_BORDER * surface->scale;
		int resize_corner_scaled = WINDOW_CORNER_RESIZE * surface->scale;
		if (ui->input.pointer_down_x < resize_border_scaled) {
			if (ui->input.pointer_down_y < resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
			} else if (ui->input.pointer_down_y > surf_height-resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
			} else {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
			}
		// right
		} else if (ui->input.pointer_down_x > surf_width-resize_border_scaled) {
			if (ui->input.pointer_down_y < resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
			} else if (ui->input.pointer_down_y > surf_height-resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
			} else {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
			}
		// top
		} else if (ui->input.pointer_down_y < resize_border_scaled) {
			if (ui->input.pointer_down_x < resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
			} else if (ui->input.pointer_down_x > surf_width-resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
			} else {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP;
			}
		// and bottom
		} else if (ui->input.pointer_down_y > surf_height-resize_border_scaled) {
			if (ui->input.pointer_down_x < resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
			} else if (ui->input.pointer_down_x > surf_width-resize_corner_scaled) {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
			} else {
				edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
			}
		}
		uint32_t serial = ui->input.pointer_serial;
		if (edges) {
			xdg_toplevel_resize(surface->role.toplevel.wl, ui->input.last_input->wl_seat,
				serial, edges);
			return;
		}
		/* calculate draggable window space */
		struct nk_rect header;
		header.x = mainwin->bounds.x;
		header.y = mainwin->bounds.y;
		header.w = mainwin->bounds.w;
		if (nk_panel_has_header(mainwin->flags, "mainwin")) {
			header.h = ctx->style.font->height + 2.0f * ctx->style.window.header.padding.y;
			header.h += 2.0f * ctx->style.window.header.label_padding.y;
		}

		left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(&ctx->input,
			NK_BUTTON_LEFT, header, 1);
		if (left_mouse_click_in_cursor) {
			xdg_toplevel_move(surface->role.toplevel.wl, ui->input.last_input->wl_seat, serial);
		}
	}

}

void wlpavuo_ui_destroy(struct nwl_surface *surface) {
	struct wlpavuo_surface *wlpsurface = wl_container_of(surface, wlpsurface, main_surface);
	struct wlpavuo_ui *ui = wlpsurface->ui;
	struct wlpavuo_state *state = state_from_core(surface->core);
	if (ui->backend->iterate) {
		nwl_easy_del_fd(&state->nwl, ui->backend->get_fd());
	} else {
		nwl_easy_del_fd(&state->nwl, ui->evfd);
		close(ui->evfd);
	}
	ui->backend->uninit();
	free(ui->context);
	free(ui->draw_last);
	free(ui->draw_buf);
	free(ui);
	nwl_cairo_renderer_finish(&wlpsurface->cairo_renderer);
	wlpsurface->ui = NULL;
}

void do_volume_control(struct nwl_surface *surface, struct nk_context *ctx,
		struct nk_user_font *font, struct nk_user_font *fontb, char *label, unsigned long *volume, int *mute) {
	struct nk_color color_orig = ctx->style.text.color;
	struct nk_color color_muted = color_orig;
	color_muted.a = color_muted.a/2;
	if (*mute) {
		ctx->style.text.color = color_muted;
	}
	nk_style_set_font(ctx, font);
	nk_layout_row_begin(ctx, NK_STATIC, 22, 2);
	nk_layout_row_push(ctx, surface->width-90);
	if (label != NULL) {
		nk_label(ctx,label,NK_TEXT_ALIGN_LEFT|NK_TEXT_ALIGN_MIDDLE);
	} else {
		// Is there a "spacer" widget?
		nk_label(ctx,"",NK_TEXT_ALIGN_LEFT);
	}
	nk_layout_row_push(ctx, 50);
	nk_style_set_font(ctx, fontb);
	nk_checkbox_label(ctx, "Mute", mute);
	nk_layout_row_end(ctx);
	nk_layout_row_dynamic(ctx, 30, 1);
	struct nk_style_progress progress_style = ctx->style.progress;
	if (*mute) {
		ctx->style.progress.normal.data.color.a = progress_style.normal.data.color.a/2;
		ctx->style.progress.hover.data.color.a = progress_style.hover.data.color.a/2;
		ctx->style.progress.active.data.color.a = progress_style.active.data.color.a/2;
		ctx->style.progress.cursor_normal.data.color.a = progress_style.cursor_normal.data.color.a/2;
		ctx->style.progress.cursor_hover.data.color.a = progress_style.cursor_hover.data.color.a/2;
		ctx->style.progress.cursor_active.data.color.a = progress_style.cursor_active.data.color.a/2;
	}
	nk_progress(ctx, volume, 0x10000U, 1);
	ctx->style.text.color = color_orig;
	ctx->style.progress = progress_style;
}

void do_keyboard_input(struct wlpavuo_ui *ui, struct nk_context *ctx, unsigned long *volume, int *mute, int *scroll) {
	set_nk_color(&ctx->style.progress.normal.data.color, 37, 57, 86, 255);
	set_nk_color(&ctx->style.progress.hover.data.color, 72, 72, 101, 255);
	set_nk_color(&ctx->style.progress.active.data.color, 52, 72, 101, 255);
	set_nk_color(&ctx->style.text.color, 235, 235, 250, 240);
	if (ui->input.adjusting_volume) {
		set_nk_color(&ctx->style.progress.cursor_normal.data.color, 140, 160, 180, 230);
		set_nk_color(&ctx->style.progress.cursor_hover.data.color, 165, 185, 205, 230);
		set_nk_color(&ctx->style.progress.cursor_active.data.color, 195, 210, 230, 230);
	} else {
		set_nk_color(&ctx->style.progress.cursor_normal.data.color, 110, 110, 110, 195);
		set_nk_color(&ctx->style.progress.cursor_hover.data.color, 135, 135, 135, 195);
		set_nk_color(&ctx->style.progress.cursor_active.data.color, 160, 160, 160, 195);
	}
	if (((long)*volume + ui->input.adjust_vol) < 0) {
		*volume = 0;
	} else {
		*volume += ui->input.adjust_vol;
	}
	if (*volume > 0x10000U) {
		*volume = 0x10000U;
	}
	ui->input.adjust_vol = 0;
	if (ui->input.mute_selected) {
		*mute = !*mute;
		ui->input.mute_selected = 0;
	}
	if (ui->input.scroll_to_selected) {
		if ((unsigned int)ctx->current->layout->at_y+40 > ui->height/2) {
			unsigned int vscroll = (unsigned int)ctx->current->layout->at_y+40 - ui->height/2;
			*scroll = vscroll;
		} else {
			*scroll = 0;
		}
		ui->input.scroll_to_selected = 0;
	}
}

static void ui_select_item(struct wlpavuo_ui *ui, int dir) {
	int new_pos = ui->input.selected += dir;
	if (new_pos < 0) {
		new_pos = ui->num_items-1;
	} else if (new_pos > ui->num_items-1) {
		new_pos = 0;
	}
	ui->input.selected = new_pos;
	ui->input.scroll_to_selected = 1;
}

void wlpavuo_ui_input_stdin(struct nwl_easy *state, uint32_t events, void *data) {
	struct wlpavuo_surface *wlpsurface = data;
	struct wlpavuo_ui *ui = wlpsurface->ui;
	if (!ui) {
		return;
	}
	unsigned char buf[32];
	size_t read_amt = read(0, buf, 32);
	for (int i = 0; i < read_amt; i++) {
		char c = buf[i];
		switch (c) {
			case 's':
				ui->input.adjusting_volume = !ui->input.adjusting_volume;
				break;
			case 'u':
				if (ui->input.adjusting_volume) {
					ui->input.adjust_vol += 2500;
				} else {
					ui_select_item(ui, 1);
				}
				break;
			case 'd':
				if (ui->input.adjusting_volume) {
					ui->input.adjust_vol -= 2500;
				} else {
					ui_select_item(ui, -1);
				}
				break;
			case 'j':
				ui_select_item(ui, 1);
				break;
			case 'k':
				ui_select_item(ui, -1);
				break;
			case 'm':
				ui->input.mute_selected = 1;
				break;
			case 'q':
				wlpsurface->main_surface.core->num_surfaces = 0;
				break;
		}
	}
	nwl_surface_set_need_update(&wlpsurface->main_surface, true);
}

void wlpavuo_ui_input_keyboard(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_keyboard_event *event) {
	struct wlpavuo_surface *wlpsurface = wl_container_of(surface, wlpsurface, main_surface);
	struct wlpavuo_ui *ui = wlpsurface->ui;
	if (!ui) {
		return;
	}
	// This stuff shouldn't depend on the interface rendering..
	// It should also be rebindable..
	if (event->type == NWL_KEYBOARD_EVENT_KEYDOWN || event->type == NWL_KEYBOARD_EVENT_KEYREPEAT) {
		int adjust_amount = ui->input.num_shifts ? 10000 : 2500;
		switch (event->keysym) {
			case XKB_KEY_h:
			case XKB_KEY_Left:
			case XKB_KEY_H:
				ui->input.adjust_vol -= adjust_amount;
				break;
			case XKB_KEY_l:
			case XKB_KEY_Right:
			case XKB_KEY_L:
				ui->input.adjust_vol += adjust_amount;
				break;
			case XKB_KEY_j:
			case XKB_KEY_J:
			case XKB_KEY_Down:
				ui_select_item(ui, 1);
				break;
			case XKB_KEY_k:
			case XKB_KEY_K:
			case XKB_KEY_Up:
				ui_select_item(ui, -1);
				break;
			case XKB_KEY_m:
			case XKB_KEY_M:
				ui->input.mute_selected = 1;
				break;
			case XKB_KEY_Shift_L:
			case XKB_KEY_Shift_R:
				if (event->type == NWL_KEYBOARD_EVENT_KEYDOWN) {
					ui->input.num_shifts++;
				}
				break;
		}
	} else if (event->type == NWL_KEYBOARD_EVENT_KEYUP) {
		switch (event->keysym) {
			case XKB_KEY_Escape:
				surface->core->num_surfaces = 0;
				break;
			case XKB_KEY_Shift_L:
			case XKB_KEY_Shift_R:
				if (ui->input.num_shifts > 0) {
					ui->input.num_shifts--;
				}
				break;

		}
	} else if (event->type == NWL_KEYBOARD_EVENT_FOCUS) {
		ui->input.num_shifts = 0;
		// This shouldn't be here, and maybe repeat should be on by default?
		seat->keyboard_repeat_enabled = true;
	}
	nwl_surface_set_need_update(surface, true);
}

void wlpavuo_ui_input_pointer(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_pointer_event *event) {
	struct wlpavuo_surface *wlpsurface = wl_container_of(surface, wlpsurface, main_surface);
	struct wlpavuo_ui *ui = wlpsurface->ui;
	if (!ui) {
		return;
	}
	if (event->changed & NWL_POINTER_EVENT_FOCUS) {
		if (event->focus) {
			ui->input.pointer_focus++;
		} else {
			ui->input.pointer_focus--;
			ui->input.pointer_buttons = 0;
		}
	}
	if (event->changed & NWL_POINTER_EVENT_MOTION) {
		ui->input.pointer_x = wl_fixed_to_int(event->surface_x);
		ui->input.pointer_y = wl_fixed_to_int(event->surface_y);
	}
	if (event->changed & NWL_POINTER_EVENT_BUTTON) {
		if (!(ui->input.pointer_buttons & NWL_MOUSE_LEFT) && (event->buttons & NWL_MOUSE_LEFT)) {
			ui->input.pointer_down_x = ui->input.pointer_x;
			ui->input.pointer_down_y = ui->input.pointer_y;
		}
		ui->input.pointer_buttons = event->buttons;
	}
	if (event->changed & NWL_POINTER_EVENT_AXIS) {
		ui->input.scroll_hori += wl_fixed_to_int(event->axis_hori);
		ui->input.scroll_vert += wl_fixed_to_int(event->axis_vert);
	}
	ui->input.pointer_serial = event->serial;
	ui->input.last_input = seat;
	nwl_surface_set_need_update(surface, true);
}

static void maybe_update_size(struct wlpavuo_surface *surf) {
	struct wlpavuo_ui *ui = surf->ui;
	struct wlpavuo_state *state = state_from_core(surf->main_surface.core);
	if (state->dynamic_height) {
		uint32_t new_height = 50;
		if (surf->main_surface.states & NWL_SURFACE_STATE_CSD) {
			new_height += 40;
		}
		struct wlpavuo_audio_client *client;
		struct wlpavuo_audio_sink *sink;
		wl_list_for_each(client, ui->backend->get_clients(), link) {
			if (client->streams_count) {
				new_height += 24 + (client->streams_count * 60);
			}
		}
		wl_list_for_each(sink, ui->backend->get_sinks(), link) {
			new_height += 65;
		}
		if (new_height > state->height) {
			new_height = state->height;
		}
		if (new_height > surf->main_surface.desired_height) {
			nwl_surface_set_size(&surf->main_surface, surf->main_surface.desired_width, new_height);
		}
	}
}

static void rerender_from_event(struct nwl_easy *state, uint32_t events, void *data) {
	UNUSED(state);
	UNUSED(events);
	struct wlpavuo_surface *surf = data;
	eventfd_t val;
	eventfd_read(surf->ui->evfd, &val);
	maybe_update_size(surf);
	nwl_surface_set_need_update(&surf->main_surface, false);
}

static void handle_audio_update_singlethread(void *data) {
	struct wlpavuo_surface *surf = data;
	maybe_update_size(surf);
	nwl_surface_set_need_update(&surf->main_surface, false);
}

static void handle_audio_update(void *data) {
	struct wlpavuo_surface *surface = data;
	// Tell the main thread to update..
	eventfd_write(surface->ui->evfd, 1);
}

static void set_progress_unselected_color(struct nk_context *ctx) {
	set_nk_color(&ctx->style.progress.normal.data.color, 35, 35, 35, 195);
	set_nk_color(&ctx->style.progress.hover.data.color, 45, 45, 45, 195);
	set_nk_color(&ctx->style.progress.active.data.color, 45, 45, 45, 195);
	set_nk_color(&ctx->style.progress.cursor_normal.data.color, 110, 110, 110, 195);
	set_nk_color(&ctx->style.progress.cursor_hover.data.color, 135, 135, 135, 195);
	set_nk_color(&ctx->style.progress.cursor_active.data.color, 160, 160, 160, 195);
}

static void do_iterate(struct nwl_easy *state, uint32_t events, void *data) {
	struct wlpavuo_ui *ui = data;
	ui->backend->iterate();
}

void wlpavuo_ui_run(struct wlpavuo_surface *wlpsurface) {
	struct wlpavuo_ui *ui = wlpsurface->ui;
	struct nwl_surface *surface = &wlpsurface->main_surface;
	if(!ui) {
		wlpsurface->ui = calloc(1, sizeof(struct wlpavuo_ui));
		ui = wlpsurface->ui;
		ui->context = calloc(1, sizeof(struct nk_context));
		ui->draw_buf = calloc(1,64*1024);
		ui->draw_last = calloc(1,64*1024);
		ui->font.width = calc_text_length;
		ui->fontsmol.width = calc_text_length;
		ui->fontsmol.height = 12;
		ui->font.height = 16;
		nk_init_fixed(ui->context, ui->draw_buf,64*1024,&ui->font);
		memcpy(ui->color_table, nk_default_color_style, sizeof(nk_default_color_style));
		set_nk_color(&ui->color_table[NK_COLOR_WINDOW], 8,8,8,231);
		nk_style_from_table(ui->context, ui->color_table);
#ifdef HAVE_PIPEWIRE
		struct wlpavuo_state *state = state_from_core(surface->core);
		if (state->use_pipewire) {
			ui->backend = wlpavuo_audio_get_pw();
		} else
#endif
		{
			ui->backend = wlpavuo_audio_get_pa();
		}
		if (ui->backend->iterate) {
			nwl_easy_add_fd(&state->nwl, ui->backend->get_fd(), EPOLLIN, do_iterate, ui);
			ui->backend->set_update_callback(handle_audio_update_singlethread, wlpsurface);
		} else {
			ui->evfd = eventfd(0, 0);
			nwl_easy_add_fd(&state->nwl, ui->evfd, EPOLLIN, rerender_from_event, wlpsurface);
			ui->backend->set_update_callback(handle_audio_update, wlpsurface);
		}
	}
	const struct wlpavuo_audio_impl *aimpl = ui->backend;
	struct nwl_cairo_surface *cairo_surface = nwl_cairo_renderer_get_surface(&wlpsurface->cairo_renderer, surface, false);
	cairo_t *cr = cairo_surface->ctx;
	ui->font.userdata.ptr = cr;
	ui->fontsmol.userdata.ptr = cr;
	struct nk_context *ctx = ui->context;
	if (surface->states & NWL_SURFACE_STATE_ACTIVE) {
		if (!ui->active) {
			set_nk_color(&ui->color_table[NK_COLOR_HEADER], 40, 40, 40, 255);
			nk_style_from_table(ctx, ui->color_table);
			ui->active = 1;
		}
	} else {
		if (ui->active) {
			set_nk_color(&ui->color_table[NK_COLOR_HEADER], 30,30,30, 233);
			nk_style_from_table(ctx, ui->color_table);
			ui->active = 0;
		}
	}
	enum wlpavuo_audio_status status = aimpl->init();
	aimpl->lock();
	copy_wlpavuo_input_to_nk(ui, ctx);
	int32_t scale = surface->scale;
	nk_flags winflags = surface->states & NWL_SURFACE_STATE_CSD ? NK_WINDOW_TITLE|NK_WINDOW_BORDER|NK_WINDOW_CLOSABLE:
		surface->role_id == NWL_SURFACE_ROLE_LAYER || surface->role_id == NWL_SURFACE_ROLE_SUB ? NK_WINDOW_BORDER : 0;
	int inset = winflags ? 1 : 0;
	nk_style_set_font(ctx, &ui->font);
	if (nk_begin_titled(ctx, "mainwin", surface->title,nk_rect(inset,inset,
			surface->width-(inset*2),surface->height-(inset*2)),
			winflags)) {
		int scroll_to = -1;
		char buf[256];
		if (status == WLPAVUO_AUDIO_STATUS_CONNECTING) {
			nk_layout_row_dynamic(ctx, 22, 1);
			snprintf(buf,255, "Connecting to %s...", aimpl->get_name());
			nk_label(ctx,buf,NK_TEXT_ALIGN_CENTERED);
		} else if (status == WLPAVUO_AUDIO_STATUS_FAILED) {
			nk_layout_row_dynamic(ctx, 22, 1);
			snprintf(buf,255,"Failed connecting to %s :(", aimpl->get_name());
			nk_label(ctx,buf, NK_TEXT_ALIGN_CENTERED);
			if (nk_button_label(ctx, "Quit")) {
				surface->core->num_surfaces = 0;
			}
		} else if (status == WLPAVUO_AUDIO_STATUS_READY) {
			struct wl_list *sinks = aimpl->get_sinks();
			struct wl_list *clients = aimpl->get_clients();
			int counter = 0;
			struct wlpavuo_audio_sink *sink;
			if (!ui->inited) {
				ui->inited = true;
				wl_list_for_each(sink, sinks, link) {
					if (sink->flags & WLPAVUO_AUDIO_DEFAULT_SINK) {
						ui->input.selected = counter;
						break;
					}
					counter++;
				}
				counter = 0;
			}
			wl_list_for_each(sink, sinks, link) {
				int mutetmp = sink->flags & WLPAVUO_AUDIO_MUTED;
				unsigned long voltmp = sink->volume;
				struct nk_color orig_text_color = ctx->style.text.color;
				if (counter == ui->input.selected) {
					do_keyboard_input(ui, ctx, &voltmp, &mutetmp, &scroll_to);
				}
				else {
					set_progress_unselected_color(ctx);
				}
				snprintf(buf,255,"%s", sink->name);
				do_volume_control(surface, ctx, &ui->font, &ui->fontsmol, buf, &voltmp, &mutetmp);
				if (mutetmp != (sink->flags & WLPAVUO_AUDIO_MUTED)) {
					aimpl->set_sink_mute(sink, mutetmp ? 1 : 0);
				}
				if (voltmp != sink->volume) {
					aimpl->set_sink_volume(sink, voltmp);
				}
				ctx->style.text.color = orig_text_color;
				counter++;
			}
			nk_style_set_font(ctx, &ui->font);
			nk_label(ctx,"-----",NK_TEXT_ALIGN_CENTERED);
			uint32_t count = 0;
			struct wlpavuo_audio_client *client;
			// Go through the list in reverse, to make new clients not push everything down
			wl_list_for_each_reverse(client, clients, link) {
				nk_style_set_font(ctx, &ui->font);
				struct nk_color orig_text_color = ctx->style.text.color;
				if (client->streams_count == 0) {
					continue;
				}
				count++;
				nk_layout_row_dynamic(ctx, 20, 1);
				snprintf(buf, 255, "%s",client->name);
				nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
				struct wlpavuo_audio_client_stream *stream;
				wl_list_for_each(stream,&client->streams,link) {
					int mutetmp = stream->flags & WLPAVUO_AUDIO_MUTED;
					unsigned long voltmp = stream->volume;
					if (counter == ui->input.selected) {
						do_keyboard_input(ui, ctx, &voltmp, &mutetmp, &scroll_to);
					} else {
						set_progress_unselected_color(ctx);
					}
					snprintf(buf, 255, "%s", stream->name);
					do_volume_control(surface, ctx, &ui->fontsmol, &ui->fontsmol, buf, &voltmp, &mutetmp);
					if (mutetmp != (stream->flags & WLPAVUO_AUDIO_MUTED)) {
						aimpl->set_stream_mute(stream, mutetmp ? 1 : 0);
					}
					if (voltmp != stream->volume) {
						aimpl->set_stream_volume(stream, voltmp);
					}
					ctx->style.text.color = orig_text_color;
					counter++;
				}
			}
			if (count == 0) {
				nk_layout_row_dynamic(ctx, 22, 1);
				nk_label(ctx, "Nothing is making noise!", NK_TEXT_ALIGN_CENTERED);
			}
			if (counter-1 < ui->input.selected) {
				ui->input.selected = counter-1;
				nwl_surface_set_need_update(surface,true);
			}
			ui->num_items = counter;
		}
		if (scroll_to != -1) {
			nk_window_set_scroll(ctx, 0, scroll_to);
		}
	}
	aimpl->unlock();
	nk_end(ctx);
	check_window_move(wlpsurface, ctx);
	void *cmds = nk_buffer_memory(&ctx->memory);
	if (memcmp(cmds, ui->draw_last, ctx->memory.allocated) ||
		ui->height != surface->height || ui->width != surface->width || surface->scale != ui->scale) {
		cairo_scale(cr, scale,scale);
		ui->scale = surface->scale;
		memcpy(ui->draw_last,cmds,ctx->memory.allocated);
		ui->width = surface->width;
		ui->height = surface->height;
		cairo_set_source_rgba(cr, 0,0,0,0);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		wlpavuo_ui_render(ctx, cr);
		nwl_cairo_renderer_submit(&wlpsurface->cairo_renderer, surface, 0, 0);
		// Ugly hack..
		if (surface->role_id == NWL_SURFACE_ROLE_SUB) {
			wl_surface_commit(wlpsurface->bg_surface.wl.surface);
		}
	}
	nk_clear(ctx);
}
