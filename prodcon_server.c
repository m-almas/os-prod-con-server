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

#define QLEN 5
#define PRODUCE 1
#define CONSUME 2

ITEM **itemBuffer; // here we are going to store the pointers to the address
sem_t consumed;	   // initial value is equal to the bufferSize
sem_t produced;	   // initial value is 0
sem_t lock;		   // to sync access among clients
int bufferIndex;   // always points to the empty space in the buffer
int producerNumber;
int consumerNumber;
int clientNumber; 

void *workerForClient(void *ign);
ITEM *initItem(int size);
int getCommandType(char *commandBuffer);

void handleProducer(int ssock);

void handleConsumer(int ssock);

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
	//its size is fixed
	// pthread_t threads[THREADS];
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
	sem_init(&lock, 0, 1);
	bufferIndex = 0;
	producerNumber = 0;
	consumerNumber = 0;
	clientNumber = 0;
	//****

	msock = passivesock(service, "tcp", QLEN, &rport);
	if (rport)
	{
		//	Tell the user the selected port
		printf("server: port %d\n", rport);
		//print now
		fflush(stdout);
	}

	for (;;)
	{
		int *fdSock = (int *)malloc(sizeof(int));
		alen = sizeof(fsin);
		//blocking call
		*fdSock = accept(msock, (struct sockaddr *)&fsin, &alen);
		if (*fdSock < 0)
		{
			fprintf(stderr, "accept: %s\n", strerror(errno));
			exit(-1);
		}

		sem_wait(&lock);
		if (clientNumber >= MAX_CLIENTS)
		{
			close(*fdSock);
			free(fdSock);
			printf("REJECTED\n");
			fflush(stdout);
			sem_post(&lock);
			continue;
		}
		clientNumber++; 
		sem_post(&lock);
		//******* separate it into function
		// j = j % THREADS;
		pthread_t pid;
		status = pthread_create(&pid, NULL, workerForClient, fdSock);
		// j++;
		//******* separate it into function
	}
	return 0;
}

void *workerForClient(void *ign)
{
	int *ssock = (int *)ign; //its like file descriptor
	int cc;
	char commandBuffer[512]; // buffer for controll/command messages

	if ((cc = read(*ssock, commandBuffer, 512)) <= 0)
	{
		printf("The client has gone.\n");
		close(*ssock);
		free(ssock);
		pthread_exit(0);
	}
	else
	{
		commandBuffer[cc] = '\0';
		switch (getCommandType(commandBuffer))
		{
		case PRODUCE:
			handleProducer(*ssock);
			break;

		case CONSUME:
			handleConsumer(*ssock);
			break;
		default:
			break;
		}
	}
	close(*ssock);
	free(ssock);
	pthread_exit(0);
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

void handleProducer(int ssock)
{
	sem_wait(&lock);
	if (producerNumber >= MAX_PROD)
	{
		clientNumber--;
		printf("REJECTED\n");
		fflush(stdout);
		sem_post(&lock);
		return;
	}
	else
	{
		producerNumber++;
	}
	sem_post(&lock);

	ITEM *item;
	int size;
	int cc;
	write(ssock, "GO\r\n", 4);
	read(ssock, &size, 4);
	size = ntohl(size);
	item = initItem(size);
	// make sure read happends
	if (properRead(ssock, size, item->letters) != 0)
	{
		sem_wait(&lock);
		clientNumber--;
		producerNumber--;
		sem_post(&lock);
		return;
	}
	sem_wait(&consumed);
	sem_wait(&lock);

	itemBuffer[bufferIndex] = item;
	bufferIndex++;
	producerNumber--;
	clientNumber--;
	sem_post(&lock);
	write(ssock, "DONE\r\n", 6);
	sem_post(&produced);
}

void handleConsumer(int ssock)
{
	sem_wait(&lock);
	if (consumerNumber >= MAX_CON)
	{
		clientNumber--;
		printf("REJECTED\n");
		fflush(stdout);
		sem_post(&lock);
		return;
	}
	else
	{
		consumerNumber++;
	}
	sem_post(&lock);

	ITEM *item;
	int itemIndex;
	int netInt; // to send accross network
	sem_wait(&produced);
	sem_wait(&lock);
	bufferIndex--;
	item = itemBuffer[bufferIndex];
	itemBuffer[bufferIndex] = NULL;
	sem_post(&lock);
	sem_post(&consumed);
	netInt = htonl(item->size);
	write(ssock, &netInt, 4);
	write(ssock, item->letters, item->size);
	free(item->letters);
	free(item);

	sem_wait(&lock);
	consumerNumber--;
	clientNumber--;
	sem_post(&lock);
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

// int properWrite(int ssock, int size, char *letters){
// 	int writeUpTo = 0;
// 	int cc;
// 	for (;writeUpTo < size;){
// 		cc = read(ssock, (letters + writeUpTo), size - writeUpTo);
// 		writeUpTo += cc;
// 		if (cc <= 0)
// 		{
// 			break;
// 		}
// 	}
// 	if (writeUpTo != size)
// 	{
// 		//we have problem
// 		return 1;
// 	}
// 	return 0;
// }
