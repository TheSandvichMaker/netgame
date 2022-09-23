#pragma once

// ------------------------------------------------------------------
// internal includes

#include "protocol.h"

// ------------------------------------------------------------------
// cl_net.h: interface for accessing networking in the client's
// application code, providing a simpler interface than if we used
// net.h directly.


// ------------------------------------------------------------------

int CL_NetInit(char *server, int port);
int CL_NetExit(void);

void CL_SendPacketSized(void *packet, size_t packet_size);

// this macro is nice, but be aware of the fact that it has `packet`
// in there twice, so don't put any calls that should happen only
// once in the compound literal (if you're using a compound literal)
#define CL_SendPacket(packet) CL_SendPacketSized(packet, sizeof(*(packet)))

// reuses a global buffer, so be careful about not calling it if you
// were still looking at the previous packet, or from multiple
// threads
net_header_t *CL_GetNextPacket(void);
