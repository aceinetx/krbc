#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
	OPTIMIZE_NONE = 0,
	OPTIMIZE_MID = 1,
	OPTIMIZE_HIGH = 2,
};

// #region die

void die(const char* s) {
	puts(s);
	abort();
}

void die_perror(const char* s) {
	printf("%s: ", s);
	die(strerror(errno));
}

// #endregion

// #region fs

char* fs_read(const char* filename, size_t* out_size) {
	FILE* file;
	size_t fileSize;
	char* buffer;
	size_t charsRead;

	file = fopen(filename, "rb");
	if (!file) {
		die_perror("fopen");
	}

	if (fseek(file, 0, SEEK_END)) {
		die_perror("fseek");
	}

	fileSize = ftell(file);
	if (fileSize < 0) {
		die_perror("ftell");
	}

	if (fseek(file, 0, SEEK_SET)) {
		die_perror("fseek");
	}

	buffer = (char*)malloc(fileSize + 1);
	if (!buffer) {
		die("out of memory");
	}

	buffer[fileSize] = 0;

	charsRead = fread(buffer, 1, fileSize, file);
	if (charsRead != fileSize) {
		die_perror("fread");
	}

	fclose(file);

	if (out_size) {
		*out_size = (size_t)fileSize;
	}

	return buffer;
}

// #endregion

// #region compiler

enum { OP_NULL = 0, OP_NOP, OP_MOD, OP_MOVE, OP_INPUT, OP_OUTPUT, OP_LOOP_BEGIN, OP_LOOP_END };

struct Op {
	unsigned int type : 3;
	int count : 29;
};

struct Op* ops = NULL;
int opcount = 0;

void emit_fp(FILE* ofd) {
	int i;
	uint32_t loop_stack[128], *loop_p = loop_stack, loop_c = 0;
	struct Op* op = ops;

	fprintf(ofd, "format ELF executable\n");
	fprintf(ofd, "entry _start\n");
	fprintf(ofd, "_start:\n");
	fprintf(ofd, "mov edi, mem\n");
	fprintf(ofd, "mov ebx, 1\n");
	fprintf(ofd, "mov edx, 1\n");
	fprintf(ofd, "; -- code begin\n");

	while (op->type != OP_NULL) {
		switch (op->type) {
		case OP_MOD:
			if (op->count == 1)
				fprintf(ofd, "inc byte [edi]\n");
			else if (op->count == -1)
				fprintf(ofd, "dec byte [edi]\n");
			else if (op->count > 0)
				fprintf(ofd, "add byte [edi], %d\n", op->count);
			else if (op->count < 0)
				fprintf(ofd, "sub byte [edi], %d\n", 0 - op->count);
			break;
		case OP_MOVE:
			if (op->count == 1)
				fprintf(ofd, "inc edi\n");
			else if (op->count == -1)
				fprintf(ofd, "dec edi\n");
			else if (op->count > 0)
				fprintf(ofd, "add edi, %d\n", op->count);
			else if (op->count < 0)
				fprintf(ofd, "sub edi, %d\n", 0 - op->count);
			break;
		case OP_OUTPUT:
			fprintf(ofd, "mov ecx, edi\n");
			for (i = 0; i < op->count; i++) {
				fprintf(ofd, "mov eax, 4\n");
				fprintf(ofd, "int 0x80\n");
			}
			break;
		case OP_INPUT:
			fprintf(ofd, "mov ecx, edi\n");
			for (i = 0; i < op->count; i++) {
				fprintf(ofd, "mov eax, 3\n");
				fprintf(ofd, "int 0x80\n");
			}
			break;
		case OP_LOOP_BEGIN:
			loop_p += 1;
			*loop_p = loop_c++;
			fprintf(ofd, "l%d:\n", *loop_p);
			fprintf(ofd, "cmp byte [edi], 0\n");
			fprintf(ofd, "je e%d\n", *loop_p);
			break;
		case OP_LOOP_END:
			fprintf(ofd, "jmp l%d\n", *loop_p);
			fprintf(ofd, "e%d:\n", *loop_p);
			loop_p -= 1;
			break;
		default:
			break;
		}
		op++;
	}

	fprintf(ofd, "; code end --\n");
	fprintf(ofd, "mov eax, 1\n");
	fprintf(ofd, "mov ebx, 0\n");
	fprintf(ofd, "int 0x80\n");
	fprintf(ofd, "mem: rb 30000\n");

	if (loop_p != loop_stack && loop_c != 0) {
		puts("warning: unmatched loops");
	}
}

void shift(void) {
	int i, shifted = 0;
	for (i = 0; i < opcount; i++) {
		struct Op op = ops[i];
		if (op.type == OP_NOP) {
			if (i == opcount - 1) {
				op.type = OP_NULL;
			} else {
				struct Op* src = ops + i + 1;
				memmove(ops + i, src, (opcount - i + 1) * sizeof ops[0]);
				shifted++;
			}
		} else if (op.type == OP_NULL)
			break;
	}
	opcount -= shifted;
}

int optimize(void) {
	int optimized = 0;
	struct Op* op = ops;
	while (op->type != OP_NULL) {
		switch (op->type) {
		case OP_MOD:
		case OP_MOVE:
		case OP_OUTPUT:
		case OP_INPUT:
			if ((op + 1)->type == op->type) {
				op->count += (op + 1)->count;
				(op + 1)->type = OP_NOP;
				optimized++;
			}
			break;
		}
		op++;
	}
	shift();
	return optimized;
}

void addop(struct Op op) {
	ops[opcount++] = op;
}

void buildops(char* code) {
	char* c = code;
	while (*c) {
		switch (*c) {
		case '+':
			addop((struct Op){OP_MOD, 1});
			break;
		case '-':
			addop((struct Op){OP_MOD, -1});
			break;
		case '>':
			addop((struct Op){OP_MOVE, 1});
			break;
		case '<':
			addop((struct Op){OP_MOVE, -1});
			break;
		case '.':
			addop((struct Op){OP_OUTPUT, 1});
			break;
		case ',':
			addop((struct Op){OP_INPUT, 1});
			break;
		case '[':
			addop((struct Op){OP_LOOP_BEGIN, 0});
			break;
		case ']':
			addop((struct Op){OP_LOOP_END, 0});
			break;
		default:
			break;
		}
		c++;
	}
}

// #endregion

// #region interpreter

typedef void (*interpreter_output_func)(void* userdata, uint8_t cell);
typedef uint8_t (*interpreter_input_func)(void* userdata);

static uint8_t interpret_default_input_func(void* userdata) {
	(void)userdata;
	return getchar();
}

static void interpret_default_output_func(void* userdata, uint8_t cell) {
	(void)userdata;
	putchar(cell);
}

void interpret(void* userdata, interpreter_output_func output_f, interpreter_input_func input_f) {
	if (output_f == NULL)
		output_f = interpret_default_output_func;
	if (input_f == NULL)
		input_f = interpret_default_input_func;

	uint8_t memory[30000];
	size_t pointer = 0;

	memset(memory, 0, sizeof memory);

	// OP_NOP, OP_MOD, OP_MOVE, OP_INPUT, OP_OUTPUT, OP_LOOP_BEGIN, OP_LOOP_END
	for (size_t i = 0;; i++) {
		struct Op op = ops[i];
		if (op.type == OP_NULL)
			break;

		// printf("%u\n", op.type);

		switch (op.type) {
		case OP_NULL:
			break;
		case OP_MOD:
			memory[pointer] += op.count;
			break;
		case OP_MOVE:
			pointer += op.count;
			break;
		case OP_INPUT:
			while (op.count-- > 0)
				memory[pointer] = input_f(userdata);
			break;
		case OP_OUTPUT:
			while (op.count-- > 0)
				output_f(userdata, memory[pointer]);
			break;
		case OP_LOOP_BEGIN:
			if (memory[pointer] == 0) {
				int loops = 0;
				for (size_t j = i;; j++) {
					struct Op op = ops[j];

					if (op.type == OP_NULL)
						die("interpret: unterminated loop");
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
					struct Op op = ops[j];

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

				if (loops != 0)
					die("interpret: ureachable state");
			}
			break;
		}
	}
}

// #endregion

// #region operations
void compile(char* file, int optimize_level) {
	char* code;
	size_t size, ops_size;

	code = fs_read(file, &size);

	ops_size = (size + 1) * sizeof ops[0];
	ops = malloc(ops_size);
	if (!ops) {
		die("out of memory");
	}

	memset(ops, 0, ops_size);

	buildops(code);
	free(code);

	if (optimize_level >= OPTIMIZE_MID)
		while (optimize() > 0)
			;
}

int contains_input_op(void) {
	for (size_t i = 0;; i++) {
		struct Op op = ops[i];
		if (op.type == OP_NULL)
			break;

		if (op.type == OP_INPUT)
			return true;
	}
	return false;
}

size_t emit_data_len;
void emit_data_print_f(void* fp_, uint8_t ch) {
	FILE* fp = fp_;
	fprintf(fp, "%u, ", ch);
	emit_data_len++;
}

void emit(char* output, int optimize_level) {
	FILE* fp = fopen(output, "w");
	if (!fp) {
		die_perror("fopen");
	}

	if (!contains_input_op() && optimize_level >= OPTIMIZE_HIGH) {
		emit_data_len = 0;

		fprintf(fp, "format ELF executable\n");
		fprintf(fp, "dat: db ");
		interpret(fp, emit_data_print_f, NULL);
		fprintf(fp, "0\n");
		fprintf(fp, "entry _start\n");
		fprintf(fp, "_start:\n");
		fprintf(fp, "; -- code begin\n");
		fprintf(fp, "mov eax, 4\n");
		fprintf(fp, "mov ebx, 1\n");
		fprintf(fp, "mov ecx, dat\n");
		fprintf(fp, "mov edx, %zu\n", emit_data_len);
		fprintf(fp, "int 0x80\n");
		fprintf(fp, "; code end --\n");
		fprintf(fp, "mov eax, 1\n");
		fprintf(fp, "mov ebx, 0\n");
		fprintf(fp, "int 0x80\n");
	} else {
		emit_fp(fp);
	}

	fclose(fp);
}
// #endregion

int main(int argc, char** argv) {
	int opt;
	char* output = "a.out";
	bool do_compile = false;
	bool do_run = false;
	bool do_interpret = false;
	int optimize_level = OPTIMIZE_MID;

	while ((opt = getopt(argc, argv, "o:hcriO:")) != -1) {
		switch (opt) {
		case 'i':
			do_interpret = true;
			break;
		case 'r':
			do_run = true;
		case 'c':
			do_compile = true;
			break;
		case 'o':
			output = optarg;
			break;
		case 'O':
			switch (*optarg) {
			case '0':
				optimize_level = OPTIMIZE_NONE;
				break;
			case '1':
				optimize_level = OPTIMIZE_MID;
				break;
			case '2':
				optimize_level = OPTIMIZE_HIGH;
				break;
			default:
				die("invalid optimization level, valid modes are: 0, 1, 2");
			}
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	if (optind >= argc) {
		goto usage;
	}

	compile(argv[optind], optimize_level);

	if (do_interpret) {
		interpret(NULL, NULL, NULL);
		goto cleanup;
	}

	emit(output, optimize_level);

	if (do_compile) {
		char* fasm_command = malloc(strlen(output) * 2 + 16);
		sprintf(fasm_command, "fasm %s %s", output, output);
		system(fasm_command);
		free(fasm_command);

		struct stat st;
		if (stat(output, &st) == -1)
			die_perror("stat");
		mode_t m = st.st_mode & 07777;
		m |= 0111;
		if (chmod(output, m) == -1)
			die_perror("chmod");

		if (do_run) {
			char* real = realpath(output, NULL);

			system(real);

			free(real);
		}
	}

cleanup:
	free(ops);

	return 0;

usage:
	printf("usage: %s [-o output] [-h] [-c] [-r] [-i] [-O] name\n", argv[0]);
	return 1;
}
