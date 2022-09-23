// ------------------------------------------------------------------
// standard library includes

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdalign.h>

// ------------------------------------------------------------------
// internal includes

#include "protocol.h"
#include "util.h"
#include "net.h"
#include "os.h"
#include "sv_simulation.h"
#include "sv_server.h"

// ------------------------------------------------------------------
// sv_main.c: entry point for the server application


// ------------------------------------------------------------------
// constants

enum { PORT = 4950 };

// the "framerate" of the serverside simulation
static int    g_tickrate = 120;

// if clients go off-grid for longer than this many seconds, we 
// consider them disconnected. (TODO: reimplement!)
static double g_client_timeout_time = 10.0;

// ------------------------------------------------------------------
// main loop

int main(int argc, char **argv)
{
	bool local_session = false;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-local_session") == 0)
		{
			local_session = true;
		}
		else
		{
			fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
		}
	}

	SV_Init(PORT);

	double tick_timer = 0.0;
	double seconds_per_tick = 1.0 / (double)g_tickrate;

	os_time_t start_time = OS_GetHiresTime();

	for (;;)
	{
		// TODO: How to make this less busy-waity?

		if (tick_timer >= seconds_per_tick)
		{
			SV_ProcessPackets();

			Sim_Run((float)seconds_per_tick);
			tick_timer -= seconds_per_tick;
		}

		os_time_t end_time = OS_GetHiresTime();
		tick_timer += OS_GetSecondsElapsed(start_time, end_time);

		SWAP(os_time_t, start_time, end_time);
	}

	// SV_Exit();
}