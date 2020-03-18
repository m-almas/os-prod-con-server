INCLUDES        = -I. -I/usr/include

LIBS		= libsocklib.a  \
			-ldl -lpthread -lm

COMPILE_FLAGS   = ${INCLUDES} -c
COMPILE         = gcc ${COMPILE_FLAGS}
LINK            = gcc -o

C_SRCS		= \
		passivesock.c \
		connectsock.c \
		client.c \
		echoserver_simple.c \
		consumerclient.c

SOURCE          = ${C_SRCS}

OBJS            = ${SOURCE:.c=.o}

EXEC		= client echoserver consumerclient

.SUFFIXES       :       .o .c .h

all		:	library client echoserver consumerclient

.c.o            :	${SOURCE}
			@echo "    Compiling $< . . .  "
			@${COMPILE} $<

library		:	passivesock.o connectsock.o
			ar rv libsocklib.a passivesock.o connectsock.o

echoserver	:	echoserver_simple.o
			${LINK} $@ echoserver_simple.o ${LIBS}

client		:	client.o
			${LINK} $@ client.o ${LIBS}

consumerclient : consumerclient.o
			${LINK} $@ consumerclient.o ${LIBS}

clean           :
			@echo "    Cleaning ..."
			rm -f tags core *.out *.o *.lis *.a ${EXEC} libsocklib.a
