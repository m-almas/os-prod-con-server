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
#include <semaphore.h>
#include <prodcon.h>
#include <sys/types.h>
#include <sys/time.h>

#define QLEN 5
#define PRODUCE 1
#define CONSUME 2

ITEM **itemBuffer; // here we are going to store the pointers to the address
sem_t consumed;	   // initial value is equal to the bufferSize
sem_t produced;	   // initial value is 0
sem_t lock;		   // to sync access among clients
sem_t fprodNum, fconNum; 
int bufferIndex;   // always points to the empty space in the buffer
int producerNumber;
int consumerNumber;
fd_set rfds, afds;

ITEM *initItem(int size);
int getCommandType(char *commandBuffer);

void* handleProducer(void *ign);

void* handleConsumer(void *ign);

int properRead(int ssock, int size, char *letters);

int main(int argc, char *argv[])
{
	int bufferSize;
	char *service;
	struct sockaddr_in fsin;
	socklen_t alen;
	int msock;
	int rport = 0;
	int cc;
	int status;
	int j = 0;
	int fd;
	int mfdv;
	char commandBuffer[512];
	//its size is fixed
	pthread_t threads[THREADS];
	switch (argc)
	{
	case 2:
		// No args? let the OS choose a port and tell the user
		rport = 1;
		bufferSize = atoi(argv[1]);
		break;
	case 3:
		// User provides a port? then use it
		service = argv[1];
		bufferSize = atoi(argv[2]);
		break;
	default:
		fprintf(stderr, "usage: cpserver [port] buffsize\n");
		exit(-1);
	}

	//initialization
	itemBuffer = (ITEM **)malloc(sizeof(ITEM *) * bufferSize);
	sem_init(&consumed, 0, bufferSize);
	sem_init(&produced, 0, 0);
	sem_init(&fprodNum, 0, PRODUCER_NUMBER);
	sem_init(&fconNum, 0, CONSUMER_NUMBER); 
	sem_init(&lock, 0, 1);
	bufferIndex = 0;
	producerNumber = 0;
	consumerNumber = 0;
	//****

	msock = passivesock(service, "tcp", QLEN, &rport);
	mfdv = msock + 1;
	if (rport)
	{
		//	Tell the user the selected port
		printf("server: port %d\n", rport);
		//print now
		fflush(stdout);
	}
	FD_ZERO(&afds);
	FD_SET(msock, &afds);
	for (;;)
	{
		memcpy((char *)&rfds, (char *)&afds, sizeof(rfds));
		alen = sizeof(fsin);
		if (select(FD_SETSIZE, &rfds, (fd_set *)0, (fd_set *)0,
				   (struct timeval *)0) < 0)
		{
			fprintf(stderr, "server select: %s\n", strerror(errno));
			exit(-1);
		}

		if (FD_ISSET(msock, &rfds))
		{
			int ssock;
			ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
			if (ssock < 0)
			{
				fprintf(stderr, "accept: %s\n", strerror(errno));
				exit(-1);
			}
			if(ssock + 1 > mfdv){
				mfdv = ssock + 1;
			}
			FD_SET(ssock, &afds);
		}

		for (fd = 0; fd < mfdv; fd++)
		{
			if (fd != msock && FD_ISSET(fd, &rfds))
			{
				FD_CLR(fd, &afds);
				if ((cc = read(fd, commandBuffer, 512)) <= 0)
				{
					printf("The client has gone.\n");
					close(fd);
				}
				else
				{
					int* pass = (int *)malloc(sizeof(int)); // the val we pass into threads
					//use semaphores to check the values
					memcpy((int *)pass, (int *)&fd, sizeof(int));
					fflush(stdout);
					commandBuffer[cc] = '\0';
					int freeProdSlots;
					int freeConsumerSlots;
					pthread_t tid;
					switch (getCommandType(commandBuffer))
					{
					case PRODUCE:
						//you can use memcpy to pass that socket
							 
						sem_getvalue(&fprodNum, &freeProdSlots);
						if(freeProdSlots == 0){
							close(*pass);
							free(pass);
							break;  
						}else{
							sem_wait(&fprodNum); // decrease the number of free slots
							fflush(stdout);
							pthread_create(&tid, NULL, handleProducer, pass);
							break;
						}
						
					case CONSUME:
						  
						sem_getvalue(&fconNum, &freeConsumerSlots);
						if(freeConsumerSlots == 0){
							close(*pass); 
							free(pass);
							break; 
						}else{
							sem_wait(&fconNum); // decrease the number of free slopts 
							
							pthread_create(&tid, NULL, handleConsumer, pass);
							break;
						}
					default:
						break;
					}
				}
			}
		}
	}
	return 0;
}

ITEM *initItem(int size)
{
	ITEM *item;
	item = (ITEM *)malloc(sizeof(ITEM));
	item->size = size;
	item->letters = (char *)malloc(size);
	return item;
}

int getCommandType(char *commandBuffer)
{
	if (strncmp(commandBuffer, "PRODUCE\r\n", 9) == 0)
	{
		return PRODUCE;
	}
	else if (strncmp(commandBuffer, "CONSUME\r\n", 9) == 0)
	{
		return CONSUME;
	}
	return -1;
}
//free the sock there
void* handleProducer(void *ign)
{
	int ssock = *((int *)ign);
	ITEM *item;
	int size;
	int cc;
	sem_wait(&consumed);
	write(ssock, "GO\r\n", 4);
	read(ssock, &size, 4);
	size = ntohl(size);
	item = initItem(size);
	// make sure read happends
	if (properRead(ssock, size, item->letters) != 0)
	{
		close(ssock);
		free(ign); 
		pthread_exit(0);	
	}

	sem_wait(&lock);
	itemBuffer[bufferIndex] = item;
	bufferIndex++;
	sem_post(&lock);
	write(ssock, "DONE\r\n", 6);
	sem_post(&produced);
	fflush(stdout);
	close(ssock); 
	free(ign);
	sem_post(&fprodNum); //increasing free socket slots
}
//free the sock there
void *handleConsumer(void * ign)
{
	int ssock = *((int *)ign);
	ITEM *item;
	int itemIndex;
	int netInt; // to send accross network
	sem_wait(&produced);
	sem_wait(&lock);
	bufferIndex--;
	item = itemBuffer[bufferIndex];
	itemBuffer[bufferIndex] = NULL;
	sem_post(&lock);

	netInt = htonl(item->size);
	write(ssock, &netInt, 4);
	write(ssock, item->letters, item->size);
	free(item->letters);
	free(item);
	sem_post(&consumed);
	close(ssock); 
	free(ign);
	sem_post(&fconNum); //increasing free socket slots
}

int properRead(int ssock, int size, char *letters)
{
	int readUpTo = 0;
	int cc;
	for (; readUpTo < size;)
	{
		cc = read(ssock, (letters + readUpTo), size - readUpTo);
		readUpTo += cc;
		if (cc <= 0 || readUpTo == size)
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
