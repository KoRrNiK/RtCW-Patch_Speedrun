/*
===========================================================================
ls_shared.h  --  Shared memory IPC between game and standalone LiveSplit exe

Both the game (WolfSP.exe) and the standalone viewer (RtCW_LiveSplit.exe)
include this header.  Communication is through a named memory-mapped file
containing a single lsWndState_t struct + a sequence counter.
===========================================================================
*/

#ifndef LS_SHARED_H
#define LS_SHARED_H

#include "ls_types.h"

/* Shared memory object name (visible system-wide) */
#define LS_SHM_NAME   "RtCW_LiveSplit_SharedState"
#define LS_SHM_MUTEX  "RtCW_LiveSplit_Mutex"

/* Version tag to detect struct layout mismatches */
#define LS_SHM_VERSION  1

/* The shared memory block layout */
typedef struct {
	int            version;    /* LS_SHM_VERSION */
	volatile long  sequence;   /* incremented by game after each update */
	volatile int   gameActive; /* 1 while game is running, 0 when disconnected */
	lsWndState_t   state;     /* the actual timing data */
} lsSharedMem_t;

#endif /* LS_SHARED_H */
