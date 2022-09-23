// ------------------------------------------------------------------
// standard library includes

#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>

// ------------------------------------------------------------------
// internal includes

#include "protocol.h"
#include "net.h"
#include "util.h"
#include "sv_server.h"
#include "sv_simulation.h"

// ------------------------------------------------------------------
// sv_simulation.c: does the actual simulation of the game, all
// gameplay code is in this file.


// ------------------------------------------------------------------
// entity management

sv_entity_t g_entities[MAX_ENTITY_COUNT];

sv_entity_t *E_FromId(net_entity_id_t id)
{
	sv_entity_t *result = NULL;
	if (id.index >= 0 && id.index < MAX_ENTITY_COUNT)
	{
		if (g_entities[id.index].id.value == id.value)
		{
			result = &g_entities[id.index];
		}
	}
	return result;
}

sv_entity_t *E_Spawn(void)
{
	short index = 0;

	for (short i = 1; i < MAX_ENTITY_COUNT; i++)
	{
		sv_entity_t *e = &g_entities[i];

		if (!ENTITY_ID_VALID(e->id))
		{
			index = i;
			break;
		}
	}

	if (NEVER(index == 0))
		return NULL; // out of entities

	sv_entity_t *e = &g_entities[index];

	// save this out for a second
	short generation = e->id.generation;
	unsigned short sequence = e->last_sequence;

	// because I want to just initialize this entity with a clean slate
	memset(e, 0, sizeof(*e));

	e->id.index      = index;
	e->id.generation = generation;
	e->last_sequence = sequence;

	return e;
}

void E_Destroy(sv_entity_t *e)
{
	// disassociate the entity from the client
	sv_client_t *client = SV_GetClientForEntity(e);

	if (client)
		client->entity = NULL;
	
	// and destroy the entity
	e->id.index = INVALID_ENTITY_INDEX;

	// and increment the generation for this slot in the
	// entity array as to signify it is no longer occupied
	// by this now-destroyed entity
	e->id.generation += 1;
}

static sv_entity_t *Sim_SpawnPlayer(sv_client_t *client)
{
	// just spawn players somewhere in some area around the origin
	// where they are likely to see each other right away

	// if you called this when the client still had a living entity,
	// I guess it should be a forced respawn
	if (client->entity)
		E_Destroy(client->entity);

	int game_field_w = 300;
	int game_field_h = 200;

	int x = 20 + rand() % (game_field_w - 40) - game_field_w / 2;
	int y = 20 + rand() % (game_field_h - 40) - game_field_h / 2;

	client->entity = E_Spawn();
	client->entity->x = (float)x;
	client->entity->y = (float)y; 
	client->entity->size = 16.0f;

	return client->entity;
}

// ------------------------------------------------------------------
// entity related netcode

static unsigned short g_world_state_sequence;

static void Sim_SendWorldState(sv_client_t *client)
{
	net_world_state_t packet = {
		.header = {
			.kind     = NETPACKET_WORLD_STATE,
			.sequence = ++g_world_state_sequence,
		},
		/* world state initialized down below */
	};

	if (client->entity)
		packet.client_id = client->entity->id;

	for (size_t i = 0; i < g_client_count; i++)
	{
		sv_client_t *sv_client = &g_clients[i];
		net_player_t *player = &packet.players[i];
		memcpy(player->name, sv_client->name, NET_USERNAME_MAX_SIZE);

		if (sv_client->entity)
			player->entity = sv_client->entity->id;
	}

	packet.player_count = (unsigned)g_client_count;

	for (size_t i = MIN_ENTITY_INDEX; i <= MAX_ENTITY_INDEX; i++)
	{
		sv_entity_t *e = &g_entities[i];

		if (ENTITY_ID_VALID(e->id))
		{
			net_entity_state_t *state = &packet.world_state[i];
			state->id   = e->id;
			state->x    = e->x;
			state->y    = e->y;
			state->dx   = e->dx;
			state->dy   = e->dy;
			state->size = e->size;
		}
	}

	SV_SendPacket(client, &packet, sizeof(packet));
}

void Sim_ProcessPacket(sv_client_t *client, net_header_t *header)
{
	if (client->new_connection)
	{
		Sim_SpawnPlayer(client);
		Sim_SendWorldState(client);
	}

	switch (header->kind)
	{
		case NETPACKET_INPUT:
		{
			net_input_t *packet = (net_input_t *)header;

			if (Net_AcceptSequenceNumber(client->last_sequence, packet->header.sequence))
			{
				client->last_sequence = packet->header.sequence;

				uint32_t changes = client->btn_down ^ packet->btn_down;
				client->btn_pressed  |= changes &  packet->btn_down;
				client->btn_released |= changes & ~packet->btn_down;
				client->btn_down      = packet->btn_down;

				client->mouse_x = packet->mouse_x;
				client->mouse_y = packet->mouse_y;

				memcpy(client->name, packet->name, NET_USERNAME_MAX_SIZE);
			}
			else
			{
				// fprintf(stderr, "Discarded out-of-order or duplicate input packet\n");
			}
		} break;

		case NETPACKET_CLIENT_DISCONNECTED:
		{
			if (client->entity)
			{
				E_Destroy(client->entity);
			}
		} break;
	}
}

// ------------------------------------------------------------------
// the main loop for the simulation

void Sim_Run(float dt)
{
	for (size_t i = 0; i < g_client_count; i++)
	{
		sv_client_t *client = &g_clients[i];

		if (client->entity)
		{
			sv_entity_t *e = client->entity;

			// player movement

			float dx = 0.0f;
			float dy = 0.0f;

			float move_speed = 100.0f;

			if (client->btn_down & NETBTN_LEFT)  dx -= move_speed;
			if (client->btn_down & NETBTN_RIGHT) dx += move_speed;
			if (client->btn_down & NETBTN_UP)    dy -= move_speed;
			if (client->btn_down & NETBTN_DOWN)  dy += move_speed;

			e->dx = dx;
			e->dy = dy;

			// player shooting

			if (client->btn_pressed & NETBTN_SHOOT)
			{
				float mouse_dx = client->mouse_x - e->x;
				float mouse_dy = client->mouse_y - e->y;
				if (fabsf(mouse_dx) > 1.0f && fabsf(mouse_dy) > 1.0f)
				{
					float len = sqrtf(mouse_dx*mouse_dx + mouse_dy*mouse_dy);

					mouse_dx /= len;
					mouse_dy /= len;

					float bullet_speed = 200.0f;
					mouse_dx *= bullet_speed;
					mouse_dy *= bullet_speed;

					sv_entity_t *bullet = E_Spawn();
					bullet->parent   = e;
					bullet->flags   |= EFLAG_HURTS;
					bullet->x        = e->x;
					bullet->y        = e->y;
					bullet->dx       = mouse_dx;
					bullet->dy       = mouse_dy;
					bullet->lifetime = 2.0f;
					bullet->size     = 4.0f;
				}
			}

			if (client->btn_down & NETBTN_KILL)
				E_Destroy(e);
		}
		else
		{
			// if the client is dead, they should respawn after 5 seconds

			if (client->respawn_timer > 0.0f)
			{
				client->respawn_timer -= dt;
				if (client->respawn_timer <= 0.0f)
				{
					client->respawn_timer = 0.0f;
					Sim_SpawnPlayer(client);
				}
			}
			else
			{
				client->respawn_timer = 5.0f;
			}
		}

		client->btn_pressed  = 0;
		client->btn_released = 0;
	}

	// simulate entities
	
	for (size_t i = MIN_ENTITY_INDEX; i <= MAX_ENTITY_INDEX; i++)
	{
		sv_entity_t *e = &g_entities[i];

		if (!ENTITY_ID_VALID(e->id))
			continue;

		// check for collisions with other entities if needed

		if (e->flags & EFLAG_HURTS)
		{
			for (size_t j = MIN_ENTITY_INDEX; j <= MAX_ENTITY_INDEX; j++)
			{
				if (i == j) 
					continue;

				sv_entity_t *other_e = &g_entities[j];

				if (!ENTITY_ID_VALID(other_e->id))
					continue;

				if (other_e == e->parent)
					continue;

				float radius = 0.5f*e->size + 0.5f*other_e->size;
				if (fabsf(e->x - other_e->x) <= radius &&
					fabsf(e->y - other_e->y) <= radius)
				{
					// they collide!

					if (e->parent)
					{
						sv_client_t *parent_client = SV_GetClientForEntity(e->parent);
						sv_client_t *other_client  = SV_GetClientForEntity(other_e);
						if (parent_client && other_client)
						{
							printf("%s obliterated %s!\n", parent_client->name, other_client->name);
						}
					}

					E_Destroy(e);
					E_Destroy(other_e);
				}
			}
		}

		// if the entity had a limited lifespan, handle that

		if (e->lifetime > 0.0f)
		{
			e->lifetime -= dt;
			if (e->lifetime <= 0.0f)
			{
				E_Destroy(e);
			}
		}

		// integrate physics

		e->x += dt*e->dx;
		e->y += dt*e->dy;
	}

	// send world state out to the clients

	for (size_t i = 0; i < g_client_count; i++)
	{
		sv_client_t *client = &g_clients[i];
		Sim_SendWorldState(client);
	}
}
