//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_process.H
//
// Brief description
// Check, and what Check and Plot share:
//   do_check   read the plot sheet file and report what would be
//              plotted and which aerials the engine will draw - makes
//              and moves nothing in the project
// Plot itself runs step by step from the panel (FastPlot_run.H).
//---------------------------------------------------------------------

#ifndef FASTPLOT_PROCESS_H
#define FASTPLOT_PROCESS_H

#define OUT_BOOK      1
#define OUT_PER_SHEET 2

Text pad3(Integer v)
{
    Text t = itxt(v);
    if(v < 10) return "00" + t;
    if(v < 100) return "0" + t;
    return t;
}

//---------------------------------------------------------------------
// pdf_auto_open
//   1 when 12d opens every PDF its plotter writes (Options > Plotting >
//   PDF > "Open file after plot", env AUTO_OPEN_PDF_PLOTS_4D, default 1).
//   A macro cannot change it, and with it on every sheet pops up a
//   viewer and the run crawls.
//---------------------------------------------------------------------
Integer pdf_auto_open()
{
    Text v = Text_justify(Getenv("AUTO_OPEN_PDF_PLOTS_4D"));
    if(v == "0") return 0;
    if(Text_lower(v) == "false") return 0;
    return 1;
}

void say_auto_open()
{
    say("12d opens every PDF it plots: untick Options > Plotting > PDF > \"Open file after plot\"");
    say("(env AUTO_OPEN_PDF_PLOTS_4D = 0) so FastPlot can run without a viewer per sheet.");
}

// The panel's line on that setting
void show_auto_open(Colour_Message_Box box)
{
    if(pdf_auto_open()) Set_data(box, "\"Open file after plot\" is ON - untick it before plotting more than 2 sheets", MESSAGE_LEVEL_WARNING);
    else Set_data(box, "\"Open file after plot\" is off (AUTO_OPEN_PDF_PLOTS_4D = 0) - ready", MESSAGE_LEVEL_GOOD);
}

// Default book PDF: the book's target name, beside the project
Text default_output()
{
    Text base = g_book_target;
    if(base == "") base = g_book_name;
    if(base == "") base = "FastPlot";
    Text s = stem_of(base);
    if(s == "") s = base;
    return project_folder() + "\\" + safe_file_name(s) + ".pdf";
}

// Shared plots: 12d plots areas of sheets at once and each title block
// variant once; the engine cuts the sheets out of them
#define MAX_AREA_MM        3000.0          // largest side of one area plot on paper (PDF allows 5080)
#define AREA_MARGIN_MM     10.0
#define TITLE_PLACEHOLDER  "1234567890"    // every digit, so the font carries them all

Text sheet_problem(Integer i);

// per-sheet file name: the sheet name, made unique
Text unique_out_name(Text name, Integer seq, Dynamic_Text &used)
{
    Text out_name = safe_file_name(name);
    Integer nu = 0;
    Get_number_of_items(used, nu);
    Integer u;
    for(u = 1; u <= nu; u++)
    {
        if(Text_lower(get_text(used, u)) == Text_lower(out_name)) out_name = out_name + " (" + itxt(seq) + ")";
    }
    Append(out_name, used);
    return out_name;
}

// Everything that makes a sheet's title block differ from another's, bar its number
Text title_key(Integer s)
{
    Text k = get_text(g_sh_tbf, s) + "|" + get_text(g_sh_paper, s) + "|" + get_text(g_sh_plotter, s) + "|" +
             get_text(g_sh_tb_colour, s) + "|" + num(get_real(g_sh_tb_size, s)) + "|" +
             num(get_real(g_sh_ml, s)) + "|" + num(get_real(g_sh_mb, s)) + "|" +
             num(get_real(g_sh_mr, s)) + "|" + num(get_real(g_sh_mt, s));
    Integer first = get_int(g_sh_uv_first, s);
    Integer count = get_int(g_sh_uv_count, s);
    Integer u;
    for(u = first; u < first + count; u++)
        k = k + "|" + itxt(get_int(g_uv_index, u)) + "=" + get_text(g_uv_value, u);
    return k;
}

//---------------------------------------------------------------------
// print_sheet_table
//---------------------------------------------------------------------
void print_sheet_table(Integer sel[])
{
    Integer i;
    for(i = 1; i <= g_n_sheets; i++)
    {
        Text mark = " ";
        if(sel[i]) mark = "*";
        Text prob = get_text(g_sh_problem, i);
        if(get_int(g_sh_active, i) == 0) prob = "inactive " + prob;
        Text line = mark + " " + pad3(i) + "  " + get_text(g_sh_chapter, i) + " / " + get_text(g_sh_name, i) +
                    "   view '" + get_text(g_sh_view, i) + "'  1:" + To_text(get_real(g_sh_scale, i), 0) +
                    "  centre " + To_text(get_real(g_sh_ox, i), 3) + ", " + To_text(get_real(g_sh_oy, i), 3);
        if(get_real(g_sh_rot, i) != 0.0) line = line + "  rotation " + To_text(get_real(g_sh_rot, i), 3);
        if(prob != "") line = line + "   ** " + prob;
        Print(line + "\n");
    }
}

// Sheet i can be plotted: "" or the reason it cannot
Text sheet_problem(Integer i)
{
    Text prob = get_text(g_sh_problem, i);
    if(get_int(g_sh_active, i) == 0) prob = "inactive in the plot sheet";
    return prob;
}

// The views of the selected sheets, their models and rasters
void study_views(Integer sel[], Text override_list)
{
    reset_views();
    Integer i;
    for(i = 1; i <= g_n_sheets; i++)
    {
        if(sel[i] == 0) continue;
        if(sheet_problem(i) != "") continue;
        Text err = "";
        study_view(get_text(g_sh_view, i), override_list, err);
    }
}

// Prints the views and rasters found, n_ok of them usable by the engine
void print_study(Integer n_ok)
{
    Integer i;
    Print("Views:\n");
    Integer v;
    for(v = 1; v <= g_n_views; v++)
    {
        Text src = get_text(g_vw_source, v);
        if(get_int(g_vw_ok, v) == 0)
        {
            Print("  '" + src + "': NOT FOUND - its sheets are skipped\n");
            continue;
        }
        Print("  '" + src + "': " + itxt(count_bar(get_text(g_vw_models, v))) + " models\n");
    }
    Integer m;
    for(m = 1; m <= g_n_models; m++)
    {
        Text note = get_text(g_md_note, m);
        if(note != "") Print("  model '" + get_text(g_md_name, m) + "': " + note + "\n");
    }
    if(g_n_rasters == 0)
    {
        Print("No aerial raster found in these views - 12d plots them as they are.\n");
        Print("(If there is one, name its model under Aerial models.)\n");
        return;
    }
    Print("Aerial rasters (" + itxt(n_ok) + " of " + itxt(g_n_rasters) + " usable by FastPlot):\n");
    for(i = 1; i <= g_n_rasters; i++)
    {
        Real w = get_real(g_ras_xmax, i) - get_real(g_ras_xmin, i);
        Real h = get_real(g_ras_ymax, i) - get_real(g_ras_ymin, i);
        Print("  R" + itxt(i) + "  '" + get_text(g_ras_element, i) + "' in model '" + get_text(g_ras_model, i) + "'  " +
              To_text(w, 1) + " x " + To_text(h, 1) + " m from " + To_text(get_real(g_ras_xmin, i), 3) + ", " +
              To_text(get_real(g_ras_ymin, i), 3) + "\n");
        Print("      " + get_text(g_ras_info, i) + "\n");
    }
}

//---------------------------------------------------------------------
// study_selection
//   Studies the views of the selected sheets and asks the engine about
//   their rasters, waiting for it.  Prints the views and rasters found.
//---------------------------------------------------------------------
void study_selection(Integer sel[], Text override_list, Text work)
{
    study_views(sel, override_list);
    print_study(probe_rasters(work));
}

//---------------------------------------------------------------------
// prepare_run - shared start of Check and Plot
//---------------------------------------------------------------------
Integer prepare_run(Text mpsf, Text spec, Integer sel[], Integer &n_sel, Text &work, Text &msg)
{
    Text err = "";
    if(prepare_engine(err) == 0)
    {
        msg = err;
        Print(err + "\n");
        return 1;
    }
    if(read_mpsf(mpsf, err) != 0)
    {
        msg = err;
        Print(err + "\n");
        return 2;
    }
    n_sel = parse_sheet_list(spec, g_n_sheets, sel);
    if(n_sel <= 0)
    {
        msg = "No sheets selected ('" + spec + "')";
        Print(msg + "\n");
        return 3;
    }
    // work folder, emptied each run; its creation time starts the clock
    work = temp_dir() + "\\FastPlot\\run";
    if(Directory_exists(work) != 0) Directory_delete_recursive(work);
    Directory_create_recursive(work);
    return 0;
}

//---------------------------------------------------------------------
// do_check
//---------------------------------------------------------------------
Integer do_check(Text mpsf, Text spec, Text override_list, Text &msg)
{
    Clear_console();
    Print(PROGRAM_NAME + " " + PROGRAM_VERSION + " - check\n");
    Print("Plot sheet file: " + mpsf + "\n");
    Print("Sheets:          " + spec + "\n");
    if(override_list != "") Print("Aerial models:   " + override_list + "\n");
    Print("\n");

    Integer sel[MAX_SHEETS];
    Integer n_sel = 0;
    Text work = "";
    if(prepare_run(mpsf, spec, sel, n_sel, work, msg) != 0)
    {
        Show_console(1);
        return 1;
    }
    Print("Book '" + g_book_name + "'  paper '" + g_book_paper + "'  plotter '" + g_book_plotter + "'\n");
    Print(itxt(g_n_sheets) + " sheets, " + itxt(n_sel) + " selected (*):\n");
    print_sheet_table(sel);
    Print("\n");
    study_selection(sel, override_list, work);
    Print("\n");
    if(pdf_auto_open()) say_auto_open();
    else Print("12d will not open the PDFs it plots - good, the run stays headless.\n");
    Show_console(1);
    Integer usable = 0;
    Integer i;
    for(i = 1; i <= g_n_rasters; i++) usable = usable + get_int(g_ras_ok, i);
    msg = itxt(n_sel) + " of " + itxt(g_n_sheets) + " sheets selected; " + itxt(usable) + " aerial(s) FastPlot can draw. See Output Window.";
    return 0;
}

#endif
