#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include "wlr-output-management-unstable-v1-client-protocol.h"

struct randr_state;
struct randr_head;

struct randr_mode {
	struct randr_head *head;
	struct zwlr_output_mode_v1 *wlr_mode;
	struct wl_list link;

	int32_t width, height;
	int32_t refresh; // mHz
	bool preferred;
};

struct randr_head {
	struct randr_state *state;
	struct zwlr_output_head_v1 *wlr_head;
	struct wl_list link;

	char *name, *description;
	int32_t phys_width, phys_height; // mm
	struct wl_list modes;

	bool enabled;
	struct randr_mode *mode;
	struct {
		int32_t width, height;
		int32_t refresh;
	} custom_mode;
	int32_t x, y;
	enum wl_output_transform transform;
	double scale;
};

struct randr_state {
	struct zwlr_output_manager_v1 *output_manager;

	struct wl_list heads;
	uint32_t serial;
	bool running;
	bool failed;
};

static const char *output_transform_map[] = {
	[WL_OUTPUT_TRANSFORM_NORMAL] = "normal",
	[WL_OUTPUT_TRANSFORM_90] = "90",
	[WL_OUTPUT_TRANSFORM_180] = "180",
	[WL_OUTPUT_TRANSFORM_270] = "270",
	[WL_OUTPUT_TRANSFORM_FLIPPED] = "flipped",
	[WL_OUTPUT_TRANSFORM_FLIPPED_90] = "flipped-90",
	[WL_OUTPUT_TRANSFORM_FLIPPED_180] = "flipped-180",
	[WL_OUTPUT_TRANSFORM_FLIPPED_270] = "flipped-270",
};

static void print_state(struct randr_state *state) {
	struct randr_head *head;
	wl_list_for_each(head, &state->heads, link) {
		printf("%s \"%s\"\n", head->name, head->description);
		if (head->phys_width > 0 && head->phys_height > 0) {
			printf("  Physical size: %dx%d mm\n",
				head->phys_width, head->phys_height);
		}
		printf("  Enabled: %s\n", head->enabled ? "yes" : "no");
		if (!wl_list_empty(&head->modes)) {
			printf("  Modes:\n");
			struct randr_mode *mode;
			wl_list_for_each(mode, &head->modes, link) {
				printf("    %dx%d px", mode->width, mode->height);
				if (mode->refresh > 0) {
					printf(", %f Hz", (float)mode->refresh / 1000);
				}
				bool current = head->mode == mode;
				if (current || mode->preferred) {
					printf(" (");
					if (mode->preferred) {
						printf("preferred");
					}
					if (current && mode->preferred) {
						printf(", ");
					}
					if (current) {
						printf("current");
					}
					printf(")");
				}
				printf("\n");
			}
		}

		if (!head->enabled) {
			continue;
		}

		printf("  Position: %d,%d\n", head->x, head->y);
		printf("  Transform: %s\n", output_transform_map[head->transform]);
		printf("  Scale: %f\n", head->scale);
	}

	state->running = false;
}

static void config_handle_succeeded(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
	zwlr_output_configuration_v1_destroy(config);
	state->running = false;
}

static void config_handle_failed(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
	zwlr_output_configuration_v1_destroy(config);
	state->running = false;
	state->failed = true;

	fprintf(stderr, "failed to apply configuration\n");
}

static void config_handle_cancelled(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct randr_state *state = data;
	zwlr_output_configuration_v1_destroy(config);
	state->running = false;
	state->failed = true;

	fprintf(stderr, "configuration cancelled, please try again\n");
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
	.succeeded = config_handle_succeeded,
	.failed = config_handle_failed,
	.cancelled = config_handle_cancelled,
};

static void apply_state(struct randr_state *state) {
	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(state->output_manager,
		state->serial);
	zwlr_output_configuration_v1_add_listener(config, &config_listener, state);

	struct randr_head *head;
	wl_list_for_each(head, &state->heads, link) {
		if (!head->enabled) {
			zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
			continue;
		}

		struct zwlr_output_configuration_head_v1 *config_head =
			zwlr_output_configuration_v1_enable_head(config, head->wlr_head);
		if (head->mode != NULL) {
			zwlr_output_configuration_head_v1_set_mode(config_head,
				head->mode->wlr_mode);
		} else {
			zwlr_output_configuration_head_v1_set_custom_mode(config_head,
				head->custom_mode.width, head->custom_mode.height,
				head->custom_mode.refresh);
		}
		zwlr_output_configuration_head_v1_set_position(config_head,
			head->x, head->y);
		zwlr_output_configuration_head_v1_set_transform(config_head,
			head->transform);
		zwlr_output_configuration_head_v1_set_scale(config_head,
			wl_fixed_from_double(head->scale));
	}

	zwlr_output_configuration_v1_apply(config);
}

static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *wlr_mode,
		int32_t width, int32_t height) {
	struct randr_mode *mode = data;
	mode->width = width;
	mode->height = height;
}

static void mode_handle_refresh(void *data,
		struct zwlr_output_mode_v1 *wlr_mode, int32_t refresh) {
	struct randr_mode *mode = data;
	mode->refresh = refresh;
}

static void mode_handle_preferred(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_mode *mode = data;
	mode->preferred = true;
}

static void mode_handle_finished(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_mode *mode = data;
	wl_list_remove(&mode->link);
	zwlr_output_mode_v1_destroy(mode->wlr_mode);
	free(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	.size = mode_handle_size,
	.refresh = mode_handle_refresh,
	.preferred = mode_handle_preferred,
	.finished = mode_handle_finished,
};

static void head_handle_name(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *name) {
	struct randr_head *head = data;
	head->name = strdup(name);
}

static void head_handle_description(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *description) {
	struct randr_head *head = data;
	head->description = strdup(description);
}

static void head_handle_physical_size(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height) {
	struct randr_head *head = data;
	head->phys_width = width;
	head->phys_height = height;
}

static void head_handle_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_head *head = data;

	struct randr_mode *mode = calloc(1, sizeof(*mode));
	mode->head = head;
	mode->wlr_mode = wlr_mode;
	wl_list_insert(&head->modes, &mode->link);

	zwlr_output_mode_v1_add_listener(wlr_mode, &mode_listener, mode);
}

static void head_handle_enabled(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t enabled) {
	struct randr_head *head = data;
	head->enabled = !!enabled;
	if (!enabled) {
		head->mode = NULL;
	}
}

static void head_handle_current_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct randr_head *head = data;
	struct randr_mode *mode;
	wl_list_for_each(mode, &head->modes, link) {
		if (mode->wlr_mode == wlr_mode) {
			head->mode = mode;
			return;
		}
	}
	fprintf(stderr, "received unknown current_mode\n");
	head->mode = NULL;
}

static void head_handle_position(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
	struct randr_head *head = data;
	head->x = x;
	head->y = y;
}

static void head_handle_transform(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t transform) {
	struct randr_head *head = data;
	head->transform = transform;
}

static void head_handle_scale(void *data,
		struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale) {
	struct randr_head *head = data;
	head->scale = wl_fixed_to_double(scale);
}

static void head_handle_finished(void *data,
		struct zwlr_output_head_v1 *wlr_head) {
	struct randr_head *head = data;
	wl_list_remove(&head->link);
	zwlr_output_head_v1_destroy(head->wlr_head);
	free(head->name);
	free(head->description);
	free(head);
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = head_handle_name,
	.description = head_handle_description,
	.physical_size = head_handle_physical_size,
	.mode = head_handle_mode,
	.enabled = head_handle_enabled,
	.current_mode = head_handle_current_mode,
	.position = head_handle_position,
	.transform = head_handle_transform,
	.scale = head_handle_scale,
	.finished = head_handle_finished,
};

static void output_manager_handle_head(void *data,
		struct zwlr_output_manager_v1 *manager,
		struct zwlr_output_head_v1 *wlr_head) {
	struct randr_state *state = data;

	struct randr_head *head = calloc(1, sizeof(*head));
	head->state = state;
	head->wlr_head = wlr_head;
	wl_list_init(&head->modes);
	wl_list_insert(&state->heads, &head->link);

	zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
		struct zwlr_output_manager_v1 *manager, uint32_t serial) {
	struct randr_state *state = data;
	state->serial = serial;
}

static void output_manager_handle_finished(void *data,
		struct zwlr_output_manager_v1 *manager) {
	// This space is intentionally left blank
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
	.head = output_manager_handle_head,
	.done = output_manager_handle_done,
	.finished = output_manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct randr_state *state = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		state->output_manager = wl_registry_bind(registry, name,
			&zwlr_output_manager_v1_interface, 1);
		zwlr_output_manager_v1_add_listener(state->output_manager,
			&output_manager_listener, state);
	}
}

static void registry_handle_global_remove(void *data,
		struct wl_registry *registry, uint32_t name) {
	// This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static const struct option long_options[] = {
	{"help", no_argument, 0, 'h'},
	{"output", required_argument, 0, 0},
	{"on", no_argument, 0, 0},
	{"off", no_argument, 0, 0},
	{"mode", required_argument, 0, 0},
	{"custom-mode", required_argument, 0, 0},
	{"pos", required_argument, 0, 0},
	{"transform", required_argument, 0, 0},
	{"scale", required_argument, 0, 0},
	{0},
};

static bool parse_mode(const char *value, int *width, int *height,
		int *refresh) {
	*refresh = 0;

	// width + "x" + height
	char *cur = (char *)value;
	char *end;
	*width = strtol(cur, &end, 10);
	if (end[0] != 'x' || cur == end) {
		fprintf(stderr, "invalid mode: invalid width: %s\n", value);
		return false;
	}

	cur = end + 1;
	*height = strtol(cur, &end, 10);
	if (cur == end) {
		fprintf(stderr, "invalid mode: invalid height: %s\n", value);
		return false;
	}
	if (end[0] != '\0') {
		// whitespace + "px"
		cur = end;
		while (cur[0] == ' ') {
			cur++;
		}
		if (strncmp(cur, "px", 2) == 0) {
			cur += 2;
		}

		if (cur[0] != '\0') {
			// ("," or "@") + whitespace + refresh
			if (cur[0] == ',' || cur[0] == '@') {
				cur++;
			} else {
				fprintf(stderr, "invalid mode: expected refresh rate: %s\n",
					value);
				return false;
			}
			while (cur[0] == ' ') {
				cur++;
			}
			double refresh_hz = strtod(cur, &end);
			if ((end[0] != '\0' && strcmp(end, "Hz") != 0) ||
					cur == end || refresh_hz <= 0) {
				fprintf(stderr, "invalid mode: invalid refresh rate: %s\n",
					value);
				return false;
			}

			*refresh = refresh_hz * 1000; // Hz → mHz
		}
	}

	return true;
}

static bool parse_output_arg(struct randr_head *head,
		const char *name, const char *value) {
	if (strcmp(name, "on") == 0) {
		head->enabled = true;
	} else if (strcmp(name, "off") == 0) {
		head->enabled = false;
	} else if (strcmp(name, "mode") == 0) {
		int width, height, refresh;
		if (!parse_mode(value, &width, &height, &refresh)) {
			return false;
		}

		bool found = false;
		struct randr_mode *mode;
		wl_list_for_each(mode, &head->modes, link) {
			if (mode->width == width && mode->height == height &&
					(refresh == 0 || mode->refresh == refresh)) {
				found = true;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "unknown mode: %s\n", value);
			return false;
		}

		head->mode = mode;
		head->custom_mode.width = 0;
		head->custom_mode.height = 0;
		head->custom_mode.refresh = 0;
	} else if (strcmp(name, "custom-mode") == 0) {
		int width, height, refresh;
		if (!parse_mode(value, &width, &height, &refresh)) {
			return false;
		}

		head->mode = NULL;
		head->custom_mode.width = width;
		head->custom_mode.height = height;
		head->custom_mode.refresh = refresh;
	} else if (strcmp(name, "pos") == 0) {
		char *cur = (char *)value;
		char *end;
		int x = strtol(cur, &end, 10);
		if (end[0] != ',' || cur == end) {
			fprintf(stderr, "invalid position: %s\n", value);
			return false;
		}

		cur = end + 1;
		int y = strtol(cur, &end, 10);
		if (end[0] != '\0') {
			fprintf(stderr, "invalid position: %s\n", value);
			return false;
		}

		head->x = x;
		head->y = y;
	} else if (strcmp(name, "transform") == 0) {
		bool found = false;
		size_t len =
			sizeof(output_transform_map) / sizeof(output_transform_map[0]);
		for (size_t i = 0; i < len; ++i) {
			if (strcmp(output_transform_map[i], value) == 0) {
				found = true;
				head->transform = i;
				break;
			}
		}

		if (!found) {
			fprintf(stderr, "invalid transform: %s\n", value);
			return false;
		}
	} else if (strcmp(name, "scale") == 0) {
		char *end;
		double scale = strtod(value, &end);
		if (end[0] != '\0' || value == end) {
			fprintf(stderr, "invalid scale: %s\n", value);
			return false;
		}

		head->scale = scale;
	} else {
		fprintf(stderr, "invalid option: %s\n", name);
		return false;
	}

	return true;
}

static const char usage[] =
	"usage: wlr-randr [options…]\n"
	"--help\n"
	"--output <name>\n"
	"  --on\n"
	"  --off\n"
	"  --mode|--custom-mode <width>x<height>[@<refresh>Hz]\n"
	"  --pos <x>,<y>\n"
	"  --transform normal|90|180|270|flipped|flipped-90|flipped-180|flipped-270\n"
	"  --scale <factor>\n";

int main(int argc, char *argv[]) {
	struct randr_state state = { .running = true };
	wl_list_init(&state.heads);

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to connect to display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (state.output_manager == NULL) {
		fprintf(stderr, "compositor doesn't support "
			"wlr-output-management-unstable-v1\n");
		return EXIT_FAILURE;
	}

	while (state.serial == 0) {
		if (wl_display_dispatch(display) < 0) {
			fprintf(stderr, "wl_display_dispatch failed\n");
			return EXIT_FAILURE;
		}
	}

	bool changed = false;
	struct randr_head *current_head = NULL;
	while (1) {
		int option_index = -1;
		int c = getopt_long(argc, argv, "h", long_options, &option_index);
		if (c < 0) {
			break;
		} else if (c == '?') {
			return EXIT_FAILURE;
		} else if (c == 'h') {
			fprintf(stderr, "%s", usage);
			return EXIT_SUCCESS;
		}

		const char *name = long_options[option_index].name;
		const char *value = optarg;
		if (strcmp(name, "output") == 0) {
			bool found = false;
			wl_list_for_each(current_head, &state.heads, link) {
				if (strcmp(current_head->name, value) == 0) {
					found = true;
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "unknown output %s\n", value);
				return EXIT_FAILURE;
			}
			continue;
		}

		if (current_head == NULL) {
			fprintf(stderr, "no --output specified before --%s\n", name);
			return EXIT_FAILURE;
		}

		if (!parse_output_arg(current_head, name, value)) {
			return EXIT_FAILURE;
		}

		changed = true;
	}

	if (changed) {
		apply_state(&state);
	} else {
		print_state(&state);
	}

	while (state.running && wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}

	// TODO: destroy heads
	zwlr_output_manager_v1_destroy(state.output_manager);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);

	return state.failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
