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
#include <time.h>

#define QLEN 5
#define PRODUCE 1
#define CONSUME 2
#define STATUS 3

ITEM **itemBuffer; // here we are going to store the pointers to the address
sem_t consumed;	   // initial value is equal to the bufferSize
sem_t produced;	   // initial value is 0
sem_t lock;		   // to sync access among clients
int bufferIndex;   // always points to the empty space in the buffer
fd_set rfds, afds;
int freeProdSlots = MAX_PROD;
int freeConSlots = MAX_CON;
int clientNumbers = 0;
int tServedProds = 0;
int tServedCons = 0;
int rejProds = 0;
int rejCons = 0; 
int timeouts = 0;
int rejected = 0; 

int min(int a, int b);

ITEM *initItem(uint32_t size, int socket);

int streamLetters(ITEM * item, int socket);

int getCommandType(char *commandBuffer);

char* readLetters(ITEM * item);

void* handleProducer(void *ign);

void* handleConsumer(void *ign);

int statusCommandType(char * commandBuffer);

void handleStatusClient(char * commandBuffer, int sock);

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
	time_t socketArrivalTimes[1024]; //used as a hash for socket
	for(int i = 0; i < 1024; i++){
		socketArrivalTimes[i] = -1; //initial values 
	}
	time_t seconds;
	struct timeval tv;
	tv.tv_sec = 2; 
	tv.tv_usec = 0;  
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
				   &tv) <= 0)
		{
			//here I should check whether I am waiting too long
			for (fd = 0; fd < mfdv; fd++)
			{
				if (fd != msock && FD_ISSET(fd, &afds))
				{
					seconds = time(NULL); //current time
					if(socketArrivalTimes[fd] > 0){
						if(seconds - socketArrivalTimes[fd] > REJECT_TIME){
							socketArrivalTimes[fd] = -1; 
							close(fd);
							FD_CLR(fd, &afds);
							printf("rejected this socket %i \n", fd);
							timeouts++;
							fflush(stdout);
						}
					}	
				}
			}
		}
		//something is ready to be read
		if (FD_ISSET(msock, &rfds))
		{
			int ssock;
			ssock = accept(msock, (struct sockaddr *)&fsin, &alen);
			//consider checking for max
			//we do not to increment here clientNumbers therefore we do not need critical section
			//createIffreesockts will do it for us
			//otherwise it will be rejected due to timeout
			//there might be problem in case there is more than 1024 clients that timeout
			//but for the sake of simplicity I will not hanle such case
			if (clientNumbers >= MAX_CLIENTS){
				//here we need to increment rejected clients
					rejected++;
					close(ssock);
			}else{

					if (ssock < 0)
					{
						fprintf(stderr, "accept: %s\n", strerror(errno));
						exit(-1);
					}
					if(ssock + 1 > mfdv){
						mfdv = ssock + 1;
					}
					FD_SET(ssock, &afds);
					//here I should add into time
					seconds = time(NULL); 
					socketArrivalTimes[ssock] = seconds; 
			}
		}

		for (fd = 0; fd < mfdv; fd++)
		{
			if (fd != msock && FD_ISSET(fd, &rfds))
			{
				FD_CLR(fd, &afds);
				socketArrivalTimes[fd] = -1; 
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
							rejProds++;
							printf("no free producer slots there \n");
							close(*pass);
							free(pass);
							fflush(stdout);
						}
						break;
					case CONSUME:
						if(createIfFreeSlot(handleConsumer, &freeConSlots, pass) < 0){
							rejCons++;
							printf("no free consumer slots there \n");
							close(*pass);
							free(pass);
							fflush(stdout);
						}
						break;
					case STATUS:
						//the underlying function is nonthreaded remember!
						handleStatusClient(commandBuffer, *pass);
						close(*pass);
						free(pass);
						break;
					default:
						write(*pass, "Wrong Client\n", 13);
						close(*pass);
						free(pass);
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
	else if (strncmp(commandBuffer, "STATUS", 6) == 0){
		return STATUS;
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
	free(ign);

	sem_wait(&consumed);
	sem_wait(&lock);
	itemBuffer[bufferIndex] = item;
	bufferIndex++;
	sem_post(&lock);
	sem_post(&produced);
	tServedProds++;
}
//free the sock there
void *handleConsumer(void * ign)
{
	ITEM *item;
	sem_wait(&produced);
	sem_wait(&lock);
	bufferIndex--;
	item = itemBuffer[bufferIndex];
	itemBuffer[bufferIndex] = NULL;
	sem_post(&lock);
	sem_post(&consumed);
	tServedCons++;

	int ssock = *((int *)ign);
	uint32_t netInt; // to send accross network
	netInt = htonl(item->size);
	write(ssock, &netInt, 4);
	write(item->psd, "GO\r\n", 4);
	if(streamLetters(item, ssock) != 0){ // responsible for closing producer socket
		printf("we got problems during streaming process\n");
	}
	free(item);
	close(ssock); 
	free(ign);

	sem_wait(&lock);
	freeConSlots++; 
	clientNumbers--;
	freeProdSlots++;
	clientNumbers--;
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

int streamLetters(ITEM * item, int socket){
	char * smallBuffer = (char*) malloc(BUFSIZE); //may be you should not hard code?
	//here I am assuming that read will not be interupted because the values are small 
	int cc = 0;
	uint32_t readUpTo = 0; 
	int readSize = 0;  
	while(readUpTo < item->size){
		readSize = min(BUFSIZE, item->size - readUpTo);
		cc = read(item->psd, smallBuffer, readSize); //can it block?? 
		if(cc < 0){
			break;	
		}
		readUpTo += cc;
		write(socket, smallBuffer, readSize);  
	}
	write(item->psd, "DONE\r\n", 6);
	printf("released socket with %i, and size %u\n", item->psd, item->size); 
	fflush(stdout);
	free(smallBuffer);
	close(item->psd);
	return 0; 
}

int min(int a, int b){
    if(a < b){
        return a; 
    }
    return b;
}

int statusCommandType(char * commandBuffer){
	
	if (strncmp(commandBuffer, "STATUS/CURRCLI\r\n", 14) == 0)
	{
		return 1;
	}
	else if (strncmp(commandBuffer, "STATUS/CURRPROD\r\n", 15) == 0)
	{
		return 2;
	}
	else if (strncmp(commandBuffer, "STATUS/CURRCONS\r\n", 15) == 0)
	{
		return 3;
	}
	else if (strncmp(commandBuffer, "STATUS/TOTPROD\r\n", 15) == 0)
	{
		return 4;
	}
	else if (strncmp(commandBuffer, "STATUS/TOTCONS\r\n", 15) == 0)
	{
		return 5;
	}
	else if (strncmp(commandBuffer, "STATUS/REJMAX\r\n", 15) == 0)
	{
		return 6;
	}
	else if (strncmp(commandBuffer, "STATUS/REJSLOW\r\n", 15) == 0)
	{
		return 7;
	}
	else if (strncmp(commandBuffer, "STATUS/REJPROD\r\n", 15) == 0)
	{
		return 8;
	}
	else if (strncmp(commandBuffer, "STATUS/REJCONS\r\n", 15) == 0)
	{
		return 9;
	}
	return -1;
}

//total clients Being server is clientNumbers
//max_prod - freeProdSlots = producers and they are currently being served
//max_con - freeConSlots = consumers and they are currently being server
//tservedproducers new var
//tservedconsumers new var
//rejectedProducers new var
//rejectedConsumers new var
//timeouts new var
//rejectedProd + rejectedCon

void handleStatusClient(char *commandBuffer, int sock){
	char respond[64];
	int respondNumber;
	switch(statusCommandType(commandBuffer)){
		case 1:
			respondNumber = clientNumbers;
			break;
		case 2:
			respondNumber = MAX_PROD - freeProdSlots;
			break;
		case 3:
			respondNumber = MAX_CON - freeConSlots;
			break;
		case 4:
			respondNumber = tServedProds;
			break;
		case 5:
			respondNumber = tServedCons;
			break;
		case 6:
			//think about it
			respondNumber = rejProds + rejCons + rejected;
			break;
		case 7:
			respondNumber = timeouts;
			break;
		case 8:
			respondNumber = rejProds;
			break;
		case 9:
			respondNumber = rejCons;
			break;
		case -1:
			write(sock, "Wrong Inputs\n", 13);
			return;
	}	
			sprintf(respond, "%d\r\n", respondNumber);
			write(sock, respond, strlen(respond));
}
