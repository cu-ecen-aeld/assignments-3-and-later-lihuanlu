 /**********************************************************************************
 * @file    aesdsocket.c
 * @brief   Assignment 5 part 1.
 *
 * @author        <Li-Huan Lu>
 * @date          <09/20/2025>
 * @reference     Linux manual page
 *                Lecture pdf
 *                https://beej.us/guide/bgnet/html/
 ***********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#define LISTEN_BACKLOG 50   // From linux manual page
#define BUFF_SIZE      1024

const char filename[] = "/var/tmp/aesdsocketdata";
int sockfd, new_sockfd;
int wrfd, rdfd;
	
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
	
	if (sockfd != -1) close(sockfd);
    if (new_sockfd != -1) close(new_sockfd);
    if (wrfd != -1) close(wrfd);
	if (rdfd != -1) close(rdfd);
	
	if (remove(filename)){
		perror("remove");
		syslog(LOG_ERR, "remove file failed.");
		exit(1);
	}
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
 * Main functions        
 **********************************************************************************/
int main(int argc, char **argv)
{
	// Server
	struct addrinfo hints, *servinfo;
	char recv_buf[BUFF_SIZE];
	char send_buf[BUFF_SIZE];
	//Client
	struct sockaddr client_addr;
	socklen_t addr_size;
	char client_ip[INET_ADDRSTRLEN];
	// Return code
	int ret;
	
    int send_enable = 0;
	int daemon_mode = 0;
	ssize_t ret_byte, bytes_to_wr, bytes_to_send;
	
	
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
			printf("Start in daemon.\n");
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

        // Receives data over the connection, until client closes connection, return 0
		while ((ret_byte = recv(new_sockfd, recv_buf, sizeof(recv_buf), 0)) > 0){
			if (ret_byte == -1){
				perror("recv");
				syslog(LOG_ERR, "recv");
				exit(1);
			}
			// Appends to file 1 byte at a time
			bytes_to_wr = ret_byte;
			if (recv_buf[bytes_to_wr-1] == '\n') send_enable = 1;
			
			ret_byte = write(wrfd, recv_buf, bytes_to_wr);
			if (ret_byte == -1){
				perror("write");
				syslog(LOG_ERR, "write");
				exit(1);
			}

			if (!send_enable) continue; // keep receiving
			else {                      // send whole file
				rdfd = open(filename, O_RDONLY);
				if (rdfd == -1){
					perror("open");
					syslog(LOG_ERR, "open");
					exit(1);
				}

				while ((ret_byte = read(rdfd, send_buf, sizeof(send_buf))) > 0){
					if (ret_byte == -1){
						perror("read");
						syslog(LOG_ERR, "read");
						exit(1);
					}
					
					bytes_to_send = ret_byte;
					ret_byte = send(new_sockfd, send_buf, bytes_to_send, 0);
					if (ret_byte == -1){
						perror("send");
						syslog(LOG_ERR, "send");
						exit(1);
					}
				}
				close(rdfd);
				send_enable = 0;
			}
		}
		
		// Logs message to the syslog “Closed connection from XXX”
		printf("Closed connection from %s\n", client_ip);
		syslog(LOG_DEBUG, "Closed connection from %s\n", client_ip);
		if (close(new_sockfd)){
			perror("close");
			syslog(LOG_ERR, "close failed.");
			exit(1);
		}
	}
	
    return 0;
}