// #define KRBC_IMPLEMENTATION
#include "krbc.h"

int main() {
	krbc_State state = krbc_State_init();
	krbc_load(&state, "++++++++++[>+>+++>+++++++>++++++++++<<<<-]>>>++.>+.+++++++..+++.<<++.>+++++++++++++++.>.+++.------.--------.<<+.<.");
	krbc_emit(&state, stdout, "");

	krbc_State_deinit(&state);
}

/*
void die(const char* s) {
	puts(s);
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

	while ((opt = getopt(argc, argv, "o:hcridO:b:")) != -1) {
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

	compile(argv[optind], optimize_level, do_dump_ops);

	if (do_interpret) {
		interpret(NULL, NULL, NULL);
		goto cleanup;
	}

	emit(output, optimize_level, interp_baked_input);

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
	printf("usage: %s [-o output] [-h] [-c] [-r] [-i] [-O] [-d] [-b] name\n", argv[0]);
	return 1;
}
*/
