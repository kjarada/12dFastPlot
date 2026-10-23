//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_job.H
//
// Brief description
// The hand-over to the PDF engine.  The macro writes a tab separated
// job file, runs FastPlot_compose.ps1 on it with no window - without
// waiting during a plot run, so 12d stays responsive - and reads back
// the result file.
//
// Job records (one per line, tab separated):
//   mode / output_mode / output / dpi / quality / opacity / grey /
//   recompress / bookmarks / title / result / log   <key> <value>
//   raster  id model element folder xmin ymin xmax ymax
//   sheet   seq name chapter source page outname paperW paperH
//           left bottom right top scale rotationDeg ox oy rasterIds below
//           vplot tplot          (built from shared plots when vplot is set)
//   vplot   id folder scale xmin ymin xmax ymax left bottom right top paperW paperH
//   tplot   id folder placeholder
//   input   pdf                                    (shrink mode)
// Result records:
//   STATUS OK|ERROR seconds-or-message, SHEET seq name OK|ERROR note,
//   FILE path bytes, WARN ..., INPUT ..., TOTAL pages bytes [in changed]
//---------------------------------------------------------------------

#ifndef FASTPLOT_JOB_H
#define FASTPLOT_JOB_H

// Where the macro writes the engine it carries (FastPlot_embed.H)
Text engine_dir()
{
    return temp_dir() + "\\FastPlot\\engine";
}

//---------------------------------------------------------------------
// prepare_engine
//   Writes FastPlotEngine.cs and FastPlot_compose.ps1 out of the macro,
//   so the .4do is the only file to deploy.  The script compiles the
//   engine once per version and caches it.  Returns 1 when ready.
//---------------------------------------------------------------------
Integer prepare_engine(Text &err)
{
    err = "";
    Text d = engine_dir();
    if(Directory_exists(d) == 0) Directory_create_recursive(d);
    if(write_engine_cs(d + "\\" + ENGINE_CS) != 0) err = "Cannot write " + d + "\\" + ENGINE_CS;
    if(write_engine_ps1(d + "\\" + ENGINE_PS) != 0) err = "Cannot write " + d + "\\" + ENGINE_PS;
    if(write_engine_mon(d + "\\" + ENGINE_MON) != 0) err = "Cannot write " + d + "\\" + ENGINE_MON;
    if(err != "")
    {
        log_msg(err);
        return 0;
    }
    return 1;
}

//---------------------------------------------------------------------
// job_line - "key<TAB>value"
//---------------------------------------------------------------------
void job_line(File f, Text key, Text value)
{
    File_write_line(f, key + g_tab + job_safe(value));
}

Integer open_job(Text path, File &f)
{
    Integer r = File_open(path, "w", "ccs=UTF-8", f);
    if(r != 0) return r;
    File_write_line(f, "FASTPLOT_JOB" + g_tab + "1");
    return 0;
}

void write_job_settings(File f, Text mode, Text output_mode, Text output, Integer dpi, Integer quality,
                        Integer opacity, Integer grey, Text title, Text result, Text logp)
{
    job_line(f, "mode", mode);
    job_line(f, "output_mode", output_mode);
    job_line(f, "output", output);
    job_line(f, "dpi", itxt(dpi));
    job_line(f, "quality", itxt(quality));
    job_line(f, "opacity", To_text(opacity / 100.0, 3));
    job_line(f, "grey", itxt(grey));
    job_line(f, "title", title);
    job_line(f, "result", result);
    job_line(f, "log", logp);
}

//---------------------------------------------------------------------
// write_raster_records
//   Every raster in the table.  The folder written is the project
//   folder: 12d keeps a raster's tiles in "<project>.project\<name>.image"
//   and the engine searches for it there.
//---------------------------------------------------------------------
void write_raster_records(File f, Text folder, Integer only_ok)
{
    Integer i;
    for(i = 1; i <= g_n_rasters; i++)
    {
        if(only_ok && get_int(g_ras_ok, i) == 0) continue;
        Text line = "raster" + g_tab + "R" + itxt(i) + g_tab +
                    job_safe(get_text(g_ras_model, i)) + g_tab + job_safe(get_text(g_ras_element, i)) + g_tab +
                    folder + g_tab +
                    num(get_real(g_ras_xmin, i)) + g_tab + num(get_real(g_ras_ymin, i)) + g_tab +
                    num(get_real(g_ras_xmax, i)) + g_tab + num(get_real(g_ras_ymax, i));
        File_write_line(f, line);
    }
}

void write_sheet_record(File f, Integer s, Integer seq, Text source, Text out_name, Real w, Real h,
                        Text raster_ids, Text below, Text vplot, Text tplot)
{
    Text line = "sheet" + g_tab + itxt(seq) + g_tab +
                job_safe(get_text(g_sh_name, s)) + g_tab + job_safe(get_text(g_sh_chapter, s)) + g_tab +
                source + g_tab + "1" + g_tab + job_safe(out_name) + g_tab +
                num(w) + g_tab + num(h) + g_tab +
                num(get_real(g_sh_ml, s)) + g_tab + num(get_real(g_sh_mb, s)) + g_tab +
                num(get_real(g_sh_mr, s)) + g_tab + num(get_real(g_sh_mt, s)) + g_tab +
                num(get_real(g_sh_scale, s)) + g_tab + num(get_real(g_sh_rot, s)) + g_tab +
                num(get_real(g_sh_ox, s)) + g_tab + num(get_real(g_sh_oy, s)) + g_tab +
                raster_ids + g_tab + below + g_tab + vplot + g_tab + tplot;
    File_write_line(f, line);
}

Text powershell_exe()
{
    Text root = Getenv("SystemRoot");
    if(root == "") root = "C:\\Windows";
    return root + "\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

Text engine_args(Text job_path)
{
    return "-ExecutionPolicy Bypass -NoProfile -File \"" + engine_dir() + "\\" + ENGINE_PS + "\" -Job \"" + job_path + "\"";
}

//---------------------------------------------------------------------
// run_engine
//   Runs the PowerShell engine on the job and waits for it.  With
//   CREATE_NEW_CONSOLE its window shows progress and closes by itself;
//   CREATE_NO_WINDOW runs it out of sight.
//---------------------------------------------------------------------
Integer run_engine(Text job_path, Text work_dir, Integer window_flags)
{
    Text args = engine_args(job_path);
#ifdef DEBUG
    log_msg("run_engine: " + powershell_exe() + " " + args);
#endif
    Integer r = Create_process(powershell_exe(), args, work_dir, window_flags, 1, 0);
#ifdef DEBUG
    log_msg("  Create_process returned " + itxt(r));
#endif
    return r;
}

//---------------------------------------------------------------------
// run_engine_async
//   Starts the engine out of sight and returns at once; Process_exists
//   on the handle is non-zero while it runs.  Returns 0 when started.
//---------------------------------------------------------------------
Integer run_engine_async(Text job_path, Text work_dir, Process_Handle &handle)
{
    Text args = engine_args(job_path);
    log_msg("run_engine_async: " + powershell_exe() + " " + args);
    Integer r = Create_process(powershell_exe(), args, work_dir, CREATE_NO_WINDOW, 0, handle);
    Integer alive = Process_exists(handle);
    log_msg("  Create_process returned " + itxt(r) + ", running " + itxt(alive));
    // the return code of this overload is not documented: a process
    // that is running has started, whatever it says
    if(alive != 0) return 0;
    return r;
}

//---------------------------------------------------------------------
// launch_monitor
//   Opens the progress window without waiting for it: it tails the
//   progress file until the run writes FASTPLOT_DONE.
//---------------------------------------------------------------------
void launch_monitor(Text progress_path, Text work_dir)
{
    Text args = "-ExecutionPolicy Bypass -NoProfile -File \"" + engine_dir() + "\\" + ENGINE_MON + "\" -Progress \"" + progress_path + "\"";
    Integer r = Create_process(powershell_exe(), args, work_dir, CREATE_NEW_CONSOLE, 0, 0);
    log_msg("launch_monitor -> " + itxt(r));
}

//---------------------------------------------------------------------
// probe_job
//   Writes the job that asks the engine whether it can read each
//   raster's tiles.  Returns its path, or "" when there is nothing to
//   ask (or it cannot be written).
//---------------------------------------------------------------------
Text probe_job(Text work)
{
    if(g_n_rasters == 0) return "";
    Text job_path = work + "\\probe.txt";
    Text result_path = work + "\\probe_result.txt";
    if(File_exists(result_path) != 0) File_delete(result_path);
    File f;
    if(open_job(job_path, f) != 0) return "";
    job_line(f, "mode", "probe");
    job_line(f, "result", result_path);
    job_line(f, "log", temp_dir() + "\\FastPlot\\engine.log");
    write_raster_records(f, project_folder(), 0);
    File_close(f);
    return job_path;
}

Integer probe_read(Text work);

//---------------------------------------------------------------------
// probe_rasters
//   The probe, waiting for the engine.  Sets g_ras_ok / g_ras_info.
//   Returns the number usable.
//---------------------------------------------------------------------
Integer probe_rasters(Text work)
{
    Text job_path = probe_job(work);
    if(job_path == "") return 0;
    run_engine(job_path, work, CREATE_NO_WINDOW);
    return probe_read(work);
}

//---------------------------------------------------------------------
// probe_read - the engine's answer to probe_job
//---------------------------------------------------------------------
Integer probe_read(Text work)
{
    if(g_n_rasters == 0) return 0;
    Text result_path = work + "\\probe_result.txt";
    Integer usable = 0;
    if(File_exists(result_path) == 0)
    {
        Print("  The engine did not answer the raster check - 12d draws the aerials.\n");
        return 0;
    }
    File rf;
    if(File_open(result_path, "r", "ccs=UTF-8", rf) != 0) return 0;
    Text line = "";
    while(File_read_line(rf, line) == 0)
    {
        if(field(line, 1) != "RASTER") continue;
        Text id = field(line, 2);
        Integer r = 0;
        if(Text_length(id) > 1) From_text(Get_subtext(id, 2, Text_length(id)), r);
        if(r < 1) continue;
        if(r > g_n_rasters) continue;
        if(field(line, 3) == "OK")
        {
            Set_item(g_ras_ok, r, 1);
            Set_item(g_ras_info, r, field(line, 5) + " in " + field(line, 4));
            usable++;
        }
        else Set_item(g_ras_info, r, "NOT USABLE: " + field(line, 4));
    }
    File_close(rf);
    return usable;
}

//---------------------------------------------------------------------
// report_result
//   Prints the engine's result file to the Output Window.  Returns 0
//   when the engine finished, with the main output file and its size,
//   and the number of sheet errors.
//---------------------------------------------------------------------
Integer report_result(Text result_path, Text &main_file, Real &main_mb, Integer &n_errors, Text &status_msg)
{
    main_file = "";
    main_mb = 0.0;
    n_errors = 0;
    status_msg = "";
    if(File_exists(result_path) == 0)
    {
        status_msg = "The PDF engine left no result - see " + temp_dir() + "\\FastPlot\\engine.log";
        return 1;
    }
    File f;
    if(File_open(result_path, "r", "ccs=UTF-8", f) != 0)
    {
        status_msg = "Cannot read " + result_path;
        return 2;
    }
    Integer ok = 0;
    Integer n_files = 0;
    Text line = "";
    while(File_read_line(f, line) == 0)
    {
        Text kind = field(line, 1);
        if(kind == "STATUS")
        {
            if(field(line, 2) == "OK")
            {
                ok = 1;
                Print("Engine finished in " + field(line, 3) + " s\n");
            }
            else
            {
                status_msg = "Engine error: " + field(line, 3);
                Print(status_msg + "\n");
            }
            continue;
        }
        if(kind == "SHEET")
        {
            Text state = field(line, 4);
            if(state != "OK") n_errors++;
            Print("  " + field(line, 2) + "  " + field(line, 3) + "  " + state + "  " + field(line, 5) + "\n");
            continue;
        }
        if(kind == "FILE")
        {
            n_files++;
            Real bytes = 0.0;
            From_text(field(line, 3), bytes);
            if(n_files == 1)
            {
                main_file = field(line, 2);
                main_mb = bytes / 1048576.0;
            }
            Print("  -> " + field(line, 2) + "  (" + To_text(bytes / 1048576.0, 2) + " MB)\n");
            continue;
        }
        if(kind == "TIMING")
        {
            Print("12d drew the vector sheets in " + field(line, 3) + " s\n");
            continue;
        }
        if(kind == "WARN")
        {
            Print("  WARNING " + field(line, 2) + " '" + field(line, 3) + "': " + field(line, 4) + "\n");
            continue;
        }
        if(kind == "INPUT")
        {
            Real bytes = 0.0;
            From_text(field(line, 3), bytes);
            Print("  " + field(line, 2) + "  (" + To_text(bytes / 1048576.0, 2) + " MB, " + field(line, 4) + ", " + field(line, 5) + ")\n");
            continue;
        }
        if(kind == "TOTAL")
        {
            Real bytes = 0.0;
            From_text(field(line, 3), bytes);
            Print("Total: " + field(line, 2) + " pages, " + To_text(bytes / 1048576.0, 2) + " MB\n");
            if(n_files > 1) main_mb = bytes / 1048576.0;
            continue;
        }
    }
    File_close(f);
    if(ok == 0) return 3;
    return 0;
}

#endif
