/*
===========================================================================

SDL3 DMA sound backend, adapted from RealRTCW's SDL audio path.

===========================================================================
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sdl_local.h"
#include "../client/snd_local.h"
#include "../sys/core/sys_local.h"

static qboolean snd_inited = qfalse;

static cvar_t *s_sdlBits;
static cvar_t *s_sdlSpeed;
static cvar_t *s_sdlChannels;
static cvar_t *s_sdlMixSamps;
static cvar_t *s_muteUnfocused;

static int dmapos = 0;
static int dmasize = 0;
static qboolean sdlAudioPaused = qfalse;
static SDL_AudioStream *sdlPlaybackStream = NULL;

static int SNDDMA_NextPowerOfTwo( int value ) {
	int result = 1;

	while ( result < value && result < ( 1 << 30 ) ) {
		result <<= 1;
	}
	return result;
}

static void SDLCALL SNDDMA_AudioCallback( void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount ) {
	int pos;
	int len;
	int tobufend;
	int len1;
	int len2;
	int sampleBytes;

	(void)userdata;
	(void)total_amount;

	if ( !snd_inited || !dma.buffer || dma.samplebits <= 0 ) {
		return;
	}

	sampleBytes = dma.samplebits / 8;
	pos = dmapos * sampleBytes;
	if ( pos >= dmasize ) {
		dmapos = 0;
		pos = 0;
	}

	len = additional_amount;
	len -= len % sampleBytes;
	if ( len <= 0 ) {
		return;
	}

	tobufend = dmasize - pos;
	len1 = len;
	len2 = 0;

	if ( len1 > tobufend ) {
		len1 = tobufend;
		len2 = len - len1;
	}

	SDL_PutAudioStreamData( stream, dma.buffer + pos, len1 );
	if ( len2 <= 0 ) {
		dmapos += len1 / sampleBytes;
	} else {
		SDL_PutAudioStreamData( stream, dma.buffer, len2 );
		dmapos = len2 / sampleBytes;
	}

	if ( dmapos >= dma.samples ) {
		dmapos %= dma.samples;
	}
	dma.samplepos = dmapos;
}

static void SNDDMA_PrintAudiospec( const char *label, const SDL_AudioSpec *spec ) {
	Com_Printf( "%s:\n", label );
	Com_Printf( "  Format:   %s\n", SDL_GetAudioFormatName( spec->format ) );
	Com_Printf( "  Freq:     %d\n", spec->freq );
	Com_Printf( "  Channels: %d\n", spec->channels );
}

qboolean SNDDMA_Init( void ) {
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	int tmp;

	if ( snd_inited ) {
		return qtrue;
	}

	if ( !s_sdlBits ) {
		s_sdlBits = Cvar_Get( "s_sdlBits", "16", CVAR_ARCHIVE );
		s_sdlSpeed = Cvar_Get( "s_sdlSpeed", "0", CVAR_ARCHIVE );
		s_sdlChannels = Cvar_Get( "s_sdlChannels", "2", CVAR_ARCHIVE );
		s_sdlMixSamps = Cvar_Get( "s_sdlMixSamps", "32768", CVAR_ARCHIVE );
		s_muteUnfocused = Cvar_Get( "s_muteUnfocused", "1", CVAR_ARCHIVE );
	}

	Com_DPrintf( "SDL_Init( SDL_INIT_AUDIO )... " );
	if ( !SDL_Init( SDL_INIT_AUDIO ) ) {
		Com_Printf( "SDL_Init( SDL_INIT_AUDIO ) FAILED (%s)\n", SDL_GetError() );
		return qfalse;
	}
	Com_DPrintf( "OK\n" );
	Com_Printf( "SDL audio driver: %s\n", SDL_GetCurrentAudioDriver() );

	memset( &desired, 0, sizeof( desired ) );
	memset( &obtained, 0, sizeof( obtained ) );

	tmp = s_sdlBits->integer;
	if ( tmp != 8 && tmp != 16 ) {
		tmp = 16;
	}

	desired.freq = s_sdlSpeed->integer;
	if ( desired.freq <= 0 ) {
		if ( s_khz && s_khz->integer == 48 ) {
			desired.freq = 48000;
		} else if ( s_khz && s_khz->integer == 44 ) {
			desired.freq = 44100;
		} else if ( s_khz && s_khz->integer == 22 ) {
			desired.freq = 22050;
		} else {
			desired.freq = 11025;
		}
	}
	desired.format = ( tmp == 16 ) ? SDL_AUDIO_S16 : SDL_AUDIO_U8;
	desired.channels = s_sdlChannels->integer;
	if ( desired.channels != 1 && desired.channels != 2 ) {
		desired.channels = 2;
	}

	sdlPlaybackStream = SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SNDDMA_AudioCallback, NULL );
	if ( !sdlPlaybackStream ) {
		Com_Printf( "SDL_OpenAudioDeviceStream() failed: %s\n", SDL_GetError() );
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

	obtained = desired;
	SNDDMA_PrintAudiospec( "SDL_AudioSpec", &obtained );

	tmp = s_sdlMixSamps->integer;
	if ( tmp <= 0 ) {
		tmp = 32768;
	}
	if ( tmp < 32768 ) {
		Com_Printf( "SDL audio mix buffer too small, using 32768 samples\n" );
		tmp = 32768;
	}
	tmp = SNDDMA_NextPowerOfTwo( tmp );
	if ( tmp % obtained.channels ) {
		tmp = SNDDMA_NextPowerOfTwo( tmp + obtained.channels );
	}
	Com_Printf( "SDL audio DMA samples: %d\n", tmp );

	dmapos = 0;
	dma.samplebits = SDL_AUDIO_BITSIZE( obtained.format );
	dma.channels = obtained.channels;
	dma.samples = tmp;
	dma.submission_chunk = 1;
	dma.speed = obtained.freq;
	dma.samplepos = 0;
	dmasize = dma.samples * ( dma.samplebits / 8 );
	dma.buffer = (byte *)calloc( 1, dmasize );
	if ( !dma.buffer ) {
		Com_Printf( "SDL audio failed to allocate %d bytes\n", dmasize );
		SDL_DestroyAudioStream( sdlPlaybackStream );
		sdlPlaybackStream = NULL;
		SDL_QuitSubSystem( SDL_INIT_AUDIO );
		return qfalse;
	}

	Com_Printf( "Starting SDL audio callback...\n" );
	snd_inited = qtrue;
	sdlAudioPaused = qfalse;
	SDL_ResumeAudioStreamDevice( sdlPlaybackStream );

	Com_Printf( "SDL audio initialized.\n" );
	return qtrue;
}

int SNDDMA_GetDMAPos( void ) {
	return dmapos;
}

void SNDDMA_Shutdown( void ) {
	if ( sdlPlaybackStream ) {
		Com_Printf( "Closing SDL audio playback device...\n" );
		SDL_DestroyAudioStream( sdlPlaybackStream );
		sdlPlaybackStream = NULL;
	}

	SDL_QuitSubSystem( SDL_INIT_AUDIO );
	if ( dma.buffer ) {
		free( dma.buffer );
		dma.buffer = NULL;
	}
	dmapos = 0;
	dmasize = 0;
	sdlAudioPaused = qfalse;
	snd_inited = qfalse;
	Com_Printf( "SDL audio shut down.\n" );
}

void SNDDMA_Submit( void ) {
	if ( sdlPlaybackStream ) {
		SDL_UnlockAudioStream( sdlPlaybackStream );
	}
}

void SNDDMA_BeginPainting( void ) {
	if ( sdlPlaybackStream ) {
		SDL_LockAudioStream( sdlPlaybackStream );
	}
}

void SNDDMA_Activate( void ) {
	qboolean shouldPause;

	if ( !snd_inited || !sdlPlaybackStream ) {
		return;
	}

	shouldPause = s_muteUnfocused && s_muteUnfocused->integer &&
		( !g_wv.activeApp || g_wv.isMinimized );

	if ( shouldPause == sdlAudioPaused ) {
		return;
	}

	if ( shouldPause ) {
		SDL_PauseAudioStreamDevice( sdlPlaybackStream );
	} else {
		SDL_ResumeAudioStreamDevice( sdlPlaybackStream );
	}

	sdlAudioPaused = shouldPause;
}
