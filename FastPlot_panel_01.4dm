//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// Macro Name           FastPlot_panel_01
// Type                 0
//
// Brief description
// Fast, small PDF plots of a Multi Plot Sheet (.12dmpsf).
//
// 12d still draws every sheet - linestyles, symbols, text, drafting and
// the title block come from its own plot engine - but without the
// aerial, which is what makes the built-in plotter slow and its PDFs
// huge.  FastPlot's engine then draws the aerial straight from the
// raster's tiles at the resolution and JPEG quality chosen here, shrinks
// any other images to match, and writes one PDF for the book (layers,
// bookmarks) or one per sheet.
//
// The .4do is the only file to deploy: it carries the engine
// (FastPlotEngine.cs + FastPlot_compose.ps1, see build_embed.ps1) and
// writes it to %TEMP%\FastPlot\engine when it runs.
//
// A plot runs step by step (FastPlot_run.H); 12d is busy only for its
// own plots, and the engine writes the PDF while 12d is free.
//---------------------------------------------------------------------

#include "includes/FastPlot_globals.H"
#include "includes/FastPlot_utils.H"
#include "includes/FastPlot_mpsf.H"
#include "includes/FastPlot_plot.H"
#include "includes/FastPlot_embed.H"
#include "includes/FastPlot_job.H"
#include "includes/FastPlot_process.H"
#include "includes/FastPlot_run.H"
#include "includes/FastPlot_help.H"

#define DPI_COUNT 6
#define WAIT_MS 50          // between looks at the raster probe

//---------------------------------------------------------------------
// read_int_box
//   Integer typed in an Input_Box, clamped; the default when blank or
//   not a number.  Get_data does not move the cursor like Validate.
//---------------------------------------------------------------------
Integer read_int_box(Input_Box box, Integer dflt, Integer lo, Integer hi)
{
    Text t = "";
    Get_data(box, t);
    Integer v = dflt;
    if(From_text(Text_justify(t), v) != 0) v = dflt;
    if(v < lo) v = lo;
    if(v > hi) v = hi;
    return v;
}

void manage_panel()
{
    Panel panel = Create_panel(PROGRAM_NAME + " - fast Multi Plot Sheet PDFs");
    Message_Box message = Create_message_box(" ");

    // ---------------- Plot sheet ----------------
    Vertical_Group src_group = Create_vertical_group(ALL_WIDGETS_OWN_HEIGHT);
    Set_border(src_group, "Plot sheet");

    File_Box mpsf_box = Create_file_box("Plot sheet file", message, CHECK_FILE_MUST_EXIST, "*.12dmpsf");
    Set_width_in_chars(mpsf_box, FIELD_CHARS);

    // By place in the book, so a test of one sheet is "2"
    Input_Box sheets_box = Create_input_box("Sheets (all or 1-5,8)", message);
    Set_data(sheets_box, "all");
    Set_width_in_chars(sheets_box, FIELD_CHARS);

    Append(mpsf_box,   src_group);
    Append(sheets_box, src_group);

    // ---------------- Output ----------------
    Vertical_Group out_group = Create_vertical_group(ALL_WIDGETS_OWN_HEIGHT);
    Set_border(out_group, "Output");

    Choice_Box mode_box = Create_choice_box("Write", message);
    Text modes[2];
    modes[1] = "One PDF for the whole book";
    modes[2] = "One PDF per sheet";
    Set_data(mode_box, 2, modes);
    Set_data(mode_box, modes[1]);
    Set_width_in_chars(mode_box, FIELD_CHARS);

    // Left blank it becomes the book's target name beside the project.
    // Never filled in by the macro: it holds only what the user typed.
    File_Box out_box = Create_file_box("PDF file", message, CHECK_FILE_CREATE, "*.pdf");
    Set_optional(out_box, 1);
    Set_width_in_chars(out_box, FIELD_CHARS);

    Named_Tick_Box open_tick = Create_named_tick_box("Open the PDF when finished", 1, "open_pdf");

    Append(mode_box,  out_group);
    Append(out_box,   out_group);
    Append(open_tick, out_group);

    // ---------------- Aerial ----------------
    //
    // The resolution is a ceiling: an aerial is never resampled above
    // its own pixel size, so 1:500 sheets of a 0.115 m aerial come out
    // at about 110 dpi whatever is chosen.  Cover pages are where it
    // matters - the built-in plotter embeds them at about 1400 dpi.
    Vertical_Group img_group = Create_vertical_group(ALL_WIDGETS_OWN_HEIGHT);
    Set_border(img_group, "Aerial and image size");

    Choice_Box dpi_box = Create_choice_box("Resolution (dpi)", message);
    Text dpis[DPI_COUNT];
    dpis[1] = "72";
    dpis[2] = "100";
    dpis[3] = "150";
    dpis[4] = "200";
    dpis[5] = "300";
    dpis[6] = "600";
    Set_data(dpi_box, DPI_COUNT, dpis);
    Set_data(dpi_box, DEFAULT_DPI);
    Set_width_in_chars(dpi_box, FIELD_CHARS);

    Input_Box quality_box = Create_input_box("JPEG quality (10-95)", message);
    Set_data(quality_box, itxt(DEFAULT_QUALITY));
    Set_width_in_chars(quality_box, FIELD_CHARS);

    // A macro cannot read the opacity 12d draws a raster at, so it is
    // typed here
    Input_Box opacity_box = Create_input_box("Aerial opacity (%)", message);
    Set_data(opacity_box, itxt(DEFAULT_OPACITY));
    Set_width_in_chars(opacity_box, FIELD_CHARS);

    Named_Tick_Box grey_tick = Create_named_tick_box("Greyscale aerial", 0, "grey");

    // Other pictures 12d puts in the sheets (logos, an aerial it draws
    // itself) are brought down to the same resolution and quality
    Named_Tick_Box shrink_tick = Create_named_tick_box("Shrink all other images to the same resolution", 1, "shrink");

    // String weights (pipe diameters drawn as line widths, say).  Off, the
    // model data plots at the pen widths, as the Multi Plot Sheet PDFs do;
    // on, they are drawn, each view's plot scale set to the plot's scale
    // first (12d draws weights at the view's plot scale).  Title blocks
    // keep their own weights either way.
    Named_Tick_Box weights_tick = Create_named_tick_box("Draw string weights (pipe diameter line widths)", 0, "weights");

    // Only needed if Check finds no aerial: names of models that hold
    // nothing but the aerial raster(s)
    Input_Box aerial_box = Create_input_box("Aerial models (optional)", message);
    Set_optional(aerial_box, 1);
    Set_width_in_chars(aerial_box, FIELD_CHARS);

    // Moving the raster out for the run leaves the view exactly as the
    // plot sheet draws it (model order, per-view model settings).  The
    // view copy touches no data but loses per-view model settings.
    Choice_Box hide_box = Create_choice_box("While 12d plots", message);
    Text hides[2];
    hides[1] = "Park the aerial in a hold model (view unchanged)";
    hides[2] = "Plot a temporary copy of the view without it";
    Set_data(hide_box, 2, hides);
    Set_data(hide_box, hides[1]);
    Set_width_in_chars(hide_box, FIELD_CHARS);

    Append(dpi_box,     img_group);
    Append(quality_box, img_group);
    Append(opacity_box, img_group);
    Append(grey_tick,   img_group);
    Append(shrink_tick, img_group);
    Append(weights_tick, img_group);
    Append(aerial_box,  img_group);
    Append(hide_box,    img_group);

    // ---------------- Requirements ----------------
    // One Screen_Text per line; the last line is the live state of 12d's
    // "Open file after plot", refreshed at every Check and Plot
    Vertical_Group req_group = Create_vertical_group(ALL_WIDGETS_OWN_HEIGHT);
    Set_border(req_group, "Requirements");
    Screen_Text req_12d   = Create_screen_text("12d Model 15 on Windows 10/11 (PowerShell 5.1 and .NET 4 come with Windows)");
    Screen_Text req_ps    = Create_screen_text("PowerShell allowed to run scripts and compile C# (some managed PCs block it)");
    Screen_Text req_image = Create_screen_text("Aerials as 12d tiled images - 12d draws any other raster itself, not faster");
    Screen_Text req_open  = Create_screen_text("AUTO_OPEN_PDF_PLOTS_4D = 0, so 12d does not open each PDF it plots");
    Colour_Message_Box auto_box = Create_colour_message_box(" ");
    Set_width_in_chars(auto_box, STATUS_CHARS);
    show_auto_open(auto_box);
    Append(req_12d,   req_group);
    Append(req_ps,    req_group);
    Append(req_image, req_group);
    Append(req_open,  req_group);
    Append(auto_box,  req_group);

    // ---------------- Status ----------------
    Vertical_Group stat_group = Create_vertical_group(0);
    Set_border(stat_group, "Status");
    Colour_Message_Box status_box = Create_colour_message_box("Ready");
    Set_width_in_chars(status_box, STATUS_CHARS);
    Append(status_box, stat_group);

    // ---------------- Buttons ----------------
    Horizontal_Group bgroup = Create_button_group();
    Button check_btn  = Create_button("&Check",      "check");
    Button plot_btn   = Create_button("&Plot",       "plot");
    Button help_btn   = Create_button("&Help",       "help");
    Button finish_btn = Create_button("&Finish",     "finish");
    Append(check_btn,  bgroup);
    Append(plot_btn,   bgroup);
    Append(help_btn,   bgroup);
    Append(finish_btn, bgroup);

    Append(src_group,  panel);
    Append(out_group,  panel);
    Append(img_group,  panel);
    Append(req_group,  panel);
    Append(stat_group, panel);
    Append(message,    panel);
    Append(bgroup,     panel);

    // Open on the plot sheet file of the project if it has just one
    Text only_mpsf = project_plot_sheet();
    if(only_mpsf != "") Set_data(mpsf_box, only_mpsf);

    Show_widget(panel);

    Integer engine_pending = 0;     // the last run's engine may still be writing its PDF
    Integer doit = 1;
    while(doit)
    {
        Integer64 id;
        Text cmd = "", msg = "";
        Wait_on_events(id, cmd, msg);

        if(cmd == "CodeShutdown")
        {
            Set_exit_code("CodeShutdown");
            doit = 0;
            break;
        }
        if(cmd == "keystroke") continue;

        // The last run's engine writes the PDF and opens it on its own.  Its
        // result goes to the Output Window at the first Check or Plot after
        // it has finished; that press only shows the result.
        Integer is_action = 0;
        if(id == Get_id64(check_btn)) is_action = 1;
        if(id == Get_id64(plot_btn)) is_action = 1;
        if(engine_pending && is_action)
        {
            Text eng_file = "", eng_msg = "";
            Integer er = run_tick(status_box, eng_file, eng_msg);
            if(er == TICK_BUSY)
            {
                Set_data(status_box, "The last run's PDF is still being written - see the FastPlot window", MESSAGE_LEVEL_WARNING);
                continue;
            }
            engine_pending = 0;
            if(er == TICK_DONE) Set_data(status_box, eng_msg, MESSAGE_LEVEL_GOOD);
            else Set_data(status_box, eng_msg, MESSAGE_LEVEL_ERROR);
            continue;
        }

        if(id == Get_id64(panel))
        {
            if(cmd == "Panel Quit") doit = 0;
        }
        else if(id == Get_id64(finish_btn))
        {
            if(cmd == "finish") doit = 0;
        }
        else if(id == Get_id64(help_btn))
        {
            if(cmd == "help") show_help(panel, status_box);
        }
        else if(id == Get_id64(check_btn))
        {
            if(cmd != "check") continue;
            show_auto_open(auto_box);
            Text mpsf = "", spec = "", aerial = "", out_msg = "";
            Get_data(mpsf_box, mpsf);
            Get_data(sheets_box, spec);
            Get_data(aerial_box, aerial);
            log_msg("check: '" + mpsf + "' sheets '" + spec + "'");
            if(mpsf == "")
            {
                Set_data(status_box, "Pick the plot sheet file (.12dmpsf)", MESSAGE_LEVEL_ERROR);
                continue;
            }
            Set_data(status_box, "Reading " + file_of(mpsf) + "...", MESSAGE_LEVEL_GENERAL);
            Integer cr = do_check(mpsf, spec, aerial, out_msg);
            if(cr == 0) Set_data(status_box, out_msg, MESSAGE_LEVEL_GOOD);
            else Set_data(status_box, out_msg, MESSAGE_LEVEL_ERROR);
        }
        else if(id == Get_id64(plot_btn))
        {
            if(cmd != "plot") continue;
            show_auto_open(auto_box);
            Text mpsf = "", spec = "", aerial = "", output = "", mode_t = "", dpi_t = "", hide_t = "";
            Get_data(mpsf_box, mpsf);
            Get_data(sheets_box, spec);
            Get_data(aerial_box, aerial);
            Get_data(out_box, output);
            Get_data(mode_box, mode_t);
            Get_data(dpi_box, dpi_t);
            Get_data(hide_box, hide_t);
            if(mpsf == "")
            {
                Set_data(status_box, "Pick the plot sheet file (.12dmpsf)", MESSAGE_LEVEL_ERROR);
                continue;
            }
            Integer out_mode = OUT_BOOK;
            if(mode_t == modes[2]) out_mode = OUT_PER_SHEET;
            Integer hide_mode = HIDE_MOVE;
            if(hide_t == hides[2]) hide_mode = HIDE_COPY;
            Integer dpi = 150;
            if(From_text(dpi_t, dpi) != 0) dpi = 150;
            Integer quality = read_int_box(quality_box, DEFAULT_QUALITY, 10, 95);
            Integer opacity = read_int_box(opacity_box, DEFAULT_OPACITY, 0, 100);
            Integer grey = 0, open_pdf = 0, shrink = 1, weights = 0;
            Validate(grey_tick, grey);
            Validate(open_tick, open_pdf);
            Validate(shrink_tick, shrink);
            Validate(weights_tick, weights);
            log_msg("plot: '" + mpsf + "' sheets '" + spec + "' out '" + output + "' mode " + itxt(out_mode) +
                    " dpi " + itxt(dpi) + " q " + itxt(quality) + " opacity " + itxt(opacity) + " grey " + itxt(grey) +
                    " shrink " + itxt(shrink) + " weights " + itxt(weights));

            // greyed so a click made while 12d catches up between steps is
            // not taken for a second run
            Set_enable(plot_btn, 0);
            Set_enable(check_btn, 0);
            Text main_file = "", out_msg = "";
            Integer tr = TICK_FAILED;
            Integer pr = run_begin(status_box, mpsf, spec, aerial, hide_mode, out_mode, output, dpi, quality, opacity, grey, shrink,
                                   weights, open_pdf, out_msg);
            if(pr != 0) prog_finish("FAILED: " + out_msg);
            else
            {
                // 12d's part a step at a time.  12d cannot take its messages
                // until the handler returns (a macro has no call for that:
                // the Graph_Box timer is not implemented in V15 and
                // TDF_wait_for_timer returns -1 in 12d Model), so Windows
                // may show it Not Responding until the engine takes over.
                Set_data(status_box, out_msg, MESSAGE_LEVEL_GENERAL);
                tr = TICK_BUSY;
                while(g_run == RUN_STEPS)
                {
                    // the raster probe: leave the engine the processor
                    if(proc_running()) Sleep(WAIT_MS);
                    tr = run_tick(status_box, main_file, out_msg);
                    if(tr != TICK_BUSY) break;
                }
            }
            Set_enable(plot_btn, 1);
            Set_enable(check_btn, 1);
            if(g_run == RUN_ENGINE)
            {
                if(proc_running() == 0)
                {
                    // the engine was run waiting for it: its result is in already
                    tr = run_tick(status_box, main_file, out_msg);
                    if(tr == TICK_DONE) Set_data(status_box, out_msg, MESSAGE_LEVEL_GOOD);
                    else Set_data(status_box, out_msg, MESSAGE_LEVEL_ERROR);
                    continue;
                }
                engine_pending = 1;
                Text opens = "";
                if(open_pdf) opens = " and opens";
                Set_data(status_box, "12d is free - the PDF is being written" + opens + " (see the FastPlot window)", MESSAGE_LEVEL_GOOD);
                continue;
            }
            Set_data(status_box, out_msg, MESSAGE_LEVEL_ERROR);
        }
    }
    Hide_widget(panel);
}

void main()
{
    Set_char(g_tab, 1, 9);
    open_log();
    log_msg(PROGRAM_NAME + " starting");

    // an aerial left parked, or outlines left drawn, by an interrupted run are put right first
    Integer back = restore_rasters();
    if(back > 0)
    {
        Print(PROGRAM_NAME + ": moved " + itxt(back) + " aerial element(s) back into their models after an interrupted run.\n");
        Show_console(1);
    }
    remove_outlines();

    manage_panel();

    log_msg(PROGRAM_NAME + " finished");
    close_log();
}
