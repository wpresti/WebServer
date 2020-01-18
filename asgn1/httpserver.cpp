#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h> //read only
#include <inttypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#define PORT 80

void error(char *msg) {
  perror(msg);
  printf("\n\n\nError function\n\n");
}

bool isValidName(char *headerStr) {
  int64_t fileLen = strlen(headerStr);
  if (fileLen != 27) {
    return false;
  }
  printf("strLen is...%ld\n", fileLen);
  for (size_t i = 0; i < strlen(headerStr); i++) {
    printf("%c\n", headerStr[i]);
    if ((headerStr[i] >= 'a' && headerStr[i] <= 'z') ||
        (headerStr[i] >= 'A' && headerStr[i] <= 'Z') ||
        (headerStr[i] >= '0' && headerStr[i] <= '9') || (headerStr[i] == '-') ||
        headerStr[i] == '_') {
      printf("is valid char ");

    } else {
      printf("NOT valid char ");
      return false;
    }
  }

  return true;
}

/**
 * Get the size of a file.
 * @return The filesize, or 0 if the file does not exist.
 */
size_t getFilesize(int filename) {
  struct stat st;
  if (fstat(filename, &st) != 0) {
    return 0;
  }
  return st.st_size;
}

void putReq(int newsockfd, char *fileName, int64_t len) {
  bool CreatedFile = false;
  int testO = open(fileName, O_RDONLY);
  if (testO < 0) {
    CreatedFile = true;
  }
  int fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC,
                0777); // S_IRWXG -- ins't 777 the ame as sirwxg???
  // check for no permission??
  int64_t curPosFile = 0;
  int64_t contentLength = len;
  char buf[11];
  ssize_t written = 0;
  ssize_t recved = 0;
  printf("fd is..: %d", fd);
  while (curPosFile < contentLength) { // <= !!!!!!!!!!!!
    recved = recv(newsockfd, buf, 10, 0);
    // printf("\ncurpos:%ld\n",curPosFile);
    if (recved < 0) {

      dprintf(newsockfd, "500 Internal Server Error\r\n");
      close(newsockfd);
      close(fd);
      return;
    }

    // printf("buf: %s", buf);
    curPosFile += recved;
    written = write(fd, buf, recved);
    if (written < 0) {
      printf("crashed write\n");
      dprintf(newsockfd, "500 Internal Server Error\r\n");
      close(fd);
      close(newsockfd);
      return;
    }
  }
  printf("ending Put req\n");
  if (CreatedFile == true) {
    dprintf(newsockfd, "HTTP/1.1 201 Created\r\n");

  } else {
    dprintf(newsockfd, "HTTP/1.1 200 OK\r\n");
  }

  // int n = dprintf(newsockfd, "HTTP/1.1 200 OK\r\n"); // newsockfd -- print to
  // stderr
  // add content length underneath respon header

  close(fd);
  close(newsockfd);
}

void getReq(int newsockfd, char *fileName) {
  printf("inside getreq()\n");
  // int64_t n;
  ssize_t fd;
  fd = open(fileName, O_RDONLY, S_IRWXG);
  if (fd < 0) {
    printf("can't find file-- open is -1 :(\n");
    if (errno == EACCES) {
      printf("\naccess denied son \n");
      dprintf(newsockfd, "HTTP/1.1 403\r\n");
      close(newsockfd);
      return;
    } else {
      printf("\nU have access son\n");
    }
    send(newsockfd, "HTTP/1.1 404\r\n", 25, 0); // prob wrong...
    close(newsockfd);
    // send error to curl... still to do
    return;
  }
  size_t fs = getFilesize(fd);
  char buf[11];
  size_t sent, n_char, curPosFile = 0;
  // dprintf(needovkgf, "HTTP/1.1 200 OK\r\n")
  // dprintf(newsockfd,"Content-Length: %d\r\n\r\n",fs);

  dprintf(newsockfd, "HTTP/1.1 200 OK\r\n");
  dprintf(newsockfd, "Content-Length: %ld\r\n\r\n", fs);
  // file contents coming now
  while (curPosFile < fs) {
    n_char = read(fd, buf, 10);
    if (n_char == 0) {

      break; // change /update content length to amtnt sent??
    }
    if (n_char < 0) {
      dprintf(newsockfd, "500 Internal Server Error\r\n");
      break;
    }

    curPosFile += n_char;
    sent = send(newsockfd, buf, n_char, 0);
    if (sent < 0) {
      dprintf(newsockfd, "500 Internal Server Error\r\n");
      break;
    }
  }
  close(fd);
  // n = dprintf(newsockfd, "HTTP/1.1 200\r\n");
  // dprintf(newsockfd, "Content-Length: %ld\r\n\r\n",fs); // newsockfd -- print
  // to stderr
  close(newsockfd); // close current request
  printf("ending getReq function\n");
}

// --------------------MAIN-------------------

int main(int argc, char const *argv[]) {
  bool ipNPort;
  if (argc == 3) {
    // ip and port
    printf("ip and port given in args \n");
    ipNPort = true;
  } else if (argc == 2) {
    // just ip and default PORT 80
    ipNPort = false;
    printf("just ip givenin args \n");
  } else {
    // nothing given
    printf("invalid arguments\n"); // do i quit too?
    exit(1);
  }
  int server_fd;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  char buffer[1024] = {0};

  int n, newsockfd;

  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Forcefully attaching socket to the port 8080
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;

  if (strcmp(argv[1], "localhost") == 0) {
    address.sin_addr.s_addr = INADDR_ANY;
  } else {
    address.sin_addr.s_addr = inet_addr(argv[1]);
  }

  if (ipNPort == true) {
    address.sin_port = htons(atoi(argv[2]));
  } else {
    address.sin_port = htons(PORT);
  }

  // Forcefully attaching socket to the port 8080
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 3) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  while (1) {
    if ((newsockfd = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t *)&addrlen)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }
    // int newsockfd = accept(server_fd, (struct sockaddr *)&address, (socklen_t
    // *)&addrlen);
    bzero(buffer, 1024);
    n = read(newsockfd, buffer, 255);
    if (n < 0) {
      printf("fucked up");
      close(newsockfd);
      continue;
    }

    printf("Here is the message: %s\n", buffer);

    char req[256];
    char fname[256];

    char *split_request;

    int64_t conLen = 0;
    sscanf(buffer, "%s %s", req, fname);
    printf("\n\n\nreq: %s... fname: %s...", req, fname);

    bool validFile = isValidName(fname);
    // validFile = 1;//get rid of this
    printf("File is valid?: %d\n", validFile);

    split_request = strtok(buffer, "\r\n");
    while (split_request != NULL) {
      sscanf(split_request, "Content-Length:%ld", &conLen);

      split_request = strtok(NULL, "\r\n");
    }
    printf("GOT HERE BITCH");
    printf("\n\nParsed Length %ld\n", conLen);
    if (validFile == 0) { // uncoment continue and dprintf
      // 400 error -- length or bad characters
      printf("\nFile not valid!\n");
      dprintf(newsockfd, "HTTP/1.1 400 Bad Request\r\n");
      close(newsockfd);
      continue;
    }
    // call function - get or put
    if (strcmp("PUT", req) == 0 && validFile != 0) {
      printf("PUT REQ woohoo\n");
      putReq(newsockfd, fname, conLen);
    } else if (strcmp("GET", req) == 0 && validFile != 0) {
      printf("GET REQ wohoo\n");
      getReq(newsockfd, fname);
    } else {
      // other req not supported
      // 500
      dprintf(newsockfd, "HTTP/1.1 400 Bad Request\r\n");
      close(newsockfd);
    }
  }

  return 0;
}