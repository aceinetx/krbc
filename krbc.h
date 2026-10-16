#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	KRBC_OPTIMIZE_NONE = 0,
	KRBC_OPTIMIZE_MID = 1,
	KRBC_OPTIMIZE_HIGH = 2,
};

// #region fs

enum {
	KRBC_FS_READ_OK = 0,
	KRBC_FS_READ_FOPEN,
	KRBC_FS_READ_FSEEK,
	KRBC_FS_READ_FTELL,
	KRBC_FS_READ_FREAD,
	KRBC_FS_READ_MALLOC,
};

int krbc_fs_read(const char* filename, size_t* out_size, char** out_text);
#ifdef KRBC_IMPLEMENTATION
int krbc_fs_read(const char* filename, size_t* out_size, char** out_text) {
	FILE* file;
	size_t fileSize;
	char* buffer;
	size_t charsRead;

	file = fopen(filename, "rb");
	if (!file) {
		return KRBC_FS_READ_OK;
	}

	if (fseek(file, 0, SEEK_END)) {
		fclose(file);
		return KRBC_FS_READ_FSEEK;
	}

	fileSize = ftell(file);
	if (fileSize < 0) {
		fclose(file);
		return KRBC_FS_READ_FTELL;
	}

	if (fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return KRBC_FS_READ_FSEEK;
	}

	buffer = (char*)malloc(fileSize + 1);
	if (!buffer) {
		fclose(file);
		return KRBC_FS_READ_MALLOC;
	}

	buffer[fileSize] = 0;

	charsRead = fread(buffer, 1, fileSize, file);
	if (charsRead != fileSize) {
		free(buffer);
		fclose(file);
		return KRBC_FS_READ_FREAD;
	}

	fclose(file);

	if (out_size) {
		*out_size = (size_t)fileSize;
	}

	*out_text = buffer;
	return KRBC_FS_READ_OK;
}
#endif

// #endregion

// #region op

#define krbc_XOps X(OP_NULL), X(OP_NOP), X(OP_MOD), X(OP_MOVE), X(OP_INPUT), X(OP_OUTPUT), X(OP_LOOP_BEGIN), X(OP_LOOP_END), X(OP_OPT_SET)

#define X(ident) ident
enum { krbc_XOps };
#undef X

extern const char* krbc_op_names[];
#ifdef KRBC_IMPLEMENTATION
#define X(ident) #ident
const char* krbc_op_names[] = {krbc_XOps};
#undef X
#endif

#undef krbc_XOps

typedef struct krbc_Op {
	unsigned int type : 4;
	int payload : 29;
} krbc_Op;

// #endregion

// #region state

#ifdef KRBC_IMPLEMENTATION
typedef struct krbc_State {
	krbc_Op* ops;
	int opcount;
	int optimize_level;
	char report[256];
} krbc_State;
#else
typedef struct krbc_State krbc_State;
#endif

krbc_State* krbc_State_init();
void krbc_State_deinit(krbc_State* state);
const char* krbc_State_get_report(krbc_State* state);

#ifdef KRBC_IMPLEMENTATION
krbc_State* krbc_State_init() {
	krbc_State* state = malloc(sizeof *state);
	memset(state, 0, sizeof *state);
	state->optimize_level = KRBC_OPTIMIZE_MID;
	return state;
}

void krbc_State_deinit(krbc_State* state) {
	if (state->ops)
		free(state->ops);
	memset(state, 0, sizeof *state);
	free(state);
}

const char* krbc_State_get_report(krbc_State* state) {
	return state->report;
}
#endif

// #endregion

// #region debug

void krbc_dump_ops(krbc_State* state);
#ifdef KRBC_IMPLEMENTATION
void krbc_dump_ops(krbc_State* state) {
	struct krbc_Op* op = state->ops;
	puts("ops dump:");

	for (; op->type != OP_NULL; op++) {
		printf("%-15s %5d\n", krbc_op_names[op->type], op->payload);
	}
}
#endif

// #endregion

// #region compiler

#ifdef KRBC_IMPLEMENTATION
static void krbc_shift(krbc_State* state) {
	int i, shifted = 0;
	for (i = 0; i < state->opcount; i++) {
		struct krbc_Op op = state->ops[i];
		if (op.type == OP_NOP) {
			if (i == state->opcount - 1) {
				op.type = OP_NULL;
			} else {
				struct krbc_Op* src = state->ops + i + 1;
				memmove(state->ops + i, src, (state->opcount - i + 1) * sizeof state->ops[0]);
				shifted++;
			}
		} else if (op.type == OP_NULL)
			break;
	}
	state->opcount -= shifted;
}

static int krbc_optimize_once(krbc_State* state) {
	int optimized = 0;
	struct krbc_Op* op = state->ops;
	while (op->type != OP_NULL) {
		switch (op->type) {
		case OP_MOD:
		case OP_MOVE:
		case OP_OUTPUT:
		case OP_INPUT: {
			struct krbc_Op* next = op + 1;
			if (next->type == op->type) {
				op->payload += next->payload;
				next->type = OP_NOP;
				optimized++;
			}

			/* ++++[-] pattern: set cell to 0 */ if (op->type == OP_MOD && (next->type == OP_OPT_SET)) {
				op->type = OP_OPT_SET;
				op->payload = next->payload;

				next->type = OP_NOP;

				optimized++;
			}
		} break;
		case OP_LOOP_BEGIN: {
			/* [-] / [+] pattern: set cell to 0 */ {
				struct krbc_Op* next1 = op + 1;
				struct krbc_Op* next2 = op + 2;
				if (next1->type == OP_MOD && (next1->payload == -1 || next1->payload == 1) && next2->type == OP_LOOP_END) {
					next1->type = OP_NOP;
					next2->type = OP_NOP;
					op->type = OP_OPT_SET;

					optimized++;
				}
			}
		} break;
		case OP_OPT_SET: {
			/* [-][-] pattern: set cell to 0 */ {
				struct krbc_Op* next = op + 1;
				if (next->type == OP_OPT_SET) {
					op->payload = next->payload;
					next->type = OP_NOP;

					optimized++;
				}
			}
		} break;
		}
		op++;
	}
	krbc_shift(state);
	return optimized;
}

static void krbc_addop(krbc_State* state, struct krbc_Op op) {
	state->ops[state->opcount++] = op;
}

static void krbc_buildops(krbc_State* state, char* code) {
	char* c = code;
	while (*c) {
		switch (*c) {
		case '+':
			krbc_addop(state, (struct krbc_Op){OP_MOD, 1});
			break;
		case '-':
			krbc_addop(state, (struct krbc_Op){OP_MOD, -1});
			break;
		case '>':
			krbc_addop(state, (struct krbc_Op){OP_MOVE, 1});
			break;
		case '<':
			krbc_addop(state, (struct krbc_Op){OP_MOVE, -1});
			break;
		case '.':
			krbc_addop(state, (struct krbc_Op){OP_OUTPUT, 1});
			break;
		case ',':
			krbc_addop(state, (struct krbc_Op){OP_INPUT, 1});
			break;
		case '[':
			krbc_addop(state, (struct krbc_Op){OP_LOOP_BEGIN, 0});
			break;
		case ']':
			krbc_addop(state, (struct krbc_Op){OP_LOOP_END, 0});
			break;
		default:
			break;
		}
		c++;
	}
}
#endif

// #endregion

// #region interpreter

typedef void (*krbc_interpreter_output_func)(void* userdata, uint8_t cell);
typedef uint8_t (*krbc_interpreter_input_func)(void* userdata);
void krbc_interpret(krbc_State* state, void* userdata, krbc_interpreter_output_func output_f, krbc_interpreter_input_func input_f);
#ifdef KRBC_IMPLEMENTATION
static uint8_t interpret_default_input_func(void* userdata) {
	(void)userdata;
	return getchar();
}

static void interpret_default_output_func(void* userdata, uint8_t cell) {
	(void)userdata;
	putchar(cell);
}

void krbc_interpret(krbc_State* state, void* userdata, krbc_interpreter_output_func output_f, krbc_interpreter_input_func input_f) {
	if (output_f == NULL)
		output_f = interpret_default_output_func;
	if (input_f == NULL)
		input_f = interpret_default_input_func;

	uint8_t memory[30000];
	size_t pointer = 0;

	memset(memory, 0, sizeof memory);

	// OP_NOP, OP_MOD, OP_MOVE, OP_INPUT, OP_OUTPUT, OP_LOOP_BEGIN, OP_LOOP_END
	for (size_t i = 0;; i++) {
		struct krbc_Op op = state->ops[i];
		if (op.type == OP_NULL)
			break;

		// printf("%u\n", op.type);

		switch (op.type) {
		case OP_NULL:
			break;
		case OP_MOD:
			memory[pointer] += op.payload;
			break;
		case OP_MOVE:
			pointer += op.payload;
			break;
		case OP_INPUT:
			while (op.payload-- > 0)
				memory[pointer] = input_f(userdata);
			break;
		case OP_OUTPUT:
			while (op.payload-- > 0)
				output_f(userdata, memory[pointer]);
			break;
		case OP_LOOP_BEGIN:
			if (memory[pointer] == 0) {
				int loops = 0;
				for (size_t j = i;; j++) {
					struct krbc_Op op = state->ops[j];

					if (op.type == OP_NULL) {
						strcpy(state->report, "unterminated loop");
						return;
					}

					switch (op.type) {
					case OP_LOOP_BEGIN:
						loops++;
						break;
					case OP_LOOP_END:
						loops--;
						break;
					}
					if (loops == 0) {
						i = j;
						break;
					}
				}
			}
			break;
		case OP_LOOP_END:
			if (memory[pointer] != 0) {
				int loops = 0;

				for (size_t j = i; j > 0; j--) {
					struct krbc_Op op = state->ops[j];

					switch (op.type) {
					case OP_LOOP_BEGIN:
						loops--;
						break;
					case OP_LOOP_END:
						loops++;
						break;
					default:
						break;
					}
					if (loops == 0) {
						i = j;
						break;
					}
				}

				if (loops != 0) {
					strcpy(state->report, "unreachable state");
					return;
				}
			}
			break;
		case OP_OPT_SET:
			memory[pointer] = op.payload;
			break;
		}
	}
}
#endif

// #endregion

// #region operations
bool krbc_load(krbc_State* state, int optimize_level, char* code);
void krbc_emit(krbc_State* state, FILE* fp, char* interp_baked_input);

#ifdef KRBC_IMPLEMENTATION
bool krbc_load(krbc_State* state, int optimize_level, char* code) {
	state->optimize_level = optimize_level;
	if (state->ops)
		free(state->ops);

	size_t len = strlen(code);
	size_t size = (len + 16) * sizeof state->ops[0];
	krbc_Op* ops = (krbc_Op*)malloc(size);
	if (!ops)
		return false;

	state->ops = ops;
	memset(state->ops, 0, size);

	krbc_buildops(state, code);

	if (state->optimize_level >= KRBC_OPTIMIZE_MID) {
		while (krbc_optimize_once(state) > 0)
			;
	}

	return true;
}

struct EmitInterpData {
	FILE* output_fp;
	size_t output_dat_len;
	const char* baked_input;
};

static uint8_t emit_interp_input(void* data_) {
	struct EmitInterpData* data = (struct EmitInterpData*)data_;
	if (*data->baked_input) {
		data->baked_input++;
		return data->baked_input[-1];
	} else
		return 0;
}

static void emit_interp_output(void* data_, uint8_t ch) {
	struct EmitInterpData* data = (struct EmitInterpData*)data_;
	fprintf(data->output_fp, "%u, ", ch);
	data->output_dat_len++;
}

static int krbc_contains_input_op(krbc_State* state) {
	for (size_t i = 0;; i++) {
		struct krbc_Op op = state->ops[i];
		if (op.type == OP_NULL)
			break;

		if (op.type == OP_INPUT)
			return true;
	}
	return false;
}

void krbc_emit(krbc_State* state, FILE* fp, char* interp_baked_input) {
	if ((!krbc_contains_input_op(state) || *interp_baked_input) && state->optimize_level >= KRBC_OPTIMIZE_HIGH) {
		struct EmitInterpData data = {fp, 0, interp_baked_input};

		fprintf(fp, "format ELF executable\n");
		fprintf(fp, "dat: db ");
		krbc_interpret(state, &data, emit_interp_output, emit_interp_input);
		fprintf(fp, "0\n");
		fprintf(fp, "entry _start\n");
		fprintf(fp, "_start:\n");
		fprintf(fp, "; -- code begin\n");
		fprintf(fp, "mov eax, 4\n");
		fprintf(fp, "mov ebx, 1\n");
		fprintf(fp, "mov ecx, dat\n");
		fprintf(fp, "mov edx, %zu\n", data.output_dat_len);
		fprintf(fp, "int 0x80\n");
		fprintf(fp, "; code end --\n");
		fprintf(fp, "mov eax, 1\n");
		fprintf(fp, "mov ebx, 0\n");
		fprintf(fp, "int 0x80\n");
	} else {
		int i;
		uint32_t loop_stack[128], *loop_p = loop_stack, loop_c = 0;
		struct krbc_Op* op = state->ops;

		fprintf(fp, "format ELF executable\n");
		fprintf(fp, "entry _start\n");
		fprintf(fp, "_start:\n");
		fprintf(fp, "mov edi, mem\n");
		fprintf(fp, "mov ebx, 1\n");
		fprintf(fp, "mov edx, 1\n");
		fprintf(fp, "; -- code begin\n");

		while (op->type != OP_NULL) {
			switch (op->type) {
			case OP_MOD:
				if (op->payload == 1)
					fprintf(fp, "inc byte [edi]\n");
				else if (op->payload == -1)
					fprintf(fp, "dec byte [edi]\n");
				else if (op->payload > 0)
					fprintf(fp, "add byte [edi], %d\n", op->payload);
				else if (op->payload < 0)
					fprintf(fp, "sub byte [edi], %d\n", 0 - op->payload);
				break;
			case OP_MOVE:
				if (op->payload == 1)
					fprintf(fp, "inc edi\n");
				else if (op->payload == -1)
					fprintf(fp, "dec edi\n");
				else if (op->payload > 0)
					fprintf(fp, "add edi, %d\n", op->payload);
				else if (op->payload < 0)
					fprintf(fp, "sub edi, %d\n", 0 - op->payload);
				break;
			case OP_OUTPUT:
				fprintf(fp, "mov ecx, edi\n");
				for (i = 0; i < op->payload; i++) {
					fprintf(fp, "mov eax, 4\n");
					fprintf(fp, "int 0x80\n");
				}
				break;
			case OP_INPUT:
				fprintf(fp, "mov ecx, edi\n");
				for (i = 0; i < op->payload; i++) {
					fprintf(fp, "mov eax, 3\n");
					fprintf(fp, "int 0x80\n");
				}
				break;
			case OP_LOOP_BEGIN:
				loop_p += 1;
				*loop_p = loop_c++;
				fprintf(fp, "l%d:\n", *loop_p);
				fprintf(fp, "cmp byte [edi], 0\n");
				fprintf(fp, "je e%d\n", *loop_p);
				break;
			case OP_LOOP_END:
				fprintf(fp, "jmp l%d\n", *loop_p);
				fprintf(fp, "e%d:\n", *loop_p);
				loop_p -= 1;
				break;
			case OP_OPT_SET:
				fprintf(fp, "mov byte [edi], %d\n", op->payload);
				break;
			default:
				break;
			}
			op++;
		}

		fprintf(fp, "; code end --\n");
		fprintf(fp, "mov eax, 1\n");
		fprintf(fp, "mov ebx, 0\n");
		fprintf(fp, "int 0x80\n");
		fprintf(fp, "mem: rb 30000\n");

		if (loop_p != loop_stack && loop_c != 0) {
			strcpy(state->report, "warning: unmatched loops");
		}
	}
}
#endif

// #endregion
