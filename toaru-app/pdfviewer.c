/**
 * @brief Port of MuPDF to ToaruOS
 *
 * This is a simple PDF viewer built on MuPDF/libfitz.
 *
 * @copyright
 * Copyright (C) 2006-2012 Artifex Software, Inc.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <fitz.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>

#include "badmath.c"

#define APPLICATION_TITLE "MuPDF"

gfx_context_t * gfx_ctx;
yutani_t * yctx;
yutani_window_t * window;

static struct menu_bar menu_bar = {0};
static struct menu_bar_entries menu_entries[] = {
	{"File", "file"},
	{"Help", "help"},
	{NULL, NULL},
};

int getrusage(int who, void *usage) {
	/* We don't support getrusage, required by a dependency */
	return -1;
}

enum { TEXT_PLAIN = 1, TEXT_HTML = 2, TEXT_XML = 3 };

static float resolution = 72;
static int res_specified = 0;
static float rotation = 0;

static int width = 0;
static int height = 0;
static int fit = 0;
static int errored = 0;
static int ignore_errors = 0;
static int alphabits = 8;
static int toggle_decors = 1;

static fz_text_sheet *sheet = NULL;
static fz_colorspace *colorspace;
static char *filename;
static int files = 0;

void draw_decors(int page, int epage) {
	if (toggle_decors) {
		char title[512] = {0};
		sprintf(title, "%s - Page %d of %d", APPLICATION_TITLE, page, epage);
		render_decorations(window, gfx_ctx, title);
	}
}

static void inplace_reorder(char * samples, int size) {
	for (int i = 0; i < size; ++i) {
		uint32_t c = ((uint32_t *)samples)[i];
		char b = _RED(c);
		char g = _GRE(c);
		char r = _BLU(c);
		char a = _ALP(c);
		((uint32_t *)samples)[i] = rgba(r,g,b,a);
	}
}

static int isrange(char *s)
{
	while (*s)
	{
		if ((*s < '0' || *s > '9') && *s != '-' && *s != ',')
			return 0;
		s++;
	}
	return 1;
}

static void draw_menu(void) {
	struct decor_bounds bounds;
	decor_get_bounds(window, &bounds);
	menu_bar.x = bounds.left_width;
	menu_bar.y = bounds.top_height;
	menu_bar.width = gfx_ctx->width - bounds.width;
	menu_bar.window = window;
	menu_bar_render(&menu_bar, gfx_ctx);
}

static void drawpage(fz_context *ctx, fz_document *doc, int pagenum)
{
	fz_page *page;
	fz_display_list *list = NULL;
	fz_device *dev = NULL;
	int start;
	fz_cookie cookie = { 0 };

	fz_var(list);
	fz_var(dev);

	fz_try(ctx)
	{
		page = fz_load_page(doc, pagenum - 1);
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot load page %d in file '%s'", pagenum, filename);
	}

	float zoom;
	fz_matrix ctm;
	fz_rect bounds, bounds2;
	fz_bbox bbox;
	fz_pixmap *pix = NULL;
	int w, h;

	fz_var(pix);

	bounds = fz_bound_page(doc, page);
	zoom = resolution / 72;
	ctm = fz_scale(zoom, zoom);
	ctm = fz_concat(ctm, fz_rotate(rotation));
	bounds2 = fz_transform_rect(ctm, bounds);
	bbox = fz_round_rect(bounds2);
	/* Make local copies of our width/height */
	w = width;
	h = height;
	/* If a resolution is specified, check to see whether w/h are
	 * exceeded; if not, unset them. */
	if (res_specified)
	{
		int t;
		t = bbox.x1 - bbox.x0;
		if (w && t <= w)
			w = 0;
		t = bbox.y1 - bbox.y0;
		if (h && t <= h)
			h = 0;
	}
	/* Now w or h will be 0 unless then need to be enforced. */
	if (w || h)
	{
		float scalex = w/(bounds2.x1-bounds2.x0);
		float scaley = h/(bounds2.y1-bounds2.y0);

		if (fit)
		{
			if (w == 0)
				scalex = 1.0f;
			if (h == 0)
				scaley = 1.0f;
		}
		else
		{
			if (w == 0)
				scalex = scaley;
			if (h == 0)
				scaley = scalex;
		}
		if (!fit)
		{
			if (scalex > scaley)
				scalex = scaley;
			else
				scaley = scalex;
		}
		ctm = fz_concat(ctm, fz_scale(scalex, scaley));
		bounds2 = fz_transform_rect(ctm, bounds);
	}
	bbox = fz_round_rect(bounds2);

	/* TODO: banded rendering and multi-page ppm */

	fz_try(ctx)
	{
		pix = fz_new_pixmap_with_bbox(ctx, colorspace, bbox);

		fz_clear_pixmap_with_value(ctx, pix, 255);

		dev = fz_new_draw_device(ctx, pix);
		if (list)
			fz_run_display_list(list, dev, ctm, bbox, &cookie);
		else
			fz_run_page(doc, page, dev, ctm, &cookie);
		fz_free_device(dev);
		dev = NULL;

		int size = fz_pixmap_height(ctx, pix) * fz_pixmap_width(ctx, pix);
		inplace_reorder(fz_pixmap_samples(ctx, pix), size);
		size_t x_offset = (width - fz_pixmap_width(ctx, pix)) / 2;
		size_t y_offset = (height - fz_pixmap_height(ctx, pix)) / 2;;
		if (toggle_decors) {
			struct decor_bounds bounds;
			decor_get_bounds(window, &bounds);
			x_offset += bounds.left_width;
			y_offset += bounds.top_height + MENU_BAR_HEIGHT;
		}
		for (int i = 0; i < fz_pixmap_height(ctx, pix); ++i) {
			memcpy(&GFX(gfx_ctx, x_offset, y_offset + i), &fz_pixmap_samples(ctx, pix)[fz_pixmap_width(ctx, pix) * i * 4], fz_pixmap_width(ctx, pix) * 4);
		}

	}
	fz_always(ctx)
	{
		fz_free_device(dev);
		dev = NULL;
		fz_drop_pixmap(ctx, pix);
	}
	fz_catch(ctx)
	{
		fz_free_display_list(ctx, list);
		fz_free_page(doc, page);
		fz_rethrow(ctx);
	}

	if (list)
		fz_free_display_list(ctx, list);

	fz_free_page(doc, page);

	fz_flush_warnings(ctx);

	if (cookie.errors)
		errored = 1;
}

int current_page  = 0;
int current_epage = 0;
fz_document * current_doc = NULL;
fz_context  * current_ctx = NULL;

static void redraw_window_callback(struct menu_bar * self) {
	draw_decors(current_page, current_epage);
	draw_menu();
	drawpage(current_ctx, current_doc, current_page);
}

void recalc_size(int w, int h) {
	if (!toggle_decors) {
		width = w;
		height = h;
	} else {
		struct decor_bounds bounds;
		decor_get_bounds(window, &bounds);
		width  = w - bounds.left_width - bounds.right_width;
		height = h - bounds.top_height - bounds.bottom_height - MENU_BAR_HEIGHT;
	}
}

void toggle_decorations(void) {
	toggle_decors = !toggle_decors;
	draw_fill(gfx_ctx, rgb(0,0,0));
	recalc_size(window->width, window->height);
}

static void resize_finish(int w, int h) {
	recalc_size(w, h);

	yutani_window_resize_accept(yctx, window, w, h);

	reinit_graphics_yutani(gfx_ctx, window);
	draw_fill(gfx_ctx, rgb(0,0,0));

	if (current_doc) {
		/* redraw the page */
		draw_decors(current_page, current_epage);
		draw_menu();
		drawpage(current_ctx, current_doc, current_page);
	}

	yutani_window_resize_done(yctx, window);
	yutani_flip(yctx, window);
}

static void drawrange(fz_context *ctx, fz_document *doc, char *range) {
	int page, spage, epage, pagecount;
	char *spec, *dash;

	pagecount = fz_count_pages(doc);
	spec = fz_strsep(&range, ",");
	while (spec)
	{
		dash = strchr(spec, '-');

		if (dash == spec)
			spage = epage = pagecount;
		else
			spage = epage = atoi(spec);

		if (dash)
		{
			if (strlen(dash) > 1)
				epage = atoi(dash + 1);
			else
				epage = pagecount;
		}

		spage = fz_clampi(spage, 1, pagecount);
		epage = fz_clampi(epage, 1, pagecount);

		current_doc = doc;
		current_ctx = ctx;
		current_epage = epage;

		for (page = spage; page <= epage; ) {
			if (page == 0) page = 1;

			current_page = page;

			drawpage(ctx, doc, page);
			draw_menu();
			draw_decors(page, epage);

			yutani_flip(yctx, window);

			yutani_msg_t * m = NULL;
			while (1) {
				m = yutani_poll(yctx);
				if (m) {
					if (menu_process_event(yctx, m)) {
						redraw_window_callback(NULL);
					}
					switch (m->type) {
						case YUTANI_MSG_KEY_EVENT:
							{
								struct yutani_msg_key_event * ke = (void*)m->data;
								if (ke->event.action == KEY_ACTION_DOWN) {
									switch (ke->event.keycode) {
										case KEY_ESCAPE:
										case 'q':
											yutani_close(yctx, window);
											exit(0);
											break;
										case KEY_ARROW_LEFT:
										case 'a':
											page--;
											goto _continue;
										case KEY_ARROW_RIGHT:
										case 's':
											page++;
											if (page > epage) page = epage;
											goto _continue;
										case KEY_F12:
											toggle_decorations();
											goto _continue;
										default:
											break;
									}
								}
							}
							break;
						case YUTANI_MSG_SESSION_END:
							yutani_close(yctx, window);
							exit(0);
							break;
						case YUTANI_MSG_WINDOW_FOCUS_CHANGE:
							{
								struct yutani_msg_window_focus_change * wf = (void*)m->data;
								yutani_window_t * win = hashmap_get(yctx->windows, (void*)(uintptr_t)wf->wid);
								if (win) {
									win->focused = wf->focused;
									goto _continue;
								}
							}
							break;
						case YUTANI_MSG_RESIZE_OFFER:
							{
								struct yutani_msg_window_resize * wr = (void*)m->data;
								resize_finish(wr->width, wr->height);
								goto _continue;
							}
							break;
						case YUTANI_MSG_WINDOW_MOUSE_EVENT:
							{
								struct yutani_msg_window_mouse_event * me = (void*)m->data;
								if (me->wid == window->wid) {
									int result = decor_handle_event(yctx, m);
									switch (result) {
										case DECOR_CLOSE:
											return;
										case DECOR_RIGHT:
											/* right click in decoration, show appropriate menu */
											decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
											break;
										default:
											/* Other actions */
											break;
									}
									menu_bar_mouse_event(yctx, window, &menu_bar, me, me->new_x, me->new_y);
								}
							}
							break;
						default:
							break;
					}
				}
				free(m);
			}
_continue:
			free(m);
		}

		spec = fz_strsep(&range, ",");
	}
}

static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser agpl3.trt &");
	redraw_window_callback(NULL);
}

static void _menu_action_exit(struct MenuEntry * entry) {
	exit(0);
}

static void _menu_action_about(struct MenuEntry * entry) {
	/* Show About dialog */
	char about_cmd[1024] = "\0";
	strcat(about_cmd, "about \"About MuPDF\" /usr/share/icons/48/mupdf.png \"MuPDF \" \"© 2006-2012 Artifex Software, Inc.\n-\nMuPDF is Free Software\nreleased under the terms of the\nGNU Affero General Public License.\n-\n%hhttps://www.mupdf.com/license.html\" ");
	char coords[100];
	sprintf(coords, "%d %d &", (int)window->x + (int)window->width / 2, (int)window->y + (int)window->height / 2);
	strcat(about_cmd, coords);
	system(about_cmd);
	redraw_window_callback(NULL);
}


int main(int argc, char **argv) {
	fz_document *doc = NULL;
	int c;
	fz_context *ctx;

	fz_var(doc);

	yctx = yutani_init();

	char * _width  = getenv("WIDTH");
	char * _height = getenv("HEIGHT");
	width  = _width  ? atoi(_width)  : 512;
	height = _height ? atoi(_height) : 512;

	init_decorations();

	menu_bar.entries = menu_entries;
	menu_bar.redraw_callback = redraw_window_callback;

	menu_bar.set = menu_set_create();

	struct MenuList * m = menu_create(); /* File */
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);
	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height + MENU_BAR_HEIGHT);
	yutani_window_move(yctx, window, 50, 50);

	yutani_window_advertise_icon(yctx, window, APPLICATION_TITLE, "mupdf");

	gfx_ctx = init_graphics_yutani(window);
	draw_fill(gfx_ctx,rgb(0,0,0));
	render_decorations(window, gfx_ctx, APPLICATION_TITLE " - Loading...");

	while ((c = fz_getopt(argc, argv, "wf")) != -1) {
		switch (c) {
			case 'f':
				fit = 1;
				break;
		}
	}

	ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	if (!ctx) {
		fprintf(stderr, "Could not initialize fitz context.\n");
		exit(1);
	}

	fz_set_aa_level(ctx, alphabits);
	colorspace = fz_device_rgb;

	fz_try(ctx) {
		while (fz_optind < argc)
		{
			fz_try(ctx)
			{
				filename = argv[fz_optind++];
				files++;

				fz_try(ctx)
				{
					doc = fz_open_document(ctx, filename);
				}
				fz_catch(ctx)
				{
					fz_throw(ctx, "cannot open document: %s", filename);
				}

				if (fz_optind == argc || !isrange(argv[fz_optind]))
					drawrange(ctx, doc, "1-");
				if (fz_optind < argc && isrange(argv[fz_optind]))
					drawrange(ctx, doc, argv[fz_optind++]);

				fz_close_document(doc);
				doc = NULL;
			}
			fz_catch(ctx)
			{
				if (!ignore_errors)
					fz_rethrow(ctx);

				fz_close_document(doc);
				doc = NULL;
				fz_warn(ctx, "ignoring error in '%s'", filename);
			}
		}
	}
	fz_catch(ctx) {
		fz_close_document(doc);
		fprintf(stderr, "error: cannot draw '%s'\n", filename);
		errored = 1;
	}

	fz_free_context(ctx);

	return (errored != 0);
}
