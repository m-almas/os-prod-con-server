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
int bufferIndex;   // always points to the empty space in the buffer
fd_set rfds, afds;
int freeProdSlots = MAX_PROD;
int freeConSlots = MAX_CON;
int clientNumbers = 0;

ITEM *initItem(uint32_t size, int socket);

int stramLetters(ITEM * item, int socket);

int getCommandType(char *commandBuffer);

char* readLetters(ITEM * item);

void* handleProducer(void *ign);

void* handleConsumer(void *ign);

int properRead(int ssock, int size, char *letters);

int createIfFreeSlot(void *(*handle) (void *), int * freeSlots, int * pass);
//corrected critical sections
//in order to speed up comment out prints on lines 170, 294
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
	FD_ZERO(&afds);
	//****

	msock = passivesock(service, "tcp", QLEN, &rport);
	mfdv = msock + 1;
	FD_SET(msock, &afds);
	if (rport)
	{
		//	Tell the user the selected port
		printf("server: port %d\n", rport);
		//print now
		fflush(stdout);
	}
	//start the loop
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
		//something is ready to be read
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
					memcpy((int *)pass, (int *)&fd, sizeof(int));
					commandBuffer[cc] = '\0';
					switch (getCommandType(commandBuffer))
					{
					case PRODUCE:
						if(createIfFreeSlot(handleProducer, &freeProdSlots, pass) < 0){
							printf("no free producer slots there \n");
							close(*pass);
							free(pass);
							fflush(stdout);
						}
						break;
					case CONSUME:
						if(createIfFreeSlot(handleConsumer, &freeConSlots, pass) < 0){
							printf("no free consumer slots there \n");
							close(*pass);
							free(pass);
							fflush(stdout);
						}
						break;
					default:
						break;
					}
				}
			}
		}
	}
	return 0;
}

ITEM *initItem(uint32_t size, int socket)
{
	ITEM *item;
	item = (ITEM *)malloc(sizeof(ITEM));
	item->size = size;
	item->psd = socket;
	printf("created item with socket %i, and size %u\n", socket, size);
	fflush(stdout);
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
	uint32_t size;
	int cc;
	write(ssock, "GO\r\n", 4);
	read(ssock, &size, 4);
	size = ntohl(size);
	item = initItem(size, ssock);
	sem_wait(&consumed);
	sem_wait(&lock);
	itemBuffer[bufferIndex] = item;
	bufferIndex++;
	sem_post(&lock);
	sem_post(&produced);
	free(ign);
}
//free the sock there
void *handleConsumer(void * ign)
{
	int ssock = *((int *)ign);
	ITEM *item;
	int itemIndex;
	uint32_t netInt; // to send accross network
	sem_wait(&produced);
	sem_wait(&lock);
	bufferIndex--;
	item = itemBuffer[bufferIndex];
	itemBuffer[bufferIndex] = NULL;
	sem_post(&lock);
	sem_post(&consumed);
	netInt = htonl(item->size);
	write(ssock, &netInt, 4);
	if(stramLetters(item, ssock) != 0){ // responsible for closing producer socket
		printf("we got problems during streaming process\n");
	}
	free(item);
	sem_wait(&lock);
	freeConSlots++; 
	clientNumbers--;
	freeProdSlots++;
	clientNumbers--;
	sem_post(&lock);
	close(ssock); 
	free(ign);
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

int createIfFreeSlot(void *(*handle) (void *), int * freeSlots, int * pass){
	pthread_t tid;
	sem_wait(&lock);
	if(*freeSlots <= 0 || clientNumbers >= MAX_CLIENTS){
		sem_post(&lock);
		return -1;  
	
	}
	clientNumbers++;
	*freeSlots = *freeSlots - 1; 
	sem_post(&lock);
	pthread_create(&tid, NULL, handle, pass);
	return 0;
}

char* readLetters(ITEM * item){
	char* letters = (char*)malloc(item->size);
	if(properRead(item->psd, item->size, letters) != 0){
		printf("Exit due to the read error\n");
		fflush(stdout);
		free(letters);
		close(item->psd);
		return NULL; 
	} 
	write(item->psd, "DONE\r\n", 6);
	printf("released socket with %i, and size %u\n", item->psd, item->size); 
	fflush(stdout);
	close(item->psd);
	return letters;  
}

int stramLetters(ITEM * item, int socket){
	char* smallBuffer = (char*) malloc(BUFSIZE); //may be you should not hard code?
	//here I am assuming that read will not be interupted because the values are small 
	int cc = 0;
	uint32_t readUpTo = 0;  
	while(readUpTo < item->size){
		cc = read(item->psd, smallBuffer, BUFSIZE); //can it block?? 
		if(cc < 0){
			return -1;	
		}
		readUpTo += cc;
		write(socket, smallBuffer, BUFSIZE);  
	}
	write(item->psd, "DONE\r\n", 6);
	printf("released socket with %i, and size %u\n", item->psd, item->size); 
	fflush(stdout);
	close(item->psd);
	return 0; 
}
