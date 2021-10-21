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

#include <sys/wait.h>
#include <fitz.h>

#include <toaru/yutani.h>
#include <toaru/graphics.h>
#include <toaru/decorations.h>
#include <toaru/menu.h>

#include "badmath.c"

#define APPLICATION_TITLE "MuPDF"
#define NAV_BAR_HEIGHT 0 //36

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

static char * docname = NULL;
static float resolution = 72;
static int res_specified = 0;
static float rotation = 0;

static int width = 0;
static int height = 0;
static int fit = 0;
static int toggle_decors = 1;

static int end_page = 0;
static int current_page  = 0;
static int current_epage = 0;
static fz_document * current_doc = NULL;
static fz_context  * current_ctx = NULL;

void draw_decors(int page, int epage) {
	if (toggle_decors) {
		char title[512] = {0};
		if (current_doc) {
			sprintf(title, "%s - Page %d of %d", docname ? docname : APPLICATION_TITLE, page, epage);
		} else {
			sprintf(title, "%s", APPLICATION_TITLE);
		}
		render_decorations(window, gfx_ctx, title);
	}
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

static void drawpage(fz_context *ctx, fz_document *doc, int pagenum) {
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
		fz_throw(ctx, "cannot load page %d in file", pagenum);
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
		pix = fz_new_pixmap_with_bbox(ctx, fz_device_bgr, bbox);

		fz_clear_pixmap_with_value(ctx, pix, 255);

		dev = fz_new_draw_device(ctx, pix);
		if (list)
			fz_run_display_list(list, dev, ctm, bbox, &cookie);
		else
			fz_run_page(doc, page, dev, ctm, &cookie);
		fz_free_device(dev);
		dev = NULL;

		size_t x_offset = (width - fz_pixmap_width(ctx, pix)) / 2;
		size_t y_offset = (height - fz_pixmap_height(ctx, pix)) / 2;;
		if (toggle_decors) {
			struct decor_bounds bounds;
			decor_get_bounds(window, &bounds);
			x_offset += bounds.left_width;
			y_offset += bounds.top_height + MENU_BAR_HEIGHT + NAV_BAR_HEIGHT;
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
}

static void fitz_load_file(char * filename) {
	if (current_doc) {
		fz_close_document(current_doc);
		current_doc = NULL;
	}
	if (docname) {
		free(docname);
		docname = NULL;
	}
	fz_try(current_ctx) {
		current_doc = fz_open_document(current_ctx, filename);
	} fz_catch(current_ctx) {
		fprintf(stderr, "Failed to load document.\n");
		fz_close_document(current_doc);
		current_doc = NULL;
	}

	if (current_doc) {

		char * s = strrchr(filename,'/');
		if (s) {
			docname = strdup(s+1);
		} else {
			docname = strdup(filename);
		}

		current_page = 1;
		end_page = fz_count_pages(current_doc);
	}
}

static void redraw_window(void) {
	if (current_doc) {
		drawpage(current_ctx, current_doc, current_page);
	} else {
		draw_fill(gfx_ctx, rgb(127,127,127));
	}
	draw_decors(current_page, end_page);
	draw_menu();
	yutani_flip(yctx, window);
}

static void redraw_window_callback(struct menu_bar * self) {
	redraw_window();
}

void recalc_size(int w, int h) {
	if (!toggle_decors) {
		width = w;
		height = h;
	} else {
		struct decor_bounds bounds;
		decor_get_bounds(window, &bounds);
		width  = w - bounds.left_width - bounds.right_width;
		height = h - bounds.top_height - bounds.bottom_height - MENU_BAR_HEIGHT - NAV_BAR_HEIGHT;
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

static void _menu_action_help(struct MenuEntry * entry) {
	/* show help documentation */
	system("help-browser agpl3.trt &");
	redraw_window();
}

static void _menu_action_open(struct MenuEntry * entry) {
	int pipe_fds[2];
	pipe(pipe_fds);
	pid_t child_pid = fork();
	if (!child_pid) {
		dup2(pipe_fds[1], STDOUT_FILENO);
		close(pipe_fds[0]);
		execvp("file-browser",(char*[]){"file-browser","--picker",NULL});
		exit(123);
		return;
	}

	close(pipe_fds[1]);
	char buf[1024];
	size_t accum = 0;

	do {
		int r = read(pipe_fds[0], buf+accum, 1023-accum);
		if (r == 0) break;
		if (r < 0) {
			return;
		}
		accum += r;
	} while (accum < 1023);

	waitpid(child_pid, NULL, WNOHANG);
	buf[accum] = '\0';
	if (accum && buf[accum-1] == '\n') {
		buf[accum-1] = '\0';
	}

	if (!buf[0]) {
		redraw_window();
		return;
	}

	fprintf(stderr, "opening doc '%s'\n", buf);
	fitz_load_file(buf);
	redraw_window();
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
	redraw_window();
}

static void previous_page(void) {
	current_page--;
	if (current_page == 0) current_page = 1;
	redraw_window();
}

static void next_page(void) {
	current_page++;
	if (current_page > end_page) current_page = end_page;
	redraw_window();
}

int main(int argc, char **argv) {
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
	menu_insert(m, menu_create_normal("open",NULL,"Open", _menu_action_open));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("exit",NULL,"Exit", _menu_action_exit));
	menu_set_insert(menu_bar.set, "file", m);

	m = menu_create();
	menu_insert(m, menu_create_normal("help",NULL,"Contents",_menu_action_help));
	menu_insert(m, menu_create_separator());
	menu_insert(m, menu_create_normal("star",NULL,"About " APPLICATION_TITLE,_menu_action_about));
	menu_set_insert(menu_bar.set, "help", m);

	struct decor_bounds bounds;
	decor_get_bounds(NULL, &bounds);
	window = yutani_window_create(yctx, width + bounds.width, height + bounds.height + MENU_BAR_HEIGHT + NAV_BAR_HEIGHT);
	yutani_window_move(yctx, window, 50, 50);
	yutani_window_advertise_icon(yctx, window, APPLICATION_TITLE, "mupdf");

	gfx_ctx = init_graphics_yutani(window);
	draw_fill(gfx_ctx,rgb(0,0,0));
	render_decorations(window, gfx_ctx, APPLICATION_TITLE " - Loading...");

	/* Configure Fitz */
	current_ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
	fz_set_aa_level(current_ctx, 8);

	/* Load a file */
	if (argc > 1) {
		fitz_load_file(argv[1]);
	}

	/* Draw once */
	redraw_window();

	while (1) {
		yutani_msg_t * m = yutani_poll(yctx);
		if (m) {
			if (menu_process_event(yctx, m)) {
				redraw_window();
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
									previous_page();
									break;
								case KEY_ARROW_RIGHT:
								case 's':
									next_page();
									break;
								case KEY_F12:
									toggle_decorations();
									break;
								default:
									break;
							}
						}
					}
					break;
				case YUTANI_MSG_WINDOW_CLOSE:
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
							redraw_window();
							break;
						}
					}
					break;
				case YUTANI_MSG_RESIZE_OFFER:
					{
						struct yutani_msg_window_resize * wr = (void*)m->data;
						resize_finish(wr->width, wr->height);
						break;
					}
					break;
				case YUTANI_MSG_WINDOW_MOUSE_EVENT:
					{
						struct yutani_msg_window_mouse_event * me = (void*)m->data;
						if (me->wid == window->wid) {
							int result = decor_handle_event(yctx, m);
							switch (result) {
								case DECOR_CLOSE:
									exit(0);
									break;
								case DECOR_RIGHT:
									/* right click in decoration, show appropriate menu */
									decor_show_default_menu(window, window->x + me->new_x, window->y + me->new_y);
									break;
								default:
									/* Other actions */
									break;
							}
							menu_bar_mouse_event(yctx, window, &menu_bar, me, me->new_x, me->new_y);

							/* Use scroll to switch pages */
							if (me->buttons & YUTANI_MOUSE_SCROLL_UP) {
								previous_page();
							} else if (me->buttons & YUTANI_MOUSE_SCROLL_DOWN) {
								next_page();
							}
						}
					}
					break;
				default:
					break;
			}
		}
		free(m);
	}

	if (current_doc) {
		fz_close_document(current_doc);
	}
	fz_free_context(current_ctx);

	return 0;
}
