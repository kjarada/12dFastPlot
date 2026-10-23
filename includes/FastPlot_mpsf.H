//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_mpsf.H
//
// Brief description
// Reads a Multi Plot Sheet file (.12dmpsf, UTF-16 XML) into the sheet
// table in FastPlot_globals.H.
//
// The file is a tree of <page>s - book, chapters, sheets - and a sheet
// is any page whose <children> hold <shape>s.  FastPlot plots a sheet
// that has exactly one plan frame and at most one title block, which is
// the usual plan sheet.  Anything else on a sheet
// (section frames, pictures, text shapes, a second viewport) is
// reported, and the sheet is left for 12d's own plotter.
//
// Facts this relies on, checked against 12d's own output of this file:
//   * plan_frame origin_x/origin_y is the world point at the CENTRE of
//     the viewport (the sheet less the frame's four margins)
//   * the margins are millimetres on the sheet
//   * title block user_variables_set / _name / _value are parallel
//     lists; an index can repeat, the first one is the one used
//---------------------------------------------------------------------

#ifndef FASTPLOT_MPSF_H
#define FASTPLOT_MPSF_H

// List being read inside a title block
#define UVL_NONE    0
#define UVL_SET     1
#define UVL_NAME    2
#define UVL_VALUE   3

//---------------------------------------------------------------------
// reset_sheet_table
//---------------------------------------------------------------------
void reset_sheet_table()
{
    g_n_sheets = 0;
    Null(g_sh_name);     Null(g_sh_chapter);  Null(g_sh_view);
    Null(g_sh_plotter);  Null(g_sh_paper);    Null(g_sh_tbf);
    Null(g_sh_tb_colour); Null(g_sh_problem);
    Null(g_sh_fmodel);   Null(g_sh_fcolour);  Null(g_sh_fstyle);  Null(g_sh_fweight);
    Null(g_sh_scale);    Null(g_sh_rot);      Null(g_sh_ox);   Null(g_sh_oy);
    Null(g_sh_ml);       Null(g_sh_mr);       Null(g_sh_mb);   Null(g_sh_mt);
    Null(g_sh_tb_size);
    Null(g_sh_active);   Null(g_sh_uv_first); Null(g_sh_uv_count);
    g_n_uv = 0;
    Null(g_uv_index);    Null(g_uv_alias);    Null(g_uv_value);
    g_book_name = "";
    g_book_target = "";
    g_book_paper = "";
    g_book_plotter = "";
}

//---------------------------------------------------------------------
// read_mpsf
//   Fills the sheet table.  Returns 0 on success, with err set and a
//   non-zero return when the file cannot be read or holds no sheets.
//---------------------------------------------------------------------
Integer read_mpsf(Text path, Text &err)
{
    err = "";
    reset_sheet_table();

    if(File_exists(path) == 0)
    {
        err = "Plot sheet file not found: " + path;
        return 1;
    }
    File f;
    Integer ret = File_open(path, "r", "ccs=UNICODE", f);
    if(ret != 0) ret = File_open(path, "r", "ccs=UTF-8", f);
    if(ret != 0)
    {
        err = "Cannot open " + path + " (" + itxt(ret) + ")";
        return 2;
    }

    // ---- page stack.  Every Text slot is seeded: an unset Text is a
    // null handle, not "".
    Text    ps_name[MAX_PAGE_DEPTH];
    Text    ps_plotter[MAX_PAGE_DEPTH];
    Text    ps_paper[MAX_PAGE_DEPTH];
    Text    ps_target[MAX_PAGE_DEPTH];
    Integer ps_active[MAX_PAGE_DEPTH];
    Integer ps_shapes[MAX_PAGE_DEPTH];
    Integer d;
    for(d = 1; d <= MAX_PAGE_DEPTH; d++)
    {
        ps_name[d] = "";
        ps_plotter[d] = "";
        ps_paper[d] = "";
        ps_target[d] = "";
        ps_active[d] = 1;
        ps_shapes[d] = 0;
    }
    Integer depth = 0;

    // ---- the sheet being collected
    Integer n_pf = 0, n_tb = 0, n_other = 0;
    Text    other_kinds = "";
    Text    view = "", tbf = "", tb_colour = "";
    Text    f_model = "", f_colour = "", f_style = "";
    Real    f_weight = 0.0;
    Real    scale = 0.0, rot = 0.0, ox = 0.0, oy = 0.0;
    Real    ml = 0.0, mr = 0.0, mb = 0.0, mt = 0.0, tb_size = 5.0;
    Integer pf_active = 1, tb_use_file = 1, pf_view_mode_ok = 1;

    Text uv_set[MAX_UV];
    Text uv_name[MAX_UV];
    Text uv_val[MAX_UV];
    Integer k;
    for(k = 1; k <= MAX_UV; k++)
    {
        uv_set[k] = "";
        uv_name[k] = "";
        uv_val[k] = "";
    }
    Integer n_set = 0, n_name = 0, n_val = 0;

    Integer shape_depth = 0;        // <shape> nesting
    Text    kind = "";              // plan_frame / title_block inside the current shape
    Integer kind_first = 0;         // 1 while reading the FIRST shape of that kind
    Integer uv_list = UVL_NONE;
    Integer in_book = 0;

    Integer lines = 0;
    Text line = "";
    while(File_read_line(f, line) == 0)
    {
        lines++;
        Text tag = "", value = "";
        Integer kind_of_line = xml_line(line, tag, value);
        if(kind_of_line == 0) continue;

        // ----------------------------------------------------- opening
        if(kind_of_line == 2)
        {
            if(tag == "book") in_book = 1;
            if(in_book == 0) continue;

            if(tag == "page")
            {
                if(depth < MAX_PAGE_DEPTH) depth++;
                ps_name[depth] = "";
                ps_plotter[depth] = "";
                ps_paper[depth] = "";
                ps_target[depth] = "";
                ps_active[depth] = 1;
                ps_shapes[depth] = 0;

                n_pf = 0; n_tb = 0; n_other = 0; other_kinds = "";
                view = ""; tbf = ""; tb_colour = "";
                f_model = ""; f_colour = ""; f_style = ""; f_weight = 0.0;
                scale = 0.0; rot = 0.0; ox = 0.0; oy = 0.0;
                ml = 0.0; mr = 0.0; mb = 0.0; mt = 0.0; tb_size = 5.0;
                pf_active = 1; tb_use_file = 1; pf_view_mode_ok = 1;
                n_set = 0; n_name = 0; n_val = 0;
                continue;
            }
            if(tag == "shape")
            {
                shape_depth++;
                if(shape_depth == 1 && depth >= 1) ps_shapes[depth] = 1;
                kind = "";
                kind_first = 0;
                continue;
            }
            if(shape_depth == 1 && kind == "")
            {
                if(tag == "plan_frame")
                {
                    n_pf++;
                    kind = tag;
                    kind_first = 0;
                    if(n_pf == 1) kind_first = 1;
                    continue;
                }
                if(tag == "title_block")
                {
                    n_tb++;
                    kind = tag;
                    kind_first = 0;
                    if(n_tb == 1) kind_first = 1;
                    continue;
                }
                if(tag != "children" && tag != "comment")
                {
                    n_other++;
                    kind = tag;
                    kind_first = 0;
                    other_kinds = other_kinds + " " + tag;
                    continue;
                }
            }
            if(shape_depth >= 2 && tag != "children" && tag != "comment")
            {
                // a shape inside a shape (sub-frame etc) - not supported
                if(tag == "plan_frame" || tag == "title_block")
                {
                    n_other++;
                    other_kinds = other_kinds + " nested " + tag;
                }
                continue;
            }
            if(kind == "title_block" && kind_first)
            {
                if(tag == "user_variables_set")   { uv_list = UVL_SET;   continue; }
                if(tag == "user_variables_name")  { uv_list = UVL_NAME;  continue; }
                if(tag == "user_variables_value") { uv_list = UVL_VALUE; continue; }
            }
            continue;
        }

        // ----------------------------------------------------- closing
        if(kind_of_line == 3)
        {
            if(in_book == 0) continue;
            if(tag == "book")
            {
                in_book = 0;
                continue;
            }
            if(tag == "user_variables_set" || tag == "user_variables_name" || tag == "user_variables_value")
            {
                uv_list = UVL_NONE;
                continue;
            }
            if(tag == "plan_frame" || tag == "title_block")
            {
                if(shape_depth == 1)
                {
                    kind = "done";
                    kind_first = 0;
                }
                continue;
            }
            if(tag == "shape")
            {
                if(shape_depth > 0) shape_depth--;
                if(shape_depth == 0) kind = "";
                continue;
            }
            if(tag == "page")
            {
                if(depth < 1) continue;
                if(depth == 1)
                {
                    g_book_name    = ps_name[1];
                    g_book_target  = ps_target[1];
                    g_book_paper   = ps_paper[1];
                    g_book_plotter = ps_plotter[1];
                }
                if(ps_shapes[depth] && g_n_sheets >= MAX_SHEETS)
                {
                    log_msg("read_mpsf: more than " + itxt(MAX_SHEETS) + " sheets - the rest are ignored");
                    ps_shapes[depth] = 0;
                }
                if(ps_shapes[depth])
                {
                    // inherit plotter / paper from the nearest page that sets them
                    Text plotter = "", paper = "";
                    Integer active = 1;
                    Integer e;
                    for(e = depth; e >= 1; e--)
                    {
                        if(plotter == "") plotter = ps_plotter[e];
                        if(paper == "") paper = ps_paper[e];
                        if(ps_active[e] == 0) active = 0;
                    }
                    Text chapter = "";
                    if(depth >= 3) chapter = ps_name[depth - 1];

                    Text problem = "";
                    if(n_pf == 0) problem = "no plan frame";
                    if(n_pf > 1) problem = itxt(n_pf) + " plan frames";
                    if(n_tb > 1) problem = itxt(n_tb) + " title blocks";
                    if(n_other > 0) problem = "unsupported shape(s):" + other_kinds;
                    if(problem == "")
                    {
                        if(pf_active == 0) problem = "plan frame is inactive";
                        if(pf_view_mode_ok == 0) problem = "plan frame does not draw a view";
                        if(view == "") problem = "plan frame has no view";
                        if(scale <= 0.0) problem = "plan frame has no scale";
                    }
                    if(problem == "" && n_tb == 1 && tb_use_file == 0) problem = "title block does not use a title file";
                    if(paper == "") paper = "A3 Landscape";

                    g_n_sheets++;
                    Append(ps_name[depth], g_sh_name);
                    Append(chapter,        g_sh_chapter);
                    Append(view,           g_sh_view);
                    Append(plotter,        g_sh_plotter);
                    Append(paper,          g_sh_paper);
                    Append(tbf,            g_sh_tbf);
                    Append(tb_colour,      g_sh_tb_colour);
                    Append(problem,        g_sh_problem);
                    Append(f_model,        g_sh_fmodel);
                    Append(f_colour,       g_sh_fcolour);
                    Append(f_style,        g_sh_fstyle);
                    Append(f_weight,       g_sh_fweight);
                    Append(scale,   g_sh_scale);
                    Append(rot,     g_sh_rot);
                    Append(ox,      g_sh_ox);
                    Append(oy,      g_sh_oy);
                    Append(ml,      g_sh_ml);
                    Append(mr,      g_sh_mr);
                    Append(mb,      g_sh_mb);
                    Append(mt,      g_sh_mt);
                    Append(tb_size, g_sh_tb_size);
                    Append(active,  g_sh_active);

                    // user variables: first occurrence of each index wins
                    Append(g_n_uv + 1, g_sh_uv_first);
                    Integer used = 0;
                    Integer m = n_set;
                    if(n_name < m) m = n_name;
                    if(n_val < m) m = n_val;
                    for(k = 1; k <= m; k++)
                    {
                        Integer idx = 0;
                        if(From_text(Text_justify(uv_set[k]), idx) != 0) continue;
                        if(idx < 1) continue;
                        Integer seen = 0;
                        Integer q;
                        for(q = 1; q < k; q++)
                        {
                            Integer prev = 0;
                            if(From_text(Text_justify(uv_set[q]), prev) == 0)
                            {
                                if(prev == idx) seen = 1;
                            }
                        }
                        if(seen) continue;
                        g_n_uv++;
                        used++;
                        Append(idx,        g_uv_index);
                        Append(uv_name[k], g_uv_alias);
                        Append(uv_val[k],  g_uv_value);
                    }
                    Append(used, g_sh_uv_count);
#ifdef DEBUG
                    log_msg("sheet " + itxt(g_n_sheets) + " '" + ps_name[depth] + "' chapter '" + chapter + "' view '" + view +
                            "' 1:" + num(scale) + " origin " + num(ox) + "," + num(oy) + " uv " + itxt(used) +
                            " problem '" + problem + "'");
#endif
                    n_pf = 0; n_tb = 0; n_other = 0;
                }
                depth--;
                continue;
            }
            continue;
        }

        // ------------------------------------------------- leaf values
        if(in_book == 0) continue;
        if(kind_of_line == 4) value = "";

        if(shape_depth == 0)
        {
            if(depth < 1) continue;
            if(tag == "name")         ps_name[depth] = value;
            if(tag == "plotter_type") ps_plotter[depth] = value;
            if(tag == "paper_code")   ps_paper[depth] = value;
            if(tag == "target_name")  ps_target[depth] = value;
            if(tag == "is_active")
            {
                if(value == "false") ps_active[depth] = 0;
            }
            continue;
        }
        if(shape_depth != 1) continue;

        if(kind == "plan_frame" && kind_first)
        {
            Real rv = 0.0;
            if(tag == "view_to_draw")  view = value;
            if(tag == "frame_model")   f_model = value;
            if(tag == "colour")        f_colour = value;
            if(tag == "linestyle")     f_style = value;
            if(tag == "weight")        { if(From_text(value, rv) == 0) f_weight = rv; }
            if(tag == "scale")         { if(From_text(value, rv) == 0) scale = rv; }
            if(tag == "rotation")      { if(From_text(value, rv) == 0) rot = rv; }
            if(tag == "origin_x")      { if(From_text(value, rv) == 0) ox = rv; }
            if(tag == "origin_y")      { if(From_text(value, rv) == 0) oy = rv; }
            if(tag == "left_margin")   { if(From_text(value, rv) == 0) ml = rv; }
            if(tag == "right_margin")  { if(From_text(value, rv) == 0) mr = rv; }
            if(tag == "bottom_margin") { if(From_text(value, rv) == 0) mb = rv; }
            if(tag == "top_margin")    { if(From_text(value, rv) == 0) mt = rv; }
            if(tag == "active")
            {
                if(value == "false") pf_active = 0;
            }
            if(tag == "view_mode")
            {
                if(value != "view") pf_view_mode_ok = 0;
            }
            continue;
        }
        if(kind == "title_block" && kind_first)
        {
            if(tag == "user_variable")
            {
                if(uv_list == UVL_SET && n_set < MAX_UV)     { n_set++;  uv_set[n_set] = value; }
                if(uv_list == UVL_NAME && n_name < MAX_UV)   { n_name++; uv_name[n_name] = value; }
                if(uv_list == UVL_VALUE && n_val < MAX_UV)   { n_val++;  uv_val[n_val] = value; }
                continue;
            }
            Real tv = 0.0;
            if(tag == "title_file")    tbf = value;
            if(tag == "text_colour")   tb_colour = value;
            if(tag == "text_size")     { if(From_text(value, tv) == 0) tb_size = tv; }
            if(tag == "use_title_file")
            {
                if(value == "false") tb_use_file = 0;
            }
            continue;
        }
    }
    File_close(f);

#ifdef DEBUG
    log_msg("read_mpsf: " + itxt(lines) + " lines, " + itxt(g_n_sheets) + " sheets, " + itxt(g_n_uv) + " user variables");
    log_msg("  book '" + g_book_name + "' target '" + g_book_target + "' paper '" + g_book_paper + "' plotter '" + g_book_plotter + "'");
#endif
    if(g_n_sheets == 0)
    {
        err = "No sheets found in " + path;
        return 3;
    }
    return 0;
}

#endif
