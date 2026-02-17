/*
    TornPaperEdge.cpp - Version 19
    
    Realistic torn paper with fold marks that crack through the photo
    SmartRender implementation for multi-frame rendering
    - Edge Settings nested dropdown
    - Fibers nested in Paper Appearance
    - Fold Advanced Settings nested
    - Removed fiber color var, side softness controls
    - Added dirt/smudge seed controls, crack angle controls
    - v19: Fixed preview resolution scaling (downsample factor)
*/

#define NOMINMAX

#include "TornPaperEdge.h"
#include "NoiseUtils.h"
#include "AEFX_SuiteHelper.h"
#include <cmath>
#include <algorithm>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

template<typename T>
inline T safeMin(T a, T b) { return (a < b) ? a : b; }

template<typename T>
inline T safeMax(T a, T b) { return (a > b) ? a : b; }

inline double clamp01(double x) { return safeMax(0.0, safeMin(1.0, x)); }
inline double clamp(double x, double lo, double hi) { return safeMax(lo, safeMin(hi, x)); }

inline double smoothstep(double edge0, double edge1, double x) {
    double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

#ifdef AE_OS_WIN
    BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD dwReason, LPVOID lpReserved) {
        return TRUE;
    }
#endif

PF_Err EffectMain(
    PF_Cmd          cmd,
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output,
    void            *extra)
{
    PF_Err err = PF_Err_NONE;
    
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:
                err = About(in_data, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETUP:
                err = GlobalSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_GLOBAL_SETDOWN:
                err = GlobalSetdown(in_data, out_data, params, output);
                break;
            case PF_Cmd_PARAMS_SETUP:
                err = ParamsSetup(in_data, out_data, params, output);
                break;
            case PF_Cmd_RENDER:
                err = Render(in_data, out_data, params, output);
                break;
            case PF_Cmd_SMART_PRE_RENDER:
                err = SmartPreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra));
                break;
            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra));
                break;
            default:
                break;
        }
    } catch (PF_Err &thrown_err) {
        err = thrown_err;
    }
    
    return err;
}

PF_Err About(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    PF_SPRINTF(out_data->return_msg,
               "%s v%d.%d\r%s",
               NAME,
               MAJOR_VERSION,
               MINOR_VERSION,
               DESCRIPTION);
    
    return PF_Err_NONE;
}

PF_Err GlobalSetup(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    
    // Flags from AE_Effect.h:
    // PF_OutFlag_DEEP_COLOR_AWARE (1<<25) | PF_OutFlag_I_EXPAND_BUFFER (1<<9) | PF_OutFlag_PIX_INDEPENDENT (1<<10)
    out_data->out_flags = 33555968;  // 0x02000600
    
    // PF_OutFlag2_SUPPORTS_SMART_RENDER (1<<10) | PF_OutFlag2_FLOAT_COLOR_AWARE (1<<12) | PF_OutFlag2_SUPPORTS_THREADED_RENDERING (1<<27)
    out_data->out_flags2 = 134222848;  // 0x08001400
    
    return PF_Err_NONE;
}

PF_Err GlobalSetdown(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    return PF_Err_NONE;
}

PF_Err ParamsSetup(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;
    
    // ==================== BASIC SETTINGS ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Basic Settings", PARAM_TOPIC_BASIC);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Master Scale", 10.0, 500.0, 10.0, 500.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MASTER_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Gap Width", -200.0, 500.0, -200.0, 300.0, -50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_GAP_WIDTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Random Seed", 0, 30000, 0, 30000, 12345, PARAM_RANDOM_SEED);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Edge Softness", 0.0, 10.0, 0.0, 10.0, 2.2,
        PF_Precision_TENTHS, 0, 0, PARAM_EDGE_SOFTNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_BASIC_END);
    
    // ==================== EDGE SETTINGS (wrapper) ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Edge Settings", PARAM_TOPIC_EDGE_SETTINGS);
    
    // ==================== OUTER EDGE ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Outer Edge", PARAM_TOPIC_OUTER);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Outer Roughness", 0.0, 100.0, 0.0, 100.0, 59.0,
        PF_Precision_TENTHS, 0, 0, PARAM_OUTER_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Outer Roughness Scale", 5.0, 300.0, 5.0, 300.0, 189.0,
        PF_Precision_TENTHS, 0, 0, PARAM_OUTER_ROUGH_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Outer Jaggedness", 0.0, 100.0, 0.0, 100.0, 8.0,
        PF_Precision_TENTHS, 0, 0, PARAM_OUTER_JAGGEDNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Outer Notch Depth", 0.0, 50.0, 0.0, 50.0, 2.0,
        PF_Precision_TENTHS, 0, 0, PARAM_OUTER_NOTCH);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_OUTER_END);
    
    // ==================== INNER EDGE ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Inner Edge", PARAM_TOPIC_INNER);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Roughness", 0.0, 100.0, 0.0, 100.0, 59.0,
        PF_Precision_TENTHS, 0, 0, PARAM_INNER_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Roughness Scale", 5.0, 300.0, 5.0, 300.0, 189.0,
        PF_Precision_TENTHS, 0, 0, PARAM_INNER_ROUGH_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Jaggedness", 0.0, 100.0, 0.0, 100.0, 8.0,
        PF_Precision_TENTHS, 0, 0, PARAM_INNER_JAGGEDNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Notch Depth", 0.0, 50.0, 0.0, 50.0, 2.0,
        PF_Precision_TENTHS, 0, 0, PARAM_INNER_NOTCH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Edge Expansion", 1.0, 500.0, 1.0, 500.0, 150.0,
        PF_Precision_TENTHS, 0, 0, PARAM_INNER_EXPANSION);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_INNER_END);
    
    // ==================== MIDDLE EDGE 1 ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Middle Edge 1", PARAM_TOPIC_MIDDLE1);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 1 Amount", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE1_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 1 Position", 0.0, 100.0, 0.0, 100.0, 15.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE1_POSITION);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 1 Roughness", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE1_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 1 Shadow", 0.0, 100.0, 0.0, 100.0, 40.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE1_SHADOW);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 1 Fiber Density", 0.0, 100.0, 0.0, 100.0, 40.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE1_FIBER_DENSITY);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_MIDDLE1_END);
    
    // ==================== MIDDLE EDGE 2 ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Middle Edge 2", PARAM_TOPIC_MIDDLE2);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 2 Amount", 0.0, 100.0, 0.0, 100.0, 48.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE2_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 2 Position", 0.0, 100.0, 0.0, 100.0, 25.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE2_POSITION);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 2 Roughness", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE2_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 2 Shadow", 0.0, 100.0, 0.0, 100.0, 30.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE2_SHADOW);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Middle 2 Fiber Density", 0.0, 100.0, 0.0, 100.0, 40.0,
        PF_Precision_TENTHS, 0, 0, PARAM_MIDDLE2_FIBER_DENSITY);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_MIDDLE2_END);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_EDGE_SETTINGS_END);
    
    // ==================== PAPER APPEARANCE ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Paper Appearance", PARAM_TOPIC_PAPER);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Paper Texture", 0.0, 100.0, 0.0, 100.0, 85.0,
        PF_Precision_TENTHS, 0, 0, PARAM_PAPER_TEXTURE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow Amount", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_SHADOW_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow Width", 1.0, 50.0, 1.0, 50.0, 28.9,
        PF_Precision_TENTHS, 0, 0, PARAM_SHADOW_WIDTH);
    
    // Paper Color - #efe6d9
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Paper Color", 239, 230, 217, PARAM_PAPER_COLOR);
    
    // Fiber Color - #89837a
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Fiber Color", 137, 131, 122, PARAM_FIBER_COLOR);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Content Shadow Amount", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_CONTENT_SHADOW_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Content Shadow Width", 1.0, 50.0, 1.0, 50.0, 15.0,
        PF_Precision_TENTHS, 0, 0, PARAM_CONTENT_SHADOW_WIDTH);
    
    // ==================== FIBERS (nested in Paper) ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Fibers", PARAM_TOPIC_FIBERS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Density", 0.0, 100.0, 0.0, 100.0, 28.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_DENSITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Length", 1.0, 80.0, 1.0, 80.0, 18.8,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_LENGTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Thickness", 0.1, 5.0, 0.1, 5.0, 0.6,
        PF_Precision_HUNDREDTHS, 0, 0, PARAM_FIBER_THICKNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Spread", 0.0, 90.0, 0.0, 90.0, 60.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_SPREAD);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Softness", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_SOFTNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Feather", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_FEATHER);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Range", -100.0, 100.0, -100.0, 100.0, -100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_RANGE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Shadow", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_SHADOW);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Opacity", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_OPACITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fiber Blur", 0.0, 20.0, 0.0, 20.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FIBER_BLUR);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_FIBERS_END);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_PAPER_END);
    
    // ==================== FOLD MARK ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Fold Mark", PARAM_TOPIC_FOLD);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fold Amount", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_AMOUNT);
    
    // Fold Point 1
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Fold Point 1", 50, 50, 0, PARAM_FOLD_POINT1);
    
    // Fold Point 2
    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Fold Point 2", 50, 50, 0, PARAM_FOLD_POINT2);
    
    // ==================== ADVANCED SETTINGS (nested in Fold) ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Advanced Settings", PARAM_TOPIC_FOLD_ADVANCED);
    
    // Main fold line controls
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fold Line Roughness", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_LINE_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fold Line Rough Scale", 5.0, 200.0, 5.0, 200.0, 85.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_LINE_ROUGH_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Fold Line Width", 0.5, 10.0, 0.5, 10.0, 0.5,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_LINE_WIDTH);
    
    // Side A controls
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side A Width", 1.0, 50.0, 1.0, 50.0, 1.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_A_WIDTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side A Roughness", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_A_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side A Rough Scale", 5.0, 200.0, 5.0, 200.0, 200.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_A_ROUGH_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side A Jaggedness", 0.0, 100.0, 0.0, 100.0, 20.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_A_JAGGEDNESS);
    
    // Side B controls
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side B Width", 1.0, 50.0, 1.0, 50.0, 1.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_B_WIDTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side B Roughness", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_B_ROUGHNESS);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side B Rough Scale", 5.0, 200.0, 5.0, 200.0, 40.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_B_ROUGH_SCALE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Side B Jaggedness", 0.0, 100.0, 0.0, 100.0, 20.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SIDE_B_JAGGEDNESS);
    
    // Perpendicular crack lines
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Amount", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Length", 5.0, 800.0, 5.0, 800.0, 200.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_LENGTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Length Variability", 0.0, 100.0, 0.0, 100.0, 100.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_LENGTH_VAR);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Density", 0.0, 100.0, 0.0, 100.0, 5.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_DENSITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Branching", 0.0, 100.0, 0.0, 100.0, 22.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_BRANCHING);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Angle", 0.0, 90.0, 0.0, 90.0, 90.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_ANGLE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Crack Angle Variability", 0.0, 90.0, 0.0, 90.0, 20.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_CRACK_ANGLE_VAR);
    
    // Shadow A controls (side A - typically highlight/white)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow A Opacity", 0.0, 100.0, 0.0, 100.0, 10.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_A_OPACITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow A Length", 5.0, 300.0, 5.0, 300.0, 250.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_A_LENGTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow A Variability", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_A_VARIABILITY);
    
    // Shadow A Color - black
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Shadow A Color", 0, 0, 0, PARAM_FOLD_SHADOW_A_COLOR);
    
    // Shadow B controls (side B - typically shadow/black)
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow B Opacity", 0.0, 100.0, 0.0, 100.0, 10.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_B_OPACITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow B Length", 5.0, 300.0, 5.0, 300.0, 250.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_B_LENGTH);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shadow B Variability", 0.0, 100.0, 0.0, 100.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_FOLD_SHADOW_B_VARIABILITY);
    
    // Shadow B Color - black (shadow)
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Shadow B Color", 0, 0, 0, PARAM_FOLD_SHADOW_B_COLOR);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_FOLD_ADVANCED_END);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_FOLD_END);
    
    // ==================== GRUNGE ====================
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Grunge Effects", PARAM_TOPIC_GRUNGE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dirt Amount", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_DIRT_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dirt Size", 1.0, 50.0, 1.0, 50.0, 10.0,
        PF_Precision_TENTHS, 0, 0, PARAM_DIRT_SIZE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dirt Opacity", 0.0, 100.0, 0.0, 100.0, 40.0,
        PF_Precision_TENTHS, 0, 0, PARAM_DIRT_OPACITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Dirt Seed", 0, 30000, 0, 30000, 5000, PARAM_DIRT_SEED);
    
    // Dirt Color - brownish
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Dirt Color", 80, 60, 40, PARAM_DIRT_COLOR);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smudge Amount", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_SMUDGE_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smudge Size", 10.0, 200.0, 10.0, 200.0, 50.0,
        PF_Precision_TENTHS, 0, 0, PARAM_SMUDGE_SIZE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smudge Opacity", 0.0, 100.0, 0.0, 100.0, 20.0,
        PF_Precision_TENTHS, 0, 0, PARAM_SMUDGE_OPACITY);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Smudge Seed", 0, 30000, 0, 30000, 8000, PARAM_SMUDGE_SEED);
    
    // Smudge Color - grayish
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Smudge Color", 100, 95, 85, PARAM_SMUDGE_COLOR);
    
    // Dust particles
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dust Amount", 0.0, 100.0, 0.0, 100.0, 0.0,
        PF_Precision_TENTHS, 0, 0, PARAM_DUST_AMOUNT);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Dust Size", 0.5, 10.0, 0.5, 10.0, 2.0,
        PF_Precision_TENTHS, 0, 0, PARAM_DUST_SIZE);
    
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Dust Seed", 0, 30000, 0, 30000, 9999, PARAM_DUST_SEED);
    
    // Dust Color - white
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Dust Color", 255, 255, 255, PARAM_DUST_COLOR);
    
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(PARAM_TOPIC_GRUNGE_END);
    
    out_data->num_params = PARAM_NUM_PARAMS;
    
    return err;
}

// ============================================================
// DISTANCE FIELD
// ============================================================

class DistanceField {
public:
    std::vector<float> distances;
    std::vector<float> gradX, gradY;
    int width, height;
    
    DistanceField(int w, int h) : width(w), height(h), 
        distances(w * h, 1e10f), gradX(w * h, 0.0f), gradY(w * h, 0.0f) {}
    
    float getDist(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return 1e10f;
        return distances[y * width + x];
    }
    
    void setDist(int x, int y, float d) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            distances[y * width + x] = d;
    }
    
    void getGradient(int x, int y, float& gx, float& gy) const {
        if (x < 0 || x >= width || y < 0 || y >= height) { gx = 0; gy = 0; return; }
        gx = gradX[y * width + x];
        gy = gradY[y * width + x];
    }
    
    // Helper to get alpha value normalized to 0.0-1.0 regardless of bit depth
    inline double getAlpha(PF_EffectWorld* layer, int x, int y, A_long pixelBytes) {
        if (x < 0 || x >= layer->width || y < 0 || y >= layer->height) return 0.0;
        
        if (pixelBytes >= 16) {
            // 32-bit float
            PF_PixelFloat* row = (PF_PixelFloat*)((char*)layer->data + y * layer->rowbytes);
            return row[x].alpha;
        } else if (pixelBytes >= 8) {
            // 16-bit
            PF_Pixel16* row = (PF_Pixel16*)((char*)layer->data + y * layer->rowbytes);
            return row[x].alpha / 32768.0;
        } else {
            // 8-bit
            PF_Pixel8* row = (PF_Pixel8*)((char*)layer->data + y * layer->rowbytes);
            return row[x].alpha / 255.0;
        }
    }
    
    void buildFromLayerGeneric(PF_EffectWorld* layer, A_long pixelBytes) {
        double threshold = 0.5;  // Alpha threshold for inside/outside
        
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                bool inside = getAlpha(layer, x, y, pixelBytes) > threshold;
                bool isEdge = false;
                
                if (x > 0 && (getAlpha(layer, x-1, y, pixelBytes) > threshold) != inside) isEdge = true;
                if (x < width-1 && (getAlpha(layer, x+1, y, pixelBytes) > threshold) != inside) isEdge = true;
                if (y > 0 && (getAlpha(layer, x, y-1, pixelBytes) > threshold) != inside) isEdge = true;
                if (y < height-1 && (getAlpha(layer, x, y+1, pixelBytes) > threshold) != inside) isEdge = true;
                
                setDist(x, y, isEdge ? 0.0f : (inside ? 1e10f : -1e10f));
            }
        }
        
        // Forward pass
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float current = getDist(x, y);
                float sign = current >= 0 ? 1.0f : -1.0f;
                float absCurrent = fabs(current);
                
                if (x > 0 && fabs(getDist(x-1, y)) + 1.0f < absCurrent) 
                    absCurrent = fabs(getDist(x-1, y)) + 1.0f;
                if (y > 0 && fabs(getDist(x, y-1)) + 1.0f < absCurrent) 
                    absCurrent = fabs(getDist(x, y-1)) + 1.0f;
                if (x > 0 && y > 0 && fabs(getDist(x-1, y-1)) + 1.414f < absCurrent)
                    absCurrent = fabs(getDist(x-1, y-1)) + 1.414f;
                if (x < width-1 && y > 0 && fabs(getDist(x+1, y-1)) + 1.414f < absCurrent)
                    absCurrent = fabs(getDist(x+1, y-1)) + 1.414f;
                
                setDist(x, y, absCurrent * sign);
            }
        }
        
        // Backward pass
        for (int y = height - 1; y >= 0; y--) {
            for (int x = width - 1; x >= 0; x--) {
                float current = getDist(x, y);
                float sign = current >= 0 ? 1.0f : -1.0f;
                float absCurrent = fabs(current);
                
                if (x < width-1 && fabs(getDist(x+1, y)) + 1.0f < absCurrent)
                    absCurrent = fabs(getDist(x+1, y)) + 1.0f;
                if (y < height-1 && fabs(getDist(x, y+1)) + 1.0f < absCurrent)
                    absCurrent = fabs(getDist(x, y+1)) + 1.0f;
                if (x < width-1 && y < height-1 && fabs(getDist(x+1, y+1)) + 1.414f < absCurrent)
                    absCurrent = fabs(getDist(x+1, y+1)) + 1.414f;
                if (x > 0 && y < height-1 && fabs(getDist(x-1, y+1)) + 1.414f < absCurrent)
                    absCurrent = fabs(getDist(x-1, y+1)) + 1.414f;
                
                setDist(x, y, absCurrent * sign);
            }
        }
        
        // Compute gradients
        for (int y = 1; y < height - 1; y++) {
            for (int x = 1; x < width - 1; x++) {
                float gx = getDist(x+1, y) - getDist(x-1, y);
                float gy = getDist(x, y+1) - getDist(x, y-1);
                float len = sqrt(gx*gx + gy*gy);
                if (len > 0.001f) { gx /= len; gy /= len; }
                gradX[y * width + x] = gx;
                gradY[y * width + x] = gy;
            }
        }
    }
    
    void buildFromLayer(PF_EffectWorld* layer) {
        // Default to 8-bit behavior for legacy Render function
        buildFromLayerGeneric(layer, 4);
    }
};

// ============================================================
// NOISE FUNCTIONS
// ============================================================

inline double worleyNoise(double x, double y, int seed) {
    int xi = (int)floor(x);
    int yi = (int)floor(y);
    double minDist = 1e10;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int cx = xi + dx;
            int cy = yi + dy;
            double px = cx + (double)(hash2D(cx, cy, seed) & 0xFFFF) / 65536.0;
            double py = cy + (double)(hash2D(cx, cy, seed + 1000) & 0xFFFF) / 65536.0;
            double dist = sqrt((x - px) * (x - px) + (y - py) * (y - py));
            if (dist < minDist) minDist = dist;
        }
    }
    return minDist;
}

inline double ridgedMultifractal(double x, double y, int seed, int octaves) {
    double sum = 0;
    double freq = 1.0;
    double amp = 1.0;
    double prev = 1.0;
    
    for (int i = 0; i < octaves; i++) {
        double n = valueNoise2D(x * freq, y * freq, seed + i * 100);
        n = 1.0 - fabs(n);
        n = n * n;
        sum += n * amp * prev;
        prev = n;
        freq *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

inline double calcEdgeDisplacement(double px, double py, int seed,
    double roughness, double roughScale, double jaggedness, double notchDepth, double scale)
{
    double disp = 0;
    double scaledRoughScale = roughScale * scale;
    
    if (roughness > 0) {
        double n = fbm2D(px / scaledRoughScale, py / scaledRoughScale, seed, 4, 0.5);
        disp += n * roughness * scale;
    }
    
    if (jaggedness > 0) {
        double jag = ridgedMultifractal(px / (20.0 * scale), py / (20.0 * scale), seed + 100, 4);
        disp += (jag - 0.5) * jaggedness * 0.8 * scale;
        
        double spike = valueNoise2D(px / (8.0 * scale), py / (8.0 * scale), seed + 300);
        spike = spike > 0.7 ? (spike - 0.7) * 3.0 : 0;
        disp += spike * jaggedness * 0.5 * scale;
    }
    
    if (notchDepth > 0) {
        double notch = worleyNoise(px / (40.0 * scale), py / (40.0 * scale), seed + 500);
        notch = notch < 0.3 ? (0.3 - notch) * notchDepth * scale : 0;
        disp += notch;
    }
    
    return disp;
}

// ============================================================
// FOLD MARK FUNCTIONS
// ============================================================

// Calculate distance from point to line segment
inline double pointToLineDistance(double px, double py, 
    double x1, double y1, double x2, double y2, double& alongLine)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double len = sqrt(dx*dx + dy*dy);
    
    if (len < 0.001) {
        alongLine = 0;
        return sqrt((px-x1)*(px-x1) + (py-y1)*(py-y1));
    }
    
    // Project point onto line
    double t = ((px - x1) * dx + (py - y1) * dy) / (len * len);
    alongLine = t;
    
    // Clamp to segment
    t = clamp(t, 0.0, 1.0);
    
    double closestX = x1 + t * dx;
    double closestY = y1 + t * dy;
    
    return sqrt((px - closestX)*(px - closestX) + (py - closestY)*(py - closestY));
}

// Generate jagged edge displacement for fold (similar to paper edge)
inline double foldEdgeDisplacement(double alongLine, double lineLen, int seed, 
    double roughness, double roughScale, double jaggedness, double scale)
{
    double disp = 0;
    double coord = alongLine * lineLen;
    double scaledRoughScale = roughScale * scale;
    
    // FBM roughness - larger waves
    if (roughness > 0) {
        double n = fbm2D(coord / scaledRoughScale, seed * 0.01, seed, 4, 0.5);
        disp += n * roughness * scale * 0.12;
    }
    
    // Jaggedness - sharp spikes and notches like torn paper
    if (jaggedness > 0) {
        // Ridged noise for sharp features
        double jag = ridgedMultifractal(coord / (8.0 * scale), seed * 0.01, seed + 200, 3);
        disp += (jag - 0.5) * jaggedness * scale * 0.1;
        
        // Random sharp spikes
        double spike = valueNoise2D(coord / (4.0 * scale), seed * 0.01, seed + 300);
        spike = spike > 0.75 ? (spike - 0.75) * 4.0 : 0;
        disp += spike * jaggedness * scale * 0.06;
        
        // Worley for notches
        double notch = worleyNoise(coord / (12.0 * scale), seed * 0.01, seed + 400);
        notch = notch < 0.2 ? (0.2 - notch) * jaggedness * scale * 0.08 : 0;
        disp += notch;
    }
    
    return disp;
}

// Calculate perpendicular crack lines with curves and branching
inline double perpendicularCracks(double px, double py, int seed,
    double x1, double y1, double x2, double y2,
    double alongLine, double perpDist, double lineLen,
    double crackLength, double crackLengthVar, double crackDensity, double crackBranching,
    double crackAngle, double crackAngleVar, double scale)
{
    if (crackDensity <= 0) return 0.0;
    
    double scaledLength = crackLength * scale;
    
    // Convert angles to radians
    double baseAngleRad = crackAngle * 3.14159 / 180.0;
    double angleVarRad = crackAngleVar * 3.14159 / 180.0;
    
    // Cell-based crack generation
    double cellSize = 8.0 / (crackDensity / 50.0 + 0.5);
    double coordAlongLine = alongLine * lineLen;
    
    int cellIdx = (int)floor(coordAlongLine / cellSize);
    
    double crackStrength = 0;
    
    // Check nearby cells for cracks
    for (int ci = cellIdx - 3; ci <= cellIdx + 3; ci++) {
        uint32_t cellHash = hash(ci * 7919 + seed);
        
        // Probability of crack in this cell
        double prob = (cellHash & 0xFF) / 255.0;
        if (prob > crackDensity / 100.0) continue;
        
        // Crack origin position along the fold line
        double crackOrigin = (ci + (double)((cellHash >> 8) & 0xFF) / 255.0) * cellSize;
        double distFromCrackOrigin = coordAlongLine - crackOrigin;
        
        // Which side does this crack go?
        bool crackOnSideA = ((cellHash >> 16) & 1) == 0;
        
        // Crack only extends on one side
        if ((crackOnSideA && perpDist < 0) || (!crackOnSideA && perpDist > 0)) continue;
        
        double absPerpDist = fabs(perpDist);
        
        // This crack's length with variability control
        double lengthRandom = ((cellHash >> 20) & 0xFF) / 255.0;
        double minLength = 1.0 - crackLengthVar * 0.8;  // At 100% var, min is 20% of max
        double thisCrackLen = scaledLength * (minLength + lengthRandom * crackLengthVar * 0.8);
        
        if (absPerpDist > thisCrackLen * 1.2) continue;
        
        // Calculate crack angle with variability
        double angleVariation = (((cellHash >> 4) & 0xFF) / 255.0 - 0.5) * 2.0 * angleVarRad;
        double thisCrackAngle = baseAngleRad + angleVariation;
        
        // === MAIN CRACK with angle and curves ===
        double curveAmount = ((cellHash >> 12) & 0xFF) / 255.0 * 0.5 + 0.2;
        double curveFreq = 0.03 + ((cellHash >> 4) & 0xFF) / 255.0 * 0.02;
        
        // Calculate expected position along the angled crack line
        // At perpendicular (90 deg), crack goes straight out
        // At other angles, crack goes diagonally
        double expectedAlongOffset = absPerpDist / tan(thisCrackAngle);
        if (thisCrackAngle <= 0.1) expectedAlongOffset = 0;  // Avoid division issues
        
        // Cumulative curve offset
        double curveOffset = 0;
        double curveStep = absPerpDist / 10.0;
        for (double d = 0; d < absPerpDist; d += curveStep) {
            double noiseVal = fbm2D(d * curveFreq + cellHash * 0.001, cellHash * 0.0001, cellHash, 2, 0.5);
            curveOffset += noiseVal * curveAmount * curveStep;
        }
        
        // Crack width tapers as it extends outward
        double t = absPerpDist / thisCrackLen;
        double crackWidth = (1.8 - t * 1.5) * scale;
        if (crackWidth < 0.3 * scale) crackWidth = 0.3 * scale;
        
        double distFromCrackLine = fabs(distFromCrackOrigin - expectedAlongOffset - curveOffset);
        
        if (distFromCrackLine < crackWidth && t < 1.0) {
            double crackProfile = 1.0 - smoothstep(crackWidth * 0.2, crackWidth, distFromCrackLine);
            crackProfile *= 1.0 - smoothstep(0.7, 1.0, t);
            double erratic = valueNoise2D(absPerpDist * 0.2, cellHash * 0.01, cellHash + 500);
            erratic = erratic * 0.3 + 0.7;
            crackProfile *= erratic;
            crackStrength = safeMax(crackStrength, crackProfile);
        }
        
        // === BRANCH CRACKS (controlled by branching parameter) ===
        if (crackBranching > 0) {
            // Number of branches based on branching amount
            int maxBranches = (int)(crackBranching * 3.0) + 1;  // 1-4 based on parameter
            double branchProb = crackBranching;
            
            for (int bi = 0; bi < maxBranches; bi++) {
                uint32_t branchHash = hash(cellHash + bi * 1337);
                
                // Each branch has probability based on branching parameter
                double thisBranchProb = (branchHash & 0xFF) / 255.0;
                if (thisBranchProb > branchProb) continue;
                
                // Branch starts partway along main crack
                double branchStart = 0.2 + ((branchHash >> 8) & 0xFF) / 255.0 * 0.5;
                double branchStartDist = branchStart * thisCrackLen;
                
                if (absPerpDist < branchStartDist) continue;
                
                // Branch angle (30-60 degrees from main crack direction)
                double branchAngle = (30.0 + ((branchHash >> 16) & 0xFF) / 255.0 * 30.0) * 3.14159 / 180.0;
                if ((branchHash >> 24) & 1) branchAngle = -branchAngle;
                
                // Branch length (shorter than main crack)
                double branchLen = thisCrackLen * (0.15 + ((branchHash >> 20) & 0xFF) / 255.0 * 0.25);
                
                // Calculate the main crack's position at the branch start point
                // This includes BOTH the angle offset AND the curve offset at that distance
                double angleOffsetAtBranchStart = branchStartDist / tan(thisCrackAngle);
                if (thisCrackAngle <= 0.1) angleOffsetAtBranchStart = 0;
                
                double curveOffsetAtBranchStart = 0;
                double bStep = branchStartDist / 10.0;
                for (double d = 0; d < branchStartDist; d += bStep) {
                    double noiseVal = fbm2D(d * curveFreq + cellHash * 0.001, cellHash * 0.0001, cellHash, 2, 0.5);
                    curveOffsetAtBranchStart += noiseVal * curveAmount * bStep;
                }
                
                // Branch origin: crack origin + angle offset + curve offset at branch start
                double branchOriginX = crackOrigin + angleOffsetAtBranchStart + curveOffsetAtBranchStart;
                
                // Distance along branch
                double branchDist = absPerpDist - branchStartDist;
                if (branchDist < 0 || branchDist > branchLen) continue;
                
                // Expected position on branch line (branch goes at an angle from the main crack)
                double expectedX = branchOriginX + sin(branchAngle) * branchDist;
                
                // Add curve to branch
                double branchCurve = fbm2D(branchDist * 0.05, branchHash * 0.001, branchHash, 2, 0.5);
                expectedX += branchCurve * branchLen * 0.15;
                
                double distFromBranch = fabs(coordAlongLine - expectedX);
                
                double branchT = branchDist / branchLen;
                double branchWidth = (1.0 - branchT * 0.8) * scale;
                if (branchWidth < 0.2 * scale) branchWidth = 0.2 * scale;
                
                if (distFromBranch < branchWidth) {
                    double branchProfile = 1.0 - smoothstep(branchWidth * 0.2, branchWidth, distFromBranch);
                    branchProfile *= 1.0 - smoothstep(0.6, 1.0, branchT);
                    branchProfile *= 0.6;  // Branches less intense
                    crackStrength = safeMax(crackStrength, branchProfile);
                }
            }
        }
    }
    
    return crackStrength;
}

// Generate fold crease effect with thin erratic lines and perpendicular cracks
inline void foldCrease(double px, double py, int seed,
    double x1, double y1, double x2, double y2,
    double lineRoughness, double lineRoughScale, double lineWidth,
    double sideAWidth, double sideARoughness, double sideARoughScale, double sideAJagged, double sideASoftness,
    double sideBWidth, double sideBRoughness, double sideBRoughScale, double sideBJagged, double sideBSoftness,
    double crackAmount, double crackLength, double crackLengthVar, double crackDensity, double crackBranching,
    double crackAngle, double crackAngleVar,
    double shadowAOpacity, double shadowALength, double shadowAVariability,
    double shadowBOpacity, double shadowBLength, double shadowBVariability,
    double scale,
    double& crackStrength, double& shadowAStrength, double& shadowBStrength)
{
    crackStrength = 0;
    shadowAStrength = 0;
    shadowBStrength = 0;
    
    // Line vector
    double dx = x2 - x1;
    double dy = y2 - y1;
    double lineLen = sqrt(dx*dx + dy*dy);
    if (lineLen < 1.0) return;
    
    // Normalize
    double ndx = dx / lineLen;
    double ndy = dy / lineLen;
    
    // Vector from p1 to current point
    double toPx = px - x1;
    double toPy = py - y1;
    
    // Project onto line to get position along line (0 to 1)
    double alongLine = (toPx * ndx + toPy * ndy) / lineLen;
    
    // Perpendicular distance (positive = side A, negative = side B)
    double perpDist = toPx * (-ndy) + toPy * ndx;
    
    // Add imperfections to the fold line itself - erratic wobble
    double lineWobble = 0;
    if (lineRoughness > 0) {
        double scaledLineRoughScale = lineRoughScale * scale;
        double coord = alongLine * lineLen;
        lineWobble = fbm2D(coord / scaledLineRoughScale, seed * 0.01, seed + 1000, 3, 0.6);
        lineWobble += valueNoise2D(coord / (scaledLineRoughScale * 0.3), seed * 0.01, seed + 1100) * 0.4;
        double sharpTurn = valueNoise2D(coord / (scaledLineRoughScale * 0.5), seed * 0.02, seed + 1200);
        sharpTurn = sharpTurn > 0.7 ? (sharpTurn - 0.7) * 3.0 : (sharpTurn < 0.3 ? (0.3 - sharpTurn) * -3.0 : 0);
        lineWobble += sharpTurn * 0.3;
        lineWobble *= lineRoughness * scale * 0.06;
    }
    double adjustedPerpDist = perpDist - lineWobble;
    
    // Determine which side we're on
    bool isSideA = adjustedPerpDist > 0;
    double absDist = fabs(adjustedPerpDist);
    
    // Get parameters for current side
    double sideWidth, sideRoughness, sideRoughScale, sideJagged, sideSoftness;
    int sideSeed;
    if (isSideA) {
        sideWidth = sideAWidth * scale;
        sideRoughness = sideARoughness;
        sideRoughScale = sideARoughScale;
        sideJagged = sideAJagged;
        sideSoftness = sideASoftness;
        sideSeed = seed + 2000;
    } else {
        sideWidth = sideBWidth * scale;
        sideRoughness = sideBRoughness;
        sideRoughScale = sideBRoughScale;
        sideJagged = sideBJagged;
        sideSoftness = sideBSoftness;
        sideSeed = seed + 3000;
    }
    
    // Early exit if too far from fold
    double maxDist = safeMax(sideAWidth + shadowALength, sideBWidth + shadowBLength) * scale * 1.2;
    if (absDist > maxDist) return;
    
    // Fade out at ends of fold line
    double endFade = 1.0;
    if (alongLine < 0) {
        endFade = smoothstep(-0.1, 0.02, alongLine);
    } else if (alongLine > 1.0) {
        endFade = smoothstep(1.1, 0.98, alongLine);
    }
    
    // === MAIN FOLD LINE (thin, erratic) ===
    double scaledLineWidth = lineWidth * scale;
    double mainLineDist = fabs(adjustedPerpDist);
    double mainLineStrength = 0;
    if (mainLineDist < scaledLineWidth) {
        mainLineStrength = 1.0 - smoothstep(scaledLineWidth * 0.2, scaledLineWidth, mainLineDist);
        double lineVar = fbm2D(alongLine * 20.0, seed * 0.1, seed + 1500, 2, 0.5);
        lineVar = lineVar * 0.4 + 0.6;
        mainLineStrength *= lineVar;
    }
    
    // === SIDE EDGE CRACKING ===
    double edgeDisp = foldEdgeDisplacement(alongLine, lineLen, sideSeed, sideRoughness, sideRoughScale, sideJagged, scale);
    double edgePos = sideWidth + edgeDisp;
    double distFromEdge = edgePos - absDist;
    
    double edgeCrackStrength = 0;
    if (distFromEdge > 0) {
        double softEdge = safeMax(0.3, sideSoftness * 2.0);
        edgeCrackStrength = smoothstep(-softEdge, softEdge * 0.5, distFromEdge);
        double crackVar = fbm2D(alongLine * 12.0 + absDist * 0.05, seed * 0.1, sideSeed + 500, 2, 0.5);
        crackVar = crackVar * 0.5 + 0.5;
        edgeCrackStrength *= crackVar;
    }
    
    // === PERPENDICULAR CRACKS ===
    double perpCrackStrength = 0;
    if (crackAmount > 0 && crackDensity > 0) {
        perpCrackStrength = perpendicularCracks(px, py, seed + 4000, x1, y1, x2, y2,
            alongLine, adjustedPerpDist, lineLen, crackLength, crackLengthVar, crackDensity, crackBranching,
            crackAngle, crackAngleVar, scale);
        perpCrackStrength *= crackAmount;
    }
    
    // Combine all crack elements
    crackStrength = safeMax(mainLineStrength, safeMax(edgeCrackStrength, perpCrackStrength));
    crackStrength *= endFade;
    
    // === SHADOW A (outside edge of side A) ===
    if (shadowAOpacity > 0 && isSideA) {
        double sideAEdgePos = sideAWidth * scale + foldEdgeDisplacement(alongLine, lineLen, seed + 2000, 
            sideARoughness, sideARoughScale, sideAJagged, scale);
        double distOutsideSideA = absDist - sideAEdgePos;
        
        if (distOutsideSideA > 0) {
            double shadowVar = 1.0;
            if (shadowAVariability > 0) {
                double varNoise = fbm2D(alongLine * 8.0, seed * 0.1, seed + 6000, 2, 0.5);
                varNoise = varNoise * 0.5 + 0.5;
                shadowVar = 1.0 - shadowAVariability * (1.0 - varNoise);
            }
            
            double effectiveShadowLen = shadowALength * scale * shadowVar;
            
            if (distOutsideSideA < effectiveShadowLen) {
                double shadowFalloff = 1.0 - (distOutsideSideA / effectiveShadowLen);
                shadowFalloff = shadowFalloff * shadowFalloff;
                shadowAStrength = shadowFalloff * shadowAOpacity * endFade;
            }
        }
    }
    
    // === SHADOW B (outside edge of side B) ===
    if (shadowBOpacity > 0 && !isSideA) {
        double sideBEdgePos = sideBWidth * scale + foldEdgeDisplacement(alongLine, lineLen, seed + 3000, 
            sideBRoughness, sideBRoughScale, sideBJagged, scale);
        double distOutsideSideB = absDist - sideBEdgePos;
        
        if (distOutsideSideB > 0) {
            double shadowVar = 1.0;
            if (shadowBVariability > 0) {
                double varNoise = fbm2D(alongLine * 8.0, seed * 0.1, seed + 7000, 2, 0.5);
                varNoise = varNoise * 0.5 + 0.5;
                shadowVar = 1.0 - shadowBVariability * (1.0 - varNoise);
            }
            
            double effectiveShadowLen = shadowBLength * scale * shadowVar;
            
            if (distOutsideSideB < effectiveShadowLen) {
                double shadowFalloff = 1.0 - (distOutsideSideB / effectiveShadowLen);
                shadowFalloff = shadowFalloff * shadowFalloff;
                shadowBStrength = shadowFalloff * shadowBOpacity * endFade;
            }
        }
    }
    
    crackStrength = clamp01(crackStrength);
    shadowAStrength = clamp01(shadowAStrength);
    shadowBStrength = clamp01(shadowBStrength);
}

// ============================================================
// GRUNGE FUNCTIONS
// ============================================================

inline double organicDirt(double x, double y, int seed, double size, double amount, double scale) {
    if (amount <= 0) return 0.0;
    
    double scaledSize = size * scale;
    
    double w1 = worleyNoise(x / scaledSize, y / scaledSize, seed);
    double w2 = worleyNoise(x / (scaledSize * 0.4), y / (scaledSize * 0.4), seed + 1000);
    double w3 = worleyNoise(x / (scaledSize * 0.15), y / (scaledSize * 0.15), seed + 2000);
    
    double shape = (1.0 - w1) * 0.5 + (1.0 - w2) * 0.3 + (1.0 - w3) * 0.2;
    
    double ridge = ridgedMultifractal(x / (scaledSize * 0.8), y / (scaledSize * 0.8), seed + 3000, 3);
    shape = shape * 0.6 + ridge * 0.4;
    
    double threshold = 0.75 - (amount * 0.005);
    shape = smoothstep(threshold, threshold + 0.15, shape);
    
    double dist = fbm2D(x * 0.002, y * 0.002, seed + 5000, 3, 0.6);
    double distThreshold = 0.7 - (amount * 0.006);
    dist = smoothstep(distThreshold, distThreshold + 0.2, dist);
    
    double speckle = worleyNoise(x / (scaledSize * 0.2), y / (scaledSize * 0.2), seed + 8000);
    speckle = speckle < 0.12 ? (0.12 - speckle) / 0.12 : 0.0;
    
    double dirt = shape * dist + speckle * dist * 0.6;
    
    return clamp01(dirt);
}

inline double organicSmudge(double x, double y, int seed, double size, double amount, double scale) {
    if (amount <= 0) return 0.0;
    
    double scaledSize = size * scale;
    
    double fbm1 = fbm2D(x / scaledSize, y / scaledSize, seed + 20000, 4, 0.5);
    double fbm2 = fbm2D(x / (scaledSize * 0.5), y / (scaledSize * 0.5), seed + 21000, 3, 0.6);
    
    double w1 = worleyNoise(x / (scaledSize * 1.5), y / (scaledSize * 1.5), seed + 22000);
    
    double shape = fbm1 * 0.5 + fbm2 * 0.3 + (1.0 - w1) * 0.2;
    
    double threshold = 0.7 - (amount * 0.006);
    shape = smoothstep(threshold, threshold + 0.2, shape);
    
    double dist = fbm2D(x * 0.001, y * 0.001, seed + 23000, 2, 0.7);
    double distThreshold = 0.8 - (amount * 0.007);
    dist = smoothstep(distThreshold, distThreshold + 0.15, dist);
    
    double angle = fbm2D(x * 0.005, y * 0.005, seed + 24000, 2, 0.5) * 6.28;
    double streak = sin(x * cos(angle) * 0.05 + y * sin(angle) * 0.05);
    streak = streak * 0.3 + 0.7;
    
    double smudge = shape * dist * streak;
    
    return clamp01(smudge);
}

// Dust particles - small, irregular, high-contrast specks scattered randomly
inline double dustParticles(double x, double y, int seed, double size, double amount, double scale) {
    if (amount <= 0) return 0.0;
    
    double scaledSize = size * scale;
    double dustStrength = 0;
    
    // Cell-based dust generation for truly random placement
    double cellSize = 15.0 / (amount / 30.0 + 0.5);
    
    int cellX = (int)floor(x / cellSize);
    int cellY = (int)floor(y / cellSize);
    
    // Check nearby cells
    for (int cy = cellY - 1; cy <= cellY + 1; cy++) {
        for (int cx = cellX - 1; cx <= cellX + 1; cx++) {
            uint32_t cellHash = hash2D(cx, cy, seed);
            
            // Multiple dust particles per cell based on amount
            int numParticles = 1 + (int)((cellHash & 0x3) * amount / 100.0);
            
            for (int pi = 0; pi < numParticles; pi++) {
                uint32_t particleHash = hash(cellHash + pi * 9973);
                
                // Probability check
                double prob = (particleHash & 0xFF) / 255.0;
                if (prob > amount / 100.0) continue;
                
                // Particle position within cell
                double px = cx * cellSize + ((particleHash >> 8) & 0xFFFF) / 65536.0 * cellSize;
                double py = cy * cellSize + ((particleHash >> 16) & 0xFFFF) / 65536.0 * cellSize;
                
                // Distance to this particle
                double dist = sqrt((x - px) * (x - px) + (y - py) * (y - py));
                
                // Particle size varies
                double thisSize = scaledSize * (0.3 + ((particleHash >> 4) & 0xFF) / 255.0 * 0.7);
                
                if (dist < thisSize) {
                    // Irregular shape using noise
                    double angle = atan2(y - py, x - px);
                    double irregularity = valueNoise2D(angle * 3.0, particleHash * 0.001, particleHash) * 0.4;
                    double adjustedSize = thisSize * (1.0 + irregularity);
                    
                    if (dist < adjustedSize) {
                        // Sharp, high-contrast particle
                        double particleProfile = 1.0 - smoothstep(adjustedSize * 0.5, adjustedSize, dist);
                        dustStrength = safeMax(dustStrength, particleProfile);
                    }
                }
            }
        }
    }
    
    return clamp01(dustStrength);
}

// ============================================================
// FIBER FUNCTIONS
// ============================================================

struct FiberResult {
    double opacity;
    double shadowOpacity;
    double colorVar;
    double distFromBase;
};

inline FiberResult fiberStrand(
    double px, double py,
    double fx, double fy,
    double angle,
    double length,
    double thickness,
    double softness,
    double feather,
    int seed)
{
    FiberResult result = {0, 0, 0.5, 0};
    
    double dx = px - fx;
    double dy = py - fy;
    
    double cosA = cos(-angle);
    double sinA = sin(-angle);
    double localX = dx * cosA - dy * sinA;
    double localY = dx * sinA + dy * cosA;
    
    if (localX < -2 || localX > length + 2) return result;
    
    double t = localX / length;
    result.distFromBase = localX;
    
    double taperThickness = thickness * (1.0 - t * t);
    
    double wave = sin(localX * 0.5 + seed * 0.1) * 0.5 * (1.0 - t);
    double adjustedLocalY = localY - wave;
    
    double dist = fabs(adjustedLocalY);
    
    if (dist > taperThickness * 2.0) return result;
    
    double softEdge = taperThickness * (0.3 + softness * 0.7);
    double hardEdge = taperThickness * (1.0 - softness * 0.5);
    
    double opacity = 1.0 - smoothstep(hardEdge, hardEdge + softEdge, dist);
    
    double featherStart = 0.4 - feather * 0.3;
    double featherEnd = 0.7 + feather * 0.3;
    opacity *= 1.0 - smoothstep(featherStart, featherEnd, t);
    
    if (localX < 0) opacity *= smoothstep(-2.0, 0.0, localX);
    
    double shadowLocalY = adjustedLocalY - thickness * 0.8;
    double shadowDist = fabs(shadowLocalY);
    double shadowOpacity = 1.0 - smoothstep(hardEdge * 1.2, hardEdge * 1.2 + softEdge * 1.5, shadowDist);
    shadowOpacity *= 1.0 - smoothstep(featherStart, featherEnd, t);
    if (localX < 0) shadowOpacity *= smoothstep(-2.0, 0.0, localX);
    
    result.opacity = clamp01(opacity);
    result.shadowOpacity = clamp01(shadowOpacity * 0.5);
    result.colorVar = (double)((hash(seed + (int)(localX * 10)) & 0xFF)) / 255.0;
    
    return result;
}

struct FiberFieldResult {
    double opacity;
    double shadowOpacity;
    double colorVar;
    double maxExtent;
};

inline FiberFieldResult fiberField(
    double px, double py,
    double edgeDist,
    float gradX, float gradY,
    double density,
    double length,
    double thickness,
    double spread,
    double softness,
    double feather,
    double range,
    int seed)
{
    FiberFieldResult result = {0, 0, 0.5, 0};
    
    if (density <= 0 || length <= 0) return result;
    
    double rangeMultiplier = 0.5 + (range / 100.0) * 1.0;
    double maxFiberDist = length * safeMax(0.1, rangeMultiplier);
    
    if (fabs(edgeDist) > maxFiberDist * 2.5) return result;
    
    double cellSize = 4.0 / (density / 50.0 + 0.5);
    
    int cellX = (int)floor(px / cellSize);
    int cellY = (int)floor(py / cellSize);
    
    double maxExtent = 0;
    
    for (int cy = cellY - 4; cy <= cellY + 4; cy++) {
        for (int cx = cellX - 4; cx <= cellX + 4; cx++) {
            uint32_t cellHash = hash2D(cx, cy, seed);
            
            double prob = (cellHash & 0xFF) / 255.0;
            if (prob > density / 100.0) continue;
            
            double basePosNoise = (double)((cellHash >> 8) & 0xFFFF) / 65536.0;
            double fx = cx * cellSize + basePosNoise * cellSize;
            double fy = cy * cellSize + ((cellHash >> 16) & 0xFFFF) / 65536.0 * cellSize;
            
            double angleNoise = ((double)((cellHash >> 4) & 0xFFF) / 4096.0 - 0.5) * 2.0;
            double baseAngle = atan2(-gradY, -gradX);
            double angle = baseAngle + angleNoise * spread * 3.14159 / 180.0;
            
            double lenVar = 0.5 + ((cellHash >> 20) & 0xFF) / 255.0;
            double fiberLen = length * lenVar;
            
            double thickVar = 0.7 + ((cellHash >> 12) & 0xFF) / 255.0 * 0.6;
            double fiberThick = thickness * thickVar;
            
            FiberResult fr = fiberStrand(px, py, fx, fy, angle, fiberLen, fiberThick, 
                                         softness, feather, cellHash);
            
            if (fr.opacity > result.opacity) {
                result.opacity = fr.opacity;
                result.colorVar = fr.colorVar;
            }
            if (fr.shadowOpacity > result.shadowOpacity) {
                result.shadowOpacity = fr.shadowOpacity;
            }
            
            if (fr.opacity > 0.1) {
                maxExtent = safeMax(maxExtent, fr.distFromBase);
            }
        }
    }
    
    result.opacity = clamp01(result.opacity);
    result.shadowOpacity = clamp01(result.shadowOpacity);
    result.maxExtent = maxExtent;
    
    return result;
}

// ============================================================
// MAIN RENDER
// ============================================================

PF_Err Render(
    PF_InData       *in_data,
    PF_OutData      *out_data,
    PF_ParamDef     *params[],
    PF_LayerDef     *output)
{
    PF_Err err = PF_Err_NONE;
    
    // Calculate downsample factor for preview resolution scaling
    double downsampleX = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
    double downsampleY = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;
    double downsampleFactor = (downsampleX + downsampleY) * 0.5;
    
    // Basic settings - DON'T scale masterScale, work in full-res space
    double masterScale = params[PARAM_MASTER_SCALE]->u.fs_d.value / 100.0;
    
    double gapWidth = params[PARAM_GAP_WIDTH]->u.fs_d.value * masterScale;
    int seed = params[PARAM_RANDOM_SEED]->u.sd.value;
    double edgeSoftness = params[PARAM_EDGE_SOFTNESS]->u.fs_d.value * masterScale;
    
    // Outer edge
    double outerRoughness = params[PARAM_OUTER_ROUGHNESS]->u.fs_d.value;
    double outerRoughScale = params[PARAM_OUTER_ROUGH_SCALE]->u.fs_d.value;
    double outerJaggedness = params[PARAM_OUTER_JAGGEDNESS]->u.fs_d.value;
    double outerNotch = params[PARAM_OUTER_NOTCH]->u.fs_d.value;
    
    // Inner edge
    double innerRoughness = params[PARAM_INNER_ROUGHNESS]->u.fs_d.value;
    double innerRoughScale = params[PARAM_INNER_ROUGH_SCALE]->u.fs_d.value;
    double innerJaggedness = params[PARAM_INNER_JAGGEDNESS]->u.fs_d.value;
    double innerNotch = params[PARAM_INNER_NOTCH]->u.fs_d.value;
    double innerExpansion = params[PARAM_INNER_EXPANSION]->u.fs_d.value;
    
    // Middle edges
    double middle1Amount = params[PARAM_MIDDLE1_AMOUNT]->u.fs_d.value / 100.0;
    double middle1Position = params[PARAM_MIDDLE1_POSITION]->u.fs_d.value / 100.0;
    double middle1Roughness = params[PARAM_MIDDLE1_ROUGHNESS]->u.fs_d.value;
    double middle1Shadow = params[PARAM_MIDDLE1_SHADOW]->u.fs_d.value / 100.0;
    double middle1FiberDensity = params[PARAM_MIDDLE1_FIBER_DENSITY]->u.fs_d.value;
    
    double middle2Amount = params[PARAM_MIDDLE2_AMOUNT]->u.fs_d.value / 100.0;
    double middle2Position = params[PARAM_MIDDLE2_POSITION]->u.fs_d.value / 100.0;
    double middle2Roughness = params[PARAM_MIDDLE2_ROUGHNESS]->u.fs_d.value;
    double middle2Shadow = params[PARAM_MIDDLE2_SHADOW]->u.fs_d.value / 100.0;
    double middle2FiberDensity = params[PARAM_MIDDLE2_FIBER_DENSITY]->u.fs_d.value;
    
    // Paper appearance
    double paperTexture = params[PARAM_PAPER_TEXTURE]->u.fs_d.value / 100.0;
    double shadowAmount = params[PARAM_SHADOW_AMOUNT]->u.fs_d.value / 100.0;
    double shadowWidth = params[PARAM_SHADOW_WIDTH]->u.fs_d.value * masterScale;
    
    PF_Pixel paperColor = params[PARAM_PAPER_COLOR]->u.cd.value;
    double paperBaseR = paperColor.red / 255.0;
    double paperBaseG = paperColor.green / 255.0;
    double paperBaseB = paperColor.blue / 255.0;
    
    PF_Pixel fiberColor = params[PARAM_FIBER_COLOR]->u.cd.value;
    double fiberBaseR = fiberColor.red / 255.0;
    double fiberBaseG = fiberColor.green / 255.0;
    double fiberBaseB = fiberColor.blue / 255.0;
    
    double innerShadowAmount = params[PARAM_CONTENT_SHADOW_AMOUNT]->u.fs_d.value / 100.0;
    double innerShadowWidth = params[PARAM_CONTENT_SHADOW_WIDTH]->u.fs_d.value * masterScale;
    
    // Fibers
    double fiberDensity = params[PARAM_FIBER_DENSITY]->u.fs_d.value;
    double fiberLength = params[PARAM_FIBER_LENGTH]->u.fs_d.value * masterScale;
    double fiberThickness = params[PARAM_FIBER_THICKNESS]->u.fs_d.value * masterScale;
    double fiberSpread = params[PARAM_FIBER_SPREAD]->u.fs_d.value;
    double fiberSoftness = params[PARAM_FIBER_SOFTNESS]->u.fs_d.value / 100.0;
    double fiberFeather = params[PARAM_FIBER_FEATHER]->u.fs_d.value / 100.0;
    double fiberRange = params[PARAM_FIBER_RANGE]->u.fs_d.value;
    double fiberShadow = params[PARAM_FIBER_SHADOW]->u.fs_d.value / 100.0;
    double fiberOpacity = params[PARAM_FIBER_OPACITY]->u.fs_d.value / 100.0;
    double fiberBlur = params[PARAM_FIBER_BLUR]->u.fs_d.value;
    double fiberColorVar = 0.30;  // Hardcoded, control removed
    
    // Fold mark - point values are in fixed point (16.16 format)
    double foldAmount = params[PARAM_FOLD_AMOUNT]->u.fs_d.value / 100.0;
    A_long fold1X = params[PARAM_FOLD_POINT1]->u.td.x_value;
    A_long fold1Y = params[PARAM_FOLD_POINT1]->u.td.y_value;
    A_long fold2X = params[PARAM_FOLD_POINT2]->u.td.x_value;
    A_long fold2Y = params[PARAM_FOLD_POINT2]->u.td.y_value;
    double foldLineRoughness = params[PARAM_FOLD_LINE_ROUGHNESS]->u.fs_d.value;
    double foldLineRoughScale = params[PARAM_FOLD_LINE_ROUGH_SCALE]->u.fs_d.value;
    double foldLineWidth = params[PARAM_FOLD_LINE_WIDTH]->u.fs_d.value;
    double foldSideAWidth = params[PARAM_FOLD_SIDE_A_WIDTH]->u.fs_d.value;
    double foldSideARoughness = params[PARAM_FOLD_SIDE_A_ROUGHNESS]->u.fs_d.value;
    double foldSideARoughScale = params[PARAM_FOLD_SIDE_A_ROUGH_SCALE]->u.fs_d.value;
    double foldSideAJagged = params[PARAM_FOLD_SIDE_A_JAGGEDNESS]->u.fs_d.value;
    double foldSideASoftness = 0.0;  // Hardcoded, control removed
    double foldSideBWidth = params[PARAM_FOLD_SIDE_B_WIDTH]->u.fs_d.value;
    double foldSideBRoughness = params[PARAM_FOLD_SIDE_B_ROUGHNESS]->u.fs_d.value;
    double foldSideBRoughScale = params[PARAM_FOLD_SIDE_B_ROUGH_SCALE]->u.fs_d.value;
    double foldSideBJagged = params[PARAM_FOLD_SIDE_B_JAGGEDNESS]->u.fs_d.value;
    double foldSideBSoftness = 0.0;  // Hardcoded, control removed
    double foldCrackAmount = params[PARAM_FOLD_CRACK_AMOUNT]->u.fs_d.value / 100.0;
    double foldCrackLength = params[PARAM_FOLD_CRACK_LENGTH]->u.fs_d.value;
    double foldCrackLengthVar = params[PARAM_FOLD_CRACK_LENGTH_VAR]->u.fs_d.value / 100.0;
    double foldCrackDensity = params[PARAM_FOLD_CRACK_DENSITY]->u.fs_d.value;
    double foldCrackBranching = params[PARAM_FOLD_CRACK_BRANCHING]->u.fs_d.value / 100.0;
    double foldCrackAngle = params[PARAM_FOLD_CRACK_ANGLE]->u.fs_d.value;
    double foldCrackAngleVar = params[PARAM_FOLD_CRACK_ANGLE_VAR]->u.fs_d.value;
    double foldShadowAOpacity = params[PARAM_FOLD_SHADOW_A_OPACITY]->u.fs_d.value / 100.0;
    double foldShadowALength = params[PARAM_FOLD_SHADOW_A_LENGTH]->u.fs_d.value;
    double foldShadowAVariability = params[PARAM_FOLD_SHADOW_A_VARIABILITY]->u.fs_d.value / 100.0;
    PF_Pixel foldShadowAColor = params[PARAM_FOLD_SHADOW_A_COLOR]->u.cd.value;
    double foldShadowBOpacity = params[PARAM_FOLD_SHADOW_B_OPACITY]->u.fs_d.value / 100.0;
    double foldShadowBLength = params[PARAM_FOLD_SHADOW_B_LENGTH]->u.fs_d.value;
    double foldShadowBVariability = params[PARAM_FOLD_SHADOW_B_VARIABILITY]->u.fs_d.value / 100.0;
    PF_Pixel foldShadowBColor = params[PARAM_FOLD_SHADOW_B_COLOR]->u.cd.value;
    
    // Grunge
    double dirtAmount = params[PARAM_DIRT_AMOUNT]->u.fs_d.value;
    double dirtSize = params[PARAM_DIRT_SIZE]->u.fs_d.value;
    double dirtOpacity = params[PARAM_DIRT_OPACITY]->u.fs_d.value / 100.0;
    int dirtSeed = params[PARAM_DIRT_SEED]->u.sd.value;
    PF_Pixel dirtColor = params[PARAM_DIRT_COLOR]->u.cd.value;
    double dirtR = dirtColor.red / 255.0;
    double dirtG = dirtColor.green / 255.0;
    double dirtB = dirtColor.blue / 255.0;
    
    double smudgeAmount = params[PARAM_SMUDGE_AMOUNT]->u.fs_d.value;
    double smudgeSize = params[PARAM_SMUDGE_SIZE]->u.fs_d.value;
    double smudgeOpacity = params[PARAM_SMUDGE_OPACITY]->u.fs_d.value / 100.0;
    int smudgeSeed = params[PARAM_SMUDGE_SEED]->u.sd.value;
    PF_Pixel smudgeColor = params[PARAM_SMUDGE_COLOR]->u.cd.value;
    double smudgeR = smudgeColor.red / 255.0;
    double smudgeG = smudgeColor.green / 255.0;
    double smudgeB = smudgeColor.blue / 255.0;
    
    double dustAmount = params[PARAM_DUST_AMOUNT]->u.fs_d.value;
    double dustSize = params[PARAM_DUST_SIZE]->u.fs_d.value;
    int dustSeed = params[PARAM_DUST_SEED]->u.sd.value;
    PF_Pixel dustColor = params[PARAM_DUST_COLOR]->u.cd.value;
    double dustR = dustColor.red / 255.0;
    double dustG = dustColor.green / 255.0;
    double dustB = dustColor.blue / 255.0;
    
    PF_EffectWorld* input = &params[PARAM_INPUT]->u.ld;
    
    int width = output->width;
    int height = output->height;
    
    // Convert fold points from fixed point to pixels, scaled by downsample factor
    // Fixed point format: upper 16 bits = integer, lower 16 bits = fraction
    double fp1x = (double)fold1X / 65536.0;
    double fp1y = (double)fold1Y / 65536.0;
    double fp2x = (double)fold2X / 65536.0;
    double fp2y = (double)fold2Y / 65536.0;
    
    // Build distance field
    DistanceField df(width, height);
    df.buildFromLayer(input);
    
    // Render
    for (int y = 0; y < height; y++) {
        PF_Pixel8* inRow = (PF_Pixel8*)((char*)input->data + y * input->rowbytes);
        PF_Pixel8* outRow = (PF_Pixel8*)((char*)output->data + y * output->rowbytes);
        
        for (int x = 0; x < width; x++) {
            double px = (double)x;
            double py = (double)y;
            
            // Scale coordinates to full-resolution space for consistent noise sampling
            double noisePx = px / downsampleFactor;
            double noisePy = py / downsampleFactor;
            
            // signedDist is in canvas pixels - scale to full-res space
            float signedDistRaw = df.getDist(x, y);
            double signedDist = signedDistRaw / downsampleFactor;
            float gradX, gradY;
            df.getGradient(x, y, gradX, gradY);
            
            // Source pixel
            double srcR = inRow[x].red / 255.0;
            double srcG = inRow[x].green / 255.0;
            double srcB = inRow[x].blue / 255.0;
            double srcA = inRow[x].alpha / 255.0;
            
            // Edge displacements - use noise coordinates
            double outerDisp = calcEdgeDisplacement(noisePx, noisePy, seed, 
                outerRoughness, outerRoughScale, outerJaggedness, outerNotch, masterScale);
            double innerDispRaw = calcEdgeDisplacement(noisePx + 1000, noisePy + 1000, seed + 5000,
                innerRoughness, innerRoughScale, innerJaggedness, innerNotch, masterScale);
            // Shift inner edge based on expansion control
            double expansionFactor = (100.0 - innerExpansion) / 50.0;
            double innerDispMaxEstimate = (innerRoughness + innerJaggedness * 0.5 + innerNotch * 0.3) * masterScale * expansionFactor;
            double innerDisp = innerDispRaw - innerDispMaxEstimate;
            
            double halfGap = gapWidth / 2.0;
            double outerEdge = -halfGap + outerDisp;
            double innerEdge = halfGap + innerDisp;
            
            if (innerEdge < outerEdge + 2.0) {
                double mid = (innerEdge + outerEdge) / 2.0;
                innerEdge = mid + 1.0;
                outerEdge = mid - 1.0;
            }
            
            // Middle edges
            double middle1Edge = outerEdge;
            double middle2Edge = outerEdge;
            
            if (middle1Amount > 0) {
                double m1Disp = calcEdgeDisplacement(noisePx + 2000, noisePy + 2000, seed + 10000,
                    middle1Roughness, 100.0, middle1Roughness * 0.2, 0, masterScale);
                double m1Base = outerEdge + (innerEdge - outerEdge) * middle1Position;
                middle1Edge = m1Base + m1Disp * 0.4;
                middle1Edge = clamp(middle1Edge, outerEdge + 1.0, innerEdge - 1.0);
            }
            
            if (middle2Amount > 0) {
                double m2Disp = calcEdgeDisplacement(noisePx + 3000, noisePy + 3000, seed + 15000,
                    middle2Roughness, 100.0, middle2Roughness * 0.2, 0, masterScale);
                double m2Base = outerEdge + (innerEdge - outerEdge) * middle2Position;
                middle2Edge = m2Base + m2Disp * 0.4;
                middle2Edge = clamp(middle2Edge, outerEdge + 1.0, innerEdge - 1.0);
            }
            
            // Alphas
            double softness = safeMax(0.5, edgeSoftness);
            double contentAlpha = smoothstep(innerEdge - softness, innerEdge + softness, signedDist);
            
            double paperAlpha = 0.0;
            if (signedDist <= outerEdge - softness) {
                paperAlpha = 0.0;
            } else if (signedDist >= innerEdge + softness) {
                paperAlpha = 0.0;
            } else if (signedDist > outerEdge + softness && signedDist < innerEdge - softness) {
                paperAlpha = 1.0;
            } else if (signedDist <= outerEdge + softness) {
                paperAlpha = smoothstep(outerEdge - softness, outerEdge + softness, signedDist);
            } else {
                paperAlpha = 1.0 - smoothstep(innerEdge - softness, innerEdge + softness, signedDist);
            }
            
            // Fibers
            FiberFieldResult outerFibers = fiberField(px, py, signedDist - outerEdge, gradX, gradY,
                fiberDensity, fiberLength, fiberThickness, fiberSpread, 
                fiberSoftness, fiberFeather, fiberRange, seed + 1000);
            
            FiberFieldResult innerFibers = fiberField(px, py, signedDist - innerEdge, -gradX, -gradY,
                fiberDensity * 0.7, fiberLength * 0.8, fiberThickness, fiberSpread,
                fiberSoftness, fiberFeather, fiberRange, seed + 2000);
            
            FiberFieldResult middle1Fibers = {0, 0, 0.5, 0};
            FiberFieldResult middle2Fibers = {0, 0, 0.5, 0};
            
            if (middle1Amount > 0 && middle1FiberDensity > 0) {
                middle1Fibers = fiberField(px, py, signedDist - middle1Edge, -gradX, -gradY,
                    middle1FiberDensity, fiberLength * 0.6, fiberThickness, fiberSpread,
                    fiberSoftness, fiberFeather, fiberRange * 0.5, seed + 3000);
                middle1Fibers.opacity *= middle1Amount;
                middle1Fibers.shadowOpacity *= middle1Amount;
            }
            
            if (middle2Amount > 0 && middle2FiberDensity > 0) {
                middle2Fibers = fiberField(px, py, signedDist - middle2Edge, -gradX, -gradY,
                    middle2FiberDensity, fiberLength * 0.6, fiberThickness, fiberSpread,
                    fiberSoftness, fiberFeather, fiberRange * 0.5, seed + 4000);
                middle2Fibers.opacity *= middle2Amount;
                middle2Fibers.shadowOpacity *= middle2Amount;
            }
            
            double fiberAlpha = safeMax(safeMax(outerFibers.opacity, innerFibers.opacity),
                                        safeMax(middle1Fibers.opacity, middle2Fibers.opacity));
            fiberAlpha *= fiberOpacity;
            
            double fiberShadowAlpha = safeMax(safeMax(outerFibers.shadowOpacity, innerFibers.shadowOpacity),
                                              safeMax(middle1Fibers.shadowOpacity, middle2Fibers.shadowOpacity));
            fiberShadowAlpha *= fiberOpacity;
            
            double fiberColorVariation = 0.5;
            double maxFiberOp = fiberAlpha / safeMax(0.001, fiberOpacity);
            if (outerFibers.opacity >= maxFiberOp - 0.01) fiberColorVariation = outerFibers.colorVar;
            else if (innerFibers.opacity >= maxFiberOp - 0.01) fiberColorVariation = innerFibers.colorVar;
            
            if (fiberBlur > 0 && fiberAlpha > 0) {
                double blurFactor = 1.0 / (1.0 + fiberBlur * 0.2);
                fiberAlpha *= blurFactor;
                fiberShadowAlpha *= blurFactor;
            }
            
            double totalPaperAlpha = safeMax(paperAlpha, fiberAlpha);
            
            // Compute paper texture once and reuse for both backing and paper color
            double paperTex = 0.0;
            double paperTexBlue = 0.0;
            if (paperTexture > 0) {
                double texScale = 3.0 * masterScale;
                double grain1 = fbm2D(px / texScale, py / texScale, seed + 7000, 3, 0.5);
                double grain2 = valueNoise2D(px / (texScale * 0.5), py / (texScale * 0.5), seed + 8000);
                double streaks = fbm2D(px / (texScale * 0.67), py / (texScale * 5.0), seed + 9000, 2, 0.6);

                double tex = grain1 * 0.5 + grain2 * 0.3 + streaks * 0.2;
                tex = (tex - 0.5) * paperTexture * 0.15;
                paperTex = tex;
                paperTexBlue = tex * 0.9;
            }

            // Paper backing color with texture
            double backingR = clamp01(paperBaseR + paperTex);
            double backingG = clamp01(paperBaseG + paperTex);
            double backingB = clamp01(paperBaseB + paperTexBlue);

            // Paper color
            double paperR = paperBaseR, paperG = paperBaseG, paperB = paperBaseB;

            if (totalPaperAlpha > 0.01) {
                if (fiberShadow > 0 && fiberShadowAlpha > 0.01) {
                    double shadowStr = fiberShadowAlpha * fiberShadow * 0.4;
                    paperR *= (1.0 - shadowStr);
                    paperG *= (1.0 - shadowStr);
                    paperB *= (1.0 - shadowStr * 0.8);
                }

                if (fiberAlpha > 0.05) {
                    double colorShift = (fiberColorVariation - 0.5) * fiberColorVar * 0.25;
                    double fR = fiberBaseR * (1.0 + colorShift * 0.3);
                    double fG = fiberBaseG * (1.0 + colorShift * 0.2);
                    double fB = fiberBaseB * (1.0 + colorShift * 0.1);

                    double fiberBlend = fiberAlpha * 0.6;
                    paperR = paperR * (1.0 - fiberBlend) + clamp01(fR) * fiberBlend;
                    paperG = paperG * (1.0 - fiberBlend) + clamp01(fG) * fiberBlend;
                    paperB = paperB * (1.0 - fiberBlend) + clamp01(fB) * fiberBlend;
                }

                if (paperTexture > 0) {
                    paperR = clamp01(paperR + paperTex);
                    paperG = clamp01(paperG + paperTex);
                    paperB = clamp01(paperB + paperTexBlue);
                }
                
                // Middle shadows
                if (middle1Amount > 0 && middle1Shadow > 0) {
                    double distFromMiddle1 = middle1Edge - signedDist;
                    double m1ShadowWidth = shadowWidth * 0.4;
                    if (distFromMiddle1 > 0 && distFromMiddle1 < m1ShadowWidth) {
                        double shadowFactor = 1.0 - (distFromMiddle1 / m1ShadowWidth);
                        shadowFactor = shadowFactor * shadowFactor;
                        double shadow = shadowFactor * middle1Shadow * middle1Amount * 0.35;
                        paperR *= (1.0 - shadow);
                        paperG *= (1.0 - shadow);
                        paperB *= (1.0 - shadow * 0.8);
                    }
                }
                
                if (middle2Amount > 0 && middle2Shadow > 0) {
                    double distFromMiddle2 = middle2Edge - signedDist;
                    double m2ShadowWidth = shadowWidth * 0.4;
                    if (distFromMiddle2 > 0 && distFromMiddle2 < m2ShadowWidth) {
                        double shadowFactor = 1.0 - (distFromMiddle2 / m2ShadowWidth);
                        shadowFactor = shadowFactor * shadowFactor;
                        double shadow = shadowFactor * middle2Shadow * middle2Amount * 0.35;
                        paperR *= (1.0 - shadow);
                        paperG *= (1.0 - shadow);
                        paperB *= (1.0 - shadow * 0.8);
                    }
                }
                
                // Outer shadow
                if (shadowAmount > 0) {
                    double fiberExtent = outerFibers.maxExtent;
                    double shadowStart = outerEdge + fiberExtent;
                    double distFromShadowStart = signedDist - shadowStart;
                    if (distFromShadowStart > 0 && distFromShadowStart < shadowWidth) {
                        double shadowFactor = 1.0 - (distFromShadowStart / shadowWidth);
                        shadowFactor = shadowFactor * shadowFactor;
                        double shadow = shadowFactor * shadowAmount * 0.4;
                        paperR *= (1.0 - shadow);
                        paperG *= (1.0 - shadow);
                        paperB *= (1.0 - shadow * 0.8);
                    }
                }
                
                // Inner shadow
                if (innerShadowAmount > 0) {
                    double distFromInner = innerEdge - signedDist;
                    if (distFromInner > 0 && distFromInner < innerShadowWidth) {
                        double shadowFactor = 1.0 - (distFromInner / innerShadowWidth);
                        shadowFactor = shadowFactor * shadowFactor;
                        double shadow = shadowFactor * innerShadowAmount * 0.5;
                        paperR *= (1.0 - shadow);
                        paperG *= (1.0 - shadow);
                        paperB *= (1.0 - shadow * 0.8);
                    }
                }
            }
            
            // Composite
            double finalR, finalG, finalB, finalA;
            
            if (contentAlpha > 0.01) {
                if (srcA > 0.99) {
                    finalR = srcR;
                    finalG = srcG;
                    finalB = srcB;
                    finalA = 1.0;
                } else {
                    finalR = srcR * srcA + backingR * (1.0 - srcA);
                    finalG = srcG * srcA + backingG * (1.0 - srcA);
                    finalB = srcB * srcA + backingB * (1.0 - srcA);
                    finalA = 1.0;
                }
                
                // Apply fold mark to content
                if (foldAmount > 0) {
                    double crackStrength, foldShadowAStr, foldShadowBStr;
                    foldCrease(px, py, seed + 50000, fp1x, fp1y, fp2x, fp2y,
                        foldLineRoughness, foldLineRoughScale, foldLineWidth,
                        foldSideAWidth, foldSideARoughness, foldSideARoughScale, foldSideAJagged, foldSideASoftness,
                        foldSideBWidth, foldSideBRoughness, foldSideBRoughScale, foldSideBJagged, foldSideBSoftness,
                        foldCrackAmount, foldCrackLength, foldCrackLengthVar, foldCrackDensity, foldCrackBranching,
                        foldCrackAngle, foldCrackAngleVar,
                        foldShadowAOpacity, foldShadowALength, foldShadowAVariability,
                        foldShadowBOpacity, foldShadowBLength, foldShadowBVariability,
                        masterScale,
                        crackStrength, foldShadowAStr, foldShadowBStr);
                    
                    // Blend paper through at crack
                    // Apply foldAmount as a multiplier, but ensure 100% fold amount allows full crack visibility
                    crackStrength *= foldAmount;
                    if (crackStrength > 0) {
                        // Use a steeper curve so cracks are more opaque
                        double effectiveCrack = clamp01(crackStrength * 1.5);
                        finalR = finalR * (1.0 - effectiveCrack) + backingR * effectiveCrack;
                        finalG = finalG * (1.0 - effectiveCrack) + backingG * effectiveCrack;
                        finalB = finalB * (1.0 - effectiveCrack) + backingB * effectiveCrack;
                    }
                    
                    // Apply shadow A (highlight - typically white)
                    foldShadowAStr *= foldAmount;
                    if (foldShadowAStr > 0) {
                        double shAR = foldShadowAColor.red / 255.0;
                        double shAG = foldShadowAColor.green / 255.0;
                        double shAB = foldShadowAColor.blue / 255.0;
                        double effectiveShadowA = clamp01(foldShadowAStr);
                        finalR = finalR * (1.0 - effectiveShadowA) + shAR * effectiveShadowA;
                        finalG = finalG * (1.0 - effectiveShadowA) + shAG * effectiveShadowA;
                        finalB = finalB * (1.0 - effectiveShadowA) + shAB * effectiveShadowA;
                    }
                    
                    // Apply shadow B (shadow - typically black)
                    foldShadowBStr *= foldAmount;
                    if (foldShadowBStr > 0) {
                        double shBR = foldShadowBColor.red / 255.0;
                        double shBG = foldShadowBColor.green / 255.0;
                        double shBB = foldShadowBColor.blue / 255.0;
                        double effectiveShadowB = clamp01(foldShadowBStr);
                        finalR = finalR * (1.0 - effectiveShadowB) + shBR * effectiveShadowB;
                        finalG = finalG * (1.0 - effectiveShadowB) + shBG * effectiveShadowB;
                        finalB = finalB * (1.0 - effectiveShadowB) + shBB * effectiveShadowB;
                    }
                }
                
                // Apply grunge
                if (dirtAmount > 0) {
                    double dirt = organicDirt(px, py, seed + 30000, dirtSize, dirtAmount, masterScale);
                    double dirtStr = dirt * dirtOpacity;
                    finalR = finalR * (1.0 - dirtStr) + dirtR * dirtStr;
                    finalG = finalG * (1.0 - dirtStr) + dirtG * dirtStr;
                    finalB = finalB * (1.0 - dirtStr) + dirtB * dirtStr;
                }
                
                if (smudgeAmount > 0) {
                    double smudge = organicSmudge(px, py, seed + 31000, smudgeSize, smudgeAmount, masterScale);
                    double smudgeStr = smudge * smudgeOpacity;
                    finalR = finalR * (1.0 - smudgeStr) + smudgeR * smudgeStr;
                    finalG = finalG * (1.0 - smudgeStr) + smudgeG * smudgeStr;
                    finalB = finalB * (1.0 - smudgeStr) + smudgeB * smudgeStr;
                }
                
                if (dustAmount > 0) {
                    double dust = dustParticles(px, py, dustSeed, dustSize, dustAmount, masterScale);
                    if (dust > 0) {
                        finalR = finalR * (1.0 - dust) + dustR * dust;
                        finalG = finalG * (1.0 - dust) + dustG * dust;
                        finalB = finalB * (1.0 - dust) + dustB * dust;
                    }
                }
                
                if (contentAlpha < 0.99) {
                    finalR = finalR * contentAlpha + paperR * (1.0 - contentAlpha);
                    finalG = finalG * contentAlpha + paperG * (1.0 - contentAlpha);
                    finalB = finalB * contentAlpha + paperB * (1.0 - contentAlpha);
                    finalA = finalA * contentAlpha + totalPaperAlpha * (1.0 - contentAlpha);
                }
            } else {
                finalR = paperR;
                finalG = paperG;
                finalB = paperB;
                finalA = totalPaperAlpha;
            }
            
            finalA = clamp01(finalA);
            outRow[x].alpha = (A_u_char)(finalA * 255.0);
            outRow[x].red   = (A_u_char)(clamp01(finalR) * finalA * 255.0);
            outRow[x].green = (A_u_char)(clamp01(finalG) * finalA * 255.0);
            outRow[x].blue  = (A_u_char)(clamp01(finalB) * finalA * 255.0);
        }
    }
    
    return err;
}

// ============================================================
// SMART RENDER IMPLEMENTATION
// ============================================================

PF_Err SmartPreRender(
    PF_InData           *in_data,
    PF_OutData          *out_data,
    PF_PreRenderExtra   *extra)
{
    PF_Err err = PF_Err_NONE;
    PF_RenderRequest req = extra->input->output_request;
    PF_CheckoutResult checkout;
    
    // We need the full input layer for distance field calculation
    // Request entire layer
    req.preserve_rgb_of_zero_alpha = TRUE;
    
    // Checkout the input layer
    ERR(extra->cb->checkout_layer(  in_data->effect_ref,
                                    PARAM_INPUT,
                                    PARAM_INPUT,
                                    &req,
                                    in_data->current_time,
                                    in_data->time_step,
                                    in_data->time_scale,
                                    &checkout));
    
    // Calculate output rect with expansion for fibers
    if (!err) {
        // Get fiber length param to determine expansion
        PF_ParamDef param;
        AEFX_CLR_STRUCT(param);
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_LENGTH, in_data->current_time, 
                              in_data->time_step, in_data->time_scale, &param));
        
        double fiberLength = 80.0;  // Default
        if (!err) {
            fiberLength = param.u.fs_d.value;
            ERR(PF_CHECKIN_PARAM(in_data, &param));
        }
        
        // Get master scale
        AEFX_CLR_STRUCT(param);
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MASTER_SCALE, in_data->current_time,
                              in_data->time_step, in_data->time_scale, &param));
        
        double masterScale = 1.0;
        if (!err) {
            masterScale = param.u.fs_d.value / 100.0;
            ERR(PF_CHECKIN_PARAM(in_data, &param));
        }
        
        // Calculate expansion based on fiber length
        A_long expand = (A_long)(fiberLength * masterScale + 20);
        if (expand > MAX_EXPAND_PIXELS) expand = MAX_EXPAND_PIXELS;
        
        // Set the result rect (expanded from input)
        extra->output->result_rect = checkout.result_rect;
        extra->output->result_rect.left -= expand;
        extra->output->result_rect.top -= expand;
        extra->output->result_rect.right += expand;
        extra->output->result_rect.bottom += expand;
        
        // Clamp to layer bounds
        if (extra->output->result_rect.left < 0) extra->output->result_rect.left = 0;
        if (extra->output->result_rect.top < 0) extra->output->result_rect.top = 0;
        
        extra->output->max_result_rect = extra->output->result_rect;
        extra->output->solid = FALSE;
        extra->output->flags = PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS;
    }
    
    return err;
}

PF_Err SmartRender(
    PF_InData               *in_data,
    PF_OutData              *out_data,
    PF_SmartRenderExtra     *extra)
{
    PF_Err err = PF_Err_NONE;
    
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    
    PF_EffectWorld *input = NULL, *output = NULL;
    
    // Checkout input pixels
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, PARAM_INPUT, &input));
    
    // Checkout output buffer
    ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
    
    if (!err && input && output) {
        // Checkout all parameters
        PF_ParamDef params[PARAM_NUM_PARAMS];
        for (int i = 0; i < PARAM_NUM_PARAMS; i++) {
            AEFX_CLR_STRUCT(params[i]);
        }
        
        // Checkout each parameter we need
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MASTER_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MASTER_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_GAP_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_GAP_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_RANDOM_SEED, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_RANDOM_SEED]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_EDGE_SOFTNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_EDGE_SOFTNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_OUTER_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_OUTER_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_OUTER_ROUGH_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_OUTER_ROUGH_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_OUTER_JAGGEDNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_OUTER_JAGGEDNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_OUTER_NOTCH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_OUTER_NOTCH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_INNER_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_INNER_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_INNER_ROUGH_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_INNER_ROUGH_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_INNER_JAGGEDNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_INNER_JAGGEDNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_INNER_NOTCH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_INNER_NOTCH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_INNER_EXPANSION, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_INNER_EXPANSION]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE1_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE1_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE1_POSITION, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE1_POSITION]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE1_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE1_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE1_SHADOW, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE1_SHADOW]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE1_FIBER_DENSITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE1_FIBER_DENSITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE2_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE2_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE2_POSITION, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE2_POSITION]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE2_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE2_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE2_SHADOW, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE2_SHADOW]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_MIDDLE2_FIBER_DENSITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_MIDDLE2_FIBER_DENSITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_PAPER_TEXTURE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_PAPER_TEXTURE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SHADOW_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SHADOW_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SHADOW_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SHADOW_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_PAPER_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_PAPER_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_CONTENT_SHADOW_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_CONTENT_SHADOW_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_CONTENT_SHADOW_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_CONTENT_SHADOW_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_DENSITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_DENSITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_LENGTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_LENGTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_THICKNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_THICKNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_SPREAD, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_SPREAD]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_SOFTNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_SOFTNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_FEATHER, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_FEATHER]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_RANGE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_RANGE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_SHADOW, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_SHADOW]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_OPACITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FIBER_BLUR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FIBER_BLUR]));
        // PARAM_FIBER_COLOR_VAR removed - hardcoded to 0.30
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_POINT1, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_POINT1]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_POINT2, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_POINT2]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_LINE_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_LINE_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_LINE_ROUGH_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_LINE_ROUGH_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_LINE_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_LINE_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_A_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_A_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_A_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_A_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_A_ROUGH_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_A_ROUGH_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_A_JAGGEDNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_A_JAGGEDNESS]));
        // PARAM_FOLD_SIDE_A_SOFTNESS removed - hardcoded to 0
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_B_WIDTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_B_WIDTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_B_ROUGHNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_B_ROUGHNESS]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_B_ROUGH_SCALE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_B_ROUGH_SCALE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SIDE_B_JAGGEDNESS, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SIDE_B_JAGGEDNESS]));
        // PARAM_FOLD_SIDE_B_SOFTNESS removed - hardcoded to 0
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_LENGTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_LENGTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_LENGTH_VAR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_LENGTH_VAR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_DENSITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_DENSITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_BRANCHING, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_BRANCHING]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_ANGLE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_ANGLE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_CRACK_ANGLE_VAR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_CRACK_ANGLE_VAR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_A_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_A_OPACITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_A_LENGTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_A_LENGTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_A_VARIABILITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_A_VARIABILITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_A_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_A_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_B_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_B_OPACITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_B_LENGTH, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_B_LENGTH]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_B_VARIABILITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_B_VARIABILITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_FOLD_SHADOW_B_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_FOLD_SHADOW_B_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRT_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DIRT_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRT_SIZE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DIRT_SIZE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRT_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DIRT_OPACITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRT_SEED, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DIRT_SEED]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DIRT_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DIRT_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SMUDGE_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SMUDGE_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SMUDGE_SIZE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SMUDGE_SIZE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SMUDGE_OPACITY, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SMUDGE_OPACITY]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SMUDGE_SEED, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SMUDGE_SEED]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_SMUDGE_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_SMUDGE_COLOR]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DUST_AMOUNT, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DUST_AMOUNT]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DUST_SIZE, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DUST_SIZE]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DUST_SEED, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DUST_SEED]));
        ERR(PF_CHECKOUT_PARAM(in_data, PARAM_DUST_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &params[PARAM_DUST_COLOR]));
        
        if (!err) {
            // Calculate downsample factor for preview resolution scaling
            // At full res: num=1, den=1 -> factor=1.0
            // At half res: num=1, den=2 -> factor=0.5
            double downsampleX = (double)in_data->downsample_x.num / (double)in_data->downsample_x.den;
            double downsampleY = (double)in_data->downsample_y.num / (double)in_data->downsample_y.den;
            double downsampleFactor = (downsampleX + downsampleY) * 0.5;  // Average of X and Y
            
            // Extract parameter values
            // DON'T scale masterScale - we work in full-res coordinate space
            double masterScale = params[PARAM_MASTER_SCALE].u.fs_d.value / 100.0;
            
            double gapWidth = params[PARAM_GAP_WIDTH].u.fs_d.value * masterScale;
            int seed = params[PARAM_RANDOM_SEED].u.sd.value;
            double edgeSoftness = params[PARAM_EDGE_SOFTNESS].u.fs_d.value * masterScale;
            
            double outerRoughness = params[PARAM_OUTER_ROUGHNESS].u.fs_d.value;
            double outerRoughScale = params[PARAM_OUTER_ROUGH_SCALE].u.fs_d.value;
            double outerJaggedness = params[PARAM_OUTER_JAGGEDNESS].u.fs_d.value;
            double outerNotch = params[PARAM_OUTER_NOTCH].u.fs_d.value;
            
            double innerRoughness = params[PARAM_INNER_ROUGHNESS].u.fs_d.value;
            double innerRoughScale = params[PARAM_INNER_ROUGH_SCALE].u.fs_d.value;
            double innerJaggedness = params[PARAM_INNER_JAGGEDNESS].u.fs_d.value;
            double innerNotch = params[PARAM_INNER_NOTCH].u.fs_d.value;
            double innerExpansion = params[PARAM_INNER_EXPANSION].u.fs_d.value;
            
            double middle1Amount = params[PARAM_MIDDLE1_AMOUNT].u.fs_d.value / 100.0;
            double middle1Position = params[PARAM_MIDDLE1_POSITION].u.fs_d.value / 100.0;
            double middle1Roughness = params[PARAM_MIDDLE1_ROUGHNESS].u.fs_d.value;
            double middle1Shadow = params[PARAM_MIDDLE1_SHADOW].u.fs_d.value / 100.0;
            double middle1FiberDensity = params[PARAM_MIDDLE1_FIBER_DENSITY].u.fs_d.value;
            
            double middle2Amount = params[PARAM_MIDDLE2_AMOUNT].u.fs_d.value / 100.0;
            double middle2Position = params[PARAM_MIDDLE2_POSITION].u.fs_d.value / 100.0;
            double middle2Roughness = params[PARAM_MIDDLE2_ROUGHNESS].u.fs_d.value;
            double middle2Shadow = params[PARAM_MIDDLE2_SHADOW].u.fs_d.value / 100.0;
            double middle2FiberDensity = params[PARAM_MIDDLE2_FIBER_DENSITY].u.fs_d.value;
            
            double paperTexture = params[PARAM_PAPER_TEXTURE].u.fs_d.value / 100.0;
            double shadowAmount = params[PARAM_SHADOW_AMOUNT].u.fs_d.value / 100.0;
            double shadowWidth = params[PARAM_SHADOW_WIDTH].u.fs_d.value * masterScale;
            
            PF_Pixel paperColor = params[PARAM_PAPER_COLOR].u.cd.value;
            double paperBaseR = paperColor.red / 255.0;
            double paperBaseG = paperColor.green / 255.0;
            double paperBaseB = paperColor.blue / 255.0;
            
            PF_Pixel fiberColor = params[PARAM_FIBER_COLOR].u.cd.value;
            double fiberBaseR = fiberColor.red / 255.0;
            double fiberBaseG = fiberColor.green / 255.0;
            double fiberBaseB = fiberColor.blue / 255.0;
            
            double innerShadowAmount = params[PARAM_CONTENT_SHADOW_AMOUNT].u.fs_d.value / 100.0;
            double innerShadowWidth = params[PARAM_CONTENT_SHADOW_WIDTH].u.fs_d.value * masterScale;
            
            double fiberDensity = params[PARAM_FIBER_DENSITY].u.fs_d.value;
            double fiberLength = params[PARAM_FIBER_LENGTH].u.fs_d.value * masterScale;
            double fiberThickness = params[PARAM_FIBER_THICKNESS].u.fs_d.value * masterScale;
            double fiberSpread = params[PARAM_FIBER_SPREAD].u.fs_d.value;
            double fiberSoftness = params[PARAM_FIBER_SOFTNESS].u.fs_d.value / 100.0;
            double fiberFeather = params[PARAM_FIBER_FEATHER].u.fs_d.value / 100.0;
            double fiberRange = params[PARAM_FIBER_RANGE].u.fs_d.value;
            double fiberShadow = params[PARAM_FIBER_SHADOW].u.fs_d.value / 100.0;
            double fiberOpacity = params[PARAM_FIBER_OPACITY].u.fs_d.value / 100.0;
            double fiberBlur = params[PARAM_FIBER_BLUR].u.fs_d.value;
            double fiberColorVar = 0.30;  // Hardcoded, control removed
            
            double foldAmount = params[PARAM_FOLD_AMOUNT].u.fs_d.value / 100.0;
            A_long fold1X = params[PARAM_FOLD_POINT1].u.td.x_value;
            A_long fold1Y = params[PARAM_FOLD_POINT1].u.td.y_value;
            A_long fold2X = params[PARAM_FOLD_POINT2].u.td.x_value;
            A_long fold2Y = params[PARAM_FOLD_POINT2].u.td.y_value;
            double foldLineRoughness = params[PARAM_FOLD_LINE_ROUGHNESS].u.fs_d.value;
            double foldLineRoughScale = params[PARAM_FOLD_LINE_ROUGH_SCALE].u.fs_d.value;
            // These pixel values are used in noise-coordinate space (full resolution)
            double foldLineWidth = params[PARAM_FOLD_LINE_WIDTH].u.fs_d.value;
            double foldSideAWidth = params[PARAM_FOLD_SIDE_A_WIDTH].u.fs_d.value;
            double foldSideARoughness = params[PARAM_FOLD_SIDE_A_ROUGHNESS].u.fs_d.value;
            double foldSideARoughScale = params[PARAM_FOLD_SIDE_A_ROUGH_SCALE].u.fs_d.value;
            double foldSideAJagged = params[PARAM_FOLD_SIDE_A_JAGGEDNESS].u.fs_d.value;
            double foldSideASoftness = 0.0;  // Hardcoded, control removed
            double foldSideBWidth = params[PARAM_FOLD_SIDE_B_WIDTH].u.fs_d.value;
            double foldSideBRoughness = params[PARAM_FOLD_SIDE_B_ROUGHNESS].u.fs_d.value;
            double foldSideBRoughScale = params[PARAM_FOLD_SIDE_B_ROUGH_SCALE].u.fs_d.value;
            double foldSideBJagged = params[PARAM_FOLD_SIDE_B_JAGGEDNESS].u.fs_d.value;
            double foldSideBSoftness = 0.0;  // Hardcoded, control removed
            double foldCrackAmount = params[PARAM_FOLD_CRACK_AMOUNT].u.fs_d.value / 100.0;
            double foldCrackLength = params[PARAM_FOLD_CRACK_LENGTH].u.fs_d.value;
            double foldCrackLengthVar = params[PARAM_FOLD_CRACK_LENGTH_VAR].u.fs_d.value / 100.0;
            double foldCrackDensity = params[PARAM_FOLD_CRACK_DENSITY].u.fs_d.value;
            double foldCrackBranching = params[PARAM_FOLD_CRACK_BRANCHING].u.fs_d.value / 100.0;
            double foldCrackAngle = params[PARAM_FOLD_CRACK_ANGLE].u.fs_d.value;
            double foldCrackAngleVar = params[PARAM_FOLD_CRACK_ANGLE_VAR].u.fs_d.value;
            double foldShadowAOpacity = params[PARAM_FOLD_SHADOW_A_OPACITY].u.fs_d.value / 100.0;
            double foldShadowALength = params[PARAM_FOLD_SHADOW_A_LENGTH].u.fs_d.value;
            double foldShadowAVariability = params[PARAM_FOLD_SHADOW_A_VARIABILITY].u.fs_d.value / 100.0;
            PF_Pixel foldShadowAColor = params[PARAM_FOLD_SHADOW_A_COLOR].u.cd.value;
            double foldShadowBOpacity = params[PARAM_FOLD_SHADOW_B_OPACITY].u.fs_d.value / 100.0;
            double foldShadowBLength = params[PARAM_FOLD_SHADOW_B_LENGTH].u.fs_d.value;
            double foldShadowBVariability = params[PARAM_FOLD_SHADOW_B_VARIABILITY].u.fs_d.value / 100.0;
            PF_Pixel foldShadowBColor = params[PARAM_FOLD_SHADOW_B_COLOR].u.cd.value;
            
            double dirtAmount = params[PARAM_DIRT_AMOUNT].u.fs_d.value;
            double dirtSize = params[PARAM_DIRT_SIZE].u.fs_d.value;
            double dirtOpacity = params[PARAM_DIRT_OPACITY].u.fs_d.value / 100.0;
            int dirtSeed = params[PARAM_DIRT_SEED].u.sd.value;
            PF_Pixel dirtColor = params[PARAM_DIRT_COLOR].u.cd.value;
            double dirtR = dirtColor.red / 255.0;
            double dirtG = dirtColor.green / 255.0;
            double dirtB = dirtColor.blue / 255.0;
            
            double smudgeAmount = params[PARAM_SMUDGE_AMOUNT].u.fs_d.value;
            double smudgeSize = params[PARAM_SMUDGE_SIZE].u.fs_d.value;
            double smudgeOpacity = params[PARAM_SMUDGE_OPACITY].u.fs_d.value / 100.0;
            int smudgeSeed = params[PARAM_SMUDGE_SEED].u.sd.value;
            PF_Pixel smudgeColor = params[PARAM_SMUDGE_COLOR].u.cd.value;
            double smudgeR = smudgeColor.red / 255.0;
            double smudgeG = smudgeColor.green / 255.0;
            double smudgeB = smudgeColor.blue / 255.0;
            
            double dustAmount = params[PARAM_DUST_AMOUNT].u.fs_d.value;
            double dustSize = params[PARAM_DUST_SIZE].u.fs_d.value;
            int dustSeed = params[PARAM_DUST_SEED].u.sd.value;
            PF_Pixel dustColor = params[PARAM_DUST_COLOR].u.cd.value;
            double dustR = dustColor.red / 255.0;
            double dustG = dustColor.green / 255.0;
            double dustB = dustColor.blue / 255.0;
            
            int width = output->width;
            int height = output->height;
            
            // Fold points are in full-resolution layer coordinates
            // We'll divide by downsampleFactor when calling foldCrease to match noisePx/noisePy
            double fp1x = (double)fold1X / 65536.0;
            double fp1y = (double)fold1Y / 65536.0;
            double fp2x = (double)fold2X / 65536.0;
            double fp2y = (double)fold2Y / 65536.0;
            
            // Build distance field from input
            // Need to handle different pixel formats for distance field too
            DistanceField df(input->width, input->height);
            
            // Determine bit depth properly
            // PF_WORLD_IS_DEEP checks for 16-bit
            // For 32-bit float, we need to check world_flags for specific flag
            bool is16bit = PF_WORLD_IS_DEEP(output) ? true : false;
            bool isFloat = false;
            
            // Check if 32-bit float by looking at actual bytes per pixel
            // 8-bit: 4 bytes per pixel (but rowbytes may have padding)
            // 16-bit: 8 bytes per pixel  
            // 32-bit float: 16 bytes per pixel
            // Use a more reliable check - if not 16-bit deep, check if rowbytes suggests float
            if (!is16bit) {
                // Minimum bytes needed for the row without padding
                A_long minRowBytes8 = output->width * 4;   // 8-bit ARGB
                A_long minRowBytes32 = output->width * 16; // 32-bit float ARGB
                
                // If rowbytes is large enough for float, it's probably float
                if (output->rowbytes >= minRowBytes32) {
                    isFloat = true;
                }
            }
            
            // Determine pixel size for array indexing
            int pixelSize = 4;  // Default 8-bit (4 bytes)
            if (isFloat) {
                pixelSize = 16;  // 32-bit float (16 bytes)
            } else if (is16bit) {
                pixelSize = 8;   // 16-bit (8 bytes)
            }
            
            // Build distance field
            df.buildFromLayerGeneric(input, pixelSize);
            
            // Render to output based on format
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    double px = (double)x;
                    double py = (double)y;
                    
                    // Scale coordinates to full-resolution space for consistent noise sampling
                    // At half res, pixel 50 should sample same noise as pixel 100 at full res
                    double noisePx = px / downsampleFactor;
                    double noisePy = py / downsampleFactor;
                    
                    // Map output coords to input coords (handle expansion)
                    int inX = x;
                    int inY = y;
                    
                    // Get distance field values (clamp to input bounds)
                    int dfX = safeMax(0, safeMin(input->width - 1, inX));
                    int dfY = safeMax(0, safeMin(input->height - 1, inY));
                    
                    // signedDist is in canvas pixels - scale to full-res space
                    float signedDistRaw = df.getDist(dfX, dfY);
                    double signedDist = signedDistRaw / downsampleFactor;
                    float gradX, gradY;
                    df.getGradient(dfX, dfY, gradX, gradY);
                    
                    // Get source pixel (with bounds check) - normalized to 0.0-1.0
                    double srcR = 0, srcG = 0, srcB = 0, srcA = 0;
                    if (inX >= 0 && inX < input->width && inY >= 0 && inY < input->height) {
                        if (isFloat) {
                            // 32-bit float
                            PF_PixelFloat* inRow = (PF_PixelFloat*)((char*)input->data + inY * input->rowbytes);
                            srcR = inRow[inX].red;
                            srcG = inRow[inX].green;
                            srcB = inRow[inX].blue;
                            srcA = inRow[inX].alpha;
                        } else if (is16bit) {
                            // 16-bit
                            PF_Pixel16* inRow = (PF_Pixel16*)((char*)input->data + inY * input->rowbytes);
                            srcR = inRow[inX].red / 32768.0;
                            srcG = inRow[inX].green / 32768.0;
                            srcB = inRow[inX].blue / 32768.0;
                            srcA = inRow[inX].alpha / 32768.0;
                        } else {
                            // 8-bit
                            PF_Pixel8* inRow = (PF_Pixel8*)((char*)input->data + inY * input->rowbytes);
                            srcR = inRow[inX].red / 255.0;
                            srcG = inRow[inX].green / 255.0;
                            srcB = inRow[inX].blue / 255.0;
                            srcA = inRow[inX].alpha / 255.0;
                        }
                    }
                    
                    // Edge displacements - use noise coordinates for consistency
                    // masterScale is already scaled for pixel sizes, noisePx/noisePy for noise
                    double outerDisp = calcEdgeDisplacement(noisePx, noisePy, seed, 
                        outerRoughness, outerRoughScale, outerJaggedness, outerNotch, masterScale);
                    double innerDispRaw = calcEdgeDisplacement(noisePx + 1000, noisePy + 1000, seed + 5000,
                        innerRoughness, innerRoughScale, innerJaggedness, innerNotch, masterScale);
                    // Shift inner edge based on expansion control
                    // expansion=100: no shift (innerDisp = innerDispRaw)
                    // expansion=50: current behavior (small shift)
                    // expansion=1: maximum shift inward
                    double expansionFactor = (100.0 - innerExpansion) / 50.0;  // 0 at 100, 1 at 50, ~2 at 1
                    double innerDispMaxEstimate = (innerRoughness + innerJaggedness * 0.5 + innerNotch * 0.3) * masterScale * expansionFactor;
                    double innerDisp = innerDispRaw - innerDispMaxEstimate;
                    
                    double halfGap = gapWidth / 2.0;
                    double outerEdge = -halfGap + outerDisp;
                    double innerEdge = halfGap + innerDisp;
                    
                    if (innerEdge < outerEdge + 2.0) {
                        double mid = (innerEdge + outerEdge) / 2.0;
                        innerEdge = mid + 1.0;
                        outerEdge = mid - 1.0;
                    }
                    
                    // Middle edges
                    double middle1Edge = outerEdge;
                    double middle2Edge = outerEdge;
                    
                    if (middle1Amount > 0) {
                        double m1Disp = calcEdgeDisplacement(noisePx + 2000, noisePy + 2000, seed + 10000,
                            middle1Roughness, 100.0, middle1Roughness * 0.2, 0, masterScale);
                        double m1Base = outerEdge + (innerEdge - outerEdge) * middle1Position;
                        middle1Edge = m1Base + m1Disp * 0.4;
                        middle1Edge = clamp(middle1Edge, outerEdge + 1.0, innerEdge - 1.0);
                    }
                    
                    if (middle2Amount > 0) {
                        double m2Disp = calcEdgeDisplacement(noisePx + 3000, noisePy + 3000, seed + 15000,
                            middle2Roughness, 100.0, middle2Roughness * 0.2, 0, masterScale);
                        double m2Base = outerEdge + (innerEdge - outerEdge) * middle2Position;
                        middle2Edge = m2Base + m2Disp * 0.4;
                        middle2Edge = clamp(middle2Edge, outerEdge + 1.0, innerEdge - 1.0);
                    }
                    
                    // Alphas
                    double softness = safeMax(0.5, edgeSoftness);
                    double contentAlpha = smoothstep(innerEdge - softness, innerEdge + softness, signedDist);
                    
                    double paperAlpha = 0.0;
                    if (signedDist <= outerEdge - softness) {
                        paperAlpha = 0.0;
                    } else if (signedDist >= innerEdge + softness) {
                        paperAlpha = 0.0;
                    } else if (signedDist > outerEdge + softness && signedDist < innerEdge - softness) {
                        paperAlpha = 1.0;
                    } else if (signedDist <= outerEdge + softness) {
                        paperAlpha = smoothstep(outerEdge - softness, outerEdge + softness, signedDist);
                    } else {
                        paperAlpha = 1.0 - smoothstep(innerEdge - softness, innerEdge + softness, signedDist);
                    }
                    
                    // Fibers - use noise coordinates for consistency
                    FiberFieldResult outerFibers = fiberField(noisePx, noisePy, signedDist - outerEdge, gradX, gradY,
                        fiberDensity, fiberLength, fiberThickness, fiberSpread, 
                        fiberSoftness, fiberFeather, fiberRange, seed + 1000);
                    
                    FiberFieldResult innerFibers = fiberField(noisePx, noisePy, signedDist - innerEdge, -gradX, -gradY,
                        fiberDensity * 0.7, fiberLength * 0.8, fiberThickness, fiberSpread,
                        fiberSoftness, fiberFeather, fiberRange, seed + 2000);
                    
                    FiberFieldResult middle1Fibers = {0, 0, 0.5, 0};
                    FiberFieldResult middle2Fibers = {0, 0, 0.5, 0};
                    
                    if (middle1Amount > 0 && middle1FiberDensity > 0) {
                        middle1Fibers = fiberField(noisePx, noisePy, signedDist - middle1Edge, -gradX, -gradY,
                            middle1FiberDensity, fiberLength * 0.6, fiberThickness, fiberSpread,
                            fiberSoftness, fiberFeather, fiberRange * 0.5, seed + 3000);
                        middle1Fibers.opacity *= middle1Amount;
                        middle1Fibers.shadowOpacity *= middle1Amount;
                    }
                    
                    if (middle2Amount > 0 && middle2FiberDensity > 0) {
                        middle2Fibers = fiberField(noisePx, noisePy, signedDist - middle2Edge, -gradX, -gradY,
                            middle2FiberDensity, fiberLength * 0.6, fiberThickness, fiberSpread,
                            fiberSoftness, fiberFeather, fiberRange * 0.5, seed + 4000);
                        middle2Fibers.opacity *= middle2Amount;
                        middle2Fibers.shadowOpacity *= middle2Amount;
                    }
                    
                    double fiberAlpha = safeMax(safeMax(outerFibers.opacity, innerFibers.opacity),
                                                safeMax(middle1Fibers.opacity, middle2Fibers.opacity));
                    fiberAlpha *= fiberOpacity;
                    
                    double fiberShadowAlpha = safeMax(safeMax(outerFibers.shadowOpacity, innerFibers.shadowOpacity),
                                                      safeMax(middle1Fibers.shadowOpacity, middle2Fibers.shadowOpacity));
                    fiberShadowAlpha *= fiberOpacity;
                    
                    double fiberColorVariation = 0.5;
                    double maxFiberOp = fiberAlpha / safeMax(0.001, fiberOpacity);
                    if (outerFibers.opacity >= maxFiberOp - 0.01) fiberColorVariation = outerFibers.colorVar;
                    else if (innerFibers.opacity >= maxFiberOp - 0.01) fiberColorVariation = innerFibers.colorVar;
                    
                    if (fiberBlur > 0 && fiberAlpha > 0) {
                        double blurFactor = 1.0 / (1.0 + fiberBlur * 0.2);
                        fiberAlpha *= blurFactor;
                        fiberShadowAlpha *= blurFactor;
                    }
                    
                    double totalPaperAlpha = safeMax(paperAlpha, fiberAlpha);
                    
                    // Compute paper texture once and reuse for both backing and paper color
                    double paperTex = 0.0;
                    double paperTexBlue = 0.0;
                    if (paperTexture > 0) {
                        double texScale = 3.0 * masterScale / downsampleFactor;
                        double grain1 = fbm2D(noisePx / texScale, noisePy / texScale, seed + 7000, 3, 0.5);
                        double grain2 = valueNoise2D(noisePx / (texScale * 0.5), noisePy / (texScale * 0.5), seed + 8000);
                        double streaks = fbm2D(noisePx / (texScale * 0.67), noisePy / (texScale * 5.0), seed + 9000, 2, 0.6);

                        double tex = grain1 * 0.5 + grain2 * 0.3 + streaks * 0.2;
                        tex = (tex - 0.5) * paperTexture * 0.15;
                        paperTex = tex;
                        paperTexBlue = tex * 0.9;
                    }

                    // Paper backing color with texture
                    double backingR = clamp01(paperBaseR + paperTex);
                    double backingG = clamp01(paperBaseG + paperTex);
                    double backingB = clamp01(paperBaseB + paperTexBlue);

                    // Paper color
                    double paperR = paperBaseR, paperG = paperBaseG, paperB = paperBaseB;

                    if (totalPaperAlpha > 0.01) {
                        if (fiberShadow > 0 && fiberShadowAlpha > 0.01) {
                            double shadowStr = fiberShadowAlpha * fiberShadow * 0.4;
                            paperR *= (1.0 - shadowStr);
                            paperG *= (1.0 - shadowStr);
                            paperB *= (1.0 - shadowStr * 0.8);
                        }

                        if (fiberAlpha > 0.05) {
                            double colorShift = (fiberColorVariation - 0.5) * fiberColorVar * 0.25;
                            double fR = fiberBaseR * (1.0 + colorShift * 0.3);
                            double fG = fiberBaseG * (1.0 + colorShift * 0.2);
                            double fB = fiberBaseB * (1.0 + colorShift * 0.1);

                            double fiberBlend = fiberAlpha * 0.6;
                            paperR = paperR * (1.0 - fiberBlend) + clamp01(fR) * fiberBlend;
                            paperG = paperG * (1.0 - fiberBlend) + clamp01(fG) * fiberBlend;
                            paperB = paperB * (1.0 - fiberBlend) + clamp01(fB) * fiberBlend;
                        }

                        if (paperTexture > 0) {
                            paperR = clamp01(paperR + paperTex);
                            paperG = clamp01(paperG + paperTex);
                            paperB = clamp01(paperB + paperTexBlue);
                        }
                    }
                    
                    // Composite
                    double finalR, finalG, finalB, finalA;
                    
                    if (contentAlpha > 0.01) {
                        if (srcA > 0.99) {
                            finalR = srcR;
                            finalG = srcG;
                            finalB = srcB;
                            finalA = 1.0;
                        } else {
                            finalR = srcR * srcA + backingR * (1.0 - srcA);
                            finalG = srcG * srcA + backingG * (1.0 - srcA);
                            finalB = srcB * srcA + backingB * (1.0 - srcA);
                            finalA = 1.0;
                        }
                        
                        // Apply fold mark - use noise coordinates
                        if (foldAmount > 0) {
                            double crackStrength, foldShadowAStr, foldShadowBStr;
                            // fp1/fp2 are in full-res space, so divide by downsampleFactor to match noisePx/noisePy
                            foldCrease(noisePx, noisePy, seed + 50000, 
                                fp1x / downsampleFactor, fp1y / downsampleFactor, 
                                fp2x / downsampleFactor, fp2y / downsampleFactor,
                                foldLineRoughness, foldLineRoughScale, foldLineWidth,
                                foldSideAWidth, foldSideARoughness, foldSideARoughScale, foldSideAJagged, foldSideASoftness,
                                foldSideBWidth, foldSideBRoughness, foldSideBRoughScale, foldSideBJagged, foldSideBSoftness,
                                foldCrackAmount, foldCrackLength, foldCrackLengthVar, foldCrackDensity, foldCrackBranching,
                                foldCrackAngle, foldCrackAngleVar,
                                foldShadowAOpacity, foldShadowALength, foldShadowAVariability,
                                foldShadowBOpacity, foldShadowBLength, foldShadowBVariability,
                                masterScale,
                                crackStrength, foldShadowAStr, foldShadowBStr);
                            
                            crackStrength *= foldAmount;
                            if (crackStrength > 0) {
                                double effectiveCrack = clamp01(crackStrength * 1.5);
                                finalR = finalR * (1.0 - effectiveCrack) + backingR * effectiveCrack;
                                finalG = finalG * (1.0 - effectiveCrack) + backingG * effectiveCrack;
                                finalB = finalB * (1.0 - effectiveCrack) + backingB * effectiveCrack;
                            }
                            
                            foldShadowAStr *= foldAmount;
                            if (foldShadowAStr > 0) {
                                double shAR = foldShadowAColor.red / 255.0;
                                double shAG = foldShadowAColor.green / 255.0;
                                double shAB = foldShadowAColor.blue / 255.0;
                                double effectiveShadowA = clamp01(foldShadowAStr);
                                finalR = finalR * (1.0 - effectiveShadowA) + shAR * effectiveShadowA;
                                finalG = finalG * (1.0 - effectiveShadowA) + shAG * effectiveShadowA;
                                finalB = finalB * (1.0 - effectiveShadowA) + shAB * effectiveShadowA;
                            }
                            
                            foldShadowBStr *= foldAmount;
                            if (foldShadowBStr > 0) {
                                double shBR = foldShadowBColor.red / 255.0;
                                double shBG = foldShadowBColor.green / 255.0;
                                double shBB = foldShadowBColor.blue / 255.0;
                                double effectiveShadowB = clamp01(foldShadowBStr);
                                finalR = finalR * (1.0 - effectiveShadowB) + shBR * effectiveShadowB;
                                finalG = finalG * (1.0 - effectiveShadowB) + shBG * effectiveShadowB;
                                finalB = finalB * (1.0 - effectiveShadowB) + shBB * effectiveShadowB;
                            }
                        }
                        
                        // Apply grunge
                        if (dirtAmount > 0) {
                            double dirt = organicDirt(noisePx, noisePy, dirtSeed, dirtSize, dirtAmount, masterScale);
                            double dirtStr = dirt * dirtOpacity;
                            finalR = finalR * (1.0 - dirtStr) + dirtR * dirtStr;
                            finalG = finalG * (1.0 - dirtStr) + dirtG * dirtStr;
                            finalB = finalB * (1.0 - dirtStr) + dirtB * dirtStr;
                        }
                        
                        if (smudgeAmount > 0) {
                            double smudge = organicSmudge(noisePx, noisePy, smudgeSeed, smudgeSize, smudgeAmount, masterScale);
                            double smudgeStr = smudge * smudgeOpacity;
                            finalR = finalR * (1.0 - smudgeStr) + smudgeR * smudgeStr;
                            finalG = finalG * (1.0 - smudgeStr) + smudgeG * smudgeStr;
                            finalB = finalB * (1.0 - smudgeStr) + smudgeB * smudgeStr;
                        }
                        
                        if (dustAmount > 0) {
                            double dust = dustParticles(noisePx, noisePy, dustSeed, dustSize, dustAmount, masterScale);
                            if (dust > 0) {
                                finalR = finalR * (1.0 - dust) + dustR * dust;
                                finalG = finalG * (1.0 - dust) + dustG * dust;
                                finalB = finalB * (1.0 - dust) + dustB * dust;
                            }
                        }
                        
                        if (contentAlpha < 0.99) {
                            finalR = finalR * contentAlpha + paperR * (1.0 - contentAlpha);
                            finalG = finalG * contentAlpha + paperG * (1.0 - contentAlpha);
                            finalB = finalB * contentAlpha + paperB * (1.0 - contentAlpha);
                            finalA = finalA * contentAlpha + totalPaperAlpha * (1.0 - contentAlpha);
                        }
                    } else {
                        finalR = paperR;
                        finalG = paperG;
                        finalB = paperB;
                        finalA = totalPaperAlpha;
                    }
                    
                    finalA = clamp01(finalA);
                    finalR = clamp01(finalR);
                    finalG = clamp01(finalG);
                    finalB = clamp01(finalB);
                    
                    // Write output pixel based on format
                    if (isFloat) {
                        // 32-bit float (0.0 - 1.0 range, NOT premultiplied for straight alpha)
                        PF_PixelFloat* outRow = (PF_PixelFloat*)((char*)output->data + y * output->rowbytes);
                        outRow[x].alpha = (PF_FpShort)finalA;
                        outRow[x].red   = (PF_FpShort)(finalR * finalA);
                        outRow[x].green = (PF_FpShort)(finalG * finalA);
                        outRow[x].blue  = (PF_FpShort)(finalB * finalA);
                    } else if (is16bit) {
                        // 16-bit (0 - 32768 range)
                        PF_Pixel16* outRow = (PF_Pixel16*)((char*)output->data + y * output->rowbytes);
                        outRow[x].alpha = (A_u_short)(finalA * 32768.0);
                        outRow[x].red   = (A_u_short)(finalR * finalA * 32768.0);
                        outRow[x].green = (A_u_short)(finalG * finalA * 32768.0);
                        outRow[x].blue  = (A_u_short)(finalB * finalA * 32768.0);
                    } else {
                        // 8-bit (0 - 255 range)
                        PF_Pixel8* outRow = (PF_Pixel8*)((char*)output->data + y * output->rowbytes);
                        outRow[x].alpha = (A_u_char)(finalA * 255.0);
                        outRow[x].red   = (A_u_char)(finalR * finalA * 255.0);
                        outRow[x].green = (A_u_char)(finalG * finalA * 255.0);
                        outRow[x].blue  = (A_u_char)(finalB * finalA * 255.0);
                    }
                }
            }
        }
        
        // Check in all parameters
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MASTER_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_GAP_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_RANDOM_SEED]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_EDGE_SOFTNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_OUTER_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_OUTER_ROUGH_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_OUTER_JAGGEDNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_OUTER_NOTCH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_INNER_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_INNER_ROUGH_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_INNER_JAGGEDNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_INNER_NOTCH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_INNER_EXPANSION]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE1_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE1_POSITION]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE1_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE1_SHADOW]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE1_FIBER_DENSITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE2_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE2_POSITION]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE2_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE2_SHADOW]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_MIDDLE2_FIBER_DENSITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_PAPER_TEXTURE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SHADOW_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SHADOW_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_PAPER_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_CONTENT_SHADOW_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_CONTENT_SHADOW_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_DENSITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_LENGTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_THICKNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_SPREAD]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_SOFTNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_FEATHER]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_RANGE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_SHADOW]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_OPACITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FIBER_BLUR]);
        // PARAM_FIBER_COLOR_VAR removed - hardcoded to 0.30
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_POINT1]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_POINT2]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_LINE_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_LINE_ROUGH_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_LINE_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_A_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_A_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_A_ROUGH_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_A_JAGGEDNESS]);
        // PARAM_FOLD_SIDE_A_SOFTNESS removed - hardcoded to 0
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_B_WIDTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_B_ROUGHNESS]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_B_ROUGH_SCALE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SIDE_B_JAGGEDNESS]);
        // PARAM_FOLD_SIDE_B_SOFTNESS removed - hardcoded to 0
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_LENGTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_LENGTH_VAR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_DENSITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_BRANCHING]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_ANGLE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_CRACK_ANGLE_VAR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_A_OPACITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_A_LENGTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_A_VARIABILITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_A_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_B_OPACITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_B_LENGTH]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_B_VARIABILITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_FOLD_SHADOW_B_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DIRT_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DIRT_SIZE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DIRT_OPACITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DIRT_SEED]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DIRT_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SMUDGE_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SMUDGE_SIZE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SMUDGE_OPACITY]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SMUDGE_SEED]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_SMUDGE_COLOR]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DUST_AMOUNT]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DUST_SIZE]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DUST_SEED]);
        PF_CHECKIN_PARAM(in_data, &params[PARAM_DUST_COLOR]);
    }
    
    return err;
}
