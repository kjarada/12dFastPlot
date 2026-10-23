//---------------------------------------------------------------------
// Programmer           Kamal Jarada
// Date                 21/10/26
// 12d Model            15
// Version              01
// File                 FastPlot_plot.H
//
// Brief description
// The 12d half of FastPlot: per sheet, 12d itself draws everything
// except the aerial, so linestyles, symbols, text styles, drafting and
// the title block are exactly what 12d draws.
//
//   study_view     lists a view's models in order and finds its rasters
//   hide_rasters   keeps the aerial out of 12d's plot, one of two ways:
//                  HIDE_MOVE  parks the raster elements in a hold model
//                             for the run - the view itself, its model
//                             order and its per-model settings are not
//                             touched, so the vectors plot exactly as
//                             in the Multi Plot Sheet
//                  HIDE_COPY  plots from a temporary copy of the view
//                             without the raster-only models (nothing
//                             in the project is modified, but per-view
//                             model settings are not carried over)
//   restore_rasters  moves parked rasters home (also on the next start
//                  after an interrupted run)
//   plot_sheet     builds a plot frame from the sheet (sheet size,
//                  margins, scale, origin, title file and user text)
//                  and plots it through a plot_frame_plot parameter file
//
// The aerial is what makes the built-in plotter slow and its PDFs huge:
// every 1024x1024 piece is embedded at the raster's own resolution
// whatever the plot scale (about 1400 dpi on a 1:6500 cover page).
// FastPlot_compose.ps1 draws it afterwards from the raw tiles at the
// resolution asked for.  A raster is only hidden from 12d once the
// engine has confirmed it can read its tiles.
//---------------------------------------------------------------------

#ifndef FASTPLOT_PLOT_H
#define FASTPLOT_PLOT_H

#define DEG_TO_RAD 0.0174532925199433

//---------------------------------------------------------------------
// is_raster_type
//   12d does not document the type text of a raster element, so any
//   type naming a raster or image counts.  Every type seen is logged
//   in DEBUG builds so this can be confirmed on a real project.
//---------------------------------------------------------------------
Integer is_raster_type(Text type)
{
    Text t = Text_lower(type);
    if(Find_text(t, "raster") > 0) return 1;
    if(Find_text(t, "image") > 0) return 1;
    if(Find_text(t, "bitmap") > 0) return 1;
    return 0;
}

// name is in the comma separated override list typed on the panel
Integer in_name_list(Text name, Text list)
{
    if(Text_justify(list) == "") return 0;
    Text l = "," + Text_lower(list) + ",";
    l = replace_all(l, ", ", ",");
    l = replace_all(l, " ,", ",");
    if(Find_text(l, "," + Text_lower(Text_justify(name)) + ",") > 0) return 1;
    return 0;
}

//---------------------------------------------------------------------
// raster_extent
//   World extent of a raster element.  12d stores the tiles in pixel
//   rows; the engine maps them onto this rectangle.
//---------------------------------------------------------------------
Integer raster_extent(Element elt, Real &x1, Real &y1, Real &x2, Real &y2)
{
    Integer rx = Get_extent_x(elt, x1, x2);
    Integer ry = Get_extent_y(elt, y1, y2);
    if(rx != 0 || ry != 0)
    {
        Calc_extent(elt);
        rx = Get_extent_x(elt, x1, x2);
        ry = Get_extent_y(elt, y1, y2);
    }
    if(rx != 0) return 1;
    if(ry != 0) return 1;
    if(x2 - x1 <= 0.0) return 2;
    if(y2 - y1 <= 0.0) return 2;
    return 0;
}

//---------------------------------------------------------------------
// Tables reset for each run
//---------------------------------------------------------------------
void reset_views()
{
    g_n_views = 0;
    Null(g_vw_source); Null(g_vw_plot); Null(g_vw_rasters);
    Null(g_vw_below);  Null(g_vw_made); Null(g_vw_ok); Null(g_vw_models);
    g_n_models = 0;
    Null(g_md_name); Null(g_md_kind); Null(g_md_ids); Null(g_md_note);
    g_n_rasters = 0;
    Null(g_ras_model); Null(g_ras_element);
    Null(g_ras_xmin);  Null(g_ras_ymin); Null(g_ras_xmax); Null(g_ras_ymax);
    Null(g_ras_ok);    Null(g_ras_held); Null(g_ras_info);
    Null(g_ras_elt);
}

Integer model_slot(Text name)
{
    Integer i;
    for(i = 1; i <= g_n_models; i++)
    {
        if(get_text(g_md_name, i) == name) return i;
    }
    return 0;
}

// raster id "R7" -> 7
Integer raster_index(Text id)
{
    Integer n = 0;
    if(Text_length(id) < 2) return 0;
    if(From_text(Get_subtext(id, 2, Text_length(id)), n) != 0) return 0;
    return n;
}

//---------------------------------------------------------------------
// study_model
//   Classifies a model once per run and records its rasters (those
//   with a usable extent).  Nothing is changed in the project.
//---------------------------------------------------------------------
Integer study_model(Text model_name, Text override_list)
{
    Integer slot = model_slot(model_name);
    if(slot > 0) return slot;

    Integer kind = MODEL_VECTOR;
    Text ids = "", note = "";
    Model m = Get_model(model_name);
    if(Model_exists(m) == 0) note = "model not found";
    Dynamic_Element de;
    Integer ne = 0;
    if(note == "") Get_elements(m, de, ne);

    Integer forced = in_name_list(model_name, override_list);
    Integer n_ras = 0, n_other = 0, n_weighted = 0;
    Text types = "", weights = "";
    Integer i;
    for(i = 1; i <= ne; i++)
    {
        Element e;
        if(Get_item(de, i, e) != 0) continue;
        Text t = "";
        if(Get_type(e, t) != 0) t = "";
        Integer r = is_raster_type(t);
        if(forced) r = 1;
        if(r) n_ras++;
        else n_other++;
#ifdef DEBUG
        if(Find_text("|" + types, "|" + t + "|") == 0) types = types + t + "|";
        // string weights: how the Multi Plot Sheet and plot frame plots differ on them
        Real wt = 0.0;
        if(Get_weight(e, wt) == 0)
        {
            if(wt != 0.0)
            {
                n_weighted++;
                if(n_weighted <= 3)
                {
                    Text wn = "";
                    if(Get_name(e, wn) != 0) wn = "";
                    weights = weights + " '" + wn + "'=" + To_text(wt, 4);
                }
            }
        }
#endif
        // a big model with no raster among its first elements is vector data
        if(n_ras == 0 && n_other >= 2000) break;
    }
#ifdef DEBUG
    log_msg("  model '" + model_name + "' elements " + itxt(ne) + " raster " + itxt(n_ras) + " other " + itxt(n_other) + " types |" + types +
            " weighted " + itxt(n_weighted) + weights);
#endif
    if(n_ras > 0)
    {
        kind = MODEL_RASTER;
        if(n_other > 0) kind = MODEL_MIXED;
        for(i = 1; i <= ne; i++)
        {
            Element e;
            if(Get_item(de, i, e) != 0) continue;
            Text t = "";
            if(Get_type(e, t) != 0) t = "";
            if(is_raster_type(t) == 0 && forced == 0) continue;
            Real x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
            Text en = "";
            if(Get_name(e, en) != 0) en = "";
            Integer xr = raster_extent(e, x1, y1, x2, y2);
            if(xr != 0)
            {
                note = "raster '" + en + "' has no usable extent - 12d draws it";
                continue;
            }
            g_n_rasters++;
            Append(model_name, g_ras_model);
            Append(en,         g_ras_element);
            Append(x1, g_ras_xmin);
            Append(y1, g_ras_ymin);
            Append(x2, g_ras_xmax);
            Append(y2, g_ras_ymax);
            Append(0,  g_ras_ok);
            Append(0,  g_ras_held);
            Append("", g_ras_info);
            Set_item(g_ras_elt, g_n_rasters, e);    // extends the list
            if(ids != "") ids = ids + ",";
            ids = ids + "R" + itxt(g_n_rasters);
#ifdef DEBUG
            log_msg("    raster R" + itxt(g_n_rasters) + " '" + en + "' x " + num(x1) + " .. " + num(x2) + "  y " + num(y1) + " .. " + num(y2));
#endif
        }
    }
    g_n_models++;
    Append(model_name, g_md_name);
    Append(kind,       g_md_kind);
    Append(ids,        g_md_ids);
    Append(note,       g_md_note);
    return g_n_models;
}

//---------------------------------------------------------------------
// study_view
//   Slot of src_view in g_vw_*, adding it on first use: its models in
//   view order ("|" separated) and each model studied.
//---------------------------------------------------------------------
Integer study_view(Text src_view, Text override_list, Text &err)
{
    err = "";
    Integer i;
    for(i = 1; i <= g_n_views; i++)
    {
        if(get_text(g_vw_source, i) == src_view)
        {
            if(get_int(g_vw_ok, i) == 0) err = "view '" + src_view + "' not found";
            return i;
        }
    }
    g_n_views++;
    Integer ok = 1;
    Text order = "";
    View v = Get_view(src_view);
    if(View_exists(v) == 0)
    {
        err = "view '" + src_view + "' not found";
        ok = 0;
    }
    Dynamic_Text models;
    Integer nm = 0;
    if(ok)
    {
        View_get_models(v, models);
        Get_number_of_items(models, nm);
    }
#ifdef DEBUG
    log_msg("study_view '" + src_view + "' models " + itxt(nm) + " (order as View_get_models returns it)");
#endif
    for(i = 1; i <= nm; i++)
    {
        Text mn = get_text(models, i);
        study_model(mn, override_list);
        if(order != "") order = order + "|";
        order = order + mn;
    }
    Append(src_view, g_vw_source);
    Append(src_view, g_vw_plot);
    Append("",       g_vw_rasters);
    Append("",       g_vw_below);
    Append(0,        g_vw_made);
    Append(ok,       g_vw_ok);
    Append(order,    g_vw_models);
    return g_n_views;
}

//---------------------------------------------------------------------
// split_bar - item n of a "|" separated list ("" past the end)
//---------------------------------------------------------------------
Integer count_bar(Text list)
{
    if(list == "") return 0;
    Integer n = 1;
    Integer i;
    Integer len = Text_length(list);
    for(i = 1; i <= len; i++)
    {
        Integer c = 0;
        Get_char(list, i, c);
        if(c == 124) n++;
    }
    return n;
}

Text item_bar(Text list, Integer n)
{
    Text rest = list;
    Integer i;
    for(i = 1; i < n; i++)
    {
        Integer p = Find_text(rest, "|");
        if(p == 0) return "";
        Integer len = Text_length(rest);
        if(p >= len) return "";
        rest = Get_subtext(rest, p + 1, len);
    }
    Integer q = Find_text(rest, "|");
    if(q == 0) return rest;
    if(q == 1) return "";
    return Get_subtext(rest, 1, q - 1);
}

// usable (probed OK) raster ids of model slot ms, and whether all were usable
Text usable_ids(Integer ms, Integer &all_ok)
{
    all_ok = 1;
    Text ids = get_text(g_md_ids, ms);
    Text out = "";
    Text rest = ids + ",";
    while(rest != "")
    {
        Integer p = Find_text(rest, ",");
        Text id = "";
        if(p > 1) id = Get_subtext(rest, 1, p - 1);
        Integer len = Text_length(rest);
        if(p >= len) rest = "";
        else rest = Get_subtext(rest, p + 1, len);
        if(id == "") continue;
        Integer r = raster_index(id);
        if(r < 1) continue;
        if(get_int(g_ras_ok, r) == 0)
        {
            all_ok = 0;
            continue;
        }
        if(out != "") out = out + ",";
        out = out + id;
    }
    if(get_text(g_md_note, ms) != "") all_ok = 0;
    return out;
}

//---------------------------------------------------------------------
// park_raster
//   Moves raster r into the hold model for its model.  Returns 0 when
//   moved; every other outcome is logged with its reason.
//---------------------------------------------------------------------
Integer park_raster(Integer r)
{
    Text mn = get_text(g_ras_model, r);
    Text en = get_text(g_ras_element, r);
    Element e;
    if(Get_item(g_ras_elt, r, e) != 0)
    {
        log_msg("  park R" + itxt(r) + ": no element handle");
        return 1;
    }
    if(Element_exists(e) == 0)
    {
        log_msg("  park R" + itxt(r) + ": element handle is not valid");
        return 2;
    }
    // the home model's name must come back intact on restore, so a hold
    // name 12d would have to alter is not used
    Text hname = HOLD_PREFIX + mn;
    Text vname = "";
    Valid_model_name(hname, vname);
    if(vname != hname)
    {
        log_msg("  park R" + itxt(r) + ": '" + hname + "' is not a valid model name ('" + vname + "')");
        return 3;
    }
    Model hold = Get_model_create(hname);
    if(Model_exists(hold) == 0)
    {
        log_msg("  park R" + itxt(r) + ": cannot create model '" + hname + "'");
        return 3;
    }
    Integer sr = Set_model(e, hold);
    log_msg("  park R" + itxt(r) + " '" + en + "' from '" + mn + "' into '" + hname + "' -> " + itxt(sr));
    if(sr != 0)
    {
        // nothing moved: do not leave an empty hold model behind
        Integer nh = 0;
        Get_number_of_items(hold, nh);
        if(nh == 0) Model_delete(hold);
        return 4;
    }
    return 0;
}

//---------------------------------------------------------------------
// restore_rasters
//   Every "FastPlot hold - <model>" model in the project has its
//   elements moved back to <model> and is then deleted.  Returns the
//   number of elements moved home; problems are printed.
//---------------------------------------------------------------------
Integer restore_rasters()
{
    Dynamic_Text all;
    Get_project_models(all);
    Integer n = 0;
    Get_number_of_items(all, n);
    Integer plen = Text_length(HOLD_PREFIX);
    Integer moved = 0;
    Integer i;
    for(i = 1; i <= n; i++)
    {
        Text hn = get_text(all, i);
        if(Text_length(hn) <= plen) continue;
        if(Get_subtext(hn, 1, plen) != HOLD_PREFIX) continue;
        Text home = Get_subtext(hn, plen + 1, Text_length(hn));
        Model hold = Get_model(hn);
        Dynamic_Element de;
        Integer ne = 0;
        Get_elements(hold, de, ne);
        Integer ok = 1;
        if(ne > 0)
        {
            Model dest = Get_model_create(home);
            if(Set_model(de, dest) != 0) ok = 0;
        }
        log_msg("restore " + itxt(ne) + " element(s) from '" + hn + "' to '" + home + "' ok " + itxt(ok));
        if(ok)
        {
            moved = moved + ne;
            Model_delete(hold);
        }
        else Print("WARNING: could not move the aerial back from model '" + hn + "' to '" + home + "' - move it by hand.\n");
    }
    return moved;
}

//---------------------------------------------------------------------
// hide_rasters
//   After the probe: decides, per view, the view 12d plots and the
//   rasters the engine draws.  With create 0 nothing is made or moved
//   (Check reports what Plot would do).
//---------------------------------------------------------------------
void hide_rasters(Integer create)
{
    Integer r;
    if(g_hide_mode == HIDE_MOVE && create)
    {
        for(r = 1; r <= g_n_rasters; r++)
        {
            if(get_int(g_ras_ok, r) == 0) continue;
            Integer pr = park_raster(r);
            if(pr == 0) Set_item(g_ras_held, r, 1);
            else say("  Could not move aerial '" + get_text(g_ras_element, r) + "' out of '" + get_text(g_ras_model, r) +
                     "' (" + itxt(pr) + ") - leaving its model out of a temporary copy of the view instead.");
        }
    }

    Integer v;
    for(v = 1; v <= g_n_views; v++)
    {
        if(get_int(g_vw_ok, v) == 0) continue;
        Text src = get_text(g_vw_source, v);
        Text order = get_text(g_vw_models, v);
        Integer nm = count_bar(order);
        Text ids = "", below = "", keep = "";
        Integer first_hidden = 0, excluded = 0;
        Integer i;
        for(i = 1; i <= nm; i++)
        {
            Text mn = item_bar(order, i);
            Integer ms = model_slot(mn);
            Integer all_ok = 0;
            Text mids = "";
            if(ms > 0) mids = usable_ids(ms, all_ok);
            // && does not short-circuit in 12dPL: read the kind only for a real slot
            Integer mkind = MODEL_VECTOR;
            if(ms > 0) mkind = get_int(g_md_kind, ms);

            // rasters parked out of the model (for Check: those that would be)
            Text held = "";
            if(g_hide_mode == HIDE_MOVE)
            {
                Text rest = mids + ",";
                while(rest != "")
                {
                    Integer p = Find_text(rest, ",");
                    Text id = "";
                    if(p > 1) id = Get_subtext(rest, 1, p - 1);
                    Integer len = Text_length(rest);
                    if(p >= len) rest = "";
                    else rest = Get_subtext(rest, p + 1, len);
                    if(id == "") continue;
                    Integer ri = raster_index(id);
                    if(ri < 1) continue;
                    Integer is_held = get_int(g_ras_held, ri);
                    if(create == 0) is_held = 1;
                    if(is_held == 0) continue;
                    if(held != "") held = held + ",";
                    held = held + id;
                }
            }

            Integer hidden = 0;
            if(held != "")
            {
                // parked: the model stays in the view, empty for the plot
                hidden = 1;
                mids = held;
                if(keep != "") keep = keep + "|";
                keep = keep + mn;
            }
            else
            {
                if(mkind == MODEL_RASTER && all_ok && mids != "")
                {
                    // left out of a temporary copy of the view
                    hidden = 1;
                    excluded++;
                }
                else
                {
                    mids = "";
                    if(keep != "") keep = keep + "|";
                    keep = keep + mn;
                }
            }
            if(hidden)
            {
                if(ids != "") ids = ids + ",";
                ids = ids + mids;
                if(first_hidden == 0) first_hidden = i;
            }
            else
            {
                if(first_hidden == 0)
                {
                    if(below != "") below = below + "|";
                    below = below + mn;
                }
            }
        }
        if(first_hidden == 0) below = "";

        Text plot_view = src;
        Integer made = 0;
        if(excluded > 0 && create)
        {
            Text tname = TEMP_VIEW_PREFIX + itxt(v);
            if(View_exists(tname) != 0)
            {
                View old = Get_view(tname);
                View_delete(old);
            }
            Integer vr = View_create(VIEW_TYPE_PLAN, tname, 40 + 20 * v, 40 + 20 * v, 480, 320, VIEW_ENGINE_GDI);
            View tv = Get_view(tname);
#ifdef DEBUG
            log_msg("  View_create '" + tname + "' returned " + itxt(vr) + " exists " + itxt(View_exists(tv)));
#endif
            if(View_exists(tv) == 0)
            {
                say("  Could not create a temporary view for '" + src + "' - 12d draws its aerial.");
                ids = "";
                below = "";
            }
            else
            {
                Integer nk = count_bar(keep);
                Integer k;
                for(k = 1; k <= nk; k++)
                {
                    Text kn = item_bar(keep, k);
                    Model km = Get_model(kn);
                    Integer ar = View_add_model(tv, km);
#ifdef DEBUG
                    log_msg("    add '" + kn + "' -> " + itxt(ar));
#endif
                }
                plot_view = tname;
                made = 1;
            }
        }
        Set_item(g_vw_plot,    v, plot_view);
        Set_item(g_vw_rasters, v, ids);
        Set_item(g_vw_below,   v, below);
        Set_item(g_vw_made,    v, made);
        log_msg("view '" + src + "' plot view '" + plot_view + "' engine rasters '" + ids + "' below '" + below +
                "' parked/excluded models " + itxt(excluded));
    }
}

//---------------------------------------------------------------------
// Sheet outlines
//   The Multi Plot Sheet plotter draws the outline of every plan frame
//   (its viewport) into that frame's frame model, in the frame's colour,
//   linestyle and weight - the dashed sheet key on the cover pages.  The
//   models are empty otherwise, so a plot frame plot would leave them
//   out.  draw_outlines adds them for the run and remove_outlines takes
//   them away again.  The models touched are listed in a file under
//   TEMP until then, so an interrupted run is cleaned up next time.
//---------------------------------------------------------------------
Text outline_list_path()
{
    return temp_dir() + "\\FastPlot\\outlines.txt";
}

Integer remove_outlines()
{
    Text path = outline_list_path();
    if(File_exists(path) == 0) return 0;
    File f;
    if(File_open(path, "r", "ccs=UTF-8", f) != 0) return 0;
    Integer plen = Text_length(OUTLINE_PREFIX);
    Integer removed = 0;
    Text line = "";
    while(File_read_line(f, line) == 0)
    {
        Text mn = Text_justify(line);
        if(mn == "") continue;
        Model m = Get_model(mn);
        if(Model_exists(m) == 0) continue;
        Dynamic_Element de;
        Integer ne = 0;
        Get_elements(m, de, ne);
        Integer i;
        for(i = 1; i <= ne; i++)
        {
            Element e;
            if(Get_item(de, i, e) != 0) continue;
            Text n = "";
            if(Get_name(e, n) != 0) n = "";
            if(Text_length(n) < plen) continue;
            if(Get_subtext(n, 1, plen) != OUTLINE_PREFIX) continue;
            if(Element_delete(e) == 0) removed++;
        }
    }
    File_close(f);
    File_delete(path);
    log_msg("remove_outlines: " + itxt(removed) + " removed");
    return removed;
}

Integer draw_outlines()
{
    // the frame models to touch, written down before anything is drawn
    Dynamic_Text models;
    Integer nmod = 0;
    Integer s;
    for(s = 1; s <= g_n_sheets; s++)
    {
        Text fm = get_text(g_sh_fmodel, s);
        if(fm == "") continue;
        Integer seen = 0;
        Integer k;
        for(k = 1; k <= nmod; k++)
        {
            if(get_text(models, k) == fm) seen = 1;
        }
        if(seen) continue;
        Append(fm, models);
        nmod++;
    }
    if(nmod == 0) return 0;
    File lf;
    if(File_open(outline_list_path(), "w", "ccs=UTF-8", lf) != 0)
    {
        log_msg("draw_outlines: cannot write " + outline_list_path() + " - outlines not drawn");
        return 0;
    }
    Integer km;
    for(km = 1; km <= nmod; km++) File_write_line(lf, get_text(models, km));
    File_close(lf);

    Integer drawn = 0;
    for(s = 1; s <= g_n_sheets; s++)
    {
        Text fm = get_text(g_sh_fmodel, s);
        if(fm == "") continue;
        if(get_text(g_sh_problem, s) != "") continue;
        if(get_int(g_sh_active, s) == 0) continue;
        Model m = Get_model(fm);
        if(Model_exists(m) == 0) continue;
        Real w = 0.0, h = 0.0;
        if(paper_size(get_text(g_sh_paper, s), w, h) != 0) continue;

        Real k2 = get_real(g_sh_scale, s) / 1000.0;
        Real hw = (w - get_real(g_sh_ml, s) - get_real(g_sh_mr, s)) / 2.0 * k2;
        Real hh = (h - get_real(g_sh_mb, s) - get_real(g_sh_mt, s)) / 2.0 * k2;
        Real cx = get_real(g_sh_ox, s), cy = get_real(g_sh_oy, s);
        Real a = get_real(g_sh_rot, s) * DEG_TO_RAD;
        Real ca = Cos(a), sa = Sin(a);

        Element e = Create_super(12, 4);
        if(Element_exists(e) == 0) continue;
        Real dx[5], dy[5];
        dx[1] = -hw; dy[1] = -hh;
        dx[2] =  hw; dy[2] = -hh;
        dx[3] =  hw; dy[3] =  hh;
        dx[4] = -hw; dy[4] =  hh;
        dx[5] = 0.0; dy[5] = 0.0;
        Integer v;
        for(v = 1; v <= 4; v++)
            Set_super_vertex_coord(e, v, cx + dx[v] * ca - dy[v] * sa, cy + dx[v] * sa + dy[v] * ca, 0.0);
        String_close(e);
        Set_name(e, OUTLINE_PREFIX + get_text(g_sh_name, s));
        Integer cn = 0;
        Text col = get_text(g_sh_fcolour, s);
        if(col != "")
        {
            if(Convert_colour(col, cn) == 0) Set_colour(e, cn);
        }
        Text sty = get_text(g_sh_fstyle, s);
        if(sty != "") Set_style(e, sty);
        Set_weight(e, get_real(g_sh_fweight, s));
        Calc_extent(e);
        if(Set_model(e, m) != 0)
        {
            Element_delete(e);
            continue;
        }
        drawn++;
    }
    log_msg("draw_outlines: " + itxt(drawn) + " outline(s) in " + itxt(nmod) + " model(s)");
    return drawn;
}

//---------------------------------------------------------------------
// remove_temp_views
//---------------------------------------------------------------------
void remove_temp_views()
{
    Integer i;
    for(i = 1; i <= g_n_views; i++)
    {
        if(get_int(g_vw_made, i) == 0) continue;
        View tv = Get_view(get_text(g_vw_plot, i));
        if(View_exists(tv) != 0) View_delete(tv);
    }
}

//---------------------------------------------------------------------
// set_ppf
//   One plot parameter, logged.  A name the plot type does not know
//   returns non-zero and is otherwise harmless.
//---------------------------------------------------------------------
Integer set_ppf(Plot_Parameter_File ppf, Text name, Text value)
{
    Integer r = Set_parameter(ppf, name, value);
#ifdef DEBUG
    if(r != 0) log_msg("    Set_parameter " + name + " = '" + value + "' returned " + itxt(r));
#endif
    return r;
}

//---------------------------------------------------------------------
// centre_frame
//   Moves the plot frame so the centre of its viewport sits on (ox, oy)
//   - the plot sheet's meaning of the origin.  12d does not document
//   where a plot frame element's own origin lies, so it is measured:
//   the frame's extent is either the whole sheet or the viewport at
//   scale, and whichever it is gives the viewport centre.
//---------------------------------------------------------------------
Integer centre_frame(Element frame, Real ox, Real oy, Real k, Real w, Real h,
                     Real ml, Real mr, Real mb, Real mt, Real rot_rad, Text &note)
{
    note = "";
    Real x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    Calc_extent(frame);
    Integer rx = Get_extent_x(frame, x1, x2);
    Integer ry = Get_extent_y(frame, y1, y2);
    if(rx != 0 || ry != 0)
    {
        note = "frame extent unavailable - origin used as given";
        return 1;
    }
    Real ew = x2 - x1, eh = y2 - y1;
    Real ecx = (x1 + x2) / 2.0, ecy = (y1 + y2) / 2.0;
    Real c = Absolute(Cos(rot_rad)), s = Absolute(Sin(rot_rad));

    // expected bounding boxes of the sheet and of the viewport
    Real sw = (w * c + h * s) * k, sh = (w * s + h * c) * k;
    Real vw0 = (w - ml - mr), vh0 = (h - mb - mt);
    Real vw = (vw0 * c + vh0 * s) * k, vh = (vw0 * s + vh0 * c) * k;

    Real vcx = 0.0, vcy = 0.0;
    Integer known = 0;
    if(Absolute(ew - sw) <= 0.01 * sw && Absolute(eh - sh) <= 0.01 * sh)
    {
        // extent = sheet; the viewport centre is offset by the unequal margins
        Real dx = (ml - mr) / 2.0 * k, dy = (mb - mt) / 2.0 * k;
        vcx = ecx + dx * Cos(rot_rad) - dy * Sin(rot_rad);
        vcy = ecy + dx * Sin(rot_rad) + dy * Cos(rot_rad);
        known = 1;
        note = "extent is the sheet";
    }
    else
    {
        if(Absolute(ew - vw) <= 0.01 * vw && Absolute(eh - vh) <= 0.01 * vh)
        {
            vcx = ecx;
            vcy = ecy;
            known = 1;
            note = "extent is the viewport";
        }
    }
    if(known == 0)
    {
        note = "frame extent " + num(ew) + " x " + num(eh) + " matches neither sheet nor viewport - origin used as given";
        return 2;
    }
    Real cx = 0.0, cy = 0.0;
    Get_plot_frame_origin(frame, cx, cy);
    Set_plot_frame_origin(frame, cx + (ox - vcx), cy + (oy - vcy));
    Calc_extent(frame);
    note = note + ", origin moved " + num(ox - vcx) + ", " + num(oy - vcy);
    return 0;
}

//---------------------------------------------------------------------
// View plot scale
//   12d draws a string weight at the view's plot scale: in the Multi
//   Plot Sheet workflow the view's plot scale is set to the plot's scale
//   by hand so the weights come out right.  Every plot here does the
//   same for its own scale, and the views get their own scales back at
//   the end of the run.
//---------------------------------------------------------------------
void set_view_scale(Text view_name, Real scale)
{
    View v = Get_view(view_name);
    if(View_exists(v) == 0) return;
    Real cur = 0.0;
    if(Plan_view_get_plot_scale(v, cur) != 0) return;
    Integer i, seen = 0;
    for(i = 1; i <= g_n_vscale; i++)
    {
        if(get_text(g_vs_view, i) == view_name) seen = 1;
    }
    if(seen == 0)
    {
        g_n_vscale++;
        Append(view_name, g_vs_view);
        Append(cur, g_vs_scale);
    }
    if(Absolute(cur - scale) < 0.000001) return;
    Integer r = Plan_view_set_plot_scale(v, scale);
    log_msg("  view '" + view_name + "' plot scale 1:" + num(cur) + " -> 1:" + num(scale) + " (" + itxt(r) + ")");
}

void restore_view_scales()
{
    Integer i;
    for(i = 1; i <= g_n_vscale; i++)
    {
        View v = Get_view(get_text(g_vs_view, i));
        if(View_exists(v) == 0) continue;
        Plan_view_set_plot_scale(v, get_real(g_vs_scale, i));
    }
    g_n_vscale = 0;
    Null(g_vs_view);
    Null(g_vs_scale);
}

// The plotter for the model data: with string weights off, the same
// plotter without "string weight" (pen widths from its map file only)
Text vector_plotter(Text plotter)
{
    Text p = plotter;
    if(p == "") p = g_book_plotter;
    if(p == "") p = "PDF 100% 12d colour string weight";
    if(g_weights) return p;
    return replace_all(p, " string weight", "");
}

//---------------------------------------------------------------------
// plot_frame
//   One 12d plot of a plot frame through a plot_frame_plot parameter
//   file, into stem (12d appends a number; the engine takes whatever PDF
//   lands in that folder).  The sheet is the paper name when it is
//   given, else pw x ph mm.  ts is the sheet whose title block and user
//   text go on it (0 for none) and number the drawing number written.
//   Returns 0 when 12d reports success, with the paper size in w / h.
//---------------------------------------------------------------------
Integer plot_frame(Integer seq, Model frames, Text paper, Real pw, Real ph,
                   Real ml, Real mb, Real mr, Real mt, Real scale, Real rot, Real ox, Real oy,
                   Integer ts, Text number, Text plotter, Text plot_view, Text stem,
                   Real &w, Real &h, Text &err)
{
    err = "";
    w = 0.0;
    h = 0.0;
#ifdef DEBUG
    Real t_start = clock_s();
#endif
    Text tbf = "", colour = "";
    Real tsize = 5.0;
    if(ts > 0)
    {
        tbf = get_text(g_sh_tbf, ts);
        colour = get_text(g_sh_tb_colour, ts);
        tsize = get_real(g_sh_tb_size, ts);
    }
    if(plotter == "") plotter = g_book_plotter;
    if(plotter == "") plotter = "PDF 100% 12d colour string weight";

    // ---- the plot frame
    Element frame = Create_plot_frame(PROGRAM_NAME + " " + itxt(seq));
    if(Element_exists(frame) == 0)
    {
        err = "Create_plot_frame failed";
        return 1;
    }
    Integer mr_ret = Set_model(frame, frames);
    if(mr_ret != 0)
    {
        err = "the plot frame could not be added to model '" + FRAMES_MODEL + "' (" + itxt(mr_ret) + ")";
        Element_delete(frame);
        return 5;
    }

    // Sheet size: 12d's name first; names it does not know ("A3
    // Landscape" from a user sheets.4d) and area plots are set by width
    // and height.
    Integer r = 1;
    Real tw = pw, th = ph;
    Integer known = 0;
    if(paper != "")
    {
        known = paper_size(paper, tw, th);
        r = Set_plot_frame_sheet_size(frame, paper);
        Get_plot_frame_sheet_size(frame, w, h);
    }
    if(r != 0 || w <= 0.0 || h <= 0.0)
    {
        if(known != 0 || tw <= 0.0 || th <= 0.0)
        {
            err = "unknown sheet size '" + paper + "'";
            Element_delete(frame);
            return 2;
        }
        r = Set_plot_frame_sheet_size(frame, tw, th);
        w = 0.0;
        h = 0.0;
        Get_plot_frame_sheet_size(frame, w, h);
        if(r != 0 || w <= 0.0 || h <= 0.0)
        {
            log_msg("  sheet size " + num(tw) + " x " + num(th) + " set returned " + itxt(r) + ", reads back " + num(w) + " x " + num(h));
            w = tw;
            h = th;
        }
    }
    Set_plot_frame_margins(frame, ml, mb, mr, mt);
    Set_plot_frame_scale(frame, scale);
    Set_plot_frame_rotation(frame, rot);
    Set_plot_frame_draw_border(frame, 0);
    Set_plot_frame_draw_viewport(frame, 0);
    if(tbf != "")
    {
        Set_plot_frame_draw_title_file(frame, 1);
        Set_plot_frame_title_file(frame, tbf);
        Set_plot_frame_text_size(frame, tsize);
    }
    else Set_plot_frame_draw_title_file(frame, 0);
    Set_plot_frame_plotter_name(frame, plotter);
    Set_plot_frame_plot_file(frame, stem + ".pdf");
    Set_plot_frame_origin(frame, ox, oy);
    Text cnote = "";
    Integer cr = centre_frame(frame, ox, oy, scale / 1000.0, w, h, ml, mr, mb, mt, rot, cnote);
    if(cr != 0) say("  WARNING plot " + itxt(seq) + ": " + cnote);
#ifdef DEBUG
    log_msg("  plot " + itxt(seq) + " frame " + num(w) + " x " + num(h) + " mm at 1:" + num(scale) + " - " + cnote);
#endif

    // ---- the plot parameters, as the Plot Frames panel would hold them
    Plot_Parameter_File ppf;
    r = Create_plot_frame_plot_parameter_file(ppf);
    if(r != 0)
    {
        err = "Create_plot_frame_plot_parameter_file returned " + itxt(r);
        return 3;
    }
    set_ppf(ppf, "plot_border", "0");
    if(tbf != "")
    {
        set_ppf(ppf, "use_title_file", "1");
        set_ppf(ppf, "title_file", tbf);
        set_ppf(ppf, "title_text_size", num(tsize));
        if(colour != "") set_ppf(ppf, "title_colour", colour);
        Integer first = get_int(g_sh_uv_first, ts);
        Integer count = get_int(g_sh_uv_count, ts);
        Integer u;
        for(u = first; u < first + count; u++)
        {
            Text idx = itxt(get_int(g_uv_index, u));
            set_ppf(ppf, "user_alias_" + idx, get_text(g_uv_alias, u));
            set_ppf(ppf, "user_text_" + idx, get_text(g_uv_value, u));
        }
    }
    else set_ppf(ppf, "use_title_file", "0");
    set_ppf(ppf, "start_page_number", number);
    set_ppf(ppf, "start_drawing_number", number);
    set_ppf(ppf, "drawing_register_enable", "1");
    r = Set_parameter(ppf, "single_frame", frame);
#ifdef DEBUG
    log_msg("    single_frame -> " + itxt(r));
#endif
    set_ppf(ppf, "view_mode", "0");
    set_ppf(ppf, "view_name", plot_view);
    set_ppf(ppf, "plotter_type", plotter);
    set_ppf(ppf, "plot_stem", stem);
    set_ppf(ppf, "use_drawing_numbers_in_filenames", "0");
    set_ppf(ppf, "use_frame_names_for_filenames", "0");
    // The parameters on disk beside the plot.  Not optional, whatever the
    // manual says: a build that skipped this had every Plot_parameter_file
    // return 1, with a parse error from 12d.  With DEBUG the work folder
    // is kept, so the file can be opened in the Plot Frames panel.
    Integer wr = Write_parameter_file(ppf, stem);
    if(wr != 0) log_msg("    Write_parameter_file returned " + itxt(wr));

#ifdef DEBUG
    Real t_scale = clock_s();
#endif
    set_view_scale(plot_view, scale);
#ifdef DEBUG
    Real t_plot = clock_s();
#endif
    r = Plot_parameter_file(ppf);
#ifdef DEBUG
    Real t_end = clock_s();
    log_msg("    Plot_parameter_file -> " + itxt(r) + "  view '" + plot_view + "' plotter '" + plotter + "' stem " + stem);
    log_msg("    times: frame and parameters " + To_text(t_scale - t_start, 2) + " s, view scale " + To_text(t_plot - t_scale, 2) +
            " s, 12d plot " + To_text(t_end - t_plot, 2) + " s");
#endif
    if(r != 0)
    {
        err = "12d plot returned " + itxt(r);
        return 4;
    }
    return 0;
}

//---------------------------------------------------------------------
// plot_sheet - sheet s on its own, title block and all
//---------------------------------------------------------------------
Integer plot_sheet(Integer s, Integer seq, Model frames, Text plot_view, Text stem,
                   Real &w, Real &h, Text &err)
{
    return plot_frame(seq, frames, get_text(g_sh_paper, s), 0.0, 0.0,
                      get_real(g_sh_ml, s), get_real(g_sh_mb, s), get_real(g_sh_mr, s), get_real(g_sh_mt, s),
                      get_real(g_sh_scale, s), get_real(g_sh_rot, s) * DEG_TO_RAD,
                      get_real(g_sh_ox, s), get_real(g_sh_oy, s),
                      s, itxt(seq), vector_plotter(get_text(g_sh_plotter, s)), plot_view, stem, w, h, err);
}

#endif
