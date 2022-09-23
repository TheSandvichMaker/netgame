// ------------------------------------------------------------------
// standard library includes

#include <stdio.h>
#include <stdalign.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ------------------------------------------------------------------
// internal includes

#include "cl_net.h"
#include "net.h"
#include "os.h"

// ------------------------------------------------------------------
// cl_net.c: wraps interacting with sockets to more purpose-built
// functions for exactly what the client wants to be doing

// it's a subtle thing, but providing this separate little zone lets
// the rest of the code do net stuff without having to lug around a
// socket variable, since we don't need multiple. this is a good
// usability win! and we can add small QoL stuff like provided by
// CL_GetNextPacket.


// ------------------------------------------------------------------
// initialization and uninitialization

static net_socket_t g_socket = { INVALID_SOCKET_VALUE };
static net_addr_t g_sv_address;

int CL_NetInit(char *server_address, int port)
{
	if (Net_Init() != 0)
		return -1;

	g_sv_address = Net_GetAddr(server_address, port);
	g_socket = Net_CreateSocket(CREATESOCKET_NONBLOCKING);

	if (g_socket.value == INVALID_SOCKET_VALUE)
	{
		fprintf(stderr, "failed to create socket\n");
		return -1;
	}

	char server_string[NETADDR_STR_SIZE];
	Net_StringFromNetAddr(server_string, sizeof(server_string), g_sv_address);

	printf("Server: %s:%d\n", server_string, port);

	return 0;
}

int CL_NetExit(void)
{
	if (Net_Exit() != 0)
		return -1;

	return 0;
}

// ------------------------------------------------------------------
// send and receive packets

void CL_SendPacketSized(void *packet, size_t packet_size)
{
	Net_SendPacket(g_socket, g_sv_address, packet, packet_size);
}

enum { MAX_PACKET_SIZE = 8192 };
static alignas(16) char g_packet_buffer[MAX_PACKET_SIZE];

net_header_t *CL_GetNextPacket(void)
{
	net_addr_t addr;
	int byte_count = Net_RecvPacket(g_socket, g_packet_buffer, sizeof(g_packet_buffer), &addr);

	if (byte_count <= 0)
		return NULL;

	if (!Net_AddrMatch(addr, g_sv_address)) // if it's not the server, I'm not listening!
		return NULL; 

	net_header_t *header = (net_header_t *)g_packet_buffer;
	return header;
}
