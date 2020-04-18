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
#include <fcntl.h>
#include <math.h>

#define SLOW_CLIENT 3
char *service;
char *host = "localhost";
double rate; 
int bad; 

double poissonRandomInterarrivalDelay( double r );
int connectsock(char *host, char *service, char *protocol);
void *worker(void *ign);
int properRead(int ssock, int size, char *letters);
/*
**	Consumer Client
*/
int main(int argc, char *argv[])
{
    int cNumber;
	size_t i;
	double sleepTime;
	int seconds; 
	int usec; 

    switch (argc) {
		case 5:
			service = argv[1];
			cNumber = atoi(argv[2]);
			rate = strtof(argv[3], NULL); 
			bad = atoi(argv[4]);
			break;
		case 6:
			host = argv[1];
			service = argv[2];
			cNumber = atoi(argv[3]);
			rate = strtof(argv[4], NULL); 
			bad = atoi(argv[5]);
			break;
		default:
			printf("wrong number of arguments \n");	
			fflush(stdout);
			return -1;
	}
    cNumber %= 2000;

    pthread_t threads[cNumber];

    for (i = 0; i < cNumber; i++)
    {
        sleepTime = poissonRandomInterarrivalDelay(rate);
		seconds = (int) sleepTime; 
		usec = 1000000*(sleepTime - seconds); 
        printf("sleepTime %lf, seconds %i, usec %i\n", sleepTime, seconds, usec);
        fflush(stdout); 
		sleep(seconds);
		usleep(usec);
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
    char fileName[64];
    pthread_t pid = pthread_self();
    sprintf(fileName, "%lu.txt", pid);
    int fd = open(fileName, O_CREAT | O_WRONLY, 0666);

    if ((csock = connectsock(host, service, "tcp")) == 0)
    {
        fprintf(stderr, "Cannot connect to server.\n");
        exit(-1);
    }

    int dice = rand()%100; 
	if( dice <= bad){
        printf("Slow client with dice %i\n", dice); 
        fflush(stdout);
		sleep(SLOW_CLIENT); 
	}

    write(csock, "CONSUME\r\n", 10);
    read(csock, &netInt, 4);
    size = ntohl(netInt);
    printf("%s ", fileName);
    printf("size %i \n", size);
    fflush(stdout);
    buffer = (char *)malloc(size);
    if (properRead(csock, size, buffer) != 0)
    {
        close(csock);
        pthread_exit(0);
    }
    write(fd, buffer, size);
    free(buffer);
    close(csock);
    pthread_exit(0);
}

int properRead(int ssock, int size, char *letters)
{
    int readUpTo = 0;
    int cc;
    for (; readUpTo < size;)
    {
        cc = read(ssock, (letters + readUpTo), size - readUpTo);
        readUpTo += cc;
        if (cc <= 0)
        {
            break;
        }
    }
    if (readUpTo != size)
    {
        //we have problem
        return 1;
    }
    return 0;
}

/*
**      Poisson interarrival times. Adapted from various sources
**      r = desired arrival rate
*/
double poissonRandomInterarrivalDelay( double r )
{
    return (log((double) 1.0 - 
			((double) rand())/((double) RAND_MAX)))/-r;
}
