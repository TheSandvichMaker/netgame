// ------------------------------------------------------------------
// standard library includes

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

// ------------------------------------------------------------------
// external includes

#include <raylib.h>

// ------------------------------------------------------------------
// internal includes

#include "protocol.h"
#include "util.h"
#include "net.h"
#include "cl_client.h"
#include "cl_net.h"

// ------------------------------------------------------------------
// cl_client.c: this file implements the main function of the client,
// polling input, sending it to the server, receiving world state
// updates, and rendering the world and effects.


// ------------------------------------------------------------------
// gamestates

typedef enum gamestate_e
{
	GAMESTATE_MENU,
	GAMESTATE_WORLD,
} gamestate_e;

static gamestate_e g_gamestate;

static void Menu_Tick(float dt);
static void Menu_Draw(void);

static void World_Tick(float dt);
static void World_Draw(void);

// ------------------------------------------------------------------
// some array of colors used for entities and particles

static Color g_colors[] = {
	{ 253, 249, 0, 255 },
	{ 255, 203, 0, 255 },
	{ 255, 161, 0, 255 },
	{ 255, 109, 194, 255 },
	{ 230, 41, 55, 255 },
	{ 190, 33, 55, 255 },
	{ 0, 228, 48, 255 },
	{ 0, 158, 47, 255 },
	{ 0, 117, 44, 255 },
	{ 102, 191, 255, 255 },
	{ 0, 121, 241, 255 },
	// { 0, 82, 172, 255 }, this is DARKBLUE, which I use for the background, so let's not include that
	{ 200, 122, 255, 255 },
	{ 135, 60, 190, 255 },
	{ 112, 31, 126, 255 },
	{ 211, 176, 131, 255 },
	{ 127, 106, 79, 255 },
	{ 76, 63, 47, 255 },
};

// ------------------------------------------------------------------
// input state

static unsigned short g_input_sequence;
static uint32_t g_buttons_down;
static uint32_t g_buttons_pressed;
static uint32_t g_buttons_released;
static bool     g_show_debug_info = true;

// ------------------------------------------------------------------
// client-side state (if you had split-screen you could have multiple
// of these cl_player_t things)

typedef struct cl_player_t
{
	net_entity_id_t entity; // the entity associated with the player

	float cam_x, cam_y; // current interpolated camera position 
	float cam_offset_x, cam_offset_y; // offset that gets added to the camera position but does not interfere with interpolation
	float target_cam_x, target_cam_y; // target camera position that is being interpolated towards

	float cam_shake_t; // running timer used for managing the speed of the camera shake
	float cam_shake;   // current camera shake amount (naturally returns back to 0 over time)
} cl_player_t;

static cl_player_t g_client; // our local player

// camera transforms for the player's viewport
static inline void ToCameraSpace(cl_player_t *player, float *x, float *y)
{
	float cam_x = player->cam_x;
	float cam_y = player->cam_y;

	cam_x += player->cam_offset_x;
	cam_y += player->cam_offset_y;

	*x = *x - cam_x + 0.5f*GetRenderWidth();
	*y = *y - cam_y + 0.5f*GetRenderHeight();
}

static inline void ToWorldSpace(cl_player_t *player, float *x, float *y)
{
	float cam_x = player->cam_x;
	float cam_y = player->cam_y;

	cam_x += player->cam_offset_x;
	cam_y += player->cam_offset_y;

	*x = *x + cam_x - 0.5f*GetRenderWidth();
	*y = *y + cam_y - 0.5f*GetRenderHeight();
}

// ------------------------------------------------------------------
// entities

typedef struct cl_entity_t
{
	net_entity_id_t id;

	char name[NET_USERNAME_MAX_SIZE];

	unsigned short last_sequence; // sequence number of last update packet received for this entity

	float x, y;
	float dx, dy;
	float size;
} cl_entity_t;

// entities simply occupy a global static array
static cl_entity_t g_entities[MAX_ENTITY_COUNT];

// entities are identified by an id which is a combination of index 
// and generation, to make us able to tell apart entities even if 
// they occupy the same array slot (as entities get destroyed and 
// their array slot gets reused).
static cl_entity_t *CL_GetEntity(net_entity_id_t id)
{
	if (ENTITY_ID_VALID(id))
	{
		if (g_entities[id.index].id.generation == id.generation)
		{
			return &g_entities[g_client.entity.index];
		}
	}
	return NULL;
}

static cl_entity_t *CL_GetClientEntity(void)
{
	return CL_GetEntity(g_client.entity);
}

// ------------------------------------------------------------------
// cool little client-side particles, mostly to illustrate that some
// effects are purely clientside and the server has nothing to do 
// with them

typedef struct cl_particle_t
{
	float x, y;
	float dx, dy;
	float t;
	Color color;
} cl_particle_t;

enum { MAX_PARTICLE_COUNT = 1024 };
static size_t        g_next_particle_index;
static cl_particle_t g_particles[MAX_PARTICLE_COUNT];

static void CL_SpawnParticleExplosion(float x, float y)
{
	float dist_x = g_client.target_cam_x - x;
	float dist_y = g_client.target_cam_y - y;
	float dist = sqrtf(dist_x*dist_x + dist_y*dist_y);

	float shake = (500.0f - dist) / 500.0f;

	if (shake < 0.0f) shake = 0.0f;
	shake *= shake;

	g_client.cam_shake += shake;

	int particle_count = GetRandomValue(10, 30);
	for (int i = 0; i < particle_count; i++)
	{
		cl_particle_t *particle = &g_particles[g_next_particle_index++];

		if (g_next_particle_index >= MAX_PARTICLE_COUNT) 
			g_next_particle_index %= MAX_PARTICLE_COUNT;

		particle->t = 1.0f;
		particle->x = x;
		particle->y = y;

		// this is not how you get good random floats!!
		particle->dx = (float)GetRandomValue(-1000, 1000) / 10.0f;
		particle->dy = (float)GetRandomValue(-1000, 1000) / 10.0f;

		particle->color = g_colors[GetRandomValue(0, ARRAY_COUNT(g_colors)-1)];
	}
}

// ------------------------------------------------------------------
// main loop and draw function

void CL_Init(void)
{
	// it's GAMESTATE_MENU by default anyway, but you know.
	g_gamestate = GAMESTATE_MENU;
}

void CL_Tick(float dt)
{
	// ------------------------------------------------------------------
	// gamestate independent controls

	if (IsKeyPressed(KEY_F3))
		g_show_debug_info = !g_show_debug_info;

	// ------------------------------------------------------------------

	switch (g_gamestate)
	{
		case GAMESTATE_MENU:
		{
			Menu_Tick(dt);
		} break;

		case GAMESTATE_WORLD:
		{
			World_Tick(dt);
		} break;
	}
}

void CL_Draw(void)
{
	switch (g_gamestate)
	{
		case GAMESTATE_MENU:
		{
			Menu_Draw();
		} break;

		case GAMESTATE_WORLD:
		{
			World_Draw();
		} break;
	}
}

// ------------------------------------------------------------------
// menu gamemode

static size_t g_text_cursor;
static char   g_username[NET_USERNAME_MAX_SIZE];
static float  g_need_username_timer;

void Menu_Tick(float dt)
{
	(void)dt;

	for (;;)
	{
		int c = GetCharPressed();

		if (!c)
			break;

		if (g_text_cursor < sizeof(g_username) - 1)
		{
			g_username[g_text_cursor++] = (char)c;
			g_username[g_text_cursor  ] = 0;
		}
	}

	if (IsKeyPressed(KEY_BACKSPACE))
	{
		if (g_text_cursor > 0)
		{
			g_username[--g_text_cursor] = 0;
		}
	}

	if (IsKeyPressed(KEY_ENTER))
	{
		if (g_username[0])
		{
			g_gamestate = GAMESTATE_WORLD;
		}
		else
		{
			g_need_username_timer = 1.0f;
		}
	}

	if (g_need_username_timer > 0.0f)
	{
		g_need_username_timer -= dt;
	}
}

void Menu_Draw(void)
{
	ClearBackground(DARKGREEN);

	int render_w = GetRenderWidth();
	int render_h = GetRenderHeight();

	DrawText("Enter username: ", render_w / 2 - 300, render_h / 2 - 100, 24, SKYBLUE);
	DrawText(g_username, render_w / 2 - 75, render_h / 2 - 100, 24, SKYBLUE);

	if (g_need_username_timer > 0.0f)
	{
		DrawText("Please enter a username!", 64, render_h - 64, 18, RED);
	}
}

// ------------------------------------------------------------------
// world gamemode

static unsigned short g_world_state_sequence;

void World_Tick(float dt)
{
	cl_player_t *client   = &g_client;
	cl_entity_t *client_e = CL_GetClientEntity();

	// ------------------------------------------------------------------
	// camera interpolation

	if (client_e)
	{
		client->target_cam_x = client_e->x;
		client->target_cam_y = client_e->y;

		float cam_dx = client->target_cam_x - client->cam_x;
		float cam_dy = client->target_cam_y - client->cam_y;

		client->cam_x += 8.0f*dt*cam_dx;
		client->cam_y += 8.0f*dt*cam_dy;
	}

	// ------------------------------------------------------------------
	// camera shake

	client->cam_shake_t += dt*client->cam_shake;
	client->cam_offset_x = 1.7f*client->cam_shake*sinf(28.0f*client->cam_shake_t);
	client->cam_offset_y = 1.7f*client->cam_shake*cosf(34.0f*client->cam_shake_t);

	if (client->cam_shake > 0.0f)
	{
		client->cam_shake -= 1.5f*dt;
		if (client->cam_shake < 0.0f)
		{
			client->cam_shake = 0.0f;
		}
	}

	// ------------------------------------------------------------------
	// input handling
	// 
	// I don't do anything locally with these button states so I could 
	// choose not to bother storing them but I think they will be useful
	// later

	uint32_t new_buttons = 0;
	if (IsKeyDown(KEY_LEFT))                  new_buttons |= NETBTN_LEFT;
	if (IsKeyDown(KEY_RIGHT))                 new_buttons |= NETBTN_RIGHT;
	if (IsKeyDown(KEY_UP))                    new_buttons |= NETBTN_UP;
	if (IsKeyDown(KEY_DOWN))                  new_buttons |= NETBTN_DOWN;
	if (IsKeyDown(KEY_A))                     new_buttons |= NETBTN_LEFT;
	if (IsKeyDown(KEY_D))                     new_buttons |= NETBTN_RIGHT;
	if (IsKeyDown(KEY_W))                     new_buttons |= NETBTN_UP;
	if (IsKeyDown(KEY_S))                     new_buttons |= NETBTN_DOWN;
	if (IsKeyDown(KEY_K))                     new_buttons |= NETBTN_KILL;
	if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) new_buttons |= NETBTN_SHOOT;

	uint32_t button_changes = new_buttons ^ g_buttons_down;
	g_buttons_pressed  = button_changes &  new_buttons;
	g_buttons_released = button_changes & ~new_buttons;
	g_buttons_down = new_buttons;

	float mouse_x = (float)GetMouseX();
	float mouse_y = (float)GetMouseY();
	ToWorldSpace(&g_client, &mouse_x, &mouse_y);

	{
		net_input_t packet = {
			.header = {
				.kind     = NETPACKET_INPUT,
				.sequence = ++g_input_sequence,
			},
			.btn_down = new_buttons,
			.mouse_x  = mouse_x,
			.mouse_y  = mouse_y,
		};
		memcpy(packet.name, g_username, sizeof(g_username));
		CL_SendPacket(&packet);
	}

	// ------------------------------------------------------------------
	// process packets from the server

	for (;;)
	{
		net_header_t *header = CL_GetNextPacket();
		
		if (!header)
			break;

		switch (header->kind)
		{
			case NETPACKET_WORLD_STATE:
			{
				net_world_state_t *packet = (net_world_state_t *)header;

				if (Net_AcceptSequenceNumber(g_world_state_sequence, packet->header.sequence))
				{
					g_world_state_sequence = packet->header.sequence;

					g_client.entity = packet->client_id;
					for (size_t i = MIN_ENTITY_INDEX; i < MAX_ENTITY_COUNT; i++)
					{
						cl_entity_t        *cl = &g_entities[i];
						net_entity_state_t *sv = &packet->world_state[i];

						if (ENTITY_ID_VALID(cl->id))
						{
							if (cl->id.value != sv->id.value)
							{
								// there's a different (or no) entity at this index in the
								// server's packet, so our entity must have been destroyed.
								cl->id.index = INVALID_ENTITY_INDEX;
								CL_SpawnParticleExplosion(cl->x, cl->y);
							}
						}

						if (ENTITY_ID_VALID(sv->id))
						{
							if (!ENTITY_ID_VALID(cl->id))
							{
								// new spawn
								// fprintf(stderr, "New entity spawned! id: { %d, %d }\n", sv->id.index, sv->id.generation);
							}

							// update
							cl->id   = sv->id;
							cl->x    = sv->x;
							cl->y    = sv->y;
							cl->dx   = sv->dx;
							cl->dy   = sv->dy;
							cl->size = sv->size;
							cl->name[0] = 0; // if this entity has a name, it gets updated in the next loop
						}
					}

					for (size_t i = 0; i < packet->player_count; i++)
					{
						net_player_t *net_player = &packet->players[i];
						if (ENTITY_ID_VALID(net_player->entity))
						{
							cl_entity_t *e = &g_entities[net_player->entity.index];

							if (e->id.generation == net_player->entity.generation)
							{
								memcpy(e->name, net_player->name, NET_USERNAME_MAX_SIZE);
							}
						}
					}
				}
			} break;
		}
	}

	// ------------------------------------------------------------------
	// "simulate" entities

	for (size_t i = MIN_ENTITY_INDEX; i < MAX_ENTITY_COUNT; i++)
	{
		cl_entity_t *e = &g_entities[i];

		if (!ENTITY_ID_VALID(e->id))
			continue;

		// extrapolate entity position using entity velocity
		e->x += dt*e->dx;
		e->y += dt*e->dy;
	}

	// ------------------------------------------------------------------
	// simulate particles

	for (size_t i = 0; i < MAX_PARTICLE_COUNT; i++)
	{
		cl_particle_t *particle = &g_particles[i];
		if (particle->t > 0.0f)
		{
			particle->t -= dt;
			particle->x += dt*particle->dx;
			particle->y += dt*particle->dy;
		}
	}
}

void World_Draw(void)
{
	ClearBackground(DARKBLUE);

	cl_player_t *client = &g_client;

	// ------------------------------------------------------------------
	// draw entities

	for (size_t i = MIN_ENTITY_INDEX; i < MAX_ENTITY_COUNT; i++)
	{
		cl_entity_t *e = &g_entities[i];

		if (!ENTITY_ID_VALID(e->id))
			continue;

		float x = e->x;
		float y = e->y;
		ToCameraSpace(client, &x, &y);

		int size   = (int)e->size;
		int radius = size / 2;

		Color color = g_colors[e->id.value % ARRAY_COUNT(g_colors)];
		DrawRectangle((int)x - radius, (int)y - radius, size, size, color);

		if (e->name[0])
		{
			DrawText(e->name, (int)x + radius + 2, (int)y - radius - 2, 12, color);
		}
	}

	// ------------------------------------------------------------------
	// draw particles

	for (size_t i = 0; i < MAX_PARTICLE_COUNT; i++)
	{
		cl_particle_t *particle = &g_particles[i];
		if (particle->t > 0.0f)
		{
			float x = particle->x;
			float y = particle->y;
			ToCameraSpace(client, &x, &y);

			int particle_size = 4;
			DrawRectangle(
				(int)x - particle_size / 2, 
				(int)y - particle_size / 2, 
				particle_size, 
				particle_size, 
				particle->color
			);
		}
	}
}

void CL_DrawDebug(void)
{
	if (g_show_debug_info)
	{
		net_stats_t net_stats;
		Net_GetStats(&net_stats);

		int font_height = 12;
		int y = 12;

		char text[1024];

		{
			DrawText("press f3 to toggle this information", 12, y, font_height, WHITE);
			y += 2*font_height;
		}

		{
			snprintf(text, sizeof(text), "packet acceptance rate:   %d%%", (int)(100.0f*net_stats.packets_accepted_ratio));

			DrawText(text, 12, y, font_height, WHITE);
			y += font_height;
		}

		{
			snprintf(text, sizeof(text), "%d kbps", (int)(net_stats.bytes_out_per_second / 1024.0f));

			DrawText("up:", 12, y, font_height, WHITE);
			DrawText(text, 64, y, font_height, WHITE);
			y += font_height;
		}

		{
			snprintf(text, sizeof(text), "%d kbps", (int)(net_stats.bytes_in_per_second / 1024.0f));

			DrawText("down:", 12, y, font_height, WHITE);
			DrawText(text, 64, y, font_height, WHITE);
			y += font_height;
		}
	}
}