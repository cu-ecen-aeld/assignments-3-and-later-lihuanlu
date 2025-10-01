 /**********************************************************************************
 * @file    aesdsocket.c
 * @brief   Assignment 6 part 1.
 *
 * @author        <Li-Huan Lu>
 * @date          <09/29/2025>
 * @reference     Linux manual page
 *                Lecture pdf
 *                https://beej.us/guide/bgnet/html/
 *                https://nxmnpg.lemoda.net/3/SLIST_FOREACH_SAFE
 *                https://github.com/stockrt/queue.h/blob/master/sample.c
 ***********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

// Socket
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

// Pthread
#include <pthread.h>
#include <sys/queue.h>

#define LISTEN_BACKLOG 50   // From linux manual page
#define BUFF_SIZE      1024

typedef struct slist_thread_s{
	pthread_t thread_id;
	int client_fd;
	char client_ip[INET_ADDRSTRLEN];
	bool thread_complete;
	
	SLIST_ENTRY(slist_thread_s) entries;
} slist_thread_t;

SLIST_HEAD(slisthead, slist_thread_s) head;


pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

const char filename[] = "/var/tmp/aesdsocketdata";
int sockfd, new_sockfd;
int wrfd;
volatile int terminate = 0;

pthread_t timestamp_id;

/**********************************************************************************
 * @name       signal_handler()
 *
 * @brief      { Gracefully exits when SIGINT or SIGTERM is received. }
 *
 * @param[in]  signo
 * 
 * @return     None 
 **********************************************************************************/
void signal_handler(int signo)
{
    printf("Caught signal %d, exiting\n", signo);
	syslog(LOG_DEBUG, "Caught signal, exiting\n");
	
	terminate = 1;
	
	if (sockfd != -1) close(sockfd);
    if (new_sockfd != -1) close(new_sockfd);
    if (wrfd != -1) close(wrfd);
	
	if (remove(filename)){
		perror("remove");
		syslog(LOG_ERR, "remove file failed.");
		exit(1);
	}
	
	// join timestamp thread
	pthread_join(timestamp_id, NULL);
}

/**********************************************************************************
 * @name       set_signal()       
 **********************************************************************************/
void set_signal(void)
{
	struct sigaction sa;
	
	// Set signal handler
	sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);	
}

/**********************************************************************************
 * @name       append_timestamp()       
 **********************************************************************************/
void* append_timestamp(void* arg)
{
	int ret;
	ssize_t ret_byte;
	time_t rawtime;
    struct tm * timeinfo;
    char buffer [80];
    int fd;
	
	while (!terminate){
		sleep(10);
		
	    // Get wall time	
        time (&rawtime);
        timeinfo = localtime (&rawtime);
        strftime(buffer, sizeof(buffer), "timestamp:%a %b %d %H:%M:%S %Y\n", timeinfo);
	
	    // Lock file
        ret = pthread_mutex_lock(&file_mutex);		
        if (ret){
		    perror("pthread_mutex_lock");
		    exit(1);
        }
	
		fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0664);
		if (fd == -1){
			perror("open");
			syslog(LOG_ERR, "open");
			exit(1);
		}
	
		// Append	
		ret_byte = write(fd, buffer, strlen(buffer));
		if (ret_byte != strlen(buffer)){
			perror("write");
			syslog(LOG_ERR, "write");
			pthread_mutex_unlock(&file_mutex);
			exit(1);
		}
			
		ret = pthread_mutex_unlock(&file_mutex);
		if (ret){
			perror("pthread_mutex_lock");
			exit(1);
		}
		// Unlock file
		
		close(fd);
    }
	
	pthread_exit(NULL);
}


/**********************************************************************************
 * @name       socketThread()
 *
 * @brief      { Socket thread for send and receive. }
 *
 * @param[in]  threadp { Pointer to thread parameter }
 * 
 * @return     None 
 **********************************************************************************/
void *socketThread(void *arg)
{	
	// Return code
	int ret;
	ssize_t ret_byte, bytes_to_wr, bytes_to_send;
	// Buffer
	char recv_buf[BUFF_SIZE];
	char send_buf[BUFF_SIZE];
	// Flags
    int send_enable = 0;
	// FD
	int rdfd;
	
	slist_thread_t *thread_data = (slist_thread_t *)arg;
	
    // Receives data over the connection, until client closes connection, return 0
    while ((ret_byte = recv(thread_data->client_fd, recv_buf, sizeof(recv_buf), 0)) > 0){
		if (ret_byte == -1){
			perror("recv");
			syslog(LOG_ERR, "recv");
			pthread_exit(NULL);
		}
		// Appends to file 1 byte at a time
		bytes_to_wr = ret_byte;
		if (recv_buf[bytes_to_wr-1] == '\n') send_enable = 1;
		
		// Lock file
        ret = pthread_mutex_lock(&file_mutex);		
		if (ret){
		    perror("pthread_mutex_lock");
		    pthread_exit(NULL);
		}
		
		ret_byte = write(wrfd, recv_buf, bytes_to_wr);
		if (ret_byte == -1){
			perror("write");
			syslog(LOG_ERR, "write");
			pthread_mutex_unlock(&file_mutex);
			pthread_exit(NULL);
		}
/*
		ret = pthread_mutex_unlock(&file_mutex);
		if (ret){
		    perror("pthread_mutex_lock");
		    pthread_exit(NULL);
		}		
        // Unlock file
*/		
		if (!send_enable) continue; // keep receiving
		else {                      // send whole file		
			rdfd = open(filename, O_RDONLY);
			if (rdfd == -1){
				perror("open");
				syslog(LOG_ERR, "open");
				pthread_mutex_unlock(&file_mutex);
				pthread_exit(NULL);
			}

			while ((ret_byte = read(rdfd, send_buf, sizeof(send_buf))) > 0){
				if (ret_byte == -1){
					perror("read");
					syslog(LOG_ERR, "read");
					pthread_mutex_unlock(&file_mutex);
					pthread_exit(NULL);
				}
					
				bytes_to_send = ret_byte;
				ret_byte = send(thread_data->client_fd, send_buf, bytes_to_send, 0);
				if (ret_byte == -1){
					perror("send");
					syslog(LOG_ERR, "send");
					pthread_mutex_unlock(&file_mutex);
					pthread_exit(NULL);;
				}
			}
			close(rdfd);
			send_enable = 0;
			
			ret = pthread_mutex_unlock(&file_mutex);
		    if (ret){
		        perror("pthread_mutex_lock");
		        pthread_exit(NULL);
		    }		
            // Unlock file
		}
	}
		
	// Logs message to the syslog “Closed connection from XXX”
	printf("Closed connection from %s\n", thread_data->client_ip);
	syslog(LOG_DEBUG, "Closed connection from %s\n", thread_data->client_ip);
	if (close(thread_data->client_fd)){
		perror("close");
		syslog(LOG_ERR, "close failed.");
		pthread_exit(NULL);
	}
	
	thread_data->thread_complete = true;
	pthread_exit(NULL);
}

/**********************************************************************************
 * Main functions        
 **********************************************************************************/
int main(int argc, char **argv)
{
	// Server
	struct addrinfo hints, *servinfo;
	// Client
	struct sockaddr client_addr;
	socklen_t addr_size;
	char client_ip[INET_ADDRSTRLEN];
	// Return code
	int ret;
	// Flags
	int daemon_mode = 0;
	
	SLIST_INIT(&head);
	
	// Set up signal
	set_signal();
	
	if (argc == 2 && strcmp(argv[1], "-d") == 0) daemon_mode = 1;
	
	// Open file, create if not exist
	wrfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0664);
    if (wrfd == -1){
		perror("open");
		syslog(LOG_ERR, "open");
		exit(1);
	}
	
	// Opens a stream socket bound to port 9000
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1){
	    perror("socket");
		syslog(LOG_ERR, "socket create failed.");
		exit(1);
	}
	
	memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	ret = getaddrinfo(NULL, "9000", &hints, &servinfo);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		syslog(LOG_ERR, "getaddrinfo failed with errcode %d.", ret);
        exit(1);
    }

	// bind()
	ret = bind(sockfd, servinfo->ai_addr, sizeof(struct sockaddr));
	if (ret == -1){
	    perror("bind");
		syslog(LOG_ERR, "bind failed.");
		exit(1);
	}
	// Free memory
	freeaddrinfo(servinfo); // all done with this structure
	
	// support a -d argument to run as a daemon
	// should fork after ensuring it can bind to port 9000
	if (daemon_mode){
		pid_t pid;
	
	    pid = fork();
	
	    if (pid == -1){ // error
		    perror ("fork");
		    syslog(LOG_ERR, "fork failed.");
            exit(1);
	    }
	    if (pid > 0){
			printf("Start in daemon mode.\n");
			exit(0); // parent exit
		}
	}
	
	// Listens for and accepts a connection
	ret = listen(sockfd, LISTEN_BACKLOG);
	if (ret == -1){
	    perror("listen");
		syslog(LOG_ERR, "listen failed.");
		exit(1);
	}
	
	// Start timestamp thread
	ret = pthread_create(&timestamp_id, NULL, append_timestamp, NULL);
	if (ret){
		perror("pthread_create");
		exit(1);
	}
	
	while (1){
		addr_size = sizeof(client_addr);
		new_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size); // return new file descriptor
		if (new_sockfd == -1){
			perror("accept");
			syslog(LOG_ERR, "accept failed.");
			exit(1);
		}

		// Logs message for successful connection
		inet_ntop(AF_INET, &client_addr.sa_data, client_ip, sizeof(client_ip));
		printf("Accepted connection from %s\n", client_ip);
		syslog(LOG_DEBUG, "Accepted connection from %s\n", client_ip);
        
		slist_thread_t *thread_node = (slist_thread_t *) malloc(sizeof(slist_thread_t));
		
		thread_node->client_fd = new_sockfd;
		thread_node->thread_complete = false;
		strcpy(thread_node->client_ip, client_ip);
		
		// Create thread
		ret = pthread_create(&thread_node->thread_id, NULL, socketThread, thread_node);
	    if (ret){
		    perror("pthread_create");
		    free(thread_node);
			close(new_sockfd);
		    //return false;
			exit(1);
	    }
		
	    // Insert node to linked list
		SLIST_INSERT_HEAD(&head, thread_node, entries);
		
	    // Check thread_complete and join the thread
		// Remove node
		slist_thread_t *var = SLIST_FIRST(&head);
        slist_thread_t *temp_var;

        while (var != NULL) {
			temp_var = SLIST_NEXT(var, entries);

			if (var->thread_complete) {
				pthread_join(var->thread_id, NULL);
				SLIST_REMOVE(&head, var, slist_thread_s, entries);
				free(var);
			}

			var = temp_var;
        }
	}
	
    return 0;
}