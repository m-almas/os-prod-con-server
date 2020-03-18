#ifndef PRODCON

#define PRODCON

#define QLEN 5
#define BUFSIZE 1024
#define PRODUCER_NUMBER 300
#define CONSUMER_NUMBER 212
#define MAX_CLIENT 512

#define THREADS 513 // the number of threads is 513 = 512 + 1 we need one more \
                    // to enable secure rejection, that way we will not lose any active ID

// Each item has a random-sized letters buffer between 1 and 1 million.
#define MAX_LETTERS 1000000

int connectsock(char *host, char *service, char *protocol);
int passivesock(char *service, char *protocol, int qlen, int *rport);

typedef struct item_t
{
        int size;
        char *letters;
} ITEM;

#endif
