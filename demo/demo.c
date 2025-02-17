﻿//
// Created by MightyPork on 2017/10/15.
//

#include "demo.h"

// those magic defines are needed so we can use clone()
#define _GNU_SOURCE
#define __USE_GNU

#ifdef _MSC_VER
#include <winsock2.h>
#include <Ws2tcpip.h>
#pragma comment(lib,"Ws2_32")
#define ssize_t int
#define close closesocket
#else
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <signal.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

volatile int sockfd = -1;
volatile bool conn_disband = false;
HANDLE g_Mutex;
TinyFrame* demo_tf;



#ifdef _MSC_VER
static void usleep(unsigned long usec)
{
	HANDLE timer;
	LARGE_INTEGER interval;
	interval.QuadPart = -(long)(10 * usec);

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &interval, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}
#endif
/**
 * Close socket
 */
void demo_disconn(void)
{
	conn_disband = true;
	if (sockfd >= 0) close(sockfd);
#ifdef _MSC_VER
	WSACleanup();
#endif // _MSC_VER
}

/**
 * Demo WriteImpl - send stuff to our peer
 *
 * @param buff
 * @param len
 */
void TF_WriteImpl(TinyFrame* tf, const uint8_t* buff, uint32_t len)
{
	printf("\033[32mTF_WriteImpl - sending frame:\033[0m\n");
	dumpFrame(buff, len);
	usleep(1000);

	if (sockfd != -1) {
		send(sockfd, buff, len, 0);
	}
	else {
		printf("\nNo peer!\n");
	}
}


/**
 * Client bg thread
 *
 * @param unused
 * @return unused
 */
#ifdef _MSC_VER
DWORD WINAPI demo_client(LPVOID unused)
#else
static int demo_client(void* unused)
#endif
{
	(void)unused;

	ssize_t n = 0;
	uint8_t recvBuff[1024];
	struct sockaddr_in serv_addr;

	printf("\n--- STARTING CLIENT! ---\n");

#ifdef _MSC_VER
	WaitForSingleObject(g_Mutex, INFINITE);
#endif // _MSC_VER

	memset(recvBuff, '0', sizeof(recvBuff));
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Error : Could not create socket \n");
		return false;
	}

#ifdef _MSC_VER
	ReleaseMutex(g_Mutex);
#endif // _MSC_VER


	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);

	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
		printf("\n inet_pton error occured\n");
		return false;
	}

	if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("\n Error : Connect Failed \n");
		perror("PERROR ");
		return false;
	}

	printf("\n Child Process \n");

	while ((n = recv(sockfd, recvBuff, sizeof(recvBuff) - 1, 0)) > 0) {
		// printf("\033[36m--- RX %ld bytes ---\033[0m\n", n);
		// dumpFrame(recvBuff, (size_t)n);
		TF_Accept(demo_tf, recvBuff, (size_t)n);
	}
	return 0;
}

/**
 * Server bg thread
 *
 * @param unused
 * @return unused
 */
#ifdef _MSC_VER
DWORD WINAPI demo_server(LPVOID unused)
#else
static int demo_server(void* unused)
#endif 
{
	(void)unused;
	ssize_t n;
	int listenfd = 0;
	uint8_t recvBuff[1024];
	struct sockaddr_in serv_addr;
	int option;

	printf("\n--- STARTING SERVER! ---\n");
#ifdef _MSC_VER
	WaitForSingleObject(g_Mutex, INFINITE);
#endif // _MSC_VER


	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));

	option = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(option));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(PORT);

	if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("Failed to bind");
		return 1;
	}

	if (listen(listenfd, 10) < 0) {
		perror("Failed to listen");
		return 1;
	}

#ifdef _MSC_VER
	ReleaseMutex(g_Mutex);
#endif // _MSC_VER
	while (1) {
		printf("\nWaiting for client...\n");
		sockfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(option));
		printf("\nClient connected\n");
		conn_disband = false;

		while ((n = recv(sockfd, recvBuff, sizeof(recvBuff) - 1, 0)) > 0 && !conn_disband) {
			// printf("\033[36m--- RX %ld bytes ---\033[0m\n", n);
			// dumpFrame(recvBuff, n);
			TF_Accept(demo_tf, recvBuff, (size_t)n);
		}

		if (n < 0) {
			printf("\n Read error \n");
		}

		printf("Closing socket\n");
		close(sockfd);
		sockfd = -1;
	}
	return 0;
}

/**
 * Trap - clean up
 *
 * @param sig - signal that caused this
 */
static void signal_handler(int sig)
{
	(void)sig;
	printf("Shutting down...");
	demo_disconn();

	exit(sig); // pass the signal through - this is nonstandard behavior but useful for debugging
}

/**
 * Sleaping Beauty's fave function
 */
void demo_sleep(void)
{
	while (1) usleep(10);
}

/**
 * Start the background thread
 *
 * Slave is started first and doesn't normally init transactions - but it could
 *
 * @param peer what peer we are
 */
void demo_init(TF_Peer peer)
{
	int retc;

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	printf("Starting %s...\n", peer == TF_MASTER ? "MASTER" : "SLAVE");
#ifdef _MSC_VER
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	HANDLE hThread;
	DWORD  threadId;

	g_Mutex= CreateMutex(NULL, false, NULL);  // 创建互斥量，初始化为触发状态

	if (peer == TF_MASTER) {
		retc = CreateThread(NULL, 0, demo_client, 0, 0, &threadId);
	}
	else {
		retc = CreateThread(NULL, 0, demo_server, 0, 0, &threadId);
	}

	if (retc == NULL) {
		perror("Creating new thread fail");
		signal_handler(9);
		return;
	}
#else
	void* stack = malloc(8192);
	if (stack == NULL) {
		perror("Oh fuck");
		signal_handler(9);
		return;
	}
	// CLONE_VM    --- share heap
// CLONE_FILES --- share stdout and stderr
	if (peer == TF_MASTER) {
		retc = clone(&demo_client, (char*)stack + 8192, CLONE_VM | CLONE_FILES, 0);
	}
	else {
		retc = clone(&demo_server, (char*)stack + 8192, CLONE_VM | CLONE_FILES, 0);
	}

	if (retc == 0) {
		perror("Clone fail");
		signal_handler(9);
		return;
	}
#endif
#ifdef _MSC_VER
	WaitForSingleObject(g_Mutex, INFINITE);
	ReleaseMutex(g_Mutex);
	CloseHandle(g_Mutex);
#endif // _MSC_VER
	printf("Thread started\n");
}
