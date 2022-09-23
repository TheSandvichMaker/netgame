#pragma once

// ------------------------------------------------------------------
// sv_simulation.h: actual gameplay code stuff

typedef struct sv_client_t sv_client_t;
typedef struct net_header_t net_header_t;

enum
{
	EFLAG_HURTS   = 1 << 1,
};

typedef struct sv_entity_t
{
	net_entity_id_t id;

	unsigned short last_sequence;

	struct sv_entity_t *parent;

	int flags;

	float x, y;
	float dx, dy;

	float size;

	float lifetime;
} sv_entity_t;

// ------------------------------------------------------------------

sv_entity_t *E_FromId(net_entity_id_t id);
sv_entity_t *E_Spawn(void);
void         E_Destroy(sv_entity_t *entity);

void Sim_ProcessPacket(sv_client_t *client, net_header_t *packet);
void Sim_Run(float dt);
