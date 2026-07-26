#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t byte;
typedef uint32_t dword;

byte* fs_read(const char* filename, dword* out_size) {
	FILE* file;
	long fileSize;
	byte* buffer;
	dword bytesRead;

	file = fopen(filename, "rb");
	if (!file) {
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (fileSize < 0) {
		fclose(file);
		return NULL;
	}

	buffer = (byte*)malloc(fileSize + 1);
	buffer[fileSize] = 0;
	if (!buffer) {
		fclose(file);
		return NULL;
	}

	bytesRead = fread(buffer, 1, fileSize, file);
	if (bytesRead != fileSize) {
		free(buffer);
		fclose(file);
		return NULL;
	}

	fclose(file);

	if (out_size) {
		*out_size = (dword)fileSize;
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
	dword loop_stack[128], *loop_p = loop_stack, loop_c = 0;
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

void buildops(byte* code) {
	byte* c = code;
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
	byte* code;
	dword size, ops_size;

	if (argc < 2) {
		printf("krbc: no filename provided\n");
		return 1;
	}

	code = fs_read(argv[1], &size);
	if (!code) {
		printf("krbc: no such file or directory\n");
		return 1;
	}

	ops_size = (size + 1) * sizeof ops[0];
	ops = malloc(ops_size);
	memset(ops, 0, ops_size);

	buildops(code);
	free(code);

	while (optimize() > 0)
		;

	build(stdout);
	free(ops);

	return 0;
}
