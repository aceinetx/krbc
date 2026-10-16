#define KRBC_IMPLEMENTATION
#include "krbc.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>

void die(const char* s) {
	printf("krbc: %s\n", s);
	abort();
}

void die_perror(const char* s) {
	printf("%s: ", s);
	die(strerror(errno));
}

int main(int argc, char** argv) {
	int opt;
	char* output = "a.out";
	bool do_compile = false;
	bool do_run = false;
	bool do_interpret = false;
	bool do_dump_ops = false;
	int optimize_level = KRBC_OPTIMIZE_MID;
	char* interp_baked_input = "";

	// ----------------------------------------------------------
	// parse args

	while ((opt = getopt(argc, argv, "o:hcidO:b:")) != -1) {
		switch (opt) {
		case 'i':
			do_interpret = true;
			break;
		case 'c':
			do_compile = true;
			break;
		case 'o':
			output = optarg;
			break;
		case 'b':
			interp_baked_input = optarg;
			break;
		case 'd':
			do_dump_ops = true;
			break;
		case 'O':
			if (!optarg) {
				die("expected optimization level");
			}

			switch (*optarg) {
			case '0':
				optimize_level = KRBC_OPTIMIZE_NONE;
				break;
			case '1':
				optimize_level = KRBC_OPTIMIZE_MID;
				break;
			case '2':
				optimize_level = KRBC_OPTIMIZE_HIGH;
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

	// ----------------------------------------------------------
	// emit
	char* code;
	if (krbc_fs_read(argv[optind], NULL, &code) != KRBC_FS_READ_OK) {
		puts("krbc: file read failed");
		return 1;
	}

	krbc_State* state = krbc_State_init();

	krbc_load(state, optimize_level, code);
	if (do_dump_ops) {
		krbc_dump_ops(state);
	}

	if (do_interpret) {
		krbc_interpret(state, NULL, NULL, NULL);
		krbc_State_deinit(state);
		return 1;
	}

	FILE* fp = fopen(output, "w");
	if (!fp) {
		puts("krbc: output file open failed");
		krbc_State_deinit(state);
		return 1;
	}

	krbc_emit(state, fp, interp_baked_input);

	const char* report = krbc_State_get_report(state);
	if (*report) {
		printf("krbc: report: %s\n", report);
	}

	fclose(fp);

	// ----------------------------------------------------------
	// compile
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
	}

	return 0;

usage:
	printf("usage: %s [-o output] [-h] [-c] [-i] [-O] [-d] [-b] name\n", argv[0]);
	return 1;
}
