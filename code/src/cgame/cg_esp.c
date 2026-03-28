/*
===========================================================================
cg_esp.c - Enemy & Item ESP

Enemy ESP: Re-adds enemy model parts with glow shaders for
through-wall visibility.  Two-pass (red through walls, green visible).

Item ESP: Colored boxes at item locations, visible through walls,
with 2D labels showing name, type, quantity.

Usage:  cg_drawEnemies 1   (enemy ESP on)
        cg_drawItems   1   (item ESP on)
===========================================================================
*/

#include "cg_local.h"

/* ===================== Enemy ESP ===================== */

#define ESP_MAX_DIST    16384.0f
#define ESP_LABEL_DIST  4096.0f
#define ESP_LABEL_CW    4
#define ESP_LABEL_CH    6

/* ---- lookup tables for ESP labels ---- */

static const char *aiCharNames[] = {
	"None",            // AICHAR_NONE
	"Soldier",         // AICHAR_SOLDIER
	"American",        // AICHAR_AMERICAN
	"Zombie",          // AICHAR_ZOMBIE
	"WarZombie",       // AICHAR_WARZOMBIE
	"Venom",           // AICHAR_VENOM
	"Loper",           // AICHAR_LOPER
	"EliteGuard",      // AICHAR_ELITEGUARD
	"StimSoldier1",    // AICHAR_STIMSOLDIER1
	"StimSoldier2",    // AICHAR_STIMSOLDIER2
	"StimSoldier3",    // AICHAR_STIMSOLDIER3
	"SuperSoldier",    // AICHAR_SUPERSOLDIER
	"BlackGuard",      // AICHAR_BLACKGUARD
	"ProtoSoldier",    // AICHAR_PROTOSOLDIER
	"Frogman",         // AICHAR_FROGMAN
	"Helga",           // AICHAR_HELGA
	"Heinrich",        // AICHAR_HEINRICH
	"Partisan",        // AICHAR_PARTISAN
	"Russian",         // AICHAR_RUSSIAN
	"Civilian",        // AICHAR_CIVILIAN
};
#define NUM_AICHAR_NAMES ( sizeof( aiCharNames ) / sizeof( aiCharNames[0] ) )

static const char *weaponNames[] = {
	"None",            // WP_NONE        0
	"Knife",           // WP_KNIFE       1
	"Luger",           // WP_LUGER       2
	"MP40",            // WP_MP40        3
	"Mauser",          // WP_MAUSER      4
	"FG42",            // WP_FG42        5
	"Grenade",         // WP_GRENADE_LAUNCHER 6
	"Panzerfaust",     // WP_PANZERFAUST 7
	"Venom",           // WP_VENOM       8
	"Flamethrower",    // WP_FLAMETHROWER 9
	"Tesla",           // WP_TESLA       10
	"Colt",            // WP_COLT        11
	"Thompson",        // WP_THOMPSON    12
	"Garand",          // WP_GARAND      13
	"Pineapple",       // WP_GRENADE_PINEAPPLE 14
	"SniperRifle",     // WP_SNIPERRIFLE 15
	"Snooper",         // WP_SNOOPERSCOPE 16
	"FG42Scope",       // WP_FG42SCOPE   17
	"Sten",            // WP_STEN        18
	"Silencer",        // WP_SILENCER    19
	"Akimbo",          // WP_AKIMBO      20
	"ClassSpecial",    // WP_CLASS_SPECIAL 21
	"Dynamite",        // WP_DYNAMITE    22
	"MonsterAtk1",     // WP_MONSTER_ATTACK1 23
	"MonsterAtk2",     // WP_MONSTER_ATTACK2 24
	"MonsterAtk3",     // WP_MONSTER_ATTACK3 25
	"Gauntlet",        // WP_GAUNTLET    26
	"Sniper",          // WP_SNIPER      27
	"SmokeGrenade",    // WP_GRENADE_SMOKE 28
	"MedicHeal",       // WP_MEDIC_HEAL  29
	"Mortar",          // WP_MORTAR      30
	"Explosion",       // VERYBIGEXPLOSION 31
};
#define NUM_WEAPON_NAMES ( sizeof( weaponNames ) / sizeof( weaponNames[0] ) )

static const char *ESP_AiCharName( int aiChar ) {
	if ( aiChar >= 0 && aiChar < (int)NUM_AICHAR_NAMES ) {
		return aiCharNames[aiChar];
	}
	return "Unknown";
}

static const char *ESP_WeaponName( int weapon ) {
	if ( weapon >= 0 && weapon < (int)NUM_WEAPON_NAMES ) {
		return weaponNames[weapon];
	}
	return "Unknown";
}

static qhandle_t espWallShader  = 0;   /* red, no depth test - through walls */
static qhandle_t espVisShader   = 0;   /* green, depth tested - visible parts */

/* per-entity HP tracking - updated from entityState_t.constantLight */
#define ESP_HP_UNKNOWN  -1
static int espLastHP[MAX_GENTITIES];    /* last known health */
static int espMaxHP[MAX_GENTITIES];     /* highest health seen (assumed max) */

/*
==================
CG_InitEnemyESP

Register both ESP shaders.
Call once at map load (after CG_RegisterGraphics).
==================
*/
void CG_InitEnemyESP( void ) {
	int j;
	espWallShader = trap_R_RegisterShader( "espBorderGlow" );
	if ( !espWallShader ) {
		espWallShader = cgs.media.whiteShader;
	}
	espVisShader = trap_R_RegisterShader( "espGlowVisible" );
	if ( !espVisShader ) {
		espVisShader = cgs.media.whiteShader;
	}
	for ( j = 0; j < MAX_GENTITIES; j++ ) {
		espLastHP[j] = ESP_HP_UNKNOWN;
		espMaxHP[j] = 100;  /* assume 100 until first update */
	}
}

/*
==================
ESP_AddGlowEntity

Re-add a refEntity_t with the ESP glow shader applied, so the model
silhouette renders through walls with the given color.
==================
*/
static void ESP_AddGlowEntity( refEntity_t *src, qhandle_t shader, byte r, byte g, byte b, byte a ) {
	refEntity_t ent;

	if ( !src->hModel ) {
		return;
	}

	ent = *src;
	ent.customShader = shader;
	ent.customSkin = 0;			// override skin so the glow shader is used uniformly
	ent.shaderRGBA[0] = r;
	ent.shaderRGBA[1] = g;
	ent.shaderRGBA[2] = b;
	ent.shaderRGBA[3] = a;

	trap_R_AddRefEntityToScene( &ent );
}

/*
==================
CG_DrawEnemyESP

Re-render enemy model parts (legs, torso, head) with the ESP glow shader.
This must be called AFTER CG_AddPacketEntities() so the refEnts in
playerEntity_t are populated, but BEFORE trap_R_RenderScene().
==================
*/
void CG_DrawEnemyESP( void ) {
	int i;
	centity_t *cent;
	vec3_t delta;
	float dist;

	if ( !cg_drawEnemies.integer ) {
		return;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		cent = &cg_entities[i];

		if ( !cent->currentValid ) {
			continue;
		}
		if ( cent->currentState.eType != ET_PLAYER ) {
			continue;
		}
		if ( cent->currentState.clientNum == cg.clientNum ) {
			continue;
		}

		// Distance culling
		VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > ESP_MAX_DIST ) {
			continue;
		}

		// Frustum culling: skip if behind camera
		{
			float fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
			if ( fwd < -100.0f ) {
				continue;
			}
		}

		// Alpha from cg_enemyOpacity cvar (0..255, default 255)
		{
			byte eAlpha = 255;
			byte wallA, visA;
			if ( cg_enemyOpacity.integer >= 0 && cg_enemyOpacity.integer <= 255 ) {
				eAlpha = (byte)cg_enemyOpacity.integer;
			}
			wallA = (byte)( 50 * eAlpha / 255 );
			visA  = (byte)( 60 * eAlpha / 255 );

			// Pass 1: RED glow through walls (depth test disabled)
			ESP_AddGlowEntity( &cent->pe.legsRefEnt,  espWallShader, 180, 30, 15, wallA );
			ESP_AddGlowEntity( &cent->pe.torsoRefEnt, espWallShader, 180, 30, 15, wallA );
			ESP_AddGlowEntity( &cent->pe.headRefEnt,  espWallShader, 180, 30, 15, wallA );

			// Pass 2: GREEN glow on visible parts (depth test enabled, overdraws red)
			ESP_AddGlowEntity( &cent->pe.legsRefEnt,  espVisShader, 15, 180, 30, visA );
			ESP_AddGlowEntity( &cent->pe.torsoRefEnt, espVisShader, 15, 180, 30, visA );
			ESP_AddGlowEntity( &cent->pe.headRefEnt,  espVisShader, 15, 180, 30, visA );
		}
	}
}

/*
==================
CG_DrawEnemyESPLabels

Draw 2D info panels above enemy heads showing:
  - Name / AI character type
  - Weapon
  - Status flags (DEAD, FIRING, CROUCHING, etc.)
  - HP bar (if health data available)
  - Distance

Call AFTER trap_R_RenderScene() during 2D drawing phase.
==================
*/

/* panel layout constants */
#define ESP_PANEL_W      64      /* panel width in pixels */
#define ESP_BAR_H        4       /* HP bar height */
#define ESP_BAR_W        ( ESP_PANEL_W - 2 )  /* HP bar inner width */
#define ESP_LINE_H       ( ESP_LABEL_CH + 1 ) /* line spacing */
#define ESP_PAIN_FLASH   800     /* ms to flash bar orange after pain event */

/*
==================
ESP_UpdateHealth

Read health from entityState_t.constantLight (set every server frame for AI).
Low 16 bits = current health, high 16 bits = max health.
No events needed - this field is networked directly.
==================
*/
static void ESP_UpdateHealth( int entNum, centity_t *cent, int eFlags ) {
	int packed, curHP, maxHP;

	if ( eFlags & EF_DEAD ) {
		espLastHP[entNum] = 0;
		return;
	}

	packed = cent->currentState.constantLight;
	if ( !packed ) {
		return;
	}

	curHP = packed & 0xFFFF;
	maxHP = ( packed >> 16 ) & 0xFFFF;

	if ( curHP > 0 && curHP < 10000 ) {
		espLastHP[entNum] = curHP;
	}
	if ( maxHP > 0 && maxHP < 10000 ) {
		espMaxHP[entNum] = maxHP;
	}
}

void CG_DrawEnemyESPLabels( void ) {
	int i;
	centity_t *cent;
	vec3_t labelPos, delta;
	float dist, sx, sy, distFade;
	vec4_t textColor, barBg, barFg;
	char buf[128];
	int len;
	int curY;
	const char *name;
	const char *aiName;
	const char *weapName;
	clientInfo_t *ci;
	entityState_t *es;
	int eFlags;

	if ( !cg_drawEnemies.integer ) {
		return;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		cent = &cg_entities[i];

		if ( !cent->currentValid ) {
			continue;
		}
		if ( cent->currentState.eType != ET_PLAYER ) {
			continue;
		}
		if ( cent->currentState.clientNum == cg.clientNum ) {
			continue;
		}

		es = &cent->currentState;

		/* Label position: above the model */
		VectorCopy( cent->lerpOrigin, labelPos );
		labelPos[2] += 56;

		VectorSubtract( labelPos, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > ESP_LABEL_DIST || dist < 1.0f ) {
			continue;
		}

		if ( !TrigVis_WorldToScreen( labelPos, &sx, &sy ) ) {
			continue;
		}

		distFade = 1.0f - ( dist / ESP_LABEL_DIST );
		if ( distFade < 0.1f ) {
			continue;
		}
		if ( distFade > 1.0f ) { distFade = 1.0f; }

		/* ---- gather data ---- */
		eFlags = es->eFlags;
		ci = NULL;
		if ( es->clientNum >= 0 && es->clientNum < MAX_CLIENTS ) {
			ci = &cgs.clientinfo[es->clientNum];
			if ( !ci->infoValid ) {
				ci = NULL;
			}
		}

		name = ( ci && ci->name[0] ) ? ci->name : NULL;
		aiName = ESP_AiCharName( es->aiChar );
		weapName = ESP_WeaponName( es->weapon );

		/* update HP tracking for this entity */
		ESP_UpdateHealth( i, cent, eFlags );

		curY = (int)sy;

		/* ---- line 1: name / type ---- */
		textColor[0] = 1.0f; textColor[1] = 0.85f;
		textColor[2] = 0.3f; textColor[3] = distFade;

		if ( name ) {
			Com_sprintf( buf, sizeof( buf ), "%s", name );
		} else {
			Com_sprintf( buf, sizeof( buf ), "%s #%d", aiName, es->clientNum );
		}
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * ESP_LABEL_CW ) * 0.5f ), curY,
			buf, textColor, qtrue, qtrue,
			ESP_LABEL_CW, ESP_LABEL_CH, 0, ALIGN_STRETCH );
		curY += ESP_LINE_H;

		/* ---- line 2: weapon + distance ---- */
		textColor[0] = 0.7f; textColor[1] = 0.7f;
		textColor[2] = 0.7f; textColor[3] = distFade;
		Com_sprintf( buf, sizeof( buf ), "%s %dm", weapName, (int)( dist / 40.0f ) );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * ESP_LABEL_CW ) * 0.5f ), curY,
			buf, textColor, qtrue, qtrue,
			ESP_LABEL_CW, ESP_LABEL_CH, 0, ALIGN_STRETCH );
		curY += ESP_LINE_H;

		/* ---- line 3: proportional HP bar + number ---- */
		{
			int barX = (int)( sx - ESP_BAR_W * 0.5f );
			qboolean isDead = ( eFlags & EF_DEAD ) ? qtrue : qfalse;
			int hp = espLastHP[i];
			int maxHP = espMaxHP[i];
			float hpFrac;
			char hpBuf[32];

			/* background (dark) */
			barBg[0] = 0.1f; barBg[1] = 0.1f;
			barBg[2] = 0.1f; barBg[3] = 0.6f * distFade;
			CG_FillRect( barX, curY, ESP_BAR_W, ESP_BAR_H, barBg, ALIGN_STRETCH );

			if ( isDead ) {
				/* DEAD - full red bar */
				barFg[0] = 0.8f; barFg[1] = 0.1f;
				barFg[2] = 0.1f; barFg[3] = 0.7f * distFade;
				CG_FillRect( barX, curY, ESP_BAR_W, ESP_BAR_H, barFg, ALIGN_STRETCH );
				Com_sprintf( hpBuf, sizeof( hpBuf ), "DEAD" );
			} else if ( hp > 0 && hp != ESP_HP_UNKNOWN ) {
				/* proportional bar: green > yellow > red */
				hpFrac = (float)hp / (float)maxHP;
				if ( hpFrac > 1.0f ) { hpFrac = 1.0f; }
				if ( hpFrac < 0.0f ) { hpFrac = 0.0f; }
				if ( hpFrac > 0.5f ) {
					barFg[0] = 1.0f - ( hpFrac - 0.5f ) * 2.0f;
					barFg[1] = 1.0f;
				} else {
					barFg[0] = 1.0f;
					barFg[1] = hpFrac * 2.0f;
				}
				barFg[2] = 0.0f;
				barFg[3] = 0.9f * distFade;
				CG_FillRect( barX, curY, (int)( ESP_BAR_W * hpFrac ), ESP_BAR_H, barFg, ALIGN_STRETCH );
				Com_sprintf( hpBuf, sizeof( hpBuf ), "%d/%d", hp, maxHP );
			} else {
				/* no data yet - full green bar (assumed alive) */
				barFg[0] = 0.1f; barFg[1] = 0.8f;
				barFg[2] = 0.1f; barFg[3] = 0.7f * distFade;
				CG_FillRect( barX, curY, ESP_BAR_W, ESP_BAR_H, barFg, ALIGN_STRETCH );
				Com_sprintf( hpBuf, sizeof( hpBuf ), "ALIVE" );
			}
			curY += ESP_BAR_H + 1;

			/* HP text below bar */
			{
				vec4_t hpColor;
				int hpLen = strlen( hpBuf );
				hpColor[0] = 1.0f; hpColor[1] = 1.0f;
				hpColor[2] = 1.0f; hpColor[3] = 0.8f * distFade;
				CG_DrawStringExt(
					(int)( sx - ( hpLen * ( ESP_LABEL_CW - 1 ) ) * 0.5f ), curY,
					hpBuf, hpColor, qtrue, qtrue,
					ESP_LABEL_CW - 1, ESP_LABEL_CH - 1, 0, ALIGN_STRETCH );
			}
		}
		curY += ESP_LABEL_CH;

		/* ---- line 4: status flags (compact) ---- */
		buf[0] = '\0';
		if ( eFlags & EF_FIRING ) {
			Q_strcat( buf, sizeof( buf ), "FIR " );
		}
		if ( eFlags & EF_CROUCHING ) {
			Q_strcat( buf, sizeof( buf ), "CRO " );
		}
		if ( eFlags & EF_ZOOMING ) {
			Q_strcat( buf, sizeof( buf ), "ZOM " );
		}
		if ( eFlags & EF_MG42_ACTIVE ) {
			Q_strcat( buf, sizeof( buf ), "MG42 " );
		}

		if ( buf[0] ) {
			textColor[0] = 1.0f; textColor[1] = 0.6f;
			textColor[2] = 0.1f; textColor[3] = distFade;
			len = strlen( buf );
			CG_DrawStringExt(
				(int)( sx - ( len * ESP_LABEL_CW ) * 0.5f ), curY,
				buf, textColor, qtrue, qtrue,
				ESP_LABEL_CW, ESP_LABEL_CH - 1, 0, ALIGN_STRETCH );
		}
	}
}


/* ===================== Item ESP ===================== */

#define ITEM_ESP_MAX_DIST   4096.0f
#define ITEM_ESP_LABEL_DIST 2048.0f
#define ITEM_LABEL_CW       3
#define ITEM_LABEL_CH       5
#define ITEM_BOX_SIZE       10.0f   /* half-size of the 3D marker box */

/* Color by item type */
static void Item_GetColor( itemType_t type, byte color[4] ) {
	switch ( type ) {
		case IT_HEALTH:    color[0]=50;  color[1]=220; color[2]=50;  color[3]=100; break; /* green */
		case IT_WEAPON:    color[0]=255; color[1]=180; color[2]=0;   color[3]=100; break; /* orange */
		case IT_AMMO:      color[0]=220; color[1]=220; color[2]=50;  color[3]=100; break; /* yellow */
		case IT_ARMOR:     color[0]=80;  color[1]=140; color[2]=255; color[3]=100; break; /* blue */
		case IT_POWERUP:   color[0]=200; color[1]=50;  color[2]=255; color[3]=100; break; /* purple */
		case IT_HOLDABLE:  color[0]=0;   color[1]=200; color[2]=200; color[3]=100; break; /* cyan */
		case IT_KEY:       color[0]=255; color[1]=215; color[2]=0;   color[3]=110; break; /* gold */
		case IT_TREASURE:  color[0]=255; color[1]=200; color[2]=50;  color[3]=110; break; /* gold-ish */
		case IT_CLIPBOARD: color[0]=200; color[1]=200; color[2]=200; color[3]=100; break; /* white */
		default:           color[0]=180; color[1]=180; color[2]=180; color[3]=80;  break;
	}
}

static const char *Item_TypeName( itemType_t type ) {
	switch ( type ) {
		case IT_HEALTH:    return "Health";
		case IT_WEAPON:    return "Weapon";
		case IT_AMMO:      return "Ammo";
		case IT_ARMOR:     return "Armor";
		case IT_POWERUP:   return "Powerup";
		case IT_HOLDABLE:  return "Holdable";
		case IT_KEY:       return "Key";
		case IT_TREASURE:  return "Treasure";
		case IT_CLIPBOARD: return "Clipboard";
		default:           return "Item";
	}
}

/*
==================
CG_DrawItemESP

Draw small colored boxes at item locations, visible through walls.
Call BEFORE trap_R_RenderScene().
==================
*/
void CG_DrawItemESP( void ) {
	int i;
	centity_t *cent;
	entityState_t *es;
	gitem_t *item;
	vec3_t delta, mins, maxs;
	float dist;
	byte fillColor[4], borderColor[4];

	if ( !cg_drawItems.integer ) {
		return;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		cent = &cg_entities[i];

		if ( !cent->currentValid ) {
			continue;
		}

		es = &cent->currentState;

		if ( es->eType != ET_ITEM ) {
			continue;
		}

		if ( !es->modelindex || es->modelindex >= bg_numItems ) {
			continue;
		}

		// Skip invisible/nodraw items
		if ( es->eFlags & EF_NODRAW ) {
			continue;
		}

		item = &bg_itemlist[es->modelindex];

		VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > ITEM_ESP_MAX_DIST ) {
			continue;
		}

		// Frustum culling: skip if behind camera
		{
			float fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
			if ( fwd < -50.0f ) {
				continue;
			}
		}

		// Build a small box around the item origin
		VectorSet( mins,
			cent->lerpOrigin[0] - ITEM_BOX_SIZE,
			cent->lerpOrigin[1] - ITEM_BOX_SIZE,
			cent->lerpOrigin[2] - 2.0f );
		VectorSet( maxs,
			cent->lerpOrigin[0] + ITEM_BOX_SIZE,
			cent->lerpOrigin[1] + ITEM_BOX_SIZE,
			cent->lerpOrigin[2] + ITEM_BOX_SIZE * 1.5f );

		Item_GetColor( item->giType, fillColor );

		/* scale base alpha by cg_itemOpacity cvar (0..255, default 255) */
		{
			int iOpacity = 255;
			if ( cg_itemOpacity.integer >= 0 && cg_itemOpacity.integer <= 255 ) {
				iOpacity = cg_itemOpacity.integer;
			}
			fillColor[3] = (byte)( fillColor[3] * iOpacity / 255 );
		}

		/* fade with distance */
		{
			float fade = 1.0f - ( dist / ITEM_ESP_MAX_DIST );
			if ( fade < 0.15f ) { fade = 0.15f; }
			fillColor[3] = (byte)( fillColor[3] * fade );
		}

		borderColor[0] = (byte)( fillColor[0] + ( 255 - fillColor[0] ) * 0.4f );
		borderColor[1] = (byte)( fillColor[1] + ( 255 - fillColor[1] ) * 0.4f );
		borderColor[2] = (byte)( fillColor[2] + ( 255 - fillColor[2] ) * 0.4f );
		borderColor[3] = (byte)( fillColor[3] * 1.5f > 255 ? 255 : fillColor[3] * 1.5f );

		TrigVis_DrawBox( mins, maxs, fillColor, borderColor, tvShader, tvBorderShader );
	}
}

/*
==================
CG_DrawItemESPLabels

Draw 2D text labels above items showing name, type, quantity.
Call AFTER trap_R_RenderScene() during 2D drawing phase.
==================
*/
void CG_DrawItemESPLabels( void ) {
	int i;
	centity_t *cent;
	entityState_t *es;
	gitem_t *item;
	vec3_t labelPos, delta;
	float dist, sx, sy, distFade;
	vec4_t textColor, dimColor;
	char buf[128];
	int len, curY;

	if ( !cg_drawItems.integer ) {
		return;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		cent = &cg_entities[i];

		if ( !cent->currentValid ) {
			continue;
		}

		es = &cent->currentState;

		if ( es->eType != ET_ITEM ) {
			continue;
		}

		if ( !es->modelindex || es->modelindex >= bg_numItems ) {
			continue;
		}

		if ( es->eFlags & EF_NODRAW ) {
			continue;
		}

		item = &bg_itemlist[es->modelindex];

		VectorCopy( cent->lerpOrigin, labelPos );
		labelPos[2] += ITEM_BOX_SIZE * 2.0f + 6.0f;

		VectorSubtract( labelPos, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > ITEM_ESP_LABEL_DIST || dist < 1.0f ) {
			continue;
		}

		if ( !TrigVis_WorldToScreen( labelPos, &sx, &sy ) ) {
			continue;
		}

		distFade = 1.0f - ( dist / ITEM_ESP_LABEL_DIST );
		if ( distFade < 0.1f ) {
			continue;
		}
		if ( distFade > 1.0f ) { distFade = 1.0f; }

		curY = (int)sy;

		/* line 1: pickup name or classname */
		{
			byte iColor[4];
			Item_GetColor( item->giType, iColor );
			textColor[0] = iColor[0] / 255.0f;
			textColor[1] = iColor[1] / 255.0f;
			textColor[2] = iColor[2] / 255.0f;
			textColor[3] = distFade;
		}

		if ( item->pickup_name && item->pickup_name[0] ) {
			Com_sprintf( buf, sizeof( buf ), "%s", item->pickup_name );
		} else if ( item->classname && item->classname[0] ) {
			Com_sprintf( buf, sizeof( buf ), "%s", item->classname );
		} else {
			Com_sprintf( buf, sizeof( buf ), "item#%d", es->modelindex );
		}
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * ITEM_LABEL_CW ) * 0.5f ), curY,
			buf, textColor, qtrue, qtrue,
			ITEM_LABEL_CW, ITEM_LABEL_CH, 0, ALIGN_STRETCH );
		curY += ITEM_LABEL_CH + 1;

		/* line 2: type + distance */
		dimColor[0] = 0.7f; dimColor[1] = 0.7f;
		dimColor[2] = 0.7f; dimColor[3] = 0.7f * distFade;
		Com_sprintf( buf, sizeof( buf ), "%s %dm",
			Item_TypeName( item->giType ),
			(int)( dist / 40.0f ) );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * ITEM_LABEL_CW ) * 0.5f ), curY,
			buf, dimColor, qtrue, qtrue,
			ITEM_LABEL_CW, ITEM_LABEL_CH, 0, ALIGN_STRETCH );
		curY += ITEM_LABEL_CH + 1;

		/* line 3: quantity (if relevant) */
		if ( item->giType == IT_AMMO || item->giType == IT_HEALTH ) {
			int qty = item->quantity;
			if ( qty <= 0 ) {
				qty = item->gameskillnumber[1]; /* skill 1 = normal */
			}
			if ( qty > 0 ) {
				Com_sprintf( buf, sizeof( buf ), "qty:%d", qty );
				len = strlen( buf );
				CG_DrawStringExt(
					(int)( sx - ( len * ITEM_LABEL_CW ) * 0.5f ), curY,
					buf, dimColor, qtrue, qtrue,
					ITEM_LABEL_CW, ITEM_LABEL_CH - 1, 0, ALIGN_STRETCH );
			}
		}
	}
}
