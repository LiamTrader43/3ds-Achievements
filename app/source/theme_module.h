#ifndef THEME_MODULE_H
#define THEME_MODULE_H

// For a theme built as an add-on (themes/<id>/, built with THEME_MODULE defined): include it
// AFTER every other header. The app's functions and globals the theme uses then go through
// the ThemeApi the app hands it (theme_api.h), so the same source works built into the app
// too (then this does nothing but THEME_MODULE_DEFINE).
//
// The theme ends with THEME_MODULE_DEFINE(itsTheme, "id", "Name").

#include "theme_api.h"

#ifdef THEME_MODULE

extern const ThemeApi *theme_api;

#define Draw_Eyes               (theme_api->Draw_Eyes)
#define Draw_Eye                (theme_api->Draw_Eye)
#define Draw_Depth              (theme_api->Draw_Depth)
#define Draw_Rect               (theme_api->Draw_Rect)
#define Draw_RoundRect          (theme_api->Draw_RoundRect)
#define Draw_RoundRectGradient  (theme_api->Draw_RoundRectGradient)
#define Draw_ClipRows           (theme_api->Draw_ClipRows)
#define Draw_SetFont            (theme_api->Draw_SetFont)
#define Draw_Text               (theme_api->Draw_Text)
#define Draw_TextWidth          (theme_api->Draw_TextWidth)
#define Draw_TextFit            (theme_api->Draw_TextFit)
#define Draw_TextShadow         (theme_api->Draw_TextShadow)
#define Draw_Icon               (theme_api->Draw_Icon)
#define Draw_TrophyIcon         (theme_api->Draw_TrophyIcon)
#define Draw_IconScaled         (theme_api->Draw_IconScaled)
#define Draw_DimHero            (theme_api->Draw_DimHero)
#define Draw_Hero               (theme_api->Draw_Hero)
#define Draw_Image              (theme_api->Draw_Image)
#define Draw_FrameShine         (theme_api->Draw_FrameShine)
#define Draw_FrameShineAgain    (theme_api->Draw_FrameShineAgain)
#define Draw_FrameShineOff      (theme_api->Draw_FrameShineOff)
#define Draw_Shadow             (theme_api->Draw_Shadow)
#define Draw_Darken             (theme_api->Draw_Darken)
#define Draw_RoundRing          (theme_api->Draw_RoundRing)
#define Draw_StampKeep          (theme_api->Draw_StampKeep)
#define Draw_StampAgain         (theme_api->Draw_StampAgain)
#define Draw_Gradient           (theme_api->Draw_Gradient)
#define Draw_FadeToGradient     (theme_api->Draw_FadeToGradient)
#define Draw_Into               (theme_api->Draw_Into)
#define Draw_Width              (theme_api->Draw_Width)
#define Ui_PopupText            (theme_api->Ui_PopupText)
#define Ui_Trophy               (theme_api->Ui_Trophy)
#define Ui_EllipsizeScaled      (theme_api->Ui_EllipsizeScaled)
#define Ui_Ellipsize            (theme_api->Ui_Ellipsize)
#define Ui_DrawWrappedScaled    (theme_api->Ui_DrawWrappedScaled)
#define Ui_DrawWrapped          (theme_api->Ui_DrawWrapped)
#define Ui_Totals               (theme_api->Ui_Totals)
#define Ui_Setting              (theme_api->Ui_Setting)
#define Ui_Approach             (theme_api->Ui_Approach)
#define Ui_Battery              (theme_api->Ui_Battery)
#define Update_AppVersion       (theme_api->Update_AppVersion)
#define Share_View              (theme_api->Share_View)
#define Share_QrModule          (theme_api->Share_QrModule)
#define osGetTime               (theme_api->osGetTime)
#define svcGetSystemTick        (theme_api->svcGetSystemTick)
#define snprintf                (theme_api->snprintf)
#define vsnprintf               (theme_api->vsnprintf)
#define strftime                (theme_api->strftime)
#define time                    (theme_api->time)
#define gmtime                  (theme_api->gmtime)

#define g_games                 (*theme_api->g_games)
#define g_gameCount             (*theme_api->g_gameCount)
#define g_picking               (*theme_api->g_picking)
#define g_names                 (*theme_api->g_names)
#define g_descriptions          (*theme_api->g_descriptions)
#define g_points                (*theme_api->g_points)
#define g_unlocked              (*theme_api->g_unlocked)
#define g_count                 (*theme_api->g_count)
#define g_selected              (*theme_api->g_selected)
#define g_gameUnlocked          (*theme_api->g_gameUnlocked)
#define g_gameLocked            (*theme_api->g_gameLocked)
#define g_hasGameIcons          (*theme_api->g_hasGameIcons)
#define g_recent                (*theme_api->g_recent)
#define g_recentCount           (*theme_api->g_recentCount)
#define g_hero                  (*theme_api->g_hero)
#define g_heroDim               (*theme_api->g_heroDim)
#define g_heroReady             (*theme_api->g_heroReady)
#define g_trackerRunning        (*theme_api->g_trackerRunning)
#define g_patchedAllowed        (*theme_api->g_patchedAllowed)
#define g_popup                 (*theme_api->g_popup)
#define g_popupResult           (*theme_api->g_popupResult)
#define g_tabs                  (*theme_api->g_tabs)
#define g_tabPressed            (*theme_api->g_tabPressed)

// The theme's description (read without running it), the api it was handed, and its entry
// point, which refuses an app older than the theme.
#ifdef __3DS__
// (Right after the file's header: app/themes/theme.ld.)
#define THEME_INFO_SECTION      __attribute__((section(".theme_info"), used))
#else
#define THEME_INFO_SECTION
#endif
#define THEME_MODULE_DEFINE(theme, id, name) \
    const ThemeApi *theme_api; \
    THEME_INFO_SECTION const ThemeInfo theme_info = { THEME_INFO_MAGIC, THEME_API_VERSION, id, name }; \
    __attribute__((visibility("default"))) const Theme *ThemeModule_Entry(const ThemeApi *api) \
    { \
        if (api == NULL || api->version < theme_info.apiVersion || api->size < sizeof(ThemeApi)) \
            return NULL; \
        theme_api = api; \
        return &theme; \
    }

#else

#define THEME_MODULE_DEFINE(theme, id, name)

#endif

#endif
