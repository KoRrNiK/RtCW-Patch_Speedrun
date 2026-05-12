#include "cg_local.h"

#define ZONE_MAX_ZONES          64
#define ZONE_NAME_LEN           32
#define ZONE_FILE_MAX           65536
#define ZONE_DEFAULT_HALF_XY    48.0f
#define ZONE_DEFAULT_BOTTOM     8.0f
#define ZONE_DEFAULT_TOP        72.0f
#define ZONE_MIN_SIZE           4.0f
#define ZONE_LABEL_MAX_DIST     3000.0f
#define ZONE_LABEL_CHAR_W       6
#define ZONE_LABEL_CHAR_H       8
#define ZONE_HANDLE_CENTER      8
#define ZONE_HANDLE_FACE_X_MIN  9
#define ZONE_HANDLE_FACE_X_MAX  10
#define ZONE_HANDLE_FACE_Y_MIN  11
#define ZONE_HANDLE_FACE_Y_MAX  12
#define ZONE_HANDLE_FACE_Z_MIN  13
#define ZONE_HANDLE_FACE_Z_MAX  14
#define ZONE_HANDLE_ROTATE_PITCH 15
#define ZONE_HANDLE_ROTATE_YAW   16
#define ZONE_HANDLE_ROTATE_ROLL  17
#define ZONE_HANDLE_LAST        ZONE_HANDLE_ROTATE_ROLL
#define ZONE_DRAG_MIN_DIST      16.0f
#define ZONE_DRAG_MAX_DIST      8192.0f

typedef enum {
	ZONE_START,
	ZONE_CHECKPOINT,
	ZONE_FINISH,
	ZONE_RACE
} zoneType_t;

typedef struct {
	qboolean active;
	qboolean builtin;
	qboolean raceVisited;
	zoneType_t type;
	int routeId;
	int order;
	char name[ZONE_NAME_LEN];
	char routeName[ZONE_NAME_LEN];
	vec3_t mins;
	vec3_t maxs;
	vec3_t angles;
	int bestSegmentMsec;
	int bestTotalMsec;
} speedrunZone_t;

typedef struct {
	speedrunZone_t zones[ZONE_MAX_ZONES];
	int count;
	int selectedZone;
	int hoveredZone;
	int hoveredCorner;
	qboolean dragging;
	int dragZone;
	int dragCorner;
	float dragDistance;
	vec3_t dragStartAngles;
	float dragStartAngle;
	int nextRouteId;
	int activeRouteId;
	int runningRouteId;
	int activeStartZone;
	qboolean leftStartAfterBegin;
	qboolean startedByJump;
	qboolean airborneAfterBegin;
	qboolean running;
	qboolean finished;
	int startTime;
	int lastSplitTime;
	int finishTime;
	int nextZone;
	int startGroundedSince;
	int wasInStartZone;
	qboolean wasJumpPressed;
	qboolean raceScoringWasActive;
	qboolean hasBuiltinRaceZones;
	char filePath[MAX_QPATH];
	char status[256];
	char lastSplit[128];
	char lastDelta[32];
	char lastTotalDelta[32];
	int runningBestTotalMsec;
	int lastSplitDeltaMsec;
	int lastTotalDeltaMsec;
	qboolean lastSplitWasPb;
	qboolean lastTotalWasPb;
	int lastSplitTimeShown;
	qhandle_t fillShader;
	qhandle_t borderShader;
} zoneState_t;

typedef struct {
	const char *mapName;
	const char *name;
	int order;
	vec3_t mins;
	vec3_t maxs;
	vec3_t angles;
} builtinRaceZone_t;

static zoneState_t zoneState;

static const builtinRaceZone_t cg_builtinRaceZones[] = {
	{ "xlabs", "race_01", 1, { 638.630f, -703.476f, -15.875f }, { 1076.494f, -448.497f, 192.393f }, { 0.000f, 0.000f, 0.000f } },
	{ "xlabs", "race_02", 2, { -767.948f, -1045.321f, -8.163f }, { -639.480f, -898.632f, 95.931f }, { 0.000f, 0.000f, 0.000f } },
	{ "xlabs", "race_03", 3, { -767.962f, -2339.230f, -127.335f }, { -511.567f, -1962.133f, 86.336f }, { 0.000f, 0.000f, 0.000f } },
	{ "village2", "race_01", 1, { 992.419f, 1569.533f, -128.414f }, { 1181.745f, 1855.221f, 71.790f }, { 0.000f, 0.000f, 0.000f } },
	{ "village2", "race_02", 2, { 2240.101f, -444.459f, -250.294f }, { 2423.537f, -242.378f, 159.835f }, { 0.000f, 0.000f, 0.000f } },
	{ "village2", "race_03", 3, { 320.399f, -31.872f, 1.034f }, { 447.835f, 161.104f, 170.150f }, { 0.000f, 0.000f, 0.000f } },
	{ "village2", "race_04", 4, { -1074.883f, 575.918f, -69.875f }, { -916.458f, 1059.424f, 250.517f }, { 0.000f, 0.000f, 0.000f } },
	{ "village1", "race_01", 1, { -2697.823f, -1279.930f, -511.975f }, { -2583.530f, -1134.789f, -367.006f }, { 0.000f, 0.000f, 0.000f } },
	{ "village1", "race_02", 2, { -450.333f, -703.453f, -176.346f }, { -257.055f, -576.174f, 0.048f }, { 0.000f, 0.000f, 0.000f } },
	{ "village1", "race_03", 3, { -1151.431f, 1498.534f, -79.598f }, { -896.570f, 1791.900f, 63.127f }, { 0.000f, 0.000f, 0.000f } },
	{ "village1", "race_04", 4, { -12.550f, 3111.495f, -186.699f }, { 140.992f, 3295.664f, 24.556f }, { 0.000f, 0.000f, 0.000f } },
	{ "tram", "race_01", 1, { 3328.287f, 288.748f, 992.060f }, { 3519.994f, 415.633f, 1183.792f }, { 0.000f, 0.000f, 0.000f } },
	{ "tram", "race_02", 2, { 3618.324f, -480.492f, 707.960f }, { 3743.699f, -352.865f, 879.821f }, { 0.000f, 0.000f, 0.000f } },
	{ "tram", "race_03", 3, { 1196.422f, -894.519f, 356.177f }, { 1695.087f, 1043.047f, 1675.132f }, { 0.000f, 0.000f, 0.000f } },
	{ "tram", "race_04", 4, { -3103.629f, -448.155f, -447.363f }, { -3039.824f, -384.252f, -255.977f }, { 0.000f, 0.000f, 0.000f } },
	{ "tram", "race_05", 5, { -3039.627f, -1056.176f, -640.069f }, { -2911.902f, -948.949f, -512.774f }, { 0.000f, 0.000f, 0.000f } },
	{ "trainyard", "race_01", 1, { -1641.446f, -463.446f, 64.282f }, { -1510.298f, -353.034f, 224.519f }, { 0.000f, 0.000f, 0.000f } },
	{ "trainyard", "race_02", 2, { 1288.570f, -1443.670f, -247.861f }, { 1419.866f, -1247.357f, -122.264f }, { 0.000f, 0.000f, 0.000f } },
	{ "trainyard", "race_03", 3, { 2398.585f, -143.952f, -248.444f }, { 2508.487f, 13.094f, -122.399f }, { 0.000f, 0.000f, 0.000f } },
	{ "trainyard", "race_04", 4, { 3324.048f, -1306.265f, 16.079f }, { 3588.137f, -1153.002f, 260.305f }, { 0.000f, 0.000f, 0.000f } },
	{ "swf", "race_01", 1, { 2144.099f, -943.987f, 488.195f }, { 2335.750f, -738.734f, 641.047f }, { 0.000f, 0.000f, 0.000f } },
	{ "swf", "race_02", 2, { 863.707f, -1596.209f, 319.470f }, { 1055.348f, -1500.209f, 456.270f }, { 0.000f, 0.000f, 0.000f } },
	{ "swf", "race_03", 3, { 1017.082f, -112.705f, 402.227f }, { 1146.151f, 86.444f, 624.208f }, { 0.000f, 0.000f, 0.000f } },
	{ "swf", "race_04", 4, { 1432.229f, 864.215f, 320.525f }, { 1831.858f, 1055.931f, 471.765f }, { 0.000f, 0.000f, 0.000f } },
	{ "swf", "race_05", 5, { 1408.854f, 2175.744f, 319.601f }, { 1593.875f, 2303.948f, 543.341f }, { 0.000f, 0.000f, 0.000f } },
	{ "sfm", "race_01", 1, { 542.245f, -432.491f, -127.650f }, { 926.264f, -83.129f, 431.622f }, { 0.000f, 0.000f, 0.000f } },
	{ "sfm", "race_02", 2, { 1741.720f, 387.232f, -136.692f }, { 1886.701f, 1089.741f, 183.426f }, { 0.000f, 0.000f, 0.000f } },
	{ "sfm", "race_03", 3, { -713.794f, 1408.559f, -151.609f }, { -367.179f, 1711.301f, 78.380f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_01", 1, { 1728.029f, 59.016f, 127.576f }, { 1855.912f, 311.537f, 256.030f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_02", 2, { 891.494f, -1026.864f, -383.875f }, { 1117.822f, -895.366f, -268.652f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_03", 3, { -1691.186f, 1023.764f, 312.125f }, { -1439.961f, 1151.835f, 448.505f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_04", 4, { -2381.244f, 1273.942f, 583.737f }, { -2120.828f, 1496.215f, 739.752f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_05", 5, { -1040.702f, 1765.654f, 319.574f }, { -884.983f, 2079.136f, 454.822f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_06", 6, { 256.036f, -767.445f, 63.893f }, { 571.957f, -640.046f, 189.795f }, { 0.000f, 0.000f, 0.000f } },
	{ "rocket", "race_07", 7, { 1184.161f, -1343.477f, 128.158f }, { 1311.630f, -1044.843f, 287.806f }, { 0.000f, 0.000f, 0.000f } },
	{ "norway", "race_01", 1, { 1357.067f, 1058.731f, 440.140f }, { 1624.336f, 1315.034f, 620.842f }, { 0.000f, -44.253f, 0.055f } },
	{ "norway", "race_02", 2, { 2463.531f, -2243.455f, 464.315f }, { 3012.874f, -2032.052f, 950.026f }, { 0.000f, 0.000f, 0.000f } },
	{ "norway", "race_03", 3, { -4618.852f, -2368.487f, 424.158f }, { -4495.082f, -2112.096f, 681.575f }, { 0.000f, 0.000f, 0.000f } },
	{ "norway", "race_04", 4, { -7727.515f, -1855.576f, 527.858f }, { -7568.430f, -1456.335f, 671.687f }, { 0.000f, 0.000f, 0.000f } },
	{ "forest", "race_01", 1, { -196.062f, -7010.817f, 271.366f }, { 39.098f, -4963.772f, 1167.334f }, { 0.000f, 0.000f, 0.000f } },
	{ "forest", "race_02", 2, { -56.966f, 3457.570f, 208.383f }, { 558.868f, 3709.843f, 520.361f }, { 0.000f, 0.000f, 0.000f } },
	{ "forest", "race_03", 3, { -6014.680f, 4505.955f, 549.230f }, { -5539.633f, 4979.287f, 907.703f }, { 0.000f, 0.000f, 0.000f } },
	{ "forest", "race_04", 4, { -5360.330f, -1281.644f, 176.185f }, { -4595.676f, 751.340f, 462.673f }, { 0.000f, 0.000f, 0.000f } },
	{ "factory", "race_01", 1, { 725.944f, -1211.587f, 335.913f }, { 943.138f, -964.020f, 451.267f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape2", "race_01", 1, { 64.208f, 1665.256f, 271.690f }, { 191.754f, 1845.254f, 447.985f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape2", "race_02", 2, { 761.397f, 1985.839f, -31.841f }, { 1023.954f, 2111.939f, 159.698f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape2", "race_03", 3, { 735.674f, 735.670f, 125.423f }, { 1247.516f, 928.539f, 393.125f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_01", 1, { 239.877f, 96.033f, -559.760f }, { 288.312f, 223.771f, -399.827f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_02", 2, { -383.589f, 679.865f, -367.733f }, { -321.812f, 832.328f, -137.850f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_03", 3, { -1020.278f, 1308.283f, 180.146f }, { -972.545f, 1379.984f, 303.788f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_04", 4, { -1523.451f, 319.560f, 351.733f }, { -1488.054f, 384.674f, 495.434f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_05", 5, { -1848.131f, 192.155f, 313.481f }, { -1639.497f, 560.258f, 328.141f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_06", 6, { -2305.394f, 887.665f, 144.015f }, { -2207.880f, 909.338f, 306.623f }, { 0.000f, 0.000f, 0.000f } },
	{ "escape1", "race_07", 7, { -3504.391f, 425.263f, 127.131f }, { -3311.277f, 494.719f, 446.949f }, { 0.000f, 0.000f, 0.000f } },
	{ "end", "race_01", 1, { -861.408f, -3589.326f, 198.814f }, { -534.747f, -3304.125f, 269.645f }, { 0.000f, 0.000f, 0.000f } },
	{ "end", "race_02", 2, { -444.737f, -2303.825f, 32.140f }, { -253.965f, -2048.178f, 351.951f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_01", 1, { -358.538f, -2091.908f, -697.325f }, { 201.227f, -1995.908f, -235.286f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_02", 2, { -606.346f, 262.505f, -831.853f }, { -325.012f, 555.076f, -730.962f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_03", 3, { 919.569f, 2381.905f, -1234.760f }, { 1015.569f, 2902.478f, -803.525f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_04", 4, { 1532.659f, 878.789f, -583.875f }, { 1621.486f, 1323.514f, -254.248f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_05", 5, { 784.125f, 23.836f, -76.753f }, { 1268.581f, 391.147f, -18.417f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_06", 6, { 1238.624f, 1208.995f, 10.866f }, { 1296.970f, 1554.538f, 333.776f }, { 0.000f, 0.000f, 0.000f } },
	{ "dig", "race_07", 7, { 1936.483f, 1675.740f, -19.492f }, { 2310.106f, 1952.788f, 23.802f }, { 0.000f, 0.000f, 0.000f } },
	{ "dark", "race_01", 1, { -2439.362f, 267.798f, 704.471f }, { -1871.325f, 2214.199f, 1179.779f }, { 0.000f, -60.282f, 0.000f } },
	{ "dark", "race_02", 2, { 1379.861f, 2979.268f, 760.125f }, { 1563.826f, 3245.481f, 1071.054f }, { 0.000f, 0.000f, 0.000f } },
	{ "dam", "race_01", 1, { -1087.692f, 924.968f, 2314.840f }, { -436.386f, 1207.122f, 2516.126f }, { 0.000f, 0.000f, 0.000f } },
	{ "dam", "race_02", 2, { -907.151f, 1663.877f, 2651.388f }, { -383.977f, 1728.343f, 2715.889f }, { 0.000f, 0.000f, 0.000f } },
	{ "dam", "race_03", 3, { -346.793f, 4023.736f, 2432.039f }, { -227.637f, 4265.669f, 2567.391f }, { 0.000f, 0.000f, 0.000f } },
	{ "dam", "race_04", 4, { -2233.880f, 5563.619f, 2303.261f }, { -537.779f, 5659.619f, 2720.501f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt2", "race_01", 1, { 399.388f, 128.092f, 352.448f }, { 639.736f, 255.972f, 512.565f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt2", "race_02", 2, { -1250.794f, -444.332f, 135.529f }, { -1039.632f, -316.892f, 277.978f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt2", "race_03", 3, { -1527.838f, -1253.531f, -80.604f }, { -1416.479f, -1043.051f, 23.158f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt2", "race_04", 4, { -1663.128f, -1791.882f, 208.259f }, { -1281.318f, -1407.407f, 399.297f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt2", "race_05", 5, { -1599.880f, -3391.978f, 32.169f }, { -1344.029f, -2624.002f, 207.257f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt1", "race_01", 1, { 320.714f, 607.827f, -0.078f }, { 448.078f, 799.713f, 159.602f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt1", "race_02", 2, { -208.635f, -255.876f, -16.164f }, { -40.885f, 83.783f, 175.971f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt1", "race_03", 3, { 582.029f, -1664.469f, -175.840f }, { 791.952f, -1407.732f, -16.156f }, { 0.000f, 0.000f, 0.000f } },
	{ "crypt1", "race_04", 4, { -1008.633f, -504.271f, 96.108f }, { -847.801f, -264.442f, 255.479f }, { 0.000f, 0.000f, 0.000f } },
	{ "church", "race_01", 1, { -351.736f, -655.685f, 64.140f }, { -224.154f, -384.005f, 248.113f }, { 0.000f, 0.000f, 0.000f } },
	{ "church", "race_02", 2, { 976.172f, -191.732f, 320.077f }, { 1471.759f, -64.254f, 544.241f }, { 0.000f, 0.000f, 0.000f } },
	{ "church", "race_03", 3, { -648.430f, -36.198f, 640.025f }, { -567.637f, 43.989f, 787.891f }, { 0.000f, 0.000f, 0.000f } },
	{ "church", "race_04", 4, { 527.286f, 872.185f, 1080.125f }, { 623.286f, 1072.133f, 1209.004f }, { 0.000f, 0.000f, 0.000f } },
	{ "church", "race_05", 5, { 728.017f, 648.137f, 584.237f }, { 964.962f, 767.782f, 831.963f }, { 0.000f, 0.000f, 0.000f } },
	{ "chateau", "race_01", 1, { -833.536f, 1576.086f, -112.630f }, { -669.573f, 1757.662f, 63.962f }, { 0.000f, 0.000f, 0.000f } },
	{ "chateau", "race_02", 2, { 404.213f, 1000.015f, 319.307f }, { 735.074f, 1175.683f, 509.360f }, { 0.000f, 0.000f, 0.000f } },
	{ "chateau", "race_03", 3, { 416.437f, 2352.266f, -47.926f }, { 751.989f, 2543.815f, 128.031f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_01", 1, { -221.841f, -537.596f, -426.766f }, { -86.211f, -445.142f, -268.246f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_02", 2, { -207.937f, 502.528f, -370.057f }, { -48.374f, 607.393f, -182.607f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_03", 3, { -547.380f, 685.047f, 31.644f }, { -447.976f, 785.657f, 159.767f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_04", 4, { 754.650f, -16.032f, 32.391f }, { 866.980f, 142.946f, 268.919f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_05", 5, { 960.779f, 1808.388f, 272.031f }, { 1215.996f, 2111.708f, 446.139f }, { 0.000f, 0.000f, 0.000f } },
	{ "castle", "race_06", 6, { 1359.430f, 2101.019f, -7.875f }, { 1456.343f, 2197.019f, 159.469f }, { 0.000f, 0.000f, 0.000f } },
	{ "boss2", "race_01", 1, { -73.107f, -1619.876f, -63.970f }, { 71.710f, -1523.876f, 311.164f }, { 0.000f, 0.000f, 0.000f } },
	{ "boss2", "race_02", 2, { -63.079f, 39.588f, 30.944f }, { 64.197f, 190.915f, 158.871f }, { 0.000f, 0.000f, 0.000f } },
	{ "boss2", "race_03", 3, { 597.875f, 1167.829f, 31.508f }, { 693.875f, 1263.865f, 173.241f }, { 0.000f, 0.000f, 0.000f } },
	{ "boss1", "race_01", 1, { -2258.614f, 487.715f, 0.637f }, { -2079.280f, 728.409f, 279.702f }, { 0.000f, 0.000f, 0.000f } },
	{ "boss1", "race_02", 2, { -2624.774f, -781.372f, 23.539f }, { -2495.675f, -677.305f, 160.173f }, { 0.000f, 0.000f, 0.000f } },
	{ "baseout", "race_01", 1, { -962.183f, 1856.441f, 8.057f }, { -644.443f, 2108.865f, 261.404f }, { 0.000f, 0.000f, 0.000f } },
	{ "baseout", "race_02", 2, { 461.768f, 714.551f, 118.798f }, { 832.455f, 866.457f, 319.813f }, { 0.000f, 0.000f, 0.000f } },
	{ "baseout", "race_03", 3, { 768.006f, 1023.788f, 315.625f }, { 1597.100f, 1151.121f, 446.774f }, { 0.000f, 0.000f, 0.000f } },
	{ "baseout", "race_04", 4, { 720.643f, 2530.331f, 307.826f }, { 825.780f, 2624.381f, 435.376f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_14", 1, { 948.855f, 157.884f, 88.125f }, { 1108.673f, 2805.060f, 1272.473f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_15", 2, { -3688.551f, 3647.782f, 96.018f }, { -3612.386f, 3685.879f, 416.335f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_16", 3, { -4351.951f, 3732.936f, 368.331f }, { -4213.761f, 5261.387f, 574.534f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_17", 4, { -4393.827f, 4462.219f, 610.631f }, { -4369.644f, 4558.219f, 719.911f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_18", 5, { -3061.713f, 3652.950f, 912.463f }, { -2989.491f, 3802.625f, 1020.155f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_19", 6, { -3263.728f, 3991.115f, 1424.247f }, { -3136.004f, 4545.104f, 1615.499f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_20", 7, { -4543.957f, 5013.301f, 695.937f }, { -4416.477f, 5111.875f, 861.662f }, { 0.000f, 0.000f, 0.000f } },
	{ "assault", "race_21", 8, { -3680.302f, 5067.827f, 872.784f }, { -3488.416f, 5150.721f, 991.379f }, { 0.000f, 0.000f, 0.000f } },
	{ NULL, NULL, 0, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } }
};

void CG_ZoneLoad_f( void );

static int CG_ZoneFindFirstStart( void );
static int CG_ZoneFindStartForRoute( int routeId );
static int CG_ZoneFindNextSplitZone( int afterIndex, int routeId );
static int CG_ZoneSplitNumberForZone( int zoneIndex );
static void CG_ZoneProgress( char *out, int outSize, int *progressIndex, int *progressCount );
static void CG_ZoneRaceProgress( int *racePoints, int *raceTotal );

static const char *CG_ZoneTypeName( zoneType_t type ) {
	switch ( type ) {
	case ZONE_START: return "start";
	case ZONE_FINISH: return "finish";
	case ZONE_RACE: return "race";
	default: return "checkpoint";
	}
}

static zoneType_t CG_ZoneTypeFromString( const char *text ) {
	if ( text && ( !Q_stricmp( text, "start" ) || !Q_stricmp( text, "s" ) ) ) {
		return ZONE_START;
	}
	if ( text && ( !Q_stricmp( text, "finish" ) || !Q_stricmp( text, "stop" ) || !Q_stricmp( text, "end" ) || !Q_stricmp( text, "f" ) ) ) {
		return ZONE_FINISH;
	}
	if ( text && ( !Q_stricmp( text, "race" ) || !Q_stricmp( text, "racepoint" ) || !Q_stricmp( text, "objective" ) || !Q_stricmp( text, "obj" ) || !Q_stricmp( text, "r" ) ) ) {
		return ZONE_RACE;
	}
	return ZONE_CHECKPOINT;
}

static qboolean CG_ZoneIsRouteSplit( const speedrunZone_t *zone ) {
	return zone && zone->type != ZONE_START && zone->type != ZONE_RACE ? qtrue : qfalse;
}

static void CG_ZoneSanitizeName( char *name ) {
	int i;

	if ( !name || !name[0] ) {
		return;
	}
	for ( i = 0; name[i]; i++ ) {
		char c = name[i];
		if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) {
			continue;
		}
		name[i] = '_';
	}
}

static qboolean CG_ZoneShowTimerDecimals( void ) {
	char value[8];
	trap_Cvar_VariableStringBuffer( "sp_timer_decimals", value, sizeof( value ) );
	return ( !value[0] || atoi( value ) != 0 ) ? qtrue : qfalse;
}

static qboolean CG_ZoneCheatsEnabled( void ) {
	char value[8];
	trap_Cvar_VariableStringBuffer( "sv_cheats", value, sizeof( value ) );
	return atoi( value ) != 0 ? qtrue : qfalse;
}

static qboolean CG_ZoneRaceDebugEnabled( void ) {
	return cg_zoneRaceDebug.integer ? qtrue : qfalse;
}

static void CG_ZoneFormatTime( int msec, char *out, int outSize ) {
	int sign;
	int total;
	int hours;
	int minutes;
	int seconds;
	int millis;
	qboolean decimals;

	if ( !out || outSize <= 0 ) {
		return;
	}
	sign = 0;
	if ( msec < 0 ) {
		sign = 1;
		msec = -msec;
	}
	total = msec / 1000;
	hours = total / 3600;
	minutes = ( total / 60 ) % 60;
	seconds = total % 60;
	millis = msec % 1000;
	decimals = CG_ZoneShowTimerDecimals();
	if ( !decimals ) {
		if ( hours > 0 ) {
			Com_sprintf( out, outSize, "%s%i:%02i:%02i", sign ? "-" : "", hours, minutes, seconds );
		} else if ( minutes > 0 ) {
			Com_sprintf( out, outSize, "%s%i:%02i", sign ? "-" : "", minutes, seconds );
		} else {
			Com_sprintf( out, outSize, "%s%i", sign ? "-" : "", seconds );
		}
		return;
	}
	if ( hours > 0 ) {
		Com_sprintf( out, outSize, "%s%i:%02i:%02i.%03i", sign ? "-" : "", hours, minutes, seconds, millis );
	} else if ( minutes > 0 ) {
		Com_sprintf( out, outSize, "%s%i:%02i.%03i", sign ? "-" : "", minutes, seconds, millis );
	} else {
		Com_sprintf( out, outSize, "%s%i.%03i", sign ? "-" : "", seconds, millis );
	}
}

static void CG_ZoneFormatDelta( int msec, char *out, int outSize ) {
	char timeText[32];

	if ( !out || outSize <= 0 ) {
		return;
	}
	CG_ZoneFormatTime( msec < 0 ? -msec : msec, timeText, sizeof( timeText ) );
	Com_sprintf( out, outSize, "%c%s", msec <= 0 ? '-' : '+', timeText );
}

static void CG_ZoneMapBaseName( char *out, int outSize ) {
	char map[MAX_QPATH];
	char *shortName;

	if ( !out || outSize <= 0 ) {
		return;
	}
	out[0] = '\0';
	Q_strncpyz( map, cgs.mapname[0] ? cgs.mapname : "unknown", sizeof( map ) );
	shortName = COM_SkipPath( map );
	Q_strncpyz( out, shortName, outSize );
	COM_StripExtension( out, out );
	if ( !out[0] ) {
		Q_strncpyz( out, "unknown", outSize );
	}
}

static void CG_ZoneBuildFilePath( void ) {
	char base[MAX_QPATH];

	CG_ZoneMapBaseName( base, sizeof( base ) );
	Com_sprintf( zoneState.filePath, sizeof( zoneState.filePath ), "zones/%s.zones", base );
}

static void CG_ZoneSetStatus( const char *status ) {
	Q_strncpyz( zoneState.status, status ? status : "", sizeof( zoneState.status ) );
	trap_Cvar_Set( "sp_zone_status_text", zoneState.status );
}

static int CG_ZoneClampInt( int value, int minValue, int maxValue ) {
	if ( value < minValue ) return minValue;
	if ( value > maxValue ) return maxValue;
	return value;
}

static float CG_ZoneClampFloat( float value, float minValue, float maxValue ) {
	if ( value < minValue ) return minValue;
	if ( value > maxValue ) return maxValue;
	return value;
}

static int CG_ZoneNextOrderForRoute( int routeId ) {
	int i;
	int maxOrder;

	maxOrder = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].routeId == routeId && CG_ZoneIsRouteSplit( &zoneState.zones[i] ) && zoneState.zones[i].order > maxOrder ) {
			maxOrder = zoneState.zones[i].order;
		}
	}
	return maxOrder + 1;
}

static int CG_ZoneNextRaceOrder( void ) {
	int i;
	int maxOrder;

	maxOrder = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_RACE && zoneState.zones[i].order > maxOrder ) {
			maxOrder = zoneState.zones[i].order;
		}
	}
	return maxOrder + 1;
}

static void CG_ZoneDefaultRouteName( int routeId, char *out, int outSize ) {
	if ( !out || outSize <= 0 ) {
		return;
	}
	Com_sprintf( out, outSize, "route_%i", routeId > 0 ? routeId : 0 );
}

static const char *CG_ZoneRouteNameForRoute( int routeId ) {
	int i;

	if ( routeId <= 0 ) {
		return "";
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START && zoneState.zones[i].routeId == routeId ) {
			if ( zoneState.zones[i].routeName[0] ) {
				return zoneState.zones[i].routeName;
			}
			return zoneState.zones[i].name;
		}
	}
	return va( "route_%i", routeId );
}

static void CG_ZoneSetRouteNameForRoute( int routeId, const char *name ) {
	int i;
	char cleanName[ZONE_NAME_LEN];

	if ( routeId <= 0 ) {
		return;
	}
	Q_strncpyz( cleanName, name && name[0] ? name : va( "route_%i", routeId ), sizeof( cleanName ) );
	CG_ZoneSanitizeName( cleanName );
	if ( !cleanName[0] ) {
		CG_ZoneDefaultRouteName( routeId, cleanName, sizeof( cleanName ) );
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START && zoneState.zones[i].routeId == routeId ) {
			Q_strncpyz( zoneState.zones[i].routeName, cleanName, sizeof( zoneState.zones[i].routeName ) );
			return;
		}
	}
}

static qboolean CG_ZoneShouldDrawType( const speedrunZone_t *zone ) {
	if ( !zone ) {
		return qfalse;
	}
	if ( zone->type == ZONE_START ) {
		return cg_zoneDrawStart.integer ? qtrue : qfalse;
	}
	if ( zone->type == ZONE_FINISH ) {
		return cg_zoneDrawFinish.integer ? qtrue : qfalse;
	}
	if ( zone->type == ZONE_RACE ) {
		return CG_ZoneRaceDebugEnabled() && cg_zoneDrawRace.integer ? qtrue : qfalse;
	}
	return cg_zoneDrawCheckpoints.integer ? qtrue : qfalse;
}

static int CG_ZoneDrawRouteId( void ) {
	if ( zoneState.runningRouteId > 0 ) {
		return zoneState.runningRouteId;
	}
	if ( zoneState.activeRouteId > 0 ) {
		return zoneState.activeRouteId;
	}
	if ( zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ) {
		return zoneState.zones[zoneState.selectedZone].routeId;
	}
	return 0;
}

static qboolean CG_ZoneUseFocusedRuntimeDraw( void ) {
	return !cg_zoneEdit.integer && cg_zoneDrawRunTargetOnly.integer ? qtrue : qfalse;
}

static int CG_ZoneFocusedRouteId( void ) {
	if ( zoneState.runningRouteId > 0 ) {
		return zoneState.runningRouteId;
	}
	return CG_ZoneDrawRouteId();
}

static int CG_ZoneFocusedPrimarySplit( void ) {
	int routeId;

	if ( zoneState.running ) {
		return zoneState.nextZone;
	}
	if ( zoneState.wasInStartZone < 0 ) {
		return -1;
	}
	routeId = CG_ZoneFocusedRouteId();
	return routeId > 0 ? CG_ZoneFindNextSplitZone( -1, routeId ) : -1;
}

static qboolean CG_ZoneShouldDrawZone( int index ) {
	speedrunZone_t *zone;
	int routeId;

	if ( index < 0 || index >= zoneState.count ) {
		return qfalse;
	}
	zone = &zoneState.zones[index];
	if ( !CG_ZoneShouldDrawType( zone ) ) {
		return qfalse;
	}
	if ( CG_ZoneUseFocusedRuntimeDraw() ) {
		int primarySplit;

		if ( zoneState.running ) {
			return zone->type == ZONE_RACE || index == zoneState.nextZone ? qtrue : qfalse;
		}
		if ( zone->type == ZONE_START ) {
			return qtrue;
		}
		if ( zone->type == ZONE_RACE ) {
			return qtrue;
		}
		primarySplit = CG_ZoneFocusedPrimarySplit();
		return index == primarySplit ? qtrue : qfalse;
	}
	if ( !cg_zoneDrawActiveRouteOnly.integer || index == zoneState.selectedZone ) {
		return qtrue;
	}
	if ( zone->type == ZONE_RACE ) {
		return qtrue;
	}
	routeId = CG_ZoneDrawRouteId();
	return routeId <= 0 || zone->routeId == routeId ? qtrue : qfalse;
}

static qboolean CG_ZoneIsDimmedRoute( const speedrunZone_t *zone ) {
	int routeId;

	if ( !zone || !cg_zoneEdit.integer || !cg_zoneDimInactive.integer ) {
		return qfalse;
	}
	routeId = CG_ZoneDrawRouteId();
	return routeId > 0 && zone->routeId > 0 && zone->routeId != routeId ? qtrue : qfalse;
}

static void CG_ZoneShiftRouteOrders( int routeId, int minOrder, int delta ) {
	int i;

	if ( routeId <= 0 || minOrder <= 0 || !delta ) {
		return;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].routeId == routeId && CG_ZoneIsRouteSplit( &zoneState.zones[i] ) && zoneState.zones[i].order >= minOrder ) {
			zoneState.zones[i].order += delta;
		}
	}
}

static int CG_ZoneFirstFinishOrderForRoute( int routeId ) {
	int i;
	int bestOrder;

	bestOrder = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].routeId == routeId && zoneState.zones[i].type == ZONE_FINISH && zoneState.zones[i].order > 0 && ( !bestOrder || zoneState.zones[i].order < bestOrder ) ) {
			bestOrder = zoneState.zones[i].order;
		}
	}
	return bestOrder;
}

static void CG_ZoneSortRouteSplitIndices( int routeId, int *indices, int *count ) {
	int i;
	int j;

	if ( !indices || !count ) {
		return;
	}
	*count = 0;
	if ( routeId <= 0 ) {
		return;
	}
	for ( i = 0; i < zoneState.count && *count < ZONE_MAX_ZONES; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].routeId == routeId && CG_ZoneIsRouteSplit( &zoneState.zones[i] ) ) {
			indices[( *count )++] = i;
		}
	}
	for ( i = 0; i < *count - 1; i++ ) {
		for ( j = i + 1; j < *count; j++ ) {
			int a = indices[i];
			int b = indices[j];
			if ( zoneState.zones[b].order < zoneState.zones[a].order || ( zoneState.zones[b].order == zoneState.zones[a].order && b < a ) ) {
				indices[i] = b;
				indices[j] = a;
			}
		}
	}
}

static void CG_ZoneRefreshRouteOrderAndNames( int routeId ) {
	int indices[ZONE_MAX_ZONES];
	int count;
	int i;
	int checkpointNumber;
	int finishNumber;

	if ( routeId <= 0 ) {
		return;
	}
	CG_ZoneSortRouteSplitIndices( routeId, indices, &count );
	for ( i = 0; i < count; i++ ) {
		zoneState.zones[indices[i]].order = i + 1;
	}
	if ( !cg_zoneAutoNames.integer ) {
		return;
	}
	checkpointNumber = 1;
	finishNumber = 1;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].routeId == routeId && zoneState.zones[i].type == ZONE_START ) {
			Com_sprintf( zoneState.zones[i].name, sizeof( zoneState.zones[i].name ), "start_%02i", routeId );
		}
	}
	for ( i = 0; i < count; i++ ) {
		speedrunZone_t *zone = &zoneState.zones[indices[i]];
		if ( zone->type == ZONE_FINISH ) {
			Com_sprintf( zone->name, sizeof( zone->name ), "finish_%02i", finishNumber++ );
		} else {
			Com_sprintf( zone->name, sizeof( zone->name ), "checkpoint_%02i", checkpointNumber++ );
		}
	}
}

static int CG_ZoneBestTotalForRoute( int routeId ) {
	int i;
	int best;
	int startIndex;

	if ( routeId <= 0 ) {
		return 0;
	}
	best = 0;
	startIndex = CG_ZoneFindStartForRoute( routeId );
	if ( startIndex >= 0 ) {
		best = zoneState.zones[startIndex].bestTotalMsec;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		speedrunZone_t *zone = &zoneState.zones[i];
		if ( zone->active && zone->routeId == routeId && zone->type == ZONE_FINISH && zone->bestTotalMsec > 0 && ( !best || zone->bestTotalMsec < best ) ) {
			best = zone->bestTotalMsec;
		}
	}
	return best;
}

static void CG_ZoneSyncRouteBestTotals( void ) {
	int i;

	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START ) {
			zoneState.zones[i].bestTotalMsec = CG_ZoneBestTotalForRoute( zoneState.zones[i].routeId );
		}
	}
}

static int CG_ZoneInsertOrderForRoute( int routeId ) {
	int finishOrder;

	if ( zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count && zoneState.zones[zoneState.selectedZone].routeId == routeId && CG_ZoneIsRouteSplit( &zoneState.zones[zoneState.selectedZone] ) && zoneState.zones[zoneState.selectedZone].order > 0 ) {
		return zoneState.zones[zoneState.selectedZone].order;
	}
	finishOrder = CG_ZoneFirstFinishOrderForRoute( routeId );
	if ( finishOrder > 0 ) {
		return finishOrder;
	}
	return CG_ZoneNextOrderForRoute( routeId );
}

static void CG_ZoneFormatRow( int index, char *out, int outSize ) {
	speedrunZone_t *zone;

	if ( !out || outSize <= 0 ) {
		return;
	}
	out[0] = '\0';
	if ( index < 0 || index >= zoneState.count ) {
		return;
	}
	zone = &zoneState.zones[index];
	Com_sprintf( out, outSize, "%i|%i|%i|%s|%s|%i|%i|%i", index, zone->routeId, zone->order, CG_ZoneTypeName( zone->type ), zone->name, zone->bestSegmentMsec, zone->bestTotalMsec, zone->builtin ? 1 : 0 );
}

static void CG_ZoneUpdateListCvars( void ) {
	char name[32];
	char value[160];
	int i;
	int lastOrder;
	int routeId;
	int routeCount;

	routeId = zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ? zoneState.zones[zoneState.selectedZone].routeId : zoneState.activeRouteId;
	trap_Cvar_Set( "sp_zone_view_route", va( "%i", routeId ) );
	trap_Cvar_Set( "sp_zone_row_count", va( "%i", zoneState.count ) );
	routeCount = 0;
	for ( i = 0; i < ZONE_MAX_ZONES; i++ ) {
		Com_sprintf( name, sizeof( name ), "sp_zone_row_%02i", i );
		if ( i < zoneState.count ) {
			CG_ZoneFormatRow( i, value, sizeof( value ) );
			trap_Cvar_Set( name, value );
		} else {
			trap_Cvar_Set( name, "" );
		}
	}
	lastOrder = 0;
	while ( routeCount < ZONE_MAX_ZONES ) {
		int bestIndex;
		int bestOrder;

		bestIndex = -1;
		bestOrder = 999999;
		for ( i = 0; i < zoneState.count; i++ ) {
			speedrunZone_t *zone = &zoneState.zones[i];
			if ( !zone->active || zone->routeId != routeId || !CG_ZoneIsRouteSplit( zone ) || zone->order <= lastOrder ) {
				continue;
			}
			if ( zone->order < bestOrder ) {
				bestOrder = zone->order;
				bestIndex = i;
			}
		}
		if ( bestIndex < 0 ) {
			break;
		}
		Com_sprintf( name, sizeof( name ), "sp_zone_route_row_%02i", routeCount );
		CG_ZoneFormatRow( bestIndex, value, sizeof( value ) );
		trap_Cvar_Set( name, value );
		lastOrder = bestOrder;
		routeCount++;
	}
	for ( i = routeCount; i < ZONE_MAX_ZONES; i++ ) {
		Com_sprintf( name, sizeof( name ), "sp_zone_route_row_%02i", i );
		trap_Cvar_Set( name, "" );
	}
	trap_Cvar_Set( "sp_zone_route_row_count", va( "%i", routeCount ) );
}

static void CG_ZoneUpdateCvars( void ) {
	char text[128];
	char progressText[32];
	speedrunZone_t *zone;
	int activeStart;
	int viewRoute;
	int progressIndex;
	int progressCount;
	int racePoints;
	int raceTotal;

	trap_Cvar_Set( "sp_zone_file", zoneState.filePath );
	trap_Cvar_Set( "sp_zone_count", va( "%i", zoneState.count ) );
	trap_Cvar_Set( "sp_zone_selected", va( "%i", zoneState.selectedZone ) );
	trap_Cvar_Set( "sp_zone_last_split", zoneState.lastSplit );
	trap_Cvar_Set( "sp_zone_last_delta", zoneState.lastDelta );
	trap_Cvar_Set( "sp_zone_last_total_delta", zoneState.lastTotalDelta );
	trap_Cvar_Set( "sp_zone_last_delta_pb", zoneState.lastSplitWasPb ? "1" : "0" );
	trap_Cvar_Set( "sp_zone_last_total_delta_pb", zoneState.lastTotalWasPb ? "1" : "0" );
	trap_Cvar_Set( "sp_zone_delta_visible", ( ( zoneState.lastDelta[0] || zoneState.lastTotalDelta[0] ) && cg.time - zoneState.lastSplitTimeShown < 4500 ) ? "1" : "0" );
	CG_ZoneProgress( progressText, sizeof( progressText ), &progressIndex, &progressCount );
	trap_Cvar_Set( "sp_zone_progress_text", progressText );
	trap_Cvar_Set( "sp_zone_progress_index", va( "%i", progressIndex ) );
	trap_Cvar_Set( "sp_zone_progress_count", va( "%i", progressCount ) );
	CG_ZoneRaceProgress( &racePoints, &raceTotal );
	trap_Cvar_Set( "sp_zone_completed_index", va( "%i", racePoints ) );
	trap_Cvar_Set( "sp_zone_race_points", va( "%i", racePoints ) );
	trap_Cvar_Set( "sp_zone_race_total", va( "%i", raceTotal ) );
	trap_Cvar_Set( "sp_race_objectives_found", va( "%i", cg.numObjectivesFound ) );
	trap_Cvar_Set( "sp_race_objectives_total", va( "%i", cg.numObjectives ) );
	trap_Cvar_Set( "sp_zone_active_route", va( "%i", zoneState.activeRouteId ) );
	viewRoute = zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ? zoneState.zones[zoneState.selectedZone].routeId : zoneState.activeRouteId;
	trap_Cvar_Set( "sp_zone_route_name", CG_ZoneRouteNameForRoute( viewRoute ) );
	{
		int recordMsec;
		recordMsec = zoneState.running && zoneState.runningBestTotalMsec > 0 ? zoneState.runningBestTotalMsec : CG_ZoneBestTotalForRoute( viewRoute > 0 ? viewRoute : zoneState.activeRouteId );
		trap_Cvar_Set( "sp_zone_record_msec", va( "%i", recordMsec ) );
		if ( recordMsec > 0 ) {
			CG_ZoneFormatTime( recordMsec, text, sizeof( text ) );
			trap_Cvar_Set( "sp_zone_record_time", text );
		} else {
			trap_Cvar_Set( "sp_zone_record_time", "-" );
		}
	}
	activeStart = CG_ZoneFindStartForRoute( zoneState.activeRouteId );
	if ( activeStart < 0 ) {
		activeStart = CG_ZoneFindFirstStart();
	}
	trap_Cvar_Set( "sp_zone_active_start", va( "%i", activeStart ) );
	trap_Cvar_Set( "sp_zone_active_start_name", activeStart >= 0 ? zoneState.zones[activeStart].name : "" );
	if ( zoneState.running ) {
		CG_ZoneFormatTime( cg.time - zoneState.startTime, text, sizeof( text ) );
		trap_Cvar_Set( "sp_zone_run_time", text );
	} else if ( zoneState.finished ) {
		CG_ZoneFormatTime( zoneState.finishTime - zoneState.startTime, text, sizeof( text ) );
		trap_Cvar_Set( "sp_zone_run_time", text );
	} else {
		trap_Cvar_Set( "sp_zone_run_time", "0.000" );
	}

	if ( zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ) {
		zone = &zoneState.zones[zoneState.selectedZone];
		trap_Cvar_Set( "sp_zone_selected_name", zone->name );
		trap_Cvar_Set( "sp_zone_selected_type", CG_ZoneTypeName( zone->type ) );
		trap_Cvar_Set( "sp_zone_selected_builtin", zone->builtin ? "1" : "0" );
		trap_Cvar_Set( "sp_zone_selected_route", va( "%i", zone->routeId ) );
		trap_Cvar_Set( "sp_zone_selected_order", va( "%i", zone->order ) );
		Com_sprintf( text, sizeof( text ), "%.1f %.1f %.1f", zone->mins[0], zone->mins[1], zone->mins[2] );
		trap_Cvar_Set( "sp_zone_selected_mins", text );
		Com_sprintf( text, sizeof( text ), "%.1f %.1f %.1f", zone->maxs[0], zone->maxs[1], zone->maxs[2] );
		trap_Cvar_Set( "sp_zone_selected_maxs", text );
		Com_sprintf( text, sizeof( text ), "%.1f %.1f %.1f", zone->angles[0], zone->angles[1], zone->angles[2] );
		trap_Cvar_Set( "sp_zone_selected_angles", text );
		trap_Cvar_Set( "sp_zone_selected_best", va( "%i", zone->bestSegmentMsec ) );
		trap_Cvar_Set( "sp_zone_selected_best_total", va( "%i", zone->bestTotalMsec ) );
	} else {
		trap_Cvar_Set( "sp_zone_selected_name", "" );
		trap_Cvar_Set( "sp_zone_selected_type", "" );
		trap_Cvar_Set( "sp_zone_selected_builtin", "0" );
		trap_Cvar_Set( "sp_zone_selected_route", "0" );
		trap_Cvar_Set( "sp_zone_selected_order", "0" );
		trap_Cvar_Set( "sp_zone_selected_mins", "" );
		trap_Cvar_Set( "sp_zone_selected_maxs", "" );
		trap_Cvar_Set( "sp_zone_selected_angles", "" );
		trap_Cvar_Set( "sp_zone_selected_best", "0" );
		trap_Cvar_Set( "sp_zone_selected_best_total", "0" );
	}
	CG_ZoneUpdateListCvars();
}

static void CG_ZoneNormalizeBounds( speedrunZone_t *zone ) {
	int i;

	if ( !zone ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		float tmp;
		if ( zone->mins[i] > zone->maxs[i] ) {
			tmp = zone->mins[i];
			zone->mins[i] = zone->maxs[i];
			zone->maxs[i] = tmp;
		}
		if ( zone->maxs[i] - zone->mins[i] < ZONE_MIN_SIZE ) {
			float mid = ( zone->mins[i] + zone->maxs[i] ) * 0.5f;
			zone->mins[i] = mid - ZONE_MIN_SIZE * 0.5f;
			zone->maxs[i] = mid + ZONE_MIN_SIZE * 0.5f;
		}
	}
}

static void CG_ZoneNormalizeAngles( speedrunZone_t *zone ) {
	int i;

	if ( !zone ) {
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		zone->angles[i] = AngleNormalize180( zone->angles[i] );
	}
}

static void CG_ZoneBuildAxesFromAngles( const vec3_t angles, vec3_t axes[3] ) {
	vec3_t right;

	AngleVectors( angles, axes[0], right, axes[2] );
	VectorScale( right, -1.0f, axes[1] );
}

static void CG_ZoneBuildAxes( const speedrunZone_t *zone, vec3_t axes[3] ) {
	if ( !zone ) {
		VectorSet( axes[0], 1.0f, 0.0f, 0.0f );
		VectorSet( axes[1], 0.0f, 1.0f, 0.0f );
		VectorSet( axes[2], 0.0f, 0.0f, 1.0f );
		return;
	}
	CG_ZoneBuildAxesFromAngles( zone->angles, axes );
}

static void CG_ZoneGetCenter( const speedrunZone_t *zone, vec3_t out ) {
	out[0] = ( zone->mins[0] + zone->maxs[0] ) * 0.5f;
	out[1] = ( zone->mins[1] + zone->maxs[1] ) * 0.5f;
	out[2] = ( zone->mins[2] + zone->maxs[2] ) * 0.5f;
}

static void CG_ZoneGetHalfExtents( const speedrunZone_t *zone, vec3_t out ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		out[i] = ( zone->maxs[i] - zone->mins[i] ) * 0.5f;
		if ( out[i] < ZONE_MIN_SIZE * 0.5f ) {
			out[i] = ZONE_MIN_SIZE * 0.5f;
		}
	}
}

static void CG_ZoneSetCenterAndHalfExtents( speedrunZone_t *zone, const vec3_t center, const vec3_t half ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		float size = half[i];
		if ( size < ZONE_MIN_SIZE * 0.5f ) {
			size = ZONE_MIN_SIZE * 0.5f;
		}
		zone->mins[i] = center[i] - size;
		zone->maxs[i] = center[i] + size;
	}
	CG_ZoneNormalizeBounds( zone );
}

static void CG_ZoneSetLocalBounds( speedrunZone_t *zone, const vec3_t oldCenter, const vec3_t localMins, const vec3_t localMaxs ) {
	vec3_t axes[3];
	vec3_t center;
	vec3_t half;
	int i;

	VectorCopy( oldCenter, center );
	CG_ZoneBuildAxes( zone, axes );
	for ( i = 0; i < 3; i++ ) {
		float minValue = localMins[i];
		float maxValue = localMaxs[i];
		float mid;

		if ( minValue > maxValue ) {
			float tmp = minValue;
			minValue = maxValue;
			maxValue = tmp;
		}
		if ( maxValue - minValue < ZONE_MIN_SIZE ) {
			mid = ( minValue + maxValue ) * 0.5f;
			minValue = mid - ZONE_MIN_SIZE * 0.5f;
			maxValue = mid + ZONE_MIN_SIZE * 0.5f;
		}
		mid = ( minValue + maxValue ) * 0.5f;
		half[i] = ( maxValue - minValue ) * 0.5f;
		VectorMA( center, mid, axes[i], center );
	}
	CG_ZoneSetCenterAndHalfExtents( zone, center, half );
}

static void CG_ZoneLocalToWorld( const speedrunZone_t *zone, const vec3_t local, vec3_t out ) {
	vec3_t center;
	vec3_t axes[3];

	CG_ZoneGetCenter( zone, center );
	CG_ZoneBuildAxes( zone, axes );
	VectorCopy( center, out );
	VectorMA( out, local[0], axes[0], out );
	VectorMA( out, local[1], axes[1], out );
	VectorMA( out, local[2], axes[2], out );
}

static void CG_ZoneWorldToLocalDelta( const speedrunZone_t *zone, const vec3_t point, vec3_t local ) {
	vec3_t center;
	vec3_t axes[3];
	vec3_t delta;

	CG_ZoneGetCenter( zone, center );
	CG_ZoneBuildAxes( zone, axes );
	VectorSubtract( point, center, delta );
	local[0] = DotProduct( delta, axes[0] );
	local[1] = DotProduct( delta, axes[1] );
	local[2] = DotProduct( delta, axes[2] );
}

static void CG_ZoneGetCorner( const speedrunZone_t *zone, int corner, vec3_t out ) {
	vec3_t half;
	vec3_t local;

	CG_ZoneGetHalfExtents( zone, half );
	local[0] = ( corner & 1 ) ? half[0] : -half[0];
	local[1] = ( corner & 2 ) ? half[1] : -half[1];
	local[2] = ( corner & 4 ) ? half[2] : -half[2];
	CG_ZoneLocalToWorld( zone, local, out );
}

static void CG_ZoneGetCorners( const speedrunZone_t *zone, vec3_t corners[8] ) {
	CG_ZoneGetCorner( zone, 0, corners[0] );
	CG_ZoneGetCorner( zone, 1, corners[1] );
	CG_ZoneGetCorner( zone, 3, corners[2] );
	CG_ZoneGetCorner( zone, 2, corners[3] );
	CG_ZoneGetCorner( zone, 4, corners[4] );
	CG_ZoneGetCorner( zone, 5, corners[5] );
	CG_ZoneGetCorner( zone, 7, corners[6] );
	CG_ZoneGetCorner( zone, 6, corners[7] );
}

static void CG_ZoneSetCorner( speedrunZone_t *zone, int corner, const vec3_t point ) {
	vec3_t center;
	vec3_t half;
	vec3_t local;
	vec3_t localMins;
	vec3_t localMaxs;
	int i;

	CG_ZoneGetCenter( zone, center );
	CG_ZoneGetHalfExtents( zone, half );
	CG_ZoneWorldToLocalDelta( zone, point, local );
	for ( i = 0; i < 3; i++ ) {
		localMins[i] = -half[i];
		localMaxs[i] = half[i];
		if ( corner & ( 1 << i ) ) {
			localMaxs[i] = local[i];
		} else {
			localMins[i] = local[i];
		}
	}
	CG_ZoneSetLocalBounds( zone, center, localMins, localMaxs );
}

static qboolean CG_ZonePointInside( const speedrunZone_t *zone, const vec3_t point ) {
	vec3_t half;
	vec3_t local;
	int i;

	if ( !zone ) {
		return qfalse;
	}
	CG_ZoneGetHalfExtents( zone, half );
	CG_ZoneWorldToLocalDelta( zone, point, local );
	for ( i = 0; i < 3; i++ ) {
		if ( local[i] < -half[i] || local[i] > half[i] ) {
			return qfalse;
		}
	}
	return qtrue;
}

static qboolean CG_ZoneBoundsVisible( const speedrunZone_t *zone ) {
	int corner;
	int frontCount;
	int outsideLeft;
	int outsideRight;
	int outsideTop;
	int outsideBottom;
	float tanHalfFovX;
	float tanHalfFovY;

	if ( !zone ) {
		return qfalse;
	}
	if ( CG_ZonePointInside( zone, cg.refdef.vieworg ) ) {
		return qtrue;
	}

	tanHalfFovX = tan( DEG2RAD( cg.refdef.fov_x * 0.5f ) ) * 1.12f;
	tanHalfFovY = tan( DEG2RAD( cg.refdef.fov_y * 0.5f ) ) * 1.12f;
	if ( tanHalfFovX <= 0.0f || tanHalfFovY <= 0.0f ) {
		return qtrue;
	}

	frontCount = 0;
	outsideLeft = 0;
	outsideRight = 0;
	outsideTop = 0;
	outsideBottom = 0;
	for ( corner = 0; corner < 8; corner++ ) {
		vec3_t point;
		vec3_t delta;
		float fwd;
		float lft;
		float up;
		float xLimit;
		float yLimit;

		CG_ZoneGetCorner( zone, corner, point );
		VectorSubtract( point, cg.refdef.vieworg, delta );
		fwd = DotProduct( delta, cg.refdef.viewaxis[0] );
		if ( fwd < 1.0f ) {
			continue;
		}

		frontCount++;
		lft = DotProduct( delta, cg.refdef.viewaxis[1] );
		up = DotProduct( delta, cg.refdef.viewaxis[2] );
		xLimit = fwd * tanHalfFovX;
		yLimit = fwd * tanHalfFovY;
		if ( lft > xLimit ) outsideLeft++;
		if ( lft < -xLimit ) outsideRight++;
		if ( up > yLimit ) outsideTop++;
		if ( up < -yLimit ) outsideBottom++;
	}

	if ( frontCount <= 0 ) {
		return qfalse;
	}
	return outsideLeft == frontCount || outsideRight == frontCount || outsideTop == frontCount || outsideBottom == frontCount ? qfalse : qtrue;
}

static qboolean CG_ZoneHandlesVisible( int index ) {
	vec3_t center;
	vec3_t delta;
	float maxDist;

	if ( !cg_zoneEdit.integer || !cg_zoneDrawHandles.integer || index < 0 || index >= zoneState.count ) {
		return qfalse;
	}
	if ( zoneState.dragging && zoneState.dragZone == index ) {
		return qtrue;
	}
	maxDist = CG_ZoneClampFloat( cg_zoneHandleMaxDist.value, 128.0f, 8192.0f );
	CG_ZoneGetCenter( &zoneState.zones[index], center );
	VectorSubtract( center, cg.refdef.vieworg, delta );
	return DotProduct( delta, delta ) <= maxDist * maxDist ? qtrue : qfalse;
}

static qboolean CG_ZoneHandleIsFace( int handle ) {
	return handle >= ZONE_HANDLE_FACE_X_MIN && handle <= ZONE_HANDLE_FACE_Z_MAX ? qtrue : qfalse;
}

static int CG_ZoneHandleFaceAxis( int handle ) {
	if ( handle == ZONE_HANDLE_FACE_X_MIN || handle == ZONE_HANDLE_FACE_X_MAX ) return 0;
	if ( handle == ZONE_HANDLE_FACE_Y_MIN || handle == ZONE_HANDLE_FACE_Y_MAX ) return 1;
	return 2;
}

static qboolean CG_ZoneHandleFaceIsMax( int handle ) {
	return handle == ZONE_HANDLE_FACE_X_MAX || handle == ZONE_HANDLE_FACE_Y_MAX || handle == ZONE_HANDLE_FACE_Z_MAX ? qtrue : qfalse;
}

static qboolean CG_ZoneHandleIsRotation( int handle ) {
	return handle == ZONE_HANDLE_ROTATE_PITCH || handle == ZONE_HANDLE_ROTATE_YAW || handle == ZONE_HANDLE_ROTATE_ROLL ? qtrue : qfalse;
}

static int CG_ZoneHandleRotationAxis( int handle ) {
	if ( handle == ZONE_HANDLE_ROTATE_PITCH ) return PITCH;
	if ( handle == ZONE_HANDLE_ROTATE_YAW ) return YAW;
	return ROLL;
}

static qboolean CG_ZoneHandleAvailable( int index, int handle ) {
	if ( index < 0 || index >= zoneState.count ) {
		return qfalse;
	}
	if ( !CG_ZoneHandleIsRotation( handle ) ) {
		return qtrue;
	}
	if ( !cg_zoneRotationGizmo.integer || index != zoneState.selectedZone || zoneState.zones[index].builtin ) {
		return qfalse;
	}
	return qtrue;
}

static void CG_ZoneGetRotationHandlePoint( const speedrunZone_t *zone, int handle, vec3_t out ) {
	vec3_t center;
	vec3_t axes[3];
	vec3_t half;
	float radius;
	int axis;

	CG_ZoneGetCenter( zone, center );
	CG_ZoneBuildAxes( zone, axes );
	CG_ZoneGetHalfExtents( zone, half );
	radius = half[0];
	if ( half[1] > radius ) radius = half[1];
	if ( half[2] > radius ) radius = half[2];
	radius += 32.0f;
	if ( radius < 48.0f ) radius = 48.0f;
	VectorCopy( center, out );
	axis = CG_ZoneHandleRotationAxis( handle );
	if ( axis == PITCH ) {
		VectorMA( out, radius, axes[2], out );
	} else if ( axis == YAW ) {
		VectorMA( out, radius, axes[0], out );
	} else {
		VectorMA( out, radius, axes[1], out );
	}
}

static void CG_ZoneRotationPlaneAxes( const speedrunZone_t *zone, int handle, vec3_t primary, vec3_t secondary ) {
	vec3_t axes[3];
	int axis;

	CG_ZoneBuildAxes( zone, axes );
	axis = CG_ZoneHandleRotationAxis( handle );
	if ( axis == PITCH ) {
		VectorCopy( axes[0], primary );
		VectorCopy( axes[2], secondary );
	} else if ( axis == YAW ) {
		VectorCopy( axes[0], primary );
		VectorCopy( axes[1], secondary );
	} else {
		VectorCopy( axes[1], primary );
		VectorCopy( axes[2], secondary );
	}
}

static float CG_ZoneRotationAngleForPoint( const speedrunZone_t *zone, const vec3_t angles, int handle, const vec3_t point ) {
	speedrunZone_t temp;
	vec3_t center;
	vec3_t delta;
	vec3_t primary;
	vec3_t secondary;
	float x;
	float y;

	temp = *zone;
	VectorCopy( angles, temp.angles );
	CG_ZoneGetCenter( &temp, center );
	CG_ZoneRotationPlaneAxes( &temp, handle, primary, secondary );
	VectorSubtract( point, center, delta );
	x = DotProduct( delta, primary );
	y = DotProduct( delta, secondary );
	if ( fabs( x ) < 0.001f && fabs( y ) < 0.001f ) {
		return 0.0f;
	}
	return RAD2DEG( atan2( y, x ) );
}

static void CG_ZoneApplyRotationDrag( speedrunZone_t *zone, int handle, const vec3_t point ) {
	float currentAngle;
	float delta;
	int axis;

	currentAngle = CG_ZoneRotationAngleForPoint( zone, zoneState.dragStartAngles, handle, point );
	delta = AngleNormalize180( currentAngle - zoneState.dragStartAngle );
	axis = CG_ZoneHandleRotationAxis( handle );
	zone->angles[axis] = AngleNormalize180( zoneState.dragStartAngles[axis] + delta );
}

static void CG_ZoneMoveBy( speedrunZone_t *zone, const vec3_t delta ) {
	int axis;

	for ( axis = 0; axis < 3; axis++ ) {
		zone->mins[axis] += delta[axis];
		zone->maxs[axis] += delta[axis];
	}
}

static void CG_ZoneGetHandlePoint( const speedrunZone_t *zone, int handle, vec3_t out ) {
	if ( CG_ZoneHandleIsRotation( handle ) ) {
		CG_ZoneGetRotationHandlePoint( zone, handle, out );
		return;
	}
	if ( CG_ZoneHandleIsFace( handle ) ) {
		int axis;
		vec3_t half;
		vec3_t local;

		axis = CG_ZoneHandleFaceAxis( handle );
		CG_ZoneGetHalfExtents( zone, half );
		VectorClear( local );
		local[axis] = CG_ZoneHandleFaceIsMax( handle ) ? half[axis] : -half[axis];
		CG_ZoneLocalToWorld( zone, local, out );
		return;
	}
	if ( handle == ZONE_HANDLE_CENTER ) {
		CG_ZoneGetCenter( zone, out );
		return;
	}
	CG_ZoneGetCorner( zone, handle, out );
}

static void CG_ZoneSetHandlePoint( speedrunZone_t *zone, int handle, const vec3_t point ) {
	if ( CG_ZoneHandleIsFace( handle ) ) {
		int axis;
		float value;
		vec3_t center;
		vec3_t half;
		vec3_t local;
		vec3_t localMins;
		vec3_t localMaxs;
		int i;

		axis = CG_ZoneHandleFaceAxis( handle );
		CG_ZoneGetCenter( zone, center );
		CG_ZoneGetHalfExtents( zone, half );
		CG_ZoneWorldToLocalDelta( zone, point, local );
		for ( i = 0; i < 3; i++ ) {
			localMins[i] = -half[i];
			localMaxs[i] = half[i];
		}
		value = local[axis];
		if ( CG_ZoneHandleFaceIsMax( handle ) ) {
			if ( value < localMins[axis] + ZONE_MIN_SIZE ) {
				value = localMins[axis] + ZONE_MIN_SIZE;
			}
			localMaxs[axis] = value;
		} else {
			if ( value > localMaxs[axis] - ZONE_MIN_SIZE ) {
				value = localMaxs[axis] - ZONE_MIN_SIZE;
			}
			localMins[axis] = value;
		}
		CG_ZoneSetLocalBounds( zone, center, localMins, localMaxs );
		return;
	}
	if ( handle == ZONE_HANDLE_CENTER ) {
		vec3_t center;
		vec3_t delta;

		CG_ZoneGetCenter( zone, center );
		VectorSubtract( point, center, delta );
		CG_ZoneMoveBy( zone, delta );
		return;
	}
	CG_ZoneSetCorner( zone, handle, point );
}

static qboolean CG_ZonePlayerBounds( vec3_t mins, vec3_t maxs ) {
	playerState_t *ps;
	int i;

	if ( !cg.snap ) {
		return qfalse;
	}
	ps = &cg.predictedPlayerState;
	for ( i = 0; i < 3; i++ ) {
		mins[i] = ps->origin[i] + ps->mins[i];
		maxs[i] = ps->origin[i] + ps->maxs[i];
	}
	return qtrue;
}

static qboolean CG_ZoneBoundsOverlap( const vec3_t aMins, const vec3_t aMaxs, const vec3_t bMins, const vec3_t bMaxs ) {
	if ( aMaxs[0] < bMins[0] || aMins[0] > bMaxs[0] ) return qfalse;
	if ( aMaxs[1] < bMins[1] || aMins[1] > bMaxs[1] ) return qfalse;
	if ( aMaxs[2] < bMins[2] || aMins[2] > bMaxs[2] ) return qfalse;
	return qtrue;
}

static qboolean CG_ZoneAABBOverlapOBB( const vec3_t aMins, const vec3_t aMaxs, const speedrunZone_t *zone ) {
	vec3_t aCenter;
	vec3_t aHalf;
	vec3_t bCenter;
	vec3_t bHalf;
	vec3_t axes[3];
	vec3_t t;
	float r[3][3];
	float absR[3][3];
	int i;
	int j;

	if ( !zone ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		aCenter[i] = ( aMins[i] + aMaxs[i] ) * 0.5f;
		aHalf[i] = ( aMaxs[i] - aMins[i] ) * 0.5f;
		if ( aHalf[i] < 0.0f ) aHalf[i] = -aHalf[i];
	}
	CG_ZoneGetCenter( zone, bCenter );
	CG_ZoneGetHalfExtents( zone, bHalf );
	CG_ZoneBuildAxes( zone, axes );
	VectorSubtract( bCenter, aCenter, t );

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			r[i][j] = axes[j][i];
			absR[i][j] = fabs( r[i][j] ) + 0.0001f;
		}
	}

	for ( i = 0; i < 3; i++ ) {
		float rb = bHalf[0] * absR[i][0] + bHalf[1] * absR[i][1] + bHalf[2] * absR[i][2];
		if ( fabs( t[i] ) > aHalf[i] + rb ) {
			return qfalse;
		}
	}

	for ( j = 0; j < 3; j++ ) {
		float ra = aHalf[0] * absR[0][j] + aHalf[1] * absR[1][j] + aHalf[2] * absR[2][j];
		float tb = fabs( DotProduct( t, axes[j] ) );
		if ( tb > ra + bHalf[j] ) {
			return qfalse;
		}
	}

	for ( i = 0; i < 3; i++ ) {
		int i1 = ( i + 1 ) % 3;
		int i2 = ( i + 2 ) % 3;
		for ( j = 0; j < 3; j++ ) {
			int j1 = ( j + 1 ) % 3;
			int j2 = ( j + 2 ) % 3;
			float ra = aHalf[i1] * absR[i2][j] + aHalf[i2] * absR[i1][j];
			float rb = bHalf[j1] * absR[i][j2] + bHalf[j2] * absR[i][j1];
			float value = fabs( t[i2] * r[i1][j] - t[i1] * r[i2][j] );
			if ( value > ra + rb ) {
				return qfalse;
			}
		}
	}

	return qtrue;
}

static qboolean CG_ZoneContainsPlayer( int index ) {
	vec3_t playerMins;
	vec3_t playerMaxs;

	if ( index < 0 || index >= zoneState.count || !zoneState.zones[index].active ) {
		return qfalse;
	}
	if ( !CG_ZonePlayerBounds( playerMins, playerMaxs ) ) {
		return qfalse;
	}
	if ( zoneState.zones[index].angles[0] == 0.0f && zoneState.zones[index].angles[1] == 0.0f && zoneState.zones[index].angles[2] == 0.0f ) {
		return CG_ZoneBoundsOverlap( playerMins, playerMaxs, zoneState.zones[index].mins, zoneState.zones[index].maxs );
	}
	return CG_ZoneAABBOverlapOBB( playerMins, playerMaxs, &zoneState.zones[index] );
}

static int CG_ZoneFindFirstStart( void ) {
	int i;

	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START ) {
			return i;
		}
	}
	return -1;
}

static int CG_ZoneFindStartForRoute( int routeId ) {
	int i;

	if ( routeId <= 0 ) {
		return -1;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START && zoneState.zones[i].routeId == routeId ) {
			return i;
		}
	}
	return -1;
}

static int CG_ZoneFindStartContainingPlayer( void ) {
	int i;
	int activeStart;

	activeStart = CG_ZoneFindStartForRoute( zoneState.activeRouteId );
	if ( activeStart >= 0 && CG_ZoneContainsPlayer( activeStart ) ) {
		return activeStart;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_START && CG_ZoneContainsPlayer( i ) ) {
			return i;
		}
	}
	return -1;
}

static int CG_ZoneFindNextSplitZone( int afterIndex, int routeId ) {
	int i;
	int bestIndex;
	int bestOrder;
	int afterOrder;

	afterOrder = afterIndex >= 0 && afterIndex < zoneState.count ? zoneState.zones[afterIndex].order : 0;
	bestIndex = -1;
	bestOrder = 999999;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( !zoneState.zones[i].active || zoneState.zones[i].routeId != routeId || !CG_ZoneIsRouteSplit( &zoneState.zones[i] ) ) {
			continue;
		}
		if ( zoneState.zones[i].order <= afterOrder ) {
			continue;
		}
		if ( zoneState.zones[i].order < bestOrder ) {
			bestOrder = zoneState.zones[i].order;
			bestIndex = i;
		}
	}
	return bestIndex;
}

static int CG_ZoneRouteSplitCount( int routeId ) {
	int indices[ZONE_MAX_ZONES];
	int count;

	CG_ZoneSortRouteSplitIndices( routeId, indices, &count );
	return count;
}

static int CG_ZoneSplitNumberForZone( int zoneIndex ) {
	int indices[ZONE_MAX_ZONES];
	int count;
	int i;
	int routeId;

	if ( zoneIndex < 0 || zoneIndex >= zoneState.count ) {
		return 0;
	}
	routeId = zoneState.zones[zoneIndex].routeId;
	CG_ZoneSortRouteSplitIndices( routeId, indices, &count );
	for ( i = 0; i < count; i++ ) {
		if ( indices[i] == zoneIndex ) {
			return i + 1;
		}
	}
	return 0;
}

static void CG_ZoneClearRaceVisits( void ) {
	int i;

	for ( i = 0; i < zoneState.count; i++ ) {
		zoneState.zones[i].raceVisited = qfalse;
	}
}

static int CG_ZoneRaceTotal( void ) {
	int i;
	int total;

	total = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.hasBuiltinRaceZones && !zoneState.zones[i].builtin ) {
			continue;
		}
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_RACE ) {
			total++;
		}
	}
	return total;
}

static int CG_ZoneRaceVisitedCount( void ) {
	int i;
	int points;

	points = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.hasBuiltinRaceZones && !zoneState.zones[i].builtin ) {
			continue;
		}
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_RACE && zoneState.zones[i].raceVisited ) {
			points++;
		}
	}
	return points;
}

static void CG_ZoneUpdateRaceVisits( qboolean scoringActive ) {
	int i;

	if ( !scoringActive ) {
		if ( zoneState.raceScoringWasActive ) {
			CG_ZoneClearRaceVisits();
		}
		zoneState.raceScoringWasActive = qfalse;
		return;
	}
	if ( !zoneState.raceScoringWasActive ) {
		CG_ZoneClearRaceVisits();
		zoneState.raceScoringWasActive = qtrue;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.hasBuiltinRaceZones && !zoneState.zones[i].builtin ) {
			continue;
		}
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_RACE && !zoneState.zones[i].raceVisited && CG_ZoneContainsPlayer( i ) ) {
			zoneState.zones[i].raceVisited = qtrue;
		}
	}
}

static void CG_ZoneRaceProgress( int *racePoints, int *raceTotal ) {
	int routeId;
	int start;
	int points;
	int total;

	points = CG_ZoneRaceVisitedCount();
	total = CG_ZoneRaceTotal();
	if ( total > 0 ) {
		if ( points < 0 ) points = 0;
		if ( points > total ) points = total;
		if ( racePoints ) *racePoints = points;
		if ( raceTotal ) *raceTotal = total;
		return;
	}
	points = 0;
	total = 0;

	routeId = zoneState.runningRouteId > 0 ? zoneState.runningRouteId : zoneState.activeRouteId;
	if ( routeId <= 0 && zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ) {
		routeId = zoneState.zones[zoneState.selectedZone].routeId;
	}
	if ( routeId <= 0 ) {
		start = CG_ZoneFindFirstStart();
		if ( start >= 0 ) routeId = zoneState.zones[start].routeId;
	}
	if ( routeId > 0 ) {
		total = CG_ZoneRouteSplitCount( routeId );
	}
	if ( zoneState.finished ) {
		points = total;
	} else if ( zoneState.running ) {
		if ( zoneState.nextZone >= 0 ) {
			points = CG_ZoneSplitNumberForZone( zoneState.nextZone ) - 1;
		} else {
			points = total;
		}
	}
	if ( points < 0 ) points = 0;
	if ( points > total ) points = total;
	if ( racePoints ) *racePoints = points;
	if ( raceTotal ) *raceTotal = total;
}

static void CG_ZoneProgress( char *out, int outSize, int *progressIndex, int *progressCount ) {
	int routeId;
	int current;
	int total;

	if ( out && outSize > 0 ) {
		out[0] = '\0';
	}
	routeId = zoneState.runningRouteId > 0 ? zoneState.runningRouteId : zoneState.activeRouteId;
	total = CG_ZoneRouteSplitCount( routeId );
	current = 0;
	if ( total > 0 ) {
		if ( zoneState.running && zoneState.nextZone >= 0 ) {
			current = CG_ZoneSplitNumberForZone( zoneState.nextZone );
		} else if ( zoneState.finished ) {
			current = total;
		}
		if ( current < 0 ) current = 0;
		if ( current > total ) current = total;
		if ( out && outSize > 0 ) {
			Com_sprintf( out, outSize, "%i/%i", current, total );
		}
	} else if ( out && outSize > 0 ) {
		Q_strncpyz( out, "-", outSize );
	}
	if ( progressIndex ) {
		*progressIndex = current;
	}
	if ( progressCount ) {
		*progressCount = total;
	}
}

static void CG_ZoneValidateActiveRoute( void ) {
	int i;
	int maxRoute;
	int firstStart;

	maxRoute = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].routeId > maxRoute ) {
			maxRoute = zoneState.zones[i].routeId;
		}
	}
	if ( zoneState.nextRouteId <= maxRoute ) {
		zoneState.nextRouteId = maxRoute + 1;
	}
	if ( zoneState.nextRouteId <= 0 ) {
		zoneState.nextRouteId = 1;
	}
	if ( CG_ZoneFindStartForRoute( zoneState.activeRouteId ) >= 0 ) {
		return;
	}
	firstStart = CG_ZoneFindFirstStart();
	zoneState.activeRouteId = firstStart >= 0 ? zoneState.zones[firstStart].routeId : 0;
}

static void CG_ZoneResetRun( qboolean clearMessage ) {
	zoneState.running = qfalse;
	zoneState.finished = qfalse;
	zoneState.startTime = 0;
	zoneState.lastSplitTime = 0;
	zoneState.finishTime = 0;
	zoneState.nextZone = -1;
	zoneState.startGroundedSince = 0;
	zoneState.runningRouteId = 0;
	zoneState.runningBestTotalMsec = 0;
	zoneState.activeStartZone = -1;
	zoneState.leftStartAfterBegin = qfalse;
	zoneState.startedByJump = qfalse;
	zoneState.airborneAfterBegin = qfalse;
	if ( clearMessage ) {
		zoneState.lastSplit[0] = '\0';
		zoneState.lastDelta[0] = '\0';
		zoneState.lastTotalDelta[0] = '\0';
		zoneState.lastSplitDeltaMsec = 0;
		zoneState.lastTotalDeltaMsec = 0;
		zoneState.lastSplitWasPb = qfalse;
		zoneState.lastTotalWasPb = qfalse;
		zoneState.lastSplitTimeShown = 0;
	}
	CG_ZoneUpdateCvars();
}

static void CG_ZoneStartRun( int startIndex ) {
	if ( zoneState.running || startIndex < 0 || startIndex >= zoneState.count || zoneState.zones[startIndex].type != ZONE_START ) {
		return;
	}
	zoneState.running = qtrue;
	zoneState.finished = qfalse;
	zoneState.startTime = cg.time;
	zoneState.lastSplitTime = cg.time;
	zoneState.finishTime = 0;
	zoneState.runningRouteId = zoneState.zones[startIndex].routeId;
	zoneState.runningBestTotalMsec = CG_ZoneBestTotalForRoute( zoneState.runningRouteId );
	zoneState.activeRouteId = zoneState.runningRouteId;
	zoneState.activeStartZone = startIndex;
	zoneState.leftStartAfterBegin = qfalse;
	zoneState.startedByJump = qfalse;
	zoneState.airborneAfterBegin = qfalse;
	zoneState.nextZone = CG_ZoneFindNextSplitZone( -1, zoneState.runningRouteId );
	zoneState.lastSplit[0] = '\0';
	zoneState.lastDelta[0] = '\0';
	zoneState.lastTotalDelta[0] = '\0';
	zoneState.lastSplitWasPb = qfalse;
	zoneState.lastTotalWasPb = qfalse;
	zoneState.startGroundedSince = 0;
	zoneState.lastSplitTimeShown = 0;
	CG_ZoneSetStatus( "zone timer running" );
	CG_ZoneUpdateCvars();
}

static void CG_ZoneCompleteSplit( int zoneIndex ) {
	speedrunZone_t *zone;
	int segmentMsec;
	int totalMsec;
	int deltaMsec;
	int totalDeltaMsec;
	int bestTotalMsec;
	int startIndex;
	char segmentText[32];
	char totalText[32];
	char deltaText[32];
	char totalDeltaText[32];
	qboolean hadBest;
	qboolean hadTotalBest;
	qboolean segmentPb;
	qboolean totalPb;
	qboolean recordTimes;

	if ( zoneIndex < 0 || zoneIndex >= zoneState.count ) {
		return;
	}
	zone = &zoneState.zones[zoneIndex];
	segmentMsec = cg.time - zoneState.lastSplitTime;
	totalMsec = cg.time - zoneState.startTime;
	hadBest = zone->bestSegmentMsec > 0;
	recordTimes = cg_zoneTimer.integer ? qtrue : qfalse;
	deltaMsec = hadBest ? segmentMsec - zone->bestSegmentMsec : 0;
	segmentPb = !hadBest || segmentMsec < zone->bestSegmentMsec;
	bestTotalMsec = zone->bestTotalMsec;
	if ( zone->type == ZONE_FINISH && zoneState.runningBestTotalMsec > 0 && ( !bestTotalMsec || zoneState.runningBestTotalMsec < bestTotalMsec ) ) {
		bestTotalMsec = zoneState.runningBestTotalMsec;
	}
	hadTotalBest = bestTotalMsec > 0;
	totalDeltaMsec = hadTotalBest ? totalMsec - bestTotalMsec : 0;
	totalPb = !hadTotalBest || totalMsec < bestTotalMsec;
	if ( recordTimes && ( !hadBest || segmentMsec < zone->bestSegmentMsec ) ) {
		zone->bestSegmentMsec = segmentMsec;
	}
	if ( recordTimes && ( !hadTotalBest || totalMsec < zone->bestTotalMsec || !zone->bestTotalMsec ) ) {
		zone->bestTotalMsec = totalMsec;
	}

	CG_ZoneFormatTime( segmentMsec, segmentText, sizeof( segmentText ) );
	CG_ZoneFormatTime( totalMsec, totalText, sizeof( totalText ) );
	if ( hadBest ) {
		CG_ZoneFormatDelta( deltaMsec, deltaText, sizeof( deltaText ) );
	} else {
		Q_strncpyz( deltaText, "new", sizeof( deltaText ) );
	}
	if ( hadTotalBest ) {
		CG_ZoneFormatDelta( totalDeltaMsec, totalDeltaText, sizeof( totalDeltaText ) );
	} else {
		Q_strncpyz( totalDeltaText, "new", sizeof( totalDeltaText ) );
	}

	if ( zone->type == ZONE_FINISH ) {
		startIndex = zoneState.activeStartZone;
		if ( recordTimes && startIndex >= 0 && startIndex < zoneState.count && zoneState.zones[startIndex].type == ZONE_START && ( !zoneState.zones[startIndex].bestTotalMsec || totalMsec < zoneState.zones[startIndex].bestTotalMsec ) ) {
			zoneState.zones[startIndex].bestTotalMsec = totalMsec;
		}
		Com_sprintf( zoneState.lastSplit, sizeof( zoneState.lastSplit ), "Finish  %s  total %s", deltaText, totalDeltaText );
		zoneState.running = qfalse;
		zoneState.finished = qtrue;
		zoneState.finishTime = cg.time;
		zoneState.nextZone = -1;
		CG_ZoneSetStatus( zoneState.lastSplit );
	} else {
		Com_sprintf( zoneState.lastSplit, sizeof( zoneState.lastSplit ), "%s  %s  total %s", zone->name, deltaText, totalDeltaText );
		zoneState.lastSplitTime = cg.time;
		zoneState.nextZone = CG_ZoneFindNextSplitZone( zoneIndex, zoneState.runningRouteId );
		CG_ZoneSetStatus( zoneState.lastSplit );
	}
	Q_strncpyz( zoneState.lastDelta, deltaText, sizeof( zoneState.lastDelta ) );
	Q_strncpyz( zoneState.lastTotalDelta, totalDeltaText, sizeof( zoneState.lastTotalDelta ) );
	zoneState.lastSplitDeltaMsec = hadBest ? deltaMsec : 0;
	zoneState.lastTotalDeltaMsec = hadTotalBest ? totalDeltaMsec : 0;
	zoneState.lastSplitWasPb = segmentPb;
	zoneState.lastTotalWasPb = totalPb;
	zoneState.lastSplitTimeShown = cg.time;
	CG_ZoneUpdateCvars();
}

static qboolean CG_ZoneCurrentCommand( usercmd_t *cmd ) {
	int cmdNum;

	if ( !cmd ) {
		return qfalse;
	}
	cmdNum = trap_GetCurrentCmdNumber();
	return trap_GetUserCmd( cmdNum, cmd );
}

static qboolean CG_ZoneRaceScoringActive( void ) {
	char active[8];
	char scoring[8];

	trap_Cvar_VariableStringBuffer( "ls_race_active", active, sizeof( active ) );
	if ( !atoi( active ) ) return qfalse;
	trap_Cvar_VariableStringBuffer( "ls_race_zone_scoring", scoring, sizeof( scoring ) );
	return atoi( scoring ) ? qtrue : qfalse;
}

static void CG_ZonePickPoint( vec3_t out, qboolean usePlayerOrigin ) {
	trace_t trace;
	vec3_t end;

	if ( usePlayerOrigin || !cg.snap ) {
		VectorCopy( cg.predictedPlayerState.origin, out );
		return;
	}
	VectorMA( cg.refdef.vieworg, 4096.0f, cg.refdef.viewaxis[0], end );
	CG_Trace( &trace, cg.refdef.vieworg, NULL, NULL, end, cg.snap->ps.clientNum, MASK_SOLID );
	if ( trace.fraction < 1.0f ) {
		VectorCopy( trace.endpos, out );
	} else {
		VectorMA( cg.refdef.vieworg, 192.0f, cg.refdef.viewaxis[0], out );
	}
}

static int CG_ZoneInsideAnyEditableZone( void ) {
	int i;

	for ( i = 0; i < zoneState.count; i++ ) {
		if ( CG_ZoneContainsPlayer( i ) ) {
			return i;
		}
	}
	return -1;
}

static void CG_ZoneUpdatePlayerRoutePreview( void ) {
	int insideZone;

	if ( !cg_zoneEdit.integer || zoneState.count <= 0 || !cg.snap || zoneState.dragging ) {
		return;
	}
	insideZone = CG_ZoneInsideAnyEditableZone();
	if ( insideZone < 0 ) {
		return;
	}
	if ( zoneState.selectedZone == insideZone && zoneState.activeRouteId == zoneState.zones[insideZone].routeId ) {
		return;
	}
	zoneState.selectedZone = insideZone;
	if ( zoneState.zones[insideZone].routeId > 0 ) {
		zoneState.activeRouteId = zoneState.zones[insideZone].routeId;
	}
	CG_ZoneUpdateCvars();
}

static void CG_ZoneUpdateHover( void ) {
	float bestDistSq;
	float hoverPixels;
	int bestZone;
	int bestCorner;
	int i;
	int handle;

	zoneState.hoveredZone = -1;
	zoneState.hoveredCorner = -1;
	if ( !cg_zoneEdit.integer || zoneState.count <= 0 ) {
		return;
	}

	hoverPixels = cg_zoneHoverPixels.value;
	if ( hoverPixels < 4.0f ) hoverPixels = 4.0f;
	if ( hoverPixels > 96.0f ) hoverPixels = 96.0f;
	bestDistSq = hoverPixels * hoverPixels;
	bestZone = -1;
	bestCorner = -1;

	for ( i = 0; i < zoneState.count; i++ ) {
		if ( !CG_ZoneHandlesVisible( i ) ) {
			continue;
		}
		for ( handle = 0; handle <= ZONE_HANDLE_LAST; handle++ ) {
			vec3_t point;
			float sx;
			float sy;
			float dx;
			float dy;
			float distSq;

			if ( !CG_ZoneHandleAvailable( i, handle ) ) {
				continue;
			}
			CG_ZoneGetHandlePoint( &zoneState.zones[i], handle, point );
			if ( !TrigVis_WorldToScreen( point, &sx, &sy ) ) {
				continue;
			}
			dx = sx - 320.0f;
			dy = sy - 240.0f;
			distSq = dx * dx + dy * dy;
			if ( distSq < bestDistSq ) {
				bestDistSq = distSq;
				bestZone = i;
				bestCorner = handle;
			}
		}
	}

	zoneState.hoveredZone = bestZone;
	zoneState.hoveredCorner = bestCorner;
	if ( bestZone >= 0 ) {
		zoneState.selectedZone = bestZone;
		if ( zoneState.zones[bestZone].routeId > 0 ) {
			zoneState.activeRouteId = zoneState.zones[bestZone].routeId;
		}
	}
}

static void CG_ZoneBeginDrag( int zoneIndex, int handle ) {
	vec3_t point;
	vec3_t delta;
	float distance;

	if ( zoneIndex < 0 || zoneIndex >= zoneState.count || handle < 0 || handle > ZONE_HANDLE_LAST ) {
		return;
	}
	if ( zoneState.zones[zoneIndex].builtin ) {
		CG_ZoneSetStatus( "built-in race zones are locked" );
		return;
	}
	if ( !CG_ZoneHandleAvailable( zoneIndex, handle ) ) {
		return;
	}
	CG_ZoneGetHandlePoint( &zoneState.zones[zoneIndex], handle, point );
	VectorSubtract( point, cg.refdef.vieworg, delta );
	distance = DotProduct( delta, cg.refdef.viewaxis[0] );
	if ( distance < ZONE_DRAG_MIN_DIST ) {
		distance = VectorLength( delta );
	}
	zoneState.dragging = qtrue;
	zoneState.dragZone = zoneIndex;
	zoneState.dragCorner = handle;
	zoneState.dragDistance = CG_ZoneClampFloat( distance, ZONE_DRAG_MIN_DIST, ZONE_DRAG_MAX_DIST );
	zoneState.selectedZone = zoneIndex;
	if ( CG_ZoneHandleIsRotation( handle ) ) {
		VectorCopy( zoneState.zones[zoneIndex].angles, zoneState.dragStartAngles );
		zoneState.dragStartAngle = CG_ZoneRotationAngleForPoint( &zoneState.zones[zoneIndex], zoneState.dragStartAngles, handle, point );
	}
}

static void CG_ZoneApplyDrag( const usercmd_t *cmd ) {
	vec3_t point;
	float speed;
	float step;
	speedrunZone_t *zone;

	if ( !cmd || !zoneState.dragging || zoneState.dragZone < 0 || zoneState.dragZone >= zoneState.count ) {
		return;
	}

	speed = cg_zoneDragSpeed.value;
	if ( speed < 8.0f ) speed = 8.0f;
	if ( speed > 1024.0f ) speed = 1024.0f;
	step = 0.0f;
	if ( cmd->buttons & BUTTON_ATTACK ) {
		step += speed * ( cg.frametime > 0 ? cg.frametime : 16 ) * 0.001f;
	}
	if ( cmd->wbuttons & WBUTTON_ATTACK2 ) {
		step -= speed * ( cg.frametime > 0 ? cg.frametime : 16 ) * 0.001f;
	}
	zoneState.dragDistance = CG_ZoneClampFloat( zoneState.dragDistance + step, ZONE_DRAG_MIN_DIST, ZONE_DRAG_MAX_DIST );

	zone = &zoneState.zones[zoneState.dragZone];
	if ( zone->builtin ) {
		zoneState.dragging = qfalse;
		return;
	}
	VectorMA( cg.refdef.vieworg, zoneState.dragDistance, cg.refdef.viewaxis[0], point );
	if ( CG_ZoneHandleIsRotation( zoneState.dragCorner ) ) {
		CG_ZoneApplyRotationDrag( zone, zoneState.dragCorner, point );
		CG_ZoneNormalizeAngles( zone );
	} else {
		CG_ZoneSetHandlePoint( zone, zoneState.dragCorner, point );
	}
	zoneState.selectedZone = zoneState.dragZone;
	CG_ZoneUpdateCvars();
}

static void CG_ZoneEditorFrame( const usercmd_t *cmd ) {
	qboolean useHeld;
	int insideZone;

	if ( !cg_zoneEdit.integer ) {
		zoneState.dragging = qfalse;
		zoneState.dragZone = -1;
		zoneState.dragCorner = -1;
		zoneState.dragDistance = 0.0f;
		return;
	}

	CG_ZoneUpdateHover();
	insideZone = CG_ZoneInsideAnyEditableZone();
	if ( insideZone >= 0 && zoneState.hoveredZone < 0 && !zoneState.dragging ) {
		zoneState.selectedZone = insideZone;
		if ( zoneState.zones[insideZone].routeId > 0 ) {
			zoneState.activeRouteId = zoneState.zones[insideZone].routeId;
		}
	}

	useHeld = cmd && ( cmd->buttons & BUTTON_ACTIVATE ) ? qtrue : qfalse;
	if ( useHeld ) {
		if ( !zoneState.dragging && zoneState.hoveredZone >= 0 && zoneState.hoveredCorner >= 0 ) {
			CG_ZoneBeginDrag( zoneState.hoveredZone, zoneState.hoveredCorner );
		}
		CG_ZoneApplyDrag( cmd );
	} else {
		zoneState.dragging = qfalse;
		zoneState.dragZone = -1;
		zoneState.dragCorner = -1;
		zoneState.dragDistance = 0.0f;
	}
	CG_ZoneUpdateCvars();
}

static void CG_ZoneRuntimeFrame( const usercmd_t *cmd ) {
	int startZone;
	int currentStartZone;
	qboolean raceScoringActive;
	qboolean inStart;
	qboolean jumpPressed;
	qboolean onGround;

	raceScoringActive = CG_ZoneRaceScoringActive();
	if ( !raceScoringActive ) {
		CG_ZoneUpdateRaceVisits( qfalse );
	}
	if ( ( !cg_zoneTimer.integer && !raceScoringActive ) || zoneState.count <= 0 || !cg.snap ) {
		return;
	}
	CG_ZoneUpdateRaceVisits( raceScoringActive );

	currentStartZone = CG_ZoneFindStartContainingPlayer();
	startZone = currentStartZone >= 0 ? currentStartZone : CG_ZoneFindStartForRoute( zoneState.activeRouteId );
	if ( startZone < 0 ) {
		startZone = CG_ZoneFindFirstStart();
	}
	inStart = currentStartZone >= 0;
	jumpPressed = cmd && ( cmd->wbuttons & WBUTTON_JUMP ) ? qtrue : qfalse;
	onGround = cg.predictedPlayerState.groundEntityNum != ENTITYNUM_NONE ? qtrue : qfalse;

	if ( !zoneState.running ) {
		if ( inStart ) {
			if ( zoneState.finished ) {
				CG_ZoneResetRun( qtrue );
			}
			zoneState.activeRouteId = zoneState.zones[currentStartZone].routeId;
			if ( jumpPressed && !zoneState.wasJumpPressed ) {
				CG_ZoneStartRun( currentStartZone );
				zoneState.startedByJump = qtrue;
			}
		} else if ( zoneState.wasInStartZone >= 0 ) {
			CG_ZoneStartRun( zoneState.wasInStartZone );
		}
	} else {
		qboolean resetInStart;
		int stopDelay;

		resetInStart = qfalse;
		stopDelay = cg_zoneStartStopMs.integer;
		if ( stopDelay < 0 ) stopDelay = 0;
		if ( stopDelay > 1500 ) stopDelay = 1500;
		if ( zoneState.activeStartZone >= 0 && zoneState.activeStartZone < zoneState.count ) {
			qboolean inActiveStart;

			inActiveStart = CG_ZoneContainsPlayer( zoneState.activeStartZone );
			if ( !onGround ) {
				zoneState.airborneAfterBegin = qtrue;
			}
			if ( !inActiveStart ) {
				zoneState.leftStartAfterBegin = qtrue;
				zoneState.startGroundedSince = 0;
			} else if ( zoneState.leftStartAfterBegin || zoneState.startedByJump || zoneState.airborneAfterBegin ) {
				if ( onGround && !jumpPressed ) {
					if ( !zoneState.startGroundedSince ) {
						zoneState.startGroundedSince = cg.time;
					}
					if ( cg.time - zoneState.startGroundedSince >= stopDelay ) {
						CG_ZoneResetRun( qtrue );
						CG_ZoneSetStatus( "zone timer stopped in start" );
						resetInStart = qtrue;
					}
				} else {
					zoneState.startGroundedSince = 0;
				}
			}
		}
		if ( !resetInStart && zoneState.nextZone >= 0 && CG_ZoneContainsPlayer( zoneState.nextZone ) ) {
			CG_ZoneCompleteSplit( zoneState.nextZone );
		}
	}

	zoneState.wasInStartZone = inStart ? currentStartZone : -1;
	zoneState.wasJumpPressed = jumpPressed;
	CG_ZoneUpdateCvars();
}

void CG_ZoneFrame( void ) {
	usercmd_t cmd;
	qboolean hasCmd;

	if ( !cg.snap ) {
		return;
	}
	hasCmd = CG_ZoneCurrentCommand( &cmd );
	CG_ZoneUpdatePlayerRoutePreview();
	CG_ZoneEditorFrame( hasCmd ? &cmd : NULL );
	CG_ZoneRuntimeFrame( hasCmd ? &cmd : NULL );
}

static void CG_ZoneParseBaseColor( vmCvar_t *cv, int *cachedModCount, byte cachedColor[3], const byte fallback[3], byte color[3] ) {
	int r;
	int g;
	int b;
	float a;

	if ( !cv ) {
		color[0] = fallback[0];
		color[1] = fallback[1];
		color[2] = fallback[2];
		return;
	}

	if ( *cachedModCount != cv->modificationCount ) {
		*cachedModCount = cv->modificationCount;
		if ( sscanf( cv->string, "%i %i %i %f", &r, &g, &b, &a ) >= 3 ) {
			cachedColor[0] = (byte)CG_ZoneClampInt( r, 0, 255 );
			cachedColor[1] = (byte)CG_ZoneClampInt( g, 0, 255 );
			cachedColor[2] = (byte)CG_ZoneClampInt( b, 0, 255 );
		} else {
			cachedColor[0] = fallback[0];
			cachedColor[1] = fallback[1];
			cachedColor[2] = fallback[2];
		}
	}
	color[0] = cachedColor[0];
	color[1] = cachedColor[1];
	color[2] = cachedColor[2];
}

static void CG_ZoneBaseColorForZone( const speedrunZone_t *zone, byte color[3] ) {
	static int cachedStartModCount = -1;
	static int cachedCheckpointModCount = -1;
	static int cachedFinishModCount = -1;
	static int cachedRaceModCount = -1;
	static byte cachedStartColor[3] = { 82, 255, 112 };
	static byte cachedCheckpointColor[3] = { 82, 184, 255 };
	static byte cachedFinishColor[3] = { 255, 108, 86 };
	static byte cachedRaceColor[3] = { 255, 210, 64 };
	static const byte startFallback[3] = { 82, 255, 112 };
	static const byte checkpointFallback[3] = { 82, 184, 255 };
	static const byte finishFallback[3] = { 255, 108, 86 };
	static const byte raceFallback[3] = { 255, 210, 64 };

	if ( zone && zone->type == ZONE_START ) {
		CG_ZoneParseBaseColor( &cg_zoneStartColor, &cachedStartModCount, cachedStartColor, startFallback, color );
		return;
	}
	if ( zone && zone->type == ZONE_FINISH ) {
		CG_ZoneParseBaseColor( &cg_zoneFinishColor, &cachedFinishModCount, cachedFinishColor, finishFallback, color );
		return;
	}
	if ( zone && zone->type == ZONE_RACE ) {
		CG_ZoneParseBaseColor( &cg_zoneRaceColor, &cachedRaceModCount, cachedRaceColor, raceFallback, color );
		return;
	}
	CG_ZoneParseBaseColor( &cg_zoneColor, &cachedCheckpointModCount, cachedCheckpointColor, checkpointFallback, color );
}

static byte CG_ZoneLightenByte( byte value, int amount ) {
	int out;

	out = value + amount;
	return (byte)CG_ZoneClampInt( out, 0, 255 );
}

static void CG_ZoneColor( const speedrunZone_t *zone, byte fill[4], byte border[4], qboolean selected, qboolean hovered, qboolean nextSplit ) {
	byte base[3];
	int alpha;
	int borderAlpha;
	qboolean dimmed;

	alpha = cg_zoneOpacity.integer;
	if ( alpha < 5 ) alpha = 5;
	if ( alpha > 255 ) alpha = 255;
	borderAlpha = cg_zoneBorderAlpha.integer;
	if ( borderAlpha < 20 ) borderAlpha = 20;
	if ( borderAlpha > 255 ) borderAlpha = 255;
	CG_ZoneBaseColorForZone( zone, base );
	fill[0] = base[0];
	fill[1] = base[1];
	fill[2] = base[2];
	dimmed = CG_ZoneIsDimmedRoute( zone );
	if ( dimmed && !selected && !hovered ) {
		int inactiveAlpha = cg_zoneInactiveAlpha.integer;
		if ( inactiveAlpha < 0 ) inactiveAlpha = 0;
		if ( inactiveAlpha > 255 ) inactiveAlpha = 255;
		fill[0] = (byte)( fill[0] * 0.45f );
		fill[1] = (byte)( fill[1] * 0.45f );
		fill[2] = (byte)( fill[2] * 0.45f );
		alpha = inactiveAlpha;
		borderAlpha = inactiveAlpha + 35 > 255 ? 255 : inactiveAlpha + 35;
	}
	if ( nextSplit ) {
		alpha = alpha + 35 > 255 ? 255 : alpha + 35;
		borderAlpha = 255;
	}
	fill[3] = alpha;
	border[0] = CG_ZoneLightenByte( fill[0], selected || hovered || nextSplit ? 110 : 48 );
	border[1] = CG_ZoneLightenByte( fill[1], selected || hovered || nextSplit ? 110 : 48 );
	border[2] = CG_ZoneLightenByte( fill[2], selected || hovered || nextSplit ? 110 : 48 );
	border[3] = selected || hovered || nextSplit ? 255 : borderAlpha;
}

static void CG_ZoneEnsureShaders( void ) {
	if ( zoneState.fillShader && zoneState.borderShader ) {
		return;
	}
	zoneState.fillShader = trap_R_RegisterShader( "triggerVisAlpha" );
	if ( !zoneState.fillShader ) {
		zoneState.fillShader = cgs.media.whiteShader;
	}
	zoneState.borderShader = trap_R_RegisterShader( "triggerVisBorderGlow" );
	if ( !zoneState.borderShader ) {
		zoneState.borderShader = zoneState.fillShader;
	}
}

void CG_DrawZones( void ) {
	int i;

	if ( !CG_ZoneCheatsEnabled() || zoneState.count <= 0 || ( !cg_zoneDraw.integer && !cg_zoneEdit.integer ) ) {
		return;
	}
	CG_ZoneEnsureShaders();
	for ( i = 0; i < zoneState.count; i++ ) {
		byte fill[4];
		byte border[4];
		float borderWidth;
		qboolean selected;
		qboolean hovered;
		qboolean nextSplit;
		if ( !CG_ZoneShouldDrawZone( i ) ) {
			continue;
		}
		if ( !CG_ZoneBoundsVisible( &zoneState.zones[i] ) ) {
			continue;
		}

		selected = cg_zoneEdit.integer && i == zoneState.selectedZone ? qtrue : qfalse;
		hovered = cg_zoneEdit.integer && i == zoneState.hoveredZone ? qtrue : qfalse;
		nextSplit = ( zoneState.running && i == zoneState.nextZone ) || ( CG_ZoneUseFocusedRuntimeDraw() && i == CG_ZoneFocusedPrimarySplit() ) ? qtrue : qfalse;
		borderWidth = CG_ZoneClampFloat( cg_zoneBorderWidth.value, 0.25f, 6.0f );
		CG_ZoneColor( &zoneState.zones[i], fill, border, selected, hovered, nextSplit );
		{
			vec3_t corners[8];
			CG_ZoneGetCorners( &zoneState.zones[i], corners );
			TrigVis_DrawOrientedBoxFixedBorder( corners, fill, border, borderWidth, zoneState.fillShader, zoneState.borderShader );
		}

		if ( CG_ZoneHandlesVisible( i ) ) {
			int handle;
			float handleSize;
			byte base[3];

			handleSize = cg_zoneHandleSize.value;
			if ( handleSize < 2.0f ) handleSize = 2.0f;
			if ( handleSize > 24.0f ) handleSize = 24.0f;
			CG_ZoneBaseColorForZone( &zoneState.zones[i], base );
			for ( handle = 0; handle <= ZONE_HANDLE_LAST; handle++ ) {
				vec3_t point;
				vec3_t mins;
				vec3_t maxs;
				byte hFill[4];
				byte hBorder[4];
				int axis;
				float size;

				if ( !CG_ZoneHandleAvailable( i, handle ) ) {
					continue;
				}
				CG_ZoneGetHandlePoint( &zoneState.zones[i], handle, point );
				size = handle == ZONE_HANDLE_CENTER ? handleSize * 1.15f : ( CG_ZoneHandleIsRotation( handle ) ? handleSize * 1.35f : handleSize );
				for ( axis = 0; axis < 3; axis++ ) {
					mins[axis] = point[axis] - size;
					maxs[axis] = point[axis] + size;
				}
				if ( CG_ZoneHandleIsRotation( handle ) ) {
					int angleAxis = CG_ZoneHandleRotationAxis( handle );
					if ( angleAxis == PITCH ) {
						hFill[0] = 255; hFill[1] = 88; hFill[2] = 78; hFill[3] = 220;
					} else if ( angleAxis == YAW ) {
						hFill[0] = 90; hFill[1] = 160; hFill[2] = 255; hFill[3] = 220;
					} else {
						hFill[0] = 92; hFill[1] = 235; hFill[2] = 126; hFill[3] = 220;
					}
				} else if ( handle == ZONE_HANDLE_CENTER ) {
					hFill[0] = CG_ZoneLightenByte( base[0], 88 ); hFill[1] = CG_ZoneLightenByte( base[1], 88 ); hFill[2] = CG_ZoneLightenByte( base[2], 88 ); hFill[3] = 210;
				} else if ( CG_ZoneHandleIsFace( handle ) ) {
					hFill[0] = CG_ZoneLightenByte( base[0], 44 ); hFill[1] = CG_ZoneLightenByte( base[1], 44 ); hFill[2] = CG_ZoneLightenByte( base[2], 44 ); hFill[3] = 205;
				} else {
					hFill[0] = base[0]; hFill[1] = base[1]; hFill[2] = base[2]; hFill[3] = 200;
				}
				if ( i == zoneState.hoveredZone && handle == zoneState.hoveredCorner ) {
					hFill[0] = 255; hFill[1] = 255; hFill[2] = 255; hFill[3] = 235;
				}
				hBorder[0] = CG_ZoneLightenByte( base[0], 96 ); hBorder[1] = CG_ZoneLightenByte( base[1], 96 ); hBorder[2] = CG_ZoneLightenByte( base[2], 96 ); hBorder[3] = 190;
				TrigVis_DrawBoxFixedBorder( mins, maxs, hFill, hBorder, 0.5f, zoneState.fillShader, zoneState.borderShader );
			}
		}
	}
}

static void CG_DrawZoneHud( void ) {
	vec4_t bg;
	vec4_t border;
	vec4_t textColor;
	vec4_t mutedColor;
	vec4_t deltaColor;
	vec4_t totalDeltaColor;
	char text[128];
	char recordText[64];
	char progressText[32];
	char label[64];
	float x;
	float y;
	float scale;
	float w;
	float h;
	float timerX;
	float deltaX;
	int charW;
	int charH;
	int bigW;
	int bigH;
	int textLen;
	qboolean showDelta;
	qboolean showProgress;
	int recordMsec;
	int progressIndex;
	int progressCount;

	return;

	x = CG_ZoneClampFloat( cg_zoneHudX.value, 0.0f, 620.0f );
	y = CG_ZoneClampFloat( cg_zoneHudY.value, 0.0f, 460.0f );
	scale = CG_ZoneClampFloat( cg_zoneHudScale.value, 0.55f, 2.5f );
	showDelta = ( zoneState.lastDelta[0] || zoneState.lastTotalDelta[0] ) && cg.time - zoneState.lastSplitTimeShown < 4500 ? qtrue : qfalse;
	CG_ZoneProgress( progressText, sizeof( progressText ), &progressIndex, &progressCount );
	showProgress = cg_zoneHudProgress.integer && progressCount > 0 ? qtrue : qfalse;
	w = 198.0f * scale;
	h = showDelta ? 66.0f * scale : 46.0f * scale;
	charW = CG_ZoneClampInt( (int)( 6.0f * scale ), 4, 18 );
	charH = CG_ZoneClampInt( (int)( 9.0f * scale ), 7, 28 );
	bigW = CG_ZoneClampInt( (int)( 11.0f * scale ), 8, 28 );
	bigH = CG_ZoneClampInt( (int)( 17.0f * scale ), 12, 40 );

	bg[0] = 0.0f; bg[1] = 0.0f; bg[2] = 0.0f; bg[3] = CG_ZoneClampFloat( cg_zoneHudAlpha.value, 0.0f, 1.0f );
	border[0] = 0.92f; border[1] = 0.96f; border[2] = 0.90f; border[3] = 0.22f;
	textColor[0] = 0.92f; textColor[1] = 0.98f; textColor[2] = 0.90f; textColor[3] = 0.98f;
	mutedColor[0] = 0.58f; mutedColor[1] = 0.66f; mutedColor[2] = 0.54f; mutedColor[3] = 0.88f;
	deltaColor[0] = 0.92f; deltaColor[1] = 0.74f; deltaColor[2] = 0.24f; deltaColor[3] = 0.98f;
	totalDeltaColor[0] = 0.92f; totalDeltaColor[1] = 0.74f; totalDeltaColor[2] = 0.24f; totalDeltaColor[3] = 0.98f;

	if ( zoneState.running ) {
		CG_ZoneFormatTime( cg.time - zoneState.startTime, text, sizeof( text ) );
	} else if ( zoneState.finished ) {
		CG_ZoneFormatTime( zoneState.finishTime - zoneState.startTime, text, sizeof( text ) );
	} else {
		Q_strncpyz( text, "0.000", sizeof( text ) );
	}
	recordMsec = zoneState.running && zoneState.runningBestTotalMsec > 0 ? zoneState.runningBestTotalMsec : CG_ZoneBestTotalForRoute( zoneState.activeRouteId );
	if ( recordMsec > 0 ) {
		CG_ZoneFormatTime( recordMsec, recordText, sizeof( recordText ) );
	} else {
		Q_strncpyz( recordText, "-", sizeof( recordText ) );
	}

	CG_FillRect( x, y, w, h, bg, ALIGN_STRETCH );
	CG_DrawRect( x, y, w, h, 1.0f, border, ALIGN_STRETCH );
	textLen = strlen( text );
	timerX = x + w - 8.0f * scale - textLen * bigW;
	if ( timerX < x + 8.0f * scale ) {
		timerX = x + 8.0f * scale;
	}
	CG_DrawStringExt( (int)timerX, (int)( y + 5.0f * scale ), text, textColor, qtrue, qtrue, bigW, bigH, 0, ALIGN_STRETCH );
	Com_sprintf( label, sizeof( label ), "PB %s", recordText );
	CG_DrawStringExt( (int)( x + 8.0f * scale ), (int)( y + 27.0f * scale ), label, mutedColor, qtrue, qtrue, charW, charH, 0, ALIGN_STRETCH );
	if ( showProgress ) {
		Com_sprintf( label, sizeof( label ), "CP %s", progressText );
		textLen = strlen( label );
		deltaX = x + w - 8.0f * scale - textLen * charW;
		if ( deltaX < x + 8.0f * scale ) {
			deltaX = x + 8.0f * scale;
		}
		CG_DrawStringExt( (int)deltaX, (int)( y + 27.0f * scale ), label, mutedColor, qtrue, qtrue, charW, charH, 0, ALIGN_STRETCH );
	}
	if ( showDelta ) {
		if ( zoneState.lastSplitWasPb ) {
			deltaColor[0] = 1.0f; deltaColor[1] = 0.86f; deltaColor[2] = 0.18f;
		} else if ( zoneState.lastDelta[0] == '-' ) {
			deltaColor[0] = 0.42f; deltaColor[1] = 1.0f; deltaColor[2] = 0.42f;
		} else if ( zoneState.lastDelta[0] == '+' ) {
			deltaColor[0] = 1.0f; deltaColor[1] = 0.36f; deltaColor[2] = 0.28f;
		}
		if ( zoneState.lastTotalWasPb ) {
			totalDeltaColor[0] = 1.0f; totalDeltaColor[1] = 0.86f; totalDeltaColor[2] = 0.18f;
		} else if ( zoneState.lastTotalDelta[0] == '-' ) {
			totalDeltaColor[0] = 0.42f; totalDeltaColor[1] = 1.0f; totalDeltaColor[2] = 0.42f;
		} else if ( zoneState.lastTotalDelta[0] == '+' ) {
			totalDeltaColor[0] = 1.0f; totalDeltaColor[1] = 0.36f; totalDeltaColor[2] = 0.28f;
		}
		Com_sprintf( label, sizeof( label ), "SEG %s", zoneState.lastDelta[0] ? zoneState.lastDelta : "-" );
		textLen = strlen( label );
		deltaX = x + w - 8.0f * scale - textLen * charW;
		if ( deltaX < x + 8.0f * scale ) {
			deltaX = x + 8.0f * scale;
		}
		CG_DrawStringExt( (int)deltaX, (int)( y + 41.0f * scale ), label, deltaColor, qtrue, qtrue, charW, charH, 0, ALIGN_STRETCH );
		Com_sprintf( label, sizeof( label ), "FULL %s", zoneState.lastTotalDelta[0] ? zoneState.lastTotalDelta : "-" );
		textLen = strlen( label );
		deltaX = x + w - 8.0f * scale - textLen * charW;
		if ( deltaX < x + 8.0f * scale ) {
			deltaX = x + 8.0f * scale;
		}
		CG_DrawStringExt( (int)deltaX, (int)( y + 53.0f * scale ), label, totalDeltaColor, qtrue, qtrue, charW, charH, 0, ALIGN_STRETCH );
	}
}

static qboolean CG_ZoneLabelVisibleFromView( const vec3_t point ) {
	trace_t trace;

	if ( !cg.snap ) {
		return qtrue;
	}
	CG_Trace( &trace, cg.refdef.vieworg, NULL, NULL, point, cg.snap->ps.clientNum, MASK_SOLID );
	return trace.fraction >= 0.98f ? qtrue : qfalse;
}

void CG_DrawZoneLabels( void ) {
	int i;
	vec4_t color;

	if ( !CG_ZoneCheatsEnabled() || zoneState.count <= 0 || ( !cg_zoneDraw.integer && !cg_zoneEdit.integer ) || !cg_zoneDrawLabels.integer ) {
		return;
	}
	for ( i = 0; i < zoneState.count; i++ ) {
		vec3_t mid;
		vec3_t axes[3];
		vec3_t half;
		vec3_t delta;
		float sx;
		float sy;
		float dist;
		char label[96];
		int len;
		if ( !CG_ZoneShouldDrawZone( i ) ) {
			continue;
		}
		if ( !CG_ZoneBoundsVisible( &zoneState.zones[i] ) ) {
			continue;
		}

		CG_ZoneGetCenter( &zoneState.zones[i], mid );
		CG_ZoneBuildAxes( &zoneState.zones[i], axes );
		CG_ZoneGetHalfExtents( &zoneState.zones[i], half );
		VectorMA( mid, half[2] + 10.0f, axes[2], mid );
		VectorSubtract( mid, cg.refdef.vieworg, delta );
		dist = VectorLength( delta );
		if ( dist > ZONE_LABEL_MAX_DIST || !CG_ZoneLabelVisibleFromView( mid ) || !TrigVis_WorldToScreen( mid, &sx, &sy ) ) {
			continue;
		}
		if ( i == zoneState.selectedZone ) {
			Com_sprintf( label, sizeof( label ), "[%i] r%i %s %s", i, zoneState.zones[i].routeId, CG_ZoneTypeName( zoneState.zones[i].type ), zoneState.zones[i].name );
		} else {
			Com_sprintf( label, sizeof( label ), "%i r%i %s", i, zoneState.zones[i].routeId, zoneState.zones[i].name );
		}
		if ( zoneState.zones[i].type == ZONE_RACE ) {
			color[0] = 1.0f;
			color[1] = 0.88f;
			color[2] = 0.30f;
		} else {
			color[0] = zoneState.zones[i].type == ZONE_FINISH ? 1.0f : ( zoneState.zones[i].type == ZONE_START ? 0.45f : 0.42f );
			color[1] = zoneState.zones[i].type == ZONE_FINISH ? 0.78f : ( zoneState.zones[i].type == ZONE_START ? 0.95f : 0.74f );
			color[2] = zoneState.zones[i].type == ZONE_FINISH ? 0.22f : ( zoneState.zones[i].type == ZONE_START ? 0.36f : 1.0f );
		}
		color[3] = 0.90f;
		len = strlen( label );
		CG_DrawStringExt( (int)( sx - ( len * ZONE_LABEL_CHAR_W ) * 0.5f ), (int)sy, label, color, qtrue, qtrue, ZONE_LABEL_CHAR_W, ZONE_LABEL_CHAR_H, 0, ALIGN_STRETCH );
	}
}

static void CG_ZoneAppendBuiltinRaceZones( void ) {
	char mapBase[MAX_QPATH];
	int i;
	int added;

	CG_ZoneMapBaseName( mapBase, sizeof( mapBase ) );
	added = 0;
	for ( i = 0; cg_builtinRaceZones[i].mapName; i++ ) {
		speedrunZone_t *zone;
		const builtinRaceZone_t *builtin;

		builtin = &cg_builtinRaceZones[i];
		if ( Q_stricmp( builtin->mapName, mapBase ) ) {
			continue;
		}
		if ( zoneState.count >= ZONE_MAX_ZONES ) {
			break;
		}
		zone = &zoneState.zones[zoneState.count];
		memset( zone, 0, sizeof( *zone ) );
		zone->active = qtrue;
		zone->builtin = qtrue;
		zone->type = ZONE_RACE;
		zone->routeId = 0;
		zone->order = builtin->order > 0 ? builtin->order : CG_ZoneNextRaceOrder();
		Q_strncpyz( zone->name, builtin->name && builtin->name[0] ? builtin->name : va( "race_%02i", zone->order ), sizeof( zone->name ) );
		CG_ZoneSanitizeName( zone->name );
		VectorCopy( builtin->mins, zone->mins );
		VectorCopy( builtin->maxs, zone->maxs );
		VectorCopy( builtin->angles, zone->angles );
		CG_ZoneNormalizeAngles( zone );
		CG_ZoneNormalizeBounds( zone );
		zoneState.count++;
		added++;
	}
	zoneState.hasBuiltinRaceZones = added > 0 ? qtrue : qfalse;
}

void CG_ZoneExportBuiltins_f( void ) {
	fileHandle_t f;
	char mapBase[MAX_QPATH];
	char exportPath[MAX_QPATH];
	char line[256];
	int i;
	int exported;
	int exportCount;
	qboolean includeBuiltins;

	CG_ZoneMapBaseName( mapBase, sizeof( mapBase ) );
	Com_sprintf( exportPath, sizeof( exportPath ), "zones/%s_builtin_race_zones.cfrag", mapBase );
	includeBuiltins = trap_Argc() > 1 && !Q_stricmp( CG_Argv( 1 ), "all" ) ? qtrue : qfalse;
	exportCount = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].active && zoneState.zones[i].type == ZONE_RACE && ( includeBuiltins || !zoneState.zones[i].builtin ) ) {
			exportCount++;
		}
	}
	if ( !exportCount ) {
		CG_ZoneSetStatus( va( "no %srace zones to export for %s", includeBuiltins ? "" : "editable ", mapBase ) );
		CG_Printf( "No %srace zones to export for %s.\n", includeBuiltins ? "" : "editable ", mapBase );
		return;
	}
	f = 0;
	trap_FS_FOpenFile( exportPath, &f, FS_APPEND );
	if ( !f ) {
		CG_ZoneSetStatus( "could not write race zone export" );
		CG_Printf( "Could not write %s\n", exportPath );
		return;
	}
	exported = 0;
	Com_sprintf( line, sizeof( line ), "\n/* %s %srace zones */\n", mapBase, includeBuiltins ? "all " : "editable " );
	trap_FS_Write( line, strlen( line ), f );
	for ( i = 0; i < zoneState.count; i++ ) {
		speedrunZone_t *zone;

		zone = &zoneState.zones[i];
		if ( !zone->active || zone->type != ZONE_RACE || ( !includeBuiltins && zone->builtin ) ) {
			continue;
		}
		Com_sprintf( line, sizeof( line ), "\t{ \"%s\", \"%s\", %i, { %.3ff, %.3ff, %.3ff }, { %.3ff, %.3ff, %.3ff }, { %.3ff, %.3ff, %.3ff } },\n",
					 mapBase,
					 zone->name,
					 zone->order,
					 zone->mins[0], zone->mins[1], zone->mins[2],
					 zone->maxs[0], zone->maxs[1], zone->maxs[2],
					 zone->angles[0], zone->angles[1], zone->angles[2] );
		trap_FS_Write( line, strlen( line ), f );
		exported++;
	}
	trap_FS_FCloseFile( f );
	CG_ZoneSetStatus( va( "exported %i race zone%s to %s", exported, exported == 1 ? "" : "s", exportPath ) );
	CG_Printf( "Exported %i race zone%s to %s\n", exported, exported == 1 ? "" : "s", exportPath );
}

void CG_InitZones( void ) {
	memset( &zoneState, 0, sizeof( zoneState ) );
	zoneState.selectedZone = -1;
	zoneState.hoveredZone = -1;
	zoneState.hoveredCorner = -1;
	zoneState.dragZone = -1;
	zoneState.dragCorner = -1;
	zoneState.wasInStartZone = -1;
	zoneState.activeStartZone = -1;
	zoneState.nextRouteId = 1;
	CG_ZoneBuildFilePath();
	trap_Cvar_Register( NULL, "sp_zone_count", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected", "-1", 0 );
	trap_Cvar_Register( NULL, "sp_zone_active_route", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_active_start", "-1", 0 );
	trap_Cvar_Register( NULL, "sp_zone_active_start_name", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_file", zoneState.filePath, 0 );
	trap_Cvar_Register( NULL, "sp_zone_status_text", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_last_split", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_last_delta", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_last_total_delta", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_last_delta_pb", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_last_total_delta_pb", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_delta_visible", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_progress_text", "-", 0 );
	trap_Cvar_Register( NULL, "sp_zone_progress_index", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_progress_count", "0", 0 );
	trap_Cvar_Register( NULL, "sp_timer_decimals", "1", CVAR_ARCHIVE );
	trap_Cvar_Register( NULL, "sp_zone_completed_index", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_race_points", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_race_total", "0", 0 );
	trap_Cvar_Register( NULL, "sp_race_objectives_found", "0", 0 );
	trap_Cvar_Register( NULL, "sp_race_objectives_total", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_run_time", "0.000", 0 );
	trap_Cvar_Register( NULL, "sp_zone_record_time", "-", 0 );
	trap_Cvar_Register( NULL, "sp_zone_record_msec", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_route_name", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_name", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_type", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_builtin", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_route", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_order", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_mins", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_maxs", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_angles", "", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_best", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_selected_best_total", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_view_route", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_row_count", "0", 0 );
	trap_Cvar_Register( NULL, "sp_zone_route_row_count", "0", 0 );
	CG_ZoneLoad_f();
	CG_ZoneUpdateCvars();
}

void CG_ZoneStatus_f( void ) {
	int i;

	CG_Printf( "Zones: %i zone%s, file %s, selected %i, active route %i\n", zoneState.count, zoneState.count == 1 ? "" : "s", zoneState.filePath, zoneState.selectedZone, zoneState.activeRouteId );
	for ( i = 0; i < zoneState.count; i++ ) {
		CG_Printf( "%2i: r%-3i o%-3i %-10s %-20s mins %.1f %.1f %.1f maxs %.1f %.1f %.1f ang %.1f %.1f %.1f bestSeg %i\n",
				   i,
				   zoneState.zones[i].routeId,
				   zoneState.zones[i].order,
				   CG_ZoneTypeName( zoneState.zones[i].type ),
				   zoneState.zones[i].name,
				   zoneState.zones[i].mins[0], zoneState.zones[i].mins[1], zoneState.zones[i].mins[2],
				   zoneState.zones[i].maxs[0], zoneState.zones[i].maxs[1], zoneState.zones[i].maxs[2],
				   zoneState.zones[i].angles[0], zoneState.zones[i].angles[1], zoneState.zones[i].angles[2],
				   zoneState.zones[i].bestSegmentMsec );
	}
	CG_ZoneUpdateCvars();
}

static qboolean CG_ZoneArgIsHere( int argIndex ) {
	const char *text;

	if ( trap_Argc() <= argIndex ) {
		return qfalse;
	}
	text = CG_Argv( argIndex );
	return ( !Q_stricmp( text, "here" ) || !Q_stricmp( text, "player" ) || !Q_stricmp( text, "stand" ) ) ? qtrue : qfalse;
}

static qboolean CG_ZoneIsLocked( int index ) {
	if ( index < 0 || index >= zoneState.count ) {
		return qfalse;
	}
	return zoneState.zones[index].builtin ? qtrue : qfalse;
}

static qboolean CG_ZoneRejectLockedEdit( int index ) {
	if ( !CG_ZoneIsLocked( index ) ) {
		return qfalse;
	}
	CG_ZoneSetStatus( "built-in race zone is locked" );
	return qtrue;
}

static int CG_ZoneRouteForAdd( zoneType_t type ) {
	int firstStart;

	if ( type == ZONE_START ) {
		return zoneState.nextRouteId++;
	}
	if ( type == ZONE_RACE ) {
		return 0;
	}
	if ( zoneState.activeRouteId > 0 ) {
		return zoneState.activeRouteId;
	}
	if ( zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count && zoneState.zones[zoneState.selectedZone].routeId > 0 ) {
		return zoneState.zones[zoneState.selectedZone].routeId;
	}
	firstStart = CG_ZoneFindFirstStart();
	return firstStart >= 0 ? zoneState.zones[firstStart].routeId : 0;
}

static void CG_ZoneAddAtPointOrdered( zoneType_t type, const vec3_t point, int routeId, int order, qboolean shiftOrders ) {
	speedrunZone_t *zone;
	int number;

	if ( zoneState.count >= ZONE_MAX_ZONES ) {
		CG_ZoneSetStatus( "zone limit reached" );
		return;
	}
	if ( routeId <= 0 ) {
		routeId = CG_ZoneRouteForAdd( type );
	}
	if ( type == ZONE_START ) {
		order = 0;
	} else if ( type == ZONE_RACE ) {
		routeId = 0;
		order = order > 0 ? order : CG_ZoneNextRaceOrder();
	} else if ( order <= 0 ) {
		order = CG_ZoneNextOrderForRoute( routeId );
	}
	if ( shiftOrders && type != ZONE_START && type != ZONE_RACE ) {
		CG_ZoneShiftRouteOrders( routeId, order, 1 );
	}
	zone = &zoneState.zones[zoneState.count];
	memset( zone, 0, sizeof( *zone ) );
	zone->active = qtrue;
	zone->type = type;
	zone->routeId = routeId;
	zone->order = order;
	number = zoneState.count + 1;
	Com_sprintf( zone->name, sizeof( zone->name ), "%s_%02i", CG_ZoneTypeName( type ), number );
	if ( type == ZONE_START ) {
		CG_ZoneDefaultRouteName( routeId, zone->routeName, sizeof( zone->routeName ) );
	}
	zone->mins[0] = point[0] - ZONE_DEFAULT_HALF_XY;
	zone->mins[1] = point[1] - ZONE_DEFAULT_HALF_XY;
	zone->mins[2] = point[2] - ZONE_DEFAULT_BOTTOM;
	zone->maxs[0] = point[0] + ZONE_DEFAULT_HALF_XY;
	zone->maxs[1] = point[1] + ZONE_DEFAULT_HALF_XY;
	zone->maxs[2] = point[2] + ZONE_DEFAULT_TOP;
	CG_ZoneNormalizeBounds( zone );
	zoneState.selectedZone = zoneState.count;
	zoneState.count++;
	if ( routeId > 0 ) {
		zoneState.activeRouteId = routeId;
		CG_ZoneRefreshRouteOrderAndNames( routeId );
	}
	CG_ZoneSetStatus( type == ZONE_RACE ? va( "added race zone %i", order ) : va( "added %s zone to route %i", CG_ZoneTypeName( type ), routeId ) );
	CG_ZoneUpdateCvars();
}

static void CG_ZoneAddAtPoint( zoneType_t type, const vec3_t point ) {
	CG_ZoneAddAtPointOrdered( type, point, CG_ZoneRouteForAdd( type ), 0, qfalse );
}

void CG_ZoneAdd_f( void ) {
	zoneType_t type;
	vec3_t point;
	qboolean here;
	const char *typeText;
	const char *modeText;

	typeText = CG_Argv( 1 );
	modeText = CG_Argv( 2 );
	type = CG_ZoneTypeFromString( typeText );
	if ( type == ZONE_RACE && !CG_ZoneRaceDebugEnabled() ) {
		CG_ZoneSetStatus( "race zone debug disabled" );
		return;
	}
	here = modeText && ( !Q_stricmp( modeText, "here" ) || !Q_stricmp( modeText, "player" ) || !Q_stricmp( modeText, "stand" ) ) ? qtrue : qfalse;
	CG_ZonePickPoint( point, here );
	CG_ZoneAddAtPoint( type, point );
}

void CG_ZoneAddStart_f( void ) {
	vec3_t point;
	CG_ZonePickPoint( point, CG_ZoneArgIsHere( 1 ) );
	CG_ZoneAddAtPoint( ZONE_START, point );
}

void CG_ZoneAddCheckpoint_f( void ) {
	vec3_t point;
	CG_ZonePickPoint( point, CG_ZoneArgIsHere( 1 ) );
	CG_ZoneAddAtPoint( ZONE_CHECKPOINT, point );
}

void CG_ZoneInsertCheckpoint_f( void ) {
	vec3_t point;
	int routeId;
	int order;

	routeId = zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ? zoneState.zones[zoneState.selectedZone].routeId : zoneState.activeRouteId;
	if ( routeId <= 0 ) {
		routeId = zoneState.activeRouteId;
	}
	if ( routeId <= 0 ) {
		CG_ZoneSetStatus( "no active route" );
		return;
	}
	order = CG_ZoneInsertOrderForRoute( routeId );
	CG_ZonePickPoint( point, CG_ZoneArgIsHere( 1 ) );
	CG_ZoneAddAtPointOrdered( ZONE_CHECKPOINT, point, routeId, order, qtrue );
	CG_ZoneSetStatus( va( "inserted checkpoint at order %i", order ) );
}

void CG_ZoneAddFinish_f( void ) {
	vec3_t point;
	CG_ZonePickPoint( point, CG_ZoneArgIsHere( 1 ) );
	CG_ZoneAddAtPoint( ZONE_FINISH, point );
}

void CG_ZoneAddRace_f( void ) {
	vec3_t point;
	if ( !CG_ZoneRaceDebugEnabled() ) {
		CG_ZoneSetStatus( "race zone debug disabled" );
		return;
	}
	CG_ZonePickPoint( point, CG_ZoneArgIsHere( 1 ) );
	CG_ZoneAddAtPoint( ZONE_RACE, point );
}

void CG_ZoneDelete_f( void ) {
	int index;
	int i;
	int routeId;

	index = trap_Argc() > 1 ? atoi( CG_Argv( 1 ) ) : zoneState.selectedZone;
	if ( index < 0 || index >= zoneState.count ) {
		CG_ZoneSetStatus( "no zone selected" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( index ) ) {
		return;
	}
	routeId = zoneState.zones[index].routeId;
	for ( i = index; i < zoneState.count - 1; i++ ) {
		zoneState.zones[i] = zoneState.zones[i + 1];
	}
	zoneState.count--;
	if ( zoneState.count <= 0 ) {
		zoneState.selectedZone = -1;
	} else if ( index >= zoneState.count ) {
		zoneState.selectedZone = zoneState.count - 1;
	} else {
		zoneState.selectedZone = index;
	}
	CG_ZoneRefreshRouteOrderAndNames( routeId );
	CG_ZoneValidateActiveRoute();
	CG_ZoneResetRun( qtrue );
	CG_ZoneSetStatus( "zone deleted" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneClear_f( void ) {
	int i;
	int kept;

	kept = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( zoneState.zones[i].builtin ) {
			if ( kept != i ) {
				zoneState.zones[kept] = zoneState.zones[i];
			}
			kept++;
		}
	}
	for ( i = kept; i < ZONE_MAX_ZONES; i++ ) {
		memset( &zoneState.zones[i], 0, sizeof( zoneState.zones[i] ) );
	}
	zoneState.count = kept;
	zoneState.selectedZone = -1;
	zoneState.hoveredZone = -1;
	zoneState.hoveredCorner = -1;
	zoneState.activeRouteId = 0;
	zoneState.nextRouteId = 1;
	CG_ZoneResetRun( qtrue );
	CG_ZoneSetStatus( kept > 0 ? "editable zones cleared; built-in zones kept" : "zones cleared" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneSelect_f( void ) {
	int index;

	if ( trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_select <index>\n" );
		return;
	}
	index = atoi( CG_Argv( 1 ) );
	if ( index < -1 || index >= zoneState.count ) {
		CG_ZoneSetStatus( "invalid zone index" );
		return;
	}
	zoneState.selectedZone = index;
	if ( index >= 0 && zoneState.zones[index].routeId > 0 ) {
		zoneState.activeRouteId = zoneState.zones[index].routeId;
	}
	CG_ZoneUpdateCvars();
}

void CG_ZoneRoute_f( void ) {
	int index;

	index = trap_Argc() > 1 ? atoi( CG_Argv( 1 ) ) : zoneState.selectedZone;
	if ( index < 0 || index >= zoneState.count || zoneState.zones[index].routeId <= 0 ) {
		CG_Printf( "usage: sp_zone_route <zone index from route/start>\n" );
		return;
	}
	zoneState.activeRouteId = zoneState.zones[index].routeId;
	CG_ZoneSetStatus( va( "active route set to %i", zoneState.activeRouteId ) );
	CG_ZoneUpdateCvars();
}

void CG_ZoneNext_f( void ) {
	if ( zoneState.count <= 0 ) {
		zoneState.selectedZone = -1;
	} else {
		zoneState.selectedZone++;
		if ( zoneState.selectedZone >= zoneState.count ) zoneState.selectedZone = 0;
		if ( zoneState.zones[zoneState.selectedZone].routeId > 0 ) zoneState.activeRouteId = zoneState.zones[zoneState.selectedZone].routeId;
	}
	CG_ZoneUpdateCvars();
}

void CG_ZonePrev_f( void ) {
	if ( zoneState.count <= 0 ) {
		zoneState.selectedZone = -1;
	} else {
		zoneState.selectedZone--;
		if ( zoneState.selectedZone < 0 ) zoneState.selectedZone = zoneState.count - 1;
		if ( zoneState.zones[zoneState.selectedZone].routeId > 0 ) zoneState.activeRouteId = zoneState.zones[zoneState.selectedZone].routeId;
	}
	CG_ZoneUpdateCvars();
}

static void CG_ZoneOrderMove( int direction ) {
	speedrunZone_t *selected;
	int i;
	int otherIndex;
	int otherOrder;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count ) {
		CG_ZoneSetStatus( "no zone selected" );
		return;
	}
	selected = &zoneState.zones[zoneState.selectedZone];
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	if ( !CG_ZoneIsRouteSplit( selected ) || selected->order <= 0 ) {
		CG_ZoneSetStatus( "zone has no split order" );
		return;
	}
	otherIndex = -1;
	otherOrder = direction < 0 ? 0 : 999999;
	for ( i = 0; i < zoneState.count; i++ ) {
		speedrunZone_t *zone = &zoneState.zones[i];
		if ( i == zoneState.selectedZone || !zone->active || zone->routeId != selected->routeId || !CG_ZoneIsRouteSplit( zone ) ) {
			continue;
		}
		if ( direction < 0 && zone->order < selected->order && zone->order > otherOrder ) {
			otherOrder = zone->order;
			otherIndex = i;
		} else if ( direction > 0 && zone->order > selected->order && zone->order < otherOrder ) {
			otherOrder = zone->order;
			otherIndex = i;
		}
	}
	if ( otherIndex < 0 ) {
		CG_ZoneSetStatus( "no order slot" );
		return;
	}
	zoneState.zones[otherIndex].order = selected->order;
	selected->order = otherOrder;
	CG_ZoneRefreshRouteOrderAndNames( selected->routeId );
	CG_ZoneSetStatus( "zone order changed" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneOrderUp_f( void ) {
	CG_ZoneOrderMove( -1 );
}

void CG_ZoneOrderDown_f( void ) {
	CG_ZoneOrderMove( 1 );
}

void CG_ZoneSetType_f( void ) {
	zoneType_t oldType;
	zoneType_t newType;
	int oldRouteId;
	speedrunZone_t *zone;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_type <start|checkpoint|finish|race>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	zone = &zoneState.zones[zoneState.selectedZone];
	oldType = zone->type;
	oldRouteId = zone->routeId;
	newType = CG_ZoneTypeFromString( CG_Argv( 1 ) );
	if ( newType == ZONE_RACE && !CG_ZoneRaceDebugEnabled() ) {
		CG_ZoneSetStatus( "race zone debug disabled" );
		return;
	}
	zone->type = newType;
	if ( newType == ZONE_START && oldType != ZONE_START ) {
		zone->routeId = zoneState.nextRouteId++;
		zone->order = 0;
		CG_ZoneDefaultRouteName( zone->routeId, zone->routeName, sizeof( zone->routeName ) );
		zoneState.activeRouteId = zone->routeId;
	} else if ( newType == ZONE_RACE ) {
		zone->routeId = 0;
		zone->order = CG_ZoneNextRaceOrder();
		zone->routeName[0] = '\0';
		zone->bestSegmentMsec = 0;
		zone->bestTotalMsec = 0;
	} else if ( newType == ZONE_START ) {
		if ( zone->routeId <= 0 ) zone->routeId = zoneState.nextRouteId++;
		zone->order = 0;
		if ( !zone->routeName[0] ) CG_ZoneDefaultRouteName( zone->routeId, zone->routeName, sizeof( zone->routeName ) );
		zoneState.activeRouteId = zone->routeId;
	} else if ( zone->routeId <= 0 ) {
		zone->routeId = CG_ZoneRouteForAdd( newType );
		zone->order = CG_ZoneNextOrderForRoute( zone->routeId );
	} else if ( oldType == ZONE_START || zone->order <= 0 ) {
		zone->order = CG_ZoneNextOrderForRoute( zone->routeId );
	}
	if ( oldRouteId > 0 && oldRouteId != zone->routeId ) {
		CG_ZoneRefreshRouteOrderAndNames( oldRouteId );
	}
	CG_ZoneRefreshRouteOrderAndNames( zone->routeId );
	CG_ZoneValidateActiveRoute();
	CG_ZoneSetStatus( "zone type changed" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneName_f( void ) {
	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_name <name_without_spaces>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	Q_strncpyz( zoneState.zones[zoneState.selectedZone].name, CG_Argv( 1 ), sizeof( zoneState.zones[zoneState.selectedZone].name ) );
	CG_ZoneSanitizeName( zoneState.zones[zoneState.selectedZone].name );
	CG_ZoneSetStatus( "zone name changed" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneRouteName_f( void ) {
	char name[ZONE_NAME_LEN];
	int routeId;

	if ( trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_route_name <name_without_spaces>\n" );
		return;
	}
	routeId = zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ? zoneState.zones[zoneState.selectedZone].routeId : zoneState.activeRouteId;
	if ( routeId <= 0 ) {
		CG_ZoneSetStatus( "no active route" );
		return;
	}
	trap_Args( name, sizeof( name ) );
	CG_ZoneSetRouteNameForRoute( routeId, name );
	CG_ZoneSetStatus( "route name changed" );
	CG_ZoneUpdateCvars();
}

void CG_ZoneGrow_f( void ) {
	float amount;
	int i;
	speedrunZone_t *zone;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count ) {
		CG_ZoneSetStatus( "no zone selected" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	amount = trap_Argc() > 1 ? atof( CG_Argv( 1 ) ) : 8.0f;
	zone = &zoneState.zones[zoneState.selectedZone];
	for ( i = 0; i < 3; i++ ) {
		zone->mins[i] -= amount;
		zone->maxs[i] += amount;
	}
	CG_ZoneNormalizeBounds( zone );
	CG_ZoneUpdateCvars();
}

void CG_ZoneMove_f( void ) {
	vec3_t delta;
	int i;
	speedrunZone_t *zone;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 4 ) {
		CG_Printf( "usage: sp_zone_move <x> <y> <z>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	delta[0] = atof( CG_Argv( 1 ) );
	delta[1] = atof( CG_Argv( 2 ) );
	delta[2] = atof( CG_Argv( 3 ) );
	zone = &zoneState.zones[zoneState.selectedZone];
	for ( i = 0; i < 3; i++ ) {
		zone->mins[i] += delta[i];
		zone->maxs[i] += delta[i];
	}
	CG_ZoneUpdateCvars();
}

void CG_ZoneSetBounds_f( void ) {
	speedrunZone_t *zone;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 7 ) {
		CG_Printf( "usage: sp_zone_bounds <minx> <miny> <minz> <maxx> <maxy> <maxz>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	zone = &zoneState.zones[zoneState.selectedZone];
	zone->mins[0] = atof( CG_Argv( 1 ) );
	zone->mins[1] = atof( CG_Argv( 2 ) );
	zone->mins[2] = atof( CG_Argv( 3 ) );
	zone->maxs[0] = atof( CG_Argv( 4 ) );
	zone->maxs[1] = atof( CG_Argv( 5 ) );
	zone->maxs[2] = atof( CG_Argv( 6 ) );
	CG_ZoneNormalizeBounds( zone );
	CG_ZoneUpdateCvars();
}

static qboolean CG_ZoneParseAngleAxis( const char *text, int *axis ) {
	if ( !text || !axis ) {
		return qfalse;
	}
	if ( !Q_stricmp( text, "pitch" ) || !Q_stricmp( text, "x" ) ) {
		*axis = PITCH;
		return qtrue;
	}
	if ( !Q_stricmp( text, "yaw" ) || !Q_stricmp( text, "y" ) ) {
		*axis = YAW;
		return qtrue;
	}
	if ( !Q_stricmp( text, "roll" ) || !Q_stricmp( text, "z" ) ) {
		*axis = ROLL;
		return qtrue;
	}
	return qfalse;
}

void CG_ZoneSetAngles_f( void ) {
	speedrunZone_t *zone;
	const char *arg;
	int axis;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_angles <pitch> <yaw> <roll>|reset|view|<pitch|yaw|roll> <value>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	zone = &zoneState.zones[zoneState.selectedZone];
	arg = CG_Argv( 1 );
	if ( !Q_stricmp( arg, "reset" ) || !Q_stricmp( arg, "zero" ) ) {
		VectorClear( zone->angles );
	} else if ( !Q_stricmp( arg, "view" ) ) {
		VectorSet( zone->angles, 0.0f, cg.refdefViewAngles[YAW], 0.0f );
	} else if ( CG_ZoneParseAngleAxis( arg, &axis ) ) {
		if ( trap_Argc() < 3 ) {
			CG_Printf( "usage: sp_zone_angles %s <value>\n", arg );
			return;
		}
		zone->angles[axis] = atof( CG_Argv( 2 ) );
	} else {
		if ( trap_Argc() < 4 ) {
			CG_Printf( "usage: sp_zone_angles <pitch> <yaw> <roll>\n" );
			return;
		}
		zone->angles[PITCH] = atof( CG_Argv( 1 ) );
		zone->angles[YAW] = atof( CG_Argv( 2 ) );
		zone->angles[ROLL] = atof( CG_Argv( 3 ) );
	}
	CG_ZoneNormalizeAngles( zone );
	CG_ZoneSetStatus( va( "zone angles %.1f %.1f %.1f", zone->angles[0], zone->angles[1], zone->angles[2] ) );
	CG_ZoneUpdateCvars();
}

void CG_ZoneRotate_f( void ) {
	speedrunZone_t *zone;
	const char *arg;
	int axis;

	if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count || trap_Argc() < 2 ) {
		CG_Printf( "usage: sp_zone_rotate <pitch> <yaw> <roll>|reset|view|<pitch|yaw|roll> <delta>\n" );
		return;
	}
	if ( CG_ZoneRejectLockedEdit( zoneState.selectedZone ) ) {
		return;
	}
	zone = &zoneState.zones[zoneState.selectedZone];
	arg = CG_Argv( 1 );
	if ( !Q_stricmp( arg, "reset" ) || !Q_stricmp( arg, "zero" ) ) {
		VectorClear( zone->angles );
	} else if ( !Q_stricmp( arg, "view" ) ) {
		VectorSet( zone->angles, 0.0f, cg.refdefViewAngles[YAW], 0.0f );
	} else if ( CG_ZoneParseAngleAxis( arg, &axis ) ) {
		if ( trap_Argc() < 3 ) {
			CG_Printf( "usage: sp_zone_rotate %s <delta>\n", arg );
			return;
		}
		zone->angles[axis] += atof( CG_Argv( 2 ) );
	} else {
		if ( trap_Argc() < 4 ) {
			CG_Printf( "usage: sp_zone_rotate <pitch> <yaw> <roll>\n" );
			return;
		}
		zone->angles[PITCH] += atof( CG_Argv( 1 ) );
		zone->angles[YAW] += atof( CG_Argv( 2 ) );
		zone->angles[ROLL] += atof( CG_Argv( 3 ) );
	}
	CG_ZoneNormalizeAngles( zone );
	CG_ZoneSetStatus( va( "zone angles %.1f %.1f %.1f", zone->angles[0], zone->angles[1], zone->angles[2] ) );
	CG_ZoneUpdateCvars();
}

void CG_ZoneResetRun_f( void ) {
	CG_ZoneResetRun( qtrue );
	CG_ZoneSetStatus( "zone timer reset" );
}

void CG_ZoneResetTimes_f( void ) {
	const char *mode;
	int i;
	int index;
	int routeId;
	int minOrder;
	int resetCount;

	mode = trap_Argc() > 1 ? CG_Argv( 1 ) : "selected";
	resetCount = 0;
	if ( !Q_stricmp( mode, "all" ) ) {
		for ( i = 0; i < zoneState.count; i++ ) {
			if ( zoneState.zones[i].builtin ) {
				continue;
			}
			zoneState.zones[i].bestSegmentMsec = 0;
			zoneState.zones[i].bestTotalMsec = 0;
			resetCount++;
		}
	} else if ( !Q_stricmp( mode, "route" ) ) {
		routeId = zoneState.selectedZone >= 0 && zoneState.selectedZone < zoneState.count ? zoneState.zones[zoneState.selectedZone].routeId : zoneState.activeRouteId;
		if ( routeId <= 0 ) {
			CG_ZoneSetStatus( "no active route" );
			return;
		}
		for ( i = 0; i < zoneState.count; i++ ) {
			if ( zoneState.zones[i].active && !zoneState.zones[i].builtin && zoneState.zones[i].routeId == routeId ) {
				zoneState.zones[i].bestSegmentMsec = 0;
				zoneState.zones[i].bestTotalMsec = 0;
				resetCount++;
			}
		}
	} else if ( !Q_stricmp( mode, "from" ) ) {
		if ( zoneState.selectedZone < 0 || zoneState.selectedZone >= zoneState.count ) {
			CG_ZoneSetStatus( "no zone selected" );
			return;
		}
		routeId = zoneState.zones[zoneState.selectedZone].routeId;
		minOrder = zoneState.zones[zoneState.selectedZone].type == ZONE_START ? 0 : zoneState.zones[zoneState.selectedZone].order;
		for ( i = 0; i < zoneState.count; i++ ) {
			if ( !zoneState.zones[i].active || zoneState.zones[i].builtin || zoneState.zones[i].routeId != routeId ) {
				continue;
			}
			if ( minOrder > 0 && ( zoneState.zones[i].type == ZONE_START || zoneState.zones[i].order < minOrder ) ) {
				continue;
			}
			zoneState.zones[i].bestSegmentMsec = 0;
			zoneState.zones[i].bestTotalMsec = 0;
			resetCount++;
		}
	} else {
		if ( !Q_stricmp( mode, "selected" ) ) {
			index = zoneState.selectedZone;
		} else if ( mode[0] >= '0' && mode[0] <= '9' ) {
			index = atoi( mode );
		} else {
			CG_ZoneSetStatus( "unknown reset mode" );
			return;
		}
		if ( index < 0 || index >= zoneState.count ) {
			CG_ZoneSetStatus( "no zone selected" );
			return;
		}
		if ( CG_ZoneRejectLockedEdit( index ) ) {
			return;
		}
		zoneState.zones[index].bestSegmentMsec = 0;
		zoneState.zones[index].bestTotalMsec = 0;
		resetCount = 1;
	}

	CG_ZoneSyncRouteBestTotals();
	CG_ZoneResetRun( qtrue );
	CG_ZoneSetStatus( va( "reset times for %i zone%s", resetCount, resetCount == 1 ? "" : "s" ) );
	CG_ZoneUpdateCvars();
}

void CG_ZoneEdit_f( void ) {
	if ( trap_Argc() < 2 ) {
		trap_Cvar_Set( "sp_zone_edit", cg_zoneEdit.integer ? "0" : "1" );
	} else {
		trap_Cvar_Set( "sp_zone_edit", atoi( CG_Argv( 1 ) ) ? "1" : "0" );
	}
	trap_Cvar_Update( &cg_zoneEdit );
	CG_ZoneUpdateCvars();
}

void CG_ZoneSave_f( void ) {
	fileHandle_t f;
	char line[256];
	int i;
	int saved;

	f = 0;
	trap_FS_FOpenFile( zoneState.filePath, &f, FS_WRITE );
	if ( !f ) {
		CG_ZoneSetStatus( "could not write zone file" );
		return;
	}
	saved = 0;
	for ( i = 0; i < zoneState.count; i++ ) {
		if ( !zoneState.zones[i].builtin ) {
			saved++;
		}
	}
	Com_sprintf( line, sizeof( line ), "rtcw_speedrun_zones 1\ncount %i\n", saved );
	trap_FS_Write( line, strlen( line ), f );
	for ( i = 0; i < zoneState.count; i++ ) {
		speedrunZone_t *zone = &zoneState.zones[i];
		if ( zone->builtin ) {
			continue;
		}
		Com_sprintf( line, sizeof( line ), "zone6 %s %s %i %i %s %i %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %i\n",
					 CG_ZoneTypeName( zone->type ), zone->name, zone->routeId, zone->order, zone->routeName[0] ? zone->routeName : "-",
					 zone->bestTotalMsec, zone->mins[0], zone->mins[1], zone->mins[2], zone->maxs[0], zone->maxs[1], zone->maxs[2],
					 zone->angles[0], zone->angles[1], zone->angles[2], zone->bestSegmentMsec );
		trap_FS_Write( line, strlen( line ), f );
	}
	trap_FS_FCloseFile( f );
	CG_ZoneSetStatus( va( "saved %i editable zone%s", saved, saved == 1 ? "" : "s" ) );
	CG_ZoneUpdateCvars();
}

void CG_ZoneLoad_f( void ) {
	fileHandle_t f;
	int len;
	char buffer[ZONE_FILE_MAX];
	char *cursor;
	int loaded;

	f = 0;
	len = trap_FS_FOpenFile( zoneState.filePath, &f, FS_READ );
	if ( len <= 0 || !f ) {
		if ( f ) trap_FS_FCloseFile( f );
		memset( zoneState.zones, 0, sizeof( zoneState.zones ) );
		zoneState.count = 0;
		zoneState.selectedZone = -1;
		zoneState.activeRouteId = 0;
		zoneState.nextRouteId = 1;
		zoneState.wasInStartZone = -1;
		zoneState.hasBuiltinRaceZones = qfalse;
		CG_ZoneAppendBuiltinRaceZones();
		zoneState.selectedZone = zoneState.count > 0 ? 0 : -1;
		CG_ZoneResetRun( qtrue );
		CG_ZoneSetStatus( zoneState.count > 0 ? va( "loaded %i built-in race zone%s", zoneState.count, zoneState.count == 1 ? "" : "s" ) : "no zone file for this map" );
		CG_ZoneUpdateCvars();
		return;
	}
	if ( len >= (int)sizeof( buffer ) ) {
		len = sizeof( buffer ) - 1;
	}
	trap_FS_Read( buffer, len, f );
	trap_FS_FCloseFile( f );
	buffer[len] = '\0';

	memset( zoneState.zones, 0, sizeof( zoneState.zones ) );
	zoneState.count = 0;
	zoneState.selectedZone = -1;
	zoneState.activeRouteId = 0;
	zoneState.nextRouteId = 1;
	zoneState.hasBuiltinRaceZones = qfalse;
	loaded = 0;
	cursor = buffer;
	while ( cursor && *cursor ) {
		char *line;
		char *next;
		char typeText[32];
		char nameText[ZONE_NAME_LEN];
		char routeNameText[ZONE_NAME_LEN];
		float minx, miny, minz, maxx, maxy, maxz;
		float pitch, yaw, roll;
		int best;
		int bestTotal;
		int order;
		int routeId;
		int parsed;

		line = cursor;
		next = strchr( cursor, '\n' );
		if ( next ) {
			*next = '\0';
			cursor = next + 1;
		} else {
			cursor = NULL;
		}
		best = 0;
		bestTotal = 0;
		pitch = 0.0f;
		yaw = 0.0f;
		roll = 0.0f;
		order = 0;
		routeId = 0;
		routeNameText[0] = '\0';
		parsed = sscanf( line, "zone6 %31s %31s %i %i %31s %i %f %f %f %f %f %f %f %f %f %i", typeText, nameText, &routeId, &order, routeNameText, &bestTotal, &minx, &miny, &minz, &maxx, &maxy, &maxz, &pitch, &yaw, &roll, &best );
		if ( parsed != 16 ) {
			parsed = sscanf( line, "zone5 %31s %31s %i %i %31s %i %f %f %f %f %f %f %i", typeText, nameText, &routeId, &order, routeNameText, &bestTotal, &minx, &miny, &minz, &maxx, &maxy, &maxz, &best );
		}
		if ( parsed == 13 || parsed == 16 ) {
			speedrunZone_t *zone;
			if ( loaded >= ZONE_MAX_ZONES ) {
				break;
			}
			zone = &zoneState.zones[loaded];
			zone->active = qtrue;
			zone->type = CG_ZoneTypeFromString( typeText );
			if ( zone->type == ZONE_START && routeId <= 0 ) {
				routeId = zoneState.nextRouteId++;
			} else if ( zone->type != ZONE_RACE && routeId <= 0 ) {
				continue;
			}
			if ( routeId > 0 && routeId >= zoneState.nextRouteId ) {
				zoneState.nextRouteId = routeId + 1;
			}
			zone->routeId = routeId;
			zoneState.count = loaded;
			if ( zone->type == ZONE_START ) {
				zone->order = 0;
				if ( routeNameText[0] && Q_stricmp( routeNameText, "-" ) ) {
					Q_strncpyz( zone->routeName, routeNameText, sizeof( zone->routeName ) );
					CG_ZoneSanitizeName( zone->routeName );
				} else {
					CG_ZoneDefaultRouteName( routeId, zone->routeName, sizeof( zone->routeName ) );
				}
				if ( !zone->routeName[0] ) {
					CG_ZoneDefaultRouteName( routeId, zone->routeName, sizeof( zone->routeName ) );
				}
			} else if ( zone->type == ZONE_RACE ) {
				zone->order = order > 0 ? order : CG_ZoneNextRaceOrder();
			} else if ( order > 0 ) {
				zone->order = order;
			} else {
				zone->order = CG_ZoneNextOrderForRoute( routeId );
			}
			Q_strncpyz( zone->name, nameText, sizeof( zone->name ) );
			CG_ZoneSanitizeName( zone->name );
			VectorSet( zone->mins, minx, miny, minz );
			VectorSet( zone->maxs, maxx, maxy, maxz );
			if ( parsed == 16 ) {
				VectorSet( zone->angles, pitch, yaw, roll );
				CG_ZoneNormalizeAngles( zone );
			}
			zone->bestSegmentMsec = best < 0 ? 0 : best;
			zone->bestTotalMsec = bestTotal < 0 ? 0 : bestTotal;
			CG_ZoneNormalizeBounds( zone );
			loaded++;
		}
	}
	zoneState.count = loaded;
	CG_ZoneAppendBuiltinRaceZones();
	for ( loaded = 0; loaded < zoneState.count; loaded++ ) {
		if ( zoneState.zones[loaded].active && zoneState.zones[loaded].routeId > 0 ) {
			CG_ZoneRefreshRouteOrderAndNames( zoneState.zones[loaded].routeId );
		}
	}
	CG_ZoneSyncRouteBestTotals();
	zoneState.selectedZone = loaded > 0 ? 0 : -1;
	CG_ZoneValidateActiveRoute();
	CG_ZoneResetRun( qtrue );
	CG_ZoneSetStatus( va( "loaded %i zone%s", loaded, loaded == 1 ? "" : "s" ) );
	CG_ZoneUpdateCvars();
}