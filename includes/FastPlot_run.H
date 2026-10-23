//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_run.H
//
// Brief description
// A plot run as a list of steps, so 12d is not stuck for the whole run:
//
//   run_begin   reads the plot sheet, opens the progress window and
//               queues the first steps (quick)
//   run_tick    runs steps up to the next one that takes time - one 12d
//               plot, or starting a process - and returns.  The panel
//               lets 12d catch up (pump_ui) before the next tick.  The
//               raster probe and the final PDF run in the engine while
//               12d is free: a tick only checks whether they are done.
//
// The engine finishes the run on its own: it reports to the progress
// window, ends it, and opens the PDF when asked to.
//---------------------------------------------------------------------

#ifndef FASTPLOT_RUN_H
#define FASTPLOT_RUN_H

#define RUN_IDLE        0
#define RUN_STEPS       1       // 12d's part
#define RUN_ENGINE      2       // the engine writes the PDF, 12d is free

#define ST_STUDY        1       // views, models, rasters; start the raster probe
#define ST_PARK         2       // probe answer, aerial parked, outlines drawn, plots planned
#define ST_AREA         3       // one 12d plot of an area of sheets
#define ST_TITLE        4       // one 12d plot of a title block variant
#define ST_SHARED       5       // the sheets cut from those plots; queue the rest
#define ST_SHEET        6       // one 12d plot of a sheet on its own
#define ST_RESTORE      7       // aerial back in its model (before rotated sheets)
#define ST_FINISH       8       // clean up, start the engine

#define STEP_ON         0       // carry on with the next step
#define STEP_YIELD      1       // took time (or started a process): back to the panel
#define STEP_FAILED     2       // the run is over, g_r_fail says why

#define TICK_BUSY       0
#define TICK_DONE       1
#define TICK_FAILED     2

{
    Integer         g_run = 0;
    Integer         g_step = 0;
    Integer         g_n_steps = 0;
    Dynamic_Integer g_st_kind;
    Dynamic_Integer g_st_ref;

    File            g_jf;
    Integer         g_jf_open = 0;
    Model           g_frames;
    Process_Handle  g_proc;             // the probe, then the engine
    Integer         g_proc_live = 0;    // 1 while g_proc is a process started without waiting

    Text            g_r_work = "";
    Text            g_r_job = "";
    Text            g_r_result = "";
    Text            g_r_output = "";
    Text            g_r_outdir = "";
    Text            g_r_override = "";
    Text            g_r_progress = "";
    Text            g_r_fail = "";
    Integer         g_r_outmode = 1;
    Integer         g_r_dpi = 150;
    Integer         g_r_quality = 70;
    Integer         g_r_opacity = 50;
    Integer         g_r_grey = 0;
    Integer         g_r_shrink = 1;
    Integer         g_r_open = 0;
    Integer         g_r_plotted = 0;
    Integer         g_r_skipped = 0;
    Integer         g_r_nsel = 0;

    Integer         g_sel[MAX_SHEETS];
    Integer         g_done[MAX_SHEETS];
    Integer         g_cl_of[MAX_SHEETS];
    Integer         g_tv_of[MAX_SHEETS];

    Integer         g_n_cl = 0;
    Dynamic_Text    g_cl_view;
    Dynamic_Real    g_cl_scale;
    Dynamic_Real    g_cl_x0;
    Dynamic_Real    g_cl_y0;
    Dynamic_Real    g_cl_x1;
    Dynamic_Real    g_cl_y1;
    Dynamic_Integer g_cl_first;
    Dynamic_Integer g_cl_ok;
    Dynamic_Integer g_cl_cnt;

    Integer         g_n_tv = 0;
    Dynamic_Text    g_tv_key;
    Dynamic_Integer g_tv_sheet;
    Dynamic_Integer g_tv_ok;
    Dynamic_Integer g_tv_cnt;

    Dynamic_Text    g_used_names;
}

void add_step(Integer kind, Integer ref)
{
    g_n_steps++;
    Append(kind, g_st_kind);
    Append(ref, g_st_ref);
}

// The step counter shown on the panel
Text step_text()
{
    return "step " + itxt(g_step - 1) + " of " + itxt(g_n_steps);
}

// 1 while the probe or the engine is still running
Integer proc_running()
{
    if(g_proc_live == 0) return 0;
    if(Process_exists(g_proc) != 0) return 1;
    g_proc_live = 0;
    return 0;
}

//---------------------------------------------------------------------
// plan_shared
//   Groups the selected sheets into areas (same view and scale, at most
//   MAX_AREA_MM of paper a side) and title block variants.  12d spends
//   ~3 s on every plot whatever is in it, so one plot per area and one
//   per title block beats one per sheet; the engine cuts the sheets out.
//---------------------------------------------------------------------
void plan_shared()
{
    Integer i;
    g_n_cl = 0;
    g_n_tv = 0;
    Null(g_cl_view);
    Null(g_cl_scale);
    Null(g_cl_x0);
    Null(g_cl_y0);
    Null(g_cl_x1);
    Null(g_cl_y1);
    Null(g_cl_first);
    Null(g_cl_ok);
    Null(g_cl_cnt);
    Null(g_tv_key);
    Null(g_tv_sheet);
    Null(g_tv_ok);
    Null(g_tv_cnt);
    for(i = 1; i <= MAX_SHEETS; i++)
    {
        g_done[i] = 0;
        g_cl_of[i] = 0;
        g_tv_of[i] = 0;
    }
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(g_sel[i] == 0) continue;
        if(sheet_problem(i) != "") continue;
        if(get_real(g_sh_rot, i) != 0.0) continue;
        Real pw = 0.0, ph = 0.0;
        if(paper_size(get_text(g_sh_paper, i), pw, ph) != 0) continue;
        Text err = "";
        Integer slot = study_view(get_text(g_sh_view, i), g_r_override, err);
        if(err != "") continue;
        Text view = get_text(g_vw_plot, slot);
        Real scale = get_real(g_sh_scale, i);
        Real k = scale / 1000.0;
        Real hw = (pw - get_real(g_sh_ml, i) - get_real(g_sh_mr, i)) / 2.0 * k;
        Real hh = (ph - get_real(g_sh_mb, i) - get_real(g_sh_mt, i)) / 2.0 * k;
        Real ax = get_real(g_sh_ox, i) - hw, bx = get_real(g_sh_ox, i) + hw;
        Real ay = get_real(g_sh_oy, i) - hh, by = get_real(g_sh_oy, i) + hh;
        Real lim = MAX_AREA_MM * k;
        Integer c, found = 0;
        for(c = 1; c <= g_n_cl; c++)
        {
            if(found > 0) break;
            if(get_text(g_cl_view, c) != view) continue;
            if(Absolute(get_real(g_cl_scale, c) - scale) > 0.000001) continue;
            Real x0 = get_real(g_cl_x0, c), y0 = get_real(g_cl_y0, c);
            Real x1 = get_real(g_cl_x1, c), y1 = get_real(g_cl_y1, c);
            if(ax < x0) x0 = ax;
            if(ay < y0) y0 = ay;
            if(bx > x1) x1 = bx;
            if(by > y1) y1 = by;
            if(x1 - x0 > lim) continue;
            if(y1 - y0 > lim) continue;
            Set_item(g_cl_x0, c, x0);
            Set_item(g_cl_y0, c, y0);
            Set_item(g_cl_x1, c, x1);
            Set_item(g_cl_y1, c, y1);
            found = c;
        }
        if(found == 0)
        {
            g_n_cl++;
            Append(view, g_cl_view);
            Append(scale, g_cl_scale);
            Append(ax, g_cl_x0);
            Append(ay, g_cl_y0);
            Append(bx, g_cl_x1);
            Append(by, g_cl_y1);
            Append(i, g_cl_first);
            Append(0, g_cl_ok);
            Append(0, g_cl_cnt);
            found = g_n_cl;
        }
        g_cl_of[i] = found;
        Set_item(g_cl_cnt, found, get_int(g_cl_cnt, found) + 1);

        if(get_text(g_sh_tbf, i) != "")
        {
            Text key = title_key(i);
            Integer t, tf = 0;
            for(t = 1; t <= g_n_tv; t++)
            {
                if(tf == 0)
                {
                    if(get_text(g_tv_key, t) == key) tf = t;
                }
            }
            if(tf == 0)
            {
                g_n_tv++;
                Append(key, g_tv_key);
                Append(i, g_tv_sheet);
                Append(0, g_tv_ok);
                Append(0, g_tv_cnt);
                tf = g_n_tv;
            }
            g_tv_of[i] = tf;
            Set_item(g_tv_cnt, tf, get_int(g_tv_cnt, tf) + 1);
        }
    }

    // a sheet alone in its area and with its title block (a cover page)
    // costs one ordinary plot rather than two shared ones - unless string
    // weights are off: the title block must then be plotted apart, with
    // its own weights
    if(g_weights == 0) return;
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(g_cl_of[i] == 0) continue;
        if(get_int(g_cl_cnt, g_cl_of[i]) != 1) continue;
        if(g_tv_of[i] > 0)
        {
            if(get_int(g_tv_cnt, g_tv_of[i]) != 1) continue;
            Set_item(g_tv_cnt, g_tv_of[i], 0);
        }
        Set_item(g_cl_cnt, g_cl_of[i], 0);
        g_cl_of[i] = 0;
        g_tv_of[i] = 0;
    }
}

//---------------------------------------------------------------------
// run_cleanup - everything a run changed in the project goes back
//---------------------------------------------------------------------
void run_cleanup()
{
    if(g_jf_open)
    {
        File_close(g_jf);
        g_jf_open = 0;
    }
    Integer home = restore_rasters();
    if(home > 0) say("Aerial(s) moved back into their models (" + itxt(home) + " element(s)).");
    remove_temp_views();
    remove_outlines();
    restore_view_scales();
    if(Model_exists(g_frames) != 0) Model_delete(g_frames);
}

// The run stops here: project put back, progress window told
Integer run_fail(Text msg)
{
    g_r_fail = msg;
    say(msg);
    run_cleanup();
    prog_finish("FAILED: " + msg);
    Show_console(1);
    g_run = RUN_IDLE;
    return STEP_FAILED;
}

//---------------------------------------------------------------------
// run_begin
//   Returns 0 when the run has started (run_tick takes it from there);
//   otherwise the run is over and msg says why.
//---------------------------------------------------------------------
Integer run_begin(Colour_Message_Box status, Text mpsf, Text spec, Text override_list, Integer hide_mode, Integer out_mode,
                  Text &output, Integer dpi, Integer quality, Integer opacity, Integer grey, Integer shrink_images,
                  Integer weights, Integer open_pdf, Text &msg)
{
    g_run = RUN_IDLE;
    g_proc_live = 0;
    g_jf_open = 0;
    g_weights = weights;
    g_hide_mode = hide_mode;
    g_r_override = override_list;
    g_r_outmode = out_mode;
    g_r_dpi = dpi;
    g_r_quality = quality;
    g_r_opacity = opacity;
    g_r_grey = grey;
    g_r_shrink = shrink_images;
    g_r_open = open_pdf;
    g_r_plotted = 0;
    g_r_skipped = 0;
    g_r_fail = "";
    Null(g_used_names);
    Null(g_frames);
    Clear_console();
    Print(PROGRAM_NAME + " " + PROGRAM_VERSION + " - plot\n");
    Print("Plot sheet file: " + mpsf + "\n");
    Print("Sheets:          " + spec + "\n");
    Print("Aerial:          " + itxt(dpi) + " dpi max, JPEG quality " + itxt(quality) + ", opacity " + itxt(opacity) + "%");
    if(grey) Print(", greyscale");
    Print("\n");

    // an interrupted run may have left aerials parked or outlines drawn
    Integer back = restore_rasters();
    if(back > 0) Print("Moved " + itxt(back) + " aerial element(s) back from an earlier interrupted run.\n");
    remove_outlines();

    Integer n_sel = 0;
    Text work = "";
    if(prepare_run(mpsf, spec, g_sel, n_sel, work, msg) != 0)
    {
        Show_console(1);
        return 1;
    }
    g_r_nsel = n_sel;
    g_r_work = work;
    // 12d would open a viewer for every sheet: allow a short test, refuse a book
    if(pdf_auto_open())
    {
        say_auto_open();
        if(n_sel > 2)
        {
            msg = "Untick 12d's \"Open file after plot\" first (see Output Window), or plot at most 2 sheets";
            Show_console(1);
            return 8;
        }
    }
    if(output == "") output = default_output();
    // a File_Box hands back a bare name for a file in the project folder
    if(dir_of(output) == "") output = project_folder() + "\\" + output;
    if(Text_lower(Get_subtext(output, Text_length(output) - 3, Text_length(output))) != ".pdf") output = output + ".pdf";
    Text out_dir = dir_of(output);
    if(out_dir == "") out_dir = project_folder();
    if(Directory_exists(out_dir) == 0) Directory_create_recursive(out_dir);
    g_r_output = output;
    g_r_outdir = out_dir;
    if(out_mode == OUT_BOOK) Print("Output:          " + output + "\n");
    else Print("Output folder:   " + out_dir + "\n");
    Print("\n");

    // the progress window (outside the work folder, which a release build deletes)
    g_r_progress = temp_dir() + "\\FastPlot\\progress.txt";
    if(File_exists(g_r_progress) != 0) File_delete(g_r_progress);
    if(prog_open(g_r_progress) == 0) launch_monitor(g_r_progress, work);
    prog("Plot sheet: " + file_of(mpsf) + " - " + itxt(n_sel) + " of " + itxt(g_n_sheets) + " sheet(s)");
    if(weights) prog("String weights: drawn, with each view's plot scale set to the plot's scale");
    else prog("String weights: off (pen widths only, as the Multi Plot Sheet PDFs)");

    g_r_job = work + "\\job.txt";
    g_r_result = work + "\\result.txt";
    g_step = 1;
    g_n_steps = 0;
    Null(g_st_kind);
    Null(g_st_ref);
    add_step(ST_STUDY, 0);
    add_step(ST_PARK, 0);
    g_run = RUN_STEPS;
    msg = "Plotting - see the FastPlot window";
    return 0;
}

//---------------------------------------------------------------------
// One sheet plotted by 12d on its own (rotated, alone, or its shared
// plot failed)
//---------------------------------------------------------------------
Integer run_own_sheet(Colour_Message_Box status, Integer i)
{
    Integer seq = i;   // the drawing number is the sheet's place in the book
    Text name = get_text(g_sh_name, i);
    Text prob = sheet_problem(i);
    if(prob != "")
    {
        g_r_skipped++;
        say("  " + pad3(seq) + "  " + name + "  SKIPPED: " + prob);
        return STEP_ON;
    }
    Text err = "";
    Integer slot = study_view(get_text(g_sh_view, i), g_r_override, err);
    if(err != "")
    {
        g_r_skipped++;
        say("  " + pad3(seq) + "  " + name + "  SKIPPED: " + err);
        return STEP_ON;
    }
    Integer rotated = 0;
    if(get_real(g_sh_rot, i) != 0.0) rotated = 1;
    Text plot_view = get_text(g_vw_plot, slot);
    Text ids = get_text(g_vw_rasters, slot);
    Text below = get_text(g_vw_below, slot);
    if(rotated)
    {
        // rotated sheets are not composed: the aerial is back and 12d draws it
        plot_view = get_text(g_vw_source, slot);
        ids = "";
        below = "";
    }
    Set_data(status, "12d plotting sheet " + name + " (" + step_text() + ")...", MESSAGE_LEVEL_GENERAL);
    prog("12d is plotting sheet " + name + " on its own");

    // The stem is unique per run and sheet: should 12d drop the folder
    // part, the engine still finds the file in the project folder.
    Text sheet_dir = g_r_work + "\\s" + pad3(seq);
    Directory_create_recursive(sheet_dir);
    Text stem = sheet_dir + "\\FastPlot_s" + pad3(seq);
    Real w = 0.0, h = 0.0;
    Integer pr = plot_sheet(i, seq, g_frames, plot_view, stem, w, h, err);
    if(pr != 0)
    {
        g_r_skipped++;
        say("  " + pad3(seq) + "  " + name + "  FAILED: " + err);
        return STEP_YIELD;
    }
#ifdef DEBUG
    log_msg("    vector pdf at stem: " + itxt(File_exists(stem + ".pdf")));
#endif
    Text out_name = unique_out_name(name, seq, g_used_names);
    write_sheet_record(g_jf, i, seq, sheet_dir, out_name + ".pdf", w, h, ids, below, "", "");
    g_r_plotted++;
    if(rotated) Print("  " + pad3(seq) + "  " + name + "  plotted by 12d with its aerial (rotated sheet)\n");
    else Print("  " + pad3(seq) + "  " + name + "  plotted by 12d\n");
    return STEP_YIELD;
}

//---------------------------------------------------------------------
// run_study - which aerials the engine can draw: the views are studied
// here, the tiles checked by the engine while 12d is free
//---------------------------------------------------------------------
Integer run_study(Colour_Message_Box status)
{
    Set_data(status, "Checking views and aerials...", MESSAGE_LEVEL_GENERAL);
    prog("Checking views and aerials...");
    study_views(g_sel, g_r_override);
    Text job = probe_job(g_r_work);
    if(job == "") return STEP_ON;
    if(run_engine_async(job, g_r_work, g_proc) == 0)
    {
        g_proc_live = 1;
        return STEP_YIELD;
    }
    run_engine(job, g_r_work, CREATE_NO_WINDOW);
    return STEP_ON;
}

//---------------------------------------------------------------------
// run_park - the aerial out of 12d's way, the outlines drawn and the
// 12d plots queued
//---------------------------------------------------------------------
Integer run_park(Colour_Message_Box status)
{
    print_study(probe_read(g_r_work));
    Print("\n");
    hide_rasters(1);
    Integer n_held = 0;
    Integer hr;
    for(hr = 1; hr <= g_n_rasters; hr++) n_held = n_held + get_int(g_ras_held, hr);
    if(n_held > 0) prog("Aerial parked out of 12d's way (" + itxt(n_held) + " raster) - FastPlot draws it");

    if(Model_exists(FRAMES_MODEL) != 0)
    {
        Model old = Get_model(FRAMES_MODEL);
        Model_delete(old);
    }
    g_frames = Get_model_create(FRAMES_MODEL);
    // a plot frame outside a model cannot be plotted - stop before plotting anything
    if(Model_exists(g_frames) == 0) return run_fail("Cannot create the model '" + FRAMES_MODEL + "' for the plot frames");

    // the sheet outlines the Multi Plot Sheet plotter draws into each plan
    // frame's frame model (the dashed key map on the cover pages)
    Integer n_outlines = draw_outlines();
    if(n_outlines > 0) say("Drew " + itxt(n_outlines) + " sheet outline(s) for the plot.");

    if(open_job(g_r_job, g_jf) != 0) return run_fail("Cannot write " + g_r_job);
    g_jf_open = 1;

    plan_shared();
    Integer c, t, n_area = 0, n_title = 0;
    for(c = 1; c <= g_n_cl; c++)
    {
        if(get_int(g_cl_cnt, c) == 0) continue;
        add_step(ST_AREA, c);
        n_area++;
    }
    for(t = 1; t <= g_n_tv; t++)
    {
        if(get_int(g_tv_cnt, t) == 0) continue;
        add_step(ST_TITLE, t);
        n_title++;
    }
    add_step(ST_SHARED, 0);
    if(n_area > 0) say("Plotting " + itxt(n_area) + " area(s) and " + itxt(n_title) + " title block(s) in 12d instead of one plot per sheet.");
    return STEP_YIELD;
}

// One 12d plot of every sheet's vectors in area c
Integer run_area(Colour_Message_Box status, Integer c)
{
    Real scale = get_real(g_cl_scale, c);
    Real k = scale / 1000.0;
    Real x0 = get_real(g_cl_x0, c), y0 = get_real(g_cl_y0, c);
    Real x1 = get_real(g_cl_x1, c), y1 = get_real(g_cl_y1, c);
    Real pw = (x1 - x0) / k + 2.0 * AREA_MARGIN_MM;
    Real ph = (y1 - y0) / k + 2.0 * AREA_MARGIN_MM;
    Text dir = g_r_work + "\\v" + pad3(c);
    Directory_create_recursive(dir);
    Integer s1 = get_int(g_cl_first, c);
    Set_data(status, "12d plotting area " + itxt(c) + " (" + step_text() + ")...", MESSAGE_LEVEL_GENERAL);
    prog("12d is plotting area " + itxt(c) + " of " + itxt(g_n_cl) + " (" + To_text(pw / 1000.0, 1) + " x " + To_text(ph / 1000.0, 1) +
         " m of paper at 1:" + To_text(scale, 0) + ")");
    Real w = 0.0, h = 0.0;
    Text err = "";
    Integer r = plot_frame(1000 + c, g_frames, "", pw, ph, AREA_MARGIN_MM, AREA_MARGIN_MM, AREA_MARGIN_MM, AREA_MARGIN_MM,
                           scale, 0.0, (x0 + x1) / 2.0, (y0 + y1) / 2.0, 0, "1",
                           vector_plotter(get_text(g_sh_plotter, s1)), get_text(g_cl_view, c), dir + "\\FastPlot_v" + pad3(c), w, h, err);
    if(r != 0)
    {
        say("  area " + itxt(c) + " FAILED: " + err + " - its sheets are plotted one by one");
        return STEP_YIELD;
    }
    Set_item(g_cl_ok, c, 1);
    File_write_line(g_jf, "vplot" + g_tab + "V" + itxt(c) + g_tab + dir + g_tab + num(scale) + g_tab +
                          num(x0) + g_tab + num(y0) + g_tab + num(x1) + g_tab + num(y1) + g_tab +
                          num(AREA_MARGIN_MM) + g_tab + num(AREA_MARGIN_MM) + g_tab + num(AREA_MARGIN_MM) + g_tab +
                          num(AREA_MARGIN_MM) + g_tab + num(w) + g_tab + num(h));
    say("  area " + itxt(c) + ": view '" + get_text(g_cl_view, c) + "' 1:" + To_text(scale, 0) + ", " +
        To_text(w, 0) + " x " + To_text(h, 0) + " mm plotted by 12d");
    return STEP_YIELD;
}

// One 12d plot of title block variant t, with a placeholder drawing number
Integer run_title(Colour_Message_Box status, Integer t)
{
    Integer s = get_int(g_tv_sheet, t);
    Text err = "";
    Integer slot = study_view(get_text(g_sh_view, s), g_r_override, err);
    Text dir = g_r_work + "\\t" + pad3(t);
    Directory_create_recursive(dir);
    Set_data(status, "12d plotting title block " + itxt(t) + " (" + step_text() + ")...", MESSAGE_LEVEL_GENERAL);
    prog("12d is plotting title block " + itxt(t) + " of " + itxt(g_n_tv));
    Real w = 0.0, h = 0.0;
    // the viewport is put 1000 km away so nothing but the title block is drawn
    Integer r = plot_frame(2000 + t, g_frames, get_text(g_sh_paper, s), 0.0, 0.0,
                           get_real(g_sh_ml, s), get_real(g_sh_mb, s), get_real(g_sh_mr, s), get_real(g_sh_mt, s),
                           get_real(g_sh_scale, s), 0.0, get_real(g_sh_ox, s) + 1000000.0, get_real(g_sh_oy, s) + 1000000.0,
                           s, TITLE_PLACEHOLDER, get_text(g_sh_plotter, s), get_text(g_vw_plot, slot),
                           dir + "\\FastPlot_t" + pad3(t), w, h, err);
    if(r != 0)
    {
        say("  title block " + itxt(t) + " FAILED: " + err + " - its sheets are plotted one by one");
        return STEP_YIELD;
    }
    Set_item(g_tv_ok, t, 1);
    File_write_line(g_jf, "tplot" + g_tab + "T" + itxt(t) + g_tab + dir + g_tab + TITLE_PLACEHOLDER);
    say("  title block " + itxt(t) + " (" + file_of(get_text(g_sh_tbf, s)) + ") plotted by 12d");
    return STEP_YIELD;
}

// The sheets cut from the shared plots, then every other sheet queued
Integer run_shared()
{
    Integer i, n = 0;
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(g_cl_of[i] == 0) continue;
        if(get_int(g_cl_ok, g_cl_of[i]) == 0) continue;
        Text tid = "";
        if(g_tv_of[i] > 0)
        {
            if(get_int(g_tv_ok, g_tv_of[i]) == 0) continue;
            tid = "T" + itxt(g_tv_of[i]);
        }
        Text err = "";
        Integer slot = study_view(get_text(g_sh_view, i), g_r_override, err);
        Real pw = 0.0, ph = 0.0;
        paper_size(get_text(g_sh_paper, i), pw, ph);
        Text out_name = unique_out_name(get_text(g_sh_name, i), i, g_used_names);
        write_sheet_record(g_jf, i, i, "", out_name + ".pdf", pw, ph, get_text(g_vw_rasters, slot), get_text(g_vw_below, slot),
                           "V" + itxt(g_cl_of[i]), tid);
        g_done[i] = 1;
        n++;
    }
    g_r_plotted = g_r_plotted + n;
    if(n > 0) say(itxt(n) + " sheet(s) will be cut from the shared plots.");

    // unrotated sheets while the aerial is parked, rotated ones once it is back
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(g_sel[i] == 0) continue;
        if(g_done[i]) continue;
        if(get_real(g_sh_rot, i) != 0.0) continue;
        add_step(ST_SHEET, i);
    }
    add_step(ST_RESTORE, 0);
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(g_sel[i] == 0) continue;
        if(get_real(g_sh_rot, i) == 0.0) continue;
        add_step(ST_SHEET, i);
    }
    add_step(ST_FINISH, 0);
    return STEP_ON;
}

//---------------------------------------------------------------------
// run_finish - 12d's part is over: project put back, job completed and
// the engine started without waiting for it
//---------------------------------------------------------------------
Integer run_finish(Colour_Message_Box status)
{
    remove_temp_views();
    remove_outlines();
    restore_view_scales();
    if(Model_exists(g_frames) != 0) Model_delete(g_frames);

    Text out_spec = g_r_output;
    Text out_mode_t = "single";
    if(g_r_outmode == OUT_PER_SHEET)
    {
        out_spec = g_r_outdir;
        out_mode_t = "per_sheet";
    }
    write_raster_records(g_jf, project_folder(), 1);
    Text title = g_book_name;
    if(g_book_target != "") title = stem_of(g_book_target);
    write_job_settings(g_jf, "compose", out_mode_t, out_spec, g_r_dpi, g_r_quality, g_r_opacity, g_r_grey,
                       title, g_r_result, temp_dir() + "\\FastPlot\\engine.log");
    job_line(g_jf, "fallback_dir", project_folder());
    job_line(g_jf, "shrink_images", itxt(g_r_shrink));
    job_line(g_jf, "progress", g_r_progress);
    job_line(g_jf, "open_when_done", itxt(g_r_open));
    File_close(g_jf);
    g_jf_open = 0;
    if(g_r_plotted == 0) return run_fail("No sheet was plotted - see Output Window");

    Print("\n12d plotted " + itxt(g_r_plotted) + " sheet(s); the engine is adding the aerials and writing the PDF.\n");
    Show_console(1);
    Text note = "";
    if(g_r_skipped > 0) note = ", " + itxt(g_r_skipped) + " skipped or failed";
    prog("12d's part is done (" + itxt(g_r_plotted) + " sheet(s)" + note + ") - 12d is free again");
    prog("The engine is adding the aerials and writing the PDF");
    Set_data(status, "12d is free - the PDF is being written (see the FastPlot window)", MESSAGE_LEVEL_GENERAL);
    if(run_engine_async(g_r_job, g_r_work, g_proc) == 0)
    {
        // the engine reports to the progress window and ends it
        g_proc_live = 1;
        prog_close();
    }
    else
    {
        say("Could not start the engine without waiting - running it now.");
        run_engine(g_r_job, g_r_work, CREATE_NO_WINDOW);
    }
    g_run = RUN_ENGINE;
    return STEP_YIELD;
}

Integer run_step(Colour_Message_Box status, Integer kind, Integer ref)
{
    if(kind == ST_STUDY) return run_study(status);
    if(kind == ST_PARK) return run_park(status);
    if(kind == ST_AREA) return run_area(status, ref);
    if(kind == ST_TITLE) return run_title(status, ref);
    if(kind == ST_SHARED) return run_shared();
    if(kind == ST_SHEET) return run_own_sheet(status, ref);
    if(kind == ST_RESTORE)
    {
        // the aerials go home before the rotated sheets, which 12d draws with theirs
        Integer home = restore_rasters();
        if(home > 0) say("Aerial(s) moved back into their models (" + itxt(home) + " element(s)).");
        return STEP_ON;
    }
    if(kind == ST_FINISH) return run_finish(status);
    return STEP_ON;
}

//---------------------------------------------------------------------
// run_engine_done - the engine has finished: its result to the Output
// Window, the progress window ended should the engine not have
//---------------------------------------------------------------------
Integer run_engine_done(Text &main_file, Text &msg)
{
    g_run = RUN_IDLE;
    if(prog_open(g_r_progress) == 0) prog_finish("");
    Real mb = 0.0;
    Integer n_err = 0;
    Text rmsg = "";
    Print("\n");
    Integer rr = report_result(g_r_result, main_file, mb, n_err, rmsg);
    Show_console(1);
    if(rr != 0)
    {
        msg = rmsg;
        if(msg == "") msg = "The PDF engine failed - see Output Window";
        log_msg("engine failed: " + msg);
        return TICK_FAILED;
    }
#ifndef DEBUG
    Directory_delete_recursive(g_r_work);
#endif
    if(g_r_outmode == OUT_PER_SHEET) main_file = g_r_outdir;   // opening it shows the folder
    msg = "Done: " + itxt(g_r_plotted - n_err) + " sheet(s), " + To_text(mb, 1) + " MB";
    if(g_r_skipped + n_err > 0) msg = msg + " - " + itxt(g_r_skipped + n_err) + " skipped/failed, see Output Window";
    log_msg(msg);
    return TICK_DONE;
}

//---------------------------------------------------------------------
// run_tick
//   12d's part: the steps up to and including the next one that takes
//   time.  The engine's part: whether it has finished.  Returns
//   TICK_BUSY, TICK_DONE (main_file set) or TICK_FAILED (msg set).
//---------------------------------------------------------------------
Integer run_tick(Colour_Message_Box status, Text &main_file, Text &msg)
{
    main_file = "";
    if(g_run == RUN_ENGINE)
    {
        if(proc_running()) return TICK_BUSY;
        return run_engine_done(main_file, msg);
    }
    if(g_run != RUN_STEPS) return TICK_DONE;
    if(proc_running()) return TICK_BUSY;    // the raster probe
    while(g_run == RUN_STEPS)
    {
        if(g_step > g_n_steps)
        {
            // every run ends in ST_FINISH, so this is a bug
            run_fail("The run ended without finishing - see Output Window");
            break;
        }
        Integer kind = get_int(g_st_kind, g_step);
        Integer ref = get_int(g_st_ref, g_step);
        g_step++;
#ifdef DEBUG
        log_msg("step " + itxt(g_step - 1) + "/" + itxt(g_n_steps) + ": kind " + itxt(kind) + " ref " + itxt(ref));
        Real t0 = clock_s();
#endif
        Integer sr = run_step(status, kind, ref);
#ifdef DEBUG
        log_msg("  step " + itxt(g_step - 1) + " took " + To_text(clock_s() - t0, 2) + " s");
#endif
        if(sr == STEP_YIELD) return TICK_BUSY;
    }
    if(g_run == RUN_ENGINE) return TICK_BUSY;
    msg = g_r_fail;
    return TICK_FAILED;
}

#endif
