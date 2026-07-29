#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void die(const char* s) {
	puts(s);
	abort();
}

void die_perror(const char* s) {
	printf("%s: ", s);
	die(strerror(errno));
}

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

enum { OP_NULL = 0, OP_NOP, OP_MOD, OP_MOVE, OP_INPUT, OP_OUTPUT, OP_LOOP_BEGIN, OP_LOOP_END };

struct Op {
	int type;
	int count;
};

struct Op* ops = NULL;
int opcount = 0;

void build(FILE* ofd) {
	int i;
	uint32_t loop_stack[128], *loop_p = loop_stack, loop_c = 0;
	struct Op* op = ops;

	fprintf(ofd, "format ELF executable\n");
	fprintf(ofd, "_start:\n");
	fprintf(ofd, "mov edi, mem\n");
	fprintf(ofd, "mov ebx, 1\n");
	fprintf(ofd, "mov edx, 1\n");
	fprintf(ofd, "; -- code begin\n");

	while (op->type != OP_NULL) {
		switch (op->type) {
		case OP_MOD:
			if (op->count > 0)
				fprintf(ofd, "add byte [edi], %d\n", op->count);
			else if (op->count < 0)
				fprintf(ofd, "sub byte [edi], %d\n", 0 - op->count);
			break;
		case OP_MOVE:
			if (op->count > 0)
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

int main(int argc, char** argv) {
	char* code;
	size_t size, ops_size;
	int opt;
	char* output = "a.out";
	bool compile = false;

	while ((opt = getopt(argc, argv, "o:hc")) != -1) {
		switch (opt) {
		case 'c':
			compile = true;
			break;
		case 'o':
			output = optarg;
			break;
		case 'h':
		default:
			goto usage;
		}
	}

	if (optind >= argc) {
		goto usage;
	}

	code = fs_read(argv[optind], &size);

	ops_size = (size + 1) * sizeof ops[0];
	ops = malloc(ops_size);
	if (!ops) {
		die("out of memory");
	}

	memset(ops, 0, ops_size);

	buildops(code);
	free(code);

	while (optimize() > 0)
		;

	FILE* fp = fopen(output, "w");
	if (!fp) {
		die_perror("fopen");
	}

	build(fp);

	fclose(fp);

	free(ops);

	if (compile) {
		pid_t pid = execlp("fasm", output, output, NULL);
		waitpid(pid, NULL, WIFEXITED(0));
		struct stat st;
		if (stat(output, &st) == -1)
			die_perror("stat");
		mode_t m = st.st_mode & 07777;
		m |= 0111;
		if (chmod(output, m) == -1)
			die_perror("chmod");
	}

	return 0;

usage:
	printf("usage: %s [-o output] [-h] [-c] name\n", argv[0]);
	return 1;
}
