#include <arpa/inet.h>
#include <cstdint>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "utils.h"

void run_subscriber(int sockfd) 
{
    int rc;
    ssize_t bytes_recv;
    struct pollfd poll_fds[2];

    // fd for receiving input commands
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;

    // fd for receiving messages from the server
    poll_fds[1].fd = sockfd;
    poll_fds[1].events = POLLIN;

    while (1) {
        rc = poll(poll_fds, 2, -1);
        DIE(rc < 0, "poll error");

        // input command
        if (poll_fds[0].revents & POLLIN) {
            int len;
            char buff[MSG_SIZE + 1];

            memset(buff, 0, MSG_SIZE + 1);
            fgets(buff, sizeof(buff), stdin);
            buff[strlen(buff) - 1] = '\0';
            // calculate the length of the buffer
            len = strlen(buff) + 1; 

            // first send the len of the buffer
            send_all(sockfd, &len, sizeof(int));
            /* then send exactly len bytes of the buffer to avoid sending
             * more than necesary */
            send_all(sockfd, &buff, len);

            // subscriber received exit command
            if (strcmp(buff, "exit") == 0) {
                break;                
            }

            // subscriber received subscribe <topic> command
            if (strncmp(buff, "subscribe ", 10) == 0) {
                // calculate topic length including the '\0' at the end of the string
                int topic_len = len - 10;

                /* if topic has length 1 (it means the string contains only '\0') or
                 * it has a length greater than 51 (the topic must have at most 50 bytes
                 * without '\0') then it is invalid */
                if (topic_len == 1 || topic_len > 51) {
                    fprintf(stderr, "Invalid topic\n");
                    continue;
                }

                char topic[topic_len];
                strcpy(topic, buff + 10);
                printf("Subscribed to topic %s\n", topic);
            
            // subscriber received unsubscribe <topic> command
            } else if (strncmp(buff, "unsubscribe ", 12) == 0) {
                // calculate topic length including the '\0' at the end of the string
                int topic_len = len - 12;
                
                /* if topic has length 1 (it means the string contains only '\0') or
                 * it has a length greater than 51 (the topic must have at most 50 bytes
                 * without '\0') then it is invalid */
                if (topic_len == 1 || topic_len > 51) {
                    fprintf(stderr, "Invalid topic\n");
                    continue;
                }

                char topic[topic_len];
                strcpy(topic, buff + 12);
                printf("Unsubscribed from topic %s\n", topic);
            } else {
                // any other command is considered invalid
                fprintf(stderr, "Wrong command\n");
                continue;
            }
        }

        // server message
        if (poll_fds[1].revents & POLLIN) {
            bool exit_msg;
            
            /* first receive exit message and check if the server requested 
             * the subscriber to close */
            bytes_recv = recv(sockfd, &exit_msg, sizeof(bool), MSG_WAITALL);
            DIE(bytes_recv < 0, "Error receving exit message");

            // if it has value true the subscriber must close
            if (exit_msg)
                break;

            /* if the server didn't request the subscriber to close receive
             * info of the packet sent by the udp client to the server */
            server_packet_info packet_info;
            bytes_recv = recv(sockfd, &packet_info, sizeof(server_packet_info), MSG_WAITALL);
            DIE(bytes_recv < 0, "Error receving packet info");
            // get the length of the packet that contains the topic and content sent by the udp client
            int total_len = packet_info.topic_len + packet_info.content_len + 1;

            char buff[total_len];
            memset(buff, 0, total_len);

            // receive buffer
            bytes_recv = recv(sockfd, &buff, total_len, MSG_WAITALL);
            DIE(bytes_recv < 0, "Error receiving topic and content");

            char *ip_string = inet_ntoa(packet_info.ip);

            char topic[packet_info.topic_len + 1];
            memset(topic, 0, packet_info.topic_len + 1);
            // extract the topic from the buffer
            strncpy(topic, buff, packet_info.topic_len);

            // extract the content from the buffer
            char *content = buff + packet_info.topic_len; 

            switch (packet_info.type) {
                // INT case
                case 0: {
                    uint32_t abs_number = htonl(*(uint32_t *) (content + 1));
                    int int_number;
                    // *content represents the sign byte
                    if (*content == 1)
                        int_number = -abs_number;
                    else
                        int_number = abs_number;
                    
                    printf("%s:%hu - %s - INT - %d\n", ip_string, packet_info.port, topic, int_number);
                    break;
                }
                // SHORT_REAL case
                case 1: {
                    // divide the number by 100 to get the real value
                    double real_number = (double) htons((*(uint16_t *) content)) / 100;
                    
                    printf("%s:%hu - %s - SHORT_REAL - %.2lf\n", ip_string, packet_info.port, topic, real_number);
                    break;
                }
                // FLOAT case
                case 2: {
                    // get the module of the number
                    uint32_t abs_number = htonl(*(uint32_t *) (content + 1));
                    // get the exponent
                    uint8_t exp = *(uint8_t *) (content + 5);
                    // multiply the module with 10 ^ (-exp) to obtain the real value
                    double abs_float_number = abs_number * pow(10, -exp);
                    double float_number;

                    // *content represents the sign byte
                    if (*content == 1)
                        float_number = -abs_float_number;
                    else
                        float_number = abs_float_number;

                    printf("%s:%hu - %s - FLOAT - %lf\n", ip_string, packet_info.port, topic, float_number);
                    break;
                }
                // STRING case
                case 3: {
                    printf("%s:%hu - %s - STRING - %s\n", ip_string, packet_info.port, topic, content);
                    break;
                }
                // any other type is considered invalid
                default:
                    fprintf(stderr, "Wrong packet format\n");
                    break;
            }
        }
    }
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    
    if (argc != 4) {
        fprintf(stderr, "\n Usage: %s <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n", argv[0]);
        return 1;
    }

    if (strlen(argv[1]) > 10) {
        fprintf(stderr, "Client ID must have up to 10 characters\n");
        return 1;
    }

    int rc;
    uint16_t port;

    rc = sscanf(argv[3], "%hu", &port);
    DIE(rc != 1, "Invalid port given");

    int sockfd;
    struct sockaddr_in serv_addr;
    socklen_t sock_len = sizeof(sockaddr_in);
    int optval;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "socket error");

    // disable Nagle's alogrithm
    optval = 1;
    rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int));
    DIE(rc < 0, "setsockopt error");

    memset(&serv_addr, 0, sock_len);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    rc = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
    DIE(rc <= 0, "inet_pton error");

    rc = connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    DIE(rc < 0, "connect error");

    // first send a struct that will contain ip, port and the length of the id
    subscriber_info subs_info;
    subs_info.ip = serv_addr.sin_addr;
    subs_info.port = port;
    subs_info.id_len = strlen(argv[1]) + 1;

    send_all(sockfd, &subs_info, sizeof(subscriber_info));

    // send the id of the subscriber
    send_all(sockfd, argv[1], subs_info.id_len);

    run_subscriber(sockfd);

    close(sockfd);

    return 0;
}
