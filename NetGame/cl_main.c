// ------------------------------------------------------------------
// standard library includes

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

// ------------------------------------------------------------------
// external includes

#include <raylib.h>

// ------------------------------------------------------------------
// internal includes

#include "os.h"
#include "util.h"
#include "cl_net.h"
#include "cl_client.h"

// ------------------------------------------------------------------
// cl_main.c: the entry-point for the client application. the client
// does not do any simulation, all it does is send input to the 
// server and receives world state updates from the server, and 
// renders said world state, as well as client-side effects.
// it also does basic extrapolation of entity movement purely for
// cosmetic reasons.
//
// raylib is used for creating the window, polling input devices
// and rendering graphics
//
// stuff left TODO:
// - some audio, just for fun
// - switch to full continues state updates instead of just sending
//   changes
// - maybe print some fun internet facts, make a tiny main menu?
// - make the game look a bit less drab
// - detect timeout on server and client


// ------------------------------------------------------------------
// main loop

static const int g_tickrate = 120;

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	char *server = "localhost";
	int   port   = 4950;

	if (argc == 2)
	{
		char *arg = argv[1];
		for (char *c = arg; *c; c++)
		{
			if (*c == ':')
			{
				*c = 0;
				port = atoi(c + 1);
			}
		}
		server = arg;
	}

	if (CL_NetInit(server, port) != 0)
	{
		fprintf(stderr, "Failed to initialize networking subsystem\n");
		return 1;
	}

	// if you open multiple clients to see the multiplayer in action, stuff can really slow down
	// if you're not using some kind of fps limiting
	SetConfigFlags(FLAG_VSYNC_HINT);

	InitWindow(800, 600, "NetClient");

	CL_Init();

	float tick_timer = 0.0f;
	float seconds_per_tick = 1.0f / (float)g_tickrate;

	while (!WindowShouldClose())
	{
		float dt  = GetFrameTime();
		int   fps = GetFPS(); 

		tick_timer += dt;
		while (tick_timer > seconds_per_tick)
		{
			tick_timer -= seconds_per_tick;
			CL_Tick(seconds_per_tick);
		}

		BeginDrawing();
		CL_Draw();
		CL_DrawDebug();
		EndDrawing();

		char title[256];
		snprintf(title, sizeof(title), "NetClient: frametime %.02f ms (%d fps)", 1000.0f*dt, fps);

		SetWindowTitle(title);
	}

	CloseWindow();

	CL_SendPacket(&(net_header_t) { NETPACKET_CLIENT_DISCONNECTED });

	if (CL_NetExit() != 0)
	{
		fprintf(stderr, "Failed to shut down networking subsystem\n");
		return 1;
	}

	return 0;
}