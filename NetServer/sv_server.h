#pragma once

#include <stdint.h>
#include <stdbool.h>

// ------------------------------------------------------------------

#include "net.h"

// ------------------------------------------------------------------
// sv_server.h: abstraction layer to avoid unnecessarily detailed
// interaction with the socket API in the main application code of
// the server that wants to just send and receive packets

typedef struct sv_entity_t sv_entity_t;

// the server-side representation of a unique client connection
// it holds some gameplay details which I'd prefer it didn't, but
// I also didn't want to add another layer of complication
typedef struct sv_client_t
{
	bool new_connection;
	net_addr_t address;

	uint64_t last_packet_time;

	// the username of the client
	char name[32];

	// if I was being more thorough architecturally I would have
	// clients associated with some other "player" struct managed 
	// separately so that client_t can be primarily concerned with 
	// networking and not gameplay-related details
	sv_entity_t *entity;

	unsigned short last_sequence; // sequence number of the most recently handled input packet

	uint32_t btn_pressed;
	uint32_t btn_down;
	uint32_t btn_released;

	float mouse_x, mouse_y;

	float respawn_timer;
} sv_client_t;

// ------------------------------------------------------------------

// clients for now are not stable in this array, so pointers to 
// clients should not be kept around for long. I probably want to 
// change that, because I don't like that. specifically, any call to
// SV_ForgetClient does a typical unordered remove:
// g_clients[removed_client_index] = g_clients[--g_client_count]
extern size_t	   g_client_count;
extern sv_client_t g_clients[];

int  SV_Init(int port);
void SV_Exit(void);

sv_client_t *SV_GetClientForAddress(net_addr_t address);
sv_client_t *SV_GetClientForEntity(sv_entity_t *e);
void SV_ForgetClient(sv_client_t *client);

sv_client_t *SV_ReceivePacket(char *buffer, size_t buffer_size, size_t *packet_size);
bool SV_SendPacket(sv_client_t *client, void *packet, size_t packet_size);
bool SV_SendPacketToAllClients(void *packet, size_t packet_size);

void SV_ProcessPackets(void);
