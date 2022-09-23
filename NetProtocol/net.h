#pragma once

// ------------------------------------------------------------------
// standard library includes

#include <stddef.h> // uintptr_t

// ------------------------------------------------------------------
// net.h: abstraction of the socket API to simplify application code

typedef struct net_addr_t
{
	// must be equivalent to struct sockaddr_in
	unsigned short family;
	unsigned short port;
	unsigned int   addr;
} net_addr_t;

enum { NETADDR_STR_SIZE = 22 };

typedef struct net_socket_t
{
	uintptr_t value;
} net_socket_t;

// this is exactly the same value as winsock's INVALID_SOCKET, but
// some care should be taken regarding the fact that sockets are
// signed ints on linux, if one were to port this code
#define INVALID_SOCKET_VALUE (uintptr_t)(~0) 

// ------------------------------------------------------------------

// must be called before using any of the other Net functions
// returns 0 on success, -1 on error
int Net_Init(void);

// should be called before exiting
int Net_Exit(void);

// a (somewhat) wrapping-robust way to check a sequence number is newer
int Net_AcceptSequenceNumber(unsigned short prev, unsigned short next);

// returns the address as a human-readable string into the provided
// buffer. buffer should be NETADDR_STR_SIZE or larger to ensure
// the address fits
void Net_StringFromNetAddr(char *buffer, size_t buffer_size, net_addr_t addr);

// returns 1 if the addresses are equal, 0 otherwise
int  Net_AddrMatch(net_addr_t a, net_addr_t b);

// returns an address that can be bound to in order to spin up a
// server on the local machine
net_addr_t Net_GetPassiveAddr(int port);

// returns a net_addr_t for the given address and port combination
net_addr_t Net_GetAddr(char *address, int port);

enum 
{
	CREATESOCKET_NONBLOCKING = 0x1,
};

// only creates UDP sockets
net_socket_t Net_CreateSocket(int flags);
void         Net_CloseSocket(net_socket_t sock);
int          Net_BindSocket(net_socket_t sock, net_addr_t addr);

// returns the maximum packet size possible over the socket
int Net_GetMaxMessageSize(net_socket_t sock);

// sends a packet, returns the amount of bytes sent or -1 on error
int Net_SendPacket(net_socket_t sock, net_addr_t addr, void *packet, size_t packet_size);

// receives a packet, returns the amount of bytes received or -1 on error
int Net_RecvPacket(net_socket_t sock, void *buffer, size_t buffer_size, net_addr_t *addr);

typedef struct net_stats_t
{
	float packets_accepted_ratio;
	float bytes_in_per_second;
	float bytes_out_per_second;
} net_stats_t;

// returns stats about the network usage
void Net_GetStats(net_stats_t *stats);
