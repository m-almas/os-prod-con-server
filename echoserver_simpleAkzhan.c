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
#include <fcntl.h>
//#define    QLEN            5
//#define    BUFSIZE            4096
int bufferSize;
//char            buf[bufferSize];

//int passivesock( char *service, char *protocol, int qlen, int *rport );

sem_t *full;  // initial value is 0
sem_t *empty; // initial value is buff_size
sem_t *mutex; // mutex for count
int indx = 0; // the index that points to the item that can be consumed

int conNumber = 0;
int proNumber = 0;

//typedef struct Item_struct {
//    int size;
//    char * letters;
//}item_struct;

ITEM *buffItem[BUFSIZE];

void *serve(void *tid)
{
	int cc;
	int *ssock = (int *)tid;

	/* start working for this guy */
	/* ECHO what the client says */
	char buf[512];

	if ((cc = read(*ssock, buf, bufferSize)) <= 0)
	{

		printf("The client %i has gone.\n", *ssock);
		close(*ssock);
	}
	else
	{
		buf[cc] = '\0';
		printf("%s \n", buf);
		if (strncmp(buf, "PRODUCE\r\n", 9) == 0)
		{
			int letterCount;
			ITEM *chosenItem = malloc(sizeof(ITEM));
			sem_wait(empty);
			sem_wait(mutex);
			if (proNumber > MAX_PROD)
			{
				pthread_exit(0);
			}
			else if (proNumber + conNumber > MAX_CLIENTS)
			{
				close(*ssock);
				pthread_exit(0);
			}
			else
			{
				proNumber++;
			}
			sem_post(mutex);

			write(*ssock, "GO\r\n", 4);
			//choose the empty space in the buffer
			read(*ssock, &letterCount, 4);
			letterCount = ntohl(letterCount);
			//create space with such size
			chosenItem->size = letterCount;
			printf(" size is %i \n", chosenItem->size);
			chosenItem->letters = (char *)malloc(chosenItem->size + 1);
			read(*ssock, chosenItem->letters, chosenItem->size);
			printf("read this %s \n", chosenItem->letters);
			write(*ssock, "DONE\r\n", 7);
			close(*ssock);

			sem_wait(mutex);
			buffItem[indx] = chosenItem;
			indx++;
			proNumber--;
			sem_post(mutex);
			sem_post(full);
		}
		if (strncmp(buf, "CONSUME\r\n", 9) == 0)
		{
			int letterCount;
			ITEM *chosenItem;
			sem_wait(full);
			sem_wait(mutex);
			if (conNumber > MAX_CON)
			{
				sem_post(mutex);
				pthread_exit(0);
			}
			else if ((proNumber + conNumber) > MAX_CLIENTS)
			{
				sem_post(mutex);
				close(*ssock);
				pthread_exit(0);
			}
			else
			{
				conNumber++;
			}
			indx--;
			chosenItem = buffItem[indx];
			buffItem[indx] = NULL;
			sem_post(mutex);
			printf("%i\n", chosenItem->size);
			letterCount = htonl(chosenItem->size);
			write(*ssock, &letterCount, 4);
			write(*ssock, chosenItem->letters, chosenItem->size);
			close(*ssock);
			chosenItem->size = 0;
			free(chosenItem->letters);
			sem_wait(mutex);
			conNumber--;
			sem_post(mutex);
			sem_post(empty);
		}
		printf("The client %i says: %s\n", *ssock, buf);
		if (write(*ssock, buf, cc) < 0)
		{
			/* This guy is dead */
			close(*ssock);
		}
	}
	pthread_exit(0);
}
/*
**	This poor server ... only serves one client at a time
*/
int main(int argc, char *argv[])
{

	char *service;
	struct sockaddr_in fsin;
	socklen_t alen;
	int msock;
	int ssock;
	int rport = 0;
	int cc;
	int i = 0;

	switch (argc)
	{
	case 1:
		// No args? let the OS choose a port and tell the user
		rport = 1;
		break;
	case 2:
		// User provides a port? then use it
		service = argv[1];
		exit(-1);
		break;
	case 3:
		service = argv[1];
		bufferSize = atoi(argv[2]);

		if (bufferSize > BUFSIZE)
		{
			printf("too long buffer size");
			exit(-1);
		}
		break;
	default:
		fprintf(stderr, "usage: server [port]\n");
		exit(-1);
	}
	printf("%i \n", bufferSize);
	msock = passivesock(service, "tcp", QLEN, &rport);
	if (rport)
	{
		//	Tell the user the selected port
		printf("server: port %d\n", rport);
		fflush(stdout);
	}

	sem_unlink("full");
	sem_unlink("empty");
	sem_unlink("mutex");
	//initialization of semaphores and global vars
	full = sem_open("full", O_CREAT | O_EXCL, S_IRWXU, 0);
	empty = sem_open("empty", O_CREAT | O_EXCL, S_IRWXU, bufferSize);
	mutex = sem_open("mutex", O_CREAT | O_EXCL, S_IRWXU, 1);

	//
	for (;;)
	{
		int ssock[1024];

		alen = sizeof(fsin);
		ssock[i] = accept(msock, (struct sockaddr *)&fsin, &alen);
		if (ssock < 0)
		{
			fprintf(stderr, "accept: %s\n", strerror(errno));
			exit(-1);
		}

		printf("A client has arrived for echoes.\n");
		fflush(stdout);

		pthread_t tid;
		pthread_create(&tid, NULL, serve, &ssock[i]);
		i++;
		fflush(stdout);
	}
	pthread_exit(0);
}
