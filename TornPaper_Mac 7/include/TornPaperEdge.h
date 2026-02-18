/*
    TornPaperEdge.h - Version 19
    
    Realistic torn paper with fold marks
    SmartRender implementation
*/

#pragma once

#ifndef TORNPAPEREDGE_H
#define TORNPAPEREDGE_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include "Smart_Utils.h"

#ifdef AE_OS_WIN
    #include <Windows.h>
#endif

#define MAJOR_VERSION       1
#define MINOR_VERSION       0
#define BUG_VERSION         0
#define STAGE_VERSION       PF_Stage_DEVELOP
#define BUILD_VERSION       1

#define NAME                "Torn Paper"
#define DESCRIPTION         "Realistic torn paper with fold marks"
#define MATCH_NAME          "TORN_PAPER"
#define CATEGORY            "Stylize"

// Buffer expansion for fibers extending beyond original alpha
#define MAX_EXPAND_PIXELS   100

enum {
    PARAM_INPUT = 0,
    
    // Basic Settings
    PARAM_TOPIC_BASIC,
    PARAM_MASTER_SCALE,
    PARAM_GAP_WIDTH,
    PARAM_RANDOM_SEED,
    PARAM_EDGE_SOFTNESS,
    PARAM_TOPIC_BASIC_END,
    
    // Edge Settings (contains Outer, Inner, Middle edges)
    PARAM_TOPIC_EDGE_SETTINGS,
    
    // Outer Edge
    PARAM_TOPIC_OUTER,
    PARAM_OUTER_ROUGHNESS,
    PARAM_OUTER_ROUGH_SCALE,
    PARAM_OUTER_JAGGEDNESS,
    PARAM_OUTER_NOTCH,
    PARAM_TOPIC_OUTER_END,
    
    // Inner Edge
    PARAM_TOPIC_INNER,
    PARAM_INNER_ROUGHNESS,
    PARAM_INNER_ROUGH_SCALE,
    PARAM_INNER_JAGGEDNESS,
    PARAM_INNER_NOTCH,
    PARAM_INNER_EXPANSION,
    PARAM_TOPIC_INNER_END,
    
    // Middle Edge 1
    PARAM_TOPIC_MIDDLE1,
    PARAM_MIDDLE1_AMOUNT,
    PARAM_MIDDLE1_POSITION,
    PARAM_MIDDLE1_ROUGHNESS,
    PARAM_MIDDLE1_SHADOW,
    PARAM_MIDDLE1_FIBER_DENSITY,
    PARAM_TOPIC_MIDDLE1_END,
    
    // Middle Edge 2
    PARAM_TOPIC_MIDDLE2,
    PARAM_MIDDLE2_AMOUNT,
    PARAM_MIDDLE2_POSITION,
    PARAM_MIDDLE2_ROUGHNESS,
    PARAM_MIDDLE2_SHADOW,
    PARAM_MIDDLE2_FIBER_DENSITY,
    PARAM_TOPIC_MIDDLE2_END,
    
    PARAM_TOPIC_EDGE_SETTINGS_END,
    
    // Paper Appearance (includes Fibers)
    PARAM_TOPIC_PAPER,
    PARAM_PAPER_TEXTURE,
    PARAM_SHADOW_AMOUNT,
    PARAM_SHADOW_WIDTH,
    PARAM_PAPER_COLOR,
    PARAM_FIBER_COLOR,
    PARAM_CONTENT_SHADOW_AMOUNT,
    PARAM_CONTENT_SHADOW_WIDTH,
    
    // Fibers (nested in Paper)
    PARAM_TOPIC_FIBERS,
    PARAM_FIBER_ENABLE,
    PARAM_FIBER_DENSITY,
    PARAM_FIBER_LENGTH,
    PARAM_FIBER_THICKNESS,
    PARAM_FIBER_SPREAD,
    PARAM_FIBER_SOFTNESS,
    PARAM_FIBER_FEATHER,
    PARAM_FIBER_RANGE,
    PARAM_FIBER_SHADOW,
    PARAM_FIBER_OPACITY,
    PARAM_FIBER_BLUR,
    PARAM_TOPIC_FIBERS_END,
    
    PARAM_TOPIC_PAPER_END,
    
    // Fold Mark
    PARAM_TOPIC_FOLD,
    PARAM_FOLD_AMOUNT,
    PARAM_FOLD_POINT1,
    PARAM_FOLD_POINT2,
    
    // Advanced Settings (nested in Fold)
    PARAM_TOPIC_FOLD_ADVANCED,
    PARAM_FOLD_LINE_ROUGHNESS,
    PARAM_FOLD_LINE_ROUGH_SCALE,
    PARAM_FOLD_LINE_WIDTH,
    PARAM_FOLD_SIDE_A_WIDTH,
    PARAM_FOLD_SIDE_A_ROUGHNESS,
    PARAM_FOLD_SIDE_A_ROUGH_SCALE,
    PARAM_FOLD_SIDE_A_JAGGEDNESS,
    PARAM_FOLD_SIDE_B_WIDTH,
    PARAM_FOLD_SIDE_B_ROUGHNESS,
    PARAM_FOLD_SIDE_B_ROUGH_SCALE,
    PARAM_FOLD_SIDE_B_JAGGEDNESS,
    PARAM_FOLD_CRACK_AMOUNT,
    PARAM_FOLD_CRACK_LENGTH,
    PARAM_FOLD_CRACK_LENGTH_VAR,
    PARAM_FOLD_CRACK_DENSITY,
    PARAM_FOLD_CRACK_BRANCHING,
    PARAM_FOLD_CRACK_ANGLE,
    PARAM_FOLD_CRACK_ANGLE_VAR,
    PARAM_FOLD_SHADOW_A_OPACITY,
    PARAM_FOLD_SHADOW_A_LENGTH,
    PARAM_FOLD_SHADOW_A_VARIABILITY,
    PARAM_FOLD_SHADOW_A_COLOR,
    PARAM_FOLD_SHADOW_B_OPACITY,
    PARAM_FOLD_SHADOW_B_LENGTH,
    PARAM_FOLD_SHADOW_B_VARIABILITY,
    PARAM_FOLD_SHADOW_B_COLOR,
    PARAM_TOPIC_FOLD_ADVANCED_END,
    
    PARAM_TOPIC_FOLD_END,
    
    // Grunge
    PARAM_TOPIC_GRUNGE,
    PARAM_DIRT_AMOUNT,
    PARAM_DIRT_SIZE,
    PARAM_DIRT_OPACITY,
    PARAM_DIRT_SEED,
    PARAM_DIRT_COLOR,
    PARAM_SMUDGE_AMOUNT,
    PARAM_SMUDGE_SIZE,
    PARAM_SMUDGE_OPACITY,
    PARAM_SMUDGE_SEED,
    PARAM_SMUDGE_COLOR,
    PARAM_DUST_AMOUNT,
    PARAM_DUST_SIZE,
    PARAM_DUST_SEED,
    PARAM_DUST_COLOR,
    PARAM_TOPIC_GRUNGE_END,
    
    PARAM_NUM_PARAMS
};

#ifdef __cplusplus
extern "C" {
#endif

DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr    inPtr,
    PF_PluginDataCB2    inPluginDataCallBackPtr,
    SPBasicSuite*       inSPBasicSuitePtr,
    const char*         inHostName,
    const char*         inHostVersion);

DllExport PF_Err EffectMain(
    PF_Cmd          cmd,
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output,
    void            *extra
);

#ifdef __cplusplus
}
#endif

// Standard callbacks
PF_Err About(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*);
PF_Err GlobalSetup(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*);
PF_Err ParamsSetup(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*);
PF_Err GlobalSetdown(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*);

// Legacy render (fallback)
PF_Err Render(PF_InData*, PF_OutData*, PF_ParamDef*[], PF_LayerDef*);

// SmartRender
PF_Err SmartPreRender(PF_InData*, PF_OutData*, PF_PreRenderExtra*);
PF_Err SmartRender(PF_InData*, PF_OutData*, PF_SmartRenderExtra*);

#endif
