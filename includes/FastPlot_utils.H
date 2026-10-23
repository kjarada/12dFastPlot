//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_utils.H
//
// Brief description
// Logging, text, list, path and XML-line helpers for FastPlot.
//---------------------------------------------------------------------

#ifndef FASTPLOT_UTILS_H
#define FASTPLOT_UTILS_H

//---------------------------------------------------------------------
// log_msg
//   File only, flushed per line so a crash keeps everything before it.
//---------------------------------------------------------------------
void log_msg(Text msg)
{
    if(g_log_open)
    {
        File_write_line(g_log, msg);
        File_flush(g_log);
    }
}

//---------------------------------------------------------------------
// Progress window.  12d cannot respond while the macro runs, so a
// console window (FastPlot_monitor.ps1) shows these lines as they are
// written; the engine appends its own to the same file.  "a" mode puts
// every write at the true end of the file, after the engine's lines.
//---------------------------------------------------------------------
void prog(Text msg)
{
    if(g_prog_open)
    {
        File_write_line(g_prog, msg);
        File_flush(g_prog);
    }
}

Integer prog_open(Text path)
{
    if(g_prog_open) return 0;
    if(File_open(path, "a", "ccs=UTF-8", g_prog) != 0) return 1;
    g_prog_open = 1;
    return 0;
}

// Last line of a run; the window closes itself after it
void prog_finish(Text msg)
{
    if(g_prog_open == 0) return;
    if(msg != "") prog(msg);
    prog("FASTPLOT_DONE");
    File_close(g_prog);
    g_prog_open = 0;
}

// The engine writes the rest of the run, and its last line
void prog_close()
{
    if(g_prog_open == 0) return;
    File_close(g_prog);
    g_prog_open = 0;
}

// Seconds since midnight, with the fraction - for timings in the log
Real clock_s()
{
    Integer h = 0, m = 0;
    Real s = 0.0;
    if(Get_time(h, m, s) != 0) return 0.0;
    return h * 3600.0 + m * 60.0 + s;
}

// Output Window, progress window and log: for anything the user needs to see
void say(Text msg)
{
    Print(msg + "\n");
    log_msg(msg);
    prog(msg);
}

//---------------------------------------------------------------------
// temp_dir
//   The user's TEMP folder - never next to the .4do, which may sit in a
//   folder the user cannot write to.
//---------------------------------------------------------------------
Text temp_dir()
{
    Text d = Getenv("TEMP");
    if(d == "") d = Getenv("TMP");
    if(d == "") d = ".";
    return d;
}

// Folder of the open project (the .12dmodel folder); TEMP if unknown
Text project_folder()
{
    Text f = "";
    if(Get_project_folder(f) != 0) f = "";
    if(f == "") f = temp_dir();
    return f;
}

Text file_of(Text path);

//---------------------------------------------------------------------
// project_plot_sheet
//   The plot sheet file (.12dmpsf) in the project folder when there is
//   exactly one, else "".
//---------------------------------------------------------------------
Text project_plot_sheet()
{
    Text pf = "";
    if(Get_project_folder(pf) != 0) return "";
    if(pf == "") return "";
    Integer n = 0;
    Dynamic_Text names;
    if(List_files_in_folder(pf, "*.12dmpsf", n, names) != 0) return "";
    if(n != 1) return "";
    Text f = get_text(names, 1);
    if(f == "") return "";
    // the manual does not say whether the name comes with its folder
    if(file_of(f) == f) f = pf + "\\" + f;
    if(File_exists(f) == 0) return "";
    log_msg("project plot sheet: " + f);
    return f;
}

void open_log()
{
#ifdef DEBUG
    Text log_path = temp_dir() + "\\FastPlot.log";
    Integer fret = File_open(log_path, "w", "", g_log);
    if(fret == 0)
    {
        g_log_open = 1;
        File_write_line(g_log, "=== " + Get_macro_name() + " ===");
        File_write_line(g_log, "Log:   " + log_path);
        File_flush(g_log);
    }
#endif
}

void close_log()
{
    if(g_log_open)
    {
        File_close(g_log);
        g_log_open = 0;
    }
}

//---------------------------------------------------------------------
// Number and list helpers.  Dynamic lists are 1-based.  An item that
// cannot be read comes back as "" / 0 rather than an unset handle.
//---------------------------------------------------------------------
Text num(Real v)
{
    return To_text(v, 6);
}

Text itxt(Integer v)
{
    return To_text(v);
}

Text get_text(Dynamic_Text &list, Integer i)
{
    Text t = "";
    if(Get_item(list, i, t) != 0) t = "";
    return t;
}

Real get_real(Dynamic_Real &list, Integer i)
{
    Real v = 0.0;
    if(Get_item(list, i, v) != 0) v = 0.0;
    return v;
}

Integer get_int(Dynamic_Integer &list, Integer i)
{
    Integer v = 0;
    if(Get_item(list, i, v) != 0) v = 0;
    return v;
}

//---------------------------------------------------------------------
// replace_all
//   Every occurrence of from replaced by to (from must not be empty).
//---------------------------------------------------------------------
Text replace_all(Text s, Text from, Text to)
{
    if(from == "") return s;
    Integer flen = Text_length(from);
    Text out = "";
    Text rest = s;
    Integer p = Find_text(rest, from);
    while(p > 0)
    {
        if(p > 1) out = out + Get_subtext(rest, 1, p - 1);
        out = out + to;
        Integer rlen = Text_length(rest);
        if(p + flen > rlen)
        {
            rest = "";
            break;
        }
        rest = Get_subtext(rest, p + flen, rlen);
        p = Find_text(rest, from);
    }
    return out + rest;
}

Text xml_unescape(Text t)
{
    if(Find_text(t, "&") == 0) return t;
    Text r = t;
    r = replace_all(r, "&lt;",   "<");
    r = replace_all(r, "&gt;",   ">");
    r = replace_all(r, "&quot;", "\"");
    r = replace_all(r, "&apos;", "'");
    r = replace_all(r, "&amp;",  "&");
    return r;
}

// Tabs and line breaks would break the engine's tab-separated job file
Text job_safe(Text t)
{
    Text tab = " ";
    Set_char(tab, 1, 9);
    Text r = replace_all(t, tab, " ");
    Text cr = " ";
    Set_char(cr, 1, 13);
    r = replace_all(r, cr, " ");
    Text lf = " ";
    Set_char(lf, 1, 10);
    return replace_all(r, lf, " ");
}

//---------------------------------------------------------------------
// xml_line
//   Classifies one line of 12d's pretty-printed XML.
//     0  not a tag line
//     1  <tag>value</tag>          (value unescaped)
//     2  <tag>                     (opening tag)
//     3  </tag>                    (closing tag)
//     4  <tag/>                    (empty leaf)
//   Attributes are dropped from the tag name.  12d writes one element
//   per line, which is all this has to handle.
//---------------------------------------------------------------------
Integer xml_line(Text line, Text &tag, Text &value)
{
    tag = "";
    value = "";
    Text t = Text_justify(line);
    Integer n = Text_length(t);
    if(n < 3) return 0;

    Integer c1 = 0, c2 = 0;
    Get_char(t, 1, c1);
    Get_char(t, 2, c2);
    if(c1 != 60) return 0;                          // '<'
    if(c2 == 63 || c2 == 33) return 0;              // <? or <!

    Integer gt = Find_text(t, ">");
    if(gt == 0) return 0;

    if(c2 == 47)                                    // </tag>
    {
        tag = Get_subtext(t, 3, gt - 1);
        return 3;
    }

    Integer cb = 0;
    Get_char(t, gt - 1, cb);
    Integer self_closing = 0;
    if(cb == 47) self_closing = 1;                  // <tag/>

    Text head = Get_subtext(t, 2, gt - 1 - self_closing);
    Integer sp = Find_text(head, " ");
    if(sp > 0) head = Get_subtext(head, 1, sp - 1);
    tag = head;
    if(self_closing) return 4;
    if(gt == n) return 2;

    Text rest = Get_subtext(t, gt + 1, n);
    Integer close = Find_text(rest, "</" + tag + ">");
    if(close == 0) return 2;
    if(close > 1) value = xml_unescape(Get_subtext(rest, 1, close - 1));
    return 1;
}

//---------------------------------------------------------------------
// field
//   n-th (1-based) tab separated field of a line, "" when missing.
//---------------------------------------------------------------------
Text field(Text line, Integer n)
{
    Text rest = line;
    Integer i;
    for(i = 1; i < n; i++)
    {
        Integer p = Find_text(rest, g_tab);
        if(p == 0) return "";
        Integer len = Text_length(rest);
        if(p >= len) return "";
        rest = Get_subtext(rest, p + 1, len);
    }
    Integer q = Find_text(rest, g_tab);
    if(q == 0) return rest;
    if(q == 1) return "";
    return Get_subtext(rest, 1, q - 1);
}

//---------------------------------------------------------------------
// Path helpers
//---------------------------------------------------------------------
Text dir_of(Text path)
{
    Integer n = Text_length(path);
    Integer i;
    for(i = n; i >= 1; i--)
    {
        Integer c = 0;
        Get_char(path, i, c);
        if(c == 92 || c == 47)
        {
            if(i == 1) return "";
            return Get_subtext(path, 1, i - 1);
        }
    }
    return "";
}

Text file_of(Text path)
{
    Integer n = Text_length(path);
    Integer i;
    for(i = n; i >= 1; i--)
    {
        Integer c = 0;
        Get_char(path, i, c);
        if(c == 92 || c == 47)
        {
            if(i == n) return "";
            return Get_subtext(path, i + 1, n);
        }
    }
    return path;
}

// File name without its extension
Text stem_of(Text path)
{
    Text f = file_of(path);
    Integer n = Text_length(f);
    Integer i;
    for(i = n; i >= 1; i--)
    {
        Integer c = 0;
        Get_char(f, i, c);
        if(c == 46)
        {
            if(i == 1) return f;
            return Get_subtext(f, 1, i - 1);
        }
    }
    return f;
}

// Characters Windows refuses in a file name become '_'
Text safe_file_name(Text name)
{
    Text r = name;
    Text bad[9];
    bad[1] = "\\"; bad[2] = "/"; bad[3] = ":"; bad[4] = "*"; bad[5] = "?";
    bad[6] = "\""; bad[7] = "<"; bad[8] = ">"; bad[9] = "|";
    Integer i;
    for(i = 1; i <= 9; i++) r = replace_all(r, bad[i], "_");
    return Text_justify(r);
}

//---------------------------------------------------------------------
// paper_size
//   Width and height (mm) of a paper name as plot sheets write it:
//   "A3 Landscape", "A3 Portrait", "A3", "A3P", "A1 Landscape" ...
//   12d's own sheet list only knows "A3" style names; names such as
//   "A3 Landscape" come from a user sheets.4d that
//   Set_plot_frame_sheet_size does not read.  Returns 0 when known.
//---------------------------------------------------------------------
Integer paper_size(Text code, Real &w, Real &h)
{
    w = 0.0;
    h = 0.0;
    Text c = Text_lower(replace_all(Text_justify(code), " ", ""));
    if(Text_length(c) < 2) return 1;
    Text base = Get_subtext(c, 1, 2);
    if(base == "a0") { w = 1189.0; h = 841.0; }
    if(base == "a1") { w = 841.0;  h = 594.0; }
    if(base == "a2") { w = 594.0;  h = 420.0; }
    if(base == "a3") { w = 420.0;  h = 297.0; }
    if(base == "a4") { w = 297.0;  h = 210.0; }
    if(base == "b1") { w = 1000.0; h = 707.0; }
    if(w <= 0.0) return 2;
    // landscape unless the name says portrait ("A3 Portrait", "A3P")
    Integer portrait = 0;
    if(Find_text(c, "portrait") > 0) portrait = 1;
    if(c == base + "p") portrait = 1;
    if(portrait)
    {
        Real t = w;
        w = h;
        h = t;
    }
    return 0;
}

//---------------------------------------------------------------------
// parse_sheet_list
//   "all" / "" selects every sheet; otherwise numbers and ranges by
//   position in the book: "1-5, 8, 12-20".  sel[i] = 1 when chosen.
//   Returns the number chosen, -1 when the text cannot be read.
//---------------------------------------------------------------------
Integer parse_sheet_list(Text spec, Integer n, Integer sel[])
{
    Integer i;
    for(i = 1; i <= MAX_SHEETS; i++) sel[i] = 0;
    Text s = Text_lower(Text_justify(spec));
    if(s == "" || s == "all" || s == "*")
    {
        for(i = 1; i <= n; i++) sel[i] = 1;
        return n;
    }
    s = replace_all(s, " ", "") + ",";
    Integer count = 0;
    while(Text_length(s) > 0)
    {
        Integer p = Find_text(s, ",");
        Text part = "";
        if(p > 1) part = Get_subtext(s, 1, p - 1);
        Integer len = Text_length(s);
        if(p >= len) s = "";
        else s = Get_subtext(s, p + 1, len);
        if(part == "") continue;

        Integer a = 0, b = 0;
        Integer dash = Find_text(part, "-");
        if(dash > 1)
        {
            if(From_text(Get_subtext(part, 1, dash - 1), a) != 0) return -1;
            if(From_text(Get_subtext(part, dash + 1, Text_length(part)), b) != 0) return -1;
        }
        else
        {
            if(From_text(part, a) != 0) return -1;
            b = a;
        }
        if(a > b)
        {
            Integer t = a;
            a = b;
            b = t;
        }
        for(i = a; i <= b; i++)
        {
            if(i < 1) continue;
            if(i > n) continue;
            if(sel[i] == 0) count++;
            sel[i] = 1;
        }
    }
    return count;
}

#endif
