// ------------------------------------------------------------------
// standard library includes

#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdalign.h>

// ------------------------------------------------------------------
// internal includes

#include "protocol.h"
#include "net.h"
#include "os.h"
#include "sv_simulation.h"
#include "sv_server.h"

// ------------------------------------------------------------------
// sv_server.c: wraps interacting with sockets and the managing of
// different client connections up nicely to be more purpose-built
// for what the application code wants to be doing


// ------------------------------------------------------------------
// initialization and all that

static net_socket_t g_socket = { INVALID_SOCKET_VALUE };
static int g_max_message_size;

int SV_Init(int port)
{
	Net_Init();

	net_addr_t addr = Net_GetPassiveAddr(port);
	g_socket = Net_CreateSocket(CREATESOCKET_NONBLOCKING);

	if (g_socket.value == INVALID_SOCKET_VALUE)
	{
		fprintf(stderr, "SV_Init: failed to create socket\n");
		return -1;
	}

	if (Net_BindSocket(g_socket, addr) != 0)
	{
		fprintf(stderr, "SV_Init: failed to bind socket\n");
		return -1;
	}

	g_max_message_size = Net_GetMaxMessageSize(g_socket);

	printf("Server initialized.\n");
	return 0;
}

void SV_Exit(void)
{
	Net_Exit();
}

// ------------------------------------------------------------------
// client management

size_t      g_client_count;
sv_client_t g_clients[MAX_CLIENT_COUNT];

sv_client_t *SV_GetClientForAddress(net_addr_t address)
{
	sv_client_t *result = NULL;

	for (size_t i = 0; i < g_client_count; i++)
	{
		sv_client_t *client = &g_clients[i];

		if (memcmp(&client->address, &address, sizeof(address)) == 0)
		{
			result = client;
			result->new_connection = false;
			break;
		}
	}

	if (!result)
	{
		if (g_client_count < MAX_CLIENT_COUNT)
		{
			result = &g_clients[g_client_count++];
			memset(result, 0, sizeof(*result));

			result->new_connection = true;
			result->address = address;

			char client_address[NETADDR_STR_SIZE];
			Net_StringFromNetAddr(client_address, sizeof(client_address), result->address);

			fprintf(stdout, "Received new client connection from %s:%u\n", client_address, result->address.port);
		}
	}

	return result;
}

sv_client_t *SV_GetClientForEntity(sv_entity_t *e)
{
	for (size_t i = 0; i < g_client_count; i++)
	{
		sv_client_t *client = &g_clients[i];
		if (client->entity == e)
		{
			return client;
		}
	}
	return NULL;
}

void SV_ForgetClient(sv_client_t *client)
{
	if (!client) return;

	if (g_client_count > 0)
	{
		for (size_t i = 0; i < g_client_count; i++)
		{
			if (client == &g_clients[i])
			{
				g_clients[i] = g_clients[--g_client_count];

				char client_address[NETADDR_STR_SIZE];
				Net_StringFromNetAddr(client_address, sizeof(client_address), client->address);

				fprintf(stderr, "Client disconnected: %s:%d\n", client_address, client->address.port);

				break;
			}
		}
	}
	else
	{
		assert(!"Tried to forget client, but we don't know about any clients!\n");
	}
}

// ------------------------------------------------------------------
// sending and receiving packets

sv_client_t *SV_ReceivePacket(char *buffer, size_t buffer_size, size_t *packet_size)
{
	net_addr_t addr;
	int result = Net_RecvPacket(g_socket, buffer, buffer_size, &addr);

	if (result > 0)
	{
		sv_client_t *client = SV_GetClientForAddress(addr);
		client->last_packet_time = OS_GetHiresTime();

		*packet_size = (size_t)result;
		return client;
	}

	*packet_size = 0;
	return NULL;
}

bool SV_SendPacket(sv_client_t *client, void *packet, size_t packet_size)
{
	if (packet_size <= g_max_message_size)
	{
		int bytes_sent = Net_SendPacket(g_socket, client->address, packet, packet_size);
		return bytes_sent == packet_size;
	}
	else
	{
		assert(!"Packet too big!\n");
		return false;
	}
}

bool SV_SendPacketToAllClients(void *packet, size_t packet_size)
{
	bool result = true;
	for (size_t i = 0; i < g_client_count; i++)
	{
		result &= SV_SendPacket(&g_clients[i], packet, packet_size);
	}
	return result;
}

// ------------------------------------------------------------------
// processing packets

enum { MAX_PACKET_SIZE = 8192 };

static void SV_ProcessPacket(sv_client_t *client, char *buffer, size_t buffer_size)
{
	net_header_t *header = (net_header_t *)buffer;

	Sim_ProcessPacket(client, header);
	switch (header->kind)
	{
		case NETPACKET_CLIENT_DISCONNECTED:
		{
			Sim_ProcessPacket(client, header);
			SV_ForgetClient(client);
		} break;

		case NETPACKET_PING:
		{
			SV_SendPacket(client, buffer, buffer_size);
		} break;
	}

	client->new_connection = false;
}

void SV_ProcessPackets(void)
{
	for (;;)
	{
		alignas(16) char buffer[MAX_PACKET_SIZE];

		size_t packet_size;
		sv_client_t *client = SV_ReceivePacket(buffer, sizeof(buffer), &packet_size);

		if (packet_size == 0)
		{
			if (client)
				SV_ForgetClient(client);

			break;
		}

		SV_ProcessPacket(client, buffer, packet_size);
	}
}
