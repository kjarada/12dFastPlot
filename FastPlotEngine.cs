//---------------------------------------------------------------------
// FastPlotEngine.cs
// PDF + raster engine for the 12d FastPlot macro.  Compiled at run
// time by FastPlot_compose.ps1 (Windows PowerShell 5.1 / .NET 4.x),
// so the language level is C# 5: no string interpolation, no ?.
//
//   compose : take the vector-only sheets 12d plotted, draw the aerial
//             underneath from the raw .image tiles at a chosen DPI,
//             and merge everything into one book or one file per sheet
//   shrink  : downsample / recompress the images inside existing 12d
//             PDFs and merge them
//
// The PDF reader handles what 12d's Debenu-based pdf_12d engine
// writes (classic xref, Flate streams) plus object streams, which its
// multi-page merge uses.  Objects are found by a sequential scan, not
// by trusting xref offsets.
//---------------------------------------------------------------------
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace FastPlot
{
    // =================================================================
    // PDF object model
    // =================================================================
    public abstract class PdfObj { }

    public sealed class PdfNull : PdfObj
    {
        public static readonly PdfNull Instance = new PdfNull();
    }

    public sealed class PdfBool : PdfObj
    {
        public bool Value;
        public PdfBool(bool v) { Value = v; }
    }

    public sealed class PdfNum : PdfObj
    {
        public string Text;
        public PdfNum(string t) { Text = t; }
        public PdfNum(double v) { Text = Fmt.N(v); }
        public double Value { get { return double.Parse(Text, CultureInfo.InvariantCulture); } }
    }

    public sealed class PdfStr : PdfObj
    {
        public byte[] Raw;   // including the ( ) or < > delimiters
        public PdfStr(byte[] raw) { Raw = raw; }

        public static PdfStr FromText(string s)
        {
            bool ascii = true;
            foreach (char ch in s) if (ch > 126 || ch < 32) { ascii = false; break; }
            if (ascii)
            {
                StringBuilder sb = new StringBuilder("(");
                foreach (char ch in s)
                {
                    if (ch == '(' || ch == ')' || ch == '\\') sb.Append('\\');
                    sb.Append(ch);
                }
                sb.Append(')');
                return new PdfStr(Encoding.ASCII.GetBytes(sb.ToString()));
            }
            StringBuilder hex = new StringBuilder("<FEFF");
            foreach (char ch in s) hex.Append(((int)ch).ToString("X4"));
            hex.Append('>');
            return new PdfStr(Encoding.ASCII.GetBytes(hex.ToString()));
        }

        // Decoded text value (PDFDocEncoding treated as Latin-1, or UTF-16BE with BOM)
        public string Decode()
        {
            byte[] b = DecodeBytes();
            if (b.Length >= 2 && b[0] == 0xFE && b[1] == 0xFF)
                return Encoding.BigEndianUnicode.GetString(b, 2, b.Length - 2);
            return Encoding.GetEncoding(28591).GetString(b);
        }

        public byte[] DecodeBytes()
        {
            List<byte> o = new List<byte>();
            if (Raw.Length > 0 && Raw[0] == '<')
            {
                int hi = -1;
                for (int i = 1; i < Raw.Length - 1; i++)
                {
                    int v = HexVal(Raw[i]);
                    if (v < 0) continue;
                    if (hi < 0) hi = v; else { o.Add((byte)(hi * 16 + v)); hi = -1; }
                }
                if (hi >= 0) o.Add((byte)(hi * 16));
                return o.ToArray();
            }
            for (int i = 1; i < Raw.Length - 1; i++)
            {
                byte c = Raw[i];
                if (c != '\\') { o.Add(c); continue; }
                i++;
                if (i >= Raw.Length - 1) break;
                c = Raw[i];
                switch ((char)c)
                {
                    case 'n': o.Add(10); break;
                    case 'r': o.Add(13); break;
                    case 't': o.Add(9); break;
                    case 'b': o.Add(8); break;
                    case 'f': o.Add(12); break;
                    case '\r': if (i + 1 < Raw.Length - 1 && Raw[i + 1] == '\n') i++; break;
                    case '\n': break;
                    default:
                        if (c >= '0' && c <= '7')
                        {
                            int v = c - '0'; int n = 1;
                            while (n < 3 && i + 1 < Raw.Length - 1 && Raw[i + 1] >= '0' && Raw[i + 1] <= '7') { i++; v = v * 8 + (Raw[i] - '0'); n++; }
                            o.Add((byte)v);
                        }
                        else o.Add(c);
                        break;
                }
            }
            return o.ToArray();
        }

        static int HexVal(byte c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
    }

    public sealed class PdfName : PdfObj
    {
        public string Raw;   // without the leading '/', #xx escapes kept
        public PdfName(string raw) { Raw = raw; }
    }

    public sealed class PdfArr : PdfObj
    {
        public List<PdfObj> Items = new List<PdfObj>();
    }

    public sealed class PdfDict : PdfObj
    {
        public List<string> Keys = new List<string>();
        public Dictionary<string, PdfObj> Map = new Dictionary<string, PdfObj>();

        public PdfObj Get(string k)
        {
            PdfObj v;
            return Map.TryGetValue(k, out v) ? v : null;
        }

        public void Set(string k, PdfObj v)
        {
            if (!Map.ContainsKey(k)) Keys.Add(k);
            Map[k] = v;
        }

        public void Remove(string k)
        {
            if (Map.Remove(k)) Keys.Remove(k);
        }

        public string NameOf(string k)
        {
            PdfName n = Get(k) as PdfName;
            return n == null ? null : n.Raw;
        }
    }

    public sealed class PdfRef : PdfObj
    {
        public int Num, Gen;
        public PdfRef(int n, int g) { Num = n; Gen = g; }
    }

    public sealed class PdfStream : PdfObj
    {
        public PdfDict Dict;
        public byte[] Data;   // still encoded
        public PdfStream(PdfDict d, byte[] data) { Dict = d; Data = data; }
    }

    // Keyword token used only inside the parser (obj, endobj, R, stream ...)
    public sealed class PdfKeyword : PdfObj
    {
        public string Word;
        public PdfKeyword(string w) { Word = w; }
    }

    // =================================================================
    // Formatting helpers
    // =================================================================
    public static class Fmt
    {
        public static string N(double v)
        {
            if (Math.Abs(v) < 1e-9) return "0";
            string s = v.ToString("0.#####", CultureInfo.InvariantCulture);
            return s == "-0" ? "0" : s;
        }
    }

    // =================================================================
    // Tokeniser / parser
    // =================================================================
    public sealed class PdfParser
    {
        readonly byte[] b;
        public int Pos;

        public PdfParser(byte[] data, int pos) { b = data; Pos = pos; }

        public static bool IsWhite(byte c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
        public static bool IsDelim(byte c)
        {
            return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
        }

        public void SkipWhite()
        {
            while (Pos < b.Length)
            {
                byte c = b[Pos];
                if (IsWhite(c)) { Pos++; continue; }
                if (c == '%')
                {
                    while (Pos < b.Length && b[Pos] != 10 && b[Pos] != 13) Pos++;
                    continue;
                }
                break;
            }
        }

        public bool AtEnd { get { SkipWhite(); return Pos >= b.Length; } }

        // Reads one object.  Integers followed by "G R" become references.
        public PdfObj ReadObject()
        {
            PdfObj o = ReadSimple();
            PdfNum n1 = o as PdfNum;
            if (n1 != null && IsInt(n1.Text))
            {
                int save = Pos;
                PdfObj o2 = ReadSimple();
                PdfNum n2 = o2 as PdfNum;
                if (n2 != null && IsInt(n2.Text))
                {
                    PdfObj o3 = ReadSimple();
                    PdfKeyword k = o3 as PdfKeyword;
                    if (k != null && k.Word == "R")
                        return new PdfRef(int.Parse(n1.Text, CultureInfo.InvariantCulture), int.Parse(n2.Text, CultureInfo.InvariantCulture));
                }
                Pos = save;
            }
            return o;
        }

        static bool IsInt(string t)
        {
            if (t.Length == 0) return false;
            for (int i = 0; i < t.Length; i++)
            {
                char c = t[i];
                if (c >= '0' && c <= '9') continue;
                if (i == 0 && (c == '+' || c == '-')) continue;
                return false;
            }
            return true;
        }

        PdfObj ReadSimple()
        {
            SkipWhite();
            if (Pos >= b.Length) return new PdfKeyword("");
            byte c = b[Pos];
            if (c == '/')
            {
                int s = ++Pos;
                while (Pos < b.Length && !IsWhite(b[Pos]) && !IsDelim(b[Pos])) Pos++;
                return new PdfName(Encoding.GetEncoding(28591).GetString(b, s, Pos - s));
            }
            if (c == '(') return ReadLiteralString();
            if (c == '<')
            {
                if (Pos + 1 < b.Length && b[Pos + 1] == '<') { Pos += 2; return ReadDict(); }
                int s = Pos;
                while (Pos < b.Length && b[Pos] != '>') Pos++;
                Pos++;
                byte[] raw = new byte[Pos - s];
                Array.Copy(b, s, raw, 0, raw.Length);
                return new PdfStr(raw);
            }
            if (c == '[')
            {
                Pos++;
                PdfArr a = new PdfArr();
                while (true)
                {
                    SkipWhite();
                    if (Pos >= b.Length) break;
                    if (b[Pos] == ']') { Pos++; break; }
                    a.Items.Add(ReadObject());
                }
                return a;
            }
            if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}')
            {
                Pos++;
                return new PdfKeyword(((char)c).ToString());
            }
            int st = Pos;
            while (Pos < b.Length && !IsWhite(b[Pos]) && !IsDelim(b[Pos])) Pos++;
            if (Pos == st) { Pos++; return new PdfKeyword(((char)c).ToString()); }
            string tok = Encoding.GetEncoding(28591).GetString(b, st, Pos - st);
            char f = tok[0];
            if ((f >= '0' && f <= '9') || f == '-' || f == '+' || f == '.') return new PdfNum(tok);
            if (tok == "true") return new PdfBool(true);
            if (tok == "false") return new PdfBool(false);
            if (tok == "null") return PdfNull.Instance;
            return new PdfKeyword(tok);
        }

        PdfObj ReadLiteralString()
        {
            int s = Pos;
            int depth = 0;
            while (Pos < b.Length)
            {
                byte c = b[Pos];
                if (c == '\\') { Pos += 2; continue; }
                if (c == '(') depth++;
                else if (c == ')') { depth--; if (depth == 0) { Pos++; break; } }
                Pos++;
            }
            byte[] raw = new byte[Pos - s];
            Array.Copy(b, s, raw, 0, raw.Length);
            return new PdfStr(raw);
        }

        PdfDict ReadDict()
        {
            PdfDict d = new PdfDict();
            while (true)
            {
                SkipWhite();
                if (Pos >= b.Length) break;
                if (b[Pos] == '>' && Pos + 1 < b.Length && b[Pos + 1] == '>') { Pos += 2; break; }
                PdfObj k = ReadSimple();
                PdfName kn = k as PdfName;
                if (kn == null) continue;
                PdfObj v = ReadObject();
                d.Set(kn.Raw, v);
            }
            return d;
        }

        public string PeekKeyword()
        {
            int save = Pos;
            PdfObj o = ReadSimple();
            Pos = save;
            PdfKeyword k = o as PdfKeyword;
            return k == null ? null : k.Word;
        }

        public PdfKeyword ReadKeyword()
        {
            return ReadSimple() as PdfKeyword;
        }
    }

    // =================================================================
    // Filters
    // =================================================================
    public static class Flate
    {
        public static byte[] Decode(byte[] data)
        {
            int off = 0;
            if (data.Length >= 2 && (data[0] & 0x0F) == 8 && ((data[0] << 8) | data[1]) % 31 == 0) off = 2;
            using (MemoryStream src = new MemoryStream(data, off, data.Length - off))
            using (DeflateStream z = new DeflateStream(src, CompressionMode.Decompress))
            using (MemoryStream dst = new MemoryStream())
            {
                byte[] buf = new byte[65536];
                try
                {
                    int n;
                    while ((n = z.Read(buf, 0, buf.Length)) > 0) dst.Write(buf, 0, n);
                }
                catch (InvalidDataException)
                {
                    // trailing adler32 / garbage after the deflate data - keep what decoded
                }
                return dst.ToArray();
            }
        }

        public static byte[] Encode(byte[] data)
        {
            using (MemoryStream dst = new MemoryStream())
            {
                dst.WriteByte(0x78); dst.WriteByte(0xDA);
                using (DeflateStream z = new DeflateStream(dst, CompressionLevel.Optimal, true))
                    z.Write(data, 0, data.Length);
                uint a = 1, bsum = 0;
                foreach (byte x in data) { a = (a + x) % 65521; bsum = (bsum + a) % 65521; }
                uint ad = (bsum << 16) | a;
                dst.WriteByte((byte)(ad >> 24)); dst.WriteByte((byte)(ad >> 16));
                dst.WriteByte((byte)(ad >> 8)); dst.WriteByte((byte)ad);
                return dst.ToArray();
            }
        }

        // Decoded stream data, or null when a filter is not supported
        public static byte[] DecodeStream(PdfDoc doc, PdfStream s)
        {
            PdfObj f = doc.Resolve(s.Dict.Get("Filter"));
            if (f == null || f is PdfNull) return s.Data;
            List<string> filters = new List<string>();
            if (f is PdfName) filters.Add(((PdfName)f).Raw);
            else if (f is PdfArr) foreach (PdfObj o in ((PdfArr)f).Items) { PdfName n = doc.Resolve(o) as PdfName; if (n != null) filters.Add(n.Raw); }
            if (s.Dict.Get("DecodeParms") != null && !(doc.Resolve(s.Dict.Get("DecodeParms")) is PdfNull))
            {
                PdfDict dp = doc.Resolve(s.Dict.Get("DecodeParms")) as PdfDict;
                if (dp != null && dp.Get("Predictor") != null) return null;
            }
            byte[] d = s.Data;
            foreach (string name in filters)
            {
                if (name == "FlateDecode" || name == "Fl") d = Decode(d);
                else return null;
            }
            return d;
        }
    }

    // =================================================================
    // Reader
    // =================================================================
    public sealed class PdfDoc
    {
        public string Path;
        public byte[] Bytes;
        public Dictionary<int, PdfObj> Objects = new Dictionary<int, PdfObj>();
        public PdfDict Trailer = new PdfDict();
        public PdfDict Root;
        public List<PdfDict> Pages = new List<PdfDict>();

        public static PdfDoc Load(string path)
        {
            PdfDoc d = new PdfDoc();
            d.Path = path;
            d.Bytes = File.ReadAllBytes(path);
            d.Scan();
            d.Root = d.Resolve(d.Trailer.Get("Root")) as PdfDict;
            if (d.Root == null) throw new Exception("No /Root in " + path);
            d.CollectPages(d.Resolve(d.Root.Get("Pages")) as PdfDict, null, 0);
            if (d.Pages.Count == 0) throw new Exception("No pages in " + path);
            return d;
        }

        public PdfObj Resolve(PdfObj o)
        {
            int guard = 0;
            while (o is PdfRef && guard++ < 32)
            {
                PdfObj t;
                o = Objects.TryGetValue(((PdfRef)o).Num, out t) ? t : PdfNull.Instance;
            }
            return o;
        }

        void Scan()
        {
            byte[] b = Bytes;
            int pos = 0;
            List<PdfStream> objStms = new List<PdfStream>();
            int tr = IndexOf(b, "trailer", 0);
            while (pos < b.Length)
            {
                int hit = FindObjHeader(b, pos);
                if (tr >= 0 && tr < pos) tr = IndexOf(b, "trailer", pos);
                if (tr >= 0 && (hit < 0 || tr < hit))
                {
                    PdfParser tp = new PdfParser(b, tr + 7);
                    PdfDict td = tp.ReadObject() as PdfDict;
                    if (td != null) MergeTrailer(td);
                    pos = tp.Pos;
                    continue;
                }
                if (hit < 0) break;
                PdfParser p = new PdfParser(b, hit);
                PdfNum num = p.ReadObject() as PdfNum;
                p.ReadObject();
                p.ReadKeyword();   // obj
                PdfObj o = p.ReadObject();
                string kw = p.PeekKeyword();
                if (kw == "stream" && o is PdfDict)
                {
                    p.ReadKeyword();
                    int start = p.Pos;
                    if (start < b.Length && b[start] == 13) start++;
                    if (start < b.Length && b[start] == 10) start++;
                    int len = -1;
                    PdfObj lo = ((PdfDict)o).Get("Length");
                    if (lo is PdfNum) len = (int)((PdfNum)lo).Value;
                    int end;
                    if (len >= 0 && start + len <= b.Length && LooksLikeEndstream(b, start + len)) end = start + len;
                    else
                    {
                        int es = IndexOf(b, "endstream", start);
                        if (es < 0) es = b.Length;
                        end = es;
                        while (end > start && (b[end - 1] == 10 || b[end - 1] == 13)) end--;
                    }
                    byte[] data = new byte[end - start];
                    Array.Copy(b, start, data, 0, data.Length);
                    PdfStream s = new PdfStream((PdfDict)o, data);
                    o = s;
                    int es2 = IndexOf(b, "endstream", end);
                    pos = es2 < 0 ? b.Length : es2 + 9;
                    if (((PdfDict)s.Dict).NameOf("Type") == "ObjStm") objStms.Add(s);
                    if (((PdfDict)s.Dict).NameOf("Type") == "XRef") MergeTrailer(s.Dict);
                }
                else pos = p.Pos;
                Objects[(int)num.Value] = o;
            }
            foreach (PdfStream os in objStms) LoadObjStm(os);
        }

        void MergeTrailer(PdfDict td)
        {
            foreach (string k in td.Keys)
                if (k == "Root" || k == "Info" || k == "ID" || Trailer.Get(k) == null) Trailer.Set(k, td.Get(k));
        }

        void LoadObjStm(PdfStream os)
        {
            byte[] data = Flate.DecodeStream(this, os);
            if (data == null) return;
            int n = (int)((PdfNum)Resolve(os.Dict.Get("N"))).Value;
            int first = (int)((PdfNum)Resolve(os.Dict.Get("First"))).Value;
            PdfParser hp = new PdfParser(data, 0);
            int[] nums = new int[n], offs = new int[n];
            for (int i = 0; i < n; i++)
            {
                nums[i] = (int)((PdfNum)hp.ReadObject()).Value;
                offs[i] = (int)((PdfNum)hp.ReadObject()).Value;
            }
            for (int i = 0; i < n; i++)
            {
                if (Objects.ContainsKey(nums[i]) && !(Objects[nums[i]] is PdfNull)) continue;   // a later plain object wins
                PdfParser op = new PdfParser(data, first + offs[i]);
                Objects[nums[i]] = op.ReadObject();
            }
        }

        static bool LooksLikeEndstream(byte[] b, int at)
        {
            int i = at;
            while (i < b.Length && (b[i] == 10 || b[i] == 13 || b[i] == 32)) i++;
            return i + 9 <= b.Length && b[i] == 'e' && b[i + 1] == 'n' && b[i + 2] == 'd' && b[i + 3] == 's';
        }

        // Next "N G obj" at a token boundary
        static int FindObjHeader(byte[] b, int from)
        {
            int i = from;
            while (true)
            {
                int k = IndexOf(b, "obj", i);
                if (k < 0) return -1;
                i = k + 3;
                if (k > 0 && !PdfParser.IsWhite(b[k - 1])) continue;
                if (k + 3 < b.Length && !PdfParser.IsWhite(b[k + 3]) && !PdfParser.IsDelim(b[k + 3])) continue;
                int j = k - 1;
                while (j >= 0 && PdfParser.IsWhite(b[j])) j--;
                int g1 = j;
                while (j >= 0 && b[j] >= '0' && b[j] <= '9') j--;
                if (j == g1) continue;
                while (j >= 0 && PdfParser.IsWhite(b[j])) j--;
                int n1 = j;
                while (j >= 0 && b[j] >= '0' && b[j] <= '9') j--;
                if (j == n1) continue;
                if (j >= 0 && !PdfParser.IsWhite(b[j]) && !PdfParser.IsDelim(b[j]) && b[j] != '>') continue;
                return j + 1;
            }
        }

        public static int IndexOf(byte[] b, string s, int from)
        {
            byte f = (byte)s[0];
            int last = b.Length - s.Length;
            for (int i = Math.Max(0, from); i <= last; i++)
            {
                if (b[i] != f) continue;
                int k = 1;
                while (k < s.Length && b[i + k] == (byte)s[k]) k++;
                if (k == s.Length) return i;
            }
            return -1;
        }

        void CollectPages(PdfDict node, PdfDict inherited, int depth)
        {
            if (node == null || depth > 64) return;
            PdfDict inh = new PdfDict();
            if (inherited != null) foreach (string k in inherited.Keys) inh.Set(k, inherited.Get(k));
            foreach (string k in new string[] { "Resources", "MediaBox", "CropBox", "Rotate" })
                if (node.Get(k) != null) inh.Set(k, node.Get(k));
            if (node.NameOf("Type") == "Pages" || node.Get("Kids") != null)
            {
                PdfArr kids = Resolve(node.Get("Kids")) as PdfArr;
                if (kids == null) return;
                foreach (PdfObj k in kids.Items) CollectPages(Resolve(k) as PdfDict, inh, depth + 1);
                return;
            }
            PdfDict page = new PdfDict();
            foreach (string k in node.Keys) page.Set(k, node.Get(k));
            foreach (string k in inh.Keys) if (page.Get(k) == null) page.Set(k, inh.Get(k));
            Pages.Add(page);
        }

        public double[] Box(PdfDict page, string key)
        {
            PdfArr a = Resolve(page.Get(key)) as PdfArr;
            if (a == null || a.Items.Count != 4) return null;
            double[] r = new double[4];
            for (int i = 0; i < 4; i++) r[i] = ((PdfNum)Resolve(a.Items[i])).Value;
            return r;
        }

        // Ordered content stream objects of a page (refs kept so they can be re-imported)
        public List<PdfObj> ContentRefs(PdfDict page)
        {
            List<PdfObj> list = new List<PdfObj>();
            PdfObj c = page.Get("Contents");
            if (c == null) return list;
            PdfObj rc = Resolve(c);
            if (rc is PdfArr) list.AddRange(((PdfArr)rc).Items);
            else list.Add(c);
            return list;
        }
    }

    // =================================================================
    // Writer
    // =================================================================
    public sealed class PdfWriter
    {
        readonly List<PdfObj> objs = new List<PdfObj> { null };   // index 0 unused
        readonly Dictionary<string, int> streamHash = new Dictionary<string, int>();
        public readonly Dictionary<string, int> OcgByName = new Dictionary<string, int>();
        public readonly List<int> OcgOrder = new List<int>();
        public int DedupedStreams;
        public long DedupedBytes;

        public int Reserve() { objs.Add(null); return objs.Count - 1; }
        public int Add(PdfObj o) { objs.Add(o); return objs.Count - 1; }
        public void Set(int num, PdfObj o) { objs[num] = o; }
        public PdfObj Get(int num) { return objs[num]; }

        // Deep-copies o from src into this writer, renumbering references through map.
        public PdfObj Import(PdfDoc src, PdfObj o, Dictionary<int, int> map)
        {
            PdfRef r = o as PdfRef;
            if (r != null)
            {
                int nn;
                if (map.TryGetValue(r.Num, out nn)) return new PdfRef(nn, 0);
                PdfObj target = src.Resolve(r);
                PdfStream ts = target as PdfStream;
                string key = null;
                if (ts != null && !HasRefs(ts.Dict, "Length"))
                {
                    key = HashStream(ts);
                    int existing;
                    if (streamHash.TryGetValue(key, out existing))
                    {
                        map[r.Num] = existing;
                        DedupedStreams++;
                        DedupedBytes += ts.Data.Length;
                        return new PdfRef(existing, 0);
                    }
                }
                int num = Reserve();
                map[r.Num] = num;
                if (key != null) streamHash[key] = num;
                objs[num] = ImportDirect(src, target, map);
                return new PdfRef(num, 0);
            }
            return ImportDirect(src, o, map);
        }

        PdfObj ImportDirect(PdfDoc src, PdfObj o, Dictionary<int, int> map)
        {
            if (o is PdfDict)
            {
                PdfDict d = (PdfDict)o, nd = new PdfDict();
                foreach (string k in d.Keys) nd.Set(k, Import(src, d.Get(k), map));
                return nd;
            }
            if (o is PdfArr)
            {
                PdfArr a = new PdfArr();
                foreach (PdfObj i in ((PdfArr)o).Items) a.Items.Add(Import(src, i, map));
                return a;
            }
            if (o is PdfStream)
            {
                PdfStream s = (PdfStream)o;
                PdfDict nd = (PdfDict)ImportDirect(src, s.Dict, map);
                nd.Set("Length", new PdfNum(s.Data.Length.ToString(CultureInfo.InvariantCulture)));
                return new PdfStream(nd, s.Data);
            }
            return o;
        }

        static bool HasRefs(PdfObj o, string skipKey)
        {
            if (o is PdfRef) return true;
            if (o is PdfDict)
            {
                PdfDict d = (PdfDict)o;
                foreach (string k in d.Keys) if (k != skipKey && HasRefs(d.Get(k), null)) return true;
            }
            if (o is PdfArr) foreach (PdfObj i in ((PdfArr)o).Items) if (HasRefs(i, null)) return true;
            return false;
        }

        static string HashStream(PdfStream s)
        {
            using (SHA1 sha = SHA1.Create())
            {
                PdfDict d = new PdfDict();
                foreach (string k in s.Dict.Keys) if (k != "Length") d.Set(k, s.Dict.Get(k));
                byte[] head = Serialize(d);
                sha.TransformBlock(head, 0, head.Length, null, 0);
                sha.TransformFinalBlock(s.Data, 0, s.Data.Length);
                return Convert.ToBase64String(sha.Hash) + ":" + s.Data.Length.ToString(CultureInfo.InvariantCulture);
            }
        }

        // One canonical OCG per layer name across the whole output
        public int OcgFor(string name, PdfDict usageTemplate)
        {
            int n;
            if (OcgByName.TryGetValue(name, out n)) return n;
            PdfDict g = new PdfDict();
            g.Set("Type", new PdfName("OCG"));
            g.Set("Name", PdfStr.FromText(name));
            if (usageTemplate != null) g.Set("Usage", usageTemplate);
            n = Add(g);
            OcgByName[name] = n;
            OcgOrder.Add(n);
            return n;
        }

        public static byte[] Serialize(PdfObj o)
        {
            MemoryStream ms = new MemoryStream();
            Write(ms, o);
            return ms.ToArray();
        }

        static void W(Stream s, string t) { byte[] x = Encoding.GetEncoding(28591).GetBytes(t); s.Write(x, 0, x.Length); }

        static void Write(Stream s, PdfObj o)
        {
            if (o == null || o is PdfNull) { W(s, "null"); return; }
            if (o is PdfBool) { W(s, ((PdfBool)o).Value ? "true" : "false"); return; }
            if (o is PdfNum) { W(s, ((PdfNum)o).Text); return; }
            if (o is PdfStr) { byte[] r = ((PdfStr)o).Raw; s.Write(r, 0, r.Length); return; }
            if (o is PdfName) { W(s, "/" + ((PdfName)o).Raw); return; }
            if (o is PdfRef) { W(s, ((PdfRef)o).Num.ToString(CultureInfo.InvariantCulture) + " 0 R"); return; }
            if (o is PdfArr)
            {
                W(s, "[");
                bool first = true;
                foreach (PdfObj i in ((PdfArr)o).Items) { if (!first) W(s, " "); Write(s, i); first = false; }
                W(s, "]");
                return;
            }
            if (o is PdfDict)
            {
                PdfDict d = (PdfDict)o;
                W(s, "<<");
                foreach (string k in d.Keys) { W(s, "/" + k + " "); Write(s, d.Get(k)); W(s, "\n"); }
                W(s, ">>");
                return;
            }
            if (o is PdfStream)
            {
                PdfStream st = (PdfStream)o;
                st.Dict.Set("Length", new PdfNum(st.Data.Length.ToString(CultureInfo.InvariantCulture)));
                Write(s, st.Dict);
                W(s, "\nstream\n");
                s.Write(st.Data, 0, st.Data.Length);
                W(s, "\nendstream");
                return;
            }
            W(s, "null");
        }

        public long Save(string path, int rootNum, int infoNum)
        {
            string tmp = path + ".part";
            long[] offs = new long[objs.Count];
            using (FileStream fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 20))
            {
                W(fs, "%PDF-1.5\n");
                fs.Write(new byte[] { (byte)'%', 0xE2, 0xE3, 0xCF, 0xD3, 10 }, 0, 6);
                for (int i = 1; i < objs.Count; i++)
                {
                    offs[i] = fs.Position;
                    W(fs, i.ToString(CultureInfo.InvariantCulture) + " 0 obj\n");
                    Write(fs, objs[i]);
                    W(fs, "\nendobj\n");
                }
                long xref = fs.Position;
                W(fs, "xref\n0 " + objs.Count.ToString(CultureInfo.InvariantCulture) + "\n0000000000 65535 f \n");
                for (int i = 1; i < objs.Count; i++) W(fs, offs[i].ToString("0000000000", CultureInfo.InvariantCulture) + " 00000 n \n");
                string id = Guid.NewGuid().ToString("N").ToUpperInvariant();
                W(fs, "trailer\n<</Size " + objs.Count.ToString(CultureInfo.InvariantCulture) + " /Root " + rootNum.ToString(CultureInfo.InvariantCulture) + " 0 R /Info " +
                      infoNum.ToString(CultureInfo.InvariantCulture) + " 0 R /ID [<" + id + "> <" + id + ">]>>\nstartxref\n" +
                      xref.ToString(CultureInfo.InvariantCulture) + "\n%%EOF\n");
            }
            if (File.Exists(path)) File.Delete(path);
            File.Move(tmp, path);
            return new FileInfo(path).Length;
        }
    }

    // =================================================================
    // Content stream scanning
    // =================================================================
    public static class Content
    {
        // Leading "/OC /Name BDC" resource name of a 12d model stream, or null
        public static string LeadingOcResource(byte[] data)
        {
            PdfParser p = new PdfParser(data, 0);
            for (int i = 0; i < 6 && !p.AtEnd; i++)
            {
                PdfObj o = p.ReadObject();
                PdfName n = o as PdfName;
                if (n != null && n.Raw == "OC")
                {
                    PdfName r = p.ReadObject() as PdfName;
                    return r == null ? null : r.Raw;
                }
            }
            return null;
        }

        // True when the stream contains any painting operator
        public static bool Paints(byte[] data)
        {
            PdfParser p = new PdfParser(data, 0);
            while (!p.AtEnd)
            {
                int before = p.Pos;
                PdfKeyword k = p.ReadObject() as PdfKeyword;
                if (p.Pos == before) { p.Pos++; continue; }
                if (k == null) continue;
                switch (k.Word)
                {
                    case "S": case "s": case "f": case "F": case "f*": case "B": case "B*": case "b": case "b*":
                    case "Tj": case "TJ": case "'": case "\"": case "Do": case "sh": case "BI":
                        return true;
                }
            }
            return false;
        }

        // Placed size (points) of each image XObject drawn on the page: name -> [width, height]
        public static Dictionary<string, double[]> ImagePlacements(byte[] data)
        {
            Dictionary<string, double[]> res = new Dictionary<string, double[]>();
            Stack<double[]> stack = new Stack<double[]>();
            double[] ctm = { 1, 0, 0, 1, 0, 0 };
            List<PdfObj> ops = new List<PdfObj>();
            PdfParser p = new PdfParser(data, 0);
            while (!p.AtEnd)
            {
                int before = p.Pos;
                PdfObj o = p.ReadObject();
                if (p.Pos == before) { p.Pos++; continue; }
                PdfKeyword k = o as PdfKeyword;
                if (k == null) { ops.Add(o); continue; }
                switch (k.Word)
                {
                    case "q": stack.Push((double[])ctm.Clone()); break;
                    case "Q": if (stack.Count > 0) ctm = stack.Pop(); break;
                    case "cm":
                        if (ops.Count >= 6)
                        {
                            double[] m = new double[6];
                            bool ok = true;
                            for (int i = 0; i < 6; i++)
                            {
                                PdfNum n = ops[ops.Count - 6 + i] as PdfNum;
                                if (n == null) { ok = false; break; }
                                m[i] = n.Value;
                            }
                            if (ok) ctm = Mul(m, ctm);
                        }
                        break;
                    case "Do":
                        if (ops.Count >= 1)
                        {
                            PdfName nm = ops[ops.Count - 1] as PdfName;
                            if (nm != null)
                            {
                                double w = Math.Sqrt(ctm[0] * ctm[0] + ctm[1] * ctm[1]);
                                double h = Math.Sqrt(ctm[2] * ctm[2] + ctm[3] * ctm[3]);
                                double[] prev;
                                if (!res.TryGetValue(nm.Raw, out prev) || w * h > prev[0] * prev[1]) res[nm.Raw] = new double[] { w, h };
                            }
                        }
                        break;
                    case "BI":
                        int id = PdfDoc.IndexOf(data, "ID", p.Pos);
                        int ei = id < 0 ? -1 : PdfDoc.IndexOf(data, "EI", id + 3);
                        p.Pos = ei < 0 ? data.Length : ei + 2;
                        break;
                }
                ops.Clear();
            }
            return res;
        }

        static double[] Mul(double[] a, double[] b)
        {
            return new double[] {
                a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
                a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
                a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5] };
        }
    }

    // =================================================================
    // Content index: a 12d page cut into its operations, each drawing
    // with its extent on the page, so a sheet can take just what lies in
    // its window.  State operators (colours, widths, fonts, layers, q/Q)
    // always go through; a drawing that is left out still hands on any
    // state change made inside it.
    // =================================================================
    public sealed class ContentIndex
    {
        public const int STATE = 0, DRAW = 1, PUSH = 2, POP = 3;

        public sealed class Op
        {
            public int Kind, Start, Len, Stream;
            public double X0 = double.MaxValue, Y0 = double.MaxValue, X1 = double.MinValue, Y1 = double.MinValue;
            public List<int[]> Keep;   // state operators inside a drawing: {start, length}

            public void Add(double[] m, double x, double y)
            {
                double px = m[0] * x + m[2] * y + m[4], py = m[1] * x + m[3] * y + m[5];
                if (px < X0) X0 = px; if (px > X1) X1 = px;
                if (py < Y0) Y0 = py; if (py > Y1) Y1 = py;
            }

            public void Grow(double d) { if (X0 <= X1) { X0 -= d; Y0 -= d; X1 += d; Y1 += d; } }
        }

        public byte[] Data;
        public readonly List<Op> Ops = new List<Op>();
        public readonly List<string> Layers = new List<string>();   // layer of each content stream, null if none
        public int Streams { get { return Layers.Count; } }

        public static ContentIndex Build(PdfDoc doc, PdfDict page)
        {
            ContentIndex ix = new ContentIndex();
            Dictionary<string, string> resToLayer = new Dictionary<string, string>();
            PdfDict res = doc.Resolve(page.Get("Resources")) as PdfDict;
            PdfDict props = res == null ? null : doc.Resolve(res.Get("Properties")) as PdfDict;
            if (props != null)
                foreach (string k in props.Keys)
                {
                    PdfDict g = doc.Resolve(props.Get(k)) as PdfDict;
                    PdfStr nm = g == null ? null : doc.Resolve(g.Get("Name")) as PdfStr;
                    if (nm != null) resToLayer[k] = nm.Decode();
                }
            MemoryStream all = new MemoryStream();
            List<int> bounds = new List<int>();
            foreach (PdfObj c in doc.ContentRefs(page))
            {
                PdfStream cs = doc.Resolve(c) as PdfStream;
                byte[] d = cs == null ? null : Flate.DecodeStream(doc, cs);
                bounds.Add((int)all.Length);
                string layer = null;
                if (d != null)
                {
                    string rn = Content.LeadingOcResource(d);
                    if (rn != null) resToLayer.TryGetValue(rn, out layer);
                    all.Write(d, 0, d.Length);
                }
                ix.Layers.Add(layer);
                all.WriteByte(10);
            }
            bounds.Add((int)all.Length);
            ix.Data = all.ToArray();
            ix.Scan(bounds);
            return ix;
        }

        static double[] Mul(double[] a, double[] b)
        {
            return new double[] {
                a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3],
                a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3],
                a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5] };
        }

        static double Num(List<PdfObj> ops, int fromEnd)
        {
            int i = ops.Count - fromEnd;
            if (i < 0) return 0;
            PdfNum n = ops[i] as PdfNum;
            return n == null ? 0 : n.Value;
        }

        static int StrLen(PdfObj o)
        {
            PdfStr s = o as PdfStr;
            if (s != null) return Math.Max(1, s.Raw.Length - 2);
            PdfArr a = o as PdfArr;
            int n = 0;
            if (a != null) foreach (PdfObj i in a.Items) { PdfStr t = i as PdfStr; if (t != null) n += Math.Max(0, t.Raw.Length - 2); }
            return Math.Max(1, n);
        }

        void Scan(List<int> bounds)
        {
            byte[] b = Data;
            PdfParser p = new PdfParser(b, 0);
            double[] ctm = { 1, 0, 0, 1, 0, 0 };
            Stack<double[]> cst = new Stack<double[]>();
            Stack<double> lst = new Stack<double>();
            double lw = 1, fontSize = 10, lead = 0;
            List<PdfObj> operands = new List<PdfObj>();
            int opStart = 0, stream = 0;
            Op path = null, text = null;
            bool clip = false;
            double[] tm = null, tlm = null;
            while (true)
            {
                p.SkipWhite();
                if (p.Pos >= b.Length) break;
                int start = p.Pos;
                if (operands.Count == 0) opStart = start;
                PdfObj o = p.ReadObject();
                if (p.Pos == start) { p.Pos++; continue; }
                PdfKeyword kw = o as PdfKeyword;
                if (kw == null) { operands.Add(o); continue; }
                int end = p.Pos;
                string w = kw.Word;
                while (stream + 1 < bounds.Count - 1 && opStart >= bounds[stream + 1]) stream++;
                Op cur = path != null ? path : text;
                switch (w)
                {
                    case "m": case "l": case "c": case "v": case "y": case "h": case "re":
                        if (text != null) break;
                        if (path == null) { path = new Op(); path.Kind = DRAW; path.Start = opStart; path.Stream = stream; }
                        if (w == "re")
                        {
                            double x = Num(operands, 4), y = Num(operands, 3), ww = Num(operands, 2), hh = Num(operands, 1);
                            path.Add(ctm, x, y); path.Add(ctm, x + ww, y); path.Add(ctm, x, y + hh); path.Add(ctm, x + ww, y + hh);
                        }
                        else for (int i = operands.Count - 2; i >= 0; i -= 2) path.Add(ctm, Num(operands, operands.Count - i), Num(operands, operands.Count - i - 1));
                        path.Len = end - path.Start;
                        operands.Clear();
                        continue;
                    case "W": case "W*":
                        if (path == null) { path = new Op(); path.Kind = DRAW; path.Start = opStart; path.Stream = stream; }
                        clip = true;
                        path.Len = end - path.Start;
                        operands.Clear();
                        continue;
                    case "S": case "s": case "f": case "F": case "f*": case "B": case "B*": case "b": case "b*": case "n":
                        if (path != null)
                        {
                            path.Len = end - path.Start;
                            double sc = Math.Sqrt(Math.Abs(ctm[0] * ctm[3] - ctm[1] * ctm[2]));
                            path.Grow(lw * sc / 2 + 1);
                            if (clip) path.Kind = STATE;   // clipping always applies
                            Ops.Add(path);
                            path = null;
                            clip = false;
                            operands.Clear();
                            continue;
                        }
                        break;
                    case "BT":
                        text = new Op(); text.Kind = DRAW; text.Start = opStart; text.Stream = stream;
                        tm = new double[] { 1, 0, 0, 1, 0, 0 }; tlm = (double[])tm.Clone();
                        operands.Clear();
                        continue;
                    case "ET":
                        if (text != null)
                        {
                            text.Len = end - text.Start;
                            Ops.Add(text);
                            text = null;
                            operands.Clear();
                            continue;
                        }
                        break;
                    case "BI":
                        {
                            int id = PdfDoc.IndexOf(b, "ID", p.Pos);
                            int ei = id < 0 ? -1 : PdfDoc.IndexOf(b, "EI", id + 3);
                            p.Pos = ei < 0 ? b.Length : ei + 2;
                            Op im = new Op(); im.Kind = STATE; im.Start = opStart; im.Len = p.Pos - opStart; im.Stream = stream;
                            Ops.Add(im);
                            operands.Clear();
                            continue;
                        }
                }
                if (text != null)
                {
                    bool show = false;
                    switch (w)
                    {
                        case "Tm":
                            if (operands.Count >= 6) { tm = new double[] { Num(operands, 6), Num(operands, 5), Num(operands, 4), Num(operands, 3), Num(operands, 2), Num(operands, 1) }; tlm = (double[])tm.Clone(); }
                            break;
                        case "Td": case "TD":
                            tlm = Mul(new double[] { 1, 0, 0, 1, Num(operands, 2), Num(operands, 1) }, tlm); tm = (double[])tlm.Clone();
                            if (w == "TD") lead = -Num(operands, 1);
                            break;
                        case "T*":
                            tlm = Mul(new double[] { 1, 0, 0, 1, 0, -lead }, tlm); tm = (double[])tlm.Clone();
                            break;
                        case "Tj": case "TJ": case "'": case "\"":
                            show = true;
                            if (w != "Tj" && w != "TJ") { tlm = Mul(new double[] { 1, 0, 0, 1, 0, -lead }, tlm); tm = (double[])tlm.Clone(); }
                            break;
                    }
                    if (show)
                    {
                        // generous extent: every byte a 0.6 em glyph
                        double width = fontSize * 0.6 * StrLen(operands.Count > 0 ? operands[operands.Count - 1] : null);
                        double[] m = Mul(tm, ctm);
                        text.Add(m, 0, -0.3 * fontSize); text.Add(m, width, -0.3 * fontSize);
                        text.Add(m, 0, fontSize); text.Add(m, width, fontSize);
                        tm = Mul(new double[] { 1, 0, 0, 1, width, 0 }, tm);
                        operands.Clear();
                        continue;
                    }
                    if (w == "Tf") fontSize = Math.Abs(Num(operands, 1));
                    if (w == "TL") lead = Num(operands, 1);
                    if (w == "w") lw = Num(operands, 1);
                    if (w != "Tm" && w != "Td" && w != "TD" && w != "T*")
                    {
                        if (text.Keep == null) text.Keep = new List<int[]>();
                        text.Keep.Add(new int[] { opStart, end - opStart });
                    }
                    operands.Clear();
                    continue;
                }
                // graphics state and everything else
                if (w == "w") lw = Num(operands, 1);
                if (w == "Tf") fontSize = Math.Abs(Num(operands, 1));
                if (w == "TL") lead = Num(operands, 1);
                if (path != null)
                {
                    // a state change in the middle of a path travels with it
                    if (path.Keep == null) path.Keep = new List<int[]>();
                    path.Keep.Add(new int[] { opStart, end - opStart });
                    path.Len = end - path.Start;
                    operands.Clear();
                    continue;
                }
                Op op = new Op();
                op.Start = opStart; op.Len = end - opStart; op.Stream = stream;
                if (w == "q") { op.Kind = PUSH; cst.Push((double[])ctm.Clone()); lst.Push(lw); }
                else if (w == "Q") { op.Kind = POP; if (cst.Count > 0) { ctm = cst.Pop(); lw = lst.Pop(); } }
                else if (w == "cm")
                {
                    op.Kind = STATE;
                    if (operands.Count >= 6)
                        ctm = Mul(new double[] { Num(operands, 6), Num(operands, 5), Num(operands, 4), Num(operands, 3), Num(operands, 2), Num(operands, 1) }, ctm);
                }
                else if (w == "Do")
                {
                    op.Kind = DRAW;
                    op.Add(ctm, 0, 0); op.Add(ctm, 1, 0); op.Add(ctm, 0, 1); op.Add(ctm, 1, 1);
                }
                else op.Kind = STATE;
                Ops.Add(op);
                operands.Clear();
            }
            if (path != null) { path.Kind = STATE; Ops.Add(path); }
            if (text != null) { text.Kind = STATE; text.Len = b.Length - text.Start; Ops.Add(text); }
        }

        // Operations of streams [from, to) that touch the window {x0, y0, x1, y1}, with q/Q balanced
        public byte[] Emit(double[] win, int from, int to, out bool painted)
        {
            painted = false;
            MemoryStream o = new MemoryStream();
            int depth = 0;
            foreach (Op op in Ops)
            {
                if (op.Stream < from || op.Stream >= to) continue;
                switch (op.Kind)
                {
                    case PUSH:
                        depth++;
                        break;
                    case POP:
                        if (depth == 0) continue;
                        depth--;
                        break;
                    case DRAW:
                        bool unknown = op.X0 > op.X1;
                        if (!unknown && (op.X1 < win[0] || op.X0 > win[2] || op.Y1 < win[1] || op.Y0 > win[3]))
                        {
                            if (op.Keep != null)
                                foreach (int[] k in op.Keep) { o.Write(Data, k[0], k[1]); o.WriteByte(10); }
                            continue;
                        }
                        painted = true;
                        break;
                }
                o.Write(Data, op.Start, op.Len);
                o.WriteByte(10);
            }
            for (; depth > 0; depth--) { o.WriteByte((byte)'Q'); o.WriteByte(10); }
            return o.ToArray();
        }

        // First stream drawn on the given layer, or -1
        public int StreamOfLayer(string layer)
        {
            if (layer == null) return -1;
            for (int i = 0; i < Layers.Count; i++) if (Layers[i] == layer) return i;
            return -1;
        }
    }

    // =================================================================
    // 12d tiled image (.image folder: imginfo.12d + DIB tiles)
    // =================================================================
    public sealed class TiledImage
    {
        public string Folder;
        public int Cols, Rows, TileW, TileH, Width, Height, OverlapX, OverlapY;
        public int StrideX { get { return TileW - OverlapX; } }
        public int StrideY { get { return TileH - OverlapY; } }

        sealed class Entry { public string Name; public int A, B, W, H, Vw, Vh; }
        readonly Dictionary<long, Entry> tiles = new Dictionary<long, Entry>();
        public readonly List<int[]> Overviews = new List<int[]>();   // {validW, validH, w, h}
        readonly List<string> ovNames = new List<string>();

        public static TiledImage Open(string folder)
        {
            byte[] info = File.ReadAllBytes(System.IO.Path.Combine(folder, "imginfo.12d"));
            TiledImage t = new TiledImage();
            t.Folder = folder;
            t.Cols = BitConverter.ToInt32(info, 0);
            t.Rows = BitConverter.ToInt32(info, 4);
            int nameLen = BitConverter.ToInt32(info, 8);
            t.TileW = BitConverter.ToInt32(info, 12);
            t.TileH = BitConverter.ToInt32(info, 16);
            t.Width = BitConverter.ToInt32(info, 28);
            t.Height = BitConverter.ToInt32(info, 32);
            int rec = nameLen + 36;
            for (int off = 40; off + rec <= info.Length; off += rec)
            {
                Entry e = new Entry();
                e.Name = Encoding.ASCII.GetString(info, off, nameLen).TrimEnd('\0');
                int v = off + nameLen;
                e.A = BitConverter.ToInt32(info, v);
                e.B = BitConverter.ToInt32(info, v + 4);
                e.W = BitConverter.ToInt32(info, v + 8);
                e.H = BitConverter.ToInt32(info, v + 12);
                e.Vw = BitConverter.ToInt32(info, v + 20);
                e.Vh = BitConverter.ToInt32(info, v + 24);
                t.OverlapX = BitConverter.ToInt32(info, v + 28);
                t.OverlapY = BitConverter.ToInt32(info, v + 32);
                if (e.Name.StartsWith("overview", StringComparison.OrdinalIgnoreCase))
                {
                    t.Overviews.Add(new int[] { e.Vw, e.Vh, e.W, e.H });
                    t.ovNames.Add(e.Name);
                }
                else t.tiles[Key(e.A, e.B)] = e;
            }
            if (t.Width <= 0 || t.Height <= 0 || t.tiles.Count == 0) throw new Exception("Unreadable imginfo.12d in " + folder);
            return t;
        }

        static long Key(int c, int r) { return ((long)c << 32) | (uint)r; }

        // Tile pixels as top-down BGR rows of the VALID area only (vw x vh), stride vw*3
        public byte[] ReadTile(int c, int r, out int vw, out int vh)
        {
            Entry e;
            if (!tiles.TryGetValue(Key(c, r), out e)) { vw = vh = 0; return null; }
            return ReadDib(System.IO.Path.Combine(Folder, e.Name), e.Vw, e.Vh, out vw, out vh);
        }

        public byte[] ReadOverview(int index, out int vw, out int vh)
        {
            int[] o = Overviews[index];
            return ReadDib(System.IO.Path.Combine(Folder, ovNames[index]), o[0], o[1], out vw, out vh);
        }

        // 12d stores bottom-up 24-bit DIBs whose valid rows are the bottom-most ones of the image
        static byte[] ReadDib(string path, int validW, int validH, out int vw, out int vh)
        {
            byte[] f = File.ReadAllBytes(path);
            int w = BitConverter.ToInt32(f, 4);
            int h = BitConverter.ToInt32(f, 8);
            int bpp = BitConverter.ToInt16(f, 14);
            if (bpp != 24) throw new Exception("Unsupported tile depth " + bpp + " in " + path);
            bool bottomUp = h > 0;
            h = Math.Abs(h);
            int stride = (w * 3 + 3) & ~3;
            int dataOff = f.Length - stride * h;
            if (dataOff < 40) throw new Exception("Truncated tile " + path);
            vw = Math.Min(validW, w);
            vh = Math.Min(validH, h);
            byte[] o = new byte[vw * vh * 3];
            for (int j = 0; j < vh; j++)
            {
                // j = 0 is the top valid row; valid rows are the bottom vh rows of the image
                int imgRow = h - vh + j;
                int storageRow = bottomUp ? (h - 1 - imgRow) : imgRow;
                Buffer.BlockCopy(f, dataOff + storageRow * stride, o, j * vw * 3, vw * 3);
            }
            return o;
        }

        public bool HasTile(int c, int r) { return tiles.ContainsKey(Key(c, r)); }
    }

    // =================================================================
    // Raster rendering (aerial -> page pixels)
    // =================================================================
    public sealed class RasterSpec
    {
        public string Id, Model, Element, Folder;
        public double Xmin, Ymin, Xmax, Ymax;
        public TiledImage Image;
    }

    public sealed class Canvas
    {
        public int W, H;
        public float[] Rgb;      // premultiplied colour, 3 per pixel
        public float[] Cover;    // accumulated alpha 0..1

        public Canvas(int w, int h) { W = w; H = h; Rgb = new float[w * h * 3]; Cover = new float[w * h]; }

        // Paints src (straight colour, coverage cov) over the canvas with opacity a
        public void Over(int idx, float r, float g, float b, float a)
        {
            float k = 1f - a;
            Rgb[idx * 3] = r * a + Rgb[idx * 3] * k;
            Rgb[idx * 3 + 1] = g * a + Rgb[idx * 3 + 1] * k;
            Rgb[idx * 3 + 2] = b * a + Rgb[idx * 3 + 2] * k;
            Cover[idx] = a + Cover[idx] * k;
        }
    }

    public static class RasterRender
    {
        // Renders one raster into the canvas.  u = u0 + i*du, v = v0 + j*dv map canvas pixel
        // centres (i, j) to raster pixel coordinates measured from the top-left corner.
        public static void Draw(Canvas cv, TiledImage img, double u0, double v0, double du, double dv, float opacity, StringBuilder log)
        {
            // pick a pyramid level: the coarsest that is not more than 25% coarser than needed
            int level = -1;
            double fBest = 1;
            for (int i = 0; i < img.Overviews.Count; i++)
            {
                double f = (double)img.Width / img.Overviews[i][0];
                if (f <= Math.Min(du, dv) * 1.25 && f > fBest) { fBest = f; level = i; }
            }
            if (level >= 0)
            {
                int ow, oh;
                byte[] ov = img.ReadOverview(level, out ow, out oh);
                double fx = (double)img.Width / ow, fy = (double)img.Height / oh;
                if (log != null) log.AppendLine("    level overview " + ow + "x" + oh + " (1/" + Fmt.N(fx) + ")");
                SampleBuffer(cv, ov, null, ow, oh, 0, 0, u0 / fx, v0 / fy, du / fx, dv / fy, opacity);
                return;
            }
            if (du >= 1.5 && dv >= 1.5)
            {
                if (log != null) log.AppendLine("    level base, area average " + Fmt.N(du) + " px/px");
                AreaFromTiles(cv, img, u0, v0, du, dv, opacity);
                return;
            }
            // bilinear from the base tiles: assemble just the needed window
            double ua = u0 - 0.5 * du - 2, ub = u0 + (cv.W - 0.5) * du + 2;
            double va = v0 - 0.5 * dv - 2, vb = v0 + (cv.H - 0.5) * dv + 2;
            int x0 = Math.Max(0, (int)Math.Floor(ua)), x1 = Math.Min(img.Width, (int)Math.Ceiling(ub));
            int y0 = Math.Max(0, (int)Math.Floor(va)), y1 = Math.Min(img.Height, (int)Math.Ceiling(vb));
            if (x1 <= x0 || y1 <= y0) return;
            int bw = x1 - x0, bh = y1 - y0;
            byte[] buf = new byte[(long)bw * bh * 3];
            byte[] have = new byte[(long)bw * bh];   // a missing tile leaves a hole, not black
            int tiles = 0;
            ForTiles(img, x0, y0, x1, y1, delegate (byte[] t, int tw, int th, int px, int py)
            {
                tiles++;
                for (int j = 0; j < th; j++)
                {
                    int y = py + j;
                    if (y < y0 || y >= y1) continue;
                    int xa = Math.Max(px, x0), xb = Math.Min(px + tw, x1);
                    if (xb <= xa) continue;
                    Buffer.BlockCopy(t, (j * tw + (xa - px)) * 3, buf, ((y - y0) * bw + (xa - x0)) * 3, (xb - xa) * 3);
                    for (int x = xa; x < xb; x++) have[(y - y0) * bw + (x - x0)] = 1;
                }
            });
            if (log != null) log.AppendLine("    level base, bilinear " + Fmt.N(du) + " px/px, window " + bw + "x" + bh + ", " + tiles + " tiles");
            SampleBuffer(cv, buf, have, bw, bh, x0, y0, u0, v0, du, dv, opacity);
        }

        delegate void TileFn(byte[] t, int tw, int th, int px, int py);

        // Visits every tile touching [x0,x1) x [y0,y1) (image coords from the top-left).
        // px, py is the tile's top-left image position; overlap pixels are re-delivered by neighbours.
        static void ForTiles(TiledImage img, int x0, int y0, int x1, int y1, TileFn fn)
        {
            int sx = img.StrideX, sy = img.StrideY;
            int c0 = Math.Max(0, x0 / sx - 1), c1 = Math.Min(img.Cols - 1, x1 / sx);
            // tile row r counts from the bottom; its valid area covers rows-from-bottom [r*sy, r*sy+vh)
            int fb0 = img.Height - y1, fb1 = img.Height - y0;
            int r0 = Math.Max(0, fb0 / sy - 1), r1 = Math.Min(img.Rows - 1, fb1 / sy);
            for (int r = r0; r <= r1; r++)
                for (int c = c0; c <= c1; c++)
                {
                    if (!img.HasTile(c, r)) continue;
                    int tw, th;
                    byte[] t = img.ReadTile(c, r, out tw, out th);
                    if (t == null) continue;
                    int px = c * sx;
                    int py = img.Height - (r * sy + th);
                    if (px >= x1 || px + tw <= x0 || py >= y1 || py + th <= y0) continue;
                    fn(t, tw, th, px, py);
                }
        }

        static void AreaFromTiles(Canvas cv, TiledImage img, double u0, double v0, double du, double dv, float opacity)
        {
            int n = cv.W * cv.H;
            double[] sum = new double[n * 3];
            int[] cnt = new int[n];
            double ua = u0 - 0.5 * du, va = v0 - 0.5 * dv;
            int x0 = Math.Max(0, (int)Math.Floor(ua)), x1 = Math.Min(img.Width, (int)Math.Ceiling(ua + cv.W * du));
            int y0 = Math.Max(0, (int)Math.Floor(va)), y1 = Math.Min(img.Height, (int)Math.Ceiling(va + cv.H * dv));
            if (x1 <= x0 || y1 <= y0) return;
            int sx = img.StrideX, sy = img.StrideY;
            ForTiles(img, x0, y0, x1, y1, delegate (byte[] t, int tw, int th, int px, int py)
            {
                // count each image pixel once: the right-hand columns repeat the next tile, and
                // the top rows repeat the tile above (except in the last column / top tile row)
                int useW = Math.Min(tw, sx);
                if (px + tw >= img.Width) useW = tw;
                int skipTop = (py > 0 && th > sy) ? th - sy : 0;
                for (int j = skipTop; j < th; j++)
                {
                    int y = py + j;
                    if (y < y0 || y >= y1) continue;
                    int oj = (int)Math.Floor((y + 0.5 - va) / dv);
                    if (oj < 0 || oj >= cv.H) continue;
                    int rowBase = oj * cv.W;
                    int tOff = j * tw * 3;
                    for (int i = 0; i < useW; i++)
                    {
                        int x = px + i;
                        if (x < x0 || x >= x1) continue;
                        int oi = (int)Math.Floor((x + 0.5 - ua) / du);
                        if (oi < 0 || oi >= cv.W) continue;
                        int idx = rowBase + oi;
                        int to = tOff + i * 3;
                        sum[idx * 3] += t[to + 2];
                        sum[idx * 3 + 1] += t[to + 1];
                        sum[idx * 3 + 2] += t[to];
                        cnt[idx]++;
                    }
                }
            });
            for (int idx = 0; idx < n; idx++)
            {
                if (cnt[idx] == 0) continue;
                double inv = 1.0 / cnt[idx];
                cv.Over(idx, (float)(sum[idx * 3] * inv), (float)(sum[idx * 3 + 1] * inv), (float)(sum[idx * 3 + 2] * inv), opacity);
            }
        }

        // Bilinear (or box when shrinking) sampling of a top-down BGR buffer whose top-left is image pixel (bx, by).
        // have (optional) marks buffer pixels that hold data; a canvas pixel over a hole stays uncovered.
        static void SampleBuffer(Canvas cv, byte[] buf, byte[] have, int bw, int bh, int bx, int by, double u0, double v0, double du, double dv, float opacity)
        {
            int boxX = du > 1.5 ? (int)Math.Round(du) : 1, boxY = dv > 1.5 ? (int)Math.Round(dv) : 1;
            for (int j = 0; j < cv.H; j++)
            {
                double v = v0 + j * dv - by - 0.5;
                for (int i = 0; i < cv.W; i++)
                {
                    double u = u0 + i * du - bx - 0.5;
                    float r, g, b;
                    if (boxX == 1 && boxY == 1)
                    {
                        if (u < -0.5 || v < -0.5 || u > bw - 0.5 || v > bh - 0.5) continue;
                        if (have != null && have[Clamp((int)Math.Round(v), bh) * bw + Clamp((int)Math.Round(u), bw)] == 0) continue;
                        int xi = (int)Math.Floor(u), yi = (int)Math.Floor(v);
                        double fx = u - xi, fy = v - yi;
                        int xa = Clamp(xi, bw), xb = Clamp(xi + 1, bw), ya = Clamp(yi, bh), yb = Clamp(yi + 1, bh);
                        int p00 = (ya * bw + xa) * 3, p10 = (ya * bw + xb) * 3, p01 = (yb * bw + xa) * 3, p11 = (yb * bw + xb) * 3;
                        double w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
                        b = (float)(buf[p00] * w00 + buf[p10] * w10 + buf[p01] * w01 + buf[p11] * w11);
                        g = (float)(buf[p00 + 1] * w00 + buf[p10 + 1] * w10 + buf[p01 + 1] * w01 + buf[p11 + 1] * w11);
                        r = (float)(buf[p00 + 2] * w00 + buf[p10 + 2] * w10 + buf[p01 + 2] * w01 + buf[p11 + 2] * w11);
                    }
                    else
                    {
                        int xs = (int)Math.Floor(u - (boxX - 1) / 2.0 + 0.5), ys = (int)Math.Floor(v - (boxY - 1) / 2.0 + 0.5);
                        double sr = 0, sg = 0, sb = 0; int n = 0;
                        for (int yy = ys; yy < ys + boxY; yy++)
                        {
                            if (yy < 0 || yy >= bh) continue;
                            for (int xx = xs; xx < xs + boxX; xx++)
                            {
                                if (xx < 0 || xx >= bw) continue;
                                int p = (yy * bw + xx) * 3;
                                sb += buf[p]; sg += buf[p + 1]; sr += buf[p + 2]; n++;
                            }
                        }
                        if (n == 0) continue;
                        r = (float)(sr / n); g = (float)(sg / n); b = (float)(sb / n);
                    }
                    cv.Over(j * cv.W + i, r, g, b, opacity);
                }
            }
        }

        static int Clamp(int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); }
    }

    // =================================================================
    // JPEG through GDI+
    // =================================================================
    public static class Jpeg
    {
        static ImageCodecInfo codec;

        static ImageCodecInfo Codec()
        {
            if (codec == null)
                foreach (ImageCodecInfo c in ImageCodecInfo.GetImageEncoders())
                    if (c.MimeType == "image/jpeg") codec = c;
            return codec;
        }

        // rgb: top-down R,G,B bytes
        public static byte[] Encode(byte[] rgb, int w, int h, int quality)
        {
            using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format24bppRgb))
            {
                BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
                byte[] row = new byte[bd.Stride];
                for (int j = 0; j < h; j++)
                {
                    for (int i = 0; i < w; i++)
                    {
                        int s = (j * w + i) * 3;
                        row[i * 3] = rgb[s + 2]; row[i * 3 + 1] = rgb[s + 1]; row[i * 3 + 2] = rgb[s];
                    }
                    Marshal.Copy(row, 0, new IntPtr(bd.Scan0.ToInt64() + (long)j * bd.Stride), bd.Stride);
                }
                bmp.UnlockBits(bd);
                using (MemoryStream ms = new MemoryStream())
                using (EncoderParameters ep = new EncoderParameters(1))
                {
                    ep.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, (long)quality);
                    bmp.Save(ms, Codec(), ep);
                    return ms.ToArray();
                }
            }
        }

        // Decodes a JPEG to top-down RGB; returns null for CMYK/unsupported data
        public static byte[] Decode(byte[] jpg, out int w, out int h)
        {
            w = h = 0;
            using (MemoryStream ms = new MemoryStream(jpg))
            using (Image img = Image.FromStream(ms, false, false))
            {
                if ((img.Flags & (int)ImageFlags.ColorSpaceCmyk) != 0 || (img.Flags & (int)ImageFlags.ColorSpaceYcck) != 0) return null;
                w = img.Width; h = img.Height;
                using (Bitmap bmp = new Bitmap(w, h, PixelFormat.Format24bppRgb))
                {
                    using (Graphics g = Graphics.FromImage(bmp)) g.DrawImage(img, 0, 0, w, h);
                    BitmapData bd = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
                    byte[] row = new byte[bd.Stride];
                    byte[] rgb = new byte[w * h * 3];
                    for (int j = 0; j < h; j++)
                    {
                        Marshal.Copy(new IntPtr(bd.Scan0.ToInt64() + (long)j * bd.Stride), row, 0, bd.Stride);
                        for (int i = 0; i < w; i++)
                        {
                            int d = (j * w + i) * 3;
                            rgb[d] = row[i * 3 + 2]; rgb[d + 1] = row[i * 3 + 1]; rgb[d + 2] = row[i * 3];
                        }
                    }
                    bmp.UnlockBits(bd);
                    return rgb;
                }
            }
        }

        // Area-average resample of top-down RGB
        public static byte[] Resample(byte[] src, int sw, int sh, int dw, int dh)
        {
            byte[] dst = new byte[dw * dh * 3];
            double fx = (double)sw / dw, fy = (double)sh / dh;
            for (int j = 0; j < dh; j++)
            {
                int ya = (int)Math.Floor(j * fy), yb = Math.Max(ya + 1, Math.Min(sh, (int)Math.Ceiling((j + 1) * fy)));
                for (int i = 0; i < dw; i++)
                {
                    int xa = (int)Math.Floor(i * fx), xb = Math.Max(xa + 1, Math.Min(sw, (int)Math.Ceiling((i + 1) * fx)));
                    long r = 0, g = 0, b = 0; int n = 0;
                    for (int y = ya; y < yb && y < sh; y++)
                        for (int x = xa; x < xb && x < sw; x++)
                        {
                            int p = (y * sw + x) * 3;
                            r += src[p]; g += src[p + 1]; b += src[p + 2]; n++;
                        }
                    if (n == 0) n = 1;
                    int d = (j * dw + i) * 3;
                    dst[d] = (byte)(r / n); dst[d + 1] = (byte)(g / n); dst[d + 2] = (byte)(b / n);
                }
            }
            return dst;
        }
    }

    // =================================================================
    // Job description (tab separated, written by the 12d macro)
    // =================================================================
    public sealed class SheetJob
    {
        public int Seq;
        public string Name, Source, OutName, Chapter;
        public int SourcePage;
        public double PaperW, PaperH, Left, Bottom, Right, Top, Scale, Rotation, Ox, Oy;
        public List<string> RasterIds = new List<string>();
        public List<string> Below = new List<string>();
        public string VplotId = "", TplotId = "";   // built from shared plots instead of its own
    }

    // A plot 12d made for several sheets: a "vplot" holds the vectors of an area at one
    // scale (no title block), a "tplot" one title block variant with a placeholder number
    public sealed class PlotSpec
    {
        public string Id, Folder, Placeholder = "";
        public double Scale, Xmin, Ymin, Xmax, Ymax, Left, Bottom, Right, Top, PaperW, PaperH;
        public PdfDoc Doc;
        public PdfDict Page;
        public ContentIndex Index;     // vplot: every drawing operation with its extent
        public byte[] Content;         // tplot: decoded page content
        public string Error;
    }

    public sealed class Job
    {
        public Dictionary<string, PlotSpec> Vplots = new Dictionary<string, PlotSpec>();
        public Dictionary<string, PlotSpec> Tplots = new Dictionary<string, PlotSpec>();
        public string Mode = "compose", OutputMode = "single", Output, Result, Log, Title = "", FallbackDir = "", Progress = "";
        public int Dpi = 150, Quality = 70;
        public double Opacity = 0.5;
        public bool Grey, Recompress, Bookmarks = true, ShrinkImages = true;
        public Dictionary<string, RasterSpec> Rasters = new Dictionary<string, RasterSpec>();
        public List<SheetJob> Sheets = new List<SheetJob>();
        public List<string> Inputs = new List<string>();

        static double D(string s) { return double.Parse(s.Trim(), CultureInfo.InvariantCulture); }
        static int I(string s) { return int.Parse(s.Trim(), CultureInfo.InvariantCulture); }

        public static Job Load(string path)
        {
            Job j = new Job();
            foreach (string raw in File.ReadAllLines(path, Encoding.UTF8))
            {
                string line = raw.TrimEnd('\r', '\n');
                if (line.Length == 0 || line.StartsWith("#")) continue;
                string[] f = line.Split('\t');
                string k = f[0].Trim().ToLowerInvariant();
                string v = f.Length > 1 ? f[1] : "";
                switch (k)
                {
                    case "mode": j.Mode = v.Trim().ToLowerInvariant(); break;
                    case "output_mode": j.OutputMode = v.Trim().ToLowerInvariant(); break;
                    case "output": j.Output = v.Trim(); break;
                    case "result": j.Result = v.Trim(); break;
                    case "log": j.Log = v.Trim(); break;
                    case "title": j.Title = v; break;
                    case "fallback_dir": j.FallbackDir = v.Trim(); break;
                    case "progress": j.Progress = v.Trim(); break;
                    case "dpi": j.Dpi = I(v); break;
                    case "quality": j.Quality = I(v); break;
                    case "opacity": j.Opacity = D(v); break;
                    case "grey": j.Grey = v.Trim() == "1"; break;
                    case "recompress": j.Recompress = v.Trim() == "1"; break;
                    case "bookmarks": j.Bookmarks = v.Trim() != "0"; break;
                    case "shrink_images": j.ShrinkImages = v.Trim() != "0"; break;
                    case "input": j.Inputs.Add(v.Trim()); break;
                    case "raster":
                        {
                            RasterSpec r = new RasterSpec();
                            r.Id = f[1].Trim(); r.Model = f[2]; r.Element = f[3]; r.Folder = f[4].Trim();
                            r.Xmin = D(f[5]); r.Ymin = D(f[6]); r.Xmax = D(f[7]); r.Ymax = D(f[8]);
                            j.Rasters[r.Id] = r;
                            break;
                        }
                    case "sheet":
                        {
                            SheetJob s = new SheetJob();
                            s.Seq = I(f[1]); s.Name = f[2]; s.Chapter = f[3]; s.Source = f[4].Trim(); s.SourcePage = I(f[5]); s.OutName = f[6];
                            s.PaperW = D(f[7]); s.PaperH = D(f[8]);
                            s.Left = D(f[9]); s.Bottom = D(f[10]); s.Right = D(f[11]); s.Top = D(f[12]);
                            s.Scale = D(f[13]); s.Rotation = D(f[14]); s.Ox = D(f[15]); s.Oy = D(f[16]);
                            if (f.Length > 17) foreach (string id in f[17].Split(',')) if (id.Trim().Length > 0) s.RasterIds.Add(id.Trim());
                            if (f.Length > 18) foreach (string m in f[18].Split('|')) if (m.Length > 0) s.Below.Add(m);
                            if (f.Length > 19) s.VplotId = f[19].Trim();
                            if (f.Length > 20) s.TplotId = f[20].Trim();
                            j.Sheets.Add(s);
                            break;
                        }
                    case "vplot":
                        {
                            PlotSpec p = new PlotSpec();
                            p.Id = f[1].Trim(); p.Folder = f[2].Trim(); p.Scale = D(f[3]);
                            p.Xmin = D(f[4]); p.Ymin = D(f[5]); p.Xmax = D(f[6]); p.Ymax = D(f[7]);
                            p.Left = D(f[8]); p.Bottom = D(f[9]); p.Right = D(f[10]); p.Top = D(f[11]);
                            p.PaperW = D(f[12]); p.PaperH = D(f[13]);
                            j.Vplots[p.Id] = p;
                            break;
                        }
                    case "tplot":
                        {
                            PlotSpec p = new PlotSpec();
                            p.Id = f[1].Trim(); p.Folder = f[2].Trim(); p.Placeholder = f.Length > 3 ? f[3].Trim() : "";
                            j.Tplots[p.Id] = p;
                            break;
                        }
                }
            }
            // the macro may plot some sheets in a second pass: pages go out in book order
            j.Sheets.Sort(delegate (SheetJob a, SheetJob b) { return a.Seq.CompareTo(b.Seq); });
            if (j.Dpi < 30) j.Dpi = 30;
            if (j.Dpi > 1200) j.Dpi = 1200;
            if (j.Quality < 10) j.Quality = 10;
            if (j.Quality > 100) j.Quality = 100;
            if (j.Opacity < 0) j.Opacity = 0;
            if (j.Opacity > 1) j.Opacity = 1;
            return j;
        }
    }

    // =================================================================
    // Book builder: pages from source PDFs into one or more outputs
    // =================================================================
    public sealed class Book
    {
        public readonly PdfWriter Wr = new PdfWriter();
        readonly int pagesNum;
        readonly List<int> pageNums = new List<int>();
        readonly List<string> titles = new List<string>();
        readonly string title;

        public Book(string title)
        {
            this.title = title;
            pagesNum = Wr.Reserve();
        }

        public int PageCount { get { return pageNums.Count; } }
        public int PagesNum { get { return pagesNum; } }

        // One reference map per source document, so what several pages share is copied once
        readonly Dictionary<PdfDoc, Dictionary<int, int>> maps = new Dictionary<PdfDoc, Dictionary<int, int>>();
        readonly Dictionary<PdfDoc, int> resNums = new Dictionary<PdfDoc, int>();

        public Dictionary<int, int> MapFor(PdfDoc src)
        {
            Dictionary<int, int> m;
            if (!maps.TryGetValue(src, out m)) { m = new Dictionary<int, int>(); maps[src] = m; }
            return m;
        }

        // A page's resources as one shared object, for the form XObjects cut from it
        public int ResourcesOf(PdfDoc src, PdfDict page)
        {
            int n;
            if (resNums.TryGetValue(src, out n)) return n;
            Dictionary<int, int> map = MapFor(src);
            MapOcgs(src, page, map);
            PdfObj r = page.Get("Resources");
            PdfObj imp = r == null ? new PdfDict() : Wr.Import(src, r, map);
            PdfRef rr = imp as PdfRef;
            n = rr != null ? rr.Num : Wr.Add(imp);
            resNums[src] = n;
            return n;
        }

        // Maps the page's OCGs onto the canonical one per layer name, so duplicates collapse
        public void MapOcgs(PdfDoc src, PdfDict page, Dictionary<int, int> map)
        {
            PdfDict res = src.Resolve(page.Get("Resources")) as PdfDict;
            PdfDict props = res == null ? null : src.Resolve(res.Get("Properties")) as PdfDict;
            if (props == null) return;
            foreach (string k in props.Keys)
            {
                PdfRef r = props.Get(k) as PdfRef;
                PdfDict g = src.Resolve(props.Get(k)) as PdfDict;
                if (r == null || g == null || g.NameOf("Type") != "OCG") continue;
                if (map.ContainsKey(r.Num)) continue;
                PdfStr nm = src.Resolve(g.Get("Name")) as PdfStr;
                string name = nm == null ? ("Layer " + r.Num) : nm.Decode();
                PdfDict usage = src.Resolve(g.Get("Usage")) as PdfDict;
                int canon = Wr.OcgFor(name, usage == null ? null : (PdfDict)Wr.Import(src, usage, map));
                map[r.Num] = canon;
            }
        }

        public void AddBookmark(string bookmark) { titles.Add(bookmark); }

        // Imports page from src; returns the new (inline) page dict and the per-source ref map.
        public PdfDict ImportPage(PdfDoc src, PdfDict page, Dictionary<int, int> map, string bookmark)
        {
            MapOcgs(src, page, map);
            PdfDict res = src.Resolve(page.Get("Resources")) as PdfDict;
            PdfDict np = new PdfDict();
            np.Set("Type", new PdfName("Page"));
            np.Set("Parent", new PdfRef(pagesNum, 0));
            foreach (string k in new string[] { "MediaBox", "CropBox", "Rotate", "UserUnit" })
                if (page.Get(k) != null) np.Set(k, Wr.Import(src, src.Resolve(page.Get(k)), map));
            PdfDict nres = new PdfDict();
            if (res != null)
                foreach (string k in res.Keys)
                {
                    PdfObj v = src.Resolve(res.Get(k));
                    PdfDict vd = v as PdfDict;
                    if (vd != null)
                    {
                        // copy sub-dictionaries inline so this page can extend them safely
                        PdfDict sub = new PdfDict();
                        foreach (string sk in vd.Keys) sub.Set(sk, Wr.Import(src, vd.Get(sk), map));
                        nres.Set(k, sub);
                    }
                    else nres.Set(k, Wr.Import(src, res.Get(k), map));
                }
            np.Set("Resources", nres);
            PdfArr contents = new PdfArr();
            foreach (PdfObj c in src.ContentRefs(page)) contents.Items.Add(Wr.Import(src, c, map));
            np.Set("Contents", contents);
            if (page.Get("Group") != null) np.Set("Group", Wr.Import(src, page.Get("Group"), map));
            titles.Add(bookmark);
            return np;
        }

        public void AddPage(PdfDict np)
        {
            pageNums.Add(Wr.Add(np));
        }

        public long Save(string path, bool bookmarks)
        {
            PdfDict pages = new PdfDict();
            pages.Set("Type", new PdfName("Pages"));
            PdfArr kids = new PdfArr();
            foreach (int n in pageNums) kids.Items.Add(new PdfRef(n, 0));
            pages.Set("Kids", kids);
            pages.Set("Count", new PdfNum(pageNums.Count.ToString(CultureInfo.InvariantCulture)));
            Wr.Set(pagesNum, pages);

            PdfDict cat = new PdfDict();
            cat.Set("Type", new PdfName("Catalog"));
            cat.Set("Pages", new PdfRef(pagesNum, 0));
            if (Wr.OcgOrder.Count > 0)
            {
                PdfDict ocp = new PdfDict();
                PdfArr all = new PdfArr();
                foreach (int n in Wr.OcgOrder) all.Items.Add(new PdfRef(n, 0));
                ocp.Set("OCGs", all);
                PdfDict dflt = new PdfDict();
                PdfArr order = new PdfArr();
                foreach (int n in Wr.OcgOrder) order.Items.Add(new PdfRef(n, 0));
                dflt.Set("Order", order);
                dflt.Set("BaseState", new PdfName("ON"));
                ocp.Set("D", dflt);
                cat.Set("OCProperties", ocp);
                cat.Set("PageMode", new PdfName(bookmarks && pageNums.Count > 1 ? "UseOutlines" : "UseOC"));
            }
            if (bookmarks && pageNums.Count > 1)
            {
                int outl = Wr.Reserve();
                int[] items = new int[pageNums.Count];
                for (int i = 0; i < items.Length; i++) items[i] = Wr.Reserve();
                for (int i = 0; i < items.Length; i++)
                {
                    PdfDict it = new PdfDict();
                    it.Set("Title", PdfStr.FromText(titles[i]));
                    it.Set("Parent", new PdfRef(outl, 0));
                    if (i > 0) it.Set("Prev", new PdfRef(items[i - 1], 0));
                    if (i < items.Length - 1) it.Set("Next", new PdfRef(items[i + 1], 0));
                    PdfArr dest = new PdfArr();
                    dest.Items.Add(new PdfRef(pageNums[i], 0));
                    dest.Items.Add(new PdfName("Fit"));
                    it.Set("Dest", dest);
                    Wr.Set(items[i], it);
                }
                PdfDict od = new PdfDict();
                od.Set("Type", new PdfName("Outlines"));
                od.Set("First", new PdfRef(items[0], 0));
                od.Set("Last", new PdfRef(items[items.Length - 1], 0));
                od.Set("Count", new PdfNum(items.Length.ToString(CultureInfo.InvariantCulture)));
                Wr.Set(outl, od);
                cat.Set("Outlines", new PdfRef(outl, 0));
                cat.Set("PageMode", new PdfName("UseOutlines"));
            }
            int catNum = Wr.Add(cat);
            PdfDict info = new PdfDict();
            if (title.Length > 0) info.Set("Title", PdfStr.FromText(title));
            info.Set("Creator", PdfStr.FromText("12d Model"));
            info.Set("Producer", PdfStr.FromText("12d FastPlot"));
            info.Set("CreationDate", PdfStr.FromText("D:" + DateTime.Now.ToString("yyyyMMddHHmmss", CultureInfo.InvariantCulture)));
            int infoNum = Wr.Add(info);
            string dir = System.IO.Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
            return Wr.Save(path, catNum, infoNum);
        }
    }

    // =================================================================
    // Runner
    // =================================================================
    public static class Runner
    {
        static StringBuilder log = new StringBuilder();
        static StringBuilder result = new StringBuilder();

        static void L(string s)
        {
            string line = DateTime.Now.ToString("HH:mm:ss.fff") + "  " + s;
            log.AppendLine(line);
            try { Console.WriteLine(line); } catch (Exception) { }
        }

        // One line for the progress window the macro opened.  12d holds the file open
        // for appending at the same time, so it is opened with full sharing.
        static string progressPath = "";

        static void P(string s)
        {
            L(s);
            if (progressPath.Length == 0) return;
            try
            {
                using (FileStream fs = new FileStream(progressPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                {
                    byte[] b = new UTF8Encoding(false).GetBytes(s + "\r\n");
                    fs.Write(b, 0, b.Length);
                }
            }
            catch (Exception) { }
        }

        public static int Run(string jobPath)
        {
            log = new StringBuilder();
            result = new StringBuilder();
            Job job = null;
            int rc = 0;
            DateTime t0 = DateTime.Now;
            try
            {
                job = Job.Load(jobPath);
                progressPath = job.Progress;
                L("job " + jobPath + " mode=" + job.Mode + " output_mode=" + job.OutputMode + " dpi=" + job.Dpi + " q=" + job.Quality +
                  " opacity=" + Fmt.N(job.Opacity) + " grey=" + job.Grey);
                if (job.Mode == "shrink") RunShrink(job);
                else if (job.Mode == "probe") RunProbe(job);
                else
                {
                    // the macro empties the work folder at the start of a run and writes the
                    // job once 12d has plotted every sheet: the gap is 12d's share of the time
                    try
                    {
                        string dir = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(jobPath));
                        double t12d = (File.GetLastWriteTime(jobPath) - Directory.GetCreationTime(dir)).TotalSeconds;
                        if (t12d >= 0) result.Append("TIMING\t12d\t" + Fmt.N(Math.Round(t12d, 1)) + "\n");
                    }
                    catch (Exception) { }
                    RunCompose(job);
                }
                result.Insert(0, "STATUS\tOK\t" + Fmt.N((DateTime.Now - t0).TotalSeconds) + "\n");
            }
            catch (Exception ex)
            {
                rc = 1;
                L("FAILED: " + ex);
                result.Insert(0, "STATUS\tERROR\t" + ex.Message.Replace('\t', ' ').Replace('\n', ' ').Replace('\r', ' ') + "\n");
            }
            finally
            {
                if (job != null && !string.IsNullOrEmpty(job.Result))
                    File.WriteAllText(job.Result, result.ToString(), new UTF8Encoding(false));
                if (job != null && !string.IsNullOrEmpty(job.Log))
                    File.AppendAllText(job.Log, log.ToString(), new UTF8Encoding(false));
            }
            return rc;
        }

        public static string LastLog() { return log.ToString(); }

        // --------------------------------------------------------------
        // probe: can every raster's tiles be found and do they fit the
        // extent 12d gave?  The macro only hides a raster from 12d's
        // plot once this says OK.
        // --------------------------------------------------------------
        static void RunProbe(Job job)
        {
            foreach (RasterSpec r in job.Rasters.Values)
            {
                try
                {
                    string folder = ResolveImageFolder(r);
                    if (folder == null) throw new Exception("no '" + r.Element + ".image' tile folder under " + r.Folder);
                    TiledImage t = TiledImage.Open(folder);
                    double gx = (r.Xmax - r.Xmin) / t.Width, gy = (r.Ymax - r.Ymin) / t.Height;
                    if (Math.Abs(gx - gy) > 0.005 * Math.Max(gx, gy))
                        throw new Exception("tiles are " + t.Width + "x" + t.Height + " px but the extent is " + Fmt.N(r.Xmax - r.Xmin) + " x " +
                                            Fmt.N(r.Ymax - r.Ymin) + " m (pixels would not be square)");
                    // read one tile to prove the format
                    int vw, vh;
                    if (t.ReadTile(0, 0, out vw, out vh) == null) throw new Exception("first tile missing");
                    result.Append("RASTER\t" + r.Id + "\tOK\t" + folder + "\t" + t.Width + "x" + t.Height + " px, " + Fmt.N(Math.Round(gx, 4)) + " m\n");
                    L("probe " + r.Id + " OK " + folder);
                }
                catch (Exception ex)
                {
                    result.Append("RASTER\t" + r.Id + "\tERROR\t" + ex.Message.Replace('\t', ' ') + "\n");
                    L("probe " + r.Id + " failed: " + ex.Message);
                }
            }
        }

        // --------------------------------------------------------------
        // compose
        // --------------------------------------------------------------
        static void RunCompose(Job job)
        {
            foreach (RasterSpec r in job.Rasters.Values)
            {
                try
                {
                    string folder = ResolveImageFolder(r);
                    if (folder == null) throw new Exception("no '" + r.Element + ".image' tile folder under " + r.Folder);
                    r.Folder = folder;
                    r.Image = TiledImage.Open(r.Folder);
                    double gx = (r.Xmax - r.Xmin) / r.Image.Width, gy = (r.Ymax - r.Ymin) / r.Image.Height;
                    L("raster " + r.Id + " '" + r.Element + "' " + r.Image.Width + "x" + r.Image.Height + " px, tiles " + r.Image.Cols + "x" + r.Image.Rows +
                      " stride " + r.Image.StrideX + ", pixel " + Fmt.N(gx) + " x " + Fmt.N(gy) + " m, " + r.Image.Overviews.Count + " overviews");
                    if (Math.Abs(gx - gy) > 0.005 * Math.Max(gx, gy))
                        L("  WARNING pixel is not square - extents may not match the tile set");
                    P("Engine: aerial '" + r.Element + "' ready (" + r.Image.Width + " x " + r.Image.Height + " px)");
                }
                catch (Exception ex)
                {
                    r.Image = null;
                    P("Engine: aerial '" + r.Element + "' cannot be read: " + ex.Message);
                    result.Append("WARN\traster\t" + r.Element + "\t" + ex.Message + "\n");
                }
            }
            bool single = job.OutputMode != "per_sheet";
            Book book = single ? new Book(job.Title) : null;
            Dictionary<string, PdfDoc> docs = new Dictionary<string, PdfDoc>(StringComparer.OrdinalIgnoreCase);
            long totalOut = 0;
            int pages = 0;
            List<string> strays = new List<string>();
            int nth = 0;
            foreach (SheetJob s in job.Sheets)
            {
                DateTime ts = DateTime.Now;
                nth++;
                P("Engine: sheet " + nth + " of " + job.Sheets.Count + "  " + s.Name);
                if (s.VplotId.Length > 0)
                {
                    // built from the shared vector and title block plots
                    Book tb = single ? book : new Book(s.Name);
                    PdfDict cp;
                    string note = ComposeFromPlots(job, s, tb, out cp);
                    if (cp == null)
                    {
                        result.Append("SHEET\t" + s.Seq + "\t" + s.Name + "\tERROR\t" + note + "\n");
                        P("Engine: sheet " + s.Name + " FAILED: " + note);
                        continue;
                    }
                    tb.AddBookmark(s.Chapter.Length > 0 ? (s.Chapter + " - " + s.Name) : s.Name);
                    tb.AddPage(cp);
                    pages++;
                    string st = note.StartsWith("AERIAL NOT ADDED") ? "WARN" : "OK";
                    string ln = "SHEET\t" + s.Seq + "\t" + s.Name + "\t" + st + "\t" + note + "\t" + Fmt.N((DateTime.Now - ts).TotalSeconds);
                    if (!single)
                    {
                        string op = System.IO.Path.Combine(job.Output, Safe(s.OutName));
                        long sz = tb.Save(op, false);
                        totalOut += sz;
                        ln += "\t" + op + "\t" + sz;
                        result.Append("FILE\t" + op + "\t" + sz + "\n");
                    }
                    result.Append(ln + "\n");
                    L("sheet " + s.Seq + " " + s.Name + " done: " + note);
                    continue;
                }
                string src = ResolveSource(s.Source);
                if (src == null && job.FallbackDir.Length > 0)
                {
                    // 12d dropped the folder from the plot stem: the vector plot is in the project folder
                    src = ResolveSource(System.IO.Path.Combine(job.FallbackDir, "FastPlot_s" + s.Seq.ToString("000", CultureInfo.InvariantCulture)));
                    if (src != null) { strays.Add(src); L("sheet " + s.Seq + " vector plot found in " + src); }
                }
                if (src == null)
                {
                    result.Append("SHEET\t" + s.Seq + "\t" + s.Name + "\tERROR\tno vector PDF found at " + s.Source + "\n");
                    L("sheet " + s.Seq + " " + s.Name + ": no vector PDF at " + s.Source);
                    continue;
                }
                PdfDoc doc;
                if (!docs.TryGetValue(src, out doc))
                {
                    doc = PdfDoc.Load(src);
                    docs[src] = doc;
                    // images 12d drew itself (logos, an aerial it kept) go to the same resolution ceiling
                    if (job.ShrinkImages) ShrinkImages(job, doc);
                }
                int pi = s.SourcePage > 0 ? s.SourcePage - 1 : 0;
                if (pi >= doc.Pages.Count)
                {
                    result.Append("SHEET\t" + s.Seq + "\t" + s.Name + "\tERROR\tsource has only " + doc.Pages.Count + " pages\n");
                    continue;
                }
                Book target = single ? book : new Book(s.Name);
                Dictionary<int, int> map = new Dictionary<int, int>();
                PdfDict np = target.ImportPage(doc, doc.Pages[pi], map, s.Chapter.Length > 0 ? (s.Chapter + " - " + s.Name) : s.Name);
                string aerialNote = AddAerial(job, s, doc, doc.Pages[pi], target, np, map);
                target.AddPage(np);
                pages++;
                string state = aerialNote.StartsWith("AERIAL NOT ADDED") ? "WARN" : "OK";
                string line = "SHEET\t" + s.Seq + "\t" + s.Name + "\t" + state + "\t" + aerialNote + "\t" + Fmt.N((DateTime.Now - ts).TotalSeconds);
                if (!single)
                {
                    string outPath = System.IO.Path.Combine(job.Output, Safe(s.OutName));
                    long sz = target.Save(outPath, false);
                    totalOut += sz;
                    line += "\t" + outPath + "\t" + sz;
                    result.Append("FILE\t" + outPath + "\t" + sz + "\n");
                }
                result.Append(line + "\n");
                L("sheet " + s.Seq + " " + s.Name + " done: " + aerialNote);
            }
            if (single && pages > 0)
            {
                P("Engine: writing " + job.Output + " ...");
                long sz = book.Save(job.Output, job.Bookmarks);
                totalOut += sz;
                result.Append("FILE\t" + job.Output + "\t" + sz + "\n");
                L("wrote " + job.Output + " " + sz + " bytes, deduped " + book.Wr.DedupedStreams + " streams (" + book.Wr.DedupedBytes + " bytes)");
                P("Wrote " + job.Output + " (" + Fmt.N(Math.Round(sz / 1048576.0, 1)) + " MB, " + pages + " pages)");
            }
            else if (!single && pages > 0)
                P("Wrote " + pages + " PDF(s) to " + job.Output + " (" + Fmt.N(Math.Round(totalOut / 1048576.0, 1)) + " MB)");
            result.Append("TOTAL\t" + pages + "\t" + totalOut + "\n");
            // vector plots that landed in the project folder are ours: do not leave them there
            foreach (string f in strays)
            {
                try { File.Delete(f); } catch (Exception ex) { L("could not delete " + f + ": " + ex.Message); }
            }
        }

        // The macro passes the tile folder itself or the project folder to search.  12d keeps a
        // raster element's tiles in "<project>.project\<element name>.image".
        static string ResolveImageFolder(RasterSpec r)
        {
            if (File.Exists(System.IO.Path.Combine(r.Folder, "imginfo.12d"))) return r.Folder;
            if (!Directory.Exists(r.Folder)) return null;
            List<string> found = new List<string>();
            foreach (string d in Directory.GetDirectories(r.Folder, "*.image", SearchOption.AllDirectories))
                if (File.Exists(System.IO.Path.Combine(d, "imginfo.12d"))) found.Add(d);
            foreach (string want in new string[] { r.Element, r.Model })
                foreach (string d in found)
                    if (string.Equals(System.IO.Path.GetFileNameWithoutExtension(d), want, StringComparison.OrdinalIgnoreCase)) return d;
            // a single unnamed match is unambiguous; otherwise pick the one whose shape fits the extents
            if (found.Count == 1) return found[0];
            double want2 = (r.Xmax - r.Xmin) / Math.Max(1e-9, r.Ymax - r.Ymin);
            string best = null; double bestErr = 0.002;
            foreach (string d in found)
            {
                try
                {
                    TiledImage t = TiledImage.Open(d);
                    double e = Math.Abs((double)t.Width / t.Height - want2) / want2;
                    if (e < bestErr) { bestErr = e; best = d; }
                }
                catch (Exception) { }
            }
            return best;
        }

        static string Safe(string name)
        {
            foreach (char c in System.IO.Path.GetInvalidFileNameChars()) name = name.Replace(c, '_');
            if (!name.EndsWith(".pdf", StringComparison.OrdinalIgnoreCase)) name += ".pdf";
            return name;
        }

        // The macro passes either the PDF itself or the folder 12d plotted into
        static string ResolveSource(string s)
        {
            if (File.Exists(s)) return s;
            if (File.Exists(s + ".pdf")) return s + ".pdf";
            if (Directory.Exists(s))
            {
                string best = null; DateTime bt = DateTime.MinValue;
                foreach (string f in Directory.GetFiles(s, "*.pdf"))
                {
                    DateTime t = File.GetLastWriteTime(f);
                    if (t > bt) { bt = t; best = f; }
                }
                return best;
            }
            string dir = System.IO.Path.GetDirectoryName(s);
            string stem = System.IO.Path.GetFileName(s);
            if (Directory.Exists(dir))
            {
                string best = null; DateTime bt = DateTime.MinValue;
                foreach (string f in Directory.GetFiles(dir, stem + "*.pdf"))
                {
                    DateTime t = File.GetLastWriteTime(f);
                    if (t > bt) { bt = t; best = f; }
                }
                return best;
            }
            return null;
        }

        // Draws the sheet's rasters into one image and inserts it as the bottom layer
        static string AddAerial(Job job, SheetJob s, PdfDoc doc, PdfDict srcPage, Book book, PdfDict np, Dictionary<int, int> map)
        {
            List<RasterSpec> rs = new List<RasterSpec>();
            foreach (string id in s.RasterIds)
            {
                RasterSpec r;
                if (job.Rasters.TryGetValue(id, out r) && r.Image != null) rs.Add(r);
            }
            // the macro hid these rasters from 12d: if none can be drawn the sheet has no aerial at all
            if (rs.Count == 0 && s.RasterIds.Count > 0) return "AERIAL NOT ADDED: its tiles could not be read";
            if (rs.Count == 0) return "no aerial";
            if (Math.Abs(s.Rotation) > 1e-9) return "rotated sheet - aerial left to 12d";

            // the page must be the sheet, unrotated, so sheet millimetres map straight onto it
            const double PT = 72.0 / 25.4;
            double[] mb = doc.Box(srcPage, "MediaBox");
            PdfNum rotN = doc.Resolve(srcPage.Get("Rotate")) as PdfNum;
            double pageRot = rotN == null ? 0 : rotN.Value;
            if (mb == null || Math.Abs(pageRot) > 0.5 ||
                Math.Abs((mb[2] - mb[0]) - s.PaperW * PT) > 0.01 * s.PaperW * PT ||
                Math.Abs((mb[3] - mb[1]) - s.PaperH * PT) > 0.01 * s.PaperH * PT)
            {
                string why = mb == null ? "no MediaBox" : ("page " + Fmt.N(mb[2] - mb[0]) + "x" + Fmt.N(mb[3] - mb[1]) + " pt, rotate " + Fmt.N(pageRot) +
                             ", sheet " + Fmt.N(s.PaperW) + "x" + Fmt.N(s.PaperH) + " mm");
                result.Append("WARN\tsheet " + s.Seq + "\t" + s.Name + "\tpage does not match the sheet (" + why + ") - aerial not added\n");
                return "AERIAL NOT ADDED: page does not match the sheet";
            }
            double ox0 = mb[0], oy0 = mb[1];

            // Placement.  12d writes one marked stream per model in view order.  With the raster
            // parked, its model still has its (empty) stream: the aerial goes there.  Otherwise it
            // goes after the last model listed as below it.  Nothing painted underneath means the
            // image can be pre-blended over white and drawn opaque, as 12d's alpha would look.
            PdfDict nres = (PdfDict)np.Get("Resources");
            PdfArr contents = (PdfArr)np.Get("Contents");
            Dictionary<string, string> resToLayer = LayerNames(doc, srcPage);
            List<PdfObj> srcContents = doc.ContentRefs(srcPage);
            List<string> layerOf = new List<string>();
            List<bool> paints = new List<bool>();
            int anchor = -1;
            for (int i = 0; i < srcContents.Count; i++)
            {
                PdfStream cs = doc.Resolve(srcContents[i]) as PdfStream;
                byte[] data = cs == null ? null : Flate.DecodeStream(doc, cs);
                string rn = data == null ? null : Content.LeadingOcResource(data);
                string layer = null;
                if (rn != null) resToLayer.TryGetValue(rn, out layer);
                layerOf.Add(layer);
                paints.Add(layer != null && HasMarks(data));
                if (anchor < 0 && layer != null && layer == rs[0].Model) anchor = i;
            }
            int insertAt = 0;
            bool paintedBelow = false;
            if (anchor >= 0)
            {
                // before the model's own stream: a raster sharing its model with vector data
                // sits under that data, as 12d draws it
                insertAt = anchor;
                for (int i = 0; i < anchor; i++) if (paints[i]) paintedBelow = true;
            }
            else if (s.Below.Count > 0)
            {
                HashSet<string> below = new HashSet<string>(s.Below);
                for (int i = 0; i < layerOf.Count; i++)
                    if (layerOf[i] != null && below.Contains(layerOf[i]) && paints[i]) { insertAt = i + 1; paintedBelow = true; }
            }
            bool preblend = !paintedBelow;
            AerialOut a = BuildAerial(job, s, book, rs, preblend, ox0, oy0, nres);
            if (a.Ops == null) return a.Note;
            int csNum = book.Wr.Add(new PdfStream(new PdfDict(), a.Ops));
            contents.Items.Insert(Math.Min(insertAt, contents.Items.Count), new PdfRef(csNum, 0));
            return a.Note + (anchor >= 0 ? " at its layer" : "") + (preblend ? "" : " (over other layers, alpha)");
        }

        sealed class AerialOut { public byte[] Ops; public string Note; }

        // --------------------------------------------------------------
        // pages built from shared plots
        // --------------------------------------------------------------

        // Loads a shared plot the first time a sheet needs it; returns an error or null
        static string LoadPlot(Job job, PlotSpec p, bool vector)
        {
            if (p.Doc != null || p.Error != null) return p.Error;
            const double PT = 72.0 / 25.4;
            try
            {
                string src = ResolveSource(p.Folder);
                if (src == null) throw new Exception("no PDF in " + p.Folder);
                p.Doc = PdfDoc.Load(src);
                p.Page = p.Doc.Pages[0];
                if (job.ShrinkImages) ShrinkImages(job, p.Doc);
                if (vector)
                {
                    double[] mb = p.Doc.Box(p.Page, "MediaBox");
                    if (mb == null || Math.Abs((mb[2] - mb[0]) - p.PaperW * PT) > 0.01 * p.PaperW * PT ||
                        Math.Abs((mb[3] - mb[1]) - p.PaperH * PT) > 0.01 * p.PaperH * PT)
                        throw new Exception("page is not the plot frame's sheet (" + (mb == null ? "no MediaBox" :
                            Fmt.N((mb[2] - mb[0]) / PT) + " x " + Fmt.N((mb[3] - mb[1]) / PT) + " mm") + ")");
                    DateTime t0 = DateTime.Now;
                    p.Index = ContentIndex.Build(p.Doc, p.Page);
                    P("Engine: area plot " + p.Id + " read - " + p.Index.Ops.Count + " drawing operations in " + p.Index.Streams +
                      " layers (" + Fmt.N(Math.Round((DateTime.Now - t0).TotalSeconds, 2)) + " s)");
                }
                else
                {
                    MemoryStream all = new MemoryStream();
                    foreach (PdfObj c in p.Doc.ContentRefs(p.Page))
                    {
                        PdfStream cs = p.Doc.Resolve(c) as PdfStream;
                        byte[] d = cs == null ? null : Flate.DecodeStream(p.Doc, cs);
                        if (d != null) all.Write(d, 0, d.Length);
                        all.WriteByte(10);
                    }
                    p.Content = all.ToArray();
                }
            }
            catch (Exception ex)
            {
                p.Error = ex.Message;
                P("Engine: " + (vector ? "area" : "title block") + " plot " + p.Id + " cannot be used: " + ex.Message);
            }
            return p.Error;
        }

        static void AddForm(Book book, PdfDict nres, PdfArr contents, string key, byte[] content, double[] bbox, int resNum, string before)
        {
            PdfDict fd = new PdfDict();
            fd.Set("Type", new PdfName("XObject"));
            fd.Set("Subtype", new PdfName("Form"));
            PdfArr bb = new PdfArr();
            foreach (double v in bbox) bb.Items.Add(new PdfNum(v));
            fd.Set("BBox", bb);
            fd.Set("Resources", new PdfRef(resNum, 0));
            fd.Set("Filter", new PdfName("FlateDecode"));
            int n = book.Wr.Add(new PdfStream(fd, Flate.Encode(content)));
            SubDict(nres, "XObject").Set(key, new PdfRef(n, 0));
            byte[] ops = Encoding.ASCII.GetBytes("q\n" + before + "/" + key + " Do\nQ\n");
            contents.Items.Add(new PdfRef(book.Wr.Add(new PdfStream(new PdfDict(), ops)), 0));
        }

        static byte[] ReplaceAll(byte[] data, byte[] find, byte[] with)
        {
            if (find.Length == 0) return data;
            MemoryStream o = new MemoryStream();
            int i = 0, last = 0;
            while (i <= data.Length - find.Length)
            {
                int k = 0;
                while (k < find.Length && data[i + k] == find[k]) k++;
                if (k == find.Length)
                {
                    o.Write(data, last, i - last);
                    o.Write(with, 0, with.Length);
                    i += find.Length;
                    last = i;
                }
                else i++;
            }
            o.Write(data, last, data.Length - last);
            return o.ToArray();
        }

        // The placeholder number in a title block plot -> this sheet's number.  12d writes text as
        // two-byte character codes (Identity-H) equal to Unicode; plain one-byte text is handled too.
        static byte[] SheetNumber(byte[] content, string placeholder, string value)
        {
            if (placeholder.Length == 0) return content;
            byte[] f2 = new byte[placeholder.Length * 2], w2 = new byte[value.Length * 2];
            for (int i = 0; i < placeholder.Length; i++) f2[i * 2 + 1] = (byte)placeholder[i];
            for (int i = 0; i < value.Length; i++) w2[i * 2 + 1] = (byte)value[i];
            byte[] r = ReplaceAll(content, f2, w2);
            return ReplaceAll(r, Encoding.ASCII.GetBytes(placeholder), Encoding.ASCII.GetBytes(value));
        }

        // One sheet: its window cut from the shared vector plot, the aerial at its layer and the
        // title block with this sheet's number.  np is null on failure (the note says why).
        static string ComposeFromPlots(Job job, SheetJob s, Book book, out PdfDict np)
        {
            np = null;
            const double PT = 72.0 / 25.4;
            PlotSpec vp, tp = null;
            if (!job.Vplots.TryGetValue(s.VplotId, out vp)) return "no vector plot " + s.VplotId;
            string e = LoadPlot(job, vp, true);
            if (e != null) return "vector plot " + vp.Id + ": " + e;
            if (s.TplotId.Length > 0)
            {
                if (!job.Tplots.TryGetValue(s.TplotId, out tp)) return "no title block plot " + s.TplotId;
                e = LoadPlot(job, tp, false);
                if (e != null) return "title block plot " + tp.Id + ": " + e;
            }
            if (Math.Abs(vp.Scale - s.Scale) > 1e-6) return "scale 1:" + Fmt.N(s.Scale) + " differs from its vector plot";
            double k = s.Scale / 1000.0;

            // the sheet's viewport, in the world and on the vector plot's page
            double vw = s.PaperW - s.Left - s.Right, vh = s.PaperH - s.Bottom - s.Top;
            double ax = s.Ox - vw / 2 * k, ay = s.Oy - vh / 2 * k;
            double[] mb = vp.Doc.Box(vp.Page, "MediaBox");
            double x0 = mb[0] + (vp.Left + (ax - vp.Xmin) / k) * PT;
            double y0 = mb[1] + (vp.Bottom + (ay - vp.Ymin) / k) * PT;
            double m = 3 * PT;
            double[] win = { x0 - m, y0 - m, x0 + vw * PT + m, y0 + vh * PT + m };
            string clip = Fmt.N(s.Left * PT) + " " + Fmt.N(s.Bottom * PT) + " " + Fmt.N(vw * PT) + " " + Fmt.N(vh * PT) + " re W n\n";
            string place = "1 0 0 1 " + Fmt.N(s.Left * PT - x0) + " " + Fmt.N(s.Bottom * PT - y0) + " cm\n";

            PdfDict nres = new PdfDict();
            PdfArr contents = new PdfArr();
            np = new PdfDict();
            np.Set("Type", new PdfName("Page"));
            np.Set("Parent", new PdfRef(book.PagesNum, 0));
            PdfArr box = new PdfArr();
            box.Items.Add(new PdfNum(0)); box.Items.Add(new PdfNum(0));
            box.Items.Add(new PdfNum(s.PaperW * PT)); box.Items.Add(new PdfNum(s.PaperH * PT));
            np.Set("MediaBox", box);
            np.Set("Resources", nres);
            np.Set("Contents", contents);

            // the aerial sits at its own layer: vectors of the layers under it go first
            string rnote;
            List<RasterSpec> rs = SheetRasters(job, s, out rnote);
            int split = 0;
            if (rs != null)
            {
                int anchor = vp.Index.StreamOfLayer(rs[0].Model);
                if (anchor >= 0) split = anchor;
                else if (s.Below.Count > 0)
                {
                    HashSet<string> below = new HashSet<string>(s.Below);
                    for (int i = 0; i < vp.Index.Streams; i++)
                        if (vp.Index.Layers[i] != null && below.Contains(vp.Index.Layers[i])) split = i + 1;
                }
            }
            int resNum = book.ResourcesOf(vp.Doc, vp.Page);
            bool paintedBelow = false, painted;
            byte[] under = split > 0 ? vp.Index.Emit(win, 0, split, out paintedBelow) : null;
            byte[] over = paintedBelow ? vp.Index.Emit(win, split, vp.Index.Streams, out painted)
                                       : vp.Index.Emit(win, 0, vp.Index.Streams, out painted);
            string formBase = "FPV" + book.PageCount;
            if (paintedBelow) AddForm(book, nres, contents, formBase + "a", under, mb, resNum, clip + place);
            string aerialNote = rnote;
            if (rs != null)
            {
                AerialOut a = BuildAerial(job, s, book, rs, !paintedBelow, 0, 0, nres);
                aerialNote = a.Note;
                if (a.Ops != null) contents.Items.Add(new PdfRef(book.Wr.Add(new PdfStream(new PdfDict(), a.Ops)), 0));
            }
            AddForm(book, nres, contents, formBase + "b", over, mb, resNum, clip + place);
            if (tp != null)
            {
                double[] tmb = tp.Doc.Box(tp.Page, "MediaBox");
                byte[] tc = SheetNumber(tp.Content, tp.Placeholder, s.Seq.ToString(CultureInfo.InvariantCulture));
                AddForm(book, nres, contents, "FPT" + book.PageCount, tc, tmb, book.ResourcesOf(tp.Doc, tp.Page), "");
            }
            return aerialNote + ", vectors " + ((under == null ? 0 : under.Length) + over.Length) / 1024 + " KB from " + vp.Id +
                   (tp != null ? ", title " + tp.Id : "");
        }

        // The sheet's usable rasters, or null with a note when there is nothing to draw
        static List<RasterSpec> SheetRasters(Job job, SheetJob s, out string note)
        {
            note = null;
            List<RasterSpec> rs = new List<RasterSpec>();
            foreach (string id in s.RasterIds)
            {
                RasterSpec r;
                if (job.Rasters.TryGetValue(id, out r) && r.Image != null) rs.Add(r);
            }
            // the macro hid these rasters from 12d: if none can be drawn the sheet has no aerial at all
            if (rs.Count == 0 && s.RasterIds.Count > 0) note = "AERIAL NOT ADDED: its tiles could not be read";
            else if (rs.Count == 0) note = "no aerial";
            else if (Math.Abs(s.Rotation) > 1e-9) note = "rotated sheet - aerial left to 12d";
            return note == null ? rs : null;
        }

        // Renders the sheet's rasters into one image; returns the operators that draw it in page
        // space (with their own clip) and adds what they use to nres.  Ops is null when nothing shows.
        static AerialOut BuildAerial(Job job, SheetJob s, Book book, List<RasterSpec> rs, bool preblend, double ox0, double oy0, PdfDict nres)
        {
            const double PT = 72.0 / 25.4;
            AerialOut res = new AerialOut();
            double k = s.Scale / 1000.0;                 // metres per paper mm
            double vpL = s.Left, vpB = s.Bottom, vpR = s.PaperW - s.Right, vpT = s.PaperH - s.Top;
            double cx = (vpL + vpR) / 2, cy = (vpB + vpT) / 2;
            // paper rect (mm) covered by any raster, clipped to the viewport
            double ax = double.MaxValue, ay = double.MaxValue, bx = double.MinValue, by = double.MinValue;
            double finest = double.MaxValue;
            foreach (RasterSpec r in rs)
            {
                ax = Math.Min(ax, cx + (r.Xmin - s.Ox) / k); bx = Math.Max(bx, cx + (r.Xmax - s.Ox) / k);
                ay = Math.Min(ay, cy + (r.Ymin - s.Oy) / k); by = Math.Max(by, cy + (r.Ymax - s.Oy) / k);
                finest = Math.Min(finest, (r.Xmax - r.Xmin) / r.Image.Width);
            }
            ax = Math.Max(ax, vpL); ay = Math.Max(ay, vpB); bx = Math.Min(bx, vpR); by = Math.Min(by, vpT);
            if (bx - ax < 0.5 || by - ay < 0.5) { res.Note = "aerial outside frame"; return res; }

            // never upsample past the raster's own resolution
            double nativeDpi = 25.4 * k / finest;
            double dpi = Math.Min(job.Dpi, nativeDpi);
            int w = Math.Max(1, (int)Math.Round((bx - ax) / 25.4 * dpi));
            int h = Math.Max(1, (int)Math.Round((by - ay) / 25.4 * dpi));
            double pxMm = (bx - ax) / w, pyMm = (by - ay) / h;
            StringBuilder rl = new StringBuilder();
            Canvas cv = new Canvas(w, h);
            float alpha = preblend ? (float)job.Opacity : 1f;

            foreach (RasterSpec r in rs)
            {
                double gx = (r.Xmax - r.Xmin) / r.Image.Width, gy = (r.Ymax - r.Ymin) / r.Image.Height;
                // canvas pixel (i, j) centre -> world -> raster pixel from the top-left
                double wx0 = s.Ox + (ax + 0.5 * pxMm - cx) * k;
                double wy0 = s.Oy + (by - 0.5 * pyMm - cy) * k;
                double u0 = (wx0 - r.Xmin) / gx, v0 = (r.Ymax - wy0) / gy;
                double du = pxMm * k / gx, dv = pyMm * k / gy;
                rl.Clear();
                RasterRender.Draw(cv, r.Image, u0, v0, du, dv, alpha, rl);
                L("  sheet " + s.Seq + " raster '" + r.Element + "' -> " + w + "x" + h + " px @ " + Fmt.N(dpi) + " dpi (native " + Fmt.N(nativeDpi) + ")");
                L(rl.ToString().TrimEnd());
            }

            byte[] rgb = new byte[w * h * 3];
            for (int i = 0; i < w * h; i++)
            {
                float a = cv.Cover[i];
                for (int c = 0; c < 3; c++)
                {
                    // canvas holds premultiplied colour; composite over white paper
                    float v = cv.Rgb[i * 3 + c] + 255f * (1f - a);
                    if (!preblend) v = a > 0 ? cv.Rgb[i * 3 + c] / a : 255f;
                    rgb[i * 3 + c] = (byte)Math.Max(0, Math.Min(255, Math.Round(v)));
                }
                if (job.Grey)
                {
                    byte y = (byte)Math.Round(0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2]);
                    rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = y;
                }
            }
            byte[] jpg = Jpeg.Encode(rgb, w, h, job.Quality);

            PdfDict im = new PdfDict();
            im.Set("Type", new PdfName("XObject"));
            im.Set("Subtype", new PdfName("Image"));
            im.Set("Width", new PdfNum(w.ToString(CultureInfo.InvariantCulture)));
            im.Set("Height", new PdfNum(h.ToString(CultureInfo.InvariantCulture)));
            im.Set("ColorSpace", new PdfName("DeviceRGB"));
            im.Set("BitsPerComponent", new PdfNum("8"));
            im.Set("Filter", new PdfName("DCTDecode"));
            if (!preblend)
            {
                // drawn over other layers: where no raster covers the image, let them show through
                bool partial = false;
                for (int i = 0; i < w * h && !partial; i++) if (cv.Cover[i] < 0.999f) partial = true;
                if (partial)
                {
                    byte[] mask = new byte[w * h];
                    for (int i = 0; i < w * h; i++) mask[i] = (byte)Math.Max(0, Math.Min(255, Math.Round(cv.Cover[i] * 255)));
                    PdfDict md = new PdfDict();
                    md.Set("Type", new PdfName("XObject"));
                    md.Set("Subtype", new PdfName("Image"));
                    md.Set("Width", new PdfNum(w.ToString(CultureInfo.InvariantCulture)));
                    md.Set("Height", new PdfNum(h.ToString(CultureInfo.InvariantCulture)));
                    md.Set("ColorSpace", new PdfName("DeviceGray"));
                    md.Set("BitsPerComponent", new PdfNum("8"));
                    md.Set("Filter", new PdfName("FlateDecode"));
                    im.Set("SMask", new PdfRef(book.Wr.Add(new PdfStream(md, Flate.Encode(mask))), 0));
                }
            }
            int imNum = book.Wr.Add(new PdfStream(im, jpg));

            string layerName = rs[0].Model.Length > 0 ? rs[0].Model : "Aerial";
            int ocg = book.Wr.OcgFor(layerName, null);
            string imKey = "FPIm" + imNum, ocKey = "FPOc" + ocg, gsKey = "FPGs" + imNum;
            SubDict(nres, "XObject").Set(imKey, new PdfRef(imNum, 0));
            SubDict(nres, "Properties").Set(ocKey, new PdfRef(ocg, 0));
            if (!preblend)
            {
                PdfDict gs = new PdfDict();
                gs.Set("Type", new PdfName("ExtGState"));
                gs.Set("ca", new PdfNum(job.Opacity)); gs.Set("CA", new PdfNum(job.Opacity));
                SubDict(nres, "ExtGState").Set(gsKey, new PdfRef(book.Wr.Add(gs), 0));
            }
            StringBuilder cs2 = new StringBuilder();
            cs2.Append("q\n");
            cs2.Append(Fmt.N(ox0 + vpL * PT) + " " + Fmt.N(oy0 + vpB * PT) + " " + Fmt.N((vpR - vpL) * PT) + " " + Fmt.N((vpT - vpB) * PT) + " re W n\n");
            cs2.Append("/OC /" + ocKey + " BDC\n");
            if (!preblend) cs2.Append("/" + gsKey + " gs\n");
            cs2.Append(Fmt.N((bx - ax) * PT) + " 0 0 " + Fmt.N((by - ay) * PT) + " " + Fmt.N(ox0 + ax * PT) + " " + Fmt.N(oy0 + ay * PT) + " cm\n");
            cs2.Append("/" + imKey + " Do\nEMC\nQ\n");
            res.Ops = Encoding.ASCII.GetBytes(cs2.ToString());
            res.Note = "aerial " + w + "x" + h + " @" + Fmt.N(Math.Round(dpi)) + "dpi " + (jpg.Length / 1024) + "KB";
            return res;
        }

        // A 12d model stream that drew nothing is just "q /OC /x BDC ... EMC Q"
        static bool HasMarks(byte[] data)
        {
            return Content.Paints(data);
        }

        static Dictionary<string, string> LayerNames(PdfDoc doc, PdfDict page)
        {
            Dictionary<string, string> m = new Dictionary<string, string>();
            PdfDict res = doc.Resolve(page.Get("Resources")) as PdfDict;
            PdfDict props = res == null ? null : doc.Resolve(res.Get("Properties")) as PdfDict;
            if (props == null) return m;
            foreach (string k in props.Keys)
            {
                PdfDict g = doc.Resolve(props.Get(k)) as PdfDict;
                PdfStr nm = g == null ? null : doc.Resolve(g.Get("Name")) as PdfStr;
                if (nm != null) m[k] = nm.Decode();
            }
            return m;
        }

        static PdfDict SubDict(PdfDict d, string key)
        {
            PdfDict s = d.Get(key) as PdfDict;
            if (s == null) { s = new PdfDict(); d.Set(key, s); }
            return s;
        }

        // --------------------------------------------------------------
        // shrink
        // --------------------------------------------------------------
        static void RunShrink(Job job)
        {
            bool single = job.OutputMode != "per_sheet";
            Book book = single ? new Book(job.Title) : null;
            long totalIn = 0, totalOut = 0;
            int pages = 0, changed = 0;
            foreach (string input in job.Inputs)
            {
                PdfDoc doc = PdfDoc.Load(input);
                totalIn += doc.Bytes.Length;
                int ch = ShrinkImages(job, doc);
                changed += ch;
                Book target = single ? book : new Book(System.IO.Path.GetFileNameWithoutExtension(input));
                for (int i = 0; i < doc.Pages.Count; i++)
                {
                    Dictionary<int, int> map = new Dictionary<int, int>();
                    string bm = System.IO.Path.GetFileNameWithoutExtension(input) + (doc.Pages.Count > 1 ? (" p" + (i + 1)) : "");
                    target.AddPage(target.ImportPage(doc, doc.Pages[i], map, bm));
                    pages++;
                }
                if (!single)
                {
                    string outPath = System.IO.Path.Combine(job.Output, System.IO.Path.GetFileName(input));
                    if (string.Equals(System.IO.Path.GetFullPath(outPath), System.IO.Path.GetFullPath(input), StringComparison.OrdinalIgnoreCase))
                        outPath = System.IO.Path.Combine(job.Output, System.IO.Path.GetFileNameWithoutExtension(input) + "_small.pdf");
                    long sz = target.Save(outPath, false);
                    totalOut += sz;
                    result.Append("FILE\t" + outPath + "\t" + sz + "\n");
                }
                result.Append("INPUT\t" + input + "\t" + doc.Bytes.Length + "\t" + doc.Pages.Count + " pages\t" + ch + " images resampled\n");
            }
            if (single && pages > 0)
            {
                long sz = book.Save(job.Output, job.Bookmarks);
                totalOut += sz;
                result.Append("FILE\t" + job.Output + "\t" + sz + "\n");
            }
            result.Append("TOTAL\t" + pages + "\t" + totalOut + "\t" + totalIn + "\t" + changed + "\n");
        }

        // Replaces over-resolved images in doc.Objects in place; returns how many changed
        static int ShrinkImages(Job job, PdfDoc doc)
        {
            // largest placement of every image object across all pages
            Dictionary<int, double[]> use = new Dictionary<int, double[]>();
            foreach (PdfDict page in doc.Pages)
            {
                PdfDict res = doc.Resolve(page.Get("Resources")) as PdfDict;
                PdfDict xo = res == null ? null : doc.Resolve(res.Get("XObject")) as PdfDict;
                if (xo == null) continue;
                List<byte> all = new List<byte>();
                foreach (PdfObj c in doc.ContentRefs(page))
                {
                    PdfStream cs = doc.Resolve(c) as PdfStream;
                    byte[] d = cs == null ? null : Flate.DecodeStream(doc, cs);
                    if (d != null) { all.AddRange(d); all.Add(10); }
                }
                foreach (KeyValuePair<string, double[]> kv in Content.ImagePlacements(all.ToArray()))
                {
                    PdfRef r = xo.Get(kv.Key) as PdfRef;
                    if (r == null) continue;
                    double[] prev;
                    if (!use.TryGetValue(r.Num, out prev) || kv.Value[0] * kv.Value[1] > prev[0] * prev[1]) use[r.Num] = kv.Value;
                }
            }
            int changed = 0;
            foreach (KeyValuePair<int, double[]> kv in use)
            {
                PdfStream s = doc.Resolve(new PdfRef(kv.Key, 0)) as PdfStream;
                if (s == null || s.Dict.NameOf("Subtype") != "Image") continue;
                if (s.Dict.Get("SMask") != null || s.Dict.Get("Mask") != null || s.Dict.Get("ImageMask") != null || s.Dict.Get("Decode") != null) continue;
                PdfNum bpcN = doc.Resolve(s.Dict.Get("BitsPerComponent")) as PdfNum;
                if (bpcN == null || bpcN.Value != 8) continue;
                string csName = s.Dict.NameOf("ColorSpace");
                if (csName != "DeviceRGB" && csName != "DeviceGray") continue;
                int w = (int)((PdfNum)doc.Resolve(s.Dict.Get("Width"))).Value;
                int h = (int)((PdfNum)doc.Resolve(s.Dict.Get("Height"))).Value;
                double dpiX = w / (kv.Value[0] / 72.0), dpiY = h / (kv.Value[1] / 72.0);
                double eff = Math.Max(dpiX, dpiY);
                bool over = eff > job.Dpi * 1.15;
                if (!over && !job.Recompress) continue;
                string filter = s.Dict.NameOf("Filter");
                byte[] rgb = null;
                int sw = w, sh = h;
                try
                {
                    if (filter == "DCTDecode") rgb = Jpeg.Decode(s.Data, out sw, out sh);
                    else if (filter == "FlateDecode" && s.Dict.Get("DecodeParms") == null)
                    {
                        byte[] raw = Flate.Decode(s.Data);
                        if (csName == "DeviceRGB" && raw.Length >= w * h * 3) rgb = raw;
                        else if (csName == "DeviceGray" && raw.Length >= w * h)
                        {
                            rgb = new byte[w * h * 3];
                            for (int i = 0; i < w * h; i++) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = raw[i];
                        }
                    }
                }
                catch (Exception ex) { L("  image " + kv.Key + " decode failed: " + ex.Message); rgb = null; }
                if (rgb == null) continue;
                int dw = sw, dh = sh;
                if (over)
                {
                    double f = job.Dpi / eff;
                    dw = Math.Max(1, (int)Math.Round(sw * f));
                    dh = Math.Max(1, (int)Math.Round(sh * f));
                    rgb = Jpeg.Resample(rgb, sw, sh, dw, dh);
                }
                if (job.Grey)
                    for (int i = 0; i < dw * dh; i++)
                    {
                        byte y = (byte)Math.Round(0.299 * rgb[i * 3] + 0.587 * rgb[i * 3 + 1] + 0.114 * rgb[i * 3 + 2]);
                        rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = y;
                    }
                byte[] jpg = Jpeg.Encode(rgb, dw, dh, job.Quality);
                if (!over && jpg.Length > s.Data.Length * 0.9) continue;   // recompress only when it really pays
                PdfDict nd = new PdfDict();
                foreach (string key in s.Dict.Keys)
                    if (key != "Filter" && key != "DecodeParms" && key != "Length" && key != "Width" && key != "Height" && key != "ColorSpace") nd.Set(key, s.Dict.Get(key));
                nd.Set("Width", new PdfNum(dw.ToString(CultureInfo.InvariantCulture)));
                nd.Set("Height", new PdfNum(dh.ToString(CultureInfo.InvariantCulture)));
                nd.Set("ColorSpace", new PdfName("DeviceRGB"));
                nd.Set("Filter", new PdfName("DCTDecode"));
                doc.Objects[kv.Key] = new PdfStream(nd, jpg);
                changed++;
            }
            L("  " + System.IO.Path.GetFileName(doc.Path) + ": " + use.Count + " placed images, " + changed + " resampled");
            return changed;
        }
    }
}
