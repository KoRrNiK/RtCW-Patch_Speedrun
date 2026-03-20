/*
===========================================================================
cg_triggervis.c - Brush Volume Visualization

Parses BSP entity string at map load to find ALL brush entities
(any entity with model "*N"), then renders them as colored transparent
3D boxes.  Different entity types get different colors.

Usage:  cg_drawTriggers 1          (default alpha 80)
        cg_drawTriggers 2..255     (explicit alpha value)

Colors:
  trigger_multiple       = green          func_door/_rotating = dark blue
  trigger_once           = blue           func_button         = light blue
  trigger_push           = yellow         func_explosive      = dark red
  trigger_teleport       = cyan           func_secret         = gold
  trigger_hurt           = red            func_plat           = steel
  trigger_objective_info = orange         func_train/_rot     = dark cyan
  trigger_flagonly       = purple         func_rotating       = magenta
  trigger_aidoor         = olive          func_static         = dark gray
  trigger_deathCheck     = pink           func_bobbing        = lime
  trigger_always         = gray           func_pendulum       = lavender
  func_invisible_user    = teal           func_tramcar        = tan
  ai_trigger             = brown          props_*             = salmon
  script_mover           = dark green     other               = white
===========================================================================
*/

#include "cg_local.h"

#define MAX_TRIGGER_VIS     1024
#define TRIGVIS_MAX_DIST    3000.0f

// ---- entity type enum ----
typedef enum {
	// trigger_ types
	TRIG_MULTIPLE,
	TRIG_ONCE,
	TRIG_PUSH,
	TRIG_TELEPORT,
	TRIG_HURT,
	TRIG_OBJECTIVE_INFO,
	TRIG_FLAGONLY,
	TRIG_AIDOOR,
	TRIG_DEATHCHECK,
	TRIG_ALWAYS,
	// func_ types
	TRIG_FUNC_INVISIBLE_USER,
	TRIG_FUNC_DOOR,
	TRIG_FUNC_BUTTON,
	TRIG_FUNC_EXPLOSIVE,
	TRIG_FUNC_SECRET,
	TRIG_FUNC_PLAT,
	TRIG_FUNC_TRAIN,
	TRIG_FUNC_ROTATING,
	TRIG_FUNC_STATIC,
	TRIG_FUNC_BOBBING,
	TRIG_FUNC_PENDULUM,
	TRIG_FUNC_TRAMCAR,
	// script / AI
	TRIG_AI_TRIGGER,
	TRIG_SCRIPT_MOVER,
	// props
	TRIG_PROPS,
	// catch-all
	TRIG_OTHER
} trigType_t;

typedef struct {
	vec3_t		absmin, absmax;		// world-space bounds
	trigType_t	type;
	qboolean	active;
	char		classname[64];		// entity classname for labels
	char		targetname[64];		// entity targetname for labels
	char		target[64];			// what this entity targets
	char		message[64];		// message/script_action etc.
	char		scriptName[64];		// scriptName for scripted entities
	char		aiName[64];			// aiName for AI-related triggers
	int			modelIndex;			// brush model index (*N)
	int			spawnflags;			// entity spawnflags
	float		wait;				// wait value (for triggers), float for 0.5 etc.
	int			dmg;				// damage (for trigger_hurt, func_explosive)
	float		speed;				// speed (doors, platforms, push)
	int			count;				// count/objective number
	int			key;				// key required (locked doors)
	float		delay;				// delay before activation
	int			health;				// health (breakable, trigger conditions)
} triggerVis_t;

static triggerVis_t tvTriggers[MAX_TRIGGER_VIS];
static int tvNumTriggers = 0;
qhandle_t tvShader = 0;
qhandle_t tvBorderShader = 0;

/*
==================
TrigVis_ClassifyType
==================
*/
static trigType_t TrigVis_ClassifyType( const char *classname ) {
	// trigger_ types
	if ( !Q_stricmp( classname, "trigger_multiple" ) )       return TRIG_MULTIPLE;
	if ( !Q_stricmp( classname, "trigger_once" ) )           return TRIG_ONCE;
	if ( !Q_stricmp( classname, "trigger_push" ) )           return TRIG_PUSH;
	if ( !Q_stricmp( classname, "trigger_teleport" ) )       return TRIG_TELEPORT;
	if ( !Q_stricmp( classname, "trigger_hurt" ) )           return TRIG_HURT;
	if ( !Q_stricmp( classname, "trigger_objective_info" ) ) return TRIG_OBJECTIVE_INFO;
	if ( !Q_stricmp( classname, "trigger_flagonly" ) )       return TRIG_FLAGONLY;
	if ( !Q_stricmp( classname, "trigger_aidoor" ) )         return TRIG_AIDOOR;
	if ( !Q_stricmp( classname, "trigger_deathCheck" ) )     return TRIG_DEATHCHECK;
	if ( !Q_stricmp( classname, "trigger_always" ) )         return TRIG_ALWAYS;
	// func_ types
	if ( !Q_stricmp( classname, "func_invisible_user" ) )    return TRIG_FUNC_INVISIBLE_USER;
	if ( !Q_stricmp( classname, "func_door" ) )              return TRIG_FUNC_DOOR;
	if ( !Q_stricmp( classname, "func_door_rotating" ) )     return TRIG_FUNC_DOOR;
	if ( !Q_stricmp( classname, "func_button" ) )            return TRIG_FUNC_BUTTON;
	if ( !Q_stricmp( classname, "func_explosive" ) )         return TRIG_FUNC_EXPLOSIVE;
	if ( !Q_stricmp( classname, "func_secret" ) )            return TRIG_FUNC_SECRET;
	if ( !Q_stricmp( classname, "func_plat" ) )              return TRIG_FUNC_PLAT;
	if ( !Q_stricmp( classname, "func_train" ) )             return TRIG_FUNC_TRAIN;
	if ( !Q_stricmp( classname, "func_train_rotating" ) )    return TRIG_FUNC_TRAIN;
	if ( !Q_stricmp( classname, "func_train_particles" ) )   return TRIG_FUNC_TRAIN;
	if ( !Q_stricmp( classname, "func_rotating" ) )          return TRIG_FUNC_ROTATING;
	if ( !Q_stricmp( classname, "func_static" ) )            return TRIG_FUNC_STATIC;
	if ( !Q_stricmp( classname, "func_leaky" ) )             return TRIG_FUNC_STATIC;
	if ( !Q_stricmp( classname, "func_bobbing" ) )           return TRIG_FUNC_BOBBING;
	if ( !Q_stricmp( classname, "func_pendulum" ) )          return TRIG_FUNC_PENDULUM;
	if ( !Q_stricmp( classname, "func_tramcar" ) )           return TRIG_FUNC_TRAMCAR;
	// script / AI
	if ( !Q_stricmp( classname, "ai_trigger" ) )             return TRIG_AI_TRIGGER;
	if ( !Q_stricmp( classname, "script_mover" ) )           return TRIG_SCRIPT_MOVER;
	// props
	if ( !Q_stricmpn( classname, "props_", 6 ) )             return TRIG_PROPS;
	return TRIG_OTHER;
}

/*
==================
TrigVis_GetColor

Returns RGBA color for a trigger type.
Alpha is set to the user-controlled opacity.
==================
*/
static void TrigVis_GetColor( trigType_t type, byte color[4], byte alpha ) {
	switch ( type ) {
		// triggers
		case TRIG_MULTIPLE:            color[0]=0;   color[1]=200; color[2]=0;   break;  // green
		case TRIG_ONCE:                color[0]=50;  color[1]=120; color[2]=255; break;  // blue
		case TRIG_PUSH:                color[0]=255; color[1]=255; color[2]=0;   break;  // yellow
		case TRIG_TELEPORT:            color[0]=0;   color[1]=255; color[2]=255; break;  // cyan
		case TRIG_HURT:                color[0]=255; color[1]=30;  color[2]=30;  break;  // red
		case TRIG_OBJECTIVE_INFO:      color[0]=255; color[1]=165; color[2]=0;   break;  // orange
		case TRIG_FLAGONLY:            color[0]=200; color[1]=0;   color[2]=255; break;  // purple
		case TRIG_AIDOOR:              color[0]=180; color[1]=180; color[2]=0;   break;  // olive
		case TRIG_DEATHCHECK:          color[0]=255; color[1]=100; color[2]=180; break;  // pink
		case TRIG_ALWAYS:              color[0]=160; color[1]=160; color[2]=160; break;  // gray
		// func_ types
		case TRIG_FUNC_INVISIBLE_USER: color[0]=0;   color[1]=180; color[2]=180; break;  // teal
		case TRIG_FUNC_DOOR:           color[0]=30;  color[1]=50;  color[2]=180; break;  // dark blue
		case TRIG_FUNC_BUTTON:         color[0]=100; color[1]=180; color[2]=255; break;  // light blue
		case TRIG_FUNC_EXPLOSIVE:      color[0]=180; color[1]=20;  color[2]=20;  break;  // dark red
		case TRIG_FUNC_SECRET:         color[0]=255; color[1]=215; color[2]=0;   break;  // gold
		case TRIG_FUNC_PLAT:           color[0]=140; color[1]=150; color[2]=170; break;  // steel
		case TRIG_FUNC_TRAIN:          color[0]=0;   color[1]=140; color[2]=140; break;  // dark cyan
		case TRIG_FUNC_ROTATING:       color[0]=200; color[1]=0;   color[2]=180; break;  // magenta
		case TRIG_FUNC_STATIC:         color[0]=80;  color[1]=80;  color[2]=80;  break;  // dark gray
		case TRIG_FUNC_BOBBING:        color[0]=100; color[1]=255; color[2]=50;  break;  // lime
		case TRIG_FUNC_PENDULUM:       color[0]=180; color[1]=160; color[2]=255; break;  // lavender
		case TRIG_FUNC_TRAMCAR:        color[0]=210; color[1]=180; color[2]=140; break;  // tan
		// script / AI
		case TRIG_AI_TRIGGER:          color[0]=160; color[1]=100; color[2]=40;  break;  // brown
		case TRIG_SCRIPT_MOVER:        color[0]=0;   color[1]=140; color[2]=60;  break;  // dark green
		// props
		case TRIG_PROPS:               color[0]=250; color[1]=128; color[2]=114; break;  // salmon
		// other
		default:                       color[0]=200; color[1]=200; color[2]=200; break;  // white
	}
	color[3] = alpha;
}

/*
==================
TrigVis_DrawBoxFace

Draws a single quad face of a box.
==================
*/
static void TrigVis_DrawBoxFace( vec3_t v0, vec3_t v1, vec3_t v2, vec3_t v3,
								 byte color[4], qhandle_t shader ) {
	polyVert_t verts[4];
	int i;

	VectorCopy( v0, verts[0].xyz );
	VectorCopy( v1, verts[1].xyz );
	VectorCopy( v2, verts[2].xyz );
	VectorCopy( v3, verts[3].xyz );

	for ( i = 0; i < 4; i++ ) {
		verts[i].st[0] = 0;
		verts[i].st[1] = 0;
		verts[i].modulate[0] = color[0];
		verts[i].modulate[1] = color[1];
		verts[i].modulate[2] = color[2];
		verts[i].modulate[3] = color[3];
	}

	trap_R_AddPolyToScene( shader, 4, verts );
}

/*
==================
TrigVis_DrawBorderedFace

Draws a single face with a bright border frame and darker inner fill.
v0..v3 are the 4 corners in winding order.
borderFrac = fraction of each edge used for border width (e.g. 0.06 = 6%).
==================
*/
static void TrigVis_DrawBorderedFace( vec3_t v0, vec3_t v1, vec3_t v2, vec3_t v3,
									  byte borderColor[4], byte fillColor[4],
									  float borderFrac, qhandle_t fillShader,
									  qhandle_t borderShader ) {
	vec3_t i0, i1, i2, i3;  // inner corners
	float bf = borderFrac;

	// Compute inner corners by lerping each corner towards the opposite diagonal
	// i0 = v0 + bf*(v1-v0) + bf*(v3-v0)  etc.
	// Actually simpler: lerp along each edge pair

	// i0 = lerp from v0 towards center
	i0[0] = v0[0] + bf * ( v1[0] - v0[0] ) + bf * ( v3[0] - v0[0] );
	i0[1] = v0[1] + bf * ( v1[1] - v0[1] ) + bf * ( v3[1] - v0[1] );
	i0[2] = v0[2] + bf * ( v1[2] - v0[2] ) + bf * ( v3[2] - v0[2] );

	i1[0] = v1[0] + bf * ( v0[0] - v1[0] ) + bf * ( v2[0] - v1[0] );
	i1[1] = v1[1] + bf * ( v0[1] - v1[1] ) + bf * ( v2[1] - v1[1] );
	i1[2] = v1[2] + bf * ( v0[2] - v1[2] ) + bf * ( v2[2] - v1[2] );

	i2[0] = v2[0] + bf * ( v3[0] - v2[0] ) + bf * ( v1[0] - v2[0] );
	i2[1] = v2[1] + bf * ( v3[1] - v2[1] ) + bf * ( v1[1] - v2[1] );
	i2[2] = v2[2] + bf * ( v3[2] - v2[2] ) + bf * ( v1[2] - v2[2] );

	i3[0] = v3[0] + bf * ( v2[0] - v3[0] ) + bf * ( v0[0] - v3[0] );
	i3[1] = v3[1] + bf * ( v2[1] - v3[1] ) + bf * ( v0[1] - v3[1] );
	i3[2] = v3[2] + bf * ( v2[2] - v3[2] ) + bf * ( v0[2] - v3[2] );

	// Draw inner fill (darker)
	TrigVis_DrawBoxFace( i0, i1, i2, i3, fillColor, fillShader );

	// Draw 4 border strips (bright glow frame)
	// Bottom strip: v0, v1, i1, i0
	TrigVis_DrawBoxFace( v0, v1, i1, i0, borderColor, borderShader );
	// Right strip: v1, v2, i2, i1
	TrigVis_DrawBoxFace( v1, v2, i2, i1, borderColor, borderShader );
	// Top strip: v2, v3, i3, i2
	TrigVis_DrawBoxFace( v2, v3, i3, i2, borderColor, borderShader );
	// Left strip: v3, v0, i0, i3
	TrigVis_DrawBoxFace( v3, v0, i0, i3, borderColor, borderShader );
}

#define BORDER_FRAC  0.015f

/*
==================
TrigVis_DrawBox

Draws a box with bordered faces - bright frame edges, darker fill center.
==================
*/
void TrigVis_DrawBox( vec3_t mins, vec3_t maxs, byte fillColor[4],
					 byte borderColor[4], qhandle_t fillShader,
					 qhandle_t borderShader ) {
	vec3_t c[8];

	// 8 corners of the AABB
	VectorSet( c[0], mins[0], mins[1], mins[2] );
	VectorSet( c[1], maxs[0], mins[1], mins[2] );
	VectorSet( c[2], maxs[0], maxs[1], mins[2] );
	VectorSet( c[3], mins[0], maxs[1], mins[2] );
	VectorSet( c[4], mins[0], mins[1], maxs[2] );
	VectorSet( c[5], maxs[0], mins[1], maxs[2] );
	VectorSet( c[6], maxs[0], maxs[1], maxs[2] );
	VectorSet( c[7], mins[0], maxs[1], maxs[2] );

	// 6 bordered faces
	TrigVis_DrawBorderedFace( c[3], c[2], c[1], c[0], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // bottom
	TrigVis_DrawBorderedFace( c[4], c[5], c[6], c[7], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // top
	TrigVis_DrawBorderedFace( c[0], c[1], c[5], c[4], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // front
	TrigVis_DrawBorderedFace( c[2], c[3], c[7], c[6], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // back
	TrigVis_DrawBorderedFace( c[3], c[0], c[4], c[7], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // left
	TrigVis_DrawBorderedFace( c[1], c[2], c[6], c[5], borderColor, fillColor, BORDER_FRAC, fillShader, borderShader );  // right
}

/*
==================
CG_InitTriggerVis

Parse BSP entity string at map load to find ALL brush entities.
Any entity with model "*N" gets a colored box visualization.
Call this AFTER CG_RegisterGraphics() so inline models are available.
==================
*/
void CG_InitTriggerVis( void ) {
	char token[MAX_TOKEN_CHARS];
	char classname[MAX_TOKEN_CHARS];
	char targetname[MAX_TOKEN_CHARS];
	char target[MAX_TOKEN_CHARS];
	char message[MAX_TOKEN_CHARS];
	char scriptName[MAX_TOKEN_CHARS];
	char aiName[MAX_TOKEN_CHARS];
	char modelStr[MAX_TOKEN_CHARS];
	char originStr[MAX_TOKEN_CHARS];
	int spawnflags, dmg, count, key, health;
	float wait, speed, delay;
	int modelIndex;
	vec3_t origin;

	tvNumTriggers = 0;
	memset( tvTriggers, 0, sizeof( tvTriggers ) );

	// Register the transparent shader (defined in renderer as an internal shader)
	tvShader = trap_R_RegisterShader( "triggerVisAlpha" );
	if ( !tvShader ) {
		tvShader = cgs.media.whiteShader;  // fallback
	}

	// Register the additive border glow shader
	tvBorderShader = trap_R_RegisterShader( "triggerVisBorderGlow" );
	if ( !tvBorderShader ) {
		tvBorderShader = tvShader;  // fallback to normal alpha
	}

	// Parse the BSP entity string - grab ALL entities with brush model (*N)
	while ( 1 ) {
		if ( !trap_GetEntityToken( token, sizeof( token ) ) ) {
			break;
		}
		if ( token[0] != '{' ) {
			continue;
		}

		classname[0] = 0;
		targetname[0] = 0;
		target[0] = 0;
		message[0] = 0;
		scriptName[0] = 0;
		aiName[0] = 0;
		modelStr[0] = 0;
		originStr[0] = 0;
		spawnflags = 0;
		wait = 0;
		dmg = 0;
		speed = 0;
		count = 0;
		key = 0;
		delay = 0;
		health = 0;

		// Parse key/value pairs
		while ( 1 ) {
			if ( !trap_GetEntityToken( token, sizeof( token ) ) ) {
				break;
			}
			if ( token[0] == '}' ) {
				break;
			}

			// key
			if ( !Q_stricmp( token, "classname" ) ) {
				trap_GetEntityToken( classname, sizeof( classname ) );
			} else if ( !Q_stricmp( token, "targetname" ) ) {
				trap_GetEntityToken( targetname, sizeof( targetname ) );
			} else if ( !Q_stricmp( token, "target" ) ) {
				trap_GetEntityToken( target, sizeof( target ) );
			} else if ( !Q_stricmp( token, "message" ) ) {
				trap_GetEntityToken( message, sizeof( message ) );
			} else if ( !Q_stricmp( token, "scriptName" ) ) {
				trap_GetEntityToken( scriptName, sizeof( scriptName ) );
			} else if ( !Q_stricmp( token, "ainame" ) ) {
				trap_GetEntityToken( aiName, sizeof( aiName ) );
			} else if ( !Q_stricmp( token, "model" ) ) {
				trap_GetEntityToken( modelStr, sizeof( modelStr ) );
			} else if ( !Q_stricmp( token, "origin" ) ) {
				trap_GetEntityToken( originStr, sizeof( originStr ) );
			} else if ( !Q_stricmp( token, "spawnflags" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				spawnflags = atoi( token );
			} else if ( !Q_stricmp( token, "wait" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				wait = atof( token );
			} else if ( !Q_stricmp( token, "dmg" ) || !Q_stricmp( token, "damage" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				dmg = atoi( token );
			} else if ( !Q_stricmp( token, "speed" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				speed = atof( token );
			} else if ( !Q_stricmp( token, "count" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				count = atoi( token );
			} else if ( !Q_stricmp( token, "key" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				key = atoi( token );
			} else if ( !Q_stricmp( token, "delay" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				delay = atof( token );
			} else if ( !Q_stricmp( token, "health" ) ) {
				trap_GetEntityToken( token, sizeof( token ) );
				health = atoi( token );
			} else {
				// skip value
				trap_GetEntityToken( token, sizeof( token ) );
			}
		}

		// Only care about entities with inline brush models (*N)
		if ( !modelStr[0] || modelStr[0] != '*' ) {
			continue;
		}

		if ( tvNumTriggers >= MAX_TRIGGER_VIS ) {
			CG_Printf( "^3WARNING: MAX_TRIGGER_VIS (%d) reached\n", MAX_TRIGGER_VIS );
			break;
		}

		modelIndex = atoi( modelStr + 1 );
		if ( modelIndex <= 0 || modelIndex >= cgs.numInlineModels ) {
			continue;
		}

		// Get origin offset (many triggers have origin 0,0,0)
		VectorClear( origin );
		if ( originStr[0] ) {
			sscanf( originStr, "%f %f %f", &origin[0], &origin[1], &origin[2] );
		}

		// Get model bounds and compute world-space AABB
		{
			vec3_t mins, maxs;
			triggerVis_t *tv = &tvTriggers[tvNumTriggers];

			trap_R_ModelBounds( cgs.inlineDrawModel[modelIndex], mins, maxs );

			VectorAdd( mins, origin, tv->absmin );
			VectorAdd( maxs, origin, tv->absmax );
			tv->type = TrigVis_ClassifyType( classname );
			tv->active = qtrue;
			tv->modelIndex = modelIndex;
			tv->spawnflags = spawnflags;
			tv->wait = wait;
			tv->dmg = dmg;
			tv->speed = speed;
			tv->count = count;
			tv->key = key;
			tv->delay = delay;
			tv->health = health;
			Q_strncpyz( tv->classname, classname, sizeof( tv->classname ) );
			Q_strncpyz( tv->targetname, targetname, sizeof( tv->targetname ) );
			Q_strncpyz( tv->target, target, sizeof( tv->target ) );
			Q_strncpyz( tv->message, message, sizeof( tv->message ) );
			Q_strncpyz( tv->scriptName, scriptName, sizeof( tv->scriptName ) );
			Q_strncpyz( tv->aiName, aiName, sizeof( tv->aiName ) );

			tvNumTriggers++;
		}
	}

	CG_Printf( "^5TriggerVis: found %d brush volumes\n", tvNumTriggers );
}

/*
==================
CG_DrawTriggerVis

Draw all visible brush volumes as colored transparent boxes.
Call before trap_R_RenderScene().
==================
*/
void CG_DrawTriggerVis( void ) {
	int i;
	float dist;
	vec3_t mid, delta;
	byte alpha;

	if ( !cg_drawTriggers.integer || tvNumTriggers == 0 ) {
		return;
	}

	// Alpha from cvar  (cg_drawTriggers: 1..255 sets alpha, 1 = default 80)
	if ( cg_drawTriggers.integer > 1 && cg_drawTriggers.integer <= 255 ) {
		alpha = (byte)cg_drawTriggers.integer;
	} else {
		alpha = 80;  // default ~31% opacity
	}

	for ( i = 0; i < tvNumTriggers; i++ ) {
		triggerVis_t *tv = &tvTriggers[i];

		if ( !tv->active ) {
			continue;
		}

		// Distance culling
		mid[0] = ( tv->absmin[0] + tv->absmax[0] ) * 0.5f;
		mid[1] = ( tv->absmin[1] + tv->absmax[1] ) * 0.5f;
		mid[2] = ( tv->absmin[2] + tv->absmax[2] ) * 0.5f;

		VectorSubtract( mid, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );

		if ( dist > TRIGVIS_MAX_DIST ) {
			continue;
		}

		// Draw box with bordered faces
		{
			byte baseColor[4], fillColor[4], borderColor[4];

			TrigVis_GetColor( tv->type, baseColor, alpha );

			// Fill: full color, nicely visible
			fillColor[0] = baseColor[0];
			fillColor[1] = baseColor[1];
			fillColor[2] = baseColor[2];
			fillColor[3] = (byte)( alpha * 0.6f );

			// Border: slightly brighter, moderate glow
			borderColor[0] = (byte)( baseColor[0] + ( 255 - baseColor[0] ) * 0.3f );
			borderColor[1] = (byte)( baseColor[1] + ( 255 - baseColor[1] ) * 0.3f );
			borderColor[2] = (byte)( baseColor[2] + ( 255 - baseColor[2] ) * 0.3f );
			borderColor[3] = 140;

			TrigVis_DrawBox( tv->absmin, tv->absmax, fillColor, borderColor, tvShader, tvBorderShader );
		}
	}
}

/*
==================
TrigVis_WorldToScreen

Projects a 3D world position to 2D virtual screen coordinates (640x480).
Returns qfalse if the point is behind the camera or way off-screen.
==================
*/
qboolean TrigVis_WorldToScreen( vec3_t worldPos, float *sx, float *sy ) {
	vec3_t delta;
	float fwd, lft, up;
	float tanHalfFovX, tanHalfFovY;

	VectorSubtract( worldPos, cg.refdef.vieworg, delta );

	fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
	if ( fwd < 1.0f ) {
		return qfalse;  // behind camera
	}

	lft = DotProduct( delta, cg.refdef.viewaxis[1] );
	up  = DotProduct( delta, cg.refdef.viewaxis[2] );

	tanHalfFovX = tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) );
	tanHalfFovY = tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) );

	*sx = 320.0f - ( lft / ( fwd * tanHalfFovX ) ) * 320.0f;
	*sy = 240.0f - ( up  / ( fwd * tanHalfFovY ) ) * 240.0f;

	// Cull if way off-screen
	if ( *sx < -100 || *sx > 740 || *sy < -50 || *sy > 530 ) {
		return qfalse;
	}

	return qtrue;
}

/*
==================
CG_DrawTriggerLabels

Draw 2D text labels at the center of each visible brush volume.
Shows decoded behavior, spawnflags, target chains, script/AI info.
Designed for speedrun route analysis.
Call AFTER trap_R_RenderScene() during 2D drawing phase.
==================
*/
#define LABEL_MAX_DIST  768.0f
#define LABEL_CHAR_W    4
#define LABEL_CHAR_H    6

/*
==================
TrigVis_DecodeBehavior

Build a human-readable behavior string for the entity:
 - ONE-SHOT / RETRIG 0.5s / TOGGLE / ALWAYS
 - Decoded spawnflags per entity type
==================
*/
static void TrigVis_DecodeBehavior( triggerVis_t *tv, char *out, int outSize ) {
	char flags[256];
	flags[0] = '\0';

	out[0] = '\0';

	switch ( tv->type ) {
	case TRIG_MULTIPLE:
		// wait -1 = one-shot, otherwise retrigger
		if ( tv->wait < 0 ) {
			Q_strcat( out, outSize, "ONE-SHOT" );
		} else if ( tv->wait > 0 ) {
			Q_strcat( out, outSize, va( "RETRIG %.1fs", tv->wait ) );
		} else {
			Q_strcat( out, outSize, "RETRIG 0.5s" ); // default wait
		}
		// sf1=AI_TOUCH
		if ( tv->spawnflags & 1 )  Q_strcat( flags, sizeof( flags ), " AI_TOUCH" );
		break;

	case TRIG_ONCE:
		Q_strcat( out, outSize, "ONE-SHOT" );
		if ( tv->spawnflags & 1 )  Q_strcat( flags, sizeof( flags ), " AI_TOUCH" );
		break;

	case TRIG_PUSH:
		// sf1=TOGGLE sf2=REMOVE sf4=NOAI
		if ( tv->spawnflags & 1 )       Q_strcat( out, outSize, "TOGGLE" );
		else if ( tv->spawnflags & 2 )  Q_strcat( out, outSize, "ONE-SHOT" );
		else                             Q_strcat( out, outSize, "PUSH" );
		if ( tv->spawnflags & 4 )  Q_strcat( flags, sizeof( flags ), " NO_AI" );
		break;

	case TRIG_TELEPORT:
		Q_strcat( out, outSize, "TELEPORT" );
		break;

	case TRIG_HURT:
		// sf1=START_OFF sf2=PLAYER_ONLY sf4=SILENT sf8=NO_PROTECT sf16=SLOW sf32=ONCE
		if ( tv->spawnflags & 32 )       Q_strcat( out, outSize, "ONE-SHOT" );
		else if ( tv->spawnflags & 16 )  Q_strcat( out, outSize, "SLOW 1/s" );
		else                              Q_strcat( out, outSize, "EVERY_FRAME" );
		if ( tv->dmg )  Q_strcat( out, outSize, va( " %ddmg", tv->dmg ) );
		if ( tv->spawnflags & 1 )  Q_strcat( flags, sizeof( flags ), " START_OFF" );
		if ( tv->spawnflags & 2 )  Q_strcat( flags, sizeof( flags ), " PLAYER_ONLY" );
		if ( tv->spawnflags & 4 )  Q_strcat( flags, sizeof( flags ), " SILENT" );
		if ( tv->spawnflags & 8 )  Q_strcat( flags, sizeof( flags ), " NO_PROTECT" );
		break;

	case TRIG_OBJECTIVE_INFO:
		if ( tv->count ) {
			Q_strcat( out, outSize, va( "OBJ #%d", tv->count ) );
		} else {
			Q_strcat( out, outSize, "OBJECTIVE" );
		}
		if ( tv->spawnflags & 1 )  Q_strcat( flags, sizeof( flags ), " AXIS" );
		if ( tv->spawnflags & 2 )  Q_strcat( flags, sizeof( flags ), " ALLIED" );
		break;

	case TRIG_FLAGONLY:
		if ( tv->spawnflags & 1 )       Q_strcat( out, outSize, "RED_FLAG" );
		else if ( tv->spawnflags & 2 )  Q_strcat( out, outSize, "BLUE_FLAG" );
		else                              Q_strcat( out, outSize, "FLAG" );
		break;

	case TRIG_AIDOOR:
		Q_strcat( out, outSize, "AI_DOOR" );
		break;

	case TRIG_DEATHCHECK:
		Q_strcat( out, outSize, "DEATH_CHECK" );
		if ( tv->spawnflags & 2 )  Q_strcat( flags, sizeof( flags ), " GIBFLAG" );
		break;

	case TRIG_ALWAYS:
		Q_strcat( out, outSize, "ALWAYS" );
		break;

	// func_ types
	case TRIG_FUNC_DOOR:
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( "spd:%d", (int)tv->speed ) );
		else                    Q_strcat( out, outSize, "spd:100" );
		if ( tv->key )  Q_strcat( out, outSize, va( " KEY:%d", tv->key ) );
		if ( tv->spawnflags & 1 )   Q_strcat( flags, sizeof( flags ), " START_OPEN" );
		if ( tv->spawnflags & 4 )   Q_strcat( flags, sizeof( flags ), " CRUSHER" );
		if ( tv->spawnflags & 8 )   Q_strcat( flags, sizeof( flags ), " TOGGLE" );
		if ( tv->spawnflags & 32 )  Q_strcat( flags, sizeof( flags ), " TOUCH" );
		break;

	case TRIG_FUNC_BUTTON:
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( "spd:%d", (int)tv->speed ) );
		else                    Q_strcat( out, outSize, "BUTTON" );
		if ( tv->wait > 0 )  Q_strcat( out, outSize, va( " w:%.1fs", tv->wait ) );
		break;

	case TRIG_FUNC_EXPLOSIVE:
		if ( tv->health > 0 )  Q_strcat( out, outSize, va( "HP:%d", tv->health ) );
		else                     Q_strcat( out, outSize, "BREAKABLE" );
		if ( tv->dmg )    Q_strcat( out, outSize, va( " %ddmg", tv->dmg ) );
		break;

	case TRIG_FUNC_PLAT:
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( "spd:%d", (int)tv->speed ) );
		else                    Q_strcat( out, outSize, "PLATFORM" );
		break;

	case TRIG_FUNC_TRAIN:
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( "spd:%d", (int)tv->speed ) );
		else                    Q_strcat( out, outSize, "TRAIN" );
		break;

	case TRIG_FUNC_SECRET:
		Q_strcat( out, outSize, "SECRET" );
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( " spd:%d", (int)tv->speed ) );
		break;

	case TRIG_FUNC_ROTATING:
		Q_strcat( out, outSize, "ROTATING" );
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( " spd:%d", (int)tv->speed ) );
		break;

	case TRIG_FUNC_BOBBING:
		Q_strcat( out, outSize, "BOBBING" );
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( " spd:%d", (int)tv->speed ) );
		break;

	case TRIG_FUNC_STATIC:
		Q_strcat( out, outSize, "STATIC" );
		break;

	case TRIG_FUNC_INVISIBLE_USER:
		Q_strcat( out, outSize, "INTERACT" );
		break;

	case TRIG_SCRIPT_MOVER:
		Q_strcat( out, outSize, "SCRIPTED" );
		if ( tv->speed > 0 )  Q_strcat( out, outSize, va( " spd:%d", (int)tv->speed ) );
		break;

	case TRIG_AI_TRIGGER:
		Q_strcat( out, outSize, "AI_TRIGGER" );
		break;

	default:
		if ( tv->spawnflags )  Com_sprintf( out, outSize, "sf:%d", tv->spawnflags );
		break;
	}

	// Append decoded flags
	if ( flags[0] ) {
		Q_strcat( out, outSize, flags );
	}
}

void CG_DrawTriggerLabels( void ) {
	int i;
	float dist;
	vec3_t mid, delta;
	float sx, sy;
	float distFade;
	vec4_t textColor, dimColor, distColor;
	int len;
	int curY;
	byte baseColor[4];
	char buf[128];
	char behavior[256];

	if ( !cg_drawTriggers.integer || tvNumTriggers == 0 ) {
		return;
	}

	for ( i = 0; i < tvNumTriggers; i++ ) {
		triggerVis_t *tv = &tvTriggers[i];

		if ( !tv->active ) {
			continue;
		}

		// Compute center of the box
		mid[0] = ( tv->absmin[0] + tv->absmax[0] ) * 0.5f;
		mid[1] = ( tv->absmin[1] + tv->absmax[1] ) * 0.5f;
		mid[2] = ( tv->absmin[2] + tv->absmax[2] ) * 0.5f;

		VectorSubtract( mid, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );

		if ( dist > LABEL_MAX_DIST || dist < 1.0f ) {
			continue;
		}

		// Project to screen
		if ( !TrigVis_WorldToScreen( mid, &sx, &sy ) ) {
			continue;
		}

		// Fade alpha based on distance
		distFade = 1.0f - ( dist / LABEL_MAX_DIST );
		if ( distFade < 0.1f ) {
			continue;
		}
		if ( distFade > 1.0f ) { distFade = 1.0f; }

		// Get entity type color and build text color
		TrigVis_GetColor( tv->type, baseColor, 255 );
		textColor[0] = baseColor[0] / 255.0f;
		textColor[1] = baseColor[1] / 255.0f;
		textColor[2] = baseColor[2] / 255.0f;
		textColor[3] = distFade;

		dimColor[0] = 0.7f; dimColor[1] = 0.7f;
		dimColor[2] = 0.7f; dimColor[3] = 0.7f * distFade;

		// Distance color (yellow)
		distColor[0] = 1.0f; distColor[1] = 1.0f;
		distColor[2] = 0.4f; distColor[3] = 0.6f * distFade;

		curY = (int)sy;

		// Line 1: classname (main color)
		len = strlen( tv->classname );
		CG_DrawStringExt(
			(int)( sx - ( len * LABEL_CHAR_W ) * 0.5f ), curY,
			tv->classname, textColor, qtrue, qtrue,
			LABEL_CHAR_W, LABEL_CHAR_H, 0, ALIGN_STRETCH );
		curY += LABEL_CHAR_H + 1;

		// Line 2: targetname (if present)
		if ( tv->targetname[0] ) {
			Com_sprintf( buf, sizeof( buf ), "name:%s", tv->targetname );
			len = strlen( buf );
			CG_DrawStringExt(
				(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
				buf, dimColor, qtrue, qtrue,
				LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
			curY += LABEL_CHAR_H;
		}

		// Line 3: target chain (if present)
		if ( tv->target[0] ) {
			Com_sprintf( buf, sizeof( buf ), "->%s", tv->target );
			len = strlen( buf );
			CG_DrawStringExt(
				(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
				buf, dimColor, qtrue, qtrue,
				LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
			curY += LABEL_CHAR_H;
		}

		// Line 4: Decoded behavior (ONE-SHOT / RETRIG / TOGGLE + flags)
		TrigVis_DecodeBehavior( tv, behavior, sizeof( behavior ) );
		if ( behavior[0] ) {
			len = strlen( behavior );
			CG_DrawStringExt(
				(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
				behavior, textColor, qtrue, qtrue,
				LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
			curY += LABEL_CHAR_H;
		}

		// Line 5: script/AI/delay info (if any)
		buf[0] = '\0';
		if ( tv->scriptName[0] ) {
			Com_sprintf( buf, sizeof( buf ), "scr:%s", tv->scriptName );
		}
		if ( tv->aiName[0] ) {
			if ( buf[0] ) Q_strcat( buf, sizeof( buf ), " " );
			Q_strcat( buf, sizeof( buf ), va( "ai:%s", tv->aiName ) );
		}
		if ( tv->delay > 0 ) {
			if ( buf[0] ) Q_strcat( buf, sizeof( buf ), " " );
			Q_strcat( buf, sizeof( buf ), va( "dly:%.1fs", tv->delay ) );
		}
		if ( buf[0] ) {
			len = strlen( buf );
			CG_DrawStringExt(
				(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
				buf, dimColor, qtrue, qtrue,
				LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
			curY += LABEL_CHAR_H;
		}

		// Line 6: message (if present, truncated)
		if ( tv->message[0] ) {
			Com_sprintf( buf, sizeof( buf ), "msg:%.30s", tv->message );
			len = strlen( buf );
			CG_DrawStringExt(
				(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
				buf, dimColor, qtrue, qtrue,
				LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
			curY += LABEL_CHAR_H;
		}

		// Line 7: distance from player (always shown, yellow)
		Com_sprintf( buf, sizeof( buf ), "%dm", (int)( dist / 32.0f ) );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * ( LABEL_CHAR_W - 1 ) ) * 0.5f ), curY,
			buf, distColor, qtrue, qtrue,
			LABEL_CHAR_W - 1, LABEL_CHAR_H - 1, 0, ALIGN_STRETCH );
	}
}
