#define _XOPEN_SOURCE 500
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <json-c/json.h>
#include "sway/commands.h"
#include "stringop.h"
#include "log.h"

struct cmd_handler {
	char *command;
	sway_cmd *handle;
};

// Returns error object, or NULL if check succeeds.
struct cmd_results *checkarg(int argc, const char *name, enum expected_args type, int val) {
	struct cmd_results *error = NULL;
	switch (type) {
	case EXPECTED_MORE_THAN:
		if (argc > val) {
			return NULL;
		}
		error = cmd_results_new(CMD_INVALID, name, "Invalid %s command "
			"(expected more than %d argument%s, got %d)",
			name, val, (char*[2]){"s", ""}[argc==1], argc);
		break;
	case EXPECTED_AT_LEAST:
		if (argc >= val) {
			return NULL;
		}
		error = cmd_results_new(CMD_INVALID, name, "Invalid %s command "
			"(expected at least %d argument%s, got %d)",
			name, val, (char*[2]){"s", ""}[argc==1], argc);
		break;
	case EXPECTED_LESS_THAN:
		if (argc  < val) {
			return NULL;
		};
		error = cmd_results_new(CMD_INVALID, name, "Invalid %s command "
			"(expected less than %d argument%s, got %d)",
			name, val, (char*[2]){"s", ""}[argc==1], argc);
		break;
	case EXPECTED_EQUAL_TO:
		if (argc == val) {
			return NULL;
		};
		error = cmd_results_new(CMD_INVALID, name, "Invalid %s command "
			"(expected %d arguments, got %d)", name, val, argc);
		break;
	}
	return error;
}

/**
 * Check and add color to buffer.
 *
 * return error object, or NULL if color is valid.
 */
struct cmd_results *add_color(const char *name, char *buffer, const char *color) {
	int len = strlen(color);
	if (len != 7 && len != 9) {
		return cmd_results_new(CMD_INVALID, name, "Invalid color definition %s", color);
	}

	if (color[0] != '#') {
		return cmd_results_new(CMD_INVALID, name, "Invalid color definition %s", color);
	}

	int i;
	for (i = 1; i < len; ++i) {
		if (!isxdigit(color[i])) {
			return cmd_results_new(CMD_INVALID, name, "Invalid color definition %s", color);
		}
	}

	// copy color to buffer
	strncpy(buffer, color, len);
	// add default alpha channel if color was defined without it
	if (len == 7) {
		buffer[7] = 'f';
		buffer[8] = 'f';
	}
	buffer[9] = '\0';

	return NULL;
}

/* Keep alphabetized */
static struct cmd_handler handlers[] = {
	{ "exit", cmd_exit },
};

static int handler_compare(const void *_a, const void *_b) {
	const struct cmd_handler *a = _a;
	const struct cmd_handler *b = _b;
	return strcasecmp(a->command, b->command);
}

static struct cmd_handler *find_handler(char *line, enum cmd_status block) {
	struct cmd_handler d = { .command=line };
	struct cmd_handler *res = NULL;
	sway_log(L_DEBUG, "find_handler(%s) %d", line, block == CMD_BLOCK_INPUT);
	/* TODO
	if (block == CMD_BLOCK_BAR) {
		res = bsearch(&d, bar_handlers,
			sizeof(bar_handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	} else if (block == CMD_BLOCK_BAR_COLORS){
		res = bsearch(&d, bar_colors_handlers,
			sizeof(bar_colors_handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	} else if (block == CMD_BLOCK_INPUT) {
		res = bsearch(&d, input_handlers,
			sizeof(input_handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	} else if (block == CMD_BLOCK_IPC) {
		res = bsearch(&d, ipc_handlers,
			sizeof(ipc_handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	} else if (block == CMD_BLOCK_IPC_EVENTS) {
		res = bsearch(&d, ipc_event_handlers,
			sizeof(ipc_event_handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	} else {
	*/
		res = bsearch(&d, handlers,
			sizeof(handlers) / sizeof(struct cmd_handler),
			sizeof(struct cmd_handler), handler_compare);
	//}
	return res;
}

struct cmd_results *handle_command(char *_exec) {
	// Even though this function will process multiple commands we will only
	// return the last error, if any (for now). (Since we have access to an
	// error string we could e.g. concatonate all errors there.)
	struct cmd_results *results = NULL;
	char *exec = strdup(_exec);
	char *head = exec;
	char *cmdlist;
	char *cmd;

	head = exec;
	do {
		// Split command list
		cmdlist = argsep(&head, ";");
		cmdlist += strspn(cmdlist, whitespace);
		do {
			// Split commands
			cmd = argsep(&cmdlist, ",");
			cmd += strspn(cmd, whitespace);
			if (strcmp(cmd, "") == 0) {
				sway_log(L_INFO, "Ignoring empty command.");
				continue;
			}
			sway_log(L_INFO, "Handling command '%s'", cmd);
			//TODO better handling of argv
			int argc;
			char **argv = split_args(cmd, &argc);
			if (strcmp(argv[0], "exec") != 0) {
				int i;
				for (i = 1; i < argc; ++i) {
					if (*argv[i] == '\"' || *argv[i] == '\'') {
						strip_quotes(argv[i]);
					}
				}
			}
			struct cmd_handler *handler = find_handler(argv[0], CMD_BLOCK_END);
			if (!handler) {
				if (results) {
					free_cmd_results(results);
				}
				results = cmd_results_new(CMD_INVALID, cmd, "Unknown/invalid command");
				free_argv(argc, argv);
				goto cleanup;
			}
			free_argv(argc, argv);
		} while(cmdlist);
	} while(head);
cleanup:
	free(exec);
	if (!results) {
		results = cmd_results_new(CMD_SUCCESS, NULL, NULL);
	}
	return results;
}

struct cmd_results *cmd_results_new(enum cmd_status status,
		const char *input, const char *format, ...) {
	struct cmd_results *results = malloc(sizeof(struct cmd_results));
	if (!results) {
		sway_log(L_ERROR, "Unable to allocate command results");
		return NULL;
	}
	results->status = status;
	if (input) {
		results->input = strdup(input); // input is the command name
	} else {
		results->input = NULL;
	}
	if (format) {
		char *error = malloc(256);
		va_list args;
		va_start(args, format);
		if (error) {
			vsnprintf(error, 256, format, args);
		}
		va_end(args);
		results->error = error;
	} else {
		results->error = NULL;
	}
	return results;
}

void free_cmd_results(struct cmd_results *results) {
	if (results->input) {
		free(results->input);
	}
	if (results->error) {
		free(results->error);
	}
	free(results);
}

const char *cmd_results_to_json(struct cmd_results *results) {
	json_object *result_array = json_object_new_array();
	json_object *root = json_object_new_object();
	json_object_object_add(root, "success",
			json_object_new_boolean(results->status == CMD_SUCCESS));
	if (results->input) {
		json_object_object_add(
				root, "input", json_object_new_string(results->input));
	}
	if (results->error) {
		json_object_object_add(
				root, "error", json_object_new_string(results->error));
	}
	json_object_array_add(result_array, root);
	const char *json = json_object_to_json_string(result_array);
	free(result_array);
	free(root);
	return json;
}