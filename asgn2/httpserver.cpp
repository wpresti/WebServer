#include <arpa/inet.h>
#include <cstdio>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h> //read only
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <queue>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace std;
// fucks up on default port --seg fault ./httpserver localhost
#define PORT 80
#define NUMTHREAD 8
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

char *logfname = NULL;

int64_t globalConLen = 0;
queue<int> q;

struct args {
  char *log = NULL;
};

struct put_args {
  pthread_t curThread;
  int newsfd;
  char *filename;
  int64_t len;
  char *ver;
  int blockingOther = 0;
  pthread_t waitingThread;
  int *logLen;
  char *logname = {0};
};

struct get_args {
  pthread_t curThread;
  int newsfd;
  char *filename;
  char *ver;
  int blockingOther = 0;
  pthread_t waitingThread;
  int *logLen;
  char *logname = {0};
};

void *putMulti(void *);
void *getMulti(void *);
bool isValidName(char *headerStr);
void put(int newsockfd, char *fileName, int64_t len, char *ver, char *logfile);
void get(int newsockfd, char *fileName, char *ver, char *logfile);
void *dispatcher(void *argp) {
  // char *logname = argp;
  char *logname = NULL;

  logname = (((struct args *)argp)->log);
  printf("dispatcher log: %s", (logfname));

  // logname = ((struct args *)argp)->log;
  char buffer[1024] = {0};
  int n;
  while (1) {
    pthread_mutex_lock(&lock);
    printf("in dispatcher\n");
    printf("\n\nqempty? %d\n\n\n", q.empty());
    while (q.empty()) {
      // cond wait in here
      printf("waiting...");

      pthread_cond_wait(&cv, &lock);
    }
    int newsockfd = q.front();
    q.pop();
    printf("popping -- out of dispatcher %d\n", newsockfd);
    pthread_mutex_unlock(&lock);
    // new coide below
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
    char httpVer[256];

    char *split_request;

    int64_t conLen = 0;
    sscanf(buffer, "%s %s %s", req, fname, httpVer);

    // printf("\n\n\nreq: %s... fname: %s... version: %s\n", req, fname,
    // httpVer);

    bool validFile = isValidName(fname);
    // validFile = 1;//get rid of this
    printf("File is valid?: %d\n", validFile);

    split_request = strtok(buffer, "\r\n");
    while (split_request != NULL) {
      sscanf(split_request, "Content-Length:%ld", &conLen);

      split_request = strtok(NULL, "\r\n");
    }
    // printf("GOT HERE BITCH");
    // printf("\n\nParsed Length %ld\n", conLen);
    if (validFile == 0) { // uncoment continue and dprintf
      // 400 error -- length or bad characters
      printf("\nFile not valid!\n");
      dprintf(newsockfd, "%s 400 Bad Request\r\n", httpVer);
      close(newsockfd);
      continue;
    }

    if (strcmp("PUT", req) == 0 && validFile != 0) {
      printf("PUT REQ woohoo\n");
      // putReq(newsockfd, fname, conLen);
      put(newsockfd, fname, conLen, httpVer, logfname);
    } else if (strcmp("GET", req) == 0 && validFile != 0) {
      printf("GET REQ wohoo\n");
      get(newsockfd, fname, httpVer, logfname);
    } else {
      // other req not supported
      // 500
      dprintf(newsockfd, "HTTP/1.1 400 Bad Request\r\n");
      close(newsockfd);
    }
  }

  return 0;
}

int64_t bufLength(char *buf) {
  int64_t bufLen = 0;
  for (int i = 0; i < 1024; i++) {
    if (buf[i] != '\0') {
      // printf("%c",xtraLogBufH[i]);
      bufLen++;
    }
  }
  printf("BUFFER LEN FUNC: %ld\n", bufLen);
  return bufLen;
}

void error(char *msg) {
  perror(msg);
  printf("\n\n\nError function\n\n");
}

bool isValidName(char *headerStr) { //
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

// --------------------MAIN-------------------

// arugment parsing messed up... gotta fix tmmr.
bool ipNPort = false;
int main(int argc, char *const argv[]) {

  bool logging = false;
  char *cvalue = NULL;
  char *bvalue = NULL;
  int c;
  opterr = 0;

  int choiceN = 0;
  while ((c = getopt(argc, argv, "N:l:")) != -1) {
    switch (c) {
    case 'N':
      bvalue = optarg;
      choiceN = 1;
      break;
    case 'l':
      cvalue = optarg;
      break;
    default:
      abort();
    }
  }

  if (choiceN != 1) {
    bvalue = (char *)"4";
    printf("bout to cpy");
    // strcpy(bvalue,"4");
  }

  char *temp1 = NULL;
  char *temp2 = NULL;
  char *ip = NULL;
  char *port = NULL;
  if (cvalue != NULL) {
    logging = true;
  }
  printf("bvalue = %s logfile = %s\n", bvalue, cvalue);
  int xtracount = 0;
  for (int i = optind; i < argc; i++) {
    printf("non-option %d argument %s\n", i, argv[i]);
    printf("xtracount : %d\n", xtracount);
    if (xtracount == 0)
      temp1 = argv[i];
    if (xtracount == 1)
      temp2 = argv[i];

    xtracount++;
    if (xtracount > 2) {
      printf("invalid arugments son\n");
      abort();
    }
  }

  if (xtracount == 2) {
    ipNPort = true;
    printf("two arugments\n");
    ip = temp1;
    port = temp2;
  }
  if (xtracount == 1) {
    // just port given
    printf("one arugmnet %s\n", temp1);
    ip = temp1;
    printf("1");
  }
  if (xtracount == 0) {
    // no extra arugments
    printf("no arugmnets\n");
    exit(0);
  }

  int numThreads = atoi(bvalue);
  printf("just ip %d\n", ipNPort);
  printf("--");
  // printf("numthreads arg %d\n",numThreads);
  // printf("fuck %s\n",ip);
  // printf("fuck2 %s\n",port);

  printf("0dooooood\n");
  printf("did it fucking get here");
  int server_fd;
  struct sockaddr_in address;
  int opt = 1;
  int addrlen = sizeof(address);
  // char buffer[4096] = {0};

  int newsockfd;

  // curr running threads array -- will have to change to a malloc
  pthread_t runningThreads[8];

  // Creating socket file descriptor
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }
  printf("1");
  // Forcefully attaching socket to the port 8080
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }
  address.sin_family = AF_INET;
  printf("2");

  if (strcmp(ip, "localhost") == 0) {
    address.sin_addr.s_addr = INADDR_ANY; // localhost
  } else {
    address.sin_addr.s_addr = inet_addr(ip);
  }

  printf("FUCKME");
  // int defaultport = 80;

  if (ipNPort == true) {
    address.sin_port = htons(atoi(port));
  } else {
    address.sin_port = htons(80); // default port
  }

  // Forcefully attaching socket to the port 8080
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }
  if (listen(server_fd, 100) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  // open file for logging
  if (cvalue != NULL) {
    int logfd = open(cvalue, O_CREAT | O_WRONLY | O_TRUNC, 0777);
    close(logfd);
    printf("LOG FILE CREATED");
  }
  printf("cvalue: %s\n", cvalue);

  struct args *arg = (struct args *)malloc(sizeof(args));
  arg->log = cvalue;

  logfname = cvalue;

  for (int i = 0; i < numThreads; i++) { // HARDCODED 8 THREADS IN POOL
    pthread_create(&runningThreads[i], NULL, dispatcher, (void *)&logfname);
  }

  printf("got to accept\n");

  while (1) {
    if ((newsockfd = accept(server_fd, (struct sockaddr *)&address,
                            (socklen_t *)&addrlen)) < 0) {
      perror("accept");
      exit(EXIT_FAILURE);
    }
    pthread_mutex_lock(&lock);
    printf("newsockfd %d\n", newsockfd);
    q.push(newsockfd);
    pthread_mutex_unlock(&lock);
    pthread_cond_signal(&cv);

    // cond siginal! still to do
  }

  return 0;
}

void *putMulti(void *argp) {
  // should log contents of file
  printf("iinside multithreaded shits\n");
  // struct put_args *argu = (put_args *)argp; //(put_args*)
  int newsockfd = ((struct put_args *)argp)->newsfd;
  char *fileName = ((struct put_args *)argp)->filename;
  int64_t len = ((struct put_args *)argp)->len;
  char *ver = ((struct put_args *)argp)->ver;
  char *logfile = ((struct put_args *)argp)->logname;

  printf("%i %s %ld %s\n", newsockfd, fileName, len, ver);

  // FREE STRUCT AFTER UNPACK

  // FREE SHITS
  // lock mutex
  pthread_mutex_lock(&lock);

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
  unsigned char buf[11];
  ssize_t written = 0;
  ssize_t recved = 0;
  printf("fd is..: %d", fd);

  // len is content length
  char xtraLogBufH[1024];
  char xtraLogBufT[1024];
  bzero(xtraLogBufH, 1024);
  bzero(xtraLogBufT, 1024);

  sprintf(xtraLogBufH, "PUT %s length %ld\n", fileName, len);

  int xtraBufLenH = bufLength(xtraLogBufH);
  sprintf(xtraLogBufT, "\n========\n");
  int xtraBufLenT = bufLength(xtraLogBufT);
  // int64_t numNl = ceil(len);

  int64_t miniFileCur = globalConLen;

  globalConLen += xtraBufLenH;
  globalConLen += len * 3; // each charaer is three haracters in hex
  globalConLen +=
      ceil(len / 20) * 9; // this is the space for the numbers and space
  globalConLen += xtraBufLenT;
  globalConLen += ceil(len / 20); // number of new lines (every 20 hex thingies)
  int logfd;
  if (logfile != NULL)
    logfd = open(logfile, O_WRONLY);

  pthread_mutex_unlock(&lock);
  if (logfile != NULL)
    pwrite(logfd, xtraLogBufH, xtraBufLenH, miniFileCur);
  if (logfile != NULL)
    miniFileCur += xtraBufLenH;

  int newLineQ = -1;
  char logBuf2[40];
  char LogBufNum[40];
  while (curPosFile < contentLength) { // <= !!!!!!!!!!!!
    recved = recv(newsockfd, buf, 10, 0);
    // printf("\ncurpos:%ld\n",curPosFile);
    if (recved < 0) {

      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      close(newsockfd);
      close(fd);
      return 0;
    }

    // printf("buf: %s", buf);
    int tempp;
    // format number and print to file
    if (newLineQ == -1) {
      bzero(LogBufNum, 40);
      sprintf(LogBufNum, "%08ld ", curPosFile);
      if (logfile != NULL)
        tempp = pwrite(logfd, LogBufNum, 9, miniFileCur);
      if (logfile != NULL)
        miniFileCur += tempp;
    }
    int pwritten;
    for (int i = 0; i < recved; i++) {
      sprintf((char *)logBuf2, "%02x ", buf[i]);
      printf("%c", buf[i]);
      if (logfile != NULL)
        pwritten = pwrite(logfd, logBuf2, 3, miniFileCur);
      if (logfile != NULL)
        printf("pwritten put %d\n", pwritten);
      if (logfile != NULL)
        miniFileCur += 3;
      printf("is is... %d %d ", i, newLineQ);
      int fuck = 0;
      if (i == 9 && newLineQ == 1) {
        printf("newline pwrite\n");
        // new line
        // increment miniFileCur -- make sure to take into account
        // the new lines in the global log size

        // sprintf(logBuf2,"%c","\n");
        if (logfile != NULL)
          fuck = pwrite(logfd, "\n", 1, miniFileCur);
        if (logfile != NULL)
          miniFileCur += fuck;
        printf("newLINE new shit %d\n", fuck);
      }
    }
    newLineQ = newLineQ * -1;

    curPosFile += recved;
    written = write(fd, buf, recved);
    if (written < 0) {
      printf("crashed write\n");
      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      close(fd);
      close(newsockfd);
      return 0;
    }
  }
  printf("ending Put req\n");
  if (CreatedFile == true) {
    dprintf(newsockfd, "%s 201 Created\r\n", ver);
    if (logfile != NULL)
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
    if (logfile != NULL)
      miniFileCur += xtraBufLenT;

  } else {
    dprintf(newsockfd, "%s 200 OK\r\n", ver);
    if (logfile != NULL)
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
    if (logfile != NULL)
      miniFileCur += xtraBufLenT;
  }

  close(fd);
  close(newsockfd);
  if (logfile != NULL)
    close(logfd);

  // print struct shits
  return 0;
}
// void getReq(int newsockfd, char *fileName, char *ver)
void *getMulti(void *argp) {
  // should not log contents
  printf("inside get multithread shit\n");
  // struct get_args *argu = (get_args *)argp;
  int newsockfd = ((struct get_args *)argp)->newsfd;
  char *fileName = ((struct get_args *)argp)->filename;
  char *ver = ((struct get_args *)argp)->ver;
  char *logfile = ((struct get_args *)argp)->logname;

  printf("log file %s\n", logfile);

  printf("%i %s %s \n", newsockfd, fileName, ver);
  // pthread_mutex_lock(&lock);

  ssize_t fd;
  fd = open(fileName, O_RDONLY, S_IRWXG);
  if (fd < 0) {
    printf("can't find file-- open is -1 :(\n");
    if (errno == EACCES) {
      printf("\naccess denied son \n");
      dprintf(newsockfd, "%s 403\r\n", ver);
      close(newsockfd);
      return 0;
    } else {
      printf("\nU have access son\n");
    }
    dprintf(newsockfd, "%s 404\r\n", ver); // prob wrong...
    close(newsockfd);
    // send error to curl... still to do
    return 0;
  }
  char xtraLogBufH[1024];
  char xtraLogBufT[1024];
  bzero(xtraLogBufH, 1024);
  bzero(xtraLogBufT, 1024);
  // int64_t tempfs = getFilesize(fd);
  // change to 0 length
  sprintf(xtraLogBufH, "GET %s length %d\n", fileName, 0);
  int xtraBufLen = bufLength(xtraLogBufH);

  printf("LEN OF HEADER SHITS %d\n", xtraBufLen);

  sprintf(xtraLogBufT, "========\n");

  int xtraBufLenT = bufLength(xtraLogBufT);

  // }
  // lock below for opening logFile and getting new end of log file length
  pthread_mutex_lock(&lock);
  size_t fs = getFilesize(fd); // length of file
  // int64_t minfileLen = fs; // length of file
  int64_t miniFileCur = globalConLen; // start offset minifile in lofFIle

  globalConLen += xtraBufLen; // header len

  globalConLen += xtraBufLenT; // tail len

  // open log inside
  int logfd = open(logfile, O_WRONLY);

  pthread_mutex_unlock(&lock);
  // unlock above

  char buf[11];
  size_t sent, n_char, curPosFile = 0;

  dprintf(newsockfd, "%s 200 OK\r\n", ver);
  dprintf(newsockfd, "Content-Length: %ld\r\n\r\n", fs);

  // write title head thing to log file -- know length sizeof(a)/sizeof(a[0])
  if (logfile != NULL) {
    pwrite(logfd, xtraLogBufH, xtraBufLen, miniFileCur);

    miniFileCur += xtraBufLen; // add written len to curpos len thingy
                               // add len of end to globalCONLEN
  }

  // char logBuf2[40];
  // char logBuf[40];
  // file contents coming now
  while (curPosFile < fs) {
    n_char = read(fd, buf, 10);
    if (n_char == 0) {

      break; // change /update content length to amtnt sent??
    }
    if (n_char < 0) {
      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      break;
    }

    curPosFile += n_char;
    sent = send(newsockfd, buf, n_char, 0);
    if (sent < 0) {
      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      break;
    }
  }
  close(fd);

  if (logfile != NULL) {
    pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
    miniFileCur += xtraBufLenT;
  }

  close(logfd);

  // pthread_mutex_unlock(&lock);
  close(newsockfd); // close current request
  printf("ending getReq function\n");
  printf("globalConLen %ld\n", globalConLen);
  return 0;
}

void put(int newsockfd, char *fileName, int64_t len, char *ver, char *logfile) {
  printf("INFO FO PUT %i %s %ld %s %s\n", newsockfd, fileName, len, ver,
         logfile);

  // FREE STRUCT AFTER UNPACK

  // LOG FILE ALWAYS NULL

  // FREE SHITS
  // lock mutex
  pthread_mutex_lock(&lock);
  int fd;
  int fail = -1;
  int logfd = 0;
  bool CreatedFile = false;
  int testO = open(fileName, O_RDONLY);
  if (testO < 0) {
    CreatedFile = true;
  }
  fd = open(fileName, O_CREAT | O_WRONLY | O_TRUNC,
            0777); // S_IRWXG -- ins't 777 the ame as sirwxg???
  if (fd < 0) {
    printf("fuck %s\n", strerror(errno));
    fail = 403;
  }

  // check for no permission??
  int64_t curPosFile = 0;
  int64_t contentLength = len;
  unsigned char buf[11];
  ssize_t written = 0;
  ssize_t recved = 0;
  printf("fd is..: %d", fd);

  // len is content length
  char xtraLogBufH[1024];
  char xtraLogBufT[1024];
  bzero(xtraLogBufH, 1024);
  bzero(xtraLogBufT, 1024);
  char failBuf[1024];
  bzero(failBuf, 1024);

  sprintf(failBuf, "FAIL: PUT %s %s --- response %d\n", fileName, ver, fail);

  sprintf(xtraLogBufH, "PUT %s length %ld\n", fileName, len);

  int xtraBufLenH = bufLength(xtraLogBufH);
  sprintf(xtraLogBufT, "\n========\n");
  int xtraBufLenT = bufLength(xtraLogBufT);
  // int64_t numNl = ceil(len);

  int64_t miniFileCur = globalConLen;
  if (fail > 0) {
    sprintf(xtraLogBufT, "========\n");
    xtraBufLenT = bufLength(xtraLogBufT);
    // fail
    globalConLen += bufLength(failBuf);
    globalConLen += xtraBufLenT;
  } else {
    // continue -- caluc reg length
    globalConLen += xtraBufLenH;
    globalConLen += len * 3; // each charaer is three haracters in hex
    globalConLen +=
        ceil(len / 20) * 9; // this is the space for the numbers and space
    globalConLen += xtraBufLenT;
    globalConLen +=
        ceil(len / 20); // number of new lines (every 20 hex thingies)
  }

  if (logfile != NULL)
    logfd = open(logfile, O_WRONLY | O_EXCL, 0777); // get rid of creat
  int pwritten = 0;
  printf("\n\nlogfd:%d %s\n", logfd, logfile);
  if (logfd < 0) {
    printf("open fucked");
    printf("error :%s", strerror(errno));
    exit(1);
  }
  pthread_mutex_unlock(&lock);
  // int pwritten = 0;

  if (fail < 0) {
    if (logfile != NULL)
      pwritten = pwrite(logfd, xtraLogBufH, xtraBufLenH, miniFileCur);
    if (logfile != NULL)
      miniFileCur += xtraBufLenH;
    if (pwritten < 0) {
      printf("error :%s", strerror(errno));
      exit(1);
    }
  } else {
    // fail > 0
    if (logfile != NULL)
      pwritten = pwrite(logfd, failBuf, bufLength(failBuf), miniFileCur);
    miniFileCur += pwritten;
    if (logfile != NULL)
      pwritten =
          pwrite(logfd, xtraLogBufT, bufLength(xtraLogBufT), miniFileCur);
    miniFileCur += pwritten;
    char failResp[1024];
    bzero(failResp, 1024);
    sprintf(failResp, "%s %d Forbidden\r\n", ver, fail);
    send(newsockfd, failResp, bufLength(failResp), 0);

    close(logfd);
    close(newsockfd);

    // return
    return;
  }

  printf("");

  int newLineQ = -1;
  char logBuf2[40];
  char LogBufNum[40];
  while (curPosFile < contentLength) { // <= !!!!!!!!!!!!
    recved = recv(newsockfd, buf, 10, 0);
    // printf("\ncurpos:%ld\n",curPosFile);
    if (recved < 0) {

      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      close(newsockfd);
      close(fd);
      return;
    }

    // printf("buf: %s", buf);
    int tempp;
    // format number and print to file
    if (newLineQ == -1) {
      bzero(LogBufNum, 40);
      sprintf(LogBufNum, "%08ld ", curPosFile);
      if (logfile != NULL)
        tempp = pwrite(logfd, LogBufNum, 9, miniFileCur);
      if (logfile != NULL)
        miniFileCur += tempp;
    }
    // int pwritten;
    for (int i = 0; i < recved; i++) {
      sprintf((char *)logBuf2, "%02x ", buf[i]);
      printf("%c", buf[i]);
      if (logfile != NULL)
        pwritten = pwrite(logfd, logBuf2, 3, miniFileCur);
      if (logfile != NULL)
        printf("pwritten put %d\n", pwritten);
      if (logfile != NULL)
        miniFileCur += 3;
      printf("is is... %d %d ", i, newLineQ);
      int fuck = 0;
      if (i == 9 && newLineQ == 1) {
        printf("newline pwrite\n");
        // new line
        // increment miniFileCur -- make sure to take into account
        // the new lines in the global log size

        // sprintf(logBuf2,"%c","\n");
        if (logfile != NULL)
          fuck = pwrite(logfd, "\n", 1, miniFileCur);
        if (logfile != NULL)
          miniFileCur += fuck;
        printf("newLINE new shit %d\n", fuck);
      }
    }
    newLineQ = newLineQ * -1;

    curPosFile += recved;
    written = write(fd, buf, recved);
    if (written < 0) {
      printf("crashed write\n");
      dprintf(newsockfd, "%s 500 Internal Server Error\r\n", ver);
      close(fd);
      close(newsockfd);
      return;
    }
  }
  printf("ending Put req\n");
  if (CreatedFile == true) {
    dprintf(newsockfd, "%s 201 Created\r\n", ver);
    if (logfile != NULL)
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
    if (logfile != NULL)
      miniFileCur += xtraBufLenT;

  } else {
    dprintf(newsockfd, "%s 200 OK\r\n", ver);
    if (logfile != NULL)
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
    if (logfile != NULL)
      miniFileCur += xtraBufLenT;
  }

  close(fd);
  close(newsockfd);
  if (logfile != NULL)
    close(logfd);

  // print struct shits
}

void get(int newsockfd, char *fileName, char *ver, char *logfile) {
  printf("log file %s\n", logfile);

  printf("%i %s %s \n", newsockfd, fileName, ver);
  // pthread_mutex_lock(&lock);
  int failNum = -1;
  ssize_t fd;
  fd = open(fileName, O_RDONLY, S_IRWXG);
  if (fd < 0) {
    printf("can't find file-- open is -1 :(\n");
    if (errno == EACCES) {
      printf("\naccess denied son \n");
      dprintf(newsockfd, "%s 403\r\n", ver);
      failNum = 403;
      // close(newsockfd);
      // return;
    } else {
      printf("\nU have access\n");
      dprintf(newsockfd, "%s 404\r\n", ver); // prob wrong...
      failNum = 404;
    }
    // close(newsockfd);
    // send error to curl... still to do
    // return;
  }
  char xtraLogBufH[1024];
  char xtraLogBufT[1024];
  bzero(xtraLogBufH, 1024);
  bzero(xtraLogBufT, 1024);
  char failBuf[1024];
  bzero(failBuf, 1024);

  // change to 0 length
  sprintf(xtraLogBufH, "GET %s length %d\n", fileName, 0);
  int xtraBufLen = bufLength(xtraLogBufH);

  printf("LEN OF HEADER SHITS %d\n", xtraBufLen);

  sprintf(xtraLogBufT, "========\n");

  int xtraBufLenT = bufLength(xtraLogBufT);

  sprintf(failBuf, "FAIL: GET %s %s --- response %d\n", fileName, ver, failNum);

  int failLen = bufLength(failBuf);

  // }
  // lock below for opening logFile and getting new end of log file length
  pthread_mutex_lock(&lock);
  size_t fs = getFilesize(fd); // length of file
  // int64_t minfileLen = fs; // length of file
  int64_t miniFileCur = globalConLen; // start offset minifile in lofFIle
  if (failNum < 0) {
    globalConLen += xtraBufLen; // header len

    globalConLen += xtraBufLenT; // tail len
  } else {
    // math for fail
    globalConLen += failLen;
    globalConLen += xtraBufLenT;
  }

  // open log inside
  int logfd = open(logfile, O_WRONLY);

  pthread_mutex_unlock(&lock);
  // unlock above

  char buf[11];
  size_t sent, n_char, curPosFile = 0;
  if (failNum < 0) {
    dprintf(newsockfd, "%s 200 OK\r\n", ver);
    dprintf(newsockfd, "Content-Length: %ld\r\n\r\n", fs);
  }

  // write title head thing to log file -- know length sizeof(a)/sizeof(a[0])
  if (logfile != NULL) {
    // reg header
    if (failNum < 0) {
      pwrite(logfd, xtraLogBufH, xtraBufLen, miniFileCur);
      miniFileCur += xtraBufLen;

      // reg theader
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
      printf("incremnting shits\n");
      // add written len to curpos len thingy
      miniFileCur += xtraBufLenT; // add len of end to globalCONLEN
    } else {
      // fail
      printf("failBuf: %s\n", failBuf);
      pwrite(logfd, failBuf, failLen, miniFileCur);
      miniFileCur += failLen;
      // end
      pwrite(logfd, xtraLogBufT, xtraBufLenT, miniFileCur);
      miniFileCur += xtraBufLenT;

      close(logfd);
      close(fd);
      close(newsockfd);
    }
  }

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