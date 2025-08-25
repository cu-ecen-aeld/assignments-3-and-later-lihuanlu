 /*********************************************************************************************
 * @file    writer.c
 * @brief   An alternative to the "writer.sh" test script created in assignment1. 
 *
 * @author        <Li-Huan Lu>
 * @date          <08/24/2025>
 * @reference     Linux System Programming Chapter 2.
 **********************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>

int main (int argc, char *argv[])
{
	int fd;
	ssize_t ret;
	char filename[128];
	char writestr[128];
	
	// Logging with LOG_USER facility.
    openlog(NULL, 0, LOG_USER);
	
	// Check arguments.
	if (argc < 3){
		printf("File directory or write string missing.\n");
		printf("Usage: %s /path/filename writestr.\n", argv[0]);
		syslog(LOG_ERR, "File directory or write string missing.\n");
		syslog(LOG_ERR, "Usage: %s /path/filename writestr.\n", argv[0]);
		exit(-1);
	}
	else{
        snprintf(filename, sizeof(filename), "%s", argv[1]);
        snprintf(writestr, sizeof(writestr), "%s", argv[2]);
	}
	
	// Assume directory was created by caller
	// Create and open file.
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd == -1){
		perror("open");
		syslog(LOG_ERR, "open");
		exit(-1);
	}
    
	// Write string to file.
    ret = write(fd, writestr, strlen(writestr))	;
    if (ret == -1){
		perror("write");
		syslog(LOG_ERR, "write");
		exit(-1);
	}

    syslog(LOG_DEBUG, "Writing %s to %s.\n", writestr, filename);
	
	// Close file.
    if (close(fd) == -1){
        perror ("close");
		syslog(LOG_ERR, "close");
		exit(-1);
	}
	
	return 0;
}