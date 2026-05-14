/*
===========================================================================

Return to Castle Wolfenstein single player GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein single player GPL Source Code (RTCW SP Source Code).  

RTCW SP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW SP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW SP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW SP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW SP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// cg_view.c -- setup all the parameters (position, angle, etc)
// for a 3D rendering
#include "cg_local.h"

//========================
extern int notebookModel;
//========================

/* =====================================================================
   Ghost Replay - translucent player model from best-split recording
   ===================================================================== */

static qboolean  ghost_initialized = qfalse;
static vmCvar_t  ghost_visible;
static vmCvar_t  ghost_x, ghost_y, ghost_z;
static vmCvar_t  ghost_yaw;
static vmCvar_t  ghost_speed;
static vmCvar_t  ghost_crouch;
static vmCvar_t  ghost_player;
static vmCvar_t  ghost_opacity;
static vmCvar_t  ghost_color;
static vmCvar_t  race_active;
static vmCvar_t  race_nametag;
static vmCvar_t  race_nametag_stats;
static vmCvar_t  race_nametag_icons;
static vmCvar_t  race_nametag_scale;
static vmCvar_t  race_nametag_opacity;
static vmCvar_t  race_ghost_render;
#define CG_RACE_MAX_GHOSTS 8
static vmCvar_t  race_ghost_data[CG_RACE_MAX_GHOSTS];
static qhandle_t ghostShader;
static qhandle_t raceGhostTintShader;
static qhandle_t raceGhostXrayShader;

typedef struct {
	int packedColor;
	int alpha;
	int crouched;
	int health;
	int armor;
	int weapon;
	int ammo;
	int clip;
	int legsAnim;
	int torsoAnim;
	int movementDir;
	int eFlags;
	int groundEntityNum;
	int animMovetype;
	float x, y, z, yaw, speed, pitch;
	float vx, vy, vz;
	char nick[32];
} raceGhostInfo_t;

/* Animation state for smooth frame cycling */
typedef struct {
	animation_t *anim;    /* current animation pointer */
	int          startTime;
} ghostLerpFrame_t;

static ghostLerpFrame_t ghost_legs, ghost_torso;
static ghostLerpFrame_t race_legs[CG_RACE_MAX_GHOSTS], race_torso[CG_RACE_MAX_GHOSTS];
static centity_t        race_player_ghosts[CG_RACE_MAX_GHOSTS];
static qboolean         race_player_ghost_used[CG_RACE_MAX_GHOSTS];

#define CG_RACE_GHOST_PLAYER_EFLAGS ( EF_DEAD | EF_CROUCHING | EF_MG42_ACTIVE | EF_FIRING | EF_TALK | EF_CONNECTION | EF_HEADSHOT | EF_HEADLOOK | EF_STAND_IDLE2 | EF_NO_TURN_ANIM | EF_ZOOMING | EF_NOSWINGANGLES | EF_RECENTLY_FIRING )

static qboolean CG_ParseRaceGhost( const char *text, raceGhostInfo_t *out );
static qboolean CG_AddRaceGhostPlayerModel( const raceGhostInfo_t *info, int ghostSlot );
static void CG_AddRaceGhostModel( float x, float y, float z, float yaw, float speed,
								  byte r, byte g, byte b, int alpha, qboolean crouched, int weaponNum, int legsAnimNum, int torsoAnimNum,
								  ghostLerpFrame_t *legsLf, ghostLerpFrame_t *torsoLf );

static void CG_InitGhost( void ) {
	int i;
	char name[32];
	trap_Cvar_Register( &ghost_visible, "ls_ghost_visible", "0", 0 );
	trap_Cvar_Register( &ghost_x, "ls_ghost_x", "0", 0 );
	trap_Cvar_Register( &ghost_y, "ls_ghost_y", "0", 0 );
	trap_Cvar_Register( &ghost_z, "ls_ghost_z", "0", 0 );
	trap_Cvar_Register( &ghost_yaw, "ls_ghost_yaw", "0", 0 );
	trap_Cvar_Register( &ghost_speed, "ls_ghost_speed", "0", 0 );
	trap_Cvar_Register( &ghost_crouch, "ls_ghost_crouch", "0", 0 );
	trap_Cvar_Register( &ghost_player, "ls_ghost_player", "", 0 );
	trap_Cvar_Register( &ghost_opacity, "ls_ghost_opacity", "60", CVAR_ARCHIVE );
	trap_Cvar_Register( &ghost_color, "ls_ghost_color", "0", 0 );
	trap_Cvar_Register( &race_active, "ls_race_active", "0", 0 );
	trap_Cvar_Register( &race_nametag, "ls_race_nametag", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &race_nametag_stats, "ls_race_nametag_stats", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &race_nametag_icons, "ls_race_nametag_icons", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( &race_nametag_scale, "ls_race_nametag_scale", "1.0", CVAR_ARCHIVE );
	trap_Cvar_Register( &race_nametag_opacity, "ls_race_nametag_opacity", "1.0", CVAR_ARCHIVE );
	trap_Cvar_Register( &race_ghost_render, "ls_race_ghost_render", "0", CVAR_ARCHIVE );
	for ( i = 0; i < CG_RACE_MAX_GHOSTS; i++ ) {
		Com_sprintf( name, sizeof( name ), "ls_race_ghost%d", i );
		trap_Cvar_Register( &race_ghost_data[i], name, "", 0 );
	}
	ghostShader = trap_R_RegisterShader( "ghostPlayer" );
	raceGhostTintShader = trap_R_RegisterShader( "speedrunWeaponTint" );
	raceGhostXrayShader = trap_R_RegisterShader( "speedrunWeaponXray" );
	memset( &ghost_legs, 0, sizeof( ghost_legs ) );
	memset( &ghost_torso, 0, sizeof( ghost_torso ) );
	memset( race_legs, 0, sizeof( race_legs ) );
	memset( race_torso, 0, sizeof( race_torso ) );
	ghost_initialized = qtrue;
}

/*
 CG_GhostFindAnim - find animation by name (case-insensitive).
 Returns pointer into ci->modelInfo->animations or NULL.
*/
static animation_t *CG_GhostFindAnim( clientInfo_t *ci, const char *name ) {
	int i;
	if ( !ci->modelInfo ) return NULL;
	for ( i = 0; i < ci->modelInfo->numAnimations; i++ ) {
		if ( !Q_stricmp( ci->modelInfo->animations[i].name, name ) ) {
			return &ci->modelInfo->animations[i];
		}
	}
	return NULL;
}

static animation_t *CG_GhostFindMoveTypeAnim( clientInfo_t *ci, int moveTypeMask ) {
	int i;
	if ( !ci->modelInfo ) return NULL;
	for ( i = 0; i < ci->modelInfo->numAnimations; i++ ) {
		if ( ci->modelInfo->animations[i].movetype & moveTypeMask ) {
			return &ci->modelInfo->animations[i];
		}
	}
	return NULL;
}

static animation_t *CG_GhostFindAnimIndex( clientInfo_t *ci, int index ) {
	index &= ~ANIM_TOGGLEBIT;
	if ( !ci || !ci->modelInfo ) return NULL;
	if ( index < 0 || index >= ci->modelInfo->numAnimations ) return NULL;
	return &ci->modelInfo->animations[index];
}

static animation_t *CG_GhostFindMoveAnim( clientInfo_t *ci, scriptAnimMoveTypes_t moveType ) {
	int index;
	if ( !ci ) return NULL;
	index = BG_GetAnimScriptAnimation( cg.clientNum, cg.snap ? cg.snap->ps.aiState : 0, moveType );
	if ( index < 0 ) return NULL;
	return CG_GhostFindAnimIndex( ci, index );
}

static animation_t *CG_GhostFindCrouchAnim( clientInfo_t *ci, qboolean moving ) {
	animation_t *anim;
	if ( moving ) {
		anim = CG_GhostFindMoveAnim( ci, ANIM_MT_WALKCR );
		if ( !anim ) anim = CG_GhostFindMoveAnim( ci, ANIM_MT_WALKCRBK );
		if ( !anim ) anim = CG_GhostFindAnimIndex( ci, LEGS_WALKCR );
		if ( !anim ) anim = CG_GhostFindAnimIndex( ci, LEGS_WALKCR_BACK );
		if ( !anim ) anim = CG_GhostFindMoveTypeAnim( ci, ( 1 << ANIM_MT_WALKCR ) | ( 1 << ANIM_MT_WALKCRBK ) );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "WALKCR" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "LEGS_WALKCR" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "walkcr" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "WALKCRBK" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "LEGS_WALKCRBK" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "LEGS_WALKCR_BACK" );
		if ( anim ) return anim;
	}
	anim = CG_GhostFindMoveAnim( ci, ANIM_MT_IDLECR );
	if ( !anim ) anim = CG_GhostFindAnimIndex( ci, LEGS_IDLECR );
	if ( !anim ) anim = CG_GhostFindMoveTypeAnim( ci, ( 1 << ANIM_MT_IDLECR ) | ( 1 << ANIM_MT_WALKCR ) | ( 1 << ANIM_MT_WALKCRBK ) );
	if ( !anim ) anim = CG_GhostFindAnim( ci, "IDLECR" );
	if ( !anim ) anim = CG_GhostFindAnim( ci, "LEGS_IDLECR" );
	if ( !anim ) anim = CG_GhostFindAnim( ci, "idlecr" );
	return anim;
}

static void CG_GhostApplyLegScale( clientInfo_t *ci, refEntity_t *legs, qboolean crouchFallback ) {
	float sx, sy, sz;
	if ( !ci || !legs ) return;
	sx = ci->playermodelScale[0] ? ci->playermodelScale[0] : 1.0f;
	sy = ci->playermodelScale[1] ? ci->playermodelScale[1] : 1.0f;
	sz = ci->playermodelScale[2] ? ci->playermodelScale[2] : 1.0f;
	if ( crouchFallback ) sz *= 0.72f;
	if ( sx != 1.0f || sy != 1.0f || sz != 1.0f ) {
		VectorScale( legs->axis[0], sx, legs->axis[0] );
		VectorScale( legs->axis[1], sy, legs->axis[1] );
		VectorScale( legs->axis[2], sz, legs->axis[2] );
		legs->nonNormalizedAxes = qtrue;
	}
}

/*
 CG_GhostRunLerp - compute frame / oldframe / backlerp for a ghostLerpFrame.
 Switches animation pointer when the desired anim changes.
*/
static void CG_GhostRunLerp( ghostLerpFrame_t *lf, animation_t *want,
							  int *outFrame, int *outOldFrame, float *outBacklerp ) {
	int f, elapsed, frameLerp;

	if ( !want ) {
		*outFrame = *outOldFrame = 0;
		*outBacklerp = 0.0f;
		return;
	}

	/* Animation change -> reset */
	if ( lf->anim != want ) {
		lf->anim      = want;
		lf->startTime = cg.time;
	}

	if ( want->numFrames <= 0 || !want->frameLerp ) {
		*outFrame = *outOldFrame = want->firstFrame;
		*outBacklerp = 0.0f;
		return;
	}

	frameLerp = want->frameLerp;
	elapsed   = cg.time - lf->startTime;
	if ( elapsed < 0 ) elapsed = 0;

	f = elapsed / frameLerp;

	if ( f >= want->numFrames ) {
		if ( want->loopFrames ) {
			f = ( f - want->numFrames ) % want->loopFrames
				+ ( want->numFrames - want->loopFrames );
		} else {
			f = want->numFrames - 1;
		}
	}

	*outFrame = want->firstFrame + f;

	{
		int prev = f - 1;
		if ( prev < 0 ) prev = 0;
		*outOldFrame = want->firstFrame + prev;
	}

	{
		int intra = elapsed % frameLerp;
		*outBacklerp = 1.0f - (float)intra / (float)frameLerp;
	}
}

static void CG_AddGhost( void ) {
	refEntity_t    legs, torso, head;
	clientInfo_t   *ci;
	vec3_t         ghostOrigin, lightOrigin;
	vec3_t         legsAngles, torsoAngles;
	float          yaw, speed;
	animation_t    *legsAnim, *torsoAnim;
	qboolean       crouchFallback = qfalse;
	int            legsFrame, legsOldFrame, torsoFrame, torsoOldFrame;
	float          legsBacklerp, torsoBacklerp;
	int            alpha;
	byte           gR, gG, gB; /* ghost tint color */
	raceGhostInfo_t richGhost;
	byte           r, g, b;

	if ( !ghost_initialized ) {
		CG_InitGhost();
	}

	trap_Cvar_Update( &race_active );
	if ( race_active.integer ) return;

	trap_Cvar_Update( &ghost_visible );
	if ( !ghost_visible.integer ) return;

	trap_Cvar_Update( &ghost_x );
	trap_Cvar_Update( &ghost_y );
	trap_Cvar_Update( &ghost_z );
	trap_Cvar_Update( &ghost_yaw );
	trap_Cvar_Update( &ghost_speed );
	trap_Cvar_Update( &ghost_crouch );
	trap_Cvar_Update( &ghost_player );
	trap_Cvar_Update( &ghost_opacity );
	trap_Cvar_Update( &ghost_color );
	trap_Cvar_Update( &race_ghost_render );

	alpha = ghost_opacity.integer;
	if ( alpha < 0 )   alpha = 0;
	if ( alpha > 255 ) alpha = 255;

	if ( CG_ParseRaceGhost( ghost_player.string, &richGhost ) ) {
		if ( CG_AddRaceGhostPlayerModel( &richGhost, 0 ) ) return;
		r = (byte)( ( richGhost.packedColor >> 16 ) & 255 );
		g = (byte)( ( richGhost.packedColor >> 8 ) & 255 );
		b = (byte)( richGhost.packedColor & 255 );
		CG_AddRaceGhostModel( richGhost.x, richGhost.y, richGhost.z, richGhost.yaw, richGhost.speed, r, g, b, richGhost.alpha, richGhost.crouched ? qtrue : qfalse, richGhost.weapon, richGhost.legsAnim, richGhost.torsoAnim, &ghost_legs, &ghost_torso );
		return;
	}

	/* Ghost tint: 0=blue (old ghost), 1=gold (new gold this run) */
	if ( ghost_color.integer == 1 ) {
		gR = 255; gG = 215; gB = 0;    /* gold / yellow */
	} else {
		gR = 80;  gG = 180; gB = 255;  /* blue (default) */
	}

	/* Use the local player's model */
	ci = &cgs.clientinfo[cg.clientNum];
	if ( !ci->legsModel || !ci->torsoModel || !ci->headModel ) return;
	if ( !ci->modelInfo ) return;

	ghostOrigin[0] = ghost_x.value;
	ghostOrigin[1] = ghost_y.value;
	ghostOrigin[2] = ghost_z.value;
	yaw   = ghost_yaw.value;
	speed = ghost_speed.value;

	VectorCopy( ghostOrigin, lightOrigin );
	lightOrigin[2] += 31.0f;

	/* Choose animation by name.
	   Try short names first (MDS models: "idle","run","walk")
	   then standard Q3 names ("legs_idle","legs_run","torso_stand"). */
	if ( ghost_crouch.integer ) {
		legsAnim = CG_GhostFindCrouchAnim( ci, speed > 20.0f ? qtrue : qfalse );
		if ( !legsAnim ) crouchFallback = qtrue;
	} else if ( speed > 20.0f ) {
		legsAnim = CG_GhostFindAnim( ci, "run" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "trot" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "legs_run" );
	} else {
		legsAnim = CG_GhostFindAnim( ci, "idle" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "legs_idle" );
	}
	/* Torso: try dedicated torso anims, otherwise reuse legs anim */
	if ( ghost_crouch.integer ) {
		torsoAnim = CG_GhostFindAnim( ci, "torso_crouch" );
	} else if ( speed > 20.0f ) {
		torsoAnim = CG_GhostFindAnim( ci, "torso_move" );
	} else {
		torsoAnim = CG_GhostFindAnim( ci, "torso_stand" );
	}
	if ( !torsoAnim ) torsoAnim = legsAnim;
	/* Last resort fallback */
	if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "idle" );
	if ( !legsAnim && ci->modelInfo->numAnimations > 0 ) {
		legsAnim = &ci->modelInfo->animations[0];
	}
	if ( !torsoAnim ) torsoAnim = legsAnim;

	/* Compute animated frame values */
	CG_GhostRunLerp( &ghost_legs, legsAnim,
					  &legsFrame, &legsOldFrame, &legsBacklerp );
	CG_GhostRunLerp( &ghost_torso, torsoAnim,
					  &torsoFrame, &torsoOldFrame, &torsoBacklerp );

	/* ---- LEGS ---- */
	memset( &legs, 0, sizeof( legs ) );
	memset( &torso, 0, sizeof( torso ) );
	memset( &head, 0, sizeof( head ) );

	legs.reType      = RT_MODEL;
	legs.hModel      = ci->legsModel;
	legs.customSkin  = ci->legsSkin;
	legs.customShader = ghostShader;
	legs.renderfx    = RF_NOSHADOW | RF_LIGHTING_ORIGIN;

	legs.shaderRGBA[0] = gR;
	legs.shaderRGBA[1] = gG;
	legs.shaderRGBA[2] = gB;
	legs.shaderRGBA[3] = alpha;

	VectorCopy( ghostOrigin, legs.origin );
	VectorCopy( legs.origin, legs.oldorigin );
	VectorCopy( lightOrigin, legs.lightingOrigin );

	VectorSet( legsAngles, 0, yaw, 0 );
	AnglesToAxis( legsAngles, legs.axis );
	CG_GhostApplyLegScale( ci, &legs, crouchFallback );

	legs.frame    = legsFrame;
	legs.oldframe = legsOldFrame;
	legs.backlerp = legsBacklerp;

	/* ---- TORSO ---- */
	torso.frame    = torsoFrame;
	torso.oldframe = torsoOldFrame;
	torso.backlerp = torsoBacklerp;

	if ( !ci->isSkeletal ) {
		/* MD3 path: separate legs + torso models */
		trap_R_AddRefEntityToScene( &legs );

		VectorSet( torsoAngles, 0, yaw, 0 );
		AnglesToAxis( torsoAngles, torso.axis );

		torso.reType      = RT_MODEL;
		torso.hModel      = ci->torsoModel;
		torso.customSkin  = ci->torsoSkin;
		torso.customShader = ghostShader;
		torso.renderfx    = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
		torso.shaderRGBA[0] = gR;
		torso.shaderRGBA[1] = gG;
		torso.shaderRGBA[2] = gB;
		torso.shaderRGBA[3] = alpha;
		VectorCopy( lightOrigin, torso.lightingOrigin );

		CG_PositionRotatedEntityOnTag( &torso, &legs, "tag_torso" );
		trap_R_AddRefEntityToScene( &torso );
	} else {
		/* Skeletal (MDS) path: single combined model.
		   torsoAxis must be identity - renderer applies transpose(torsoAxis)
		   to torso bones in model-space, then rotates all by legs.axis.
		   Identity means no extra torso twist. */
		legs.torsoFrame    = torsoFrame;
		legs.oldTorsoFrame = torsoOldFrame;
		legs.torsoBacklerp = torsoBacklerp;
		AxisClear( legs.torsoAxis );

		trap_R_AddRefEntityToScene( &legs );
		torso = legs;   /* head tag lookup needs the combined entity */
	}

	/* ---- HEAD ---- */
	head.reType      = RT_MODEL;
	head.hModel      = ci->headModel;
	head.customSkin  = ci->headSkin;
	head.customShader = ghostShader;
	head.renderfx    = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
	head.shaderRGBA[0] = gR;
	head.shaderRGBA[1] = gG;
	head.shaderRGBA[2] = gB;
	head.shaderRGBA[3] = alpha;
	VectorCopy( lightOrigin, head.lightingOrigin );
	AxisClear( head.axis );

	CG_PositionRotatedEntityOnTag( &head, &torso, "tag_head" );
	trap_R_AddRefEntityToScene( &head );
}

static int CG_RaceGhostRenderMode( void ) {
	int mode = race_ghost_render.integer;
	if ( mode < CG_RACE_GHOST_RENDER_TRANSLUCENT ) mode = CG_RACE_GHOST_RENDER_TRANSLUCENT;
	if ( mode > CG_RACE_GHOST_RENDER_XRAY ) mode = CG_RACE_GHOST_RENDER_XRAY;
	if ( mode == CG_RACE_GHOST_RENDER_TEXTURED ) mode = CG_RACE_GHOST_RENDER_PLAYER;
	return mode;
}

static int CG_RaceGhostEffectiveRenderMode( int renderMode, int alpha ) {
	if ( renderMode == CG_RACE_GHOST_RENDER_TEXTURED ) renderMode = CG_RACE_GHOST_RENDER_PLAYER;
	return renderMode;
}

static void CG_RaceGhostSetColor( refEntity_t *ent, byte r, byte g, byte b, int alpha ) {
	ent->shaderRGBA[0] = r;
	ent->shaderRGBA[1] = g;
	ent->shaderRGBA[2] = b;
	ent->shaderRGBA[3] = (byte)Com_Clamp( 0.0f, 255.0f, (float)alpha );
}

static void CG_RaceGhostPrepareEntity( refEntity_t *ent, int renderMode, byte r, byte g, byte b, int alpha ) {
	renderMode = CG_RaceGhostEffectiveRenderMode( renderMode, alpha );
	if ( renderMode == CG_RACE_GHOST_RENDER_TRANSLUCENT ) {
		ent->customShader = ghostShader;
		CG_RaceGhostSetColor( ent, r, g, b, alpha );
	} else if ( renderMode == CG_RACE_GHOST_RENDER_PLAYER && alpha < 255 ) {
		ent->customShader = 0;
		ent->renderfx |= RF_ENTITY_ALPHA;
		CG_RaceGhostSetColor( ent, 255, 255, 255, alpha );
	} else {
		ent->customShader = 0;
		ent->renderfx &= ~RF_ENTITY_ALPHA;
		CG_RaceGhostSetColor( ent, 255, 255, 255, 255 );
	}
}

static void CG_RaceGhostAddEntity( refEntity_t *ent, int renderMode, byte r, byte g, byte b, int alpha ) {
	refEntity_t overlay;
	refEntity_t glow;
	if ( alpha <= 0 ) return;
	renderMode = CG_RaceGhostEffectiveRenderMode( renderMode, alpha );
	if ( renderMode != CG_RACE_GHOST_RENDER_TRANSLUCENT ) {
		trap_R_AddRefEntityToScene( ent );
	}
	if ( renderMode != CG_RACE_GHOST_RENDER_TINTED && renderMode != CG_RACE_GHOST_RENDER_XRAY && renderMode != CG_RACE_GHOST_RENDER_TRANSLUCENT ) {
		return;
	}
	memcpy( &overlay, ent, sizeof( overlay ) );
	if ( renderMode == CG_RACE_GHOST_RENDER_TINTED ) {
		if ( !raceGhostTintShader ) return;
		overlay.customShader = raceGhostTintShader;
	} else if ( renderMode == CG_RACE_GHOST_RENDER_XRAY ) {
		if ( !raceGhostXrayShader ) return;
		overlay.customShader = ghostShader ? ghostShader : raceGhostXrayShader;
	} else {
		if ( !ghostShader ) return;
		overlay.customShader = ghostShader;
	}
	CG_RaceGhostSetColor( &overlay, r, g, b, alpha );
	trap_R_AddRefEntityToScene( &overlay );
	if ( renderMode == CG_RACE_GHOST_RENDER_XRAY ) {
		memcpy( &glow, ent, sizeof( glow ) );
		glow.customShader = raceGhostXrayShader;
		glow.renderfx |= RF_DEPTHHACK | RF_MINLIGHT;
		VectorScale( glow.axis[0], 1.025f, glow.axis[0] );
		VectorScale( glow.axis[1], 1.025f, glow.axis[1] );
		VectorScale( glow.axis[2], 1.025f, glow.axis[2] );
		glow.nonNormalizedAxes = qtrue;
		CG_RaceGhostSetColor( &glow, r, g, b, (int)Com_Clamp( 0.0f, 255.0f, (float)alpha * 1.25f ) );
		trap_R_AddRefEntityToScene( &glow );
	}
}

static void CG_AddRaceGhostWeapon( clientInfo_t *ci, const refEntity_t *torso, int weaponNum, byte r, byte g, byte b, int alpha, int renderMode ) {
	refEntity_t gun;
	weaponInfo_t *weapon;
	qhandle_t model;
	int i;

	if ( !ci || !torso ) return;
	if ( weaponNum <= WP_NONE || weaponNum >= WP_NUM_WEAPONS || weaponNum == WP_GAUNTLET ) return;
	CG_RegisterWeapon( weaponNum );
	weapon = &cg_weapons[weaponNum];
	model = ( ci->isSkeletal && weapon->weaponModel[W_SKTP_MODEL] ) ? weapon->weaponModel[W_SKTP_MODEL] : weapon->weaponModel[W_TP_MODEL];
	if ( !model ) return;

	memset( &gun, 0, sizeof( gun ) );
	gun.reType = RT_MODEL;
	gun.hModel = model;
	gun.renderfx = torso->renderfx;
	gun.shadowPlane = torso->shadowPlane;
	VectorCopy( torso->lightingOrigin, gun.lightingOrigin );
	CG_PositionEntityOnTag( &gun, torso, "tag_weapon", 0, NULL );
	if ( ci->playermodelScale[0] != 0 ) {
		for ( i = 0; i < 3; i++ ) {
			VectorScale( gun.axis[i], 1.0f / ci->playermodelScale[i], gun.axis[i] );
		}
	}
	CG_RaceGhostPrepareEntity( &gun, renderMode, r, g, b, alpha );
	CG_RaceGhostAddEntity( &gun, renderMode, r, g, b, alpha );
}

static void CG_AddRaceGhostModel( float x, float y, float z, float yaw, float speed,
								  byte r, byte g, byte b, int alpha, qboolean crouched, int weaponNum, int legsAnimNum, int torsoAnimNum,
								  ghostLerpFrame_t *legsLf, ghostLerpFrame_t *torsoLf ) {
	refEntity_t legs, torso, head;
	clientInfo_t *ci;
	vec3_t origin, lightOrigin, legsAngles, torsoAngles;
	animation_t *legsAnim, *torsoAnim;
	qboolean crouchFallback = qfalse;
	int legsFrame, legsOldFrame, torsoFrame, torsoOldFrame;
	int renderMode;
	float legsBacklerp, torsoBacklerp;

	if ( alpha < 0 ) alpha = 0;
	if ( alpha > 255 ) alpha = 255;
	renderMode = CG_RaceGhostRenderMode();

	ci = &cgs.clientinfo[cg.clientNum];
	if ( !ci->legsModel || !ci->torsoModel || !ci->headModel ) return;
	if ( !ci->modelInfo ) return;

	VectorSet( origin, x, y, z );
	VectorCopy( origin, lightOrigin );
	lightOrigin[2] += 31.0f;

	legsAnim = CG_GhostFindAnimIndex( ci, legsAnimNum );
	if ( crouched && ( !legsAnim || !( legsAnim->movetype & ( ( 1 << ANIM_MT_IDLECR ) | ( 1 << ANIM_MT_WALKCR ) | ( 1 << ANIM_MT_WALKCRBK ) ) ) ) ) {
		legsAnim = CG_GhostFindCrouchAnim( ci, speed > 20.0f ? qtrue : qfalse );
		if ( !legsAnim ) crouchFallback = qtrue;
	} else if ( !legsAnim && speed > 20.0f ) {
		legsAnim = CG_GhostFindAnim( ci, "run" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "trot" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "legs_run" );
	} else if ( !legsAnim ) {
		legsAnim = CG_GhostFindAnim( ci, "idle" );
		if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "legs_idle" );
	}
	torsoAnim = CG_GhostFindAnimIndex( ci, torsoAnimNum );
	if ( !torsoAnim && crouched ) torsoAnim = CG_GhostFindAnim( ci, "torso_crouch" );
	else if ( !torsoAnim && speed > 20.0f ) torsoAnim = CG_GhostFindAnim( ci, "torso_move" );
	else if ( !torsoAnim ) torsoAnim = CG_GhostFindAnim( ci, "torso_stand" );
	if ( !torsoAnim ) torsoAnim = legsAnim;
	if ( !legsAnim ) legsAnim = CG_GhostFindAnim( ci, "idle" );
	if ( !legsAnim && ci->modelInfo->numAnimations > 0 ) legsAnim = &ci->modelInfo->animations[0];
	if ( !torsoAnim ) torsoAnim = legsAnim;

	CG_GhostRunLerp( legsLf, legsAnim, &legsFrame, &legsOldFrame, &legsBacklerp );
	CG_GhostRunLerp( torsoLf, torsoAnim, &torsoFrame, &torsoOldFrame, &torsoBacklerp );

	memset( &legs, 0, sizeof( legs ) );
	memset( &torso, 0, sizeof( torso ) );
	memset( &head, 0, sizeof( head ) );

	legs.reType = RT_MODEL;
	legs.hModel = ci->legsModel;
	legs.customSkin = ci->legsSkin;
	legs.renderfx = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
	CG_RaceGhostPrepareEntity( &legs, renderMode, r, g, b, alpha );
	VectorCopy( origin, legs.origin );
	VectorCopy( legs.origin, legs.oldorigin );
	VectorCopy( lightOrigin, legs.lightingOrigin );
	VectorSet( legsAngles, 0, yaw, 0 );
	AnglesToAxis( legsAngles, legs.axis );
	CG_GhostApplyLegScale( ci, &legs, crouchFallback );
	legs.frame = legsFrame;
	legs.oldframe = legsOldFrame;
	legs.backlerp = legsBacklerp;

	torso.frame = torsoFrame;
	torso.oldframe = torsoOldFrame;
	torso.backlerp = torsoBacklerp;
	if ( !ci->isSkeletal ) {
		CG_RaceGhostAddEntity( &legs, renderMode, r, g, b, alpha );
		VectorSet( torsoAngles, 0, yaw, 0 );
		AnglesToAxis( torsoAngles, torso.axis );
		torso.reType = RT_MODEL;
		torso.hModel = ci->torsoModel;
		torso.customSkin = ci->torsoSkin;
		torso.renderfx = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
		CG_RaceGhostPrepareEntity( &torso, renderMode, r, g, b, alpha );
		VectorCopy( lightOrigin, torso.lightingOrigin );
		CG_PositionRotatedEntityOnTag( &torso, &legs, "tag_torso" );
		CG_RaceGhostAddEntity( &torso, renderMode, r, g, b, alpha );
	} else {
		legs.torsoFrame = torsoFrame;
		legs.oldTorsoFrame = torsoOldFrame;
		legs.torsoBacklerp = torsoBacklerp;
		AxisClear( legs.torsoAxis );
		CG_RaceGhostAddEntity( &legs, renderMode, r, g, b, alpha );
		torso = legs;
	}

	head.reType = RT_MODEL;
	head.hModel = ci->headModel;
	head.customSkin = ci->headSkin;
	head.renderfx = RF_NOSHADOW | RF_LIGHTING_ORIGIN;
	CG_RaceGhostPrepareEntity( &head, renderMode, r, g, b, alpha );
	VectorCopy( lightOrigin, head.lightingOrigin );
	AxisClear( head.axis );
	CG_PositionRotatedEntityOnTag( &head, &torso, "tag_head" );
	CG_RaceGhostAddEntity( &head, renderMode, r, g, b, alpha );
	CG_AddRaceGhostWeapon( ci, &torso, weaponNum, r, g, b, alpha, renderMode );
}

static int CG_RaceGhostClientNum( int ghostSlot ) {
	return MAX_CLIENTS - CG_RACE_MAX_GHOSTS + ghostSlot;
}

static int CG_RaceGhostAnimMovetype( qboolean crouched, float speed ) {
	if ( crouched ) {
		return ( 1 << ( speed > 20.0f ? ANIM_MT_WALKCR : ANIM_MT_IDLECR ) );
	}
	return ( 1 << ( speed > 20.0f ? ANIM_MT_RUN : ANIM_MT_IDLE ) );
}

static int CG_RaceGhostNormalizeMovementDir( int movementDir ) {
	if ( movementDir > 128 && movementDir <= 255 ) movementDir -= 256;
	if ( movementDir < -128 || movementDir > 128 ) movementDir = 0;
	return movementDir;
}

static int CG_RaceGhostAnimMovetypeForAnim( clientInfo_t *ci, int legsAnim, qboolean crouched, float speed ) {
	animation_t *anim;

	anim = CG_GhostFindAnimIndex( ci, legsAnim );
	if ( anim && anim->movetype ) {
		return anim->movetype;
	}
	return CG_RaceGhostAnimMovetype( crouched, speed );
}

static int CG_RaceGhostFallbackAnim( clientInfo_t *ci, qboolean crouched, float speed, qboolean torso ) {
	animation_t *anim;

	if ( !ci || !ci->modelInfo || ci->modelInfo->numAnimations <= 0 ) return 0;
	anim = NULL;
	if ( torso ) {
		if ( crouched ) anim = CG_GhostFindAnim( ci, "torso_crouch" );
		if ( !anim && speed > 20.0f ) anim = CG_GhostFindAnim( ci, "torso_move" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "torso_stand" );
	} else {
		if ( crouched ) anim = CG_GhostFindCrouchAnim( ci, speed > 20.0f ? qtrue : qfalse );
		if ( !anim && speed > 20.0f ) anim = CG_GhostFindAnim( ci, "run" );
		if ( !anim && speed > 20.0f ) anim = CG_GhostFindAnim( ci, "trot" );
		if ( !anim ) anim = CG_GhostFindAnim( ci, "idle" );
	}
	if ( !anim ) anim = &ci->modelInfo->animations[0];
	return (int)( anim - ci->modelInfo->animations );
}

static int CG_RaceGhostSanitizeAnim( clientInfo_t *ci, int anim, qboolean crouched, float speed, qboolean torso ) {
	int animIndex;

	if ( ci && ci->modelInfo ) {
		animIndex = anim & ~ANIM_TOGGLEBIT;
		if ( anim >= 0 && animIndex >= 0 && animIndex < ci->modelInfo->numAnimations ) {
			return anim;
		}
	}
	return CG_RaceGhostFallbackAnim( ci, crouched, speed, torso );
}

static qboolean CG_AddRaceGhostPlayerModel( const raceGhostInfo_t *info, int ghostSlot ) {
	centity_t *cent;
	clientInfo_t *baseCi;
	clientInfo_t backupCi;
	int backupClientModel;
	int backupClientConditions[NUM_ANIM_CONDITIONS][2];
	vec3_t origin, angles, velocity, delta;
	int baseClientNum;
	int ghostClientNum;
	int weaponNum;
	int eFlags;
	int renderMode;
	int legsAnim;
	int torsoAnim;
	byte r, g, b;
	qboolean resetEntity;

	if ( !info || !cg.snap || ghostSlot < 0 || ghostSlot >= CG_RACE_MAX_GHOSTS ) return qfalse;
	baseClientNum = cg.clientNum;
	if ( baseClientNum < 0 || baseClientNum >= MAX_CLIENTS ) baseClientNum = cg.snap->ps.clientNum;
	if ( baseClientNum < 0 || baseClientNum >= MAX_CLIENTS ) return qfalse;
	baseCi = &cgs.clientinfo[baseClientNum];
	if ( !baseCi->infoValid || !baseCi->legsModel || !baseCi->torsoModel || !baseCi->headModel || !baseCi->modelInfo ) return qfalse;
	if ( cgs.animScriptData.clientModels[baseClientNum] <= 0 || cgs.animScriptData.clientModels[baseClientNum] > MAX_ANIMSCRIPT_MODELS ) return qfalse;

	ghostClientNum = CG_RaceGhostClientNum( ghostSlot );
	backupCi = cgs.clientinfo[ghostClientNum];
	backupClientModel = cgs.animScriptData.clientModels[ghostClientNum];
	memcpy( backupClientConditions, cgs.animScriptData.clientConditions[ghostClientNum], sizeof( backupClientConditions ) );
	cgs.clientinfo[ghostClientNum] = *baseCi;
	cgs.clientinfo[ghostClientNum].clientNum = ghostClientNum;
	cgs.clientinfo[ghostClientNum].health = info->health;
	cgs.clientinfo[ghostClientNum].armor = info->armor;
	cgs.clientinfo[ghostClientNum].curWeapon = info->weapon;
	Q_strncpyz( cgs.clientinfo[ghostClientNum].name, info->nick, sizeof( cgs.clientinfo[ghostClientNum].name ) );
	cgs.animScriptData.clientModels[ghostClientNum] = cgs.animScriptData.clientModels[baseClientNum];
	memcpy( cgs.animScriptData.clientConditions[ghostClientNum], cgs.animScriptData.clientConditions[baseClientNum], sizeof( cgs.animScriptData.clientConditions[ghostClientNum] ) );

	cent = &race_player_ghosts[ghostSlot];
	VectorSet( origin, info->x, info->y, info->z );
	VectorSet( angles, info->pitch, info->yaw, 0.0f );
	VectorSet( velocity, info->vx, info->vy, info->vz );
	if ( VectorLength( velocity ) <= 1.0f && info->speed > 1.0f ) {
		vec3_t forward;
		VectorSet( angles, 0.0f, info->yaw, 0.0f );
		AngleVectors( angles, forward, NULL, NULL );
		VectorScale( forward, info->speed, velocity );
		VectorSet( angles, info->pitch, info->yaw, 0.0f );
	}
	weaponNum = info->weapon;
	if ( weaponNum < WP_NONE || weaponNum >= WP_NUM_WEAPONS ) weaponNum = WP_NONE;
	cgs.clientinfo[ghostClientNum].curWeapon = weaponNum;
	eFlags = info->eFlags & CG_RACE_GHOST_PLAYER_EFLAGS;
	if ( info->crouched ) eFlags |= EF_CROUCHING;
	renderMode = CG_RaceGhostRenderMode();
	r = (byte)( ( info->packedColor >> 16 ) & 255 );
	g = (byte)( ( info->packedColor >> 8 ) & 255 );
	b = (byte)( info->packedColor & 255 );

	resetEntity = !race_player_ghost_used[ghostSlot];
	if ( !resetEntity ) {
		VectorSubtract( origin, cent->lerpOrigin, delta );
		if ( VectorLength( delta ) > 160.0f || cent->currentState.clientNum != ghostClientNum ) resetEntity = qtrue;
	}
	if ( resetEntity ) {
		memset( cent, 0, sizeof( *cent ) );
		race_player_ghost_used[ghostSlot] = qtrue;
	}

	memset( &cent->currentState, 0, sizeof( cent->currentState ) );
	cent->currentState.number = ghostClientNum;
	cent->currentState.clientNum = ghostClientNum;
	cent->currentState.eType = ET_PLAYER;
	cent->currentState.eFlags = eFlags;
	cent->currentState.weapon = weaponNum;
	cent->currentState.groundEntityNum = ( info->groundEntityNum >= 0 && info->groundEntityNum <= ENTITYNUM_NONE ) ? info->groundEntityNum : ENTITYNUM_WORLD;
	legsAnim = CG_RaceGhostSanitizeAnim( &cgs.clientinfo[ghostClientNum], info->legsAnim, info->crouched ? qtrue : qfalse, info->speed, qfalse );
	torsoAnim = CG_RaceGhostSanitizeAnim( &cgs.clientinfo[ghostClientNum], info->torsoAnim, info->crouched ? qtrue : qfalse, info->speed, qtrue );
	cent->currentState.legsAnim = legsAnim;
	cent->currentState.torsoAnim = torsoAnim;
	cent->currentState.aiChar = AICHAR_NONE;
	cent->currentState.animMovetype = info->animMovetype ? info->animMovetype : CG_RaceGhostAnimMovetypeForAnim( &cgs.clientinfo[ghostClientNum], legsAnim, info->crouched ? qtrue : qfalse, info->speed );
	cent->currentState.angles2[YAW] = (float)info->movementDir;
	cent->currentState.pos.trType = TR_STATIONARY;
	VectorCopy( origin, cent->currentState.pos.trBase );
	VectorCopy( velocity, cent->currentState.pos.trDelta );
	cent->currentState.apos.trType = TR_STATIONARY;
	VectorCopy( angles, cent->currentState.apos.trBase );
	VectorCopy( origin, cent->currentState.origin );
	VectorCopy( angles, cent->currentState.angles );
	cent->nextState = cent->currentState;
	{
		int snapMsec = 50;
		if ( cg.snap && cg.nextSnap && cg.nextSnap->serverTime > cg.snap->serverTime ) {
			snapMsec = cg.nextSnap->serverTime - cg.snap->serverTime;
			if ( snapMsec < 1 ) snapMsec = 1;
			if ( snapMsec > 200 ) snapMsec = 200;
		}
		VectorMA( origin, (float)snapMsec * 0.001f, velocity, cent->nextState.pos.trBase );
		VectorCopy( cent->nextState.pos.trBase, cent->nextState.origin );
	}
	cent->interpolate = qfalse;
	cent->currentValid = qtrue;
	cent->pe.animSpeed = 1.0f;
	VectorCopy( origin, cent->lerpOrigin );
	VectorCopy( angles, cent->lerpAngles );
	VectorCopy( origin, cent->rawOrigin );
	VectorCopy( angles, cent->rawAngles );
	if ( resetEntity ) CG_ResetPlayerEntity( cent );

	CG_RaceGhostStyleBegin( renderMode, r, g, b, info->alpha, ghostShader, raceGhostTintShader, raceGhostXrayShader );
	CG_Player( cent );
	CG_RaceGhostStyleEnd();
	cgs.clientinfo[ghostClientNum] = backupCi;
	cgs.animScriptData.clientModels[ghostClientNum] = backupClientModel;
	memcpy( cgs.animScriptData.clientConditions[ghostClientNum], backupClientConditions, sizeof( cgs.animScriptData.clientConditions[ghostClientNum] ) );
	return qtrue;
}

static qboolean CG_ParseRaceGhost( const char *text, raceGhostInfo_t *out ) {
	int visible;
	int parsed;
	raceGhostInfo_t local;
	if ( !text || !text[0] || !out ) return qfalse;
	memset( &local, 0, sizeof( local ) );
	local.health = 100;
	local.legsAnim = -1;
	local.torsoAnim = -1;
	local.groundEntityNum = ENTITYNUM_WORLD;
	parsed = sscanf( text, "%d %d %d %f %f %f %f %f %d %d %d %d %d %d %d %d %31s %d %d %f %f %f %f %d %d",
		&visible, &local.packedColor, &local.alpha,
		&local.x, &local.y, &local.z, &local.yaw, &local.speed,
		&local.crouched, &local.health, &local.armor, &local.weapon,
		&local.ammo, &local.clip, &local.legsAnim, &local.torsoAnim, local.nick,
		&local.movementDir, &local.eFlags, &local.pitch, &local.vx, &local.vy, &local.vz,
		&local.groundEntityNum, &local.animMovetype );
	if ( parsed < 16 ) {
		local.legsAnim = -1;
		local.torsoAnim = -1;
		parsed = sscanf( text, "%d %d %d %f %f %f %f %f %d %d %d %d %d %d %31s",
			&visible, &local.packedColor, &local.alpha,
			&local.x, &local.y, &local.z, &local.yaw, &local.speed,
			&local.crouched, &local.health, &local.armor, &local.weapon,
			&local.ammo, &local.clip, local.nick );
	}
	if ( parsed < 14 ) {
		parsed = sscanf( text, "%d %d %d %f %f %f %f %f %31s",
			&visible, &local.packedColor, &local.alpha,
			&local.x, &local.y, &local.z, &local.yaw, &local.speed, local.nick );
	}
	if ( parsed < 8 ) {
		return qfalse;
	}
	if ( !visible ) return qfalse;
	if ( !local.nick[0] ) Q_strncpyz( local.nick, "Runner", sizeof( local.nick ) );
	if ( local.alpha < 0 ) local.alpha = 0;
	if ( local.alpha > 255 ) local.alpha = 255;
	if ( local.health < 0 ) local.health = 0;
	local.movementDir = CG_RaceGhostNormalizeMovementDir( local.movementDir );
	if ( local.armor < 0 ) local.armor = 0;
	if ( local.weapon < 0 || local.weapon >= WP_NUM_WEAPONS ) local.weapon = 0;
	if ( local.ammo < 0 ) local.ammo = 0;
	if ( local.clip < 0 ) local.clip = 0;
	if ( local.groundEntityNum < 0 || local.groundEntityNum > ENTITYNUM_NONE ) local.groundEntityNum = ENTITYNUM_WORLD;
	if ( local.animMovetype < 0 ) local.animMovetype = 0;
	*out = local;
	return qtrue;
}

static void CG_AddRaceGhosts( void ) {
	int i;
	if ( !ghost_initialized ) CG_InitGhost();
	trap_Cvar_Update( &race_active );
	if ( !race_active.integer ) return;
	trap_Cvar_Update( &race_ghost_render );
	for ( i = 0; i < CG_RACE_MAX_GHOSTS; i++ ) {
		raceGhostInfo_t info;
		byte r, g, b;
		trap_Cvar_Update( &race_ghost_data[i] );
		if ( !CG_ParseRaceGhost( race_ghost_data[i].string, &info ) ) continue;
		if ( CG_AddRaceGhostPlayerModel( &info, i ) ) continue;
		r = (byte)( ( info.packedColor >> 16 ) & 255 );
		g = (byte)( ( info.packedColor >> 8 ) & 255 );
		b = (byte)( info.packedColor & 255 );
		CG_AddRaceGhostModel( info.x, info.y, info.z, info.yaw, info.speed, r, g, b, info.alpha, info.crouched ? qtrue : qfalse, info.weapon, info.legsAnim, info.torsoAnim, &race_legs[i], &race_torso[i] );
	}
}

static int CG_RaceNametagClampInt( int value, int minValue, int maxValue ) {
	if ( value < minValue ) return minValue;
	if ( value > maxValue ) return maxValue;
	return value;
}

static float CG_RaceNametagClampFloat( float value, float minValue, float maxValue ) {
	if ( value < minValue ) return minValue;
	if ( value > maxValue ) return maxValue;
	return value;
}

static float CG_RaceNametagDistanceScale( const vec3_t labelPos ) {
	vec3_t delta;
	float dist;
	VectorSubtract( labelPos, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );
	if ( dist <= 350.0f ) return 1.18f;
	if ( dist >= 2000.0f ) return 0.58f;
	return 1.18f - ( ( dist - 350.0f ) / 1650.0f ) * 0.60f;
}

static void CG_RaceNametagDrawMeter( float x, float y, float w, float h, const char *label, int value, int maxValue, const float *fillColor, float scale, float alphaMul ) {
	vec4_t bg = { 0.02f, 0.025f, 0.02f, 0.68f };
	vec4_t border = { 0.72f, 0.80f, 0.70f, 0.30f };
	vec4_t text = { 0.94f, 0.98f, 0.92f, 0.95f };
	vec4_t fill;
	char line[24];
	float frac;
	int charW;
	int charH;

	if ( maxValue <= 0 ) maxValue = 100;
	frac = (float)value / (float)maxValue;
	if ( frac < 0.0f ) frac = 0.0f;
	if ( frac > 1.0f ) frac = 1.0f;
	Vector4Copy( fillColor, fill );
	bg[3] *= alphaMul;
	border[3] *= alphaMul;
	text[3] *= alphaMul;
	fill[3] *= alphaMul;
	charW = CG_RaceNametagClampInt( (int)( 4.0f * scale ), 3, 8 );
	charH = CG_RaceNametagClampInt( (int)( 6.0f * scale ), 5, 12 );
	CG_FillRect( x, y, w, h, bg, ALIGN_STRETCH );
	if ( frac > 0.0f ) {
		CG_FillRect( x + 1.0f, y + 1.0f, ( w - 2.0f ) * frac, h - 2.0f, fill, ALIGN_STRETCH );
	}
	CG_DrawRect( x, y, w, h, 0.75f, border, ALIGN_STRETCH );
	Com_sprintf( line, sizeof( line ), "%s %d", label, value );
	CG_DrawStringExt( (int)( x + 3.0f * scale ), (int)( y + ( h - charH ) * 0.5f ), line, text, qtrue, qtrue, charW, charH, 0, ALIGN_STRETCH );
}

void CG_DrawRaceGhostLabels( void ) {
	int i;
	if ( !ghost_initialized ) CG_InitGhost();
	trap_Cvar_Update( &race_active );
	if ( !race_active.integer ) return;
	trap_Cvar_Update( &race_nametag );
	if ( !race_nametag.integer ) return;
	trap_Cvar_Update( &race_nametag_stats );
	trap_Cvar_Update( &race_nametag_icons );
	trap_Cvar_Update( &race_nametag_scale );
	trap_Cvar_Update( &race_nametag_opacity );
	for ( i = 0; i < CG_RACE_MAX_GHOSTS; i++ ) {
		raceGhostInfo_t info;
		float sx, sy;
		vec3_t labelPos;
		vec4_t color;
		vec4_t muted = { 0.70f, 0.76f, 0.68f, 0.92f };
		float scale;
		float alphaMul;
		int nickW, nickH, statW, statH, width;
		trap_Cvar_Update( &race_ghost_data[i] );
		if ( !CG_ParseRaceGhost( race_ghost_data[i].string, &info ) ) continue;
		VectorSet( labelPos, info.x, info.y, info.z + ( info.crouched ? 56.0f : 72.0f ) );
		if ( !TrigVis_WorldToScreen( labelPos, &sx, &sy ) ) continue;
		color[0] = ( ( info.packedColor >> 16 ) & 255 ) / 255.0f;
		color[1] = ( ( info.packedColor >> 8 ) & 255 ) / 255.0f;
		color[2] = ( info.packedColor & 255 ) / 255.0f;
		alphaMul = CG_RaceNametagClampFloat( race_nametag_opacity.value, 0.15f, 1.0f );
		color[3] = 0.95f * alphaMul;
		muted[3] *= alphaMul;
		scale = CG_RaceNametagClampFloat( race_nametag_scale.value, 0.45f, 1.45f );
		scale *= CG_RaceNametagDistanceScale( labelPos );
		scale = CG_RaceNametagClampFloat( scale, 0.48f, 1.30f );
		nickW = CG_RaceNametagClampInt( (int)( 6.0f * scale ), 4, 13 );
		nickH = CG_RaceNametagClampInt( (int)( 9.0f * scale ), 6, 18 );
		statW = CG_RaceNametagClampInt( (int)( 4.5f * scale ), 3, 8 );
		statH = CG_RaceNametagClampInt( (int)( 7.0f * scale ), 5, 12 );
		width = (int)strlen( info.nick ) * nickW;
		CG_DrawStringExt( (int)( sx - width * 0.5f ), (int)sy, info.nick, color, qtrue, qtrue, nickW, nickH, 0, ALIGN_STRETCH );
		if ( race_nametag_stats.integer ) {
			char ammoText[32];
			int ammoTextWidth;
			float panelW;
			float panelX;
			float rowY = sy + nickH + 1.0f * scale;
			float barH;
			float gap;
			vec4_t panelBg = { 0.00f, 0.00f, 0.00f, 0.38f };
			vec4_t hpFill = { 0.22f, 0.84f, 0.30f, 0.82f };
			vec4_t hpLowFill = { 0.90f, 0.22f, 0.18f, 0.86f };
			vec4_t armorFill = { 0.28f, 0.58f, 1.00f, 0.80f };
			panelBg[3] *= alphaMul;
			panelW = (float)CG_RaceNametagClampInt( (int)( 64.0f * scale ), 44, 94 );
			panelX = sx - panelW * 0.5f;
			barH = (float)CG_RaceNametagClampInt( (int)( 6.0f * scale ), 5, 10 );
			gap = 2.0f * scale;
			CG_FillRect( panelX - 2.0f * scale, rowY - 1.0f * scale, panelW + 4.0f * scale, barH * 2.0f + gap + 2.0f * scale, panelBg, ALIGN_STRETCH );
			CG_RaceNametagDrawMeter( panelX, rowY, panelW, barH, "HP", info.health, 100, info.health <= 25 ? hpLowFill : hpFill, scale, alphaMul );
			rowY += barH + gap;
			CG_RaceNametagDrawMeter( panelX, rowY, panelW, barH, "AR", info.armor, 100, armorFill, scale, alphaMul );
			Com_sprintf( ammoText, sizeof( ammoText ), "%d/%d", info.clip, info.ammo );
			ammoTextWidth = (int)strlen( ammoText ) * statW;
			rowY += barH + 2.0f * scale;
			if ( race_nametag_icons.integer && info.weapon > WP_NONE && info.weapon < WP_NUM_WEAPONS ) {
				float iconSize = 9.0f * scale;
				qhandle_t icon;
				CG_RegisterWeapon( info.weapon );
				icon = cg_weapons[info.weapon].weaponIcon[0];
				if ( icon ) {
					float startX = sx - ( iconSize + 3.0f * scale + ammoTextWidth ) * 0.5f;
					trap_R_SetColor( muted );
					CG_DrawPic( startX, rowY - 1.0f * scale, iconSize, iconSize, icon, ALIGN_STRETCH );
					trap_R_SetColor( NULL );
					CG_DrawStringExt( (int)( startX + iconSize + 3.0f * scale ), (int)rowY, ammoText, muted, qtrue, qtrue, statW, statH, 0, ALIGN_STRETCH );
					continue;
				}
			}
			CG_DrawStringExt( (int)( sx - ammoTextWidth * 0.5f ), (int)rowY, ammoText, muted, qtrue, qtrue, statW, statH, 0, ALIGN_STRETCH );
		}
	}
}

/*
=============================================================================

  MODEL TESTING

The viewthing and gun positioning tools from Q2 have been integrated and
enhanced into a single model testing facility.

Model viewing can begin with either "testmodel <modelname>" or "testgun <modelname>".

The names must be the full pathname after the basedir, like
"models/weapons/v_launch/tris.md3" or "players/male/tris.md3"

Testmodel will create a fake entity 100 units in front of the current view
position, directly facing the viewer.  It will remain immobile, so you can
move around it to view it from different angles.

Testgun will cause the model to follow the player around and supress the real
view weapon model.  The default frame 0 of most guns is completely off screen,
so you will probably have to cycle a couple frames to see it.

"nextframe", "prevframe", "nextskin", and "prevskin" commands will change the
frame or skin of the testmodel.  These are bound to F5, F6, F7, and F8 in
q3default.cfg.

If a gun is being tested, the "gun_x", "gun_y", and "gun_z" variables will let
you adjust the positioning.

Note that none of the model testing features update while the game is paused, so
it may be convenient to test with deathmatch set to 1 so that bringing down the
console doesn't pause the game.

=============================================================================
*/

/*
=================
CG_TestModel_f

Creates an entity in front of the current position, which
can then be moved around
=================
*/
void CG_TestModel_f( void ) {
	vec3_t angles;

	memset( &cg.testModelEntity, 0, sizeof( cg.testModelEntity ) );
	if ( trap_Argc() < 2 ) {
		return;
	}

	Q_strncpyz( cg.testModelName, CG_Argv( 1 ), MAX_QPATH );
	cg.testModelEntity.hModel = trap_R_RegisterModel( cg.testModelName );

	if ( trap_Argc() == 3 ) {
		cg.testModelEntity.backlerp = atof( CG_Argv( 2 ) );
		cg.testModelEntity.frame = 1;
		cg.testModelEntity.oldframe = 0;
	}
	if ( !cg.testModelEntity.hModel ) {
		CG_Printf( "Can't register model\n" );
		return;
	}

	VectorMA( cg.refdef.vieworg, 100, cg.refdef.viewaxis[0], cg.testModelEntity.origin );

	angles[PITCH] = 0;
	angles[YAW] = 180 + cg.refdefViewAngles[1];
	angles[ROLL] = 0;

	AnglesToAxis( angles, cg.testModelEntity.axis );
	cg.testGun = qfalse;
}

/*
=================
CG_TestGun_f

Replaces the current view weapon with the given model
=================
*/
void CG_TestGun_f( void ) {
	CG_TestModel_f();
	cg.testGun = qtrue;
	cg.testModelEntity.renderfx = RF_MINLIGHT | RF_DEPTHHACK | RF_FIRST_PERSON;
}


void CG_TestModelNextFrame_f( void ) {
	cg.testModelEntity.frame++;
	CG_Printf( "frame %i\n", cg.testModelEntity.frame );
}

void CG_TestModelPrevFrame_f( void ) {
	cg.testModelEntity.frame--;
	if ( cg.testModelEntity.frame < 0 ) {
		cg.testModelEntity.frame = 0;
	}
	CG_Printf( "frame %i\n", cg.testModelEntity.frame );
}

void CG_TestModelNextSkin_f( void ) {
	cg.testModelEntity.skinNum++;
	CG_Printf( "skin %i\n", cg.testModelEntity.skinNum );
}

void CG_TestModelPrevSkin_f( void ) {
	cg.testModelEntity.skinNum--;
	if ( cg.testModelEntity.skinNum < 0 ) {
		cg.testModelEntity.skinNum = 0;
	}
	CG_Printf( "skin %i\n", cg.testModelEntity.skinNum );
}

static void CG_AddTestModel( void ) {
	int i;

	// re-register the model, because the level may have changed
	cg.testModelEntity.hModel = trap_R_RegisterModel( cg.testModelName );
	if ( !cg.testModelEntity.hModel ) {
		CG_Printf( "Can't register model\n" );
		return;
	}

	// if testing a gun, set the origin reletive to the view origin
	if ( cg.testGun ) {
		VectorCopy( cg.refdef.vieworg, cg.testModelEntity.origin );
		VectorCopy( cg.refdef.viewaxis[0], cg.testModelEntity.axis[0] );
		VectorCopy( cg.refdef.viewaxis[1], cg.testModelEntity.axis[1] );
		VectorCopy( cg.refdef.viewaxis[2], cg.testModelEntity.axis[2] );

		// allow the position to be adjusted
		for ( i = 0 ; i < 3 ; i++ ) {
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[0][i] * cg_gun_x.value;
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[1][i] * cg_gun_y.value;
			cg.testModelEntity.origin[i] += cg.refdef.viewaxis[2][i] * cg_gun_z.value;
		}
	}

	trap_R_AddRefEntityToScene( &cg.testModelEntity );
}



//============================================================================


/*
=================
CG_CalcVrect

Sets the coordinates of the rendered window
=================
*/
// TTimo: unused
//static float letterbox_frac = 1.0f;	// used for transitioning to letterbox for cutscenes // TODO: add to cg.

static void CG_CalcVrect( void ) {
	int xsize, ysize;
	float lbheight, lbdiff;

	// NERVE - SMF
	if ( cg.limboMenu ) {
		float x, y, w, h;
		x = LIMBO_3D_X;
		y = LIMBO_3D_Y;
		w = LIMBO_3D_W;
		h = LIMBO_3D_H;

		cg.refdef.width = 0;
		CG_AdjustFrom640( &x, &y, &w, &h, ALIGN_CENTER, qfalse );

		cg.refdef.x = x;
		cg.refdef.y = y;
		cg.refdef.width = w;
		cg.refdef.height = h;
		return;
	}
	// -NERVE - SMF

	// the intermission should allways be full screen
	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		xsize = ysize = 100;
	} else {
		// bound normal viewsize
		if ( cg_viewsize.integer < 30 ) {
			trap_Cvar_Set( "cg_viewsize","30" );
			xsize = ysize = 30;
		} else if ( cg_viewsize.integer > 100 ) {
			trap_Cvar_Set( "cg_viewsize","100" );
			xsize = ysize = 100;
		} else {
			xsize = ysize = cg_viewsize.integer;
		}
	}

//----(SA)	added transition to/from letterbox
// normal aspect is xx:xx
// letterbox is yy:yy  (85% of 'normal' height)

	lbheight = ysize * 0.85;
	lbdiff = ysize - lbheight;

	if ( cg_letterbox.integer ) {
		ysize = lbheight;
//		if(letterbox_frac != 0) {
//			letterbox_frac -= 0.01f;	// (SA) TODO: make non fps dependant
//			if(letterbox_frac < 0)
//				letterbox_frac = 0;
//			ysize += (lbdiff * letterbox_frac);
//		}
//	} else {
//		if(letterbox_frac != 1) {
//			letterbox_frac += 0.01f;	// (SA) TODO: make non fps dependant
//			if(letterbox_frac > 1)
//				letterbox_frac = 1;
//			ysize = lbheight + (lbdiff * letterbox_frac);
//		}
	}
//----(SA)	end


	cg.refdef.width = cgs.glconfig.vidWidth * xsize / 100;
	cg.refdef.width &= ~1;

	cg.refdef.height = cgs.glconfig.vidHeight * ysize / 100;
	cg.refdef.height &= ~1;

	cg.refdef.x = ( cgs.glconfig.vidWidth - cg.refdef.width ) / 2;
	cg.refdef.y = ( cgs.glconfig.vidHeight - cg.refdef.height ) / 2;

	if ( cg_blackbars.integer && !cg.zoomedScope && !cg.zoomedBinoc && !( cg.snap && ( cg.snap->ps.eFlags & EF_ZOOMING ) ) ) {
		int left = cg_blackbarLeft.integer;
		int right = cg_blackbarRight.integer;
		int total;
		int maxTotal;

		if ( left < 0 ) left = 0;
		if ( right < 0 ) right = 0;
		maxTotal = cg.refdef.width - 320;
		if ( maxTotal < 0 ) maxTotal = 0;
		total = left + right;
		if ( total > maxTotal && total > 0 ) {
			left = (int)( (float)left * (float)maxTotal / (float)total );
			right = maxTotal - left;
		}
		cg.refdef.x += left;
		cg.refdef.width -= left + right;
		cg.refdef.width &= ~1;
	}
}

//==============================================================================


/*
===============
CG_OffsetThirdPersonView

===============
*/
#define FOCUS_DISTANCE  512
static void CG_OffsetThirdPersonView( void ) {
	vec3_t forward, right, up;
	vec3_t view;
	vec3_t focusAngles;
	trace_t trace;
	static vec3_t mins = { -4, -4, -4 };
	static vec3_t maxs = { 4, 4, 4 };
	vec3_t focusPoint;
	float focusDist;
	float forwardScale, sideScale;

	cg.refdef.vieworg[2] += cg.predictedPlayerState.viewheight;

	VectorCopy( cg.refdefViewAngles, focusAngles );

	// if dead, look at killer
	if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		focusAngles[YAW] = cg.predictedPlayerState.stats[STAT_DEAD_YAW];
		cg.refdefViewAngles[YAW] = cg.predictedPlayerState.stats[STAT_DEAD_YAW];
	}

	if ( focusAngles[PITCH] > 45 ) {
		focusAngles[PITCH] = 45;        // don't go too far overhead
	}
	AngleVectors( focusAngles, forward, NULL, NULL );

	VectorMA( cg.refdef.vieworg, FOCUS_DISTANCE, forward, focusPoint );

	VectorCopy( cg.refdef.vieworg, view );

	view[2] += 8;

	cg.refdefViewAngles[PITCH] *= 0.5;

	AngleVectors( cg.refdefViewAngles, forward, right, up );

	forwardScale = cos( cg_thirdPersonAngle.value / 180 * M_PI );
	sideScale = sin( cg_thirdPersonAngle.value / 180 * M_PI );
	VectorMA( view, -cg_thirdPersonRange.value * forwardScale, forward, view );
	VectorMA( view, -cg_thirdPersonRange.value * sideScale, right, view );

	// trace a ray from the origin to the viewpoint to make sure the view isn't
	// in a solid block.  Use an 8 by 8 block to prevent the view from near clipping anything

	CG_Trace( &trace, cg.refdef.vieworg, mins, maxs, view, cg.predictedPlayerState.clientNum, MASK_SOLID );

	if ( trace.fraction != 1.0 ) {
		VectorCopy( trace.endpos, view );
		view[2] += ( 1.0 - trace.fraction ) * 32;
		// try another trace to this position, because a tunnel may have the ceiling
		// close enogh that this is poking out

		CG_Trace( &trace, cg.refdef.vieworg, mins, maxs, view, cg.predictedPlayerState.clientNum, MASK_SOLID );
		VectorCopy( trace.endpos, view );
	}


	VectorCopy( view, cg.refdef.vieworg );

	// select pitch to look at focus point from vieword
	VectorSubtract( focusPoint, cg.refdef.vieworg, focusPoint );
	focusDist = sqrt( focusPoint[0] * focusPoint[0] + focusPoint[1] * focusPoint[1] );
	if ( focusDist < 1 ) {
		focusDist = 1;  // should never happen
	}
	cg.refdefViewAngles[PITCH] = -180 / M_PI * atan2( focusPoint[2], focusDist );
	cg.refdefViewAngles[YAW] -= cg_thirdPersonAngle.value;
}


// this causes a compiler bug on mac MrC compiler
static void CG_StepOffset( void ) {
	int timeDelta;

	// smooth out stair climbing
	timeDelta = cg.time - cg.stepTime;
	// Ridah
	if ( timeDelta < 0 ) {
		cg.stepTime = cg.time;
	}
	if ( timeDelta < STEP_TIME ) {
		cg.refdef.vieworg[2] -= cg.stepChange
								* ( STEP_TIME - timeDelta ) / STEP_TIME;
	}
}

/*
================
CG_KickAngles
================
*/
void CG_KickAngles( void ) {
	const vec3_t centerSpeed = {2400, 2400, 2400};
	const float recoilCenterSpeed = 200;
	const float recoilIgnoreCutoff = 15;
	const float recoilMaxSpeed = 50;
	const vec3_t maxKickAngles = {10,10,10};
	float idealCenterSpeed, kickChange;
	int i, frametime, t;
	float ft;
	#define STEP 20

	// this code is frametime-dependant, so split it up into small chunks
	//cg.kickAngles[PITCH] = 0;
	cg.recoilPitchAngle = 0;
	for ( t = cg.frametime; t > 0; t -= STEP ) {
		if ( t > STEP ) {
			frametime = STEP;
		} else {
			frametime = t;
		}

		ft = ( (float)frametime / 1000 );

		// kickAngles is spring-centered
		for ( i = 0; i < 3; i++ ) {
			if ( cg.kickAVel[i] || cg.kickAngles[i] ) {
				// apply centering forces to kickAvel
				if ( cg.kickAngles[i] && frametime ) {
					idealCenterSpeed = -( 2.0 * ( cg.kickAngles[i] > 0 ) - 1.0 ) * centerSpeed[i];
					if ( idealCenterSpeed ) {
						cg.kickAVel[i] += idealCenterSpeed * ft;
					}
				}
				// add the kickAVel to the kickAngles
				kickChange = cg.kickAVel[i] * ft;
				if ( cg.kickAngles[i] && ( cg.kickAngles[i] < 0 ) != ( kickChange < 0 ) ) { // slower when returning to center
					kickChange *= 0.06;
				}
				// check for crossing back over the center point
				if ( !cg.kickAngles[i] || ( ( cg.kickAngles[i] + kickChange ) < 0 ) == ( cg.kickAngles[i] < 0 ) ) {
					cg.kickAngles[i] += kickChange;
					if ( !cg.kickAngles[i] && frametime ) {
						cg.kickAVel[i] = 0;
					} else if ( fabs( cg.kickAngles[i] ) > maxKickAngles[i] ) {
						cg.kickAngles[i] = maxKickAngles[i] * ( ( 2 * ( cg.kickAngles[i] > 0 ) ) - 1 );
						cg.kickAVel[i] = 0; // force Avel to return us to center rather than keep going outside range
					}
				} else { // about to cross, so just zero it out
					cg.kickAngles[i] = 0;
					cg.kickAVel[i] = 0;
				}
			}
		}

		// recoil is added to input viewangles per frame
		if ( cg.recoilPitch ) {
			// apply max recoil
			if ( fabs( cg.recoilPitch ) > recoilMaxSpeed ) {
				if ( cg.recoilPitch > 0 ) {
					cg.recoilPitch = recoilMaxSpeed;
				} else {
					cg.recoilPitch = -recoilMaxSpeed;
				}
			}
			// apply centering forces to kickAvel
			if ( frametime ) {
				idealCenterSpeed = -( 2.0 * ( cg.recoilPitch > 0 ) - 1.0 ) * recoilCenterSpeed * ft;
				if ( idealCenterSpeed ) {
					if ( fabs( idealCenterSpeed ) < fabs( cg.recoilPitch ) ) {
						cg.recoilPitch += idealCenterSpeed;
					} else {    // back zero out
						cg.recoilPitch = 0;
					}
				}
			}
		}
		if ( fabs( cg.recoilPitch ) > recoilIgnoreCutoff ) {
			cg.recoilPitchAngle += cg.recoilPitch * ft;
		}
	}
	// encode the kick angles into a 24bit number, for sending to the client exe
//----(SA)	commented out since it doesn't appear to be used, and it spams the console when in "developer 1"
//	trap_Cvar_Set( "cg_recoilPitch", va("%f", cg.recoilPitchAngle) );
}


/*
CG_Concussive
*/
void CG_Concussive( centity_t *cent ) {
	float length;
//	vec3_t	dir, forward;
	vec3_t vec;
//	float	dot;

	//
	float pitchRecoilAdd, pitchAdd;
	float yawRandom;
	vec3_t recoil;
	//

	if ( !cg.renderingThirdPerson && cent->currentState.density == cg.snap->ps.clientNum ) {
		//
		pitchRecoilAdd = 0;
		pitchAdd = 0;
		yawRandom = 0;
		//

		VectorSubtract( cg.snap->ps.origin, cent->currentState.origin, vec );
		length = VectorLength( vec );

		// pitchAdd = 12+rand()%3;
		// yawRandom = 6;

		if ( length > 1024 ) {
			return;
		}

		pitchAdd = ( 32 / length ) * 64;
		yawRandom = ( 32 / length ) * 64;

		// recoil[YAW] = crandom()*yawRandom;
		if ( rand() % 100 > 50 ) {
			recoil[YAW] = -yawRandom;
		} else {
			recoil[YAW] = yawRandom;
		}

		recoil[ROLL] = -recoil[YAW];    // why not
		recoil[PITCH] = -pitchAdd;
		// scale it up a bit (easier to modify this while tweaking)
		VectorScale( recoil, 30, recoil );
		// set the recoil
		VectorCopy( recoil, cg.kickAVel );
		// set the recoil
		cg.recoilPitch -= pitchRecoilAdd;

	}
}


/*
==============
CG_ZoomSway
	sway for scoped weapons.
	this takes aimspread into account so the view settles after a bit
==============
*/
static void CG_ZoomSway( void ) {
	float spreadfrac;
	float phase;

	if ( !cg.zoomval ) { // not zoomed
		return;
	}

	if ( cg.snap->ps.eFlags & EF_MG42_ACTIVE ) { // don't draw when on mg_42
		return;
	}

	spreadfrac = (float)cg.snap->ps.aimSpreadScale / 255.0;

	phase = cg.time / 1000.0 * ZOOM_PITCH_FREQUENCY * M_PI * 2;
	cg.refdefViewAngles[PITCH] += ZOOM_PITCH_AMPLITUDE * sin( phase ) * ( spreadfrac + ZOOM_PITCH_MIN_AMPLITUDE );

	phase = cg.time / 1000.0 * ZOOM_YAW_FREQUENCY * M_PI * 2;
	cg.refdefViewAngles[YAW] += ZOOM_YAW_AMPLITUDE * sin( phase ) * ( spreadfrac + ZOOM_YAW_MIN_AMPLITUDE );

}



/*
===============
CG_OffsetFirstPersonView

===============
*/
static void CG_OffsetFirstPersonView( void ) {
	float           *origin;
	float           *angles;
	float bob;
	float ratio;
	float delta;
	float speed;
	float f;
	vec3_t predictedVelocity;
	int timeDelta;

	if ( cg.snap->ps.pm_type == PM_INTERMISSION ) {
		return;
	}

	origin = cg.refdef.vieworg;
	angles = cg.refdefViewAngles;

	// if dead, fix the angle and don't add any kick
	if ( cg.snap->ps.stats[STAT_HEALTH] <= 0 ) {
		angles[ROLL] = 40;
		angles[PITCH] = -15;
		angles[YAW] = cg.snap->ps.stats[STAT_DEAD_YAW];
		origin[2] += cg.predictedPlayerState.viewheight;
		return;
	}

	// add angles based on weapon kick
	VectorAdd( angles, cg.kick_angles, angles );

	// RF, add new weapon kick angles
	CG_KickAngles();
	VectorAdd( angles, cg.kickAngles, angles );
	// RF, pitch is already added
	//angles[0] -= cg.kickAngles[PITCH];

	// add angles based on damage kick
	if ( cg.damageTime ) {
		ratio = cg.time - cg.damageTime;
		if ( ratio < DAMAGE_DEFLECT_TIME ) {
			ratio /= DAMAGE_DEFLECT_TIME;
			angles[PITCH] += ratio * cg.v_dmg_pitch;
			angles[ROLL] += ratio * cg.v_dmg_roll;
		} else {
			ratio = 1.0 - ( ratio - DAMAGE_DEFLECT_TIME ) / DAMAGE_RETURN_TIME;
			if ( ratio > 0 ) {
				angles[PITCH] += ratio * cg.v_dmg_pitch;
				angles[ROLL] += ratio * cg.v_dmg_roll;
			}
		}
	}

	// add pitch based on fall kick
#if 0
	ratio = ( cg.time - cg.landTime ) / FALL_TIME;
	if ( ratio < 0 ) {
		ratio = 0;
	}
	angles[PITCH] += ratio * cg.fall_value;
#endif

	// add angles based on velocity
	VectorCopy( cg.predictedPlayerState.velocity, predictedVelocity );

	delta = DotProduct( predictedVelocity, cg.refdef.viewaxis[0] );
	angles[PITCH] += delta * cg_runpitch.value;

	delta = DotProduct( predictedVelocity, cg.refdef.viewaxis[1] );
	angles[ROLL] -= delta * cg_runroll.value;

	// add angles based on bob

	// make sure the bob is visible even at low speeds
	speed = cg.xyspeed > 200 ? cg.xyspeed : 200;

	delta = cg.bobfracsin * cg_bobpitch.value * speed;
	if ( cg.predictedPlayerState.pm_flags & PMF_DUCKED ) {
		delta *= 3;     // crouching
	}
	angles[PITCH] += delta;
	delta = cg.bobfracsin * cg_bobroll.value * speed;
	if ( cg.predictedPlayerState.pm_flags & PMF_DUCKED ) {
		delta *= 3;     // crouching accentuates roll
	}
	if ( cg.bobcycle & 1 ) {
		delta = -delta;
	}
	angles[ROLL] += delta;

//===================================

	// add view height
	origin[2] += cg.predictedPlayerState.viewheight;

	// smooth out duck height changes
	timeDelta = cg.time - cg.duckTime;
	if ( timeDelta < 0 ) { // Ridah
		cg.duckTime = cg.time - DUCK_TIME;
	}
	if ( timeDelta < DUCK_TIME ) {
		cg.refdef.vieworg[2] -= cg.duckChange
								* ( DUCK_TIME - timeDelta ) / DUCK_TIME;
	}

	// add bob height
	bob = cg.bobfracsin * cg.xyspeed * cg_bobup.value;
	if ( bob > 6 ) {
		bob = 6;
	}

	origin[2] += bob;


	// add fall height
	delta = cg.time - cg.landTime;
	if ( delta < 0 ) { // Ridah
		cg.landTime = cg.time - ( LAND_DEFLECT_TIME + LAND_RETURN_TIME );
	}
	if ( delta < LAND_DEFLECT_TIME ) {
		f = delta / LAND_DEFLECT_TIME;
		cg.refdef.vieworg[2] += cg.landChange * f;
	} else if ( delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME ) {
		delta -= LAND_DEFLECT_TIME;
		f = 1.0 - ( delta / LAND_RETURN_TIME );
		cg.refdef.vieworg[2] += cg.landChange * f;
	}

	// add step offset
	CG_StepOffset();

	CG_ZoomSway();

	// adjust for 'lean'
	if ( cg.predictedPlayerState.leanf != 0 ) {
		//add leaning offset
		vec3_t right;
		cg.refdefViewAngles[2] += cg.predictedPlayerState.leanf / 2.0f;
		AngleVectors( cg.refdefViewAngles, NULL, right, NULL );
		VectorMA( cg.refdef.vieworg, cg.predictedPlayerState.leanf, right, cg.refdef.vieworg );
	}

	// add kick offset

	VectorAdd( origin, cg.kick_origin, origin );

	// pivot the eye based on a neck length
#if 0
	{
#define NECK_LENGTH     8
		vec3_t forward, up;

		cg.refdef.vieworg[2] -= NECK_LENGTH;
		AngleVectors( cg.refdefViewAngles, forward, NULL, up );
		VectorMA( cg.refdef.vieworg, 3, forward, cg.refdef.vieworg );
		VectorMA( cg.refdef.vieworg, NECK_LENGTH, up, cg.refdef.vieworg );
	}
#endif
}

//======================================================================

//
// Zoom controls
//


// probably move to server variables
float zoomTable[ZOOM_MAX_ZOOMS][2] = {
// max {out,in}
	{0, 0},

	{36, 8},    //	binoc
	{20, 4},    //	sniper
	{60, 20},   //	snooper
	{40, 30},	//	fg42 //Knightmare- was 55, 55
	{55, 55}    //	mg42
};

void CG_AdjustZoomVal( float val, int type ) {
	cg.zoomval += val;
	if ( cg.zoomval > zoomTable[type][ZOOM_OUT] ) {
		cg.zoomval = zoomTable[type][ZOOM_OUT];
	}
	if ( cg.zoomval < zoomTable[type][ZOOM_IN] ) {
		cg.zoomval = zoomTable[type][ZOOM_IN];
	}
}

void CG_ZoomIn_f( void ) {
	if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_SNIPERRIFLE ) {
		CG_AdjustZoomVal( -( cg_zoomStepSniper.value ), ZOOM_SNIPER );
	} else if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_SNOOPERSCOPE )      {
		CG_AdjustZoomVal( -( cg_zoomStepSnooper.value ), ZOOM_SNOOPER );
	} else if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_FG42SCOPE )      {
		CG_AdjustZoomVal( -( cg_zoomStepSnooper.value ), ZOOM_FG42SCOPE );
	} else if ( cg.zoomedBinoc )      {
		CG_AdjustZoomVal( -( cg_zoomStepBinoc.value ), ZOOM_BINOC );
	}
}

void CG_ZoomOut_f( void ) {
	if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_SNIPERRIFLE ) {
		CG_AdjustZoomVal( cg_zoomStepSniper.value, ZOOM_SNIPER );
	} else if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_SNOOPERSCOPE )      {
		CG_AdjustZoomVal( cg_zoomStepSnooper.value, ZOOM_SNOOPER );
	} else if ( cg_entities[cg.snap->ps.clientNum].currentState.weapon == WP_FG42SCOPE )      {
		CG_AdjustZoomVal( cg_zoomStepSnooper.value, ZOOM_FG42SCOPE );
	} else if ( cg.zoomedBinoc )      {
		CG_AdjustZoomVal( cg_zoomStepBinoc.value, ZOOM_BINOC );
	}
}


/*
==============
CG_Zoom
==============
*/
void CG_Zoom( void ) {
	if ( cg.predictedPlayerState.eFlags & EF_ZOOMING ) {
		if ( cg.zoomedBinoc ) {
			return;
		}
		cg.zoomedBinoc  = qtrue;
		cg.zoomTime = cg.time;
		cg.zoomval = cg_zoomDefaultBinoc.value;
	} else {
		if ( !cg.zoomedBinoc ) {
			return;
		}
		cg.zoomedBinoc  = qfalse;
		cg.zoomTime = cg.time;

		// check for scope wepon in use, and switch to if necessary
		if ( cg.predictedPlayerState.weapon == WP_SNOOPERSCOPE ) {
			cg.zoomval = cg_zoomDefaultSnooper.value;
		} else if ( cg.predictedPlayerState.weapon == WP_SNIPERRIFLE ) {
			cg.zoomval = cg_zoomDefaultSniper.value;
		} else if ( cg.predictedPlayerState.weapon == WP_FG42SCOPE ) {
			cg.zoomval = cg_zoomDefaultFG.value;
		} else {
			cg.zoomval = 0;
		}
	}
}


/*
====================
CG_CalcFov

Fixed fov at intermissions, otherwise account for fov variable and zooms.
====================
*/
#define WAVE_AMPLITUDE  1
#define WAVE_FREQUENCY  0.4
#define STANDARD_ASPECT_RATIO ((float)640/(float)480)	// Knightmare added

static int CG_CalcFov( void ) {
	static float lastfov = 90;      // for transitions back from zoomed in modes
	float x;
	float phase;
	float v;
	int contents;
	float fov_x, fov_y;
	float zoomFov;
	float f;
	int inwater;
	qboolean dead;

	CG_Zoom();

	if ( cg.predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		dead = qtrue;
		cg.zoomedBinoc = qfalse;
		cg.zoomTime = 0;
		cg.zoomval = 0;
	} else {
		dead = qfalse;
	}

	if ( cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
		// if in intermission, use a fixed value
		fov_x = 90;
	} else {
		// user selectable
		if ((cgs.dmflags & DF_FIXED_FOV)) {
			// dmflag to prevent wide fov for all clients
			cg.fov = fov_x = 90;
		} else {
			fov_x = cg_fov.value;
			if ( fov_x < 1 ) {
				fov_x = 1;
			} else if ( fov_x > 160 ) {
				fov_x = 160;
			}
			
			cg.fov = fov_x;
			
		}

		// account for zooms
		if ( cg.zoomval ) {
			zoomFov = cg.zoomval;   // (SA) use user scrolled amount

			if ( zoomFov < 1 ) {
				zoomFov = 1;
			} else if ( zoomFov > 160 ) {
				zoomFov = 160;
			}
		} else {
			zoomFov = lastfov;
		}

		// do smooth transitions for the binocs
		if ( cg.zoomedBinoc ) {        // binoc zooming in
			f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
			if ( f > 1.0 ) {
				fov_x = zoomFov;
			} else {
				fov_x = fov_x + f * ( zoomFov - fov_x );
			}
			lastfov = fov_x;
		} else if ( cg.zoomval ) {    // zoomed by sniper/snooper
			fov_x = cg.zoomval;
			lastfov = fov_x;
		} else {                    // binoc zooming out
			f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
			if ( f > 1.0 ) {
				fov_x = fov_x;
			} else {
				fov_x = zoomFov + f * ( fov_x - zoomFov );
			}
		}
	}

	// DHM - Nerve :: zoom in for Limbo or Spectator
	if ( cgs.gametype == GT_WOLF ) {
		if ( cg.snap->ps.pm_flags & PMF_FOLLOW && cg.snap->ps.weapon == WP_SNIPERRIFLE ) {
			fov_x = cg_zoomDefaultSniper.value;
		}
	}
	// dhm - end

	if ( !dead && ( cg.weaponSelect == WP_SNOOPERSCOPE ) ) {
		cg.refdef.rdflags |= RDF_SNOOPERVIEW;
	} else {
		cg.refdef.rdflags &= ~RDF_SNOOPERVIEW;
	}

	if ( cg.snap->ps.persistant[PERS_HWEAPON_USE] ) {
		fov_x = 55;
	}

	// Knightmare- adjust fov_x for wide screen aspect
	if (cg_widescreen_fov.value)
	{
		float aspectRatio = (float)cg.refdef.width/(float)cg.refdef.height;
		if (aspectRatio > STANDARD_ASPECT_RATIO)
			fov_x = RAD2DEG( 2 * atan( (aspectRatio / STANDARD_ASPECT_RATIO) * tan(DEG2RAD(fov_x) * 0.5) ) );
		fov_x = min(fov_x, 160);
	}
	// end Knightmare

	x = cg.refdef.width / tan( fov_x / 360 * M_PI );
	fov_y = atan2( cg.refdef.height, x );
	fov_y = fov_y * 360 / M_PI;


	// warp if underwater
	contents = CG_PointContents( cg.refdef.vieworg, -1 );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		phase = cg.time / 1000.0 * WAVE_FREQUENCY * M_PI * 2;
		v = WAVE_AMPLITUDE * sin( phase );
		fov_x += v;
		fov_y -= v;
		inwater = qtrue;
		cg.refdef.rdflags |= RDF_UNDERWATER;
	} else {
		cg.refdef.rdflags &= ~RDF_UNDERWATER;
		inwater = qfalse;
	}

	contents = CG_PointContents( cg.refdef.vieworg, -1 );
	if ( contents & ( CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA ) ) {
		cg.refdef.rdflags |= RDF_UNDERWATER;
	} else {
		cg.refdef.rdflags &= ~RDF_UNDERWATER;
	}

	// set it
	cg.refdef.fov_x = fov_x;
	cg.refdef.fov_y = fov_y;

	if ( !cg.zoomedBinoc ) {
		// NERVE - SMF - fix for zoomed in/out movement bug
		if ( cg.zoomval ) {
			if ( cg.snap->ps.weapon == WP_SNOOPERSCOPE ) {
				cg.zoomSensitivity = 0.3f * ( cg.zoomval / 90.f );  // NERVE - SMF - changed to get less sensitive as you zoom in;
			}
//				cg.zoomSensitivity = 0.2;
			else {
				cg.zoomSensitivity = 0.6 * ( cg.zoomval / 90.f );   // NERVE - SMF - changed to get less sensitive as you zoom in
			}
//				cg.zoomSensitivity = 0.1;
		} else {
			cg.zoomSensitivity = 1;
		}
		// -NERVE - SMF
	} else {
		cg.zoomSensitivity = cg.refdef.fov_y / 75.0;
	}

	return inwater;
}


/*
==============
CG_UnderwaterSounds
==============
*/
#define UNDERWATER_BIT 8
static void CG_UnderwaterSounds( void ) {
//	trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, cgs.media.underWaterSound, 255 );
	trap_S_AddLoopingSound( cg.snap->ps.clientNum, cg.snap->ps.origin, vec3_origin, cgs.media.underWaterSound, 255 & ( 1 << 8 ) );
}


/*
===============
CG_DamageBlendBlob

===============
*/
static void CG_DamageBlendBlob( void ) {
	int t,i;
	int maxTime;
	refEntity_t ent;
	qboolean pointDamage;
	viewDamage_t *vd;
	float redFlash;

	// ragePro systems can't fade blends, so don't obscure the screen
	if ( cgs.glconfig.hardwareType == GLHW_RAGEPRO ) {
		return;
	}

	redFlash = 0;

	for ( i = 0; i < MAX_VIEWDAMAGE; i++ ) {

		vd = &cg.viewDamage[i];

		if ( !vd->damageValue ) {
			continue;
		}

		maxTime = vd->damageDuration;
		t = cg.time - vd->damageTime;
		if ( t <= 0 || t >= maxTime ) {
			vd->damageValue = 0;
			continue;
		}

		pointDamage = !( !vd->damageX && !vd->damageY );

		// if not point Damage, only do flash blend
		if ( !pointDamage ) {
			redFlash += 10.0 * ( 1.0 - (float)t / maxTime );
			continue;
		}

		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_SPRITE;
		ent.renderfx = RF_FIRST_PERSON;

		VectorMA( cg.refdef.vieworg, 8, cg.refdef.viewaxis[0], ent.origin );
		VectorMA( ent.origin, vd->damageX * -8, cg.refdef.viewaxis[1], ent.origin );
		VectorMA( ent.origin, vd->damageY * 8, cg.refdef.viewaxis[2], ent.origin );

		ent.radius = vd->damageValue * 0.4 * ( 0.5 + 0.5 * (float)t / maxTime ) * ( 0.75 + 0.5 * fabs( sin( vd->damageTime ) ) );

		ent.customShader = cgs.media.viewBloodAni[(int)( floor( ( (float)t / maxTime ) * 4.9 ) )]; //cgs.media.viewBloodShader;
		ent.shaderRGBA[0] = 255;
		ent.shaderRGBA[1] = 255;
		ent.shaderRGBA[2] = 255;
		ent.shaderRGBA[3] = 255;
		trap_R_AddRefEntityToScene( &ent );

		redFlash += ent.radius;
	}

	/* moved over to cg_draw.c
	if (cg.v_dmg_time > cg.time) {
		redFlash = fabs(cg.v_dmg_pitch * ((cg.v_dmg_time - cg.time) / DAMAGE_TIME));

		// blend the entire screen red
		if (redFlash > 5)
			redFlash = 5;

		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_SPRITE;
		ent.renderfx = RF_FIRST_PERSON;

		VectorMA( cg.refdef.vieworg, 8, cg.refdef.viewaxis[0], ent.origin );
		ent.radius = 80;	// occupy entire screen
		ent.customShader = cgs.media.viewFlashBlood;
		ent.shaderRGBA[3] = (int)(180.0 * redFlash/5.0);

		trap_R_AddRefEntityToScene( &ent );
	}
	*/
}

/*
===============
CG_CalcViewValues

Sets cg.refdef view values
===============
*/
static int CG_CalcViewValues( void ) {
	playerState_t   *ps;
	static vec3_t oldOrigin = {0,0,0};
	static qboolean oldOriginValid = qfalse;
	static int oldOriginTime = 0;

	memset( &cg.refdef, 0, sizeof( cg.refdef ) );

	// strings for in game rendering
	// Q_strncpyz( cg.refdef.text[0], "Park Ranger", sizeof(cg.refdef.text[0]) );
	// Q_strncpyz( cg.refdef.text[1], "19", sizeof(cg.refdef.text[1]) );

	// calculate size of 3D view
	CG_CalcVrect();

	ps = &cg.predictedPlayerState;

	if ( cg.cameraMode ) {
		vec3_t origin, angles;
		float fov = 90;
		float x;
		float originDelta;
		qboolean sendCameraOrigin;

		if ( trap_getCameraInfo( CAM_PRIMARY, cg.time, &origin, &angles, &fov ) ) {
			VectorCopy( origin, cg.refdef.vieworg );
			angles[ROLL] = 0;
			angles[PITCH] = -angles[PITCH];     // (SA) compensate for reversed pitch (this makes the game match the editor, however I'm guessing the real fix is to be done there)
			VectorCopy( angles, cg.refdefViewAngles );
			AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );

			// Knightmare- adjust fov_x for wide screen aspect
			if (cg_widescreen_fov.value)
			{
				float aspectRatio = (float)cg.refdef.width/(float)cg.refdef.height;
				if (aspectRatio > STANDARD_ASPECT_RATIO)
					fov = RAD2DEG( 2 * atan( (aspectRatio / STANDARD_ASPECT_RATIO) * tan(DEG2RAD(fov) * 0.5) ) );
				fov = min(fov, 160);
			}
			// end Knightmare
			
			

			x = cg.refdef.width / tan( fov / 360 * M_PI );
			cg.refdef.fov_y = atan2( cg.refdef.height, x );
			cg.refdef.fov_y = cg.refdef.fov_y * 360 / M_PI;
			cg.refdef.fov_x = fov;

			originDelta = oldOriginValid ? DistanceSquared( origin, oldOrigin ) : 999999.0f;
			sendCameraOrigin = !oldOriginValid || originDelta > 4096.0f || ( originDelta > 0.25f && ( cg.time < oldOriginTime || cg.time - oldOriginTime >= 50 ) );
			if ( sendCameraOrigin ) {
				VectorCopy( origin, oldOrigin );
				oldOriginValid = qtrue;
				oldOriginTime = cg.time;
				trap_SendClientCommand( va( "setCameraOrigin %f %f %f", origin[0], origin[1], origin[2] ) );
			}
			return 0;

		} else {
			cg.cameraMode = qfalse;                 // camera off in cgame
			oldOriginValid = qfalse;
			oldOriginTime = 0;
			trap_Cvar_Set( "cg_letterbox", "0" );
			trap_SendClientCommand( "stopCamera" );    // camera off in game
			trap_stopCamera( CAM_PRIMARY );           // camera off in client

			CG_Fade( 0, 0, 0, 255, 0, 0 );                // go black
			CG_Fade( 0, 0, 0, 0, cg.time + 200, 1500 );   // then fadeup
		}
	}
	oldOriginValid = qfalse;
	oldOriginTime = 0;

	// intermission view
	if ( ps->pm_type == PM_INTERMISSION ) {
		VectorCopy( ps->origin, cg.refdef.vieworg );
		VectorCopy( ps->viewangles, cg.refdefViewAngles );
		AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );
		return CG_CalcFov();
	}

	cg.bobcycle = ( ps->bobCycle & 128 ) >> 7;
	cg.bobfracsin = fabs( sin( ( ps->bobCycle & 127 ) / 127.0 * M_PI ) );
	cg.xyspeed = sqrt( ps->velocity[0] * ps->velocity[0] +
					   ps->velocity[1] * ps->velocity[1] );


	VectorCopy( ps->origin, cg.refdef.vieworg );
	VectorCopy( ps->viewangles, cg.refdefViewAngles );

	// add error decay
	if ( cg_errorDecay.value > 0 ) {
		int t;
		float f;

		t = cg.time - cg.predictedErrorTime;
		f = ( cg_errorDecay.value - t ) / cg_errorDecay.value;
		if ( f > 0 && f < 1 ) {
			VectorMA( cg.refdef.vieworg, f, cg.predictedError, cg.refdef.vieworg );
		} else {
			cg.predictedErrorTime = 0;
		}
	}

	// Ridah, lock the viewangles if the game has told us to
	if ( ps->viewlocked ) {

		/*
		if (ps->viewlocked == 4)
		{
			centity_t *tent;
			tent = &cg_entities[ps->viewlocked_entNum];
			VectorCopy (tent->currentState.apos.trBase, cg.refdefViewAngles);
		}
		else
		*/
		BG_EvaluateTrajectory( &cg_entities[ps->viewlocked_entNum].currentState.apos, cg.time, cg.refdefViewAngles );

		if ( ps->viewlocked == 2 ) {
			cg.refdefViewAngles[0] += crandom();
			cg.refdefViewAngles[1] += crandom();
		}
	}
	// done.

	if ( cg.renderingThirdPerson ) {
		// back away from character
		CG_OffsetThirdPersonView();
	} else {
		// offset for local bobbing and kicks
		CG_OffsetFirstPersonView();

		// Ridah, lock the viewangles if the game has told us to
		if ( ps->viewlocked == 4 ) {
			vec3_t fwd;
			AngleVectors( cg.refdefViewAngles, fwd, NULL, NULL );
			VectorMA( cg_entities[ps->viewlocked_entNum].currentState.pos.trBase, 16, fwd, cg.refdef.vieworg );
		} else if ( ps->viewlocked )     {
			vec3_t fwd;
			float oldZ;
			// set our position to be behind it
			oldZ = cg.refdef.vieworg[2];
			AngleVectors( cg.refdefViewAngles, fwd, NULL, NULL );
			VectorMA( cg_entities[ps->viewlocked_entNum].currentState.pos.trBase, -34, fwd, cg.refdef.vieworg );
			cg.refdef.vieworg[2] = oldZ;
		}
		// done.
	}

	// position eye reletive to origin
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );

	if ( cg.hyperspace ) {
		cg.refdef.rdflags |= RDF_HYPERSPACE;
	}

	// field of view
	return CG_CalcFov();
}


/*
=====================
CG_PowerupTimerSounds
=====================
*/
static void CG_PowerupTimerSounds( void ) {
	int i;
	int t;

	// powerup timers going away
	for ( i = 0 ; i < MAX_POWERUPS ; i++ ) {
		t = cg.snap->ps.powerups[i];
		if ( t <= cg.time ) {
			continue;
		}
		if ( t - cg.time >= POWERUP_BLINKS * POWERUP_BLINK_TIME ) {
			continue;
		}
		if ( ( t - cg.time ) / POWERUP_BLINK_TIME != ( t - cg.oldTime ) / POWERUP_BLINK_TIME ) {
			trap_S_StartSound( NULL, cg.snap->ps.clientNum, CHAN_ITEM, cgs.media.wearOffSound );
		}
	}
}

//=========================================================================

/*
==============
CG_DrawSkyBoxPortal
==============
*/
void CG_DrawSkyBoxPortal( void ) {
	static float lastfov = 90;      // for transitions back from zoomed in modes
	refdef_t backuprefdef;
	float fov_x;
	float fov_y;
	float x;
	char *cstr;
	char *token;
	float zoomFov;
	float f;
	static qboolean foginited = qfalse; // only set the portal fog values once

	if ( !( cstr = (char *)CG_ConfigString( CS_SKYBOXORG ) ) || !strlen( cstr ) ) {
		// no skybox in this map
		return;
	}

	// if they are waiting at the mission stats screen, show the stats
	if ( cg_gameType.integer == GT_SINGLE_PLAYER ) {
		if ( strlen( cg_missionStats.string ) > 1 ) {
			return;
		}
	}

	backuprefdef = cg.refdef;

	if ( cg_skybox.integer ) {
		token = COM_ParseExt( &cstr, qfalse );
		if ( !token || !token[0] ) {
			CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring\n" );
		}
		cg.refdef.vieworg[0] = atof( token );

		token = COM_ParseExt( &cstr, qfalse );
		if ( !token || !token[0] ) {
			CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring\n" );
		}
		cg.refdef.vieworg[1] = atof( token );

		token = COM_ParseExt( &cstr, qfalse );
		if ( !token || !token[0] ) {
			CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring\n" );
		}
		cg.refdef.vieworg[2] = atof( token );

		token = COM_ParseExt( &cstr, qfalse );
		if ( !token || !token[0] ) {
			CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring\n" );
		}
		fov_x = atoi( token );

		if ( !fov_x ) {
			fov_x = 90;
		}


		// setup fog the first time, ignore this part of the configstring after that
		token = COM_ParseExt( &cstr, qfalse );
		if ( !token || !token[0] ) {
			CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring.  No fog state\n" );
		} else {
			vec4_t fogColor;
			int fogStart, fogEnd;

			if ( atoi( token ) ) {   // this camera has fog
				//			if(!foginited) {
				if ( 1 ) {
					token = COM_ParseExt( &cstr, qfalse );
					if ( !token || !token[0] ) {
						CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring.  No fog[0]\n" );
					}
					fogColor[0] = atof( token );

					token = COM_ParseExt( &cstr, qfalse );
					if ( !token || !token[0] ) {
						CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring.  No fog[1]\n" );
					}
					fogColor[1] = atof( token );

					token = COM_ParseExt( &cstr, qfalse );
					if ( !token || !token[0] ) {
						CG_Error( "CG_DrawSkyBoxPortal: error parsing skybox configstring.  No fog[2]\n" );
					}
					fogColor[2] = atof( token );

					token = COM_ParseExt( &cstr, qfalse );
					if ( !token || !token[0] ) {
						fogStart = 0;
					} else {
						fogStart = atoi( token );
					}

					token = COM_ParseExt( &cstr, qfalse );
					if ( !token || !token[0] ) {
						fogEnd = 0;
					} else {
						fogEnd = atoi( token );
					}

					trap_R_SetFog( FOG_PORTALVIEW, fogStart, fogEnd, fogColor[0], fogColor[1], fogColor[2], 1.1 );
					foginited = qtrue;
				}
			} else {
				if ( !foginited ) {
					trap_R_SetFog( FOG_PORTALVIEW, 0,0,0,0,0,0 ); // init to null
					foginited = qtrue;
				}
			}
		}

		//----(SA)	end


		if ( cg.predictedPlayerState.pm_type == PM_INTERMISSION ) {
			// if in intermission, use a fixed value
			fov_x = 90;
		} else {
			// user selectable
			if ((cgs.dmflags & DF_FIXED_FOV)) {
				// dmflag to prevent wide fov for all clients
				fov_x = 90;
			} else {
				fov_x = cg_fov.value;
				if ( fov_x < 1 ) {
					fov_x = 1;
				} else if ( fov_x > 160 ) {
					fov_x = 160;
				}
			}

			// account for zooms
			if ( cg.zoomval ) {
				zoomFov = cg.zoomval;   // (SA) use user scrolled amount

				if ( zoomFov < 1 ) {
					zoomFov = 1;
				} else if ( zoomFov > 160 ) {
					zoomFov = 160;
				}
			} else {
				zoomFov = lastfov;
			}

			// do smooth transitions for the binocs
			if ( cg.zoomedBinoc ) {        // binoc zooming in
				f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
				if ( f > 1.0 ) {
					fov_x = zoomFov;
				} else {
					fov_x = fov_x + f * ( zoomFov - fov_x );
				}
				lastfov = fov_x;
			} else if ( cg.zoomval ) {    // zoomed by sniper/snooper
				fov_x = cg.zoomval;
				lastfov = fov_x;
			} else {                    // binoc zooming out
				f = ( cg.time - cg.zoomTime ) / (float)ZOOM_TIME;
				if ( f > 1.0 ) {
					fov_x = fov_x;
				} else {
					fov_x = zoomFov + f * ( fov_x - zoomFov );
				}
			}
		}

		if ( cg.weaponSelect == WP_SNOOPERSCOPE ) {
			cg.refdef.rdflags |= RDF_SNOOPERVIEW;
		} else {
			cg.refdef.rdflags &= ~RDF_SNOOPERVIEW;
		}

		if ( cg.snap->ps.persistant[PERS_HWEAPON_USE] ) {
			fov_x = 55;
		}

		// Knightmare- adjust fov_x for wide screen aspect
		if (cg_widescreen_fov.value)
		{
			float aspectRatio = (float)cg.refdef.width/(float)cg.refdef.height;
			if (aspectRatio > STANDARD_ASPECT_RATIO)
				fov_x = RAD2DEG( 2 * atan( (aspectRatio / STANDARD_ASPECT_RATIO) * tan(DEG2RAD(fov_x) * 0.5) ) );
			fov_x = min(fov_x, 160);
		}
		// end Knightmare



		x = cg.refdef.width / tan( fov_x / 360 * M_PI );
		fov_y = atan2( cg.refdef.height, x );
		fov_y = fov_y * 360 / M_PI;

		cg.refdef.fov_x = fov_x;
		cg.refdef.fov_y = fov_y;

		cg.refdef.rdflags |= RDF_SKYBOXPORTAL;
		cg.refdef.rdflags |= RDF_DRAWSKYBOX;

	} else {    // end if(cg_skybox.integer)

		cg.refdef.rdflags |= RDF_SKYBOXPORTAL;
		cg.refdef.rdflags &= ~RDF_DRAWSKYBOX;
	}


	cg.refdef.time = cg.time;

	// draw the skybox
	trap_R_RenderScene( &cg.refdef );

	cg.refdef = backuprefdef;
}

/*
=========================
removed CG_DrawNotebook
=========================
*/


//=========================================================================

extern void CG_SetupDlightstyles( void );


//#define DEBUGTIME_ENABLED
#ifdef DEBUGTIME_ENABLED
#define DEBUGTIME CG_Printf( "t%i:%i ", dbgCnt++, elapsed = ( trap_Milliseconds() - dbgTime ) ); dbgTime += elapsed;
#else
#define DEBUGTIME
#endif

/*
=================
CG_DrawActiveFrame

Generates and draws a game scene and status information at the given time.
=================
*/
void CG_DrawActiveFrame( int serverTime, stereoFrame_t stereoView, qboolean demoPlayback ) {
	int inwater;

	cg.cld = 0;         // NERVE - SMF - reset clientDamage

#ifdef DEBUGTIME_ENABLED
	int dbgTime = trap_Milliseconds(),elapsed;
	int dbgCnt = 0;
#endif

	cg.time = serverTime;
	cg.demoPlayback = demoPlayback;

	// update cvars
	CG_UpdateCvars();
	// RF, if we should force a weapon, then do so
	if ( cg_loadWeaponSelect.integer > WP_NONE && cg_loadWeaponSelect.integer < WP_NUM_WEAPONS ) {
		if ( cg.weaponSelect != cg_loadWeaponSelect.integer ) {
			cg.weaponSelect = cg_loadWeaponSelect.integer;
			cg.weaponSelectTime = cg.time;
		}
		trap_Cvar_Set( "cg_loadWeaponSelect", "0" );	// turn it off
	}
#ifdef DEBUGTIME_ENABLED
	CG_Printf( "\n" );
#endif
	DEBUGTIME

	// if we are only updating the screen as a loading
	// pacifier, don't even try to read snapshots
	if ( cg.infoScreenText[0] != 0 ) {
		CG_DrawInformation();
		return;
	}

	// any looped sounds will be respecified as entities
	// are added to the render list
	trap_S_ClearLoopingSounds( qfalse );

	DEBUGTIME

	// clear all the render lists
	trap_R_ClearScene();

	DEBUGTIME

	// set up cg.snap and possibly cg.nextSnap
	CG_ProcessSnapshots();

	DEBUGTIME

	// if we haven't received any snapshots yet, all
	// we can draw is the information screen
	if ( !cg.snap || ( cg.snap->snapFlags & SNAPFLAG_NOT_ACTIVE ) ) {
		CG_DrawInformation();
		return;
	}

	if ( cg.weaponSelect == WP_FG42SCOPE || cg.weaponSelect == WP_SNOOPERSCOPE || cg.weaponSelect == WP_SNIPERRIFLE ) {
		float spd;
		spd = VectorLength( cg.snap->ps.velocity );
		if ( spd > 180.0f ) {
			switch ( cg.weaponSelect ) {
			case WP_FG42SCOPE:
				CG_FinishWeaponChange( cg.weaponSelect, WP_FG42 );
				break;
			case WP_SNOOPERSCOPE:
				CG_FinishWeaponChange( cg.weaponSelect, WP_GARAND );
				break;
			case WP_SNIPERRIFLE:
				CG_FinishWeaponChange( cg.weaponSelect, WP_MAUSER );
				break;
			}
		}
	}

	DEBUGTIME

	if ( !cg.lightstylesInited ) {
		CG_SetupDlightstyles();
	}

	DEBUGTIME

	// if we have been told not to render, don't
	// During demo playback, always render regardless of cg_norender -
	// the server-side AICast code and "rockandroll" command set it to 1
	// and nothing clears it without a real game module running.
	if ( cg_norender.integer && !cg.demoPlayback ) {
		return;
	}

	// this counter will be bumped for every valid scene we generate
	cg.clientFrame++;

	// update cg.predictedPlayerState
	CG_PredictPlayerState();

	// update jump statistics (needs snap data)
	CG_UpdateJumpStats();
	CG_UpdateMovementBar();

	DEBUGTIME

	// decide on third person view
	cg.renderingThirdPerson = cg_thirdPerson.integer /*|| (cg.snap->ps.stats[STAT_HEALTH] <= 0)*/;

	// build cg.refdef
	inwater = CG_CalcViewValues();

	CG_CalcShakeCamera();
	CG_ApplyShakeCamera();

	/* Demo freecam: override cg.refdef BEFORE entities are added so
	   that all distance/LOD/shadow calculations use the camera position
	   rather than the player's.  The engine-side override in cl_cgame.c
	   happens later at RenderScene time; this early override ensures
	   the cgame also works from the correct viewpoint. */
	{
		if ( cg_freecamActive.integer ) {
			float x, y, z, pitch, yaw, roll;
			vec3_t fcAngles;
			if ( sscanf( cg_freecamPos.string, "%f %f %f", &x, &y, &z ) == 3 ) {
				cg.refdef.vieworg[0] = x;
				cg.refdef.vieworg[1] = y;
				cg.refdef.vieworg[2] = z;
			}
			if ( sscanf( cg_freecamAngles.string, "%f %f %f", &pitch, &yaw, &roll ) == 3 ) {
				fcAngles[0] = pitch;
				fcAngles[1] = yaw;
				fcAngles[2] = roll;
				AnglesToAxis( fcAngles, cg.refdef.viewaxis );
			}
			memset( cg.refdef.areamask, 0, sizeof( cg.refdef.areamask ) );
		}
	}

	DEBUGTIME

	// RF, draw the skyboxportal
	CG_DrawSkyBoxPortal();

	DEBUGTIME

	if ( inwater ) {
		CG_UnderwaterSounds();
	}

	DEBUGTIME

	// first person blend blobs, done after AnglesToAxis
	if ( !cg.renderingThirdPerson ) {
		CG_DamageBlendBlob();
	}

	DEBUGTIME

	// build the render lists
	if ( !cg.hyperspace ) {
		CG_AddPacketEntities();         // adter calcViewValues, so predicted player state is correct
		CG_AddGhost();                  // ghost replay from best split
		CG_AddRaceGhosts();             // live remote race players
		CG_AddMarks();

		DEBUGTIME

		// Rafael particles
		CG_AddParticles();
		// done.

		DEBUGTIME

		CG_AddLocalEntities();

		DEBUGTIME
	}


	CG_AddViewWeapon( &cg.predictedPlayerState );


	DEBUGTIME

	// Ridah, trails
	if ( !cg.hyperspace ) {
		CG_AddFlameChunks();
		CG_AddTrails();         // this must come last, so the trails dropped this frame get drawn
	}
	// done.

	DEBUGTIME

	// finish up the rest of the refdef
	if ( cg.testModelEntity.hModel ) {
		CG_AddTestModel();
	}
	cg.refdef.time = cg.time;
	memcpy( cg.refdef.areamask, cg.snap->areamask, sizeof( cg.refdef.areamask ) );
	if ( cg_freecamActive.integer ) {
		memset( cg.refdef.areamask, 0, sizeof( cg.refdef.areamask ) );
	}

	DEBUGTIME

	// warning sounds when powerup is wearing off
	CG_PowerupTimerSounds();

	// make sure the lagometerSample and frame timing isn't done twice when in stereo
	if ( stereoView != STEREO_RIGHT ) {
		cg.frametime = cg.time - cg.oldTime;
		if ( cg.frametime < 0 ) {
			cg.frametime = 0;
		}
		cg.oldTime = cg.time;
		CG_AddLagometerFrameInfo();
	}

	DEBUGTIME

	// let the client system know what our weapon, holdable item and zoom settings are
	trap_SetUserCmdValue( cg.weaponSelect, cg.holdableSelect, cg.zoomSensitivity, cg.cld );

	// actually issue the rendering calls
	CG_DrawActive( stereoView );

	DEBUGTIME

	// update audio positions
	trap_S_Respatialize( cg.snap->ps.clientNum, cg.refdef.vieworg, cg.refdef.viewaxis, inwater );

	if ( cg_stats.integer ) {
		CG_Printf( "cg.clientFrame:%i\n", cg.clientFrame );
	}

	DEBUGTIME
}
