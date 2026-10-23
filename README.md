# FastPlot — fast, small PDFs from a Multi Plot Sheet

FastPlot plots a Multi Plot Sheet file (`.12dmpsf`) in 12d Model. Its output
looks the same as 12d's Multi Plot Sheet plotter, but it is faster and the PDF is
a fraction of the size. You set the image resolution and JPEG quality.

## Requirements

- **12d Model 15.** The macro was written and tested on V15 only.
- **Windows 10 or 11** with Windows PowerShell 5.1 and .NET Framework 4.x. Both
  come with Windows. Nothing else needs to be installed, and no administrator
  rights are needed.
- **PowerShell must be allowed to run** the engine script and compile C# code
  (`Add-Type`). Some managed PCs block this, and the PDF step then fails with a
  message. Two settings cause it: a Group Policy execution policy, which
  overrides the macro's `-ExecutionPolicy Bypass`, and Constrained Language Mode
  (AppLocker / WDAC). Antivirus can also stop the engine DLL that is compiled
  into `%TEMP%\FastPlot`.
- **The aerial must be a 12d tiled image**, kept by 12d in
  `<project>.project\<raster>.image`. FastPlot leaves any other raster for 12d to
  draw, so the run still works but is not faster.

## Why the built-in plotter is slow and the files are huge

12d's `pdf_12d` engine embeds the aerial at the raster's own pixel size,
whatever the plot scale. It cuts the aerial into 1024 × 1024 JPEG pieces.

| Sheet | Scale | Aerial pieces | Effective resolution | 12d PDF |
|---|---|---|---|---|
| Cover page (Sheet 1) | 1:6500 | 224 | ~1430 dpi | 78.3 MB |
| Plan sheet (Sheet 2) | 1:500 | 4 | ~110 dpi | 0.9 MB |

The vectors are small: about 0.2–0.5 MB per sheet, including fonts. The aerial
accounts for 99% of the cover page.

## How it works

1. **12d draws the vectors, in a few plots instead of one per sheet.** Every 12d
   plot costs about 3 s whatever it holds, so per-sheet plotting can never be
   fast. The macro groups the sheets instead:
   - **Areas:** one plot per area of sheets that share a view and scale, up to
     3 m of paper. It has the vectors only.
   - **Title blocks:** one plot per title-block variant (title file, user text,
     paper). The viewport is empty and the drawing number is `1234567890`.

   Linestyles, symbols, fonts, drafting, labels and title blocks still come from
   12d's own plotter. For this 66-sheet book that is about 6 plots instead of 66.
   A sheet alone in its group (a cover page), a rotated sheet, or any sheet whose
   shared plot fails gets one ordinary plot of its own.
2. **The engine cuts the sheets out.** It indexes every path and text object in an
   area plot once, then gives each sheet only what touches its window. This
   becomes a form XObject that is translated and clipped to the sheet's viewport.
   The title-block plot is added with the placeholder digits swapped for the
   sheet's own number. 12d writes text as Unicode character codes, and the
   placeholder makes the font carry every digit.
3. **The aerial is kept out of 12d's plot.** By default the raster element is
   moved into a model called `FastPlot/hold/<model>` for the run, then moved
   back. The view is not touched: its model order and per-view settings stay as
   they are. A raster is only moved after the engine has confirmed it can read
   that raster's tiles. If anything is uncertain, 12d draws that aerial as usual.
4. **The engine adds the aerial.** `FastPlot_compose.ps1` runs `FastPlotEngine.cs`.
   The engine reads the raster's tiles straight from
   `<project>.project\<raster>.image`, resamples them to the chosen dpi and
   applies the opacity. It encodes a JPEG at the chosen quality and inserts it at
   the raster model's own layer in 12d's vector page.
5. **Pages are merged.** The engine writes one PDF for the book (one layer per
   model, bookmarks, duplicate font programs shared) or one PDF per sheet.
   With **Shrink all other images** ticked, any other picture in 12d's sheet
   plots (a logo, or an aerial 12d had to draw itself) is brought down to the
   same resolution and quality.
6. **Sheet outlines are added for the run.** The Multi Plot Sheet plotter draws
   each plan frame's outline (dashed, in the frame's colour, linestyle and
   weight) into its frame model. This is the sheet key map on the cover pages.
   FastPlot draws the same outlines for the run and removes them afterwards.

## Install

Deploy only `FastPlot_panel_01.4do`. It carries the engine and writes
`FastPlotEngine.cs`, `FastPlot_compose.ps1` and `FastPlot_monitor.ps1` to
`%TEMP%\FastPlot\engine` at the start of every run. The engine compiles itself once per version, into
`%TEMP%\FastPlot`. It uses Windows PowerShell 5.1 and .NET 4, which are already
on every Windows 10/11 PC. No other software is needed.

**Once, in 12d:** untick **Plotting → PDF → "Open file after plot"**
(`AUTO_OPEN_PDF_PLOTS_4D = 0`, in the Create/Edit env.4d panel). Otherwise 12d
opens a viewer for every sheet it plots. **Check** reports this setting, and
**Plot** refuses a full book while it is on.

## Developing

`FastPlotEngine.cs`, `FastPlot_compose.ps1` and `FastPlot_monitor.ps1` stay the
source of truth. After editing any of them, run `build_embed.ps1`; it regenerates
`includes\FastPlot_embed.H`.
Then compile the macro.

## Panel

| Field | Meaning |
|---|---|
| Plot sheet file | The `.12dmpsf`. If the project folder holds just one, the panel opens on it. |
| Sheets | `all`, or positions in the book, such as `1-5,8`. Use `2` to test one sheet. |
| Write | One PDF for the whole book, or one PDF per sheet. |
| PDF file | Leave blank to use the book's target name, beside the project. The macro never fills this box in; the Output Window lists the file written. |
| Resolution (dpi) | The maximum aerial resolution. An aerial is never resampled above its own pixel size: a 0.115 m aerial at 1:500 stays at ~110 dpi whatever you choose. |
| JPEG quality | 10–95. 60–75 suits plans. |
| Aerial opacity | The opacity 12d draws the aerial at (default 50%). A macro cannot read that setting, so type it here. |
| Greyscale aerial | Makes the aerial grey, which gives a smaller file. |
| Shrink all other images | Applies the same resolution ceiling and JPEG quality to every other image in the sheets. On by default. |
| Draw string weights | **Off** (default): model data plots at the pen widths, as the Multi Plot Sheet PDFs do. **On**: string weights (for example pipe diameters drawn as line widths) are drawn. Before each plot, its view's plot scale is set to the plot's scale, because 12d draws weights at the view's plot scale. The original view scales are put back afterwards. Title blocks keep their own weights either way. |
| Aerial models | Only needed if **Check** finds no aerial. List models that hold nothing but the aerial raster. |
| While 12d plots | **Park the aerial** (default; the view is untouched) or **Plot a temporary copy of the view** (no data is touched, but per-view model settings are lost). |
| Requirements | The requirements above, as text. Its last line shows whether 12d's "Open file after plot" is off (`AUTO_OPEN_PDF_PLOTS_4D = 0`). It is green when off and a warning when on, and is checked again at every **Check** and **Plot**. |

### While a plot runs

A **progress window** opens and shows each step: the 12d plots, the aerial being
parked and restored, the engine building each sheet, and the file written. It
closes itself 15 s after the run.

A run is a list of steps, and 12d is busy only for its own part:

- **Checking the aerial.** The engine reads the tiles; this takes about 1 s.
- **12d's plots.** There is one step per plot. 12d is busy from the first plot to
  the last, and Windows may show it as **Not Responding** for that time. This is
  expected; do not close 12d. The progress window shows each plot as it goes.
- **The PDF.** The engine writes it while 12d is free and the panel is usable
  again. It opens the PDF when it is done, if "Open the PDF" is ticked. The next
  **Check** or **Plot** you press only shows that run's result in the Output
  Window.

A macro cannot let 12d take its messages part-way through a handler:

- The Graph_Box timer (`Start_timer`) is declared in V15, but cc4d rejects it as
  not implemented.
- The console call `TDF_wait_for_timer` returns -1 in 12d Model.

The debug log times every step and splits each plot into frame setup, view scale
and 12d's own plotting.

The buttons:

- **Check** reads the plot sheet and lists sheets, views, aerial rasters and
  their tile folders, and the "Open file after plot" setting. It changes nothing.
- **Plot** plots the selected sheets and writes the PDF.
- **Help** writes a help page covering every field, button, run step, temporary
  file and error message to `%TEMP%\FastPlot_help.html`, and opens it in the
  browser. It works at any time, even while a PDF is being written.

## Measured (one 66-sheet A3 book)

- **Fidelity.** The engine was run on 12d's own Sheet 1 and Sheet 2, with the
  aerial layer removed to stand in for the vector plot. The result differs from
  12d's original by 3–4 grey levels out of 255 inside the viewport, which is
  JPEG noise. There is no measurable misalignment at 1:500; one aerial pixel
  is 0.23 mm on paper.
- **Speed and size, all 66 sheets.** About 8 s for the engine. The book comes to
  15 MB at 150 dpi, 13 MB at 100 dpi and 17 MB at 300 dpi. 12d's output for the
  same book would be about 214 MB.

- **Live in 12d, all 66 sheets, weights on (21/10/26).** About 55 s in all, against
  5–15 minutes for the Multi Plot Sheet plotter. 12d's six plots took 28 s and the
  engine 13 s. The PDF was 29 MB.

- **Live, step-by-step run with the background engine, weights off (21/10/26).**
  12d was busy for 35 s: the aerial check, six plots (2.3, 12.0, 2.1, 4.9, 2.5 and
  2.6 s, about 1.2 s apart) and the clean-up. The engine then wrote the 28.8 MB,
  66-page PDF in 21 s while 12d was free, and opened it.

## First run: what to check

Run **Check** first, then **Plot** with `Sheets = 2`. Then plot sheets `1-2` and
compare them with the PDFs the Multi Plot Sheet plotter makes.

The checks below read the debug log, `%TEMP%\FastPlot.log`. Release builds do
not write it: uncomment `#define DEBUG` in `includes\FastPlot_globals.H` and
compile again. The work folder, `%TEMP%\FastPlot\run`, is then also kept after
each run.

1. **Raster type.** Check should list the aerial raster. The log shows every
   element type seen per model. If the aerial is not found, put its model name in
   *Aerial models*.
2. **Vector PDF location.** The log line `vector pdf at stem:` is 1 when 12d wrote
   `...\run\s002\FastPlot_s002.pdf`. If 12d put the file in the project folder
   instead, the engine finds it there and removes it.
3. **Frame position.** The log line `frame: ... extent is the sheet / viewport,
   origin moved ...` shows how the plot frame origin was centred.
4. **Title block.** Look at the `$user_text` values, the sheet number (the
   sheet's position in the book) and `$time`. Any `Set_parameter ... returned`
   line names a plot parameter 12d did not accept.
5. **Draw order.** The PDF layer order should match the Multi Plot Sheet output.

## Known limits

- A sheet must have exactly one plan frame and at most one title block. Other
  shapes, such as pictures, text shapes, section frames or a second viewport,
  are reported and the sheet is skipped.
- On rotated sheets 12d draws the aerial itself (pass 2 of a run), so they are
  not faster yet.
- JPEG 2000 images (12d's own multi-page merge uses them) pass through Shrink
  unchanged.
- **Aerial opacity** is typed on the panel, not read from 12d.
