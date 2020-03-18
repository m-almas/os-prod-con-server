#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFSIZE 512
char *service;
char *host = "localhost";

int connectsock(char *host, char *service, char *protocol);
void *worker(void *ign);
/*
**	Consumer Client
*/
int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("wrong number of arguments \n");
        fflush(stdout);
    }

    size_t i;
    int cNumber;
    host = argv[1];
    service = argv[2];
    cNumber = atoi(argv[3]);

    pthread_t threads[cNumber];

    for (i = 0; i < cNumber; i++)
    {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for (i = 0; i < cNumber; i++)
    {
        pthread_join(threads[i], NULL);
    }
    exit(0);
}

void *worker(void *ign)
{
    int csock;
    int netInt;
    int size;
    int cc;
    char *buffer;

    if ((csock = connectsock(host, service, "tcp")) == 0)
    {
        fprintf(stderr, "Cannot connect to server.\n");
        exit(-1);
    }

    write(csock, "CONSUME\r\n", 10);
    read(csock, &netInt, 4);
    size = ntohl(netInt);
    buffer = (char *)malloc(size + 1);
    cc = read(csock, buffer, size);
    buffer[cc] = '\0';
    printf("size is %d \n", size);
    printf("%s \n", buffer);
    close(csock);
}
