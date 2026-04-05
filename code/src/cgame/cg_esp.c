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

/* ===================== Enemy Sight Visualization ===================== */

/*
Draws a ground-plane FOV cone and inner detection circle for each enemy,
color-coded by AI state, so speedrunners can plan routes to avoid detection.

Usage:  cg_drawEnemySight 1         (toggle on/off)
        cg_sightOpacity   0..255    (fill alpha, default 40)
        cg_sightRange     <float>   (visual range in units, default 1500)

AI state colors:
  RELAXED (0) = green    FOV = 90 deg
  QUERY   (1) = yellow   FOV = 135 deg
  ALERT   (2) = orange   FOV = 135 deg
  COMBAT  (3) = red      FOV = 180 deg

Inner detection radius (512 units) = cyan circle (always detect regardless of FOV).
White line = exact look direction.
*/

#define SIGHT_MAX_RENDER_DIST   4096.0f
#define SIGHT_DEFAULT_RANGE     1500.0f
#define SIGHT_INNER_RADIUS      512.0f
#define SIGHT_CONE_SEGMENTS     32
#define SIGHT_CIRCLE_SEGMENTS   24
#define SIGHT_GROUND_OFFSET     2.0f
#define SIGHT_DIRLINE_WIDTH     8.0f

/* FOV scale factors per AI state (matches server-side aiStateFovScales in ai_cast_sight.c) */
static const float sightFovScales[] = { 1.0f, 1.5f, 1.5f, 2.0f };

/*
==================
Sight_GetStateColor

Returns RGBA color for the given AI state.
==================
*/
static void Sight_GetStateColor( int aiState, byte color[4], byte alpha ) {
	switch ( aiState ) {
	case 0: /* RELAXED - green */
		color[0] = 0; color[1] = 200; color[2] = 0; color[3] = alpha;
		break;
	case 1: /* QUERY - yellow */
		color[0] = 255; color[1] = 255; color[2] = 0; color[3] = alpha;
		break;
	case 2: /* ALERT - orange */
		color[0] = 255; color[1] = 140; color[2] = 0; color[3] = alpha;
		break;
	case 3: /* COMBAT - red */
		color[0] = 255; color[1] = 0; color[2] = 0; color[3] = alpha;
		break;
	default:
		color[0] = 200; color[1] = 200; color[2] = 200; color[3] = alpha;
		break;
	}
}

/*
==================
Sight_DrawTriangle

Draws a single triangle poly (3 verts) with uniform color.
==================
*/
static void Sight_DrawTriangle( vec3_t v0, vec3_t v1, vec3_t v2, byte color[4], qhandle_t shader ) {
	polyVert_t verts[3];
	int k;

	VectorCopy( v0, verts[0].xyz );
	VectorCopy( v1, verts[1].xyz );
	VectorCopy( v2, verts[2].xyz );

	for ( k = 0; k < 3; k++ ) {
		verts[k].st[0] = 0;
		verts[k].st[1] = 0;
		verts[k].modulate[0] = color[0];
		verts[k].modulate[1] = color[1];
		verts[k].modulate[2] = color[2];
		verts[k].modulate[3] = color[3];
	}

	trap_R_AddPolyToScene( shader, 3, verts );
}

/*
==================
CG_DrawEnemySight

For each living enemy within range, draws:
  1. A ground-plane FOV cone (triangle fan) showing their field of view
  2. A filled inner detection circle (512 units radius)
  3. A narrow direction indicator line

Must be called BEFORE trap_R_RenderScene().
==================
*/
void CG_DrawEnemySight( void ) {
	int i, seg;
	centity_t *cent;
	vec3_t delta, center, edge1, edge2;
	float dist, fov, halfFov, range, yaw;
	float angle1, angle2, angleStep;
	int aiState;
	byte coneColor[4], innerColor[4], dirColor[4];
	byte alpha;
	int alphaVal;

	if ( !cg_drawEnemySight.integer ) {
		return;
	}

	/* alpha from cvar (0..255, default 40) */
	alphaVal = cg_sightOpacity.integer;
	if ( alphaVal < 0 ) {
		alphaVal = 0;
	}
	if ( alphaVal > 255 ) {
		alphaVal = 255;
	}
	alpha = (byte)alphaVal;

	/* visual range from cvar */
	range = cg_sightRange.value;
	if ( range <= 0 ) {
		range = SIGHT_DEFAULT_RANGE;
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
		if ( cent->currentState.eFlags & EF_DEAD ) {
			continue;
		}

		/* distance culling - only draw for enemies within render distance */
		VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > SIGHT_MAX_RENDER_DIST ) {
			continue;
		}

		/* AI state and FOV computation */
		aiState = cent->currentState.aiState;
		if ( aiState < 0 || aiState > 3 ) {
			aiState = 0;
		}
		fov = 90.0f * sightFovScales[aiState];
		if ( fov > 360.0f ) {
			fov = 360.0f;
		}
		halfFov = fov * 0.5f;

		/* yaw direction (degrees) */
		yaw = cent->lerpAngles[YAW];

		/* center at enemy ground level + small offset to prevent z-fighting */
		VectorCopy( cent->lerpOrigin, center );
		center[2] += SIGHT_GROUND_OFFSET;

		/* cone fill color by AI state */
		Sight_GetStateColor( aiState, coneColor, alpha );

		/* inner detection circle: cyan, slightly less opaque */
		innerColor[0] = 0;
		innerColor[1] = 200;
		innerColor[2] = 255;
		innerColor[3] = (byte)( alphaVal * 7 / 10 );

		/* direction indicator line: white, more visible */
		{
			int dirA = alphaVal * 2;
			if ( dirA > 255 ) {
				dirA = 255;
			}
			dirColor[0] = 255;
			dirColor[1] = 255;
			dirColor[2] = 255;
			dirColor[3] = (byte)dirA;
		}

		/* ---- 1. FOV cone (triangle fan on ground plane) ---- */
		angleStep = fov / SIGHT_CONE_SEGMENTS;
		for ( seg = 0; seg < SIGHT_CONE_SEGMENTS; seg++ ) {
			angle1 = yaw - halfFov + angleStep * seg;
			angle2 = yaw - halfFov + angleStep * ( seg + 1 );

			edge1[0] = center[0] + range * cos( DEG2RAD( angle1 ) );
			edge1[1] = center[1] + range * sin( DEG2RAD( angle1 ) );
			edge1[2] = center[2];

			edge2[0] = center[0] + range * cos( DEG2RAD( angle2 ) );
			edge2[1] = center[1] + range * sin( DEG2RAD( angle2 ) );
			edge2[2] = center[2];

			Sight_DrawTriangle( center, edge1, edge2, coneColor, tvShader );
		}

		/* ---- 2. Inner detection circle (full 360) ---- */
		angleStep = 360.0f / SIGHT_CIRCLE_SEGMENTS;
		for ( seg = 0; seg < SIGHT_CIRCLE_SEGMENTS; seg++ ) {
			angle1 = angleStep * seg;
			angle2 = angleStep * ( seg + 1 );

			edge1[0] = center[0] + SIGHT_INNER_RADIUS * cos( DEG2RAD( angle1 ) );
			edge1[1] = center[1] + SIGHT_INNER_RADIUS * sin( DEG2RAD( angle1 ) );
			edge1[2] = center[2];

			edge2[0] = center[0] + SIGHT_INNER_RADIUS * cos( DEG2RAD( angle2 ) );
			edge2[1] = center[1] + SIGHT_INNER_RADIUS * sin( DEG2RAD( angle2 ) );
			edge2[2] = center[2];

			Sight_DrawTriangle( center, edge1, edge2, innerColor, tvShader );
		}

		/* ---- 3. Direction indicator line ---- */
		{
			vec3_t dirEnd, perpL, perpR;
			float dirRad = DEG2RAD( yaw );
			float perpRad = DEG2RAD( yaw + 90.0f );
			float halfW = SIGHT_DIRLINE_WIDTH * 0.5f;

			dirEnd[0] = center[0] + range * cos( dirRad );
			dirEnd[1] = center[1] + range * sin( dirRad );
			dirEnd[2] = center[2];

			perpL[0] = center[0] + halfW * cos( perpRad );
			perpL[1] = center[1] + halfW * sin( perpRad );
			perpL[2] = center[2];

			perpR[0] = center[0] - halfW * cos( perpRad );
			perpR[1] = center[1] - halfW * sin( perpRad );
			perpR[2] = center[2];

			Sight_DrawTriangle( perpL, perpR, dirEnd, dirColor, tvShader );
		}
	}
}


/* ===================== AI Path Visualization ===================== */

/*
Draws movement trail, direction arrow, facing line and 2D status labels
for each living AI enemy so developers/speedrunners can study AI routing.

Usage:  cg_drawAIPath 1         (toggle on/off)
        cg_pathLength <int>     (trail history length, default 64)

Trail dots are stored per entity and updated every frame the entity is alive.
Connected line segments on the ground show where the AI has been.
A direction arrow shows current movement heading.
A thin line shows the exact look/facing direction.
2D labels show AI state, move type and speed.

AI state names:
  0 = RELAXED   1 = QUERY   2 = ALERT   3 = COMBAT

Movement type names (from scriptAnimMoveTypes_t):
  IDLE, IDLECR, WALK, WALKBK, WALKCR, WALKCRBK, RUN, RUNBK,
  SWIM, SWIMBK, STRAFE_R, STRAFE_L, TURN_R, TURN_L, CLIMB_UP, CLIMB_DN
*/

#define PATH_MAX_TRAIL      128     /* max trail points per entity */
#define PATH_DEFAULT_LEN    64      /* default trail length */
#define PATH_MAX_DIST       8192.0f /* max render distance */
#define PATH_LABEL_DIST     4096.0f /* max label distance */
#define PATH_TRAIL_HEIGHT   4.0f    /* height above ground for trail */
#define PATH_TRAIL_WIDTH    4.0f    /* half-width of trail line */
#define PATH_ARROW_LEN      80.0f   /* length of direction arrow */
#define PATH_ARROW_WIDTH    16.0f   /* width of direction arrow */
#define PATH_LOOK_LEN       200.0f  /* length of look direction line */
#define PATH_LOOK_WIDTH     3.0f    /* width of look direction line */
#define PATH_MIN_MOVE_DIST  2.0f    /* minimum distance to add new trail point */
#define PATH_LABEL_CW       4
#define PATH_LABEL_CH       6
#define PATH_LINE_H         8

static const char *aiStateNames[] = { "RELAXED", "QUERY", "ALERT", "COMBAT" };
static const char *moveTypeNames[] = {
	"UNUSED", "IDLE", "IDLECR", "WALK", "WALKBK", "WALKCR", "WALKCRBK",
	"RUN", "RUNBK", "SWIM", "SWIMBK", "STRAFE_R", "STRAFE_L",
	"TURN_R", "TURN_L", "CLIMB_UP", "CLIMB_DN"
};
#define NUM_MOVETYPE_NAMES ( sizeof( moveTypeNames ) / sizeof( moveTypeNames[0] ) )

/* Per-entity trail storage */
typedef struct {
	vec3_t points[PATH_MAX_TRAIL];
	int    head;            /* next write index (circular) */
	int    count;           /* how many valid points */
	int    lastTime;        /* last time a point was added */
	vec3_t lastOrigin;      /* last recorded origin */
} aiTrail_t;

static aiTrail_t aiTrails[MAX_GENTITIES];
static qboolean  aiPathInited = qfalse;

/*
==================
CG_InitAIPath

Reset all trail data.
==================
*/
void CG_InitAIPath( void ) {
	memset( aiTrails, 0, sizeof( aiTrails ) );
	aiPathInited = qtrue;
}

/*
==================
Path_AddTrailPoint

Add a new point to the entity's trail if it moved enough.
==================
*/
static void Path_AddTrailPoint( int entNum, vec3_t origin ) {
	aiTrail_t *trail = &aiTrails[entNum];
	vec3_t delta;
	float dist;
	int maxLen;

	maxLen = cg_pathLength.integer;
	if ( maxLen < 4 ) maxLen = 4;
	if ( maxLen > PATH_MAX_TRAIL ) maxLen = PATH_MAX_TRAIL;

	if ( trail->count > 0 ) {
		VectorSubtract( origin, trail->lastOrigin, delta );
		dist = VectorLength( delta );
		if ( dist < PATH_MIN_MOVE_DIST ) {
			return; /* hasn't moved enough */
		}
	}

	VectorCopy( origin, trail->points[trail->head] );
	trail->head = ( trail->head + 1 ) % maxLen;
	if ( trail->count < maxLen ) {
		trail->count++;
	}
	trail->lastTime = cg.time;
	VectorCopy( origin, trail->lastOrigin );
}

/*
==================
Path_DrawTrail

Draw connected line segments for the entity's trail.
Uses quad polys (2 triangles per segment) for visible line width.
==================
*/
static void Path_DrawTrail( aiTrail_t *trail, byte color[4] ) {
	int j, maxLen, idx0, idx1;
	vec3_t p0, p1, dir, perp;
	vec3_t v0, v1, v2, v3;
	float len;

	if ( trail->count < 2 ) {
		return;
	}

	maxLen = cg_pathLength.integer;
	if ( maxLen < 4 ) maxLen = 4;
	if ( maxLen > PATH_MAX_TRAIL ) maxLen = PATH_MAX_TRAIL;

	for ( j = 0; j < trail->count - 1; j++ ) {
		/* oldest to newest */
		if ( trail->count >= maxLen ) {
			idx0 = ( trail->head + j ) % maxLen;
			idx1 = ( trail->head + j + 1 ) % maxLen;
		} else {
			idx0 = j;
			idx1 = j + 1;
		}

		VectorCopy( trail->points[idx0], p0 );
		VectorCopy( trail->points[idx1], p1 );
		p0[2] += PATH_TRAIL_HEIGHT;
		p1[2] += PATH_TRAIL_HEIGHT;

		VectorSubtract( p1, p0, dir );
		len = VectorLength( dir );
		if ( len < 0.1f ) continue;

		/* perpendicular on XY plane */
		perp[0] = -dir[1] / len * PATH_TRAIL_WIDTH;
		perp[1] = dir[0] / len * PATH_TRAIL_WIDTH;
		perp[2] = 0;

		VectorAdd( p0, perp, v0 );
		VectorSubtract( p0, perp, v1 );
		VectorSubtract( p1, perp, v2 );
		VectorAdd( p1, perp, v3 );

		/* Fade older segments */
		{
			byte segColor[4];
			float fade = (float)( j + 1 ) / (float)trail->count;
			segColor[0] = color[0];
			segColor[1] = color[1];
			segColor[2] = color[2];
			segColor[3] = (byte)( color[3] * fade );

			Sight_DrawTriangle( v0, v1, v2, segColor, tvShader );
			Sight_DrawTriangle( v0, v2, v3, segColor, tvShader );
		}
	}
}

/*
==================
Path_DrawArrow

Draw a direction arrow (triangle) at an entity's position.
==================
*/
static void Path_DrawArrow( vec3_t origin, float yaw, byte color[4] ) {
	vec3_t tip, left, right;
	float rad = DEG2RAD( yaw );
	float perpRad = DEG2RAD( yaw + 90.0f );
	float hw = PATH_ARROW_WIDTH * 0.5f;

	tip[0] = origin[0] + PATH_ARROW_LEN * cos( rad );
	tip[1] = origin[1] + PATH_ARROW_LEN * sin( rad );
	tip[2] = origin[2] + PATH_TRAIL_HEIGHT;

	left[0] = origin[0] + hw * cos( perpRad );
	left[1] = origin[1] + hw * sin( perpRad );
	left[2] = origin[2] + PATH_TRAIL_HEIGHT;

	right[0] = origin[0] - hw * cos( perpRad );
	right[1] = origin[1] - hw * sin( perpRad );
	right[2] = origin[2] + PATH_TRAIL_HEIGHT;

	Sight_DrawTriangle( left, right, tip, color, tvShader );
}

/*
==================
Path_DrawLookLine

Draw a thin line from the entity's eye position in the facing direction.
==================
*/
static void Path_DrawLookLine( vec3_t origin, float yaw, float pitch, byte color[4] ) {
	vec3_t start, endP, left, right, endL, endR;
	float radYaw = DEG2RAD( yaw );
	float radPitch = DEG2RAD( -pitch ); /* pitch is inverted in Q3 */
	float cosPitch = cos( radPitch );
	float hw = PATH_LOOK_WIDTH;

	start[0] = origin[0];
	start[1] = origin[1];
	start[2] = origin[2] + 36.0f; /* eye height */

	endP[0] = start[0] + PATH_LOOK_LEN * cosPitch * cos( radYaw );
	endP[1] = start[1] + PATH_LOOK_LEN * cosPitch * sin( radYaw );
	endP[2] = start[2] + PATH_LOOK_LEN * sin( radPitch );

	/* offset perpendicular for line width */
	{
		float perpRad = DEG2RAD( yaw + 90.0f );
		left[0]  = start[0] + hw * cos( perpRad );
		left[1]  = start[1] + hw * sin( perpRad );
		left[2]  = start[2];
		right[0] = start[0] - hw * cos( perpRad );
		right[1] = start[1] - hw * sin( perpRad );
		right[2] = start[2];
		endL[0]  = endP[0] + hw * cos( perpRad );
		endL[1]  = endP[1] + hw * sin( perpRad );
		endL[2]  = endP[2];
		endR[0]  = endP[0] - hw * cos( perpRad );
		endR[1]  = endP[1] - hw * sin( perpRad );
		endR[2]  = endP[2];
	}

	Sight_DrawTriangle( left, right, endR, color, tvShader );
	Sight_DrawTriangle( left, endR, endL, color, tvShader );
}

/*
==================
CG_DrawAIPath

3D component: trail lines, direction arrows, look lines.
Must be called BEFORE trap_R_RenderScene().
==================
*/
void CG_DrawAIPath( void ) {
	int i;
	centity_t *cent;
	entityState_t *es;
	vec3_t delta;
	float dist, speed;
	int aiState;
	byte trailColor[4], arrowColor[4], lookColor[4];

	if ( !cg_drawAIPath.integer ) {
		return;
	}

	if ( !aiPathInited ) {
		CG_InitAIPath();
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
		if ( cent->currentState.eFlags & EF_DEAD ) {
			/* Clear trail for dead entities */
			aiTrails[i].count = 0;
			aiTrails[i].head = 0;
			continue;
		}

		es = &cent->currentState;

		/* distance culling */
		VectorSubtract( cent->lerpOrigin, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > PATH_MAX_DIST ) {
			continue;
		}

		/* Update trail */
		Path_AddTrailPoint( i, cent->lerpOrigin );

		/* AI state for coloring */
		aiState = es->aiState;
		if ( aiState < 0 || aiState > 3 ) aiState = 0;

		/* Trail color by AI state */
		switch ( aiState ) {
		case 0: /* RELAXED - green */
			trailColor[0] = 0; trailColor[1] = 200; trailColor[2] = 0; trailColor[3] = 160;
			break;
		case 1: /* QUERY - yellow */
			trailColor[0] = 255; trailColor[1] = 255; trailColor[2] = 0; trailColor[3] = 160;
			break;
		case 2: /* ALERT - orange */
			trailColor[0] = 255; trailColor[1] = 140; trailColor[2] = 0; trailColor[3] = 160;
			break;
		case 3: /* COMBAT - red */
			trailColor[0] = 255; trailColor[1] = 0; trailColor[2] = 0; trailColor[3] = 160;
			break;
		default:
			trailColor[0] = 200; trailColor[1] = 200; trailColor[2] = 200; trailColor[3] = 160;
			break;
		}

		/* Draw trail */
		Path_DrawTrail( &aiTrails[i], trailColor );

		/* Compute speed from position delta (since trDelta may be zero for TR_INTERPOLATE) */
		{
			vec3_t vel;
			VectorSubtract( cent->nextState.pos.trBase, cent->currentState.pos.trBase, vel );
			speed = VectorLength( vel );
			if ( speed < 0.5f && cent->currentState.pos.trType != TR_STATIONARY ) {
				VectorCopy( cent->currentState.pos.trDelta, vel );
				speed = VectorLength( vel );
			}
		}

		/* Direction arrow (movement direction = yaw angle) */
		if ( speed > 1.0f ) {
			arrowColor[0] = 255; arrowColor[1] = 255; arrowColor[2] = 255; arrowColor[3] = 200;
			Path_DrawArrow( cent->lerpOrigin, cent->lerpAngles[YAW], arrowColor );
		}

		/* Look direction line (always) */
		lookColor[0] = 0; lookColor[1] = 180; lookColor[2] = 255; lookColor[3] = 150;
		Path_DrawLookLine( cent->lerpOrigin, cent->lerpAngles[YAW], cent->lerpAngles[PITCH], lookColor );
	}
}

/*
==================
CG_DrawAIPathLabels

2D component: status labels above enemies.
Must be called AFTER trap_R_RenderScene().
==================
*/
void CG_DrawAIPathLabels( void ) {
	int i;
	centity_t *cent;
	entityState_t *es;
	vec3_t labelPos, delta;
	float sx, sy, dist, distFade;
	int aiState, moveType;
	int curY, len;
	char buf[128];
	vec4_t textColor, dimColor;

	if ( !cg_drawAIPath.integer ) {
		return;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		cent = &cg_entities[i];

		if ( !cent->currentValid ) continue;
		if ( cent->currentState.eType != ET_PLAYER ) continue;
		if ( cent->currentState.clientNum == cg.clientNum ) continue;
		if ( cent->currentState.eFlags & EF_DEAD ) continue;

		es = &cent->currentState;

		/* Label position: above head */
		VectorCopy( cent->lerpOrigin, labelPos );
		labelPos[2] += 72; /* above the ESP labels */

		VectorSubtract( labelPos, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > PATH_LABEL_DIST || dist < 1.0f ) continue;

		if ( !TrigVis_WorldToScreen( labelPos, &sx, &sy ) ) continue;

		distFade = 1.0f - ( dist / PATH_LABEL_DIST );
		if ( distFade < 0.1f ) continue;
		if ( distFade > 1.0f ) distFade = 1.0f;

		aiState = es->aiState;
		if ( aiState < 0 || aiState > 3 ) aiState = 0;

		moveType = es->animMovetype;
		if ( moveType < 0 || moveType >= (int)NUM_MOVETYPE_NAMES ) {
			moveType = 0;
		}

		curY = (int)sy;

		/* ---- line 1: AI state ---- */
		switch ( aiState ) {
		case 0: /* green */
			textColor[0] = 0.2f; textColor[1] = 1.0f; textColor[2] = 0.2f; textColor[3] = distFade;
			break;
		case 1: /* yellow */
			textColor[0] = 1.0f; textColor[1] = 1.0f; textColor[2] = 0.2f; textColor[3] = distFade;
			break;
		case 2: /* orange */
			textColor[0] = 1.0f; textColor[1] = 0.6f; textColor[2] = 0.1f; textColor[3] = distFade;
			break;
		case 3: /* red */
			textColor[0] = 1.0f; textColor[1] = 0.1f; textColor[2] = 0.1f; textColor[3] = distFade;
			break;
		default:
			textColor[0] = 0.8f; textColor[1] = 0.8f; textColor[2] = 0.8f; textColor[3] = distFade;
			break;
		}
		Com_sprintf( buf, sizeof( buf ), "[%s]", aiStateNames[aiState] );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * PATH_LABEL_CW ) * 0.5f ), curY,
			buf, textColor, qtrue, qtrue,
			PATH_LABEL_CW, PATH_LABEL_CH, 0, ALIGN_STRETCH );
		curY += PATH_LINE_H;

		/* ---- line 2: movement type ---- */
		dimColor[0] = 0.7f; dimColor[1] = 0.85f;
		dimColor[2] = 0.7f; dimColor[3] = distFade * 0.9f;
		Com_sprintf( buf, sizeof( buf ), "%s", moveTypeNames[moveType] );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * PATH_LABEL_CW ) * 0.5f ), curY,
			buf, dimColor, qtrue, qtrue,
			PATH_LABEL_CW, PATH_LABEL_CH, 0, ALIGN_STRETCH );
		curY += PATH_LINE_H;

		/* ---- line 3: angles (yaw) ---- */
		dimColor[0] = 0.5f; dimColor[1] = 0.7f;
		dimColor[2] = 1.0f; dimColor[3] = distFade * 0.8f;
		Com_sprintf( buf, sizeof( buf ), "YAW:%.0f", AngleNormalize360( cent->lerpAngles[YAW] ) );
		len = strlen( buf );
		CG_DrawStringExt(
			(int)( sx - ( len * PATH_LABEL_CW ) * 0.5f ), curY,
			buf, dimColor, qtrue, qtrue,
			PATH_LABEL_CW, PATH_LABEL_CH, 0, ALIGN_STRETCH );
	}
}
