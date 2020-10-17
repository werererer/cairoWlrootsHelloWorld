#define _POSIX_C_SOURCE 200112L
#include <GLES2/gl2.h>
#include <getopt.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "cat.h"
#include <cairo/cairo.h>

struct sample_state {
    struct wl_display *display;
    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_list link;
    struct timespec last_frame;
    struct wlr_renderer *renderer;
    enum wl_output_transform transform;
};

struct sample_keyboard {
    struct sample_state *sample;
    struct wlr_input_device *device;
    struct wl_listener key;
    struct wl_listener destroy;
};

static void output_frame_notify(struct wl_listener *listener, void *data) {
    struct sample_state *sample = wl_container_of(listener, sample, frame);
    struct wlr_output *wlr_output = data;

    int32_t width, height;
    wlr_output_effective_resolution(wlr_output, &width, &height);
    cairo_format_t cFormat = CAIRO_FORMAT_ARGB32;

    cairo_surface_t *cSurface =
        cairo_image_surface_create(cFormat, width, height);

    cairo_t *cr = cairo_create(cSurface);
    cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
    cairo_set_line_width(cr, 1);

    cairo_rectangle(cr, 0, 0, 100, 100);
    cairo_set_source_rgba(cr, 0.0, 0.0, 1.0, 1.0);
    cairo_fill(cr);
    cairo_surface_flush(cSurface);
    int stride = cairo_format_stride_for_width(cFormat, width);
    unsigned char *cData = cairo_image_surface_get_data(cSurface);

    /* After calling this every kind of wlr_render_... will fail with the same error*/
    struct wlr_texture *texture = wlr_texture_from_pixels(
        sample->renderer, WL_SHM_FORMAT_ARGB8888, stride, width, height, cData);

    wlr_output_attach_render(wlr_output, NULL);
    wlr_renderer_begin(sample->renderer, wlr_output->width, wlr_output->height);
    wlr_renderer_clear(sample->renderer, (float[]){0.25f, 0.25f, 0.25f, 1});
    cairo_destroy(cr);
    cairo_surface_destroy(cSurface);

    float color[] = {0.0f, 0.0f, 0.0f, 0.1f};

    wlr_render_texture(sample->renderer, texture,
            wlr_output->transform_matrix, 0, 0, 1.0f);

    wlr_renderer_end(sample->renderer);
    wlr_output_commit(wlr_output);
}

static void output_remove_notify(struct wl_listener *listener, void *data) {
    struct sample_state *sample = wl_container_of(listener, sample, destroy);
    wl_list_remove(&sample->frame.link);
    wl_list_remove(&sample->destroy.link);
}

static void new_output_notify(struct wl_listener *listener, void *data) {
    struct wlr_output *output = data;
    struct sample_state *sample = wl_container_of(listener, sample, new_output);
    if (!wl_list_empty(&output->modes)) {
        struct wlr_output_mode *mode = wl_container_of(output->modes.prev, mode, link);
        wlr_output_set_mode(output, mode);
    }
    wlr_output_set_transform(output, sample->transform);
    wl_signal_add(&output->events.frame, &sample->frame);
    sample->frame.notify = output_frame_notify;
    wl_signal_add(&output->events.destroy, &sample->destroy);
    sample->destroy.notify = output_remove_notify;

    wlr_output_commit(output);
}

static void keyboard_key_notify(struct wl_listener *listener, void *data) {
    struct sample_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct sample_state *sample = keyboard->sample;
    struct wlr_event_keyboard_key *event = data;
    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->device->keyboard->xkb_state,
            keycode, &syms);
    for (int i = 0; i < nsyms; i++) {
        xkb_keysym_t sym = syms[i];
        if (sym == XKB_KEY_Escape) {
            wl_display_terminate(sample->display);
        }
    }
}

static void keyboard_destroy_notify(struct wl_listener *listener, void *data) {
    struct sample_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->key.link);
    free(keyboard);
}

static void new_input_notify(struct wl_listener *listener, void *data) {
    struct wlr_input_device *device = data;
    struct sample_state *sample = wl_container_of(listener, sample, new_input);
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:;
        struct sample_keyboard *keyboard = calloc(1, sizeof(struct sample_keyboard));
        keyboard->device = device;
        keyboard->sample = sample;
        wl_signal_add(&device->events.destroy, &keyboard->destroy);
        keyboard->destroy.notify = keyboard_destroy_notify;
        wl_signal_add(&device->keyboard->events.key, &keyboard->key);
        keyboard->key.notify = keyboard_key_notify;
        struct xkb_rule_names rules = { 0 };
        rules.rules = getenv("XKB_DEFAULT_RULES");
        rules.model = getenv("XKB_DEFAULT_MODEL");
        rules.layout = getenv("XKB_DEFAULT_LAYOUT");
        rules.variant = getenv("XKB_DEFAULT_VARIANT");
        rules.options = getenv("XKB_DEFAULT_OPTIONS");
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) {
            wlr_log(WLR_ERROR, "Failed to create XKB context");
            exit(1);
        }
        struct xkb_keymap *keymap = xkb_map_new_from_names(context, &rules,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (!keymap) {
            wlr_log(WLR_ERROR, "Failed to create XKB keymap");
            exit(1);
        }
        wlr_keyboard_set_keymap(device->keyboard, keymap);
        xkb_keymap_unref(keymap);
        xkb_context_unref(context);
        break;
    default:
        break;
    }
}


int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);
    struct wl_display *display = wl_display_create();
    struct sample_state state = {
        .display = display,
        .transform = WL_OUTPUT_TRANSFORM_NORMAL,
    };

    struct wlr_backend *wlr = wlr_backend_autocreate(display, NULL);
    if (!wlr) {
        exit(1);
    }

    wl_signal_add(&wlr->events.new_output, &state.new_output);
    state.new_output.notify = new_output_notify;
    wl_signal_add(&wlr->events.new_input, &state.new_input);
    state.new_input.notify = new_input_notify;
    clock_gettime(CLOCK_MONOTONIC, &state.last_frame);

    state.renderer = wlr_backend_get_renderer(wlr);
    if (!state.renderer) {
        wlr_log(WLR_ERROR, "Could not start compositor, OOM");
        wlr_backend_destroy(wlr);
        exit(EXIT_FAILURE);
    }

    if (!wlr_backend_start(wlr)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        wlr_backend_destroy(wlr);
        exit(1);
    }
    wl_display_run(display);

    wl_display_destroy(display);
}
