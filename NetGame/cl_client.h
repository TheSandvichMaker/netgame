#pragma once

// ------------------------------------------------------------------
// cl_client.h: main functions that implement the client's
// functionality

void CL_Init(void);

// per-tick simulation
void CL_Tick(float dt);

// per-frame drawing
void CL_Draw(void);

// debug ui drawing
void CL_DrawDebug(void);
