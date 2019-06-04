/*
 * ClientSocket.h
 *
 *  Created on: 2018年9月19日
 *      Author: rosrobot
 */

#ifndef CLIENTSOCKET_H_
#define CLIENTSOCKET_H_

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>

void init_udp_client_socket(int port);
void clientReceiveSocketMsg(char* recv_buffer, int buffer_len);
int clientSendSocketMsg(char* content_buf, int content_len);
int init_tcp_socket_client_noblock(int* sockfd, const char* serverHost, int port);
int init_tcp_socket_client_block(int* sockfd, const char* serverHost, int port);
int sendTcpMsg(int clientSocketfd, char* sendServerMsg, int len);


#endif /* CLIENTSOCKET_H_ */
