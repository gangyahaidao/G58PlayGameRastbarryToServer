/*
 * ClientSocket.cpp
 *
 *  Created on: 2018年9月19日
 *      Author: rosrobot
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "ClientSocket.h"
#ifdef __cplusplus
}
#endif

int socket_fd = 0;
struct sockaddr_in addr;
socklen_t addr_length = 0;
void init_udp_client_socket(int port){
    // 初始化接收导航数据socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) {
            perror("socket error");
            exit(-1);
    }
    addr_length = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Receive data sent from any IP addr
    // Binding
    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Fail to bind socket_fd");
            exit(-1);
    }
    fcntl(socket_fd, F_SETFL, fcntl(socket_fd, F_GETFL, 0) | O_NONBLOCK); //设置非阻塞
    printf("Start receiving guide messages at port %d...\n", port);
}

void clientReceiveSocketMsg(char* recv_buffer, int buffer_len) {
	int len = recvfrom(socket_fd, recv_buffer, sizeof(buffer_len), 0, (struct sockaddr*)&addr, &addr_length);
	if (len > 0) {
		printf("--recv msg = %s\n", recv_buffer);
	}
}

int clientSendSocketMsg(char* content_buf, int content_len){
	int ret = sendto(socket_fd, content_buf, content_len, 0, (struct sockaddr*)&addr, addr_length);
	if(ret < 0) {
		printf("--接收数据错误\n");
	}
	return ret;
}

/**
 * 初始化tcp相关的socket
 * */
int init_tcp_socket_client_noblock(int* sockfd, const char* serverHost, int port) {
	if((*sockfd = socket(AF_INET,SOCK_STREAM,0))<0)
	{
		perror("Create socket failed");
		return -1;
	}
	//处理域名
	struct hostent * ent;
	if((ent = gethostbyname(serverHost))==NULL)
	{
		perror("Gethostbyname failed");
		return -1;
	}
	//2.和服务器建立连接
	struct sockaddr_in peer;
	bzero(&peer, sizeof(peer));
	peer.sin_family = AF_INET;
	peer.sin_port = htons(port);
	//inet_pton(AF_INET,serverHost,&peer.sin_addr);
	memcpy(&peer.sin_addr,ent->h_addr,sizeof(peer.sin_addr));
	memset(peer.sin_zero,0,sizeof(peer.sin_zero));
	if(connect(*sockfd,(struct sockaddr*)&peer,sizeof(peer))<0)
	{
		perror("Connect failed");
		return -1;
	}
	fcntl(*sockfd, F_SETFL, fcntl(*sockfd, F_GETFL, 0) | O_NONBLOCK); //设置非阻塞
	return 0;
}

int init_tcp_socket_client_block(int* sockfd, const char* serverHost, int port) {
	if((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("--Create socket failed");
		return -1;
	}
	//处理域名
	struct hostent * ent;
	if((ent = gethostbyname(serverHost))==NULL)
	{
		perror("Gethostbyname failed");
		return -1;
	}
	//2.和服务器建立连接
	struct sockaddr_in peer;
	bzero(&peer, sizeof(peer));
	peer.sin_family = AF_INET;
	peer.sin_port = htons(port);
	// inet_pton(AF_INET,serverHost,&peer.sin_addr);
	memcpy(&peer.sin_addr, ent->h_addr, sizeof(peer.sin_addr));
	memset(peer.sin_zero, 0, sizeof(peer.sin_zero));
	if(connect(*sockfd, (struct sockaddr*)&peer, sizeof(peer)) < 0)
	{
		perror("Connect failed");
		return -1;
	}
	return 0;
}

int sendTcpMsg(int clientSocketfd, char* sendServerMsg, int len){
	if(clientSocketfd < 0) { // 表示接收心跳超时已经断开连接
		return -1;
	}
	return send(clientSocketfd, sendServerMsg, len, 0);
}

