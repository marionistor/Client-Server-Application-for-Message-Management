#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <bits/stdc++.h>

#include "utils.h"

using namespace std;

// init the socket used for udp connections
int init_udp_socket(uint16_t port, struct sockaddr_in &udp_sockaddr)
{
    int rc;
    int udp_listenfd;
    socklen_t udp_sock_len = sizeof(struct sockaddr_in);

    udp_listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    DIE(udp_listenfd < 0, "udp socket error");

    memset(&udp_sockaddr, 0, udp_sock_len);
    udp_sockaddr.sin_family = AF_INET;
    udp_sockaddr.sin_port = htons(port);
    udp_sockaddr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(udp_listenfd, (struct sockaddr *) &udp_sockaddr, sizeof(udp_sockaddr));
    DIE(rc < 0, "udp bind error");

    return udp_listenfd;
}

// init the socket used for accepting tcp connections
int init_tcp_socket(uint16_t port, struct sockaddr_in &tcp_sockaddr)
{
    int rc;
    int tcp_listenfd;
    socklen_t tcp_sock_len = sizeof(struct sockaddr_in);
    int optval;

    tcp_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(tcp_listenfd < 0, "tcp socket error");

    // make socket address reusable
    optval = 1;
    rc = setsockopt(tcp_listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    DIE(rc < 0, "setsockopt reuse addr and port error");
    
    // disable Nagle's algorithm
    optval = 1;
    rc = setsockopt(tcp_listenfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int));
    DIE(rc < 0, "setsockopt TCP_NODELAY error");

    memset(&tcp_sockaddr, 0, tcp_sock_len);
    tcp_sockaddr.sin_family = AF_INET;
    tcp_sockaddr.sin_port = htons(port);
    tcp_sockaddr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(tcp_listenfd, (struct sockaddr *) &tcp_sockaddr, sizeof(tcp_sockaddr));
    DIE(rc < 0, "tcp bind error");

    rc = listen(tcp_listenfd, MAX_CONNECTIONS);
    DIE(rc < 0, "tcp listen error");

    return tcp_listenfd;
}

/* function for signaling all online clients to close 
 * (used when the server is closing) */
void send_exit_to_subscribers(map<string, client_status> ids_map)
{
    bool exit_msg = true;

    for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
        /* send true value to all online clients to
         * signal them to close */
        if (it->second.is_online == true) {
            send_all(it->second.current_sockfd, &exit_msg, sizeof(bool));
            close(it->second.current_sockfd);
        }
    }
}

// function used for splitting topic words having the separator '/'
vector<string> split_topic(string topic)
{
    stringstream topic_stream(topic);
    vector<string> tokens;
    string token;
    char delim = '/';

    while (getline(topic_stream, token, delim)) {
        tokens.push_back(token);
    }

    return tokens;
}

/* function for matching a topic sent by the udp client to a topic that may
 * contain wildcards */
bool match_topics(vector<string> udp_topic_tokens, vector<string> client_topic_tokens, size_t udp_index, size_t client_index)
{
    // if we traversed both the topics succesfully it means that they match
    if (udp_index == udp_topic_tokens.size() && client_index == client_topic_tokens.size())
        return true;

    /* if we traversed only one topic and we still have remaining words from the
     * other one it means that they don't match */
    if (udp_index == udp_topic_tokens.size() || client_index == client_topic_tokens.size())
        return false;

    /* if the topic contains the plus wildcard or a word that matches the same word
     * as the udp client topic we move to the next words from both topics by
     * incrementing the indices */
    if (client_topic_tokens[client_index].compare("+") == 0 || client_topic_tokens[client_index].compare(udp_topic_tokens[udp_index]) == 0) {
        return match_topics(udp_topic_tokens, client_topic_tokens, udp_index + 1, client_index + 1);

    /* if the topic contains the star wildcard we check all the remaining words from the udp client
     * with the next word of the topic recursively */
    } else if (client_topic_tokens[client_index].compare("*") == 0) {
        for (size_t current_index = udp_index; current_index <= udp_topic_tokens.size(); current_index++) {
             if (match_topics(udp_topic_tokens, client_topic_tokens, current_index, client_index + 1))
                return true;           
        }
        
        // if we didn't find any match it means that the topics don't match
        return false;
    } else {
        return false;
    }
}

/* function for sending the packet from the udp client to the
 * tcp clients that are subscribed to the topic in that packet */
void send_to_subscribers(map<string, client_status> ids_map, server_udp_packet udp_packet, struct sockaddr_in udp_addr)
{
    // initialise the structure that contains packet info
    server_packet_info packet_info;

    packet_info.ip = udp_addr.sin_addr;
    packet_info.port = ntohs(udp_addr.sin_port);
    packet_info.topic_len = strlen(udp_packet.topic);
    packet_info.type = udp_packet.type;

    /* initialise the size of the content based on the type
     * to avoid sending more bytes than necessary */
    switch (udp_packet.type) {
        case 0:
           packet_info.content_len = 1 + sizeof(uint32_t);
           break;
        case 1:
            packet_info.content_len = sizeof(uint16_t);
            break;
        case 2:
            packet_info.content_len = 1 + sizeof(uint32_t) + sizeof(uint8_t);
            break;
        case 3:
            packet_info.content_len = strlen(udp_packet.content);
            break;
        default:
            break; 
    }

    int total_len = packet_info.content_len + packet_info.topic_len + 1;

    char buff[total_len];
    memset(buff, 0, total_len);

    // the buffer will contain the topic and the content from the packet
    memcpy(buff, &udp_packet.topic, packet_info.topic_len);
    memcpy(buff + packet_info.topic_len, &udp_packet.content, packet_info.content_len);

    // send false value to clients to tell them not to close
    bool exit_msg = false;

    for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
        if (it->second.is_online == true) {
            string udp_topic(udp_packet.topic);

            for (string client_topic : it->second.topics) {
                vector<string> udp_topic_tokens = split_topic(udp_topic);
                vector<string> client_topic_tokens = split_topic(client_topic);
                // send the data for the first topic that matches the udp topic
                if (match_topics(udp_topic_tokens, client_topic_tokens, 0, 0)) {
                    send_all(it->second.current_sockfd, &exit_msg, sizeof(bool));
                    send_all(it->second.current_sockfd, &packet_info, sizeof(server_packet_info));
                    send_all(it->second.current_sockfd, &buff, total_len);
                    break;
                }
            }
        }
    }
}

void run_server(int tcp_listenfd, int udp_listenfd)
{
    int rc;
    bool close_serv = false;
    ssize_t bytes_recv;
    
    // we first have a capacity of MAX_CONNECTIONS for incoming connections
    struct pollfd *poll_fds = (struct pollfd *) malloc(MAX_CONNECTIONS * sizeof(struct pollfd));
    int num_fds = 3;

    /* map that stores client id along with relevant info such as
     * topics, status (online/offline) and the socket fd used
     * by the server to communicate to that client */
    map<string, client_status> ids_map;

    // fd for receiving input commands 
    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;

    // fd for accepting tcp connections
    poll_fds[1].fd = tcp_listenfd;
    poll_fds[1].events = POLLIN;

    // fd for receiving packets from udp clients
    poll_fds[2].fd = udp_listenfd;
    poll_fds[2].events = POLLIN;

    while (1) {
        // the server must close
        if (close_serv == true)
            break;
        
        rc = poll(poll_fds, num_fds, -1);
        DIE(rc < 0, "poll error");        

        for (int i = 0; i < num_fds; i++) {
            if (poll_fds[i].revents & POLLIN) {
                // input command
                if (poll_fds[i].fd == STDIN_FILENO) {
                    char buff[MSG_SIZE + 1];

                    memset(buff, 0, MSG_SIZE + 1);
                    fgets(buff, sizeof(buff), stdin);
                    buff[strlen(buff) - 1] = '\0';

                    // server received exit command
                    if (strcmp(buff, "exit") == 0) {
                        // signals all clients to close
                        send_exit_to_subscribers(ids_map);
                        close_serv = true;
                        break;
                    } else {
                        // any other command received is considered invalid
                        fprintf(stderr, "Wrong command\n");
                        continue;
                    }
                // new tcp connection request
                } else if (poll_fds[i].fd == tcp_listenfd) {
                    int new_tcp_sockfd;
                    int optval;
                    struct sockaddr_in tcp_subscriber_addr;
                    socklen_t addr_len = sizeof(struct sockaddr_in);

                    new_tcp_sockfd = accept(tcp_listenfd, (struct sockaddr *)&tcp_subscriber_addr, &addr_len);
                    DIE(new_tcp_sockfd < 0, "accept error");

                    // disable Nagle's algorithm for the new socket
                    optval = 1;
                    rc = setsockopt(new_tcp_sockfd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int));
                    DIE(rc < 0, "setsockopt new_tcp_sockfd error");

                    // first receive subscriber info
                    subscriber_info subs_info;
                    bytes_recv = recv(new_tcp_sockfd, &subs_info, sizeof(subscriber_info), MSG_WAITALL);
                    DIE(bytes_recv < 0, "Error while receiving subscriber information");

                    char id[subs_info.id_len];
                    memset(id, 0, subs_info.id_len);

                    // receive the id of the subscriber
                    bytes_recv = recv(new_tcp_sockfd, &id, subs_info.id_len, MSG_WAITALL);
                    DIE(bytes_recv < 0, "Error while receiving id");

                    string id_str(id);
                    bool id_exists = false;

                    /* check if the client has already connected once to the
                     * server or is still connected */
                    for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
                        if (it->first == id_str) {
                            id_exists = true;

                            /* if the client is online we can't have two clients
                             * with the same id and we request the new client to
                             * close */
                            if (it->second.is_online == true) {
                                cout << "Client " << id_str << " already connected.\n";

                                // send true value to signal client to close
                                bool exit_msg = true;
                                send_all(new_tcp_sockfd, &exit_msg, sizeof(bool));
                                close(new_tcp_sockfd);
                            // if the client isn't online it means it is reconnecting
                            } else {
                                // add new socket fd
                                poll_fds[num_fds].fd = new_tcp_sockfd;
                                poll_fds[num_fds].events = POLLIN;
                                num_fds++;

                                /* if the number of fds has become a multiple of MAX_CONNECTIONS we
                                 * realloc the pollfd structure with a size that is double the current
                                 * number of fds */
                                if (num_fds % MAX_CONNECTIONS == 0)
                                    poll_fds = (struct pollfd *) realloc(poll_fds, 2 * num_fds);

                                it->second.current_sockfd = new_tcp_sockfd;
                                it->second.is_online = true;

                                cout << "New client " << id_str << " connected from " << inet_ntoa(subs_info.ip) << ":" << subs_info.port << ".\n";
                            }

                            break;
                        }
                    }

                    // client hasn't connected before
                    if (id_exists == false) {
                        // add new socket fd
                        poll_fds[num_fds].fd = new_tcp_sockfd;
                        poll_fds[num_fds].events = POLLIN;
                        num_fds++;

                        /* if the number of fds has become a multiple of MAX_CONNECTIONS we
                         * realloc the pollfd structure with a size that is double the current
                         * number of fds */
                        if (num_fds % MAX_CONNECTIONS == 0)
                            poll_fds = (struct pollfd *) realloc(poll_fds, 2 * num_fds);

                        // add the new client to the map
                        client_status current_client = {new_tcp_sockfd, vector<string>(), true};
                        ids_map.insert({id_str, current_client});
                        cout << "New client " << id_str << " connected from " << inet_ntoa(subs_info.ip) << ":" << subs_info.port << ".\n";
                    }
                // message from udp client
                } else if (poll_fds[i].fd == udp_listenfd) {
                    server_udp_packet udp_packet;
                    struct sockaddr_in udp_subs_sockaddr;
                    socklen_t udp_addr_len = sizeof(udp_subs_sockaddr);

                    // receive the packet in a struct server_udp_packet
                    bytes_recv = recvfrom(udp_listenfd, &udp_packet, sizeof(server_udp_packet), MSG_WAITALL, (struct sockaddr *) &udp_subs_sockaddr, &udp_addr_len);
                    DIE(bytes_recv < 0, "Error while receiving packet from udp client");

                    /* send packet to clients that are subscribed to a topic
                     * that matches the topic from the packet */
                    send_to_subscribers(ids_map, udp_packet, udp_subs_sockaddr);
                // message from the connected tcp clients
                } else {
                    int len;
                    // first receive the length of the message
                    bytes_recv = recv(poll_fds[i].fd, &len, sizeof(int), MSG_WAITALL);
                    DIE(bytes_recv < 0, "Error receiving tcp subscriber message length");

                    char buff[len];
                    // then receive the buffer with the message
                    bytes_recv = recv(poll_fds[i].fd, &buff, len, MSG_WAITALL);
                    DIE(bytes_recv < 0, "Error receiving tcp subscriber message");

                    // client has disconnected
                    if (strcmp(buff, "exit") == 0) {
                        for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
                            if (it->second.current_sockfd == poll_fds[i].fd) {
                                it->second.is_online = false;
                                // close socket fd used for communicating with client
                                close(poll_fds[i].fd);

                                // erase the socket fd from pollfd structure
                                for (int j = i; j < num_fds - 1; j++) {
                                    poll_fds[j] = poll_fds[j + 1];
                                }

                                memset(&poll_fds[num_fds - 1], 0, sizeof(struct pollfd));

                                num_fds--;

                                cout << "Client " << it->first << " disconnected.\n";
                                break;                                
                            }
                        }
                    }

                    // the client wants to subscribe to a topic
                    if (strncmp(buff, "subscribe ", 10) == 0) {
                        string topic(buff + 10);

                        /* if the client isn't already subscribed to that topic
                         * we add the topic to the topics that the client is
                         * subscribed to */
                        for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
                            if (it->second.current_sockfd == poll_fds[i].fd) {
                                if (find(it->second.topics.begin(), it->second.topics.end(), topic) == it->second.topics.end()) {
                                    it->second.topics.push_back(topic);
                                }
                            }
                        }
                    // the client wants to unsubscribe from a topic
                    } else if (strncmp(buff, "unsubscribe ", 12) == 0) {
                        string topic(buff + 12);

                        /* if the client is subscribed to that topic we delete the
                         * topic from the topics that the client is subscribed
                         * to */
                        for (auto it = ids_map.begin(); it != ids_map.end(); it++) {
                            if (it->second.current_sockfd == poll_fds[i].fd) {
                                auto found = find(it->second.topics.begin(), it->second.topics.end(), topic);

                                if (found != it->second.topics.end()) {
                                    it->second.topics.erase(found);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // deallocate pollfd structure memory
    free(poll_fds);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 2) {
        fprintf(stderr, "\n Usage: %s <PORT>\n", argv[0]);
        return 1;
    }

    int rc;
    uint16_t port;

    rc = sscanf(argv[1], "%hu", &port);
    DIE(rc != 1, "Invalid port given");

    int udp_listenfd, tcp_listenfd;
    struct sockaddr_in udp_sockaddr, tcp_sockaddr;

    // init the socket used for udp connections
    udp_listenfd = init_udp_socket(port, udp_sockaddr);
    // init the socket used for accepting tcp connections
    tcp_listenfd = init_tcp_socket(port, tcp_sockaddr);

    run_server(tcp_listenfd, udp_listenfd);    

    close(udp_listenfd);
    close(tcp_listenfd);

    return 0;
}
