#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wayland-client-protocol.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

/* Shared memory support code */
static void randname(char* buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int allocate_shm_file(size_t size) {
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Wayland code */
struct client_state {
    /* Globals */
    struct wl_display* wl_display;
    struct wl_registry* wl_registry;
    struct wl_shm* wl_shm;
    struct wl_compositor* wl_compositor;
    struct zwlr_layer_shell_v1* zwlr_layer_shell_v1;
    struct wl_output* wl_output;
    /* Objects */
    struct wl_surface* wl_surface;
    struct zwlr_layer_surface_v1* zwlr_layer_surface_v1;

    // data
    uint8_t* image_data;
    int width;
    int height;
    uint8_t* scaled_image_data;
    int target_width;
    int target_height;

    char* output_name;
};

static struct client_state* g_state;

static void wl_buffer_release(void* data, struct wl_buffer* wl_buffer) {
    (void)data;
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

static struct wl_buffer* draw_frame(struct client_state* state) {
    printf("[lwr] drawing frame\n");
    int stride = state->target_width * 4;
    int size = stride * state->target_height;

    int fd = allocate_shm_file(size);
    if (fd == -1) {
        return NULL;
    }

    uint32_t* data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool* pool = wl_shm_create_pool(state->wl_shm, fd, size);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(
        pool,
        0,
        state->target_width,
        state->target_height,
        stride,
        WL_SHM_FORMAT_ARGB8888
    );
    wl_shm_pool_destroy(pool);
    close(fd);

    /* Draw image */
    for (int y = 0; y < state->target_height; ++y) {
        for (int x = 0; x < state->target_width; ++x) {
            uint8_t* pixel = state->scaled_image_data + (y * state->target_width + x) * 4;
            uint32_t color = (pixel[3] << 24) | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
            data[y * state->target_width + x] = color;
        }
    }

    munmap(data, size);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
}

static void zwlr_layer_surface_configure(
    void* data,
    struct zwlr_layer_surface_v1* zwlr_layer_surface_v1,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    struct client_state* state = data;
    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface_v1, serial);
    zwlr_layer_surface_v1_set_size(zwlr_layer_surface_v1, width, height);

    struct wl_buffer* buffer = draw_frame(state);
    wl_surface_attach(state->wl_surface, buffer, 0, 0);
    wl_surface_commit(state->wl_surface);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = zwlr_layer_surface_configure,
};

static void wl_output_name(void* data, struct wl_output* wl_output, const char* name) {
    struct client_state* state = data;
    printf("[lwr] output name: %s\n", name);
    if (state->output_name != NULL && strcmp(name, state->output_name) == 0) {
        state->wl_output = wl_output;
    }
}

// we need to fill in all the fields, but we only care about the name

static void
wl_output_description(void* data, struct wl_output* wl_output, const char* description) {
    (void)data;
    (void)wl_output;
    (void)description;
}

static void wl_output_done(void* data, struct wl_output* wl_output) {
    (void)data;
    (void)wl_output;
}

static void wl_output_scale(void* data, struct wl_output* wl_output, int32_t scale) {
    (void)data;
    (void)wl_output;
    (void)scale;
}

static void wl_output_geometry(
    void* data,
    struct wl_output* wl_output,
    int32_t x,
    int32_t y,
    int32_t physical_width,
    int32_t physical_height,
    int32_t subpixel,
    const char* make,
    const char* model,
    int32_t transform
) {
    (void)data;
    (void)wl_output;
    (void)x;
    (void)y;
    (void)physical_width;
    (void)physical_height;
    (void)subpixel;
    (void)make;
    (void)model;
    (void)transform;
}

static void wl_output_mode(
    void* data,
    struct wl_output* wl_output,
    uint32_t flags,
    int32_t width,
    int32_t height,
    int32_t refresh
) {
    (void)data;
    (void)wl_output;
    (void)flags;
    (void)width;
    (void)height;
    (void)refresh;
}

static const struct wl_output_listener wl_output_listener = {
    .name = wl_output_name,
    .description = wl_output_description,
    .done = wl_output_done,
    .scale = wl_output_scale,
    .geometry = wl_output_geometry,
    .mode = wl_output_mode,
};

static void registry_global(
    void* data,
    struct wl_registry* wl_registry,
    uint32_t name,
    const char* interface,
    uint32_t version
) {
    (void)version;
    struct client_state* state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        state->zwlr_layer_shell_v1 =
            wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output* wl_output =
            wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_add_listener(wl_output, &wl_output_listener, state);
    }
}

static void registry_global_remove(void* data, struct wl_registry* wl_registry, uint32_t name) {
    (void)data;
    (void)wl_registry;
    (void)name;
    /* This space deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static void cleanup(void) {
    // get state
    struct client_state* state = g_state;

    zwlr_layer_surface_v1_destroy(state->zwlr_layer_surface_v1);
    wl_surface_destroy(state->wl_surface);
    zwlr_layer_shell_v1_destroy(state->zwlr_layer_shell_v1);
    wl_compositor_destroy(state->wl_compositor);
    wl_shm_destroy(state->wl_shm);
    wl_registry_destroy(state->wl_registry);
    wl_display_disconnect(state->wl_display);

    if (state->scaled_image_data != state->image_data)
        free(state->scaled_image_data);

    stbi_image_free(state->image_data);
}

// signal handler
static void signal_cleanup(int sig) {
    printf("[lwr] received signal %d\n", sig);

    if (sig == SIGINT || sig == SIGTERM) {
        printf("[lwr] exiting\n");
    }
    cleanup();
    exit(0);
}

typedef struct args {
    char* image_path;
    int target_width;
    int target_height;
    int margin;
    enum zwlr_layer_surface_v1_anchor anchor;
    char* output_name;
} args_t;

void usage(char* argv[]) {
    printf(
        "%s " PROJECT_VERSION "\n"
        "get an overlay of your choice on your wayland compositor\n"
        "\n"
        "Usage: %s <path> [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  -w, --width <width>              set the width of the overlay\n"
        "                                   default: image width\n"
        "  -h, --height <height>            set the height of the overlay\n"
        "                                   default: image height\n"
        "  -m, --margin <margin>            set the margin of the overlay\n"
        "                                   default: 0\n"
        "  -a, --anchor <anchor>:<anchor>   set the anchors of the overlay\n"
        "                                   (top|middle|bottom):(left|middle|right)\n"
        "                                   default: top:left\n"
        "  -o, --output <output>            set the output of the overlay\n"
        "                                   default: NULL\n"
        "\n"
        "Example:\n"
        "  %s /path/to/image.png -w 240 -m 8 -a top:middle\n",
        argv[0],
        argv[0],
        argv[0]
    );
}

args_t args_parse(int argc, char* argv[]) {
    args_t args = {
        .image_path = NULL,
        .target_width = 0,
        .target_height = 0,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
        .margin = 0,
        .output_name = NULL,
    };
    if (argc < 2) {
        usage(argv);
        exit(1);
    }

    args.image_path = argv[1];

    // check if file exists
    if (access(args.image_path, F_OK) == -1) {
        printf("[lwr] error: file %s does not exist\n", args.image_path);
        exit(1);
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--width") == 0) {
            args.target_width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--height") == 0) {
            args.target_height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--margin") == 0) {
            args.margin = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            char* output = argv[++i];
            args.output_name = output;
        } else if (strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--anchor") == 0) {
            char* anchor = argv[++i];
            if (strcmp(anchor, "top:left") == 0) {
                args.anchor =
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            } else if (strcmp(anchor, "top:middle") == 0) {
                args.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(anchor, "top:right") == 0) {
                args.anchor =
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(anchor, "middle:left") == 0) {
                args.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            } else if (strcmp(anchor, "middle:middle") == 0) {
                args.anchor =
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(anchor, "middle:right") == 0) {
                args.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(anchor, "bottom:left") == 0) {
                args.anchor =
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            } else if (strcmp(anchor, "bottom:middle") == 0) {
                args.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else if (strcmp(anchor, "bottom:right") == 0) {
                args.anchor =
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            } else {
                usage(argv);
                exit(1);
            }
        } else {
            usage(argv);
            exit(1);
        }
    }
    return args;
}

int main(int argc, char* argv[]) {
    args_t args = args_parse(argc, argv);

    struct client_state state = { 0 };
    g_state = &state;

    // register signal handler
    if (signal(SIGINT, signal_cleanup) == SIG_ERR) {
        printf("[lwr] error: unable to register signal handler\n");
        exit(1);
    }

    if (signal(SIGTERM, signal_cleanup) == SIG_ERR) {
        printf("[lwr] error: unable to register signal handler\n");
        exit(1);
    }

    state.image_data = stbi_load(args.image_path, &state.width, &state.height, NULL, 4);

    printf("[lwr] loading image %s (%dx%d)\n", args.image_path, state.width, state.height);

    if (args.target_width == 0 && args.target_height == 0) {
        args.target_width = state.width;
        args.target_height = state.height;
    } else if (args.target_width == 0) {
        args.target_width =
            (int)((float)args.target_height * (float)state.width / state.height);
    } else if (args.target_height == 0) {
        args.target_height =
            (int)((float)args.target_width * (float)state.height / state.width);
    }

    if (args.target_width != state.width || args.target_height != state.height) {
        printf(
            "[lwr] resizing image %s (%dx%d) -> (%dx%d)\n",
            args.image_path,
            state.width,
            state.height,
            args.target_width,
            args.target_height
        );
        state.scaled_image_data = malloc(args.target_width * args.target_height * 4);
        if (state.scaled_image_data == NULL) {
            exit(1);
        }
        stbir_resize_uint8_srgb(
            state.image_data,
            state.width,
            state.height,
            0,
            state.scaled_image_data,
            args.target_width,
            args.target_height,
            0,
            STBIR_RGBA_PM
        );
    } else {
        state.scaled_image_data = state.image_data;
    }

    state.target_width = args.target_width;
    state.target_height = args.target_height;

    if (args.output_name != NULL) {
        state.output_name = args.output_name;
    }

    state.wl_display = wl_display_connect(NULL);
    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    struct wl_region* region = wl_compositor_create_region(state.wl_compositor);
    wl_surface_set_input_region(state.wl_surface, region);
    wl_region_destroy(region);

    struct wl_output* output = NULL;

    if (args.output_name != NULL) {
        wl_display_dispatch(state.wl_display);

        output = state.wl_output;
        if (output == NULL) {
            printf("[lwr] error: output %s not found\n", args.output_name);
            exit(1);
        }
    }

    state.zwlr_layer_surface_v1 = zwlr_layer_shell_v1_get_layer_surface(
        state.zwlr_layer_shell_v1,
        state.wl_surface,
        output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        PROJECT_NAME
    );
    zwlr_layer_surface_v1_set_size(
        state.zwlr_layer_surface_v1,
        args.target_width,
        args.target_height
    );
    zwlr_layer_surface_v1_set_anchor(state.zwlr_layer_surface_v1, args.anchor);
    zwlr_layer_surface_v1_set_margin(
        state.zwlr_layer_surface_v1,
        args.margin,
        args.margin,
        args.margin,
        args.margin
    );
    zwlr_layer_surface_v1_set_keyboard_interactivity(state.zwlr_layer_surface_v1, 0);
    zwlr_layer_surface_v1_add_listener(
        state.zwlr_layer_surface_v1,
        &layer_surface_listener,
        &state
    );

    wl_surface_commit(state.wl_surface);

    while (wl_display_dispatch(state.wl_display)) {
        /* This space deliberately left blank */
    }

    return 0;
}
