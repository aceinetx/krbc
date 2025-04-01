#include <krbc.h>

FILE *ofd;
dword loop_stack[128];
dword *loop_p;
dword loop_c;
char *repeat_start, *repeat_end, *c;

void do_repeat(void) {
  if (repeat_start != NULL && repeat_end != NULL) {
    dword count, i;
    count = repeat_end - repeat_start;
    switch (*repeat_start) {
    case '+':
      if (count == 1) {
        fprintf(ofd, "; +\n");
        fprintf(ofd, "inc byte [edi]\n");
      } else {
        fprintf(ofd, "; +(%d)\n", count);
        fprintf(ofd, "add byte [edi], %d\n", count);
      }
      break;
    case '-':
      if (count == 1) {
        fprintf(ofd, "; -\n");
        fprintf(ofd, "dec byte [edi]\n");
      } else {
        fprintf(ofd, "; -(%d)\n", count);
        fprintf(ofd, "sub byte [edi], %d\n", count);
      }
      break;
    case '>':
      if (count == 1) {
        fprintf(ofd, "; >\n");
        fprintf(ofd, "inc edi\n");
      } else {
        fprintf(ofd, "; >(%d)\n", count);
        fprintf(ofd, "add edi, %d\n", count);
      }
      break;
    case '<':
      if (count == 1) {
        fprintf(ofd, "; <\n");
        fprintf(ofd, "dec edi\n");
      } else {
        fprintf(ofd, "; <(%d)\n", count);
        fprintf(ofd, "sub edi, %d\n", count);
      }
      break;
    case ',':
      fprintf(ofd, "mov ecx, edi\n");
      for (i = 0; i < count; i++) {
        fprintf(ofd, "mov eax, 3\n");
        fprintf(ofd, "int 0x80\n");
      }
      break;
    case '.':
      fprintf(ofd, "mov ecx, edi\n");
      for (i = 0; i < count; i++) {
        fprintf(ofd, "mov eax, 4\n");
        fprintf(ofd, "int 0x80\n");
      }
      break;
    }
    repeat_start = NULL;
    repeat_end = NULL;
  }
}

void do_repeating_op(void) {
  if (repeat_start == NULL)
    repeat_start = c;
  else if (*repeat_start != *c) {
    repeat_end = c;
    do_repeat();
    repeat_start = c;
  }
}

int main(int argc, char **argv) {
  byte *data;

  if (argc < 2) {
    printf("krbc: no filename provided\n");
    return 1;
  }

  data = fs_read(argv[1], NULL);
  if (!data) {
    printf("krbc: no such file or directory\n");
    return 1;
  }
  c = (char *)data;
  ofd = stdout;
  loop_p = loop_stack;
  loop_c = 0;
  repeat_start = NULL;
  repeat_end = NULL;

  fprintf(ofd, "format ELF executable\n");
  fprintf(ofd, "_start:\n");
  fprintf(ofd, "mov edi, mem\n");
  fprintf(ofd, "mov ebx, 1\n");
  fprintf(ofd, "mov edx, 1\n");

  while (*c) {
    switch (*c) {
    case '+':
      do_repeating_op();
      break;
    case '-':
      do_repeating_op();
      break;
    case '>':
      do_repeating_op();
      break;
    case '<':
      do_repeating_op();
      break;
    case ',':
      do_repeating_op();
      break;
    case '.':
      do_repeating_op();
      break;
    case '[':
      repeat_end = c;
      do_repeat();
      loop_p += 1;
      *loop_p = loop_c++;
      fprintf(ofd, "l%d:\n", *loop_p);
      fprintf(ofd, "cmp byte [edi], 0\n");
      fprintf(ofd, "je e%d\n", *loop_p);
      break;
    case ']':
      repeat_end = c;
      do_repeat();
      fprintf(ofd, "jmp l%d\n", *loop_p);
      fprintf(ofd, "e%d:\n", *loop_p);
      loop_p -= 1;
      break;
    default:
      repeat_end = c;
      do_repeat();
      break;
    }

    c++;
  }
  repeat_end = c;
  do_repeat();

  fprintf(ofd, "mov eax, 1\n");
  fprintf(ofd, "mov ebx, 0\n");
  fprintf(ofd, "int 0x80\n");
  fprintf(ofd, "mem: rb 30000\n");

  free(data);

  return 0;
}
