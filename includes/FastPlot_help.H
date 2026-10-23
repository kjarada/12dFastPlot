//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 23/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_help.H
//
// Brief description
// The Help button: a self-contained HTML page written to
// %TEMP%\FastPlot_help.html and opened in the default browser.  It
// carries no user, domain or machine details, so it can be shared.
// Every literal stays short (cc4d's per-line limit); the browser joins
// the runs.  Special characters are HTML entities, never UTF-8.
//---------------------------------------------------------------------

#ifndef FASTPLOT_HELP_H
#define FASTPLOT_HELP_H

void hwl(File f, Text line)
{
    File_write_line(f, line);
}

// A two-column table row
void help_row(File f, Text name, Text meaning)
{
    hwl(f, "<tr><td>" + name + "</td><td>" + meaning + "</td></tr>");
}

void help_head(File f)
{
    hwl(f, "<!DOCTYPE html>");
    hwl(f, "<html lang=\"en\"><head><meta charset=\"utf-8\">");
    hwl(f, "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
    hwl(f, "<title>FastPlot Help</title>");
    hwl(f, "<style>");
    hwl(f, ":root{--fg:#1f2328;--muted:#5b636e;--bg:#ffffff;--line:#d5dae0;--head:#f2f5f8;");
    hwl(f, "--accent:#1f5f99;--note:#fff6e0;--noteline:#d99a1c}");
    hwl(f, "@media (prefers-color-scheme:dark){:root{--fg:#e3e6ea;--muted:#9aa3ad;--bg:#16181c;");
    hwl(f, "--line:#3a3f46;--head:#22262c;--accent:#7db4e6;--note:#2e2716;--noteline:#c08a20}}");
    hwl(f, "body{font-family:'Segoe UI',Arial,sans-serif;color:var(--fg);background:var(--bg);");
    hwl(f, "max-width:62rem;margin:2rem auto;padding:0 1rem;line-height:1.5}");
    hwl(f, "h1{color:var(--accent);margin-bottom:.2rem}");
    hwl(f, "h2{color:var(--accent);border-bottom:1px solid var(--line);padding-bottom:.2rem;margin-top:2.2rem}");
    hwl(f, "table{border-collapse:collapse;width:100%;margin:.8rem 0}");
    hwl(f, "th,td{border:1px solid var(--line);padding:.4rem .6rem;text-align:left;vertical-align:top}");
    hwl(f, "th{background:var(--head)}td:first-child{font-weight:600;width:28%}");
    hwl(f, "code{background:var(--head);padding:.05rem .3rem;border-radius:3px;font-size:.92em}");
    hwl(f, ".note{background:var(--note);border-left:4px solid var(--noteline);padding:.6rem 1rem;margin:1rem 0}");
    hwl(f, ".sub{color:var(--muted);margin-top:0}nav a{margin-right:.9rem;white-space:nowrap}");
    hwl(f, "</style></head><body>");
    hwl(f, "<h1>" + PROGRAM_NAME + "</h1>");
    hwl(f, "<p class=\"sub\">Fast, small PDFs from a Multi Plot Sheet &mdash; for 12d Model 15. ");
    hwl(f, "Macro version " + PROGRAM_VERSION + ".</p>");
    hwl(f, "<nav><a href=\"#what\">What it does</a><a href=\"#before\">Before you start</a>");
    hwl(f, "<a href=\"#quick\">Quick start</a><a href=\"#panel\">The panel</a><a href=\"#buttons\">Buttons</a>");
    hwl(f, "<a href=\"#run\">During a plot</a><a href=\"#project\">Project changes</a><a href=\"#output\">Output</a>");
    hwl(f, "<a href=\"#files\">Temporary files</a><a href=\"#trouble\">Troubleshooting</a>");
    hwl(f, "<a href=\"#limits\">Limits</a></nav>");
}

void help_intro(File f)
{
    hwl(f, "<h2 id=\"what\">What FastPlot does</h2>");
    hwl(f, "<p>12d's Multi Plot Sheet plotter puts the aerial into the PDF at the raster's own pixel size, ");
    hwl(f, "whatever the plot scale. A cover page at 1:6500 can carry the aerial at about 1400 dpi and ");
    hwl(f, "run to 80 MB, and a whole book takes minutes to plot.</p>");
    hwl(f, "<p>FastPlot keeps the aerial out of 12d's plots. 12d still draws everything else: linework, ");
    hwl(f, "linestyles, symbols, text, drafting and title blocks all come from its own plotter. ");
    hwl(f, "FastPlot's engine then draws the aerial straight from the raster's tiles, at the resolution ");
    hwl(f, "and JPEG quality you choose, and writes one PDF for the book or one PDF per sheet.</p>");
    hwl(f, "<p>12d also plots fewer times: one plot per area of sheets that share a view and scale, and ");
    hwl(f, "one per title block variant, instead of one per sheet. The engine cuts each sheet out of ");
    hwl(f, "those plots. On a 66-sheet A3 book that was 6 plots instead of 66, about a minute in all ");
    hwl(f, "instead of 5&ndash;15 minutes, and a 29 MB PDF instead of about 214 MB.</p>");
}

void help_requirements(File f)
{
    hwl(f, "<h2 id=\"before\">Before you start</h2><ul>");
    hwl(f, "<li><b>12d Model 15.</b> FastPlot was written and tested on V15 only.</li>");
    hwl(f, "<li><b>Windows 10 or 11.</b> The engine runs in Windows PowerShell 5.1 with .NET Framework 4, ");
    hwl(f, "which come with Windows. Nothing is installed and no administrator rights are needed.</li>");
    hwl(f, "<li><b>PowerShell allowed to run scripts and compile C#</b> (<code>Add-Type</code>). Some managed ");
    hwl(f, "PCs block this through a Group Policy execution policy, Constrained Language Mode ");
    hwl(f, "(AppLocker / WDAC) or antivirus. The PDF step then fails with a message.</li>");
    hwl(f, "<li><b><code>AUTO_OPEN_PDF_PLOTS_4D = 0</code></b>, 12d's &quot;Open file after plot&quot; setting ");
    hwl(f, "for PDFs. While it is on, 12d opens a viewer for every plot it makes, so Plot refuses more ");
    hwl(f, "than 2 sheets. The last line of the panel's Requirements box shows the current state.</li>");
    hwl(f, "<li><b>The aerial must be a 12d tiled image</b>, which 12d keeps in ");
    hwl(f, "<code>&lt;project&gt;.project\\&lt;raster&gt;.image</code>. Any other raster is left for 12d ");
    hwl(f, "to draw: the run still works, but that aerial is not faster or smaller.</li>");
    hwl(f, "<li><b>Each sheet</b> must have exactly one plan frame and at most one title block. ");
    hwl(f, "Other sheets are listed by Check and skipped.</li>");
    hwl(f, "</ul>");
}

void help_quick_start(File f)
{
    hwl(f, "<h2 id=\"quick\">Quick start</h2><ol>");
    hwl(f, "<li>Open the project in 12d and run FastPlot.</li>");
    hwl(f, "<li>Pick the <b>Plot sheet file</b> (<code>.12dmpsf</code>). If the project folder holds only ");
    hwl(f, "one, it is already filled in.</li>");
    hwl(f, "<li>Press <b>Check</b>. The Output Window lists the sheets, views and aerial rasters. ");
    hwl(f, "Nothing in the project changes.</li>");
    hwl(f, "<li>Type <code>2</code> in <b>Sheets</b> and press <b>Plot</b> to test one sheet. Compare it ");
    hwl(f, "with 12d's own plot of that sheet.</li>");
    hwl(f, "<li>Set <b>Sheets</b> back to <code>all</code> and press <b>Plot</b>.</li>");
    hwl(f, "</ol>");
    hwl(f, "<div class=\"note\">While 12d plots, Windows may show it as <b>Not Responding</b>. That is ");
    hwl(f, "expected: do not close 12d. The FastPlot progress window shows each step, and 12d is free ");
    hwl(f, "again once the engine starts writing the PDF.</div>");
}

void help_panel(File f)
{
    hwl(f, "<h2 id=\"panel\">The panel</h2>");

    hwl(f, "<h3>Plot sheet</h3><table><tr><th>Field</th><th>What it does</th></tr>");
    help_row(f, "Plot sheet file",
             "The Multi Plot Sheet file (<code>.12dmpsf</code>) to plot. Filled in when the project " +
             "folder holds exactly one.");
    help_row(f, "Sheets (all or 1-5,8)",
             "<code>all</code>, or sheet positions in the book such as <code>1-5,8,12-20</code>. " +
             "1 is the first sheet of the book, whatever its name. Use <code>2</code> to test one sheet.");
    hwl(f, "</table>");

    hwl(f, "<h3>Output</h3><table><tr><th>Field</th><th>What it does</th></tr>");
    help_row(f, "Write",
             "<b>One PDF for the whole book</b>, with a bookmark per sheet and a layer per model, or " +
             "<b>One PDF per sheet</b>, each named after its sheet.");
    help_row(f, "PDF file",
             "Where the PDF goes. Left blank, it is the book's target name from the plot sheet, in the " +
             "project folder. With one PDF per sheet only its folder is used. FastPlot never fills this " +
             "box in; the Output Window lists every file written.");
    help_row(f, "Open the PDF when finished",
             "Opens the book when the engine is done, or the folder for one PDF per sheet.");
    hwl(f, "</table>");

    hwl(f, "<h3>Aerial and image size</h3><table><tr><th>Field</th><th>What it does</th></tr>");
    help_row(f, "Resolution (dpi)",
             "The most the aerial is drawn at: 72 to 600, default " + DEFAULT_DPI + ". An aerial is " +
             "never resampled above its own pixel size, so a 0.115 m aerial at 1:500 stays at about " +
             "110 dpi whatever is chosen. It matters most on small-scale sheets such as cover pages.");
    help_row(f, "JPEG quality (10-95)",
             "Default " + itxt(DEFAULT_QUALITY) + ". 60&ndash;75 suits plans; lower gives smaller files.");
    help_row(f, "Aerial opacity (%)",
             "0 to 100, default " + itxt(DEFAULT_OPACITY) + ". Type the opacity 12d draws the aerial " +
             "at; a macro cannot read it. 100 is solid.");
    help_row(f, "Greyscale aerial", "Draws the aerial in grey, for a smaller file.");
    help_row(f, "Shrink all other images",
             "Brings every other picture 12d puts in the sheets (a logo, or an aerial 12d had to draw " +
             "itself) down to the same resolution and quality. On by default.");
    help_row(f, "Draw string weights",
             "<b>Off</b> (default): model data plots at the pen widths, as 12d's Multi Plot Sheet " +
             "PDFs do. <b>On</b>: string weights are drawn, for example pipe diameters drawn as line " +
             "widths. Each view's plot scale is set to the plot's scale for the run, because 12d draws " +
             "weights at the view's plot scale, and put back afterwards. Title blocks keep their own " +
             "weights either way.");
    help_row(f, "Aerial models (optional)",
             "Only needed when Check finds no aerial. The names of the models that hold nothing but " +
             "the aerial raster(s), separated by commas.");
    help_row(f, "While 12d plots",
             "<b>Park the aerial in a hold model</b> (default): the raster is moved into " +
             "<code>FastPlot/hold/&lt;model&gt;</code> for 12d's plots and moved back; the view is " +
             "untouched. <b>Plot a temporary copy of the view</b>: no data is moved, but the copy " +
             "loses the view's per-model settings.");
    hwl(f, "</table>");

    hwl(f, "<h3>Requirements and Status</h3><table><tr><th>Box</th><th>What it shows</th></tr>");
    help_row(f, "Requirements",
             "What FastPlot needs (see <a href=\"#before\">Before you start</a>). Its last line is " +
             "green when <code>AUTO_OPEN_PDF_PLOTS_4D</code> is 0 and a warning when it is not; it is " +
             "checked again at every Check and Plot.");
    help_row(f, "Status",
             "What FastPlot is doing, or the result of the last Check or Plot. Details go to the " +
             "Output Window.");
    hwl(f, "</table>");
}

void help_buttons(File f)
{
    hwl(f, "<h2 id=\"buttons\">Buttons</h2><table><tr><th>Button</th><th>What it does</th></tr>");
    help_row(f, "Check",
             "Reads the plot sheet and lists in the Output Window every sheet (<code>*</code> = " +
             "selected) with its view, scale and centre, and why any sheet will be skipped; the " +
             "models of each view; each aerial raster and whether the engine can read its tiles; " +
             "and the <code>AUTO_OPEN_PDF_PLOTS_4D</code> setting. It changes nothing in the project.");
    help_row(f, "Plot",
             "Plots the selected sheets and writes the PDF. Check and Plot are greyed out while 12d plots.");
    help_row(f, "Help", "Opens this page.");
    help_row(f, "Finish",
             "Closes the panel. A PDF that is still being written carries on, and opens if asked to.");
    hwl(f, "</table>");
    hwl(f, "<p>The engine writes the PDF after 12d's part, while 12d is free. If you press Check or ");
    hwl(f, "Plot before it is done, the Status box says the PDF is still being written. The first ");
    hwl(f, "press after it has finished only shows that run's result.</p>");
}

void help_run(File f)
{
    hwl(f, "<h2 id=\"run\">What happens during a plot</h2>");
    hwl(f, "<p>A progress window, <i>12d FastPlot - progress</i>, opens and shows each step. It closes ");
    hwl(f, "itself 15 seconds after the run.</p><ol>");
    hwl(f, "<li><b>Read and study.</b> FastPlot reads the plot sheet and finds each sheet's view, its ");
    hwl(f, "models and its aerial rasters. The engine checks that it can read each raster's tiles, ");
    hwl(f, "which takes about a second.</li>");
    hwl(f, "<li><b>Park.</b> Each aerial the engine can read is moved out of 12d's way. The sheet ");
    hwl(f, "outlines are drawn, as 12d's Multi Plot Sheet plotter does (each plan frame's outline in ");
    hwl(f, "its frame model, which makes the key map on a cover page).</li>");
    hwl(f, "<li><b>12d plots the areas:</b> groups of sheets that share a view and scale, up to 3 m of ");
    hwl(f, "paper a side, vectors only.</li>");
    hwl(f, "<li><b>12d plots the title blocks:</b> one plot per title block variant, with the drawing ");
    hwl(f, "number <code>1234567890</code>, which the engine replaces with each sheet's own number.</li>");
    hwl(f, "<li><b>Sheets on their own.</b> A sheet whose shared plot failed (and, with string weights ");
    hwl(f, "on, a sheet alone in its group, such as a cover page) gets a 12d plot of its own.</li>");
    hwl(f, "<li><b>Aerial back.</b> The parked aerial goes back into its model. Rotated sheets are then ");
    hwl(f, "plotted by 12d with their aerial.</li>");
    hwl(f, "<li><b>Clean-up.</b> Outlines, plot frames and temporary views are removed, and view plot ");
    hwl(f, "scales are put back. 12d is free.</li>");
    hwl(f, "<li><b>The engine</b>, in the background, cuts each sheet out of the area plots, adds its ");
    hwl(f, "title block, and draws the aerial from the tiles at the chosen resolution, quality and ");
    hwl(f, "opacity, in the aerial model's place in the drawing order. It shrinks other images, ");
    hwl(f, "writes the PDF and opens it if asked.</li>");
    hwl(f, "</ol>");
}

void help_project(File f)
{
    hwl(f, "<h2 id=\"project\">What FastPlot changes in the project</h2>");
    hwl(f, "<p>Only for the length of a run; everything is put back at the end.</p>");
    hwl(f, "<table><tr><th>What</th><th>Why</th><th>Put back</th></tr>");
    hwl(f, "<tr><td>Model <code>FastPlot/hold/&lt;model&gt;</code></td>");
    hwl(f, "<td>Holds the aerial raster while 12d plots (Park mode).</td>");
    hwl(f, "<td>Raster moved back into its model before rotated sheets are plotted.</td></tr>");
    hwl(f, "<tr><td>Model <code>FastPlot/frames</code></td><td>The plot frames 12d plots through.</td>");
    hwl(f, "<td>Deleted at the end of the run.</td></tr>");
    hwl(f, "<tr><td>Strings named <code>FastPlot outline &lt;sheet&gt;</code></td>");
    hwl(f, "<td>The sheet outlines, in each plan frame's model.</td><td>Removed at the end of the run.</td></tr>");
    hwl(f, "<tr><td>Views <code>FastPlot temp &lt;n&gt;</code></td>");
    hwl(f, "<td>Copies of the views without the aerial (temporary copy mode only).</td>");
    hwl(f, "<td>Deleted at the end of the run.</td></tr>");
    hwl(f, "<tr><td>View plot scales</td><td>Set to each plot's scale (Draw string weights on only).</td>");
    hwl(f, "<td>Put back at the end of the run.</td></tr>");
    hwl(f, "</table>");
    hwl(f, "<div class=\"note\">If 12d closes or the run stops part way, run FastPlot again. Before the ");
    hwl(f, "panel opens it moves a parked aerial back into its model and removes left-over outlines. ");
    hwl(f, "The next plot replaces <code>FastPlot/frames</code>. Delete any left-over ");
    hwl(f, "<code>FastPlot temp</code> views by hand, and check the view plot scales if Draw string ");
    hwl(f, "weights was on.</div>");
}

void help_output(File f)
{
    hwl(f, "<h2 id=\"output\">Output</h2><ul>");
    hwl(f, "<li><b>One PDF for the book:</b> the PDF file, or the book's target name in the project ");
    hwl(f, "folder. It has a bookmark per sheet and a layer per 12d model, so models can be hidden ");
    hwl(f, "in the PDF viewer.</li>");
    hwl(f, "<li><b>One PDF per sheet:</b> named after each sheet (made unique), in the PDF file's ");
    hwl(f, "folder, or the project folder when the PDF file is blank.</li>");
    hwl(f, "<li><b>Output Window:</b> each sheet with OK or the reason it failed, every file written ");
    hwl(f, "with its size, and the total.</li>");
    hwl(f, "</ul>");
}

void help_files(File f)
{
    hwl(f, "<h2 id=\"files\">Temporary files</h2>");
    hwl(f, "<p>Outside the project, FastPlot writes only to your TEMP folder and to the PDF location. ");
    hwl(f, "Everything below can be deleted when FastPlot is not running.</p>");
    hwl(f, "<table><tr><th>File or folder</th><th>What it is</th></tr>");
    help_row(f, "<code>%TEMP%\\FastPlot\\engine\\</code>",
             "The engine (<code>FastPlotEngine.cs</code>, <code>FastPlot_compose.ps1</code>, " +
             "<code>FastPlot_monitor.ps1</code>). The macro carries it and writes it out at every " +
             "Check and Plot.");
    help_row(f, "<code>%TEMP%\\FastPlot\\FastPlotEngine_*.dll</code>",
             "The engine, compiled on first use, once per version.");
    help_row(f, "<code>%TEMP%\\FastPlot\\run\\</code>",
             "One run's work: 12d's plots and the job and result files. Emptied at the start of " +
             "every run, and removed after a successful run once its result has been shown.");
    help_row(f, "<code>%TEMP%\\FastPlot\\engine.log</code>",
             "The engine's log. Read it first when the PDF step fails.");
    help_row(f, "<code>%TEMP%\\FastPlot\\progress.txt</code>", "What the progress window shows.");
    help_row(f, "<code>%TEMP%\\FastPlot\\outlines.txt</code>",
             "The outlines drawn, so an interrupted run can be cleaned up.");
    help_row(f, "<code>%TEMP%\\FastPlot_help.html</code>", "This page, written each time Help is pressed.");
    hwl(f, "</table>");
}

void help_trouble(File f)
{
    hwl(f, "<h2 id=\"trouble\">Troubleshooting</h2>");
    hwl(f, "<table><tr><th>You see</th><th>What to do</th></tr>");
    help_row(f, "Untick 12d's &quot;Open file after plot&quot; first ... or plot at most 2 sheets",
             "Set <code>AUTO_OPEN_PDF_PLOTS_4D</code> to 0 in 12d, then plot again. The last line of " +
             "the Requirements box turns green.");
    help_row(f, "No aerial raster found in these views",
             "Type the aerial's model name in <b>Aerial models</b> and press Check again.");
    help_row(f, "A raster listed as NOT USABLE",
             "The engine cannot read its tiles: it is not a 12d tiled image, or its " +
             "<code>.image</code> folder is missing. 12d draws that aerial itself; the run works " +
             "but is not faster.");
    help_row(f, "The engine did not answer the raster check",
             "PowerShell did not run, so the PDF cannot be written either. Check the PowerShell " +
             "requirement and read <code>%TEMP%\\FastPlot\\engine.log</code>.");
    help_row(f, "area N FAILED: ... its sheets are plotted one by one",
             "FastPlot falls back to one 12d plot per sheet: slower, still correct. If the sheets " +
             "fail too, try the same sheet in 12d's own Multi Plot Sheet panel.");
    help_row(f, "A sheet marked ** by Check",
             "It will be skipped. The reason follows the marks, for example a second plan frame, a " +
             "picture, or a view that does not exist.");
    help_row(f, "The PDF engine left no result, or Engine error",
             "Read <code>%TEMP%\\FastPlot\\engine.log</code>. A blocked PowerShell or antivirus is " +
             "the usual cause.");
    help_row(f, "The last run's PDF is still being written",
             "Wait for the progress window to say Done, then press Check or Plot again.");
    help_row(f, "12d shows Not Responding",
             "Normal while 12d plots. Do not close it; the progress window shows it is working.");
    help_row(f, "The aerial looks faded or too strong",
             "Set <b>Aerial opacity</b> to the opacity 12d draws it at.");
    help_row(f, "The aerial is blurry",
             "Raise <b>JPEG quality</b>, or <b>Resolution</b> where the aerial's own pixels are " +
             "finer than the chosen dpi.");
    help_row(f, "12d or the run stopped part way",
             "Run FastPlot again: before the panel opens it moves a parked aerial back and removes " +
             "left-over outlines.");
    hwl(f, "</table>");
}

void help_limits(File f)
{
    hwl(f, "<h2 id=\"limits\">Known limits</h2><ul>");
    hwl(f, "<li>A sheet must have exactly one plan frame and at most one title block. Pictures, text ");
    hwl(f, "shapes, section frames or a second viewport make Check skip the sheet.</li>");
    hwl(f, "<li>On rotated sheets 12d draws the aerial itself, so they are not faster.</li>");
    hwl(f, "<li>JPEG 2000 images pass through Shrink unchanged.</li>");
    hwl(f, "<li>Aerial opacity is typed on the panel, not read from 12d.</li>");
    hwl(f, "<li>Written and tested on 12d Model 15 only.</li>");
    hwl(f, "</ul>");
}

//---------------------------------------------------------------------
// write_help_html - the whole page; 0 when written
//---------------------------------------------------------------------
Integer write_help_html(Text path)
{
    File f;
    if(File_open(path, "w", "", f) != 0) return 1;
    help_head(f);
    help_intro(f);
    help_requirements(f);
    help_quick_start(f);
    help_panel(f);
    help_buttons(f);
    help_run(f);
    help_project(f);
    help_output(f);
    help_files(f);
    help_trouble(f);
    help_limits(f);
    hwl(f, "<p class=\"sub\" style=\"margin-top:2rem\">" + PROGRAM_NAME + " " + PROGRAM_VERSION +
           " for 12d Model 15.</p>");
    hwl(f, "</body></html>");
    File_close(f);
    return 0;
}

//---------------------------------------------------------------------
// show_help - write the page to TEMP and open it in the browser
//---------------------------------------------------------------------
void show_help(Panel panel, Colour_Message_Box status)
{
    Text path = temp_dir() + "\\FastPlot_help.html";
    if(write_help_html(path) != 0)
    {
        log_msg("help: cannot write " + path);
        Set_data(status, "Cannot write the help page to " + path, MESSAGE_LEVEL_ERROR);
        return;
    }
    Integer rc = Shell_execute(panel, "open", path, "", temp_dir(), 1);
    log_msg("help: " + path + " -> " + itxt(rc));
    Set_data(status, "Help opened in your browser (" + path + ")", MESSAGE_LEVEL_GENERAL);
}

#endif
