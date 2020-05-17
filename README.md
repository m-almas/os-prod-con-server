# This is final project of OS course at Nazarbayev University
## What is in there ???
- producer that reads random characters from ``` dev/urandom ``` and streams it with ``` BUFSIZE ``` chunks
- consumer that receives characters and streams it into ``` dev/null ``` 
- prodcon_server server that manages consumers and producers properly 

## Features 
- each component is multithreaded 
- to synchronize access semaphores were used 
- blocking calls were minimized using ``` select ``` 
- streaming was used to reduce workload of server 
- server rejects bad and slow clients due to timeout to effectively use resource
- producers and consumers have built in rate to avoid DDOSing the server

## How it works 
- server has buffer of producer sockets waiting to stream random characters of specified size
- consumers connect to server and each will wait or get producer socket from buffer in separate thread
- then producers start to stream to server and server will redirect it to consumer
- each consumer will create log file with the id of thread denoting whether streaming process finished successfully
- end :)

## How to start
```
make 
./pcserver 4444 10 // executableName port buffsize
./producers 4444 10 5 5 // executableName server-port producerNumber rate percentageOfBadProducers
./consumers 4444 10 5 5 // executableName server-port consumerNumber rate percentageOfBadConsumers
make clean
```

