/*
===========================================================================
cg_movement.c - Movement HUD Systems

Contains all movement-related HUD overlays:
  - Position / Angles HUD (cg_drawPos)
  - Jump Statistics with per-bounce tracking (cg_drawJumpStats)
  - Movement Quality Bar

Two jump stats display modes (switched by bh_movement cvar):
  bh_movement 0 (RtCW default): per-jump height/dist/speed/sync
  bh_movement 1 (HL1 bhop):     speed-focused, chain counter, gain/loss

Bounce detection uses Z velocity sign reversal to catch
autobhop bounces that happen within a single server frame
(where groundEntityNum never shows as on-ground).
===========================================================================
*/

#include "cg_local.h"


/*
===========================================================================
Position / Angles HUD

Shows player XYZ coordinates, pitch/yaw view angles, and current
XY speed on a compact overlay.  Essential for documenting speedrun
routes and finding precise positions for tricks.

Usage:  cg_drawPos 1   (top-left corner)
        cg_drawPos 0   (off)
===========================================================================
*/

#define POS_HUD_X       2
#define POS_HUD_Y       2
#define POS_HUD_CW      4
#define POS_HUD_CH      6
#define POS_HUD_GAP     1

void CG_DrawPositionHUD( void ) {
	char buf[128];
	int curY;
	int len;
	vec4_t labelColor = { 0.6f, 0.8f, 1.0f, 0.85f };  /* light blue */
	vec4_t valueColor = { 1.0f, 1.0f, 1.0f, 0.9f };    /* white */
	vec4_t speedColor = { 1.0f, 1.0f, 0.4f, 0.85f };   /* yellow */
	float xySpeed;
	vec3_t hvel;

	if ( !cg_drawPos.integer ) {
		return;
	}

	curY = POS_HUD_Y;

	/* X */
	Com_sprintf( buf, sizeof( buf ), "X:%.1f", cg.snap->ps.origin[0] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, labelColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* Y */
	Com_sprintf( buf, sizeof( buf ), "Y:%.1f", cg.snap->ps.origin[1] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, labelColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* Z */
	Com_sprintf( buf, sizeof( buf ), "Z:%.1f", cg.snap->ps.origin[2] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, labelColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* YAW */
	Com_sprintf( buf, sizeof( buf ), "YAW:%.1f", cg.snap->ps.viewangles[YAW] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, valueColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* PITCH */
	Com_sprintf( buf, sizeof( buf ), "PIT:%.1f", cg.snap->ps.viewangles[PITCH] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, valueColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* XY speed */
	hvel[0] = cg.snap->ps.velocity[0];
	hvel[1] = cg.snap->ps.velocity[1];
	hvel[2] = 0;
	xySpeed = VectorLength( hvel );

	Com_sprintf( buf, sizeof( buf ), "SPD:%.0f", xySpeed );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf, speedColor, qtrue, qtrue,
		POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
	curY += POS_HUD_CH + POS_HUD_GAP;

	/* VZ */
	Com_sprintf( buf, sizeof( buf ), "VZ:%.0f", cg.snap->ps.velocity[2] );
	len = strlen( buf );
	CG_DrawStringExt( POS_HUD_X, curY, buf,
		cg.snap->ps.velocity[2] > 0 ? speedColor : labelColor,
		qtrue, qtrue, POS_HUD_CW, POS_HUD_CH, 0, ALIGN_STRETCH );
}


/*
===========================================================================
Jump Statistics

Two display modes controlled by bh_movement cvar:

  bh_movement 0 (RtCW default):
    Per-jump stats: height, distance, pre>post speed, max, air time, sync%

  bh_movement 1 (HL1 bhop):
    Bhop-focused: chain counter, speed delta per bounce, sync%,
    total distance, peak speed - every bounce updates instantly.

Both modes: stats refresh on EVERY landing (no waiting for chain end).

Bounce detection: uses BOTH groundEntityNum AND Z velocity reversal
so that autobhop bounces (land+jump within one server frame) are caught.
===========================================================================
*/

#define JUMP_DISPLAY_TIME   3500    /* ms to show last jump stats */
#define JUMP_FADE_TIME      700     /* ms fade-out at end */
#define JUMP_HUD_CW         5
#define JUMP_HUD_CH         8
#define JUMP_HUD_LINE       (JUMP_HUD_CH + 2)
#define JUMP_HUD_PAD_X      7
#define JUMP_HUD_PAD_Y      5
#define JUMP_HUD_GAP        7       /* gap above keystrokes/movebar */
#define JUMP_HUD_PANEL_W    154

#define MOVEBAR_W           JUMP_HUD_PANEL_W
#define MOVEBAR_H           6
#define TURNBAR_H           5
#define MOVEBAR_GAP         3
#define MOVEBAR_GAIN_THRESH 3.0f
#define STRAFEGUIDE_STACK_H 30      /* text + turn bar above movement bar */

#define BHOP_WINDOW         280     /* ms grace period to chain jumps */
#define BOUNCE_VEL_DOWN     -40.0f  /* Z vel threshold: was falling */
#define BOUNCE_VEL_UP       180.0f  /* Z vel threshold: now rising (jump ~270) */

static int CG_MovementOverlayTopY( void ) {
	int keysBaseY = 452 - 10 - ( 18 + 2 ) * 3;
	int barY = keysBaseY - MOVEBAR_GAP - MOVEBAR_H;
	if ( cg_strafeGuide.integer ) {
		return barY - STRAFEGUIDE_STACK_H;
	}
	return barY - 1;
}

/* helper: centered text */
static void JumpHUD_CenterText( int y, const char *text, vec4_t color ) {
	int w = strlen( text ) * JUMP_HUD_CW;
	int x = ( SCREEN_WIDTH - w ) / 2;
	CG_DrawStringExt( x, y, text, color, qtrue, qtrue,
		JUMP_HUD_CW, JUMP_HUD_CH, 0, ALIGN_CENTER );
}

/* helper: left + right aligned pair on same line within panel */
static void JumpHUD_LabelValue( int panelX, int y, const char *label,
								const char *value, vec4_t lc, vec4_t vc ) {
	CG_DrawStringExt( panelX + JUMP_HUD_PAD_X, y, label, lc, qtrue, qtrue,
		JUMP_HUD_CW, JUMP_HUD_CH, 0, ALIGN_CENTER );
	{
		int vw = strlen( value ) * JUMP_HUD_CW;
		int vx = panelX + JUMP_HUD_PANEL_W - JUMP_HUD_PAD_X - vw;
		CG_DrawStringExt( vx, y, value, vc, qtrue, qtrue,
			JUMP_HUD_CW, JUMP_HUD_CH, 0, ALIGN_CENTER );
	}
}

static void JumpHUD_DrawMiniBar( int x, int y, int w, int h, float frac, vec4_t bg, vec4_t fill ) {
	if ( frac < 0.0f ) frac = 0.0f;
	if ( frac > 1.0f ) frac = 1.0f;
	CG_FillRect( x, y, w, h, bg, ALIGN_CENTER );
	CG_FillRect( x, y, w * frac, h, fill, ALIGN_CENTER );
}

/*
==================
CG_DrawJumpStats

Draw per-bounce stats centered above keystroke overlay + movement bar.
==================
*/
void CG_DrawJumpStats( void ) {
	char buf[64];
	int elapsed, numLines;
	float alpha, syncPct, speedGain;
	int keysBaseY, panelH, panelX, panelTop, curY;
	vec4_t bg, bg2, title, label, value, good, warn, bad, accent, dim;

	if ( !cg_drawJumpStats.integer ) {
		return;
	}
	if ( cg.lastJumpTime == 0 ) {
		return;
	}

	elapsed = cg.time - cg.lastJumpTime;
	if ( elapsed > JUMP_DISPLAY_TIME ) {
		return;
	}

	/* fade */
	if ( elapsed > JUMP_DISPLAY_TIME - JUMP_FADE_TIME ) {
		alpha = 1.0f - (float)( elapsed - ( JUMP_DISPLAY_TIME - JUMP_FADE_TIME ) ) / JUMP_FADE_TIME;
	} else {
		alpha = 1.0f;
	}

	/* layout: automatically moves up when the strafe guide stack is visible */
	keysBaseY = CG_MovementOverlayTopY();

	if ( bh_movement.integer ) {
		numLines = 6;  /* HL1 mode: title, spd, gain, sync, chain, dist */
	} else {
		numLines = 6;  /* RtCW mode: title, h/d, spd, max/air, sync, bhop */
	}

	panelH   = JUMP_HUD_PAD_Y * 2 + numLines * JUMP_HUD_LINE + 5;
	panelX   = ( SCREEN_WIDTH - JUMP_HUD_PANEL_W ) / 2;
	panelTop = keysBaseY - JUMP_HUD_GAP - panelH;
	curY     = panelTop + JUMP_HUD_PAD_Y;

	/* colors (all alpha-adjusted) */
	bg[0] = 0.02f;  bg[1] = 0.025f; bg[2] = 0.02f;  bg[3] = 0.68f * alpha;
	bg2[0] = 0.10f; bg2[1] = 0.16f;  bg2[2] = 0.08f;  bg2[3] = 0.35f * alpha;
	title[0] = 1.0f;  title[1] = 0.85f;  title[2] = 0.15f;  title[3] = alpha;
	label[0] = 0.6f;  label[1] = 0.7f;   label[2] = 0.8f;   label[3] = alpha * 0.85f;
	value[0] = 1.0f;  value[1] = 1.0f;   value[2] = 1.0f;   value[3] = alpha * 0.95f;
	good[0] = 0.2f;   good[1] = 1.0f;    good[2] = 0.2f;    good[3] = alpha * 0.95f;
	warn[0] = 1.0f;   warn[1] = 0.65f;    warn[2] = 0.15f;   warn[3] = alpha * 0.95f;
	bad[0] = 1.0f;    bad[1] = 0.25f;     bad[2] = 0.15f;    bad[3] = alpha * 0.95f;
	accent[0] = 1.0f;  accent[1] = 0.55f;  accent[2] = 0.0f;  accent[3] = alpha;
	dim[0] = 0.35f;   dim[1] = 0.38f;      dim[2] = 0.35f;    dim[3] = alpha * 0.42f;

	/* background */
	CG_FillRect( panelX, panelTop, JUMP_HUD_PANEL_W, panelH, bg, ALIGN_CENTER );
	CG_FillRect( panelX, panelTop, JUMP_HUD_PANEL_W, 13, bg2, ALIGN_CENTER );
	CG_FillRect( panelX, panelTop, 2, panelH, accent, ALIGN_CENTER );

	if ( bh_movement.integer ) {
		/* ================ HL1 BHOP MODE ================ */
		float spd = cg.lastJumpPostSpeed;
		float delta = cg.lastJumpPostSpeed - cg.lastJumpPreSpeed;

		/* line 1: title - jump count + bhop chain */
		if ( cg.bhopChain >= 2 ) {
			Com_sprintf( buf, sizeof( buf ), "BHOP x%d  #%d", cg.bhopChain, cg.lastJumpCount );
			JumpHUD_CenterText( curY, buf, accent );
		} else {
			Com_sprintf( buf, sizeof( buf ), "JUMP #%d", cg.lastJumpCount );
			JumpHUD_CenterText( curY, buf, title );
		}
		curY += JUMP_HUD_LINE;

		/* line 2: current speed */
		Com_sprintf( buf, sizeof( buf ), "%.0f", spd );
		JumpHUD_LabelValue( panelX, curY, "SPD", buf, label, value );
		curY += JUMP_HUD_LINE;

		/* line 3: speed delta this bounce */
		if ( delta >= 0 ) {
			Com_sprintf( buf, sizeof( buf ), "+%.0f", delta );
		} else {
			Com_sprintf( buf, sizeof( buf ), "%.0f", delta );
		}
		JumpHUD_LabelValue( panelX, curY, "GAIN", buf, label, delta >= 0 ? good : bad );
		curY += JUMP_HUD_LINE;

		/* line 4: strafe sync % */
		syncPct = cg.lastJumpAirFrames > 0 ?
			( (float)cg.lastJumpSyncFrames / cg.lastJumpAirFrames ) * 100.0f : 0.0f;
		Com_sprintf( buf, sizeof( buf ), "%.0f%%", syncPct );
		JumpHUD_LabelValue( panelX, curY, "SYNC", buf, label,
			syncPct >= 75.0f ? good : ( syncPct >= 45.0f ? warn : bad ) );
		curY += JUMP_HUD_LINE;
		JumpHUD_DrawMiniBar( panelX + JUMP_HUD_PAD_X, curY - 2, JUMP_HUD_PANEL_W - JUMP_HUD_PAD_X * 2, 3,
			syncPct / 100.0f, dim, syncPct >= 75.0f ? good : ( syncPct >= 45.0f ? warn : bad ) );
		curY += 4;

		/* line 5: distance this bounce */
		Com_sprintf( buf, sizeof( buf ), "%.0fu", cg.lastJumpDist );
		JumpHUD_LabelValue( panelX, curY, "DIST", buf, label, value );
		curY += JUMP_HUD_LINE;

		/* line 6: chain totals or max speed */
		if ( cg.bhopChain >= 2 ) {
			float chainGain = cg.lastJumpPostSpeed - cg.bhopChainStartSpd;
			if ( chainGain >= 0 ) {
				Com_sprintf( buf, sizeof( buf ), "+%.0f", chainGain );
			} else {
				Com_sprintf( buf, sizeof( buf ), "%.0f", chainGain );
			}
			JumpHUD_LabelValue( panelX, curY, "TOTAL", buf, label,
				chainGain >= 0 ? good : bad );
		} else {
			Com_sprintf( buf, sizeof( buf ), "%.0f", cg.lastJumpMaxSpeed );
			JumpHUD_LabelValue( panelX, curY, "MAX", buf, label, value );
		}
	} else {
		/* ================ RtCW DEFAULT MODE ================ */

		/* line 1: title */
		if ( cg.bhopChain >= 2 ) {
			Com_sprintf( buf, sizeof( buf ), "JUMP #%d [x%d]", cg.lastJumpCount, cg.bhopChain );
			JumpHUD_CenterText( curY, buf, accent );
		} else {
			Com_sprintf( buf, sizeof( buf ), "--- JUMP #%d ---", cg.lastJumpCount );
			JumpHUD_CenterText( curY, buf, title );
		}
		curY += JUMP_HUD_LINE;

		/* line 2: height + distance */
		Com_sprintf( buf, sizeof( buf ), "H:%.0f  D:%.0f",
			cg.lastJumpHeight, cg.lastJumpDist );
		JumpHUD_CenterText( curY, buf, value );
		curY += JUMP_HUD_LINE;

		/* line 3: pre > post speed */
		speedGain = cg.lastJumpPostSpeed - cg.lastJumpPreSpeed;
		if ( speedGain >= 0 ) {
			Com_sprintf( buf, sizeof( buf ), "%.0f>%.0f +%.0f",
				cg.lastJumpPreSpeed, cg.lastJumpPostSpeed, speedGain );
		} else {
			Com_sprintf( buf, sizeof( buf ), "%.0f>%.0f %.0f",
				cg.lastJumpPreSpeed, cg.lastJumpPostSpeed, speedGain );
		}
		JumpHUD_CenterText( curY, buf, speedGain >= 0 ? good : bad );
		curY += JUMP_HUD_LINE;

		/* line 4: max speed + air time */
		Com_sprintf( buf, sizeof( buf ), "MAX:%.0f  AIR:%dms",
			cg.lastJumpMaxSpeed, cg.lastJumpAirTime );
		JumpHUD_CenterText( curY, buf, label );
		curY += JUMP_HUD_LINE;

		/* line 5: strafe sync */
		syncPct = cg.lastJumpAirFrames > 0 ?
			( (float)cg.lastJumpSyncFrames / cg.lastJumpAirFrames ) * 100.0f : 0.0f;
		Com_sprintf( buf, sizeof( buf ), "SYNC:%.0f%%  STR:%d",
			syncPct, cg.lastJumpStrafeCount );
		JumpHUD_CenterText( curY, buf,
			syncPct >= 75.0f ? good : ( syncPct >= 45.0f ? warn : bad ) );
		curY += JUMP_HUD_LINE;
		JumpHUD_DrawMiniBar( panelX + JUMP_HUD_PAD_X, curY - 2, JUMP_HUD_PANEL_W - JUMP_HUD_PAD_X * 2, 3,
			syncPct / 100.0f, dim, syncPct >= 75.0f ? good : ( syncPct >= 45.0f ? warn : bad ) );
		curY += 4;

		/* line 6: bhop chain info / strafes */
		if ( cg.bhopChain >= 2 ) {
			float chainGain = cg.lastJumpPostSpeed - cg.bhopChainStartSpd;
			Com_sprintf( buf, sizeof( buf ), "BH:%.0fu %+.0f",
				cg.bhopChainDist, chainGain );
			JumpHUD_CenterText( curY, buf, chainGain >= 0 ? good : accent );
		} else {
			Com_sprintf( buf, sizeof( buf ), "STR:%d  H:%.0f",
				cg.lastJumpStrafeCount, cg.lastJumpHeight );
			JumpHUD_CenterText( curY, buf, label );
		}
	}
}


/*
==================
Jump_RecordLanding

Helper: record landing stats from current jump state.
Called both from normal groundEntityNum detection and from
Z velocity bounce detection.
==================
*/
static void Jump_RecordLanding( float xySpeed ) {
	vec3_t delta;

	cg.jumpActive = qfalse;

	delta[0] = cg.predictedPlayerState.origin[0] - cg.jumpStartPos[0];
	delta[1] = cg.predictedPlayerState.origin[1] - cg.jumpStartPos[1];
	delta[2] = 0;

	cg.lastJumpDist      = VectorLength( delta );
	cg.lastJumpHeight    = cg.jumpMaxHeight - cg.jumpStartZ;
	cg.lastJumpPreSpeed  = cg.jumpStartSpeed;
	cg.lastJumpMaxSpeed  = cg.jumpMaxSpeed;
	cg.lastJumpPostSpeed = xySpeed;
	cg.lastJumpTime      = cg.time;    /* resets display timer every bounce */
	cg.lastJumpAirTime   = cg.time - cg.jumpStartTime;

	cg.lastJumpSyncFrames   = cg.jumpSyncFrames;
	cg.lastJumpAirFrames    = cg.jumpAirFrames;
	cg.lastJumpStrafeCount  = cg.jumpStrafeCount;
	cg.lastJumpCount++;

	/* accumulate into bhop chain */
	cg.bhopChainDist       += cg.lastJumpDist;
	cg.bhopChainSyncFrames += cg.jumpSyncFrames;
	cg.bhopChainAirFrames  += cg.jumpAirFrames;
	cg.bhopChainStrafes    += cg.jumpStrafeCount;
	cg.bhopLandTime         = cg.time;
}

/*
==================
Jump_BeginTakeoff

Helper: start tracking a new jump.
==================
*/
static void Jump_BeginTakeoff( float xySpeed ) {
	qboolean isBhop = qfalse;

	cg.jumpActive = qtrue;
	VectorCopy( cg.predictedPlayerState.origin, cg.jumpStartPos );
	cg.jumpStartSpeed = xySpeed;
	cg.jumpMaxSpeed = xySpeed;
	cg.jumpStartZ = cg.predictedPlayerState.origin[2];
	cg.jumpMaxHeight = cg.predictedPlayerState.origin[2];
	cg.jumpStartTime = cg.time;
	cg.jumpPrevYaw = cg.predictedPlayerState.viewangles[YAW];
	cg.jumpAirFrames = 0;
	cg.jumpSyncFrames = 0;
	cg.jumpStrafeCount = 0;
	cg.jumpPrevStrafe = 0;

	/* check bhop chain continuity */
	if ( cg.bhopLandTime > 0 &&
		 ( cg.time - cg.bhopLandTime ) <= BHOP_WINDOW ) {
		isBhop = qtrue;
	}

	if ( isBhop ) {
		cg.bhopChain++;
		if ( xySpeed > cg.bhopChainMaxSpd ) {
			cg.bhopChainMaxSpd = xySpeed;
		}
	} else {
		cg.bhopChain = 1;
		cg.bhopChainDist = 0;
		cg.bhopChainStartSpd = xySpeed;
		cg.bhopChainMaxSpd = xySpeed;
		cg.bhopChainSyncFrames = 0;
		cg.bhopChainAirFrames = 0;
		cg.bhopChainStrafes = 0;
	}
}

static int strafeGuideDir;
static int strafeGuideInput;
static int strafeGuideGood;
static float strafeGuideYawDelta;
static float strafeGuideAimOffset;
static float strafeGuideTurnRate;
static float strafeGuideIdealTurnRate;
static qboolean strafeGuideAimValid;
static qboolean strafeGuideSmoothValid;
static float strafeGuidePrevYaw;
static qboolean strafeGuidePrevYawValid;

static void CG_UpdateStrafeGuide( const usercmd_t *cmd, float xySpeed ) {
	float curYaw, yawDelta, absYaw, yawRate, idealRate, turnError, smoothStep, rawOffset;
	int inputDir = 0;

	if ( !cg_strafeGuide.integer ) {
		strafeGuideDir = 0;
		strafeGuideInput = 0;
		strafeGuideGood = 0;
		strafeGuideYawDelta = 0.0f;
		strafeGuideAimOffset = 0.0f;
		strafeGuideTurnRate = 0.0f;
		strafeGuideIdealTurnRate = 0.0f;
		strafeGuideAimValid = qfalse;
		strafeGuideSmoothValid = qfalse;
		strafeGuidePrevYawValid = qfalse;
		return;
	}

	curYaw = cg.predictedPlayerState.viewangles[YAW];
	if ( !strafeGuidePrevYawValid ) {
		strafeGuidePrevYaw = curYaw;
		strafeGuidePrevYawValid = qtrue;
	}
	yawDelta = curYaw - strafeGuidePrevYaw;
	while ( yawDelta > 180.0f ) yawDelta -= 360.0f;
	while ( yawDelta < -180.0f ) yawDelta += 360.0f;
	strafeGuidePrevYaw = curYaw;

	if ( cmd->rightmove > 0 ) inputDir = 1;
	else if ( cmd->rightmove < 0 ) inputDir = -1;

	/* Matches existing sync logic: turning right wants A, turning left wants D. */
	if ( yawDelta > 0.12f ) strafeGuideDir = -1;
	else if ( yawDelta < -0.12f ) strafeGuideDir = 1;
	else if ( inputDir ) strafeGuideDir = inputDir;
	else strafeGuideDir = 0;

	strafeGuideInput = inputDir;
	strafeGuideYawDelta = yawDelta;
	strafeGuideGood = ( inputDir != 0 && inputDir == strafeGuideDir && xySpeed > 80.0f ) ? 1 : 0;

	absYaw = fabs( yawDelta );
	yawRate = absYaw * 1000.0f / (float)( cg.frametime > 0 ? cg.frametime : 1 );

	/* Stable degrees/second target; higher speed wants a smaller turn rate. */
	idealRate = ( 480.0f / ( xySpeed + 160.0f ) ) * 125.0f;
	if ( idealRate < 35.0f ) idealRate = 35.0f;
	if ( idealRate > 260.0f ) idealRate = 260.0f;

	smoothStep = (float)cg.frametime / 180.0f;
	if ( smoothStep < 0.08f ) smoothStep = 0.08f;
	if ( smoothStep > 0.35f ) smoothStep = 0.35f;

	if ( xySpeed > 80.0f && strafeGuideDir != 0 ) {
		turnError = ( yawRate - idealRate ) / idealRate;
		if ( turnError < -1.0f ) turnError = -1.0f;
		if ( turnError >  1.0f ) turnError =  1.0f;
		rawOffset = turnError;
		strafeGuideAimValid = qtrue;
	} else {
		rawOffset = 0.0f;
		strafeGuideAimValid = qfalse;
	}

	if ( !strafeGuideSmoothValid ) {
		strafeGuideAimOffset = rawOffset;
		strafeGuideTurnRate = yawRate;
		strafeGuideIdealTurnRate = idealRate;
		strafeGuideSmoothValid = qtrue;
	} else {
		strafeGuideAimOffset += ( rawOffset - strafeGuideAimOffset ) * smoothStep;
		strafeGuideTurnRate += ( yawRate - strafeGuideTurnRate ) * smoothStep;
		strafeGuideIdealTurnRate += ( idealRate - strafeGuideIdealTurnRate ) * smoothStep;
	}
}


/*
==================
CG_UpdateJumpStats

Called every frame to track jump state transitions.

Uses predictedPlayerState for render-framerate resolution.
Two detection methods:
  1. groundEntityNum transitions (standard)
  2. Z velocity sign reversal (catches autobhop where
     land+jump happens within one server frame)

Every landing refreshes the stats display immediately.
==================
*/
void CG_UpdateJumpStats( void ) {
	float xySpeed, curVelZ;
	vec3_t hvel;
	usercmd_t cmd;
	int cmdNum;

	if ( !cg_drawJumpStats.integer && !cg_strafeGuide.integer ) {
		return;
	}

	/* calculate current XY speed from predicted state */
	hvel[0] = cg.predictedPlayerState.velocity[0];
	hvel[1] = cg.predictedPlayerState.velocity[1];
	hvel[2] = 0;
	xySpeed = VectorLength( hvel );

	curVelZ = cg.predictedPlayerState.velocity[2];

	/* get current input */
	cmdNum = trap_GetCurrentCmdNumber();
	trap_GetUserCmd( cmdNum, &cmd );
	CG_UpdateStrafeGuide( &cmd, xySpeed );
	if ( !cg_drawJumpStats.integer ) {
		return;
	}

	/* --- On ground: check bhop chain expiry --- */
	if ( !cg.jumpActive && cg.bhopChain > 0 &&
		 cg.predictedPlayerState.groundEntityNum != ENTITYNUM_NONE ) {
		if ( cg.time - cg.bhopLandTime > BHOP_WINDOW ) {
			/* chain ended */
			if ( cg.bhopChain >= 2 ) {
				cg.lastBhopChain     = cg.bhopChain;
				cg.lastBhopDist      = cg.bhopChainDist;
				cg.lastBhopStartSpd  = cg.bhopChainStartSpd;
				cg.lastBhopEndSpd    = cg.lastJumpPostSpeed;
				cg.lastBhopMaxSpd    = cg.bhopChainMaxSpd;
				cg.lastBhopStrafes   = cg.bhopChainStrafes;
				cg.lastBhopTime      = cg.time;
				if ( cg.bhopChainAirFrames > 0 ) {
					cg.lastBhopSyncPct = ( (float)cg.bhopChainSyncFrames / cg.bhopChainAirFrames ) * 100.0f;
				} else {
					cg.lastBhopSyncPct = 0.0f;
				}
			}
			cg.bhopChain = 0;
		}
	}

	if ( !cg.jumpActive ) {
		/* On ground - check if we just left it */
		if ( cg.predictedPlayerState.groundEntityNum == ENTITYNUM_NONE ) {
			/* TAKEOFF */
			Jump_BeginTakeoff( xySpeed );
		}
	} else {
		/* In air - update peak stats */
		if ( xySpeed > cg.jumpMaxSpeed ) {
			cg.jumpMaxSpeed = xySpeed;
		}
		if ( cg.predictedPlayerState.origin[2] > cg.jumpMaxHeight ) {
			cg.jumpMaxHeight = cg.predictedPlayerState.origin[2];
		}
		if ( xySpeed > cg.bhopChainMaxSpd ) {
			cg.bhopChainMaxSpd = xySpeed;
		}

		/* Strafe sync tracking */
		{
			float curYaw = cg.predictedPlayerState.viewangles[YAW];
			float yawDelta = curYaw - cg.jumpPrevYaw;
			int strafeDir = 0;

			while ( yawDelta > 180.0f ) yawDelta -= 360.0f;
			while ( yawDelta < -180.0f ) yawDelta += 360.0f;

			if ( cmd.rightmove > 0 ) strafeDir = 1;
			else if ( cmd.rightmove < 0 ) strafeDir = -1;

			cg.jumpAirFrames++;

			if ( strafeDir != 0 &&
				( ( strafeDir > 0 && yawDelta < -0.1f ) ||
				  ( strafeDir < 0 && yawDelta > 0.1f ) ) ) {
				cg.jumpSyncFrames++;
			}

			if ( strafeDir != 0 && strafeDir != cg.jumpPrevStrafe ) {
				cg.jumpStrafeCount++;
			}
			if ( strafeDir != 0 ) {
				cg.jumpPrevStrafe = strafeDir;
			}

			cg.jumpPrevYaw = curYaw;
		}

		/*
		 * Landing detection - two methods:
		 *
		 * Method 1: groundEntityNum changed to on-ground
		 *   Standard detection, works for normal jumps.
		 *
		 * Method 2: Z velocity reversal (autobhop bounce)
		 *   Previous frame Z was significantly negative (falling),
		 *   current frame Z is significantly positive (jumped).
		 *   This means a land+jump happened between frames.
		 *   Record landing then immediately start a new jump.
		 */

		if ( cg.predictedPlayerState.groundEntityNum != ENTITYNUM_NONE ) {
			/* Method 1: standard landing */
			Jump_RecordLanding( xySpeed );
		}
		else if ( cg.jumpPrevVelZ < BOUNCE_VEL_DOWN && curVelZ > BOUNCE_VEL_UP ) {
			/* Method 2: Z velocity bounce - autobhop
			   Record landing, then immediately begin new jump */
			Jump_RecordLanding( xySpeed );
			Jump_BeginTakeoff( xySpeed );
		}
	}

	/* Track Z velocity for next frame's bounce detection */
	cg.jumpPrevVelZ = curVelZ;
}


/*
===========================================================================
Movement Quality Bar
===========================================================================
*/

void CG_UpdateMovementBar( void ) {
	float xySpeed, delta, quality;
	vec3_t hvel;

	if ( !cg_drawJumpStats.integer && !cg_strafeGuide.integer ) {
		return;
	}

	hvel[0] = cg.predictedPlayerState.velocity[0];
	hvel[1] = cg.predictedPlayerState.velocity[1];
	hvel[2] = 0;
	xySpeed = VectorLength( hvel );

	if ( cg.time < cg.moveBarLastTime || cg.time - cg.moveBarLastTime > 1000 ) {
		cg.moveBarCount = 0;
		cg.moveBarHead = 0;
		cg.moveBarLastTime = 0;
	}

	if ( cg.moveBarLastTime == 0 ) {
		cg.moveBarPrevSpeed = xySpeed;
		cg.moveBarLastTime  = cg.time;
		return;
	}

	if ( cg.time - cg.moveBarLastTime >= MOVEBAR_INTERVAL ) {
		delta = xySpeed - cg.moveBarPrevSpeed;

		quality = delta / 30.0f;
		if ( quality >  1.0f ) quality =  1.0f;
		if ( quality < -1.0f ) quality = -1.0f;

		cg.moveBarQuality[cg.moveBarHead] = quality;
		cg.moveBarHead = ( cg.moveBarHead + 1 ) % MOVEBAR_SEGMENTS;
		if ( cg.moveBarCount < MOVEBAR_SEGMENTS ) cg.moveBarCount++;

		cg.moveBarPrevSpeed = xySpeed;
		cg.moveBarLastTime  = cg.time;
	}
}

void CG_DrawMovementBar( void ) {
	int keysBaseY, barY, barX, guideY, turnBarY;
	int i, idx;
	float segW, segX, dotX;
	float q;
	vec4_t segColor, borderColor, emptyColor, guideBg, guideGood, guideBad, guideWarn, centerColor, dotColor;
	char guideBuf[64];

	if ( !cg_drawJumpStats.integer && !cg_strafeGuide.integer ) {
		return;
	}

	keysBaseY = 452 - 10 - ( 18 + 2 ) * 3;
	barY = keysBaseY - MOVEBAR_GAP - MOVEBAR_H;
	barX = ( SCREEN_WIDTH - MOVEBAR_W ) / 2;

	borderColor[0] = 0.0f; borderColor[1] = 0.0f; borderColor[2] = 0.0f; borderColor[3] = 0.4f;
	emptyColor[0] = 0.10f; emptyColor[1] = 0.11f; emptyColor[2] = 0.10f; emptyColor[3] = 0.55f;
	CG_FillRect( barX - 1, barY - 1, MOVEBAR_W + 2, MOVEBAR_H + 2, borderColor, ALIGN_CENTER );
	CG_FillRect( barX, barY, MOVEBAR_W, MOVEBAR_H, emptyColor, ALIGN_CENTER );

	segW = (float)MOVEBAR_W / (float)MOVEBAR_SEGMENTS;

	for ( i = 0; i < cg.moveBarCount; i++ ) {
		if ( cg.moveBarCount < MOVEBAR_SEGMENTS ) {
			idx = i;
		} else {
			idx = ( cg.moveBarHead + i ) % MOVEBAR_SEGMENTS;
		}

		q = cg.moveBarQuality[idx];

		if ( q > MOVEBAR_GAIN_THRESH / 30.0f ) {
			segColor[0] = 0.1f;  segColor[1] = 0.9f;  segColor[2] = 0.2f;
		} else if ( q < -MOVEBAR_GAIN_THRESH / 30.0f ) {
			segColor[0] = 0.9f;  segColor[1] = 0.15f;  segColor[2] = 0.1f;
		} else {
			segColor[0] = 1.0f;  segColor[1] = 0.6f;  segColor[2] = 0.1f;
		}
		segColor[3] = 0.85f;

		segX = (float)barX + (float)i * segW;
		CG_FillRect( segX + 0.5f, barY, segW - 1.0f, MOVEBAR_H, segColor, ALIGN_CENTER );
	}

	if ( cg_strafeGuide.integer ) {
		const char *want = strafeGuideDir < 0 ? "A" : ( strafeGuideDir > 0 ? "D" : "--" );
		const char *state = strafeGuideGood ? "OK" : ( strafeGuideInput ? "BAD" : "TURN" );
		guideY = barY - 28;
		turnBarY = barY - 13;
		Com_sprintf( guideBuf, sizeof( guideBuf ), "STRAFE %s  %s %.0f/%.0f", want, state, strafeGuideTurnRate, strafeGuideIdealTurnRate );
		guideBg[0] = 0.02f; guideBg[1] = 0.025f; guideBg[2] = 0.02f; guideBg[3] = 0.58f;
		guideGood[0] = 0.20f; guideGood[1] = 1.00f; guideGood[2] = 0.25f; guideGood[3] = 0.95f;
		guideBad[0] = 1.00f; guideBad[1] = 0.25f; guideBad[2] = 0.18f; guideBad[3] = 0.95f;
		guideWarn[0] = 1.00f; guideWarn[1] = 0.65f; guideWarn[2] = 0.15f; guideWarn[3] = 0.95f;
		centerColor[0] = 0.92f; centerColor[1] = 0.92f; centerColor[2] = 0.92f; centerColor[3] = 0.80f;
		CG_FillRect( barX, guideY - 2, MOVEBAR_W, 12, guideBg, ALIGN_CENTER );
		CG_DrawStringExt( barX + 6, guideY, guideBuf,
			strafeGuideGood ? guideGood : ( strafeGuideInput ? guideBad : guideWarn ),
			qtrue, qtrue, 5, 8, 0, ALIGN_CENTER );

		CG_FillRect( barX - 1, turnBarY - 1, MOVEBAR_W + 2, TURNBAR_H + 2, borderColor, ALIGN_CENTER );
		CG_FillRect( barX, turnBarY, MOVEBAR_W, TURNBAR_H, guideBg, ALIGN_CENTER );
		CG_FillRect( barX + MOVEBAR_W / 2 - 1, turnBarY - 2, 2, TURNBAR_H + 4, centerColor, ALIGN_CENTER );
		dotX = (float)barX + ( ( strafeGuideAimOffset + 1.0f ) * 0.5f ) * (float)MOVEBAR_W;
		if ( dotX < (float)barX + 2.0f ) dotX = (float)barX + 2.0f;
		if ( dotX > (float)( barX + MOVEBAR_W - 3 ) ) dotX = (float)( barX + MOVEBAR_W - 3 );
		dotColor[0] = strafeGuideAimValid ? ( 0.25f + 0.75f * fabs( strafeGuideAimOffset ) ) : 1.0f;
		dotColor[1] = strafeGuideAimValid ? ( 1.00f - 0.75f * fabs( strafeGuideAimOffset ) ) : 0.65f;
		dotColor[2] = strafeGuideAimValid ? 0.12f : 0.15f;
		dotColor[3] = 0.95f;
		CG_FillRect( dotX - 2.0f, turnBarY - 3, 4, TURNBAR_H + 6, dotColor, ALIGN_CENTER );
	}
}
