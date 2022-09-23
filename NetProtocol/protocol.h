#pragma once

// ------------------------------------------------------------------
// protocol.h: this header provides the agreed upon "shared language"
// between the server and client. these are the packets that will be
// sent back and forth over the network to provide a basic 
// multiplayer experience

// this is the packet kind which is indicated in the header to let
// the receiving code know how to interpret the rest of the message
typedef enum net_packet_e
{
	// a ping to the server get responded to with another ping
	NETPACKET_PING,

	// this gets sent to the server from the client if they
	// (willingly) disconnect
	NETPACKET_CLIENT_DISCONNECTED,

	// this is the input state of the client, the main means
	// in which the client tells the server about its intentions
	NETPACKET_INPUT,

	// this is the packet that the server sends back to the client
	// to let it know about the entities in the game world
	NETPACKET_WORLD_STATE,
} net_packet_e;

// this is the header that needs to be in front of all packets
typedef struct net_header_t
{
	unsigned short kind;
	unsigned short sequence; // the sequence number can be used to discard (or re-order) out-of-order packets
} net_header_t;

// these are the input button states the client could send to
// the server
typedef enum net_button_e
{
	NETBTN_LEFT     = 1 << 0,
	NETBTN_RIGHT    = 1 << 1,
	NETBTN_UP       = 1 << 2,
	NETBTN_DOWN     = 1 << 3,
	NETBTN_SHOOT    = 1 << 4,
	NETBTN_KILL     = 1 << 5,
} net_button_e;

enum { NET_USERNAME_MAX_SIZE = 32 };

// this is the packet associated with NETPACKET_INPUT
typedef struct net_input_t
{
	// all packets start with the header
	net_header_t header;

	// the username of the client
	char name[NET_USERNAME_MAX_SIZE];

	// bitmask of held down buttons
	int btn_down;

	// mouse position in world space (the server doesn't have a concept
	// of screenspace)
	float mouse_x;
	float mouse_y;
} net_input_t;

// to indicate a dead/invalid entity, we reserve the 0th index
enum 
{ 
	INVALID_ENTITY_INDEX = 0, 
	MIN_ENTITY_INDEX     = 1, 
	MAX_ENTITY_INDEX     = 127, 
	MAX_ENTITY_COUNT     = MAX_ENTITY_INDEX + 1 ,
};

// entities can be uniquely identified through a combination of index 
// and generation the index can be used straight as an array index, 
// the generation makes it clear whether we are talking about the 
// same entity inside that slot in the array
typedef union net_entity_id_t
{
	struct
	{
		short index;
		short generation;
	};
	int value;
} net_entity_id_t;

#define ENTITY_ID_VALID(id) ((id).index >= MIN_ENTITY_INDEX && (id).index <= MAX_ENTITY_INDEX)

enum { MAX_CLIENT_COUNT = 32 };

// this tells the client what players are connected to the server
typedef struct net_player_t
{
	char name[NET_USERNAME_MAX_SIZE];
	net_entity_id_t entity;
} net_player_t;

typedef struct net_entity_state_t
{
	net_entity_id_t id;     // 4
	float x, y;             // 12
	float dx, dy;           // 20
	float size;             // 24
} net_entity_state_t;

// this packet comes to 4236 bytes, which will likely be fragmented into 3 separate IP packets,
// if any one of those fragments is lost, all of the packet is discarded.
// so this is not the best way to send a big state update!
// of course, we only need to send updates for entities that actually exist... exercise for the
// reader.
typedef struct net_world_state_t
{
	net_header_t header;

	unsigned player_count;
	net_player_t players[MAX_CLIENT_COUNT];

	net_entity_id_t client_id;
	net_entity_state_t world_state[MAX_ENTITY_COUNT];
} net_world_state_t;
