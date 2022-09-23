// ------------------------------------------------------------------
// standard library and OS includes

#include <stdio.h>
#include <stddef.h>
#include <assert.h>

#include <WinSock2.h>
#include <WS2tcpip.h>

// this is a way to link libraries without having to edit the
// project configuration, supported by MSVC and clang
#pragma comment(lib, "ws2_32.lib")

// ------------------------------------------------------------------
// internal includes

#include "os.h"
#include "util.h"
#include "net.h"
#include "nettypes.h"

// ------------------------------------------------------------------
// net.c: this exists to abstract over the socket API. I am not sure
// whether that is a good idea, but since a lot of these functions
// want to be shared between the client and the server anyway, I 
// figured I might as well build a shared abstraction layer.
// I only deal with IPv4 addresses for simplicity.

// this file is not as heavily documented as some of the other files
// because I don't feel confident giving a good enough explanation
// of the socket API. check this guide instead:

// https://beej.us/guide/bgnet/


// ------------------------------------------------------------------
// internal functions

static inline void Net_AddrFromSockAddr(net_addr_t *net_addr, struct sockaddr_in *sock_addr)
{
	net_addr->family = sock_addr->sin_family;
	net_addr->port   = sock_addr->sin_port;
	net_addr->addr   = sock_addr->sin_addr.s_addr;
}

static inline void Net_SockAddrFromAddr(struct sockaddr_in *sock_addr, net_addr_t *net_addr)
{
	sock_addr->sin_family      = net_addr->family;
	sock_addr->sin_port        = net_addr->port;
	sock_addr->sin_addr.s_addr = net_addr->addr;
}

typedef enum net_stat_direction_e
{
	NETSTAT_INBOUND,
	NETSTAT_OUTBOUND,
} net_stat_direction_e;

static void Net_RecordSequenceTest(int accepted);
static void Net_RecordPacketStat(net_stat_direction_e direction, int size);

// ------------------------------------------------------------------

enum { PACKET_BUFFER_SIZE = INT16_MAX };
static _Alignas(16) char g_packet_buffer[PACKET_BUFFER_SIZE];

int Net_Init(void)
{
	// winsock needs to be initialized before use

	WSADATA winsock_data;
	if (WSAStartup(MAKEWORD(2, 0), &winsock_data) != 0)
	{
		return -1;
	}

	return 0;
}

int Net_Exit(void)
{
	// and shut down, preferably

	if (WSACleanup() != 0)
	{
		return -1;
	}

	return 0;
}

int Net_AcceptSequenceNumber(unsigned short prev, unsigned short next)
{
	// if we're in the last third of the sequence numbers, we're going to accept
	// sequence numbers in the first third as newer to allow for some robustness
	// if the sequence number wrapped
	int prev_in_last_third  = prev >= (UINT16_MAX - (UINT16_MAX / 3));
	int next_in_first_third = next <  (UINT16_MAX / 3);

	int wrapped = (prev_in_last_third && next_in_first_third);
	int result  = next > prev || wrapped;

	Net_RecordSequenceTest(result);
	return result;
}

void Net_StringFromNetAddr(char *buffer, size_t buffer_size, net_addr_t addr)
{
	inet_ntop(addr.family, &addr.addr, buffer, buffer_size);
}

int Net_AddrMatch(net_addr_t a, net_addr_t b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

net_addr_t Net_GetPassiveAddr(int port)
{
	// I still get confused about some of this, but by passing AI_PASSIVE in the
	// hints and NULL to getaddrinfo as the address, we should get back an address
	// we can use for ourselves as server
	net_addr_t addr = { 0 };

	struct addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags    = AI_PASSIVE,
	};

	char port_string[256];
	snprintf(port_string, sizeof(port_string), "%d", port);

	struct addrinfo *first_info = NULL;
	{
		int result;
		do 
		{
			result = getaddrinfo(NULL, port_string, &hints, &first_info);
		} while (result == WSATRY_AGAIN);

		if (result != 0)
		{
			// From MSDN (https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo):
			// Use the gai_strerror function to print error messages based on the EAI codes 
			// returned by the getaddrinfo function. The gai_strerror function is provided 
			// for compliance with IETF recommendations, but it is not thread safe. 
			// Therefore, use of traditional Windows Sockets functions such as WSAGetLastError 
			// is recommended.

			OS_PError("Net_CreateSocket: getaddrinfo");
			return addr;
		}
	}

	for (struct addrinfo *info = first_info; info; info = info->ai_next)
	{
		if (info->ai_family == AF_INET &&
			info->ai_socktype == SOCK_DGRAM)
		{
			struct sockaddr_in *in = (struct sockaddr_in *)info->ai_addr;
			Net_AddrFromSockAddr(&addr, in);

			break;
		}
		else
		{
			// surely this shouldn't happen...
		}
	}

	freeaddrinfo(first_info);
	return addr;
}

net_addr_t Net_GetAddr(char *address, int port)
{
	net_addr_t addr = { 0 };

	struct addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_DGRAM,
	};

	char port_string[256];
	snprintf(port_string, sizeof(port_string), "%d", port);

	struct addrinfo *first_info = NULL;
	{
		int result;
		do 
		{
			result = getaddrinfo(address, port_string, &hints, &first_info);
		} while (result == WSATRY_AGAIN);

		if (result != 0)
		{
			// From MSDN (https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo):
			// Use the gai_strerror function to print error messages based on the EAI codes 
			// returned by the getaddrinfo function. The gai_strerror function is provided 
			// for compliance with IETF recommendations, but it is not thread safe. 
			// Therefore, use of traditional Windows Sockets functions such as WSAGetLastError 
			// is recommended.

			OS_PError("Net_CreateSocket: getaddrinfo");
			return addr;
		}
	}

	for (struct addrinfo *info = first_info; info; info = info->ai_next)
	{
		if (info->ai_family == AF_INET &&
			info->ai_socktype == SOCK_DGRAM)
		{
			struct sockaddr_in *in = (struct sockaddr_in *)info->ai_addr;
			Net_AddrFromSockAddr(&addr, in);

			break;
		}
		else
		{
			// surely this shouldn't happen
		}
	}

	freeaddrinfo(first_info);
	return addr;
}

net_socket_t Net_CreateSocket(int flags)
{
	net_socket_t sock = { INVALID_SOCKET };

	// I am hardcoding this to only create IPv4 UDP sockets because
	// that is all I am using in this project
	sock.value = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock.value == INVALID_SOCKET)
	{
		fprintf(stderr, "Net_CreateSocket: failed to create socket\n");
		return sock;
	}

	if (flags & CREATESOCKET_NONBLOCKING)
	{
		// make socket non-blocking
		u_long mode = 1;
		if (ioctlsocket(sock.value, FIONBIO, &mode) != 0)
		{
			OS_PError("SV_Init: ioctlsocket (FIONBIO)");
			return (net_socket_t) { INVALID_SOCKET_VALUE };
		}
	}

	return sock;
}

void Net_CloseSocket(net_socket_t sock)
{
	closesocket(sock.value);
}

int Net_BindSocket(net_socket_t sock, net_addr_t addr)
{
	struct sockaddr_in sockaddr;
	Net_SockAddrFromAddr(&sockaddr, &addr);

	int yes = 1;
	if (setsockopt(sock.value, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes)) == -1)
	{
		OS_PError("SV_Init: setsockopt (SO_REUSEADDR)");
		return -1;
	}

	if (bind(sock.value, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) == -1)
	{
		OS_PError("SV_Init: bind");
		return -1;
	}

	return 0;
}

int Net_GetMaxMessageSize(net_socket_t sock)
{
	int max_message_size;
	int max_message_size_size = sizeof(max_message_size);

	if (getsockopt(sock.value, SOL_SOCKET, SO_MAX_MSG_SIZE, (char *)&max_message_size, &max_message_size_size) != 0)
	{
		fprintf(stderr, "getsockopt failed. what?\n");
		return 1;
	}

	return max_message_size;
}

int Net_SendPacket(net_socket_t sock, net_addr_t addr, void *packet, size_t packet_size)
{
	struct sockaddr_in sock_addr;
	Net_SockAddrFromAddr(&sock_addr, &addr);

	int byte_count = sendto(sock.value, packet, (int)packet_size, 0, (struct sockaddr *)&sock_addr, sizeof(sock_addr));

	if (byte_count == -1)
	{
		if (WSAGetLastError() == WSAEWOULDBLOCK)
		{
			return 0;
		}
		else
		{
			OS_PError("Net_SendPacket");
			return -1;
		}
	}

	Net_RecordPacketStat(NETSTAT_OUTBOUND, byte_count);

	return byte_count;
}

int Net_RecvPacket(net_socket_t sock, void *buffer, size_t buffer_size, net_addr_t *addr)
{
	if (NEVER(buffer_size > INT_MAX)) buffer_size = INT_MAX;

	struct sockaddr_storage their_address;
	int address_size = sizeof(their_address);

	int byte_count = recvfrom(sock.value, buffer, (int)buffer_size, 0, (struct sockaddr *)&their_address, &address_size);

	if (byte_count == -1)
	{
		switch (WSAGetLastError())
		{
			case WSAEWOULDBLOCK:
			{
				// all good
				return 0;
			} break;

			default:
			{
				OS_PError("Net_RecvPacket");
				// not good
				return -1;
			} break;
		}
	}

	Net_RecordPacketStat(NETSTAT_INBOUND, byte_count);

	Net_AddrFromSockAddr(addr, (struct sockaddr_in *)&their_address);

	return byte_count;
}

// ------------------------------------------------------------------
// recording network related stats

static float g_stat_sample_window = 1.0f; 

typedef struct net_stat_bucket_t
{
	int tested_packets;
	int accepted_packets;

	int inbound_bytes;
	int outbound_bytes;

	float actual_time;
} net_stat_bucket_t;

enum { NET_STATS_BUCKET_COUNT = 30 };

static size_t g_stat_bucket_index;
static os_time_t g_stat_last_bucket_time;
static net_stat_bucket_t g_stat_buckets[NET_STATS_BUCKET_COUNT];

void Net_GetStats(net_stats_t *stats)
{
	int accepted_packets = 0;
	int tested_packets   = 0;

	int inbound_bytes  = 0;
	int outbound_bytes = 0;

	int total_samples = 0;

	float actual_time = 0.0f;

	for (size_t i = 0; i < NET_STATS_BUCKET_COUNT; i++)
	{
		net_stat_bucket_t *bucket = &g_stat_buckets[i];

		if (bucket->tested_packets > 0 ||
			bucket->inbound_bytes  > 0 ||
			bucket->outbound_bytes > 0)
		{
			total_samples += 1;

			accepted_packets += bucket->accepted_packets;
			tested_packets   += bucket->tested_packets;

			inbound_bytes  += bucket->inbound_bytes;
			outbound_bytes += bucket->outbound_bytes;

			actual_time += bucket->actual_time;
		}
	}

	stats->packets_accepted_ratio = 0.0f;

	if (tested_packets > 0)
		stats->packets_accepted_ratio = (float)accepted_packets / (float)tested_packets;

	if (actual_time == 0.0f)
		actual_time = 1.0f;

	stats->bytes_in_per_second  = (float)inbound_bytes  / actual_time;
	stats->bytes_out_per_second = (float)outbound_bytes / actual_time;
}

static net_stat_bucket_t *Net_GetStatBucket(void)
{
	if (g_stat_last_bucket_time == 0)
		g_stat_last_bucket_time = OS_GetHiresTime();

	os_time_t current_time = OS_GetHiresTime();
	double time_elapsed = OS_GetSecondsElapsed(g_stat_last_bucket_time, current_time);

	net_stat_bucket_t *bucket = &g_stat_buckets[g_stat_bucket_index];

	float bucket_time = g_stat_sample_window / (float)NET_STATS_BUCKET_COUNT;
	if (time_elapsed > bucket_time)
	{
		bucket->actual_time = (float)time_elapsed;

		size_t offset = (size_t)(time_elapsed / bucket_time);

		g_stat_last_bucket_time = current_time;
		g_stat_bucket_index     = (g_stat_bucket_index + offset) % NET_STATS_BUCKET_COUNT;

		bucket = &g_stat_buckets[g_stat_bucket_index];
		memset(bucket, 0, sizeof(*bucket));
	}

	return bucket;
}

static void Net_RecordSequenceTest(int accepted)
{
	net_stat_bucket_t *bucket = Net_GetStatBucket();

	if (accepted)
		bucket->accepted_packets += 1;

	bucket->tested_packets += 1;
}

void Net_RecordPacketStat(net_stat_direction_e direction, int size)
{
	net_stat_bucket_t *bucket = Net_GetStatBucket();

	if (direction == NETSTAT_INBOUND)
		bucket->inbound_bytes  += size;

	if (direction == NETSTAT_OUTBOUND)
		bucket->outbound_bytes += size;
}
