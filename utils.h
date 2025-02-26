#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <bits/stdc++.h>

using namespace std;

#define DIE(assertion, call_description)                                       \
  do {                                                                         \
    if (assertion) {                                                           \
      fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                       \
      perror(call_description);                                                \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define MSG_SIZE 1024
#define MAX_CONNECTIONS 1024

/* struct used to store the current socket fd used by
 * server to comunicate with the client, the topics
 * to which the client is subscribed to and a value that
 * is true if the client is online */
struct client_status {
    int current_sockfd;
    vector<string> topics;
    bool is_online;
};

// struct that represents the packet received from the udp client
struct server_udp_packet {
    char topic[50];
    uint8_t type;
    char content[1500];
};

/* struct that contains info of the packet sent by the udp client
 * (ip and port of the client, type of content and the lengths of
 * the topic and content) */
struct server_packet_info {
    int topic_len;
    int content_len;
    uint8_t type;
    struct in_addr ip;
    uint16_t port;
};

/* struct that contains info of the subscriber that is
 * trying to connect to the server (ip, port and the length
 * of the id) */
struct subscriber_info {
    struct in_addr ip;
    uint16_t port;
    int id_len;
};

// function used to send exactly len bytes from the buffer
int send_all(int sockfd, void *buffer, size_t len);

#endif
