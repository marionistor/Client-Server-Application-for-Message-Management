#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "utils.h"

// function used to send exactly len bytes from the buffer
int send_all(int sockfd, void *buffer, size_t len)
{
    ssize_t total_sent = 0;
    ssize_t total_remaining = len;
    char *buff = (char *) buffer;

    while (total_remaining) {
        ssize_t currently_sent = send(sockfd, buff + total_sent, total_remaining, 0);
        DIE(currently_sent < 0, "Error while sending packet");

        total_sent += currently_sent;
        total_remaining -= currently_sent; 
    }

    return total_sent;
}
