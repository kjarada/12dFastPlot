//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_globals.H
//
// Brief description
// Defines, global storage and forward declarations for the FastPlot
// macro (fast, small PDF plots of a Multi Plot Sheet .12dmpsf).
//---------------------------------------------------------------------

#ifndef FASTPLOT_GLOBALS_H
#define FASTPLOT_GLOBALS_H

//---------------------------------------------------------------------
// Debug switch - comment out for release builds.  With DEBUG on the
// log is written and the work folder of every run is kept, plot
// parameter files and all, so a sheet can be re-plotted by hand.
//---------------------------------------------------------------------
// #define DEBUG

#define PROGRAM_NAME            "FastPlot"
#define PROGRAM_VERSION         "01"

//---------------------------------------------------------------------
// set_ups.h style constants, declared here so the macro does not depend
// on a particular set_ups.h subset
//---------------------------------------------------------------------
#define TRUE                    1
#define FALSE                   0

#define CHECK_FILE_MUST_EXIST   1
#define GET_FILE_MUST_EXIST     17
#define CHECK_FILE_CREATE       14
#define GET_FILE_CREATE         15
#define NO_FILE                 4

#define ALL_WIDGETS_OWN_HEIGHT  2

#define MESSAGE_LEVEL_GENERAL   1
#define MESSAGE_LEVEL_WARNING   2
#define MESSAGE_LEVEL_ERROR     3
#define MESSAGE_LEVEL_GOOD      4

// View_create arguments (12d Macro Manual 5.35)
#define VIEW_TYPE_PLAN          0
#define VIEW_ENGINE_GDI         1

// Create_process flags
#define CREATE_NEW_CONSOLE      16
#define CREATE_NO_WINDOW        134217728

//---------------------------------------------------------------------
// Limits and defaults
//---------------------------------------------------------------------
#define MAX_SHEETS              2000    // pages read from one plot sheet file
#define MAX_PAGE_DEPTH          16      // nesting of book / chapter / sheet pages
#define MAX_UV                  100     // title block user variables on one sheet

#define DEFAULT_DPI             "150"
#define DEFAULT_QUALITY         70
#define DEFAULT_OPACITY         50

#define FIELD_CHARS             60
#define STATUS_CHARS            80

// The engine files sit beside the compiled macro
#define ENGINE_PS               "FastPlot_compose.ps1"
#define ENGINE_CS               "FastPlotEngine.cs"
#define ENGINE_MON              "FastPlot_monitor.ps1"

// Temporary 12d objects made for one run and removed after it.  A hold
// model is named after the model its rasters came from, so an
// interrupted run is put right the next time the macro starts.
// Model names take letters, digits, spaces and "/" (folders) - 12d
// refuses to create "FastPlot frames (temporary)" or "FastPlot hold - x".
#define FRAMES_MODEL            "FastPlot/frames"
#define TEMP_VIEW_PREFIX        "FastPlot temp "
#define HOLD_PREFIX             "FastPlot/hold/"
#define OUTLINE_PREFIX          "FastPlot outline "

// How the aerial is kept out of 12d's plot
#define HIDE_MOVE               1       // rasters parked in a hold model; the view is untouched
#define HIDE_COPY               2       // a temporary copy of the view without raster-only models

// What a model in a sheet's view turns out to hold
#define MODEL_VECTOR            0       // no raster: drawn by 12d
#define MODEL_RASTER            1       // nothing but rasters
#define MODEL_MIXED             2       // rasters AND other data

//---------------------------------------------------------------------
// Global storage
//---------------------------------------------------------------------
{
    File    g_log;
    Integer g_log_open = 0;
    Text    g_tab = " ";

    // ---- the progress window: a file the macro and the engine append to
    File    g_prog;
    Integer g_prog_open = 0;

    // ---- string weights on the vector plots (0 = the pen widths only, as the MPS PDFs)
    Integer g_weights = 0;

    // ---- view plot scales changed for a run, put back afterwards
    Integer         g_n_vscale = 0;
    Dynamic_Text    g_vs_view;
    Dynamic_Real    g_vs_scale;

    // ---- book level, from the outermost page of the plot sheet file
    Text    g_book_name    = "";
    Text    g_book_target  = "";
    Text    g_book_paper   = "";
    Text    g_book_plotter = "";

    // ---- one entry per sheet (a page holding shapes), in book order
    Integer         g_n_sheets = 0;
    Dynamic_Text    g_sh_name;
    Dynamic_Text    g_sh_chapter;
    Dynamic_Text    g_sh_view;
    Dynamic_Text    g_sh_plotter;
    Dynamic_Text    g_sh_paper;
    Dynamic_Text    g_sh_tbf;
    Dynamic_Text    g_sh_tb_colour;
    Dynamic_Text    g_sh_problem;       // "" when the sheet can be plotted
    Dynamic_Text    g_sh_fmodel;        // plan frame outline: model, colour, linestyle, weight
    Dynamic_Text    g_sh_fcolour;
    Dynamic_Text    g_sh_fstyle;
    Dynamic_Real    g_sh_fweight;
    Dynamic_Real    g_sh_scale;
    Dynamic_Real    g_sh_rot;           // degrees, as stored in the file
    Dynamic_Real    g_sh_ox;
    Dynamic_Real    g_sh_oy;
    Dynamic_Real    g_sh_ml;
    Dynamic_Real    g_sh_mr;
    Dynamic_Real    g_sh_mb;
    Dynamic_Real    g_sh_mt;
    Dynamic_Real    g_sh_tb_size;
    Dynamic_Integer g_sh_active;
    Dynamic_Integer g_sh_uv_first;      // first slot in g_uv_*
    Dynamic_Integer g_sh_uv_count;

    // ---- title block user variables of every sheet, flattened
    Integer         g_n_uv = 0;
    Dynamic_Integer g_uv_index;
    Dynamic_Text    g_uv_alias;
    Dynamic_Text    g_uv_value;

    // ---- views prepared for this run
    Integer         g_n_views = 0;
    Dynamic_Text    g_vw_source;        // view named by the plot sheet
    Dynamic_Text    g_vw_plot;          // view 12d plots (a temporary copy without the aerial)
    Dynamic_Text    g_vw_rasters;       // engine raster ids, comma separated
    Dynamic_Text    g_vw_below;         // models drawn under the aerial, "|" separated
    Dynamic_Integer g_vw_made;          // 1 when g_vw_plot is ours to delete
    Dynamic_Integer g_vw_ok;            // 0 when the view could not be found
    Dynamic_Text    g_vw_models;        // its models in view order, "|" separated

    // ---- every model met in those views, classified once per run
    Integer         g_hide_mode = 1;
    Integer         g_n_models = 0;
    Dynamic_Text    g_md_name;
    Dynamic_Integer g_md_kind;          // MODEL_VECTOR / RASTER / MIXED
    Dynamic_Text    g_md_ids;           // rasters the engine draws for it, "R1,R2"
    Dynamic_Text    g_md_note;

    // ---- raster elements found in those views
    Integer         g_n_rasters = 0;
    Dynamic_Text    g_ras_model;
    Dynamic_Text    g_ras_element;
    Dynamic_Real    g_ras_xmin;
    Dynamic_Real    g_ras_ymin;
    Dynamic_Real    g_ras_xmax;
    Dynamic_Real    g_ras_ymax;
    Dynamic_Element g_ras_elt;          // the raster element itself
    Dynamic_Integer g_ras_ok;           // 1 once the engine has read its tiles
    Dynamic_Integer g_ras_held;         // 1 while parked in a hold model
    Dynamic_Text    g_ras_info;         // what the engine found
}

//---------------------------------------------------------------------
// Forward declarations
//---------------------------------------------------------------------
void    log_msg(Text msg);
Text    num(Real v);
Text    itxt(Integer v);
Text    get_text(Dynamic_Text &list, Integer i);
Real    get_real(Dynamic_Real &list, Integer i);
Integer get_int(Dynamic_Integer &list, Integer i);

#endif
