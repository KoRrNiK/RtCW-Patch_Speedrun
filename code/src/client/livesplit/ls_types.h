/*
===========================================================================
ls_types.h  --  LiveSplit external window type definitions

All enums, structs, constants, and API declarations.
No engine or Win32 dependencies - pure C types only.
===========================================================================
*/

#ifndef LS_TYPES_H
#define LS_TYPES_H

/* ---- Color mode (gradient support) ---------------------------------- */
#define LSCLR_MODE_PLAIN       0
#define LSCLR_MODE_VGRADIENT   1
#define LSCLR_MODE_HGRADIENT   2

/* Sentinel: inherit color from parent / global layout */
#define LSCLR_INHERIT  0xFFFFFFFFu

typedef struct {
	int      mode;      /* LSCLR_MODE_* */
	unsigned c1;        /* 0xRRGGBB  or  LSCLR_INHERIT */
	unsigned c2;        /* second colour for gradients */
	int      alpha;     /* 0-255, 255 = fully opaque */
	int      gradPos;   /* gradient midpoint 0-100 (50 = linear) */
} lsColor_t;

/* ---- Alignment ------------------------------------------------------ */
#define LSALIGN_LEFT    0
#define LSALIGN_CENTER  1
#define LSALIGN_RIGHT   2

/* ---- Accuracy (decimal places for times) ---------------------------- */
#define LSACC_SECONDS       0
#define LSACC_TENTHS        1
#define LSACC_HUNDREDTHS    2
#define LSACC_MILLISECONDS  3
#define LSACC_MINUTES       4   /* truncate to minutes only */

/* ---- Timing method -------------------------------------------------- */
#define LSTIME_GAME_TIME    0
#define LSTIME_IGT          1

/* ---- Limits --------------------------------------------------------- */
#define LSWND_MAX_ROWS       40
#define LSWND_MAX_COMPONENTS 64
#define LSWND_MAX_COLUMNS    6

/* =====================================================================
   Component types
   ===================================================================== */
typedef enum {
	LSCOMP_TITLE,
	LSCOMP_SPLITS,
	LSCOMP_TIMER,
	LSCOMP_DETAILED_TIMER,
	LSCOMP_SEG_TIMER,
	LSCOMP_PREV_SEGMENT,
	LSCOMP_SUM_OF_BEST,
	LSCOMP_BEST_POSSIBLE,
	LSCOMP_POSSIBLE_SAVE,
	LSCOMP_COMPARISON,
	LSCOMP_SEPARATOR,
	LSCOMP_TEXT,
	LSCOMP_BLANK_SPACE,
	LSCOMP_HEADER,
	LSCOMP_BEST_SEGMENTS,
	LSCOMP_REAL_TIME,
	LSCOMP_GHOST_SEG_TIME,
	LSCOMP_100PCT,
	LSCOMP_TYPE_COUNT
} lsCompType_t;

/* ---- Column types (Splits component) -------------------------------- */
typedef enum {
	LSCOL_DELTA,
	LSCOL_SPLIT_TIME,
	LSCOL_SEG_TIME,
	LSCOL_BEST_SEG,
	LSCOL_PB_SPLIT,
	LSCOL_DELTA_BEST,
	LSCOL_TYPE_COUNT
} lsColumnType_t;

/* Per-column settings */
typedef struct {
	char      label[32];   /* custom header text, empty = default */
	lsColor_t textColor;   /* column text color (INHERIT = use row color) */
	int       width;       /* 0 = auto (78) */
	char      font[64];    /* per-column font override, empty = inherit */
	int       fontSize;    /* 0 = inherit component font size */
	int       bold;        /* -1 = inherit, 0 = normal, 1 = bold */
	lsColor_t beforeColor; /* completed rows column color */
	lsColor_t currentColor;/* current row column color */
	lsColor_t afterColor;  /* future rows column color */
} lsColumnSettings_t;

/* =====================================================================
   Info-row settings  (shared by PrevSeg, SoB, BPT, PTS, Comparison)
   ===================================================================== */
typedef struct {
	int       accuracy;       /* LSACC_* */
	int       dropDecimals;   /* drop fractions when > 1 min */
	int       showLive;       /* only PrevSeg uses this */
	int       compareAgainst; /* -1=use global, 0=PB, 1=Best, 2=Avg */
	lsColor_t valueColor;
	lsColor_t labelColor;
	char      label[32];      /* custom label (empty = default) */
} lsInfoSettings_t;

/* =====================================================================
   Per-component settings
   ===================================================================== */
typedef struct {
	int       enabled;
	char      font[64];
	int       fontSize;       /* 0 = inherit global */
	int       bold;           /* 0 = normal, 1 = bold */
	lsColor_t textColor;
	lsColor_t bgColor;
	int       padH;           /* horizontal padding (left+right), -1 = default */
	int       padV;           /* vertical padding (top+bottom), -1 = default */
	int       overrideHeight; /* custom height, 0 = auto */
	int       alignment;      /* LSALIGN_* */

	union {
		/* ---- Title ---- */
		struct {
			int showGameName;
			int showCategory;
			int showAttempts;
			int attemptsOnNewLine;  /* 1=separate line (default), 0=append to last line */
			int showTopAccent;      /* 0=hide top accent bar (default 1) */
			int showBottomAccent;   /* 0=hide bottom accent bar (default 1) */
		} title;

		/* ---- Splits ---- */
		struct {
			int       visibleSplits;
			int       columns[LSWND_MAX_COLUMNS];
			lsColumnSettings_t colSettings[LSWND_MAX_COLUMNS];
			int       numColumns;
			int       showThinSeps;
			int       alwaysShowLast;
			int       lockLastToBottom;
			int       sepBeforeLastSplit;
			int       deltaAccuracy;       /* LSACC_* */
			int       deltaDropDecimals;
			int       splitAccuracy;       /* LSACC_* */
			int       deltaCountdownSec;   /* live delta countdown start (0=off) */
			lsColor_t currentBg;
			lsColor_t beforeCurrentBg;
			lsColor_t currentSplitClr;     /* text colour on current */
			lsColor_t afterCurrentBg;
			lsColor_t liveDeltaColor;
			lsColor_t splitTimeColor;
			lsColor_t beforeSplitColor;
			int       showHeader;          /* show column headers row */
			int       alternateRows;       /* alternate row shading */
			int       shortNames;           /* use abbreviated split names */
			lsColor_t headerTextColor;     /* header text color */
			lsColor_t nameColor;           /* split name text color */
			lsColor_t altRowColor;         /* alternate row BG (inherit = auto-lighten) */
			lsColor_t rowColor;            /* normal row BG (inherit = use baseBG) */
			lsColor_t rowSepColor;         /* row separator color (inherit = auto) */

			/* per-state name styling */
			char      beforeNameFont[64]; /* completed rows name font */
			int       beforeNameSize;     /* 0 = inherit */
			int       beforeNameBold;     /* -1 = inherit */
			lsColor_t beforeNameColor;   /* completed rows name color */

			char      currentNameFont[64]; /* current row name font */
			int       currentNameSize;    /* 0 = inherit */
			int       currentNameBold;    /* -1 = inherit */

			char      afterNameFont[64];  /* future rows name font */
			int       afterNameSize;      /* 0 = inherit */
			int       afterNameBold;      /* -1 = inherit */
			lsColor_t afterNameColor;    /* future rows name color */
			int       compareAgainst;     /* -1=use global, 0=PB, 1=Best, 2=Avg */
		} splits;

		/* ---- Timer ---- */
		struct {
			int       decimals;
			int       timingMethod;        /* LSTIME_* */
			lsColor_t aheadColor;
			lsColor_t behindColor;
			lsColor_t goldColor;
			int       colorOnDeltaPlus;   /* change green->red when delta goes + */
			int       compareAgainst;     /* -1=use global, 0=PB, 1=Best, 2=Avg */
		} timer;

		/* ---- Detailed Timer ---- */
		struct {
			int       mainDecimals;        /* 1-3 */
			int       compDecimals;        /* comparison times */
			int       timingMethod;        /* LSTIME_* */
			int       showPB;
			int       showBest;
			int       mainFontSize;        /* 0 = inherit+big */
			int       compFontSize;        /* 0 = inherit+small */
			int       segFontSize;         /* 0 = inherit, segment timer size */
			lsColor_t mainAheadColor;
			lsColor_t mainBehindColor;
			lsColor_t mainGoldColor;
			lsColor_t pbColor;
			lsColor_t bestColor;
			lsColor_t labelColor;          /* "PB:"/"Best:" label color */
			lsColor_t segNameColor;        /* segment name color */
			lsColor_t segTimerColor;       /* segment timer text color */
			int       showSegTimer;        /* show segment timer in bottom row */
			int       colorOnDeltaPlus;   /* change green->red when delta goes + */
			int       compareAgainst;     /* -1=use global, 0=PB, 1=Best, 2=Avg */
		} detailedTimer;

		/* ---- Segment Timer ---- */
		struct {
			int       decimals;
			int       timingMethod;
			lsColor_t timerColor;    /* segment timer text color */
			int       colorOnDeltaPlus;   /* change green->red when delta goes + */
			int       compareAgainst;     /* -1=use global, 0=PB, 1=Best, 2=Avg */
		} segTimer;

		/* ---- Real Time timer ---- */
		struct {
			int       decimals;
			lsColor_t timerColor;    /* timer text color */
		} realTimer;

		/* ---- Info rows ---- */
		lsInfoSettings_t prevSeg;
		lsInfoSettings_t sumOfBest;
		lsInfoSettings_t bestPossible;
		lsInfoSettings_t possibleSave;
		lsInfoSettings_t comparison;

		/* ---- Best Segments ---- */
		struct {
			int showSegments;  /* 0=total only (default), 1=show individual rows */
		} bestSegments;

		/* ---- Separator ---- */
		struct {
			lsColor_t color;
			int       height;
		} separator;

		/* ---- Custom Text ---- */
		struct {
			char text[128];
		} text;

		/* ---- Blank Space ---- */
		struct {
			int height;        /* default 24 */
		} blankSpace;

		/* ---- Header ---- */
		struct {
			char text[128];
			int  showLine;     /* show accent line below, default 1 */
		} header;

		/* ---- Ghost Segment Time ---- */
		struct {
			int       decimals;       /* LSACC_* */
			lsColor_t timerColor;    /* ghost timer text color */
			lsColor_t labelColor;    /* label text color */
		} ghostSegTimer;

		/* ---- 100% Tracker ---- */
		struct {
			lsColor_t labelColor;    /* label text color */
			lsColor_t valueColor;    /* value text color */
			lsColor_t completeColor; /* color when found==total */
			int       showTotal;     /* 1=show cumulative totals (default) */
			int       showSegment;   /* 1=show per-segment counts */
		} pct;
	} u;
} lsCompSettings_t;

/* =====================================================================
   Component instance
   ===================================================================== */
typedef struct {
	lsCompType_t     type;
	lsCompSettings_t s;
} lsComponent_t;

/* =====================================================================
   Layout  (JSON-persisted)
   ===================================================================== */
typedef struct {
	int       version;
	int       alwaysOnTop;
	int       transparent;
	int       opacity;         /* 0-255 */
	int       compareAgainst;  /* 0=PB, 1=Best, 2=Avg */

	char      globalFont[64];
	int       globalFontSize;
	int       windowWidth;     /* default window width, 0 = auto (250) */

	lsColor_t bgColor;
	lsColor_t headerBg;
	lsColor_t accentColor;
	lsColor_t textColor;
	lsColor_t aheadColor;
	lsColor_t behindColor;
	lsColor_t goldColor;

	/* new visual options */
	int       textShadow;     /* 0=off, 1=on -- draw dark shadow behind text */
	int       thinAccent;     /* draw 1px accent line between components */
	int       globalBold;     /* global bold font */
	int       lockResize;     /* prevent manual resize of window */
	int       flipLayout;     /* render components bottom-to-top */
	int       componentSpacing; /* extra pixels between components (0=none) */

	int           numComponents;
	lsComponent_t components[LSWND_MAX_COMPONENTS];
} lsLayout_t;

/* =====================================================================
   Shared state  (game thread ---> window thread, volatile)
   ===================================================================== */
typedef struct {
	struct {
		char  name[64];
		char  shortName[64];
		int   state;           /* 0 = future, 1 = current, 2 = completed */
		char  delta[20];
		char  splitTime[20];
		char  segTime[20];
		char  bestSeg[20];
		char  pbSplitTime[20];
		char  deltaBest[20];   /* delta from best segment (seg - gold) */
		int   isBehind;
		int   isGold;
		int   liveDeltaMs;     /* live cumulative delta for current split (ms), INT_MIN = n/a */
		int   cumTimeMs;       /* raw cumulative time in ms (0 = n/a) */
		int   pbCumMs;         /* PB cumulative time in ms (-1 = n/a) */
		int   bestCumMs;       /* best-segments cumulative ms (-1 = n/a) */
		int   avgCumMs;        /* average-segments cumulative ms (-1 = n/a) */
		int   segTimeMs;       /* raw segment time in ms */
		int   segCompareMs;    /* comparison segment time (PB seg) in ms, 0 = n/a */
		int   bestSegMs;       /* best (gold) segment time in ms, 0 = n/a */
		int   pbSegMs;         /* PB segment time in ms, 0 = n/a */
		char  pbSegTime[20];   /* formatted PB segment time */
		int   segSecretsFound; /* per-segment secrets found */
		int   segSecretsTotal; /* per-segment secrets total */
		int   segTreasureFound;/* per-segment treasures found */
		int   segTreasureTotal;/* per-segment treasures total */
	} rows[LSWND_MAX_ROWS];
	int  numRows;
	int  curRow;

	int  active;
	int  paused;
	int  finished;

	char timerText[32];
	char timerFrac[8];
	char segTimerText[32];
	char rtTimerText[32];     /* real-time (RTA) timer text */
	char rtTimerFrac[8];      /* real-time fractional part */

	char gameName[64];
	char categoryText[64];
	int  attempts;
	int  completions;

	char prevSegLabel[32];
	char prevSegValue[20];
	int  prevSegBehind;
	int  prevSegGold;

	char sobText[20];
	int  sobBestMs;     /* sum of best segments (golds), -1=n/a */
	int  sobPbMs;       /* sum of PB segments, -1=n/a */
	int  sobAvgMs;      /* sum of average segments, -1=n/a */
	char bptText[20];
	int  bptBestMs;     /* best possible using golds, -1=n/a */
	int  bptPbMs;       /* best possible using PB segs, -1=n/a */
	int  bptAvgMs;      /* best possible using avg segs, -1=n/a */
	char ptsText[20];
	int  ptsMs;            /* raw Possible Time Save in ms (INT_MIN = n/a) */
	char pbText[20];
	char compareLabel[32];

	char segTimerFrac[8];  /* segment timer fractional part */
	int  timerBehind;      /* 1 if current delta is positive (behind) */

	/* Ghost segment time (gold/PB ghost total time for current map) */
	int  ghostSegMs;       /* ghost total segment time in ms, -1 = n/a */
	char ghostSegText[32]; /* formatted ghost segment time */
	char ghostSegFrac[8];  /* fractional part */

	/* 100% tracking */
	int  pctSecretsFound;
	int  pctSecretsTotal;
	int  pctTreasureFound;
	int  pctTreasureTotal;

	/* Best Segments list */
	struct {
		char name[64];
		char time[20];     /* absolute gold time */
		char delta[20];    /* delta vs gold (seg - gold) */
		int  deltaMs;      /* raw delta ms (INT_MIN = n/a) */
		int  state;        /* 0=future, 1=current, 2=completed */
		int  isGold;       /* 1 if this split beat or matched gold */
	} bestSegs[LSWND_MAX_ROWS];
	int numBestSegs;
} lsWndState_t;

/* =====================================================================
   Global state & public API
   ===================================================================== */
extern volatile lsWndState_t lswnd_state;

void LS_WindowCreate( void );
void LS_WindowDestroy( void );
int  LS_WindowIsActive( void );
void LS_WindowRepaint( void );
void LS_LayoutSetDefault( lsLayout_t *layout );

#endif /* LS_TYPES_H */
