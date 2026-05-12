#ifndef CL_SPEEDRUN_IMGUI_H
#define CL_SPEEDRUN_IMGUI_H

#ifdef __cplusplus
extern "C" {
#endif

void CL_SpeedrunImGui_Init( void );
void CL_SpeedrunImGui_Shutdown( void );
void CL_SpeedrunImGui_InvalidateDeviceObjects( void );
void CL_SpeedrunImGui_Draw( void );
int  CL_SpeedrunImGui_WndProc( void *hWnd, unsigned int uMsg, unsigned int wParam, long lParam );
int  CL_SpeedrunImGui_IsOpen( void );
void CL_SpeedrunImGui_CloseAllForGameplay( void );
void CL_SpeedrunImGui_OpenRace( void );
void CL_SpeedrunImGui_OpenRaceChat( void );
int  CL_SpeedrunImGui_IsRaceChatOpen( void );

#ifdef __cplusplus
}
#endif

#endif /* CL_SPEEDRUN_IMGUI_H */
