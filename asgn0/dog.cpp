#include <cstdint>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SIZE 32768

void myStdInput(char *buf) {
  ssize_t n_char = 0;
  ssize_t endN_char = 0;
  while ((n_char = read(0, buf, SIZE)) != 0) {
    endN_char = write(1, buf, n_char);
    if (n_char != endN_char) { // i/o error
      warnx("write error: Input/output error");
    }
  }
}

void readFile(char *argv[], int curIndex, char *buf) {
  int64_t fd = 0; // signed
  ssize_t n_char = 0;
  ssize_t endN_char = 0;
  fd = open(argv[curIndex], O_RDONLY);
  if (fd == -1) { // open error
    warnx("%s: %s", argv[curIndex], strerror(errno));
    return;
  }
  while ((n_char = read(fd, buf, SIZE)) != 0) {
    if (n_char == -1) { // read error
      warnx("%s: %s", argv[curIndex], strerror(errno));
      break;
    }
    endN_char = write(1, buf, n_char);
    if (n_char != endN_char) { // io error
      warnx("write error: Input/output error");
    }
  }
  close(fd);
}

int main(int argc, char **argv) {

  char buffer[SIZE];

  const char dash[] = "-";
  if (argc == 1) {
    myStdInput(buffer);
  }
  // loop thru all arguments
  for (size_t i = 1; i < size_t(argc); i++) {
    if (strncmp(argv[i], dash, 2) == 0) {
      myStdInput(buffer);
    } else {
      readFile(argv, i, buffer);
    }
  }
  return 0;
}