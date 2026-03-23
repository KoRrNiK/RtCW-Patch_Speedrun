/*
===========================================================================
cl_perf.c  -  Performance profiler & frame-time graph overlay for RtCW SP

Runs inside the client engine (WolfSP.exe).
Provides a visual real-time overlay showing:
  - Frame time graph with color-coded bars (green/yellow/red)
  - Subsystem time breakdown (server, events, client, renderer FE/BE)
  - Statistics: FPS, avg/min/max/1%low frame time, variance
  - Renderer stats: surfaces, tris, shaders, leaves

Cvars:
  cl_perfGraph     0-3    0=off, 1=frame graph, 2=graph+stats, 3=full detail
  cl_perfGraphH    80     Graph height in virtual pixels
  cl_perfGraphW    300    Graph width in virtual pixels
  cl_perfGraphX    332    Graph X position (right edge)
  cl_perfGraphY    475    Graph Y position (bottom edge)
  cl_perfGraphAlpha 0.8   Graph background opacity

Toggle command:
  perfgraph              Cycle through modes (0->1->2->3->0)
===========================================================================
*/

#include "client.h"

/* ===================================================================
   Configuration
   =================================================================== */

#define PERF_HISTORY_SIZE   512     /* must be power of 2 */
#define PERF_HISTORY_MASK   ( PERF_HISTORY_SIZE - 1 )

/* Target frametime thresholds (ms) for color coding */
#define PERF_GOOD_MS    16.7f   /* 60 fps */
#define PERF_WARN_MS    33.3f   /* 30 fps */
#define PERF_BAD_MS     50.0f   /* 20 fps */

/* ===================================================================
   Cvars
   =================================================================== */

static cvar_t *cl_perfGraph;
static cvar_t *cl_perfGraphH;
static cvar_t *cl_perfGraphW;
static cvar_t *cl_perfGraphX;
static cvar_t *cl_perfGraphY;
static cvar_t *cl_perfGraphAlpha;

/* ===================================================================
   Frame sample data
   =================================================================== */

typedef struct {
    float   frameMs;        /* total frame time in ms */
    float   serverMs;       /* server time */
    float   eventMs;        /* event processing */
    float   clientMs;       /* client logic (minus renderer) */
    float   rendFrontMs;    /* renderer frontend */
    float   rendBackMs;     /* renderer backend */
    int     surfaces;       /* renderer surface count */
    int     tris;           /* triangle count */
    int     shaders;        /* active shaders */
    int     leafs;          /* visible BSP leaves */
} perfSample_t;

static perfSample_t s_history[PERF_HISTORY_SIZE];
static int          s_historyIdx;
static int          s_sampleCount;  /* total samples collected */

/* Per-frame timing checkpoints (set from Com_Frame) */
static int  s_timeBeforeFirstEvents;
static int  s_timeBeforeServer;
static int  s_timeBeforeEvents;
static int  s_timeBeforeClient;
static int  s_timeAfterClient;

/* ===================================================================
   Colors
   =================================================================== */

static const float colorBg[]        = { 0.0f, 0.0f, 0.0f, 0.7f };
static const float colorGrid33[]    = { 0.3f, 0.3f, 0.3f, 0.4f };
static const float colorGrid16[]    = { 0.2f, 0.5f, 0.2f, 0.4f };
static const float colorGood[]      = { 0.1f, 0.9f, 0.1f, 0.9f };
static const float colorWarn[]      = { 0.9f, 0.9f, 0.1f, 0.9f };
static const float colorBad[]       = { 0.9f, 0.1f, 0.1f, 0.9f };
static const float colorServer[]    = { 0.2f, 0.4f, 0.9f, 0.8f };
static const float colorEvent[]     = { 0.5f, 0.5f, 0.5f, 0.6f };
static const float colorClient[]    = { 0.9f, 0.6f, 0.1f, 0.8f };
static const float colorRendFE[]    = { 0.1f, 0.9f, 0.5f, 0.8f };
static const float colorRendBE[]    = { 0.9f, 0.1f, 0.5f, 0.8f };
static const float colorPerfWhite[]  = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float colorLabel[]     = { 0.7f, 0.8f, 0.7f, 0.9f };
static const float colorDim[]       = { 0.5f, 0.6f, 0.5f, 0.7f };
static const float colorStatGood[]  = { 0.3f, 1.0f, 0.3f, 1.0f };
static const float colorStatWarn[]  = { 1.0f, 1.0f, 0.3f, 1.0f };
static const float colorStatBad[]   = { 1.0f, 0.3f, 0.3f, 1.0f };

/* ===================================================================
   Helper: pick color based on frame time
   =================================================================== */

static const float *Perf_FrameColor( float ms ) {
    if ( ms <= PERF_GOOD_MS ) return colorGood;
    if ( ms <= PERF_WARN_MS ) return colorWarn;
    return colorBad;
}

static const float *Perf_StatColor( float ms ) {
    if ( ms <= PERF_GOOD_MS ) return colorStatGood;
    if ( ms <= PERF_WARN_MS ) return colorStatWarn;
    return colorStatBad;
}

/* ===================================================================
   Comparison for qsort (ascending float)
   =================================================================== */

static int Perf_CmpFloat( const void *a, const void *b ) {
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    if ( fa < fb ) return -1;
    if ( fa > fb ) return 1;
    return 0;
}

/* ===================================================================
   Public: record a frame sample
   Called from Com_Frame after all subsystems have run.
   =================================================================== */

void SCR_PerfRecordFrame( int tBeforeFirst, int tBeforeServer,
                          int tBeforeEvents, int tBeforeClient,
                          int tAfterClient ) {
    perfSample_t *s;
    int total;

    if ( !cl_perfGraph || !cl_perfGraph->integer ) {
        return;
    }

    s = &s_history[s_historyIdx & PERF_HISTORY_MASK];

    total = tAfterClient - tBeforeFirst;
    if ( total < 0 ) total = 0;
    if ( total > 1000 ) total = 1000; /* clamp absurd spikes */

    s->frameMs     = (float)total;
    s->serverMs    = (float)( tBeforeEvents - tBeforeServer );
    s->eventMs     = (float)( tBeforeServer - tBeforeFirst + tBeforeClient - tBeforeEvents );
    s->clientMs    = (float)( tAfterClient - tBeforeClient ) - (float)time_frontend - (float)time_backend;
    s->rendFrontMs = (float)time_frontend;
    s->rendBackMs  = (float)time_backend;
    s->surfaces    = 0;
    s->tris        = 0;
    s->shaders     = 0;
    s->leafs       = 0;

    /* Clamp negatives from timer imprecision */
    if ( s->serverMs < 0 ) s->serverMs = 0;
    if ( s->eventMs < 0 ) s->eventMs = 0;
    if ( s->clientMs < 0 ) s->clientMs = 0;
    if ( s->rendFrontMs < 0 ) s->rendFrontMs = 0;
    if ( s->rendBackMs < 0 ) s->rendBackMs = 0;

    s_historyIdx++;
    if ( s_sampleCount < PERF_HISTORY_SIZE ) {
        s_sampleCount++;
    }
}

/* ===================================================================
   Draw helper: filled rect with per-bar alpha
   =================================================================== */

static void Perf_DrawRect( float x, float y, float w, float h, const float *color ) {
    float c[4];
    c[0] = color[0]; c[1] = color[1]; c[2] = color[2];
    c[3] = color[3] * cl_perfGraphAlpha->value;
    SCR_FillRect( x, y, w, h, c );
}

/* ===================================================================
   Draw helper: small text
   =================================================================== */

static void Perf_DrawText( float x, float y, float charW, const char *text, const float *color ) {
    float c[4];
    c[0] = color[0]; c[1] = color[1]; c[2] = color[2];
    c[3] = color[3];
    SCR_DrawStringExt( (int)x, (int)y, charW, text, c, qtrue );
}

/* ===================================================================
   Draw: Frame time graph (mode >= 1)
   =================================================================== */

static void Perf_DrawGraph( float gx, float gy, float gw, float gh ) {
    int i, count, idx;
    float barW, maxMs, x, h;
    float bgColor[4];
    const perfSample_t *s;

    count = (int)gw;
    if ( count > s_sampleCount ) count = s_sampleCount;
    if ( count > PERF_HISTORY_SIZE ) count = PERF_HISTORY_SIZE;
    if ( count < 1 ) return;

    barW = 1.0f;
    maxMs = PERF_BAD_MS; /* graph scale = 50ms */

    /* Background */
    bgColor[0] = colorBg[0]; bgColor[1] = colorBg[1]; bgColor[2] = colorBg[2];
    bgColor[3] = colorBg[3] * cl_perfGraphAlpha->value;
    SCR_FillRect( gx, gy - gh, gw, gh, bgColor );

    /* Grid lines */
    /* 16.7ms line (60fps) */
    h = ( PERF_GOOD_MS / maxMs ) * gh;
    if ( h > 0 && h < gh ) {
        SCR_FillRect( gx, gy - h, gw, 1, colorGrid16 );
    }
    /* 33.3ms line (30fps) */
    h = ( PERF_WARN_MS / maxMs ) * gh;
    if ( h > 0 && h < gh ) {
        SCR_FillRect( gx, gy - h, gw, 1, colorGrid33 );
    }

    /* Draw bars from right (newest) to left (oldest) */
    for ( i = 0; i < count; i++ ) {
        idx = ( s_historyIdx - 1 - i ) & PERF_HISTORY_MASK;
        s = &s_history[idx];
        x = gx + gw - 1.0f - (float)i * barW;

        if ( x < gx ) break;

        if ( cl_perfGraph->integer >= 3 ) {
            /* Stacked subsystem bars */
            float baseY = gy;
            float sv, ev, cl, fe, be;

            sv = ( s->serverMs / maxMs ) * gh;
            ev = ( s->eventMs / maxMs ) * gh;
            cl = ( s->clientMs / maxMs ) * gh;
            fe = ( s->rendFrontMs / maxMs ) * gh;
            be = ( s->rendBackMs / maxMs ) * gh;

            /* Clamp total */
            if ( sv + ev + cl + fe + be > gh ) {
                float scale = gh / ( sv + ev + cl + fe + be + 0.001f );
                sv *= scale; ev *= scale; cl *= scale; fe *= scale; be *= scale;
            }

            /* Draw bottom-up: server, events, client, frontend, backend */
            if ( sv > 0.5f ) {
                SCR_FillRect( x, baseY - sv, barW, sv, colorServer );
                baseY -= sv;
            }
            if ( ev > 0.5f ) {
                SCR_FillRect( x, baseY - ev, barW, ev, colorEvent );
                baseY -= ev;
            }
            if ( cl > 0.5f ) {
                SCR_FillRect( x, baseY - cl, barW, cl, colorClient );
                baseY -= cl;
            }
            if ( fe > 0.5f ) {
                SCR_FillRect( x, baseY - fe, barW, fe, colorRendFE );
                baseY -= fe;
            }
            if ( be > 0.5f ) {
                SCR_FillRect( x, baseY - be, barW, be, colorRendBE );
            }
        } else {
            /* Single-color bar based on frame time */
            float barH = ( s->frameMs / maxMs ) * gh;
            if ( barH > gh ) barH = gh;
            if ( barH < 1.0f ) barH = 1.0f;

            SCR_FillRect( x, gy - barH, barW, barH, Perf_FrameColor( s->frameMs ) );
        }
    }

    /* Border */
    {
        float borderColor[4] = { 0.4f, 0.4f, 0.4f, 0.6f };
        SCR_FillRect( gx, gy - gh, gw, 1, borderColor );           /* top */
        SCR_FillRect( gx, gy - 1, gw, 1, borderColor );            /* bottom */
        SCR_FillRect( gx, gy - gh, 1, gh, borderColor );            /* left */
        SCR_FillRect( gx + gw - 1, gy - gh, 1, gh, borderColor );  /* right */
    }

    /* Scale labels */
    {
        char buf[32];
        Com_sprintf( buf, sizeof(buf), "60" );
        Perf_DrawText( gx + 2, gy - ( PERF_GOOD_MS / maxMs ) * gh - 8, 3, buf, colorGrid16 );
        Com_sprintf( buf, sizeof(buf), "30" );
        Perf_DrawText( gx + 2, gy - ( PERF_WARN_MS / maxMs ) * gh - 8, 3, buf, colorGrid33 );
    }
}

/* ===================================================================
   Draw: Statistics panel (mode >= 2)
   =================================================================== */

static void Perf_DrawStats( float sx, float sy ) {
    int i, count, idx;
    float sorted[PERF_HISTORY_SIZE];
    float avg, minMs, maxMs, onePercentLow, variance, sum, fps;
    float charW = 4.0f;
    float lineH = 8.0f;
    float y;
    float bgColor[4];
    float panelW = 140.0f;
    float panelH;
    char buf[128];
    const float *fpsColor;

    count = s_sampleCount;
    if ( count < 2 ) return;
    if ( count > PERF_HISTORY_SIZE ) count = PERF_HISTORY_SIZE;

    /* Gather frame times */
    for ( i = 0; i < count; i++ ) {
        idx = ( s_historyIdx - 1 - i ) & PERF_HISTORY_MASK;
        sorted[i] = s_history[idx].frameMs;
    }

    /* Compute stats */
    sum = 0;
    minMs = 99999.0f;
    maxMs = 0.0f;
    for ( i = 0; i < count; i++ ) {
        sum += sorted[i];
        if ( sorted[i] < minMs ) minMs = sorted[i];
        if ( sorted[i] > maxMs ) maxMs = sorted[i];
    }
    avg = sum / (float)count;
    fps = ( avg > 0.001f ) ? 1000.0f / avg : 999.0f;

    /* Variance (standard deviation) */
    variance = 0;
    for ( i = 0; i < count; i++ ) {
        float d = sorted[i] - avg;
        variance += d * d;
    }
    variance = (float)sqrt( variance / (float)count );

    /* 1% low: sort and take the 99th percentile */
    qsort( sorted, count, sizeof(float), Perf_CmpFloat );
    {
        int idx1p = count - 1 - count / 100;
        if ( idx1p < 0 ) idx1p = 0;
        onePercentLow = sorted[idx1p];
    }

    /* Draw panel */
    panelH = lineH * 8 + 4;  /* 8 lines */
    bgColor[0] = 0.0f; bgColor[1] = 0.0f; bgColor[2] = 0.0f;
    bgColor[3] = 0.6f * cl_perfGraphAlpha->value;
    SCR_FillRect( sx, sy, panelW, panelH, bgColor );

    y = sy + 2;

    /* FPS */
    fpsColor = Perf_StatColor( avg );
    Com_sprintf( buf, sizeof(buf), "FPS: %.0f", fps );
    Perf_DrawText( sx + 3, y, charW, buf, fpsColor );
    y += lineH;

    /* Avg frame time */
    Com_sprintf( buf, sizeof(buf), "Avg: %.1f ms", avg );
    Perf_DrawText( sx + 3, y, charW, buf, Perf_StatColor( avg ) );
    y += lineH;

    /* Min frame time */
    Com_sprintf( buf, sizeof(buf), "Min: %.1f ms", minMs );
    Perf_DrawText( sx + 3, y, charW, buf, Perf_StatColor( minMs ) );
    y += lineH;

    /* Max frame time */
    Com_sprintf( buf, sizeof(buf), "Max: %.1f ms", maxMs );
    Perf_DrawText( sx + 3, y, charW, buf, Perf_StatColor( maxMs ) );
    y += lineH;

    /* 1% low (worst frames) */
    {
        float onePercentFps = ( onePercentLow > 0.001f ) ? 1000.0f / onePercentLow : 999.0f;
        Com_sprintf( buf, sizeof(buf), "1%%low: %.0f fps", onePercentFps );
        Perf_DrawText( sx + 3, y, charW, buf, Perf_StatColor( onePercentLow ) );
        y += lineH;
    }

    /* Variance (stdev) */
    Com_sprintf( buf, sizeof(buf), "Jitter: %.1f ms", variance );
    {
        const float *jitCol = ( variance < 3.0f ) ? colorStatGood :
                              ( variance < 8.0f ) ? colorStatWarn : colorStatBad;
        Perf_DrawText( sx + 3, y, charW, buf, jitCol );
    }
    y += lineH;

    /* Current frame */
    {
        int newest = ( s_historyIdx - 1 ) & PERF_HISTORY_MASK;
        float curMs = s_history[newest].frameMs;
        float curFps = ( curMs > 0.001f ) ? 1000.0f / curMs : 999.0f;
        Com_sprintf( buf, sizeof(buf), "Now: %.0f fps (%.1fms)", curFps, curMs );
        Perf_DrawText( sx + 3, y, charW, buf, Perf_FrameColor( curMs ) );
    }
    y += lineH;

    /* Samples */
    Com_sprintf( buf, sizeof(buf), "Samples: %d", count );
    Perf_DrawText( sx + 3, y, charW, buf, colorDim );
}

/* ===================================================================
   Draw: Subsystem breakdown legend (mode 3)
   =================================================================== */

static void Perf_DrawBreakdownLegend( float lx, float ly ) {
    float charW = 3.5f;
    float lineH = 7.0f;
    float boxSz = 5.0f;
    float y = ly;
    float bgColor[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
    float panelW = 100.0f;
    float panelH = lineH * 5 + 4;

    bgColor[3] *= cl_perfGraphAlpha->value;
    SCR_FillRect( lx, ly, panelW, panelH, bgColor );

    y += 2;

    SCR_FillRect( lx + 3, y + 1, boxSz, boxSz, colorServer );
    Perf_DrawText( lx + 3 + boxSz + 2, y, charW, "Server", colorLabel );
    y += lineH;

    SCR_FillRect( lx + 3, y + 1, boxSz, boxSz, colorEvent );
    Perf_DrawText( lx + 3 + boxSz + 2, y, charW, "Events", colorLabel );
    y += lineH;

    SCR_FillRect( lx + 3, y + 1, boxSz, boxSz, colorClient );
    Perf_DrawText( lx + 3 + boxSz + 2, y, charW, "Client", colorLabel );
    y += lineH;

    SCR_FillRect( lx + 3, y + 1, boxSz, boxSz, colorRendFE );
    Perf_DrawText( lx + 3 + boxSz + 2, y, charW, "Rend FE", colorLabel );
    y += lineH;

    SCR_FillRect( lx + 3, y + 1, boxSz, boxSz, colorRendBE );
    Perf_DrawText( lx + 3 + boxSz + 2, y, charW, "Rend BE", colorLabel );
}

/* ===================================================================
   Draw: Subsystem time bars for last frame (mode 3)
   =================================================================== */

static void Perf_DrawBreakdownBars( float bx, float by ) {
    int newest;
    const perfSample_t *s;
    float totalMs, barFullW, y, barH, w;
    float bgColor[4] = { 0.0f, 0.0f, 0.0f, 0.5f };
    float panelW = 140.0f;
    float charW = 3.5f;
    float lineH = 9.0f;
    char buf[64];

    if ( s_sampleCount < 1 ) return;

    newest = ( s_historyIdx - 1 ) & PERF_HISTORY_MASK;
    s = &s_history[newest];
    totalMs = s->frameMs;
    if ( totalMs < 0.001f ) return;

    barFullW = panelW - 50.0f;
    barH = 6.0f;
    bgColor[3] *= cl_perfGraphAlpha->value;
    SCR_FillRect( bx, by, panelW, lineH * 5 + 4, bgColor );

    y = by + 2;

    /* Server */
    w = ( s->serverMs / totalMs ) * barFullW;
    SCR_FillRect( bx + 46, y, w, barH, colorServer );
    Com_sprintf( buf, sizeof(buf), "SV %4.1f", s->serverMs );
    Perf_DrawText( bx + 3, y, charW, buf, colorLabel );
    y += lineH;

    /* Events */
    w = ( s->eventMs / totalMs ) * barFullW;
    SCR_FillRect( bx + 46, y, w, barH, colorEvent );
    Com_sprintf( buf, sizeof(buf), "EV %4.1f", s->eventMs );
    Perf_DrawText( bx + 3, y, charW, buf, colorLabel );
    y += lineH;

    /* Client */
    w = ( s->clientMs / totalMs ) * barFullW;
    SCR_FillRect( bx + 46, y, w, barH, colorClient );
    Com_sprintf( buf, sizeof(buf), "CL %4.1f", s->clientMs );
    Perf_DrawText( bx + 3, y, charW, buf, colorLabel );
    y += lineH;

    /* Renderer Frontend */
    w = ( s->rendFrontMs / totalMs ) * barFullW;
    SCR_FillRect( bx + 46, y, w, barH, colorRendFE );
    Com_sprintf( buf, sizeof(buf), "FE %4.1f", s->rendFrontMs );
    Perf_DrawText( bx + 3, y, charW, buf, colorLabel );
    y += lineH;

    /* Renderer Backend */
    w = ( s->rendBackMs / totalMs ) * barFullW;
    SCR_FillRect( bx + 46, y, w, barH, colorRendBE );
    Com_sprintf( buf, sizeof(buf), "BE %4.1f", s->rendBackMs );
    Perf_DrawText( bx + 3, y, charW, buf, colorLabel );
}

/* ===================================================================
   Command: perfgraph - cycle modes
   =================================================================== */

static void Cmd_PerfGraph_f( void ) {
    int mode;
    if ( !cl_perfGraph ) return;
    mode = ( cl_perfGraph->integer + 1 ) % 4;
    Cvar_SetValue( "cl_perfGraph", mode );
    switch ( mode ) {
        case 0: Com_Printf( "Performance graph: OFF\n" ); break;
        case 1: Com_Printf( "Performance graph: Frame graph\n" ); break;
        case 2: Com_Printf( "Performance graph: Graph + stats\n" ); break;
        case 3: Com_Printf( "Performance graph: Full detail (stacked + breakdown)\n" ); break;
    }
}

/* ===================================================================
   Public: Init
   =================================================================== */

void SCR_PerfInit( void ) {
    cl_perfGraph      = Cvar_Get( "cl_perfGraph",      "0",   CVAR_ARCHIVE );
    cl_perfGraphH     = Cvar_Get( "cl_perfGraphH",     "80",  CVAR_ARCHIVE );
    cl_perfGraphW     = Cvar_Get( "cl_perfGraphW",     "300", CVAR_ARCHIVE );
    cl_perfGraphX     = Cvar_Get( "cl_perfGraphX",     "332", CVAR_ARCHIVE );
    cl_perfGraphY     = Cvar_Get( "cl_perfGraphY",     "475", CVAR_ARCHIVE );
    cl_perfGraphAlpha = Cvar_Get( "cl_perfGraphAlpha", "0.8", CVAR_ARCHIVE );

    Cmd_AddCommand( "perfgraph", Cmd_PerfGraph_f );

    memset( s_history, 0, sizeof( s_history ) );
    s_historyIdx = 0;
    s_sampleCount = 0;

    Com_Printf( "Performance profiler initialized.\n" );
}

/* ===================================================================
   Public: Shutdown
   =================================================================== */

void SCR_PerfShutdown( void ) {
    Cmd_RemoveCommand( "perfgraph" );
}

/* ===================================================================
   Public: Draw the overlay
   Called from SCR_DrawScreenField after everything else.
   =================================================================== */

void SCR_PerfDraw( void ) {
    int mode;
    float gx, gy, gw, gh;

    if ( !cl_perfGraph || !cl_perfGraph->integer ) {
        return;
    }

    mode = cl_perfGraph->integer;
    gx = cl_perfGraphX->value;
    gy = cl_perfGraphY->value;
    gw = cl_perfGraphW->value;
    gh = cl_perfGraphH->value;

    /* Clamp */
    if ( gw < 50 ) gw = 50;
    if ( gw > 600 ) gw = 600;
    if ( gh < 20 ) gh = 20;
    if ( gh > 300 ) gh = 300;

    /* Mode 1+: Frame time graph */
    if ( mode >= 1 ) {
        Perf_DrawGraph( gx, gy, gw, gh );
    }

    /* Mode 2+: Statistics panel */
    if ( mode >= 2 ) {
        Perf_DrawStats( gx, gy - gh - 2 );
    }

    /* Mode 3: Stacked bars + legend + breakdown */
    if ( mode >= 3 ) {
        float legendY = gy - gh - 2 - 68 - 2;     /* above stats panel */
        Perf_DrawBreakdownLegend( gx, legendY );
        Perf_DrawBreakdownBars( gx + 104, legendY );
    }
}
