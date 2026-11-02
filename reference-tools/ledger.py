#!/usr/bin/env python3
"""Reference-side coverage ledger for the clean-room No One Lives Forever spec.

This tool lives in the reference repository only.  It inventories everything
whose behaviour the new engine must cover and partitions the inventory into
work units for spec writers.

  ledger.py build [--jobs N] [--level-survey out.txt] [--no-cache]
      Parse the reference and write the inventory:
        <staging>/inventory/all.json       every item
        <staging>/inventory/units.json     the work units
        <staging>/inventory/<unit>.json    the items of one unit
  ledger.py check
      Verify that every item of all.json has an entry in some
      <staging>/<domain>/map/<unit>.json ({"spec": [...]} or
      {"obsolete": "<reason>"}); print per-unit uncovered counts as JSON and
      exit 1 when anything is uncovered.
  ledger.py stats
      Print totals of the current inventory.

Inventory sources
  * game classes and every function/method defined in src/game/{ClientShellDLL,
    ObjectDLL,Shared} -- clang's JSON AST (clang++ -Xclang -ast-dump=json
    -fsyntax-only) with the flags of out/native-release/compile_commands.json;
  * every function in src/engine/{lithtech,server,d3d} with the retail address
    the code cites, and every function of the renderer core src/modern/render
    (mr_*.cpp and its headers; the gl/ and d3d9/ device backends are platform
    glue and are not inventoried) -- clang JSON AST as well.  Files that have no
    compile command (lithtech.exe, d3d.ren and renderer core are VC6/ICL or
    Windows-only) are parsed with synthesised flags (-fms-extensions,
    -fms-compatibility, the include paths of their build, generated stub
    headers for missing Windows/DirectX headers); clang keeps invalid
    declarations in the AST, so definitions survive missing types.
    A regex scanner runs over every file as a cross-check: a definition it finds
    that the AST does not have (code in a preprocessor branch the native
    configuration does not compile, or a definition clang could not recover)
    is added with "parser": "regex".
  * every level object class used by any world (OBJ lines of the light survey;
    field 3 is the class name) with the number of worlds using it -- cached in
    reference-tools/data/level_classes.json so a rebuild does not need the
    survey again;
  * every Bute attribute file (ATTRIBUTES/*.txt in the retail .rez archives of
    out/native-data-resources, plus custom/ATTRIBUTES);
  * console variables and console commands registered by game and engine
    (names and defaults), and the two default cfg files.
"""

import argparse
import bisect
import collections
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent
REPO = TOOL_DIR.parent
SRC = REPO / "src"
STAGING = Path(os.environ.get("NOLF_SPEC_STAGING", "/home/dmitriy/sources/nolf-spec-staging"))
INV = STAGING / "inventory"
CACHE = TOOL_DIR / ".cache"
DATA_DIR = TOOL_DIR / "data"
CC_JSON = REPO / "out/native-release/compile_commands.json"
GAME_DATA = REPO / "out/native-data-resources"
LEVEL_SURVEY = Path("/tmp/claude-1000/-home-dmitriy-sources-nolfsrc/"
                    "6d7ff391-8863-4e6a-bbdb-3c4510ecd02c/scratchpad/lightsurvey/out.txt")
LEVEL_CACHE = DATA_DIR / "level_classes.json"
CLANG = os.environ.get("CLANGXX", "clang++")
EXTRACT_VERSION = "6"

DOMAINS = ["FMT", "SIM", "NET", "SVC", "RWORLD", "ROBJ", "PLAYER", "WEAPON",
           "AI", "AIB", "WORLD", "CUI"]

# (id prefix, directory under src, id base directory under src, recurse)
ROOTS = [
    ("game", "game/ClientShellDLL", "game", True),
    ("game", "game/ObjectDLL", "game", True),
    ("game", "game/Shared", "game", True),
    ("eng", "engine/lithtech", "engine", False),
    ("eng", "engine/server", "engine", False),
    ("eng", "engine/d3d", "engine", False),
    ("ren", "modern/render", "modern", False),
]
CODE_EXT = {".cpp", ".h", ".c", ".hpp", ".inl", ".cc"}
GRAMMAR_EXT = {".y", ".l"}
SKIP_DIRS = {"link", "Release", "Debug", "gl", "d3d9", "docs"}

ADDR_RE = re.compile(r"0x(?:1[0-9a-fA-F]{7}|00[45][0-9a-fA-F]{5})\b")


def rel(p):
    return str(Path(p).resolve().relative_to(REPO))


def root_of(path):
    """(prefix, root dir, id base) of an inventoried file, or None."""
    p = Path(path)
    try:
        r = p.resolve().relative_to(SRC)
    except ValueError:
        return None
    parts = r.parts
    for prefix, d, base, recurse in ROOTS:
        dparts = tuple(d.split("/"))
        if parts[:len(dparts)] == dparts:
            sub = parts[len(dparts):]
            if len(sub) == 0:
                return None
            if len(sub) > 1 and (not recurse or sub[0] in SKIP_DIRS):
                return None
            if any(s in SKIP_DIRS for s in sub[:-1]):
                return None
            if p.suffix.lower() not in CODE_EXT | GRAMMAR_EXT:
                return None
            return prefix, d, base
    return None


def inventoried_files():
    out = []
    for prefix, d, base, recurse in ROOTS:
        top = SRC / d
        it = top.rglob("*") if recurse else top.glob("*")
        for f in sorted(it):
            if f.is_file() and root_of(f):
                out.append(f.resolve())
    return sorted(set(out))


def id_path(path):
    prefix, d, base = root_of(path)
    return prefix, str(Path(path).resolve().relative_to(SRC / base))


# --------------------------------------------------------------------------
# clang JSON AST extraction
# --------------------------------------------------------------------------

WIN_STUB = r"""
#pragma once
#ifndef NOLF_LEDGER_WINSTUB
#define NOLF_LEDGER_WINSTUB
typedef unsigned long DWORD; typedef int BOOL; typedef unsigned char BYTE;
typedef unsigned short WORD; typedef unsigned int UINT; typedef long LONG;
typedef unsigned long ULONG; typedef long HRESULT; typedef void *HANDLE;
typedef void *HWND, *HINSTANCE, *HMODULE, *HDC, *HGLRC, *HBITMAP, *HBRUSH,
    *HFONT, *HICON, *HCURSOR, *HMENU, *HKEY, *HPALETTE, *HGDIOBJ, *HRGN,
    *HMONITOR, *HGLOBAL, *HLOCAL, *HPEN, *HACCEL, *HMMIO, *HWAVEOUT, *HMIDIOUT;
typedef char CHAR; typedef unsigned char UCHAR; typedef short SHORT;
typedef unsigned short USHORT; typedef char *LPSTR, *PSTR; typedef const char *LPCSTR, *PCSTR;
typedef char TCHAR; typedef char *LPTSTR; typedef const char *LPCTSTR;
typedef unsigned short WCHAR; typedef WCHAR *LPWSTR; typedef const WCHAR *LPCWSTR;
typedef void *LPVOID, *PVOID; typedef const void *LPCVOID; typedef DWORD *LPDWORD, *PDWORD;
typedef BYTE *LPBYTE, *PBYTE; typedef WORD *LPWORD; typedef LONG *LPLONG; typedef BOOL *LPBOOL;
typedef long LPARAM; typedef unsigned int WPARAM; typedef long LRESULT;
typedef float FLOAT; typedef double DOUBLE; typedef long long LONGLONG, __int64_t_stub;
typedef unsigned long long ULONGLONG, DWORDLONG; typedef unsigned int UINT_PTR, *PUINT;
typedef long LONG_PTR; typedef unsigned long ULONG_PTR, DWORD_PTR; typedef unsigned short ATOM;
typedef DWORD COLORREF; typedef int INT; typedef unsigned char BOOLEAN;
typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *LPRECT, *PRECT;
typedef const RECT *LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *LPPOINT, *PPOINT;
typedef struct tagSIZE { LONG cx, cy; } SIZE, *LPSIZE;
typedef union _LARGE_INTEGER { struct { DWORD LowPart; LONG HighPart; } u; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct _GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; } GUID, IID, CLSID;
typedef const GUID &REFGUID, &REFIID, &REFCLSID;
typedef struct tagMSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct tagPALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY, *LPPALETTEENTRY;
typedef struct _RGNDATA { int dummy; } RGNDATA, *LPRGNDATA;
typedef struct _CRITICAL_SECTION { int dummy[6]; } CRITICAL_SECTION, *LPCRITICAL_SECTION;
typedef struct _WIN32_FIND_DATA { DWORD dwFileAttributes; char cFileName[260]; } WIN32_FIND_DATA, WIN32_FIND_DATAA;
typedef struct tWAVEFORMATEX { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX, *LPWAVEFORMATEX;
typedef struct _SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct _OVERLAPPED { int dummy; } OVERLAPPED, *LPOVERLAPPED;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
struct IUnknown { virtual HRESULT QueryInterface(REFIID, void **) = 0; virtual ULONG AddRef() = 0; virtual ULONG Release() = 0; };
typedef IUnknown *LPUNKNOWN;
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define PASCAL
#define FAR
#define NEAR
#define far
#define near
#define CONST const
#define IN
#define OUT
#define OPTIONAL
#define STDMETHODCALLTYPE
#define STDAPI extern "C" HRESULT
#define STDMETHOD(m) virtual HRESULT m
#define STDMETHOD_(t, m) virtual t m
#define PURE = 0
#define THIS_
#define THIS void
#define DECLARE_INTERFACE(i) struct i
#define DECLARE_INTERFACE_(i, b) struct i : public b
#define MAKEFOURCC(a, b, c, d) ((DWORD)(a) | ((DWORD)(b) << 8) | ((DWORD)(c) << 16) | ((DWORD)(d) << 24))
#define DEFINE_GUID(n, ...) extern const GUID n
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define S_OK 0
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(h) ((HRESULT)(h) >= 0)
#define FAILED(h) ((HRESULT)(h) < 0)
#define LOWORD(l) ((WORD)(l))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define __try try
#define __except(x) catch (...)
#define __finally
#endif
"""

WIN_HEADER_RE = re.compile(
    r"^(windows|winbase|windef|winuser|wingdi|winnt|winreg|winsock2?|mmsystem|mmreg|"
    r"ddraw|d3d\w*|dinput|dsound|dmusic\w*|dmusi\w*|dmdls|dls\w*|objbase|ole2|objidl|"
    r"unknwn|commctrl|commdlg|shlobj|shellapi|process|direct|io|conio|malloc|new|"
    r"tchar|crtdbg|mbstring|dos|vfw|msacm|digitalv|imm|wtypes|basetsd|mss|smack|bink|"
    r"atlbase|afx\w*|excpt|winerror|ras|wininet|tlhelp32|richedit|wsipx|nspapi|"
    r"winsvc|winver|lmcons|dplay\w*|dxerr\w*|ddeml|vfwmsgs|ks\w*|regstr|cpl|winioctl|"
    r"setupapi|multimon|zmouse|mapi)\.h$", re.I)


def stub_dir():
    d = CACHE / "stubs"
    d.mkdir(parents=True, exist_ok=True)
    common = d / "_nolf_ledger_winstub.h"
    if not common.exists() or common.read_text() != WIN_STUB:
        common.write_text(WIN_STUB)
    return d


def make_stub(name):
    d = stub_dir()
    target = (d / name).resolve()
    if not str(target).startswith(str(d.resolve())):
        return False
    target.parent.mkdir(parents=True, exist_ok=True)
    base = Path(name).name
    body = '#pragma once\n'
    if WIN_HEADER_RE.match(base):
        body += '#include "_nolf_ledger_winstub.h"\n'
    if not target.exists():
        tmp = target.with_suffix(target.suffix + ".%d" % os.getpid())
        tmp.write_text(body)
        os.replace(tmp, target)
    return True


def load_cc():
    cc = json.loads(CC_JSON.read_text())
    by_file = {}
    for e in cc:
        f = str(Path(e["file"]).resolve())
        by_file.setdefault(f, e)
    return by_file


def cc_to_clang(entry):
    toks = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    out = []
    skip = False
    for t in toks[1:]:
        if skip:
            skip = False
            continue
        if t == "-o":
            skip = True
            continue
        if t == "-c" or t.startswith(("-O", "-W", "-fPIC", "-fvisibility", "-g", "-M", "-fno-strict-aliasing")):
            continue
        if str(Path(entry["directory"], t).resolve()) == str(Path(entry["file"]).resolve()):
            continue
        out.append(t)
    return out, entry["directory"]


def synth_flags(f, cc_by_file):
    """Flags for a translation unit that has no compile command."""
    f = Path(f)
    d = f.parent
    # a sibling with a compile command gives the flags of its target
    for sib in sorted(d.glob("*.cpp")):
        e = cc_by_file.get(str(sib.resolve()))
        if e:
            args, cwd = cc_to_clang(e)
            if "engine/lithtech" not in str(d):
                return args + ["-I" + str(d), "-idirafter", str(stub_dir())], cwd, "sibling"
            break
    S = str(SRC)
    inc = ["-I" + str(d), "-I" + S + "/engine/server", "-I" + S + "/engine/lithtech",
           "-I" + S + "/LT2/sdk/inc", "-I" + S + "/LT2/lithshared/incs",
           "-I" + S + "/LT2/lithshared/stdlith", "-I" + S + "/LT2/lithshared/lith",
           "-I" + S + "/LT2/lithshared/mfcstub", "-I" + S + "/LT2/lithshared/butemgr",
           "-I" + S, "-I" + S + "/modern", "-I" + S + "/modern/render", "-I" + S + "/engine/d3d",
           "-I" + S + "/nolfsrc", "-I" + str(REPO / "out/native-release/generated")]
    defs = ["-DWIN32", "-D_WINDOWS", "-DNDEBUG", "-DNO_PRAGMA_LIBS", "-D_M_IX86=500",
            "-D__cdecl=", "-D__stdcall=", "-D__fastcall=", "-D_cdecl=", "-D_stdcall="]
    std = "-std=gnu++17"
    if "engine/server" in str(d):
        defs.append("-DLT_SERVER_BUILD")
    return [std, "-fms-extensions", "-fms-compatibility", "-fdelayed-template-parsing",
            "-fno-builtin"] + defs + inc + ["-idirafter", str(stub_dir())], str(d), "synthesised"


INTEREST_ROOTS = [str(SRC / d) + "/" for _, d, _, _ in ROOTS]
FUNC_KINDS = {"FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl",
              "CXXConversionDecl"}
BODY_KINDS = {"CompoundStmt", "CXXTryStmt"}


def interesting_path(p):
    return p is not None and any(p.startswith(r) for r in INTEREST_ROOTS) and root_of(p) is not None


def _real_file_before(buf, lo, hi):
    """Last non-includedFrom "file" value in buf[lo:hi], or None."""
    pat = b'"file": "'
    end = hi
    while True:
        m = buf.rfind(pat, lo, end)
        if m < 0:
            return None
        ls = buf.rfind(b"\n", 0, m)
        ps = buf.rfind(b"\n", 0, ls)
        if buf[ps + 1:ls].rstrip().endswith(b'"includedFrom": {'):
            end = m
            continue
        q = buf.find(b'"', m + len(pat))
        while buf[q - 1:q] == b"\\":
            q = buf.find(b'"', q + 1)
        return json.loads(buf[m + len(pat) - 1:q + 1])


def _chunk_mentions_interest(buf, lo, hi):
    pat = b'"file": "' + str(SRC).encode() + b"/"
    pos = lo
    while True:
        m = buf.find(pat, pos, hi)
        if m < 0:
            return False
        ls = buf.rfind(b"\n", 0, m)
        ps = buf.rfind(b"\n", 0, ls)
        if not buf[ps + 1:ls].rstrip().endswith(b'"includedFrom": {'):
            q = buf.find(b'"', m + 9)
            path = buf[m + 9:q].decode("utf-8", "replace")
            if interesting_path(path):
                return True
        pos = m + 1


def _resolve_loc(loc, cur):
    """(expansion file, offset, spelling file or None when not a macro)."""
    if not loc:
        return None, None, None
    if "spellingLoc" in loc or "expansionLoc" in loc:
        f = cur
        sp = loc.get("spellingLoc") or {}
        f = sp.get("file", f)
        spf = f
        ex = loc.get("expansionLoc") or {}
        f = ex.get("file", f)
        return f, ex.get("offset"), spf or "?"
    return loc.get("file", cur), loc.get("offset"), None


def _walk_chunk(obj, cur_file, out):
    """Document-order walk of one top-level declaration, tracking clang's
    'file only when it changes' location encoding.  Returns the last file."""
    stack = [(obj, (), False)]
    while stack:
        v, ctx, skip = stack.pop()
        if isinstance(v, list):
            for x in reversed(v):
                if isinstance(x, (dict, list)):
                    stack.append((x, ctx, skip))
            continue
        if "file" in v and isinstance(v["file"], str):
            cur_file = v["file"]
        kind = v.get("kind")
        child_ctx, child_skip = ctx, skip
        if kind and kind.endswith("Decl") and "loc" in v:
            f, off, macro = _resolve_loc(v.get("loc"), cur_file)
            implicit = v.get("isImplicit", False)
            if kind in FUNC_KINDS:
                inner = v.get("inner") or ()
                has_body = any(isinstance(c, dict) and c.get("kind") in BODY_KINDS for c in inner)
                if (has_body and not implicit and not skip and interesting_path(f)
                        and off is not None and v.get("name")):
                    out.append({"k": "method" if (kind != "FunctionDecl") else "function",
                                "name": v.get("name"), "ctx": [c for c in ctx if c],
                                "file": f, "off": off, "macro": bool(macro), "spelling": macro,
                                "mangled": v.get("mangledName"),
                                "type": (v.get("type") or {}).get("qualType", ""),
                                "ckind": kind, "virtual": v.get("virtual", False)})
                child_ctx = ctx + ("<fn>",)
                child_skip = True
            elif kind == "CXXRecordDecl":
                name = v.get("name")
                if (v.get("completeDefinition") and name and not implicit and not skip
                        and v.get("tagUsed") in ("class", "struct") and interesting_path(f)
                        and off is not None):
                    out.append({"k": v.get("tagUsed"), "name": name, "ctx": [c for c in ctx if c],
                                "file": f, "off": off, "macro": bool(macro), "spelling": macro})
                if name and not implicit:
                    child_ctx = ctx + (name,)
            elif kind in ("ClassTemplateSpecializationDecl",):
                child_skip = True
                child_ctx = ctx + (v.get("name") or "",)
            elif kind == "ClassTemplatePartialSpecializationDecl":
                child_ctx = ctx + (v.get("name") or "",)
            elif kind == "NamespaceDecl":
                child_ctx = ctx + (v.get("name") or "",)
        # children in document order (includedFrom carries no location state)
        items = [x for k, x in v.items() if k != "includedFrom" and isinstance(x, (dict, list))]
        for x in reversed(items):
            stack.append((x, child_ctx, child_skip))
    return cur_file


def scan_ast_dump(buf):
    """Extract raw declarations of interest from one TU's JSON AST (bytes)."""
    out = []
    pos = buf.find(b'\n  "inner": [\n')
    if pos < 0:
        return out
    cur_file = None
    open_pat, close_pat = b"\n    {\n", b"\n    }"
    while True:
        s = buf.find(open_pat, pos)
        if s < 0:
            break
        e = buf.find(close_pat, s)
        if e < 0:
            break
        e_end = e + len(close_pat)
        if interesting_path(cur_file) or _chunk_mentions_interest(buf, s, e_end):
            obj = json.loads(buf[s + 1:e_end])
            cur_file = _walk_chunk(obj, cur_file, out)
        else:
            f = _real_file_before(buf, s, e_end)
            if f is not None:
                cur_file = f
        pos = e_end
    return out


def _tu_job(args):
    path, flags, cwd, how, cache_key, use_cache = args
    cache_file = CACHE / "tu" / (cache_key + ".json")
    if use_cache and cache_file.exists():
        return json.loads(cache_file.read_text())
    cmd = [CLANG, "-fsyntax-only", "-ferror-limit=0", "-w", "-Xclang", "-ast-dump=json"] + flags + [path]
    res = None
    stubs_made = []
    for _ in range(60):
        with tempfile.TemporaryFile() as tf:
            p = subprocess.run(cmd, cwd=cwd, stdout=tf, stderr=subprocess.PIPE)
            err = p.stderr.decode("utf-8", "replace")
            m = re.search(r"fatal error: '([^']+)' file not found", err)
            if m and how != "compile_commands":
                if make_stub(m.group(1)):
                    stubs_made.append(m.group(1))
                    continue
            tf.seek(0)
            buf = tf.read()
        errors = len(re.findall(r"\berror:", err))
        fatal = re.findall(r"fatal error: [^\n]*", err)
        decls = scan_ast_dump(buf) if buf else []
        res = {"file": path, "how": how, "errors": errors, "fatal": fatal[:3],
               "stubs": stubs_made, "decls": decls}
        break
    if res is None:
        res = {"file": path, "how": how, "errors": -1, "fatal": ["stub loop"], "stubs": stubs_made, "decls": []}
    cache_file.parent.mkdir(parents=True, exist_ok=True)
    tmp = cache_file.with_suffix(".tmp%d" % os.getpid())
    tmp.write_text(json.dumps(res))
    os.replace(tmp, cache_file)
    return res


def tree_fingerprint():
    h = hashlib.sha256(EXTRACT_VERSION.encode())
    for top in ["game", "engine", "LT2", "lithshared", "modern", "nolfsrc"]:
        for f in sorted((SRC / top).rglob("*")):
            if f.is_file() and f.suffix.lower() in CODE_EXT:
                st = f.stat()
                h.update(("%s %d %d\n" % (f, st.st_size, int(st.st_mtime))).encode())
    return h.hexdigest()[:16]


def run_clang(files, jobs, use_cache, log):
    cc_by_file = load_cc()
    fp = tree_fingerprint()
    tasks = []
    for f in files:
        if f.suffix.lower() not in (".cpp", ".c", ".cc"):
            continue
        e = cc_by_file.get(str(f))
        if e:
            flags, cwd = cc_to_clang(e)
            how = "compile_commands"
        else:
            flags, cwd, how = synth_flags(f, cc_by_file)
        key = hashlib.sha256(json.dumps([fp, str(f), flags]).encode()).hexdigest()[:24]
        tasks.append((str(f), flags, cwd, how, key, use_cache))
    results = []
    with cf.ProcessPoolExecutor(max_workers=jobs) as ex:
        futs = {ex.submit(_tu_job, t): t[0] for t in tasks}
        done = 0
        for fu in cf.as_completed(futs):
            r = fu.result()
            results.append(r)
            done += 1
            if done % 25 == 0 or done == len(tasks):
                log("  clang: %d/%d translation units" % (done, len(tasks)))
    return results


# --------------------------------------------------------------------------
# regex cross-check scanner
# --------------------------------------------------------------------------

_LEX_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
_KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "do", "else", "new",
             "delete", "defined", "__asm", "_asm", "asm", "case", "goto", "throw", "typedef",
             "using", "static_assert", "alignof", "decltype", "__declspec", "__attribute__",
             "template", "typename", "enum", "class", "struct", "union", "namespace"}
_TYPE_PART_RE = re.compile(r"^[\w\s\*&:<>,~\[\]]*$")
_NAME_RE = re.compile(
    r"((?:[A-Za-z_]\w*\s*(?:<[^<>;{}()]*(?:<[^<>;{}()]*>[^<>;{}()]*)*>)?\s*::\s*)*"
    r"(?:~\s*)?(?:operator\s*(?:\(\s*\)|\[\s*\]|new\s*\[\s*\]|delete\s*\[\s*\]|new|delete|"
    r"[^\w\s(]+|\s+[A-Za-z_][\w\s\*&:]*)|[A-Za-z_]\w*))\s*$")
_TAIL_RE = re.compile(
    r"^\s*(?:const\b\s*)?(?:volatile\b\s*)?(?:throw\s*\([^)]*\)\s*)?(?:noexcept\b\s*)?"
    r"(?:(?:override|final)\b\s*)*(?::(?!:).*)?$", re.S)
_CLASS_HEAD_RE = re.compile(
    r"^(?:template\s*<.*>\s*)?(?:typedef\s+)?(class|struct|union)\s+"
    r"(?:__declspec\s*\([^)]*\)\s*)?(?:[A-Z_]+_API\s+)?(\w+)?\s*(?:final\s*)?(?::(?!:)[^{]*)?$", re.S)
_NS_HEAD_RE = re.compile(r"^(?:inline\s+)?namespace\b\s*(\w*)\s*$")
_EXTERN_RE = re.compile(r'^extern\s*"\s*"\s*$')
_ACCESS_RE = re.compile(r"^\s*(?:(?:public|private|protected)\s*:(?!:)\s*)+")


def blank_code(text):
    """Comments, literals and preprocessor lines blanked (offsets kept);
    #if 0 regions blanked."""
    def rep(m):
        s = m.group(0)
        if s[0] in "\"'":
            return s[0] + re.sub(r"[^\n]", " ", s[1:-1]) + s[-1]
        return re.sub(r"[^\n]", " ", s)
    t = _LEX_RE.sub(rep, text)
    lines = t.split("\n")
    out = []
    dead = 0
    cont = False
    for ln in lines:
        st = ln.strip()
        if cont:
            cont = ln.rstrip().endswith("\\")
            out.append(" " * len(ln))
            continue
        if st.startswith("#"):
            d = re.match(r"#\s*(\w+)\s*(.*)", st)
            word = d.group(1) if d else ""
            arg = d.group(2).strip() if d else ""
            if dead:
                if word in ("if", "ifdef", "ifndef"):
                    dead += 1
                elif word == "endif":
                    dead -= 1
                elif word in ("else", "elif") and dead == 1:
                    dead = 0
            elif word == "if" and re.match(r"^\(?\s*0\s*\)?$", arg):
                dead = 1
            cont = ln.rstrip().endswith("\\")
            out.append(" " * len(ln))
            continue
        out.append(" " * len(ln) if dead else ln)
    return "\n".join(out)


def _strip_templates(q):
    prev = None
    while prev != q:
        prev = q
        q = re.sub(r"<[^<>]*>", "", q)
    return q


def parse_function_head(head):
    h = _ACCESS_RE.sub("", head)
    h = re.sub(r"__declspec\s*\(\s*\w+\s*(?:\([^)]*\))?\s*\)", " ", h)
    h = h.strip()
    if h.startswith("template"):
        depth = 0
        for i, ch in enumerate(h):
            if ch == "<":
                depth += 1
            elif ch == ">":
                depth -= 1
                if depth == 0:
                    h = h[i + 1:].strip()
                    break
    if not h or "(" not in h:
        return None
    # the parameter list opens at the first '(' not inside <...> (operator() aside)
    p = -1
    depth = 0
    i = 0
    while i < len(h):
        ch = h[i]
        if ch == "<":
            depth += 1
        elif ch == ">" and depth:
            depth -= 1
        elif ch == "(" and depth == 0:
            if re.search(r"\boperator\s*$", h[:i]) and re.match(r"\(\s*\)\s*\(", h[i:]):
                i = h.index(")", i) + 1
                continue
            p = i
            break
        i += 1
    if p < 0:
        return None
    pre = h[:p].rstrip()
    m = _NAME_RE.search(pre)
    if not m:
        return None
    name = m.group(1)
    typ = pre[:m.start(1)].strip()
    depth = 0
    q = -1
    for j in range(p, len(h)):
        if h[j] == "(":
            depth += 1
        elif h[j] == ")":
            depth -= 1
            if depth == 0:
                q = j
                break
    if q < 0:
        return None
    if not _TAIL_RE.match(h[q + 1:]):
        return None
    short = re.sub(r"\s+", "", _strip_templates(name)).split("::")[-1]
    if short.lstrip("~") in _KEYWORDS or (short in _KEYWORDS):
        return None
    if "=" in typ or "(" in typ or ")" in typ or typ.endswith(",") or not _TYPE_PART_RE.match(typ):
        return None
    if re.search(r"\b(return|else|case|goto|throw|new|delete)\b", typ):
        return None
    params = h[p + 1:q]
    if ";" in params:
        return None
    return name, typ, None, params


def regex_functions(text):
    """[(qualified name, short name, offset of name)] for definitions."""
    s = blank_code(text)
    res = []
    stack = []
    fn_depth = 0
    seg = 0
    for m in re.finditer(r"[{};]", s):
        c = m.group(0)
        pos = m.start()
        if c == ";":
            seg = pos + 1
            continue
        if c == "}":
            if stack:
                t = stack.pop()
                if t[0] == "func":
                    fn_depth -= 1
            seg = pos + 1
            continue
        head = s[seg:pos]
        seg = pos + 1
        if fn_depth or (stack and stack[-1][0] == "block"):
            stack.append(("block", None))
            continue
        hs = re.sub(r"\s+", " ", _ACCESS_RE.sub("", head)).strip()
        mns = _NS_HEAD_RE.match(hs)
        if mns:
            stack.append(("ns", mns.group(1) or None))
            continue
        if _EXTERN_RE.match(hs):
            stack.append(("ns", None))
            continue
        mc = _CLASS_HEAD_RE.match(hs)
        if mc and "(" not in hs.split(mc.group(1))[0]:
            stack.append(("class", mc.group(2)))
            continue
        if re.match(r"^(?:typedef\s+)?enum\b", hs) or "=" in hs.split("(")[0]:
            stack.append(("block", None))
            continue
        r = parse_function_head(head)
        if r:
            name, typ, _unused, params = r
            in_class = bool(stack) and stack[-1][0] == "class"
            nm = re.sub(r"\s+", "", _strip_templates(name))
            if nm.startswith("operator"):
                nm = "operator" + nm[len("operator"):]
            short = nm.split("::")[-1]
            ok = True
            if not typ and "::" not in nm:
                cls = stack[-1][1] if in_class else None
                if not (in_class and cls and short.lstrip("~") == cls):
                    ok = False
            if ok and not typ and re.fullmatch(r"[A-Z0-9_]+", short) and "::" not in nm:
                ok = False
            if ok:
                quals = [n for t, n in stack if t in ("ns", "class") and n]
                qual = "::".join(quals + [nm])
                # offset of the name token: last occurrence of the short name before '('
                hp = head.find("(" if not short.startswith("operator") else short[:8])
                off_in = head.rfind(short.split("(")[0].lstrip("~")[:40], 0, hp if hp >= 0 else len(head))
                if off_in < 0:
                    off_in = len(head) - len(head.lstrip())
                name_off = (pos - len(head)) + off_in
                res.append((qual, short, name_off))
                stack.append(("func", None))
                fn_depth += 1
                continue
        stack.append(("block", None))
    return res


# --------------------------------------------------------------------------
# code items
# --------------------------------------------------------------------------

_text_cache = {}


def file_bytes(path):
    b = _text_cache.get(path)
    if b is None:
        b = Path(path).read_bytes()
        _text_cache[path] = b
    return b


_nl_cache = {}


def line_of(path, off):
    nl = _nl_cache.get(path)
    if nl is None:
        b = file_bytes(path)
        nl = [i for i, c in enumerate(b) if c == 10]
        _nl_cache[path] = nl
    return bisect.bisect_left(nl, off) + 1


_QUAL_BEFORE = re.compile(
    rb"((?:[A-Za-z_]\w*\s*(?:<[^<>;{}()]*(?:<[^<>;{}()]*>[^<>;{}()]*)*>)?\s*::\s*)+)(?:~\s*)?$")


def source_qualifier(path, off):
    b = file_bytes(path)
    pre = b[max(0, off - 400):off]
    m = _QUAL_BEFORE.search(pre)
    if not m:
        return ""
    q = re.sub(r"\s+", "", _strip_templates(m.group(1).decode("latin-1")))
    return q[:-2] if q.endswith("::") else q


def demangle_all(names):
    names = sorted({n for n in names if n and n.startswith("_Z")})
    if not names:
        return {}
    p = subprocess.run(["c++filt"], input="\n".join(names) + "\n", capture_output=True, text=True)
    outs = p.stdout.split("\n")
    return {n: outs[i] for i, n in enumerate(names) if i < len(outs)}


def _strip_params(dem):
    """'A::b(int) const' -> 'A::b' (template args removed)."""
    s = dem.strip()
    depth = 0
    for i in range(len(s) - 1, -1, -1):
        c = s[i]
        if c == ")":
            depth += 1
        elif c == "(":
            depth -= 1
            if depth == 0:
                s = s[:i]
                break
    s = _strip_templates(s)
    # a leading return type (function template instances) is separated by a space
    if " " in s and not re.search(r"operator\s", s):
        s = s.split(" ")[-1]
    return s


def _params_of(qualtype):
    m = re.search(r"\((.*)\)", qualtype or "")
    return "(" + (m.group(1) if m else "") + ")"


def find_address(path, line, short):
    m = re.search(r"(?:sub|fn|FUN|loc|func|C|j)_?([0-9a-fA-F]{6,8})$", short)
    if m and len(m.group(1)) in (6, 8):
        v = int(m.group(1), 16)
        if 0x400000 <= v < 0x500000 or 0x10000000 <= v < 0x10100000:
            return "0x%08x" % v
    lines = file_bytes(path).decode("latin-1").split("\n")
    i = line - 1
    sig = " ".join(lines[i:i + 3]) if i < len(lines) else ""
    sig = sig.split("{")[0]
    ms = ADDR_RE.findall(sig)
    if ms:
        return ms[0].lower()
    # the comment block above the definition, up to the previous code line
    j = i - 1
    block = []
    while j >= 0 and i - j <= 120:
        st = lines[j].strip()
        if not st or st.startswith(("//", "/*", "*", "*/")) or st.endswith("*/"):
            if st:
                block.append(st)
            j -= 1
            continue
        # a return type on the line above a multi-line signature
        if j == i - 1 and not st.endswith((";", "}", "{", ")")) and not st.startswith("#"):
            block.append(st)
            j -= 1
            continue
        break
    block.reverse()
    for st in block:
        if short and re.search(r"\b%s\b" % re.escape(short), st):
            ms = ADDR_RE.findall(st)
            if ms:
                return ms[0].lower()
    # a banner line that starts with the address ("// 0x10036950, 74 bytes.")
    for st in block:
        mm = re.match(r"^(?://+|/\*+|\*)\s*[-=*#:]*\s*(0x[0-9a-fA-F]{8})\b", st)
        if mm and ADDR_RE.match(mm.group(1)):
            return mm.group(1).lower()
    for st in block[:3]:
        ms = ADDR_RE.findall(st)
        if ms:
            return ms[0].lower()
    for st in block[-3:]:
        ms = ADDR_RE.findall(st)
        if ms:
            return ms[0].lower()
    return None


def code_items(files, clang_results, log):
    """Turn raw clang declarations plus the regex cross-check into items."""
    raw = []
    for r in clang_results:
        raw.extend(r["decls"])
    mang = demangle_all(d.get("mangled") for d in raw if d.get("macro") or not d["ctx"])
    seen = {}
    sdk_macro = set()
    for d in raw:
        f = str(Path(d["file"]).resolve())
        if not root_of(f):
            continue
        sp = d.get("spelling")
        if (d.get("macro") and sp not in (None, "?") and not interesting_path(sp)
                and root_of(f)[0] == "game" and d["k"] in ("function", "method")
                and (sp != "<scratch space>" or re.match(r"^(Default\w+(Con|De)structor|__\w+)$", d["name"]))):
            # generated by an SDK macro (object registration glue): covered by the
            # class and level-class items, not a definition of its own
            sdk_macro.add((f, d["name"]))
            continue
        prefix, _ = id_path(f)
        k = d["k"]
        if k in ("class", "struct") and prefix != "game":
            continue
        line = line_of(f, d["off"])
        name = d["name"]
        if k in ("class", "struct"):
            qual = "::".join(d["ctx"] + [name])
        else:
            if d["ctx"]:
                qual = "::".join(d["ctx"] + [name])
            elif d.get("macro") and d.get("mangled") in mang:
                qual = _strip_params(mang[d["mangled"]])
            else:
                q = source_qualifier(f, d["off"])
                qual = (q + "::" + name) if q else name
                if not q and d.get("mangled") in mang and d["ckind"] != "FunctionDecl":
                    qual = _strip_params(mang[d["mangled"]])
        qual = re.sub(r"\s+", " ", qual).replace("operator ", "operator ") if "operator" in qual else qual
        key = (f, line, name if k not in ("class", "struct") else "class:" + name)
        cur = seen.get(key)
        cand = {"kind": k, "qual": qual, "short": name, "file": f, "line": line,
                "params": _params_of(d.get("type", "")) if k not in ("class", "struct") else "",
                "parser": "clang"}
        if cur is None or len(qual) < len(cur["qual"]):
            seen[key] = cand
    items = list(seen.values())
    by_file = collections.defaultdict(list)
    for it in items:
        if it["kind"] not in ("class", "struct"):
            by_file[it["file"]].append(it)
    # regex cross-check
    added = collections.Counter()
    for f in files:
        f = str(f)
        if Path(f).suffix.lower() in GRAMMAR_EXT:
            continue
        text = file_bytes(f).decode("latin-1")
        have = by_file.get(f, [])
        for qual, short, off in regex_functions(text):
            line = text.count("\n", 0, off) + 1
            sh = short.split("::")[-1]
            ok = False
            for it in have:
                if abs(it["line"] - line) <= 3 and (it["short"] == sh or it["short"].replace(" ", "") == sh
                                                    or it["qual"].split("::")[-1].replace(" ", "") == sh):
                    ok = True
                    break
            if ok:
                continue
            it = {"kind": "method" if "::" in qual else "function", "qual": qual, "short": sh,
                  "file": f, "line": line, "params": "", "parser": "regex"}
            items.append(it)
            have.append(it)
            added[f] += 1
    log("  regex cross-check added %d definitions in %d files" % (sum(added.values()), len(added)))
    log("  skipped %d definitions generated by SDK macros" % len(sdk_macro))
    return items, added


def finalize_code_items(items):
    out = []
    ids = collections.Counter()
    pending = []
    for it in items:
        prefix, ip = id_path(it["file"])
        kind = it["kind"]
        if prefix == "game" and kind in ("class", "struct"):
            base = "game:%s:%s" % (ip, it["qual"])
            k = "class"
        else:
            k = kind
            base = "%s:%s:%s" % (prefix, ip, it["qual"])
        addr = None
        if prefix in ("eng", "ren") and k in ("function", "method"):
            addr = find_address(it["file"], it["line"], it["short"])
            if addr and prefix == "eng":
                base += "@" + addr
        pending.append((base, k, it, addr))
        ids[base] += 1
    used = set()
    for base, k, it, addr in sorted(pending, key=lambda x: (x[0], x[2]["line"])):
        iid = base
        if ids[base] > 1:
            iid = base + (it["params"] if it["params"] else "")
            if k == "class":
                iid = base + "#class"
        n = 2
        root = iid
        while iid in used:
            iid = "%s#%d" % (root, n)
            n += 1
        used.add(iid)
        rec = {"id": iid, "kind": k, "name": it["qual"], "file": rel(it["file"]), "line": it["line"]}
        if addr:
            rec["address"] = addr
        if it["params"]:
            rec["signature"] = it["qual"] + it["params"]
        rec["parser"] = it["parser"]
        out.append(rec)
    return out


def grammar_items(files):
    out = []
    for f in files:
        if Path(f).suffix.lower() in GRAMMAR_EXT:
            prefix, ip = id_path(f)
            out.append({"id": "%s:%s" % (prefix, ip), "kind": "grammar", "name": Path(f).name,
                        "file": rel(f), "line": 1, "parser": "file"})
    return out


# --------------------------------------------------------------------------
# data items: level classes, Bute files, console variables, cfg files
# --------------------------------------------------------------------------

def level_classes(survey, log):
    survey = Path(survey) if survey else LEVEL_SURVEY
    if survey.exists():
        worlds = collections.defaultdict(set)
        count = collections.Counter()
        with open(survey, encoding="latin-1") as fh:
            for ln in fh:
                if not ln.startswith("OBJ|"):
                    continue
                parts = ln.split("|", 3)
                if len(parts) < 3:
                    continue
                worlds[parts[2]].add(parts[1])
                count[parts[2]] += 1
        data = {"source": str(survey), "worlds": len({w for s in worlds.values() for w in s}),
                "classes": {c: {"worlds": len(worlds[c]), "instances": count[c]} for c in sorted(worlds)}}
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        LEVEL_CACHE.write_text(json.dumps(data, indent=1, sort_keys=True))
    elif LEVEL_CACHE.exists():
        data = json.loads(LEVEL_CACHE.read_text())
        log("  level survey missing; using cached %s" % LEVEL_CACHE)
    else:
        raise SystemExit("no level survey at %s and no cache %s" % (survey, LEVEL_CACHE))
    return data


def _class_def_sites():
    """level class name -> (file, line) of its BEGIN_CLASS or class definition."""
    sites = {}
    for f in sorted((SRC / "game").rglob("*.cpp")):
        if not root_of(f):
            continue
        text = f.read_text(encoding="latin-1")
        for m in re.finditer(r"^\s*BEGIN_CLASS\s*\(\s*(\w+)\s*\)", text, re.M):
            sites.setdefault(m.group(1), (str(f.resolve()), text.count("\n", 0, m.start()) + 1))
    return sites


def _macro_class_site(name):
    """AI_BAD_HARMGuard style names made by token pasting
    (BEGIN_IMPLEMENT_xxx(HARMGuard, BAD))."""
    m = re.match(r"AI_(BAD|GOOD|NEUTRAL)_(\w+)$", name)
    if not m:
        return None
    align, tail = m.group(1), m.group(2)
    pats = [r"\bBEGIN_IMPLEMENT_\w*\(\s*%s\s*,\s*%s\s*\)" % (re.escape(tail), align),
            r"\bBEGIN_IMPLEMENT_\w*\(\s*%s\s*[,)]" % re.escape(tail),
            r"\bIMPLEMENT_ALIGNMENTS\(\s*%s\s*\)" % re.escape(tail)]
    for pat in pats:
        for f in sorted((SRC / "game/ObjectDLL").glob("*.cpp")):
            text = f.read_text(encoding="latin-1")
            mm = re.search(pat, text)
            if mm:
                return str(f.resolve()), text.count("\n", 0, mm.start()) + 1
    return None


def level_items(data):
    sites = _class_def_sites()
    out = []
    for c, v in sorted(data["classes"].items()):
        site = sites.get(c) or _macro_class_site(c)
        rec = {"id": "lvl:%s" % c, "kind": "level_class", "name": c,
               "file": rel(site[0]) if site else None, "line": site[1] if site else 0,
               "worlds": v["worlds"], "instances": v["instances"]}
        if not site:
            rec["note"] = "no game class registers this name (engine/editor class)"
        out.append(rec)
    return out


def _rez_class():
    import importlib.util
    spec = importlib.util.spec_from_file_location("nolf_rezextract", str(REPO / "tools/rezextract.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.Rez


def bute_items():
    Rez = _rez_class()
    order = ["nolf.rez", "nolf2.rez", "nolfu003.rez", "nolfgoty.rez"]
    aj = GAME_DATA / "assets.json"
    if aj.exists():
        order = json.loads(aj.read_text()).get("archive_order", order)
    found = collections.OrderedDict()
    for a in order:
        p = GAME_DATA / a
        if not p.exists():
            continue
        for path, _pos, size in Rez(str(p)).walk():
            up = path.upper()
            if up.startswith("ATTRIBUTES/") and up.endswith(".TXT") and up.count("/") == 1:
                key = Path(path).name.upper()
                found.setdefault(key, {"archives": [], "name": Path(path).name, "size": size})
                found[key]["archives"].append(a)
                found[key]["size"] = size
    for f in sorted(GAME_DATA.glob("custom/ATTRIBUTES/*.txt")):
        key = f.name.upper()
        found.setdefault(key, {"archives": [], "name": f.name, "size": f.stat().st_size})
        found[key]["archives"].append("custom")
    # spelling and reader from the game code
    refs = {}
    for f in sorted((SRC / "game").rglob("*")):
        if not f.is_file() or not root_of(f) or f.suffix.lower() not in CODE_EXT:
            continue
        text = f.read_text(encoding="latin-1")
        for m in re.finditer(r'"Attributes\\\\(\w+\.txt)"', text, re.I):
            refs.setdefault(m.group(1).upper(), (m.group(1), str(f.resolve()), text.count("\n", 0, m.start()) + 1))
    out = []
    for key, v in found.items():
        spell, f, line = refs.get(key, (v["name"], None, 0))
        rec = {"id": "bute:%s" % spell, "kind": "bute_file", "name": "ATTRIBUTES/%s" % spell,
               "file": "out/native-data-resources/%s:ATTRIBUTES/%s" % (
                   v["archives"][-1] if v["archives"][-1] != "custom" else "custom", v["name"]),
               "line": 0, "archives": v["archives"], "bytes": v["size"]}
        if f:
            rec["reader"] = rel(f)
            rec["reader_line"] = line
        if v["archives"] == ["custom"]:
            rec["note"] = "only in custom/ (modern data, not retail)"
        out.append(rec)
    return out


def _code_text(f):
    def rep(m):
        s = m.group(0)
        return s if s[0] in "\"'" else re.sub(r"[^\n]", " ", s)
    return _LEX_RE.sub(rep, Path(f).read_text(encoding="latin-1"))


def _num(s):
    s = s.strip().rstrip("fFlL")
    try:
        v = float(s)
        return int(v) if v == int(v) and "." not in s else v
    except ValueError:
        return s


def cvar_items(files):
    files = [Path(f) for f in files if Path(f).suffix.lower() in CODE_EXT]
    texts = {str(f): _code_text(f) for f in files}
    found = collections.OrderedDict()   # lower name -> rec
    sites = collections.defaultdict(list)

    def add(kind, name, default, f, off, how):
        line = texts[f].count("\n", 0, off) + 1
        key = (kind, name.lower())
        sites[key].append("%s:%d" % (rel(f), line))
        if key not in found:
            found[key] = {"kind": kind, "name": name, "default": default, "file": f, "line": line,
                          "registered_by": how}
        elif found[key]["default"] in (None, "") and default not in (None, ""):
            found[key]["default"] = default

    # engine globals with initialisers, for the binding table defaults
    inits = {}
    for f, t in texts.items():
        if "/engine/" in f:
            for m in re.finditer(r"\b(?:int|float|uint32|short|unsigned|long|DWORD|LTBOOL|BOOL|char\s*\*)\s+"
                                 r"(\w+)\s*=\s*([^;,]+);", t):
                inits.setdefault(m.group(1), m.group(2).strip())
    for f, t in sorted(texts.items(), key=lambda kv: (root_of(kv[0])[0] == "game", kv[0])):
        prefix = root_of(f)[0]
        if prefix == "game":
            for m in re.finditer(r'\.\s*Init\s*\(\s*[\w>\-\.\(\)]+\s*,\s*"(\w+)"\s*,\s*(NULL|LTNULL|"[^"]*"|0)\s*,'
                                 r'\s*([-+]?[\d.]+[fF]?)\s*\)', t):
                d = _num(m.group(3))
                s = m.group(2)
                add("cvar", m.group(1), s.strip('"') if s.startswith('"') else d, f, m.start(1), "game variable tracker")
            for m in re.finditer(r'RegisterConsoleProgram\s*\(\s*"(\w+)"', t):
                add("ccmd", m.group(1), None, f, m.start(1), "game console program")
            for m in re.finditer(r'GetConsoleVar\s*\(\s*"(\w+)"\s*\)', t):
                add("cvar", m.group(1), None, f, m.start(1), "game reads it")
            for m in re.finditer(r'SetGameConVar\s*\(\s*"(\w+)"\s*,\s*([^)]*)\)', t):
                add("cvar", m.group(1), None, f, m.start(1), "game sets it")
            for m in re.finditer(r'GetGameConVar\s*\(\s*"(\w+)"', t):
                add("cvar", m.group(1), None, f, m.start(1), "game reads it")
        else:
            for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*(?:(?:LTNULL|NULL|\(\w+\s*\*\)\s*&\s*\w+|&\s*\w+)\s*,\s*){3}(?:LTNULL|NULL|\(\w+\s*\*\)\s*&\s*\w+|&\s*\w+)\s*\}', t):
                ptrs = re.findall(r"&\s*(\w+)", m.group(0))
                d = _num(inits[ptrs[0]]) if ptrs and ptrs[0] in inits else 0
                if ptrs and ptrs[0] in inits and isinstance(d, str):
                    d = d.strip('"')
                add("cvar", m.group(1), d, f, m.start(1), "engine console binding table")
            for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*(?:\(void\s*\*\)\s*)?(\w+)\s*,\s*(?:LTNULL|NULL|0)\s*\}', t):
                if m.group(2) in ("LTNULL", "NULL"):
                    continue
                add("ccmd", m.group(1), None, f, m.start(1), "engine console command table")
            for m in re.finditer(r'\.\s*sub_10021f4[04]\s*\(\s*"(\w+)"\s*,\s*([-+]?[\d.]+[fF]?)', t):
                add("cvar", m.group(1), _num(m.group(2)), f, m.start(1), "renderer console variable")
            if prefix == "ren":
                for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*([-+]?[\d.]+[fF]?)\s*\}', t):
                    add("cvar", m.group(1), _num(m.group(2)), f, m.start(1), "renderer console variable")
            for m in re.finditer(r'RegisterConsoleProgram\s*\(\s*"(\w+)"', t):
                add("ccmd", m.group(1), None, f, m.start(1), "engine console program")
    # retail cfg defaults
    cfg = {}
    cfg_files = []
    for name in ("defaults.cfg.retail", "defctrls.cfg"):
        p = GAME_DATA / name
        if not p.exists():
            continue
        cfg_files.append(p)
        for n, ln in enumerate(p.read_text(encoding="latin-1").splitlines(), 1):
            m = re.match(r'^\s*"([^"]+)"\s+"([^"]*)"\s*$', ln)
            if m:
                cfg.setdefault(m.group(1).lower(), (m.group(1), m.group(2), p.name, rel(p), n))
    out = []
    names = set()
    for (kind, low), v in found.items():
        rec = {"id": "%s:%s" % (kind, v["name"]), "kind": kind, "name": v["name"], "file": rel(v["file"]),
               "line": v["line"], "default": v["default"], "registered_by": v["registered_by"],
               "sites": sites[(kind, low)][:12]}
        if kind == "cvar" and low in cfg:
            rec["cfg_default"] = cfg[low][1]
            rec["cfg_file"] = cfg[low][2]
        names.add((kind, low))
        out.append(rec)
    for low, (name, val, fname, cpath, cline) in sorted(cfg.items()):
        if ("cvar", low) in names:
            continue
        out.append({"id": "cvar:%s" % name, "kind": "cvar", "name": name, "file": cpath, "line": cline,
                    "default": None, "cfg_default": val, "cfg_file": fname,
                    "registered_by": "retail cfg file only"})
    for p in cfg_files:
        nm = p.name.replace(".retail", "")
        out.append({"id": "cfg:%s" % nm, "kind": "cfg_file", "name": nm,
                    "file": rel(p), "line": 0})
    # ids are case-sensitive strings; make them unique ignoring case
    seen = set()
    for rec in out:
        k = rec["id"].lower()
        n = 2
        while k in seen:
            rec["id"] = rec["id"] + "#%d" % n
            k = rec["id"].lower()
            n += 1
        seen.add(k)
    return out


# --------------------------------------------------------------------------
# work units
# --------------------------------------------------------------------------
# (domain, title, [stems]) -- a stem is a path under src/game, src/engine or
# src/modern without extension (all of its .cpp/.h/... files), fnmatch globs
# allowed.  Topics larger than UNIT_MAX lines are split into parts; a single
# file larger than FILE_SPLIT lines is split by line range.

UNIT_MAX = 7800
UNIT_TARGET = 6500
FILE_SPLIT = 8000

G, C, O, SH = "game/", "game/ClientShellDLL/", "game/ObjectDLL/", "game/Shared/"
L, SV, R = "engine/lithtech/", "engine/server/", "modern/render/"


def _p(base, names):
    return [base + n for n in names.split()]


TOPICS = [
    # ---------------- FMT
    ("FMT", "World file loading, BSP, spatial structures and light animation",
     _p(SV, "world_bsp worldbsp world_bsp_interface world_clip world_containment world_grid "
            "world_lifetime world_lighting world_line_query world_loader worldmodels world_node "
            "world_object_record world_polygon world_polygon_geometry world_record world_records "
            "world_spatial world_state worldtree world_visibility container_query "
            "light_anim_properties serverlightanim shade")),
    ("FMT", "Model files, animation evaluation and server model service",
     _p(SV, "commonmodel modelanim model_cache model_callback model_evaluator model_file "
            "model_geometry model_instance model_loading modelnew model_service servermodel "
            "animation_tracker_init")),
    ("FMT", "Client model and texture loading",
     _p(L, "cmodelload modchunk demodtx lttexmod clienttexmgr")),
    # ---------------- SIM
    ("SIM", "Server physics, movement and collision",
     _p(SV, "commonphysics objmover server_physics physics_lifetime world_move collelem1cs "
            "collelem20 servermath transform")),
    ("SIM", "World simulation and server objects",
     _p(SV, "worldsim objtypes ltobject ltobject_internal object_base object_data object_data_pool "
            "object_intersection object_lifetime object_manager object_pool object_properties "
            "object_sprite inter_object_link inter_object_link_engine attachment attachment_engine "
            "counter observer position_reference spritecontrol sprite_data polygrid_record")),
    ("SIM", "Server manager core",
     _p(SV, "smgr smgrmisc smgrnew")),
    ("SIM", "Save/restore of engine state and engine object banks",
     _p(SV, "saveio save_message save_objects save_runtime savrest objbank structbank stringbank "
            "dhash collection") + _p(L, "savobj")),
    ("SIM", "Client physics, world tree and transforms",
     _p(L, "clientphysics collelem wmrotate worldtree worldtree2 wgrid transform")),
    # ---------------- NET
    ("NET", "Engine networking: net manager, packets and drivers",
     _p(SV, "netmgr packet net_connection net_driver net_manager net_packet tcpdriver localdriver "
            "netobj msgterm lthread file_transfer")),
    ("NET", "Server messages, client state and server engine",
     _p(SV, "servermessage servermsg servermgr client_state client_update client_reference message "
            "message_fwd lmessage sdkforce server server_access serverengine server_lifetime svlast "
            "svnew svtiny")),
    ("NET", "Client manager core",
     _p(L, "clientmgr clientmgr2 clientmgr3")),
    ("NET", "Client message handlers and packet tracing",
     _p(L, "cchandler ccobj nettrace tcpqarray")),
    ("NET", "Client prediction, client shell and world start",
     _p(L, "predobj predtick clientshell lt2new1 startload")),
    ("NET", "Game server shell and server base services",
     _p(O, "GameServerShell GlobalServerMgr GameBase ServerUtilities CachedFiles CDynArray CVarTrack "
           "ServerRes object_list Sparam")),
    ("NET", "Game client shell",
     _p(C, "GameClientShell")),
    ("NET", "Multiplayer: teams, server options, join and player info",
     _p(SH, "TeamMgr ServerOptionMgr") + _p(C, "NetInfo IpMgr ClientInfoMgr PlayerData")
     + _p(O, "MyGameSpyMgr")),
    ("NET", "Shared protocol definitions and game utilities",
     _p(SH, "CommonUtilities ClientServerShared MsgIDs SFXMsgIds NetDefs CommandIDs ContainerCodes "
            "CSDefs SCDefs Globals GlobalMgr GameButeMgr Factory FastHeap FastStack BankedList "
            "BankedList_impl BareList TemplateList CRC32 Timer AssertMgr DebugNew DebugNew_impl "
            "DebugMgr Stdafx unistd VersionMgr")),
    # ---------------- SVC
    ("SVC", "Engine console, console variables, cfg files and file manager",
     _p(SV, "console console_checkpoint console_state command_parser dsi engine_config de_error "
            "filemgr fsstream basestream streamsim dalloc csbase genericprop class_registry "
            "class_registry_engine common_interface servercommon sharedcommon cache_entry")),
    ("SVC", "Client console and command files",
     _p(L, "clientconsole ccclient ccline conline controlfile")),
    ("SVC", "Input devices, actions and bindings",
     _p(L, "clientinput") + _p(C, "JoystickAxis MouseMgr KeyFixes VKDefs WinUtil SystemDependant")),
    ("SVC", "Window, cursor and client surface API",
     _p(L, "clientwin cltcursor lt2new2 lt2new3")),
    ("SVC", "Client startup and module loading",
     _p(L, "clientdsi cmgrinit lithtech stdlithalloc cltdib cluglob")),
    ("SVC", "Client API",
     _p(L, "clientinterface cltapi")),
    ("SVC", "3D sound",
     _p(L, "clientsound clientsoundinst clientsoundmgr sndinst2")),
    ("SVC", "Sound streams and server sound",
     _p(L, "sndmgr2 soundinst2 clientstreams")
     + _p(SV, "sound_cache sound_instance sound_record sound_request sound_sent_state sound_wire")),
    ("SVC", "Music",
     _p(L, "clientmusic dmextra dmgrow dmloader")),
    ("SVC", "Video playback",
     _p(L, "binkvideo smackvideo lvideomgr ltnew1")),
    ("SVC", "2D surfaces and blits",
     _p(L, "cissurf clientblit diblit warpblit")),
    ("SVC", "Pixel formats, fonts and surface manager",
     _p(L, "fmconvert fmcvt32 fntdraw cmgsurf")),
    # ---------------- RWORLD / ROBJ (d3d.ren is added automatically below)
    ("RWORLD", "Client world lighting and visibility",
     _p(L, "clientworld csvis clientworldinfo ltnew2 lt2new4")),
    ("RWORLD", "Renderer core: scene, world, lightmaps and textures",
     _p(R, "mr_scene mr_lightmap mr_main mr_texture mr_window ltworld mrscene rdev mrplatform "
           "ltrender mr")),
    ("ROBJ", "Client render setup, particles and canvases",
     _p(L, "clientrender partfm cltnew cltrest")),
    ("ROBJ", "Renderer core: models, shadows, sprites, effects and 2D",
     _p(R, "mr_models mr_shadows mr_sprites mr_effects mr_objects mr_2d mr_movie mrobjects")),
    # ---------------- PLAYER
    ("PLAYER", "Server player object",
     _p(O, "PlayerObj PlayerButes") + _p(SH, "PlayerShared")),
    ("PLAYER", "Client movement, camera and view effects",
     _p(C, "CMoveMgr client_physics HeadBobMgr CameraOffsetMgr PlayerCamera CameraFX ScreenTintMgr "
           "LightScaleMgr FlashLight LeashFX PlayerSoundFX") + _p(SH, "SharedMovement")),
    ("PLAYER", "Player vehicles",
     _p(C, "VehicleMgr PlayerVehicleFX") + _p(O, "PlayerVehicle AnimatorPlayer")),
    ("PLAYER", "View weapon model",
     _p(C, "WeaponModel")),
    ("PLAYER", "Player stats, inventory, health and weapon chooser",
     _p(C, "PlayerStats WeaponChooser")),
    # ---------------- WEAPON
    ("WEAPON", "Weapons, ammo and weapon attributes",
     _p(O, "Weapon Weapons HHWeaponModel") + _p(SH, "WeaponMgr") + _p(C, "WeaponStringDefs")),
    ("WEAPON", "Projectiles, explosions, mines and damage types",
     _p(O, "Projectile ProjectileTypes Explosion Mine ClientWeaponSFX") + _p(SH, "DamageTypes")
     + _p(C, "ClientWeaponUtils")),
    ("WEAPON", "Damage model, surfaces and debris",
     _p(O, "Destructible DebrisFuncs") + _p(SH, "SurfaceMgr SurfaceFunctions SurfaceDefs DebrisMgr GibTypes")),
    ("WEAPON", "Weapon fire and impact effects",
     _p(C, "WeaponFX ProjectileFX TracerFX ShellCasingFX MuzzleFlashFX MuzzleFlashParticleFX MineFX "
           "MarkSFX BulletTrailFX LaserBeam") + _p(SH, "WeaponFXTypes") + _p(O, "ServerMark")),
    # ---------------- AI
    ("AI", "AI core: AI object, brain, attributes, targets and groups",
     _p(O, "AI AIBrain AIButeMgr AIUtils AITarget AIState AIGroup AIHumans AIAnimal AIVehicle")),
    ("AI", "AI senses, sounds and reactions",
     _p(O, "AISense AISounds AIReactions AIHumanReactions AIDogReactions")),
    ("AI", "AI nodes, volumes and regions",
     _p(O, "AINode AINodeMgr AIVolume AIVolumeMgr AIVolumeNeighbor AIRegion AIRegionMgr")),
    ("AI", "AI paths, movement and steering",
     _p(O, "AIPath AIPathMgr AIMovement AISteering AINudge")),
    ("AI", "AI commands and goals",
     _p(O, "AICmd AICmdMgr AIGoal AIGoalMgr")),
    ("AI", "Human AI states",
     _p(O, "AIHumanState")),
    # ---------------- AIB
    ("AIB", "Human AI strategies",
     _p(O, "AIHumanStrategy AIHuman")),
    ("AIB", "Animal AI: dogs, poodles and sharks",
     _p(O, "AIAnimalState AIAnimalStrategy AIDog AIPoodle AIShark")),
    ("AIB", "Vehicle AI, helicopters and AI animators",
     _p(O, "AIHelicopter AIVehicleState AIVehicleStrategy AnimatorAIVehicle AnimatorAIAnimal")),
    ("AIB", "Animation sets, parser and animators",
     _p(O, "AnimationMgr AnimationMgrHuman AnimationParser AnimationStd AnimationLex AnimationParse")
     + _p(SH, "Animator")),
    ("AIB", "Characters, hit boxes and alignment",
     _p(O, "Character CharacterHitBox CharacterMgr") + _p(SH, "CharacterAlignment")),
    ("AIB", "Bodies, death scenes, attachments and model attributes",
     _p(O, "Body BodyState DeathScene Attachments") + _p(SH, "AttachButeMgr ModelButeMgr")),
    # ---------------- WORLD
    ("WORLD", "Doors, switches, locks and keypads",
     _p(O, "Door DoorKnob HingedDoor RotatingDoor Switch Lock KeyPad")),
    ("WORLD", "Keyframers, moving and breakable world models",
     _p(O, "KeyFramer keyframer_light RotatingWorldModel TranslucentWorldModel WorldModelDebris Breakable")),
    ("WORLD", "Props, pickups, keys and spawners",
     _p(O, "Prop PropType PropTypeMgr DestructibleModel PickupItem AmmoBox ModItem GearItems WeaponItems "
           "Key KeyData Spawner RandomSpawner") + _p(C, "PickupItemFX")),
    ("WORLD", "Triggers, commands, groups and controllers",
     _p(O, "Trigger CommandMgr EventCounter ExitTrigger LaserTrigger Controller Group ObjectMsgs Editable "
           "TeleportPoint ObjectRemover") + _p(SH, "TriggerMessage")),
    ("WORLD", "Cinematics, dialogue, intelligence, objectives and level displays",
     _p(O, "CinematicTrigger Camera DialogueWindow Intelligence IntelMgr ObjectiveSprite DisplayMeter "
           "DisplayTimer") + _p(SH, "Objectives")),
    ("WORLD", "Mission flow, start points and world properties",
     _p(SH, "MissionMgr MissionData SharedMission") + _p(O, "GameStartPoint WorldProperties ServerButeMgr")),
    ("WORLD", "Security devices, searchlights, alarms and volume brushes",
     _p(O, "SecurityCamera Scanner SearchLight Alarm VolumeBrush VolumeBrushTypes")
     + _p(C, "VolumeBrushFX SearchLightFX")),
    ("WORLD", "Level effect objects",
     _p(O, "ClientLightFX Lightning ParticleSystem PolyGrid Fire Steam Sprinkles ScaleSprite SoundFX "
           "Speaker Video ScreenShake NodeLine ClientSFX SFXFuncs ClientFX ZipHook")),
    # ---------------- CUI
    ("CUI", "Interface manager",
     _p(C, "InterfaceMgr")),
    ("CUI", "Interface resources, layout, loading screen and on-screen text",
     _p(C, "InterfaceResMgr InterfaceSurfMgr LayoutMgr InterfaceMeter InterfaceTimer LoadingScreen "
           "Overlays MissionText PopupText")),
    ("CUI", "String table and credits",
     _p(C, "ClientRes Credits resource")),
    ("CUI", "Menu folders",
     _p(C, "BaseFolder BaseSelectionFolder FolderMgr Folder*")),
    ("CUI", "UI controls and windows",
     _p(C, "BitmapCtrl CycleCtrl GroupCtrl ListCtrl SliderCtrl StaticTextCtrl MessageBoxCtrl MessageBox "
           "LtWnd LTDecisionWnd LTDialogueWnd LTMaskedWnd LTMenuWnd LTMultiWnd LTTextWnd LTWndMessages "
           "BaseMenu MultiplayerMenu")),
    ("CUI", "HUD messages, damage effects, subtitles and cheats",
     _p(C, "MessageMgr DamageFXMgr Subtitle") + _p(SH, "CheatDefs")),
    ("CUI", "Client base services, settings, intel items and save summaries",
     _p(C, "ClientButeMgr GameButes GlobalClientMgr ClientUtilities VarTrack Sparam stack FastList "
           "SharedDefs plasma GameSettings IntelItemMgr ObjEditMgr") + _p(SH, "PlayerSummary")),
    ("CUI", "Sound, music and sound filter managers",
     _p(SH, "SoundMgr SoundFilterMgr SoundTypes") + _p(C, "ClientSoundMgr Music SoundFX VideoFX")
     + _p(O, "ServerSoundMgr MusicMgr")),
    ("CUI", "FX attribute tables",
     _p(SH, "FXButeMgr FXStructs")),
    ("CUI", "Client FX framework",
     _p(C, "SFXMgr SpecialFX SpecialFXList BaseScaleFX PVFXMgr") + _p(SH, "SharedFXStructs SharedBaseFXStructs")),
    ("CUI", "Particle, line and poly FX",
     _p(C, "BaseParticleSystemFX ParticleSystemFX ParticleExplosionFX ParticleShowerFX ParticleTrailFX "
           "ParticleTrailSegmentFX BaseLineSystemFX LineSystemFX BasePolyDrawFX PolyLineFX PolyGridFX "
           "sprinklesfx RandomSparksFX")),
    ("CUI", "Debris, gibs, explosion, environment and light FX",
     _p(C, "PolyDebrisFX DebrisFX GibFX ExplosionFX FireFX SmokeFX SteamFX WeatherFX BeamFX ObjSpriteFX "
           "LightFX DynamicLightFX LensFlareFX LightningFX LaserTriggerFX")),
    ("CUI", "Character client FX and head tracking",
     _p(C, "CharacterFX NodeController BodyFX DeathFX NodeLinesFX")),
]

# keyword scores that place a d3d.ren file in a renderer/service domain
# (the file's header is weighted 5x; ties go to ROBJ)
D3D_KEYWORDS = {
    "RWORLD": r"lightmap|world ?model|\bsky|\bfog\b|detail ?tex|env(?:ironment)? ?map|portal|\bbsp\b|"
              r"world poly|lit[- ]polygon|light ?anim|volumetric|\bworld\b",
    "ROBJ": r"\bmodels?\b|skin|\blod\b|chrome|shadow|sprite|particle|polygrid|line ?system|canvas|"
            r"attachment|near[- ]?(?:depth|plane|z)|\bgun\b|\bbones?\b",
    "SVC": r"\bblit|optimi[sz]ed ?2d|\b2d\b|console[- ]variable|\bfont|colou?r[- ]?key|screen mode|"
           r"display mode|pixel format|cursor|lockscreen|lock (?:the )?screen",
}
D3D_OVERRIDES = {"big21f90.cpp": "SVC", "w15_10021e50_os.cpp": "SVC", "w15_10022e90_osoiqip.cpp": "SVC",
                 "w34_1003e620_osoiqipsf.cpp": "RWORLD", "w15_100316e0.cpp": "RWORLD",
                 "w15_100235d0.cpp": "RWORLD", "w4_100234d0.cpp": "RWORLD"}
D3D_TITLES = {"RWORLD": "world drawing", "ROBJ": "object drawing and frame pipeline",
              "SVC": "2D, blits and renderer console"}
D3D_TOPIC_WORDS = [
    ("lightmap", r"lightmap"), ("world models", r"world ?model"), ("sky", r"\bsky"), ("fog", r"\bfog\b"),
    ("detail textures", r"detail ?tex"), ("environment maps", r"env(?:ironment)? ?map"),
    ("lit polygons", r"lit[- ]polygon"), ("light animation", r"light ?anim"), ("models", r"\bmodels?\b"),
    ("skinning", r"\bskin|\bbones?\b"), ("LOD", r"\blod\b"), ("chrome", r"chrome"), ("shadows", r"shadow"),
    ("sprites", r"sprite"), ("particles", r"particle"), ("polygrids", r"polygrid"),
    ("line systems", r"line ?system"), ("canvases", r"canvas"), ("attachments", r"attachment"),
    ("clipping", r"\bclip"), ("render buckets", r"bucket"), ("textures", r"\btexture"),
    ("camera", r"\bcamera|viewport"), ("blits", r"\bblit"), ("2D", r"optimi[sz]ed ?2d|\b2d\b"),
    ("console variables", r"console[- ]variable"), ("fonts", r"\bfont"), ("colour keys", r"colou?r[- ]?key"),
    ("vertex buffers", r"vertex (?:buffer|pool)"), ("device setup", r"\bdevice\b"),
]


def _d3d_head(t):
    return "\n".join(t.split("\n")[:60])


def _d3d_topics(fl, n=4):
    c = collections.Counter()
    for f in fl:
        t = file_bytes(str(f)).decode("latin-1").lower()
        h = _d3d_head(t)
        for name, rx in D3D_TOPIC_WORDS:
            c[name] += 0.1 * len(re.findall(rx, t)) + 5 * len(re.findall(rx, h))
    return [k for k, v in c.most_common(n) if v > 0]


def _stem_files(pattern, files_by_stem):
    import fnmatch
    if any(ch in pattern for ch in "*?["):
        return sorted(f for st, fs in files_by_stem.items() if fnmatch.fnmatch(st, pattern) for f in fs)
    return sorted(files_by_stem.get(pattern, []))


def _file_lines(f):
    return file_bytes(str(f)).count(b"\n") + 1


def _d3d_addr(f, items_by_file):
    m = re.search(r"_([0-9a-f]{5,8})(?:_|\.)", Path(f).name) or re.search(r"big([0-9a-f]{4,5})\.", Path(f).name)
    if m:
        v = int(m.group(1), 16)
        return v + 0x10000000 if v < 0x10000000 else v
    addrs = sorted(int(it["address"], 16) for it in items_by_file.get(rel(f), []) if it.get("address"))
    return addrs[len(addrs) // 2] if addrs else 0x10100000


def _d3d_domain(f):
    if Path(f).name in D3D_OVERRIDES:
        return D3D_OVERRIDES[Path(f).name]
    t = file_bytes(str(f)).decode("latin-1").lower()
    h = _d3d_head(t)
    sc = {d: len(re.findall(rx, t)) + 4 * len(re.findall(rx, h)) for d, rx in D3D_KEYWORDS.items()}
    best = max(sc.items(), key=lambda kv: (kv[1], kv[0] == "ROBJ"))
    return best[0] if best[1] > 0 else "ROBJ"


def build_units(files, items):
    files_by_stem = collections.defaultdict(list)
    for f in files:
        prefix, d, base = root_of(f)
        stem = str(Path(f).resolve().relative_to(SRC).with_suffix(""))
        files_by_stem[stem].append(f)
    items_by_file = collections.defaultdict(list)
    for it in items:
        if it.get("file"):
            items_by_file[it["file"]].append(it)
    assigned = set()
    topics = []
    for dom, title, pats in TOPICS:
        fl = []
        for p in pats:
            for f in _stem_files(p, files_by_stem):
                if f not in assigned:
                    fl.append(f)
                    assigned.add(f)
        if not fl:
            raise SystemExit("topic %r matches no file" % title)
        topics.append([dom, title, fl])
    # d3d.ren: by domain keyword score, in retail address order
    d3d = [f for f in files if f not in assigned and "/engine/d3d/" in str(f)]
    groups = collections.defaultdict(list)
    for f in d3d:
        groups[_d3d_domain(f)].append(f)
        assigned.add(f)
    for dom in ("RWORLD", "ROBJ", "SVC"):
        fl = sorted(groups.get(dom, []), key=lambda f: (_d3d_addr(f, items_by_file), str(f)))
        if fl:
            topics.append([dom, "d3d.ren " + D3D_TITLES[dom], fl])
    left = [f for f in files if f not in assigned]
    if left:
        raise SystemExit("files without a topic:\n  " + "\n  ".join(rel(f) for f in left))
    # split into parts
    order = {d: i for i, d in enumerate(DOMAINS)}
    topics.sort(key=lambda t: order[t[0]])
    units = []
    for dom, title, fl in topics:
        # segments: whole stems, or line ranges of a stem's big file
        segs = []
        stems = collections.OrderedDict()
        for f in fl:
            stems.setdefault(str(Path(f).with_suffix("")), []).append(f)
        for st, sfl in stems.items():
            sfl = sorted(sfl, key=lambda f: (Path(f).suffix != ".h", str(f)))
            n = sum(_file_lines(f) for f in sfl)
            big = [f for f in sfl if _file_lines(f) > FILE_SPLIT]
            if not big:
                segs.append(([(f, None) for f in sfl], n))
                continue
            for f in sfl:
                if f not in big:
                    segs.append(([(f, None)], _file_lines(f)))
            for f in big:
                nl = _file_lines(f)
                k = -(-nl // UNIT_TARGET)
                starts = sorted({it["line"] for it in items_by_file.get(rel(f), [])})
                cuts = [1]
                for p in range(1, k):
                    want = nl * p // k
                    # cut at the first item start at or after the even split
                    c = next((s for s in starts if s >= want), want)
                    cuts.append(c)
                cuts.append(nl + 1)
                for a, b in zip(cuts, cuts[1:]):
                    if b > a:
                        segs.append(([(f, (a, b - 1))], b - a))
        total = sum(n for _, n in segs)
        k = 1 if total <= UNIT_MAX else -(-total // UNIT_TARGET)
        target = total / k
        parts = [[]]
        acc = 0
        for seg in segs:
            if parts[-1] and acc + seg[1] / 2 > target * len(parts) and len(parts) < k:
                parts.append([])
            parts[-1].append(seg)
            acc += seg[1]
        for i, part in enumerate(parts):
            t = title if len(parts) == 1 else "%s (part %d of %d)" % (title, i + 1, len(parts))
            if title.startswith("d3d.ren"):
                pf = [f for seg in part for f, _ in seg[0]]
                addrs = [_d3d_addr(f, items_by_file) for f in pf]
                addrs = [x for x in addrs if x < 0x10100000]
                t = "%s %d: %s" % (title, i + 1, ", ".join(_d3d_topics(pf)))
                if addrs:
                    t += " (around 0x%08x..0x%08x)" % (min(addrs), max(addrs))
            units.append({"domain": dom, "title": t, "segments": [x for seg in part for x in seg[0]],
                          "source_lines": sum(seg[1] for seg in part)})
    for i, u in enumerate(units):
        u["id"] = "U%03d" % (i + 1)
    return units


def assign_items(units, items):
    """Every item gets exactly one unit."""
    whole = {}
    ranged = collections.defaultdict(list)
    for u in units:
        for f, rng in u["segments"]:
            if rng is None:
                whole[rel(f)] = u["id"]
            else:
                ranged[rel(f)].append((rng[0], rng[1], u["id"]))
    by_title = {}
    for u in units:
        by_title.setdefault(u["title"].split(" (part")[0], u["id"])

    def unit_of_file(f, line=0):
        if f in whole:
            return whole[f]
        for a, b, uid in ranged.get(f, []):
            if a <= max(line, 1) <= b:
                return uid
        if f in ranged:
            return ranged[f][0][2]
        return None

    fallback = {
        "level_class": by_title["World file loading, BSP, spatial structures and light animation"],
        "bute_file": by_title["World file loading, BSP, spatial structures and light animation"],
        "cvar": by_title["Engine console, console variables, cfg files and file manager"],
        "ccmd": by_title["Engine console, console variables, cfg files and file manager"],
        "cfg_file": by_title["Engine console, console variables, cfg files and file manager"],
    }
    music = by_title["Sound, music and sound filter managers"]
    for it in items:
        uid = None
        if it["kind"] == "bute_file":
            if it.get("reader"):
                uid = unit_of_file(it["reader"], it.get("reader_line", 0))
            elif re.search(r"juke|music", it["name"], re.I):
                uid = music
        elif it.get("file") and not str(it["file"]).startswith("out/"):
            uid = unit_of_file(it["file"], it.get("line", 0))
        if uid is None:
            uid = fallback[it["kind"]]
        it["unit"] = uid
    return items


# --------------------------------------------------------------------------
# build / check
# --------------------------------------------------------------------------

def log(msg):
    print(msg, file=sys.stderr, flush=True)


def _seg_name(f, rng):
    return rel(f) + ("" if rng is None else ":%d-%d" % rng)


def write_json(path, obj):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(obj, indent=1, ensure_ascii=False) + "\n")
    os.replace(tmp, path)


def map_path(domain, uid):
    return STAGING / domain / "map" / ("%s.json" % uid)


def cmd_build(a):
    files = inventoried_files()
    log("inventoried files: %d" % len(files))
    log("parsing with clang (%s) ..." % CLANG)
    results = run_clang(files, a.jobs, not a.no_cache, log)
    parse = collections.Counter(r["how"] for r in results)
    errs = sum(1 for r in results if r["errors"])
    fatal = [r for r in results if r["fatal"]]
    log("  translation units: %s; with recovered errors: %d; fatal: %d"
        % (dict(parse), errs, len(fatal)))
    raw_items, added = code_items(files, results, log)
    items = finalize_code_items(raw_items) + grammar_items(files)
    lv = level_classes(a.level_survey, log)
    items += level_items(lv)
    items += bute_items()
    items += cvar_items(files)
    ids = collections.Counter(it["id"] for it in items)
    dup = [i for i, n in ids.items() if n > 1]
    if dup:
        raise SystemExit("duplicate ids: %s" % dup[:20])
    units = build_units(files, items)
    assign_items(units, items)
    items.sort(key=lambda it: (it["unit"], it["id"]))
    per_unit = collections.defaultdict(list)
    for it in items:
        per_unit[it["unit"]].append(it)
    INV.mkdir(parents=True, exist_ok=True)
    for old in INV.glob("U*.json"):
        old.unlink()
    unit_recs = []
    for u in units:
        its = per_unit.get(u["id"], [])
        rec = {"id": u["id"], "domain": u["domain"], "title": u["title"],
               "files": [_seg_name(f, r) for f, r in u["segments"]],
               "source_lines": u["source_lines"], "item_count": len(its),
               "kinds": dict(sorted(collections.Counter(it["kind"] for it in its).items())),
               "map": str(map_path(u["domain"], u["id"])),
               "fragments": str(STAGING / u["domain"] / "fragments" / ("%s.md" % u["id"]))}
        unit_recs.append(rec)
        write_json(INV / ("%s.json" % u["id"]), {"unit": rec, "items": its})
    kinds = collections.Counter(it["kind"] for it in items)
    prefixes = collections.Counter(it["id"].split(":")[0] for it in items)
    totals = {
        "items": len(items),
        "game_classes": kinds["class"],
        "game_methods": sum(1 for it in items if it["id"].startswith("game:") and it["kind"] in ("method", "function")),
        "engine_functions": sum(1 for it in items if it["id"].startswith("eng:")),
        "render_functions": sum(1 for it in items if it["id"].startswith("ren:")),
        "level_classes": kinds["level_class"],
        "bute_files": kinds["bute_file"],
        "cvars": kinds["cvar"], "console_commands": kinds["ccmd"], "cfg_files": kinds["cfg_file"],
        "grammar_files": kinds["grammar"],
        "regex_only_definitions": sum(1 for it in items if it.get("parser") == "regex"),
        "engine_functions_with_address": sum(1 for it in items if it["id"].startswith("eng:") and it.get("address")),
        "units": len(unit_recs),
    }
    meta = {"tool": rel(Path(__file__)), "repo": str(REPO), "staging": str(STAGING),
            "parser": {"method": "clang JSON AST (%s -Xclang -ast-dump=json -fsyntax-only)" % CLANG,
                       "translation_units": dict(parse),
                       "units_with_recovered_errors": errs,
                       "fatal": [{"file": rel(r["file"]), "error": r["fatal"]} for r in fatal],
                       "regex_cross_check_added": {rel(f): n for f, n in sorted(added.items())}},
            "level_survey": lv.get("source"), "worlds": lv.get("worlds"),
            "totals": totals, "by_prefix": dict(prefixes), "by_kind": dict(kinds)}
    write_json(INV / "all.json", {"meta": meta, "items": items})
    write_json(INV / "units.json", {"meta": {"totals": totals, "domains": DOMAINS,
                                             "map_format": {"<inventory id>": "{\"spec\": [\"SPEC-...\"]} | {\"obsolete\": \"<reason>\"}"}},
                                    "units": unit_recs})
    print(json.dumps({"totals": totals, "inventory": str(INV)}, indent=1))


SPEC_ID = re.compile(r"^SPEC-[A-Z]+-U\d{3}-\d{3,}$")


def _valid_entry(e):
    if not isinstance(e, dict):
        return False
    if "spec" in e:
        s = e["spec"]
        return isinstance(s, list) and len(s) > 0 and all(isinstance(x, str) and SPEC_ID.match(x) for x in s)
    if "obsolete" in e:
        return isinstance(e["obsolete"], str) and len(e["obsolete"].strip()) >= 10
    return False


def cmd_check(a):
    allp = INV / "all.json"
    if not allp.exists():
        raise SystemExit("no inventory at %s (run: ledger.py build)" % allp)
    items = json.loads(allp.read_text())["items"]
    units = {u["id"]: u for u in json.loads((INV / "units.json").read_text())["units"]}
    entries = {}
    bad_files = []
    for mp in sorted(STAGING.glob("*/map/*.json")):
        try:
            m = json.loads(mp.read_text())
        except (OSError, ValueError) as e:
            bad_files.append({"file": str(mp), "error": str(e)})
            continue
        if not isinstance(m, dict):
            bad_files.append({"file": str(mp), "error": "not a JSON object"})
            continue
        for k, v in m.items():
            if k not in entries or not _valid_entry(entries[k]):
                entries[k] = v
    known = set()
    report = collections.OrderedDict()
    for uid, u in units.items():
        report[uid] = {"domain": u["domain"], "title": u["title"], "items": 0, "uncovered": 0, "invalid": 0}
    examples = collections.defaultdict(list)
    for it in items:
        known.add(it["id"])
        r = report[it["unit"]]
        r["items"] += 1
        e = entries.get(it["id"])
        if e is None:
            r["uncovered"] += 1
            if len(examples[it["unit"]]) < 3:
                examples[it["unit"]].append(it["id"])
        elif not _valid_entry(e):
            r["invalid"] += 1
            r["uncovered"] += 1
    unknown = sorted(k for k in entries if k not in known)
    total_unc = sum(r["uncovered"] for r in report.values())
    out = {"uncovered_total": total_unc,
           "items_total": len(items),
           "units_fully_covered": sum(1 for r in report.values() if r["uncovered"] == 0),
           "units_total": len(report),
           "unknown_map_ids": len(unknown),
           "unreadable_maps": bad_files,
           "units": {uid: dict(r, **({"examples": examples[uid]} if examples[uid] else {}))
                     for uid, r in report.items()}}
    if a.verbose and unknown:
        out["unknown_map_id_examples"] = unknown[:50]
    print(json.dumps(out, indent=1, ensure_ascii=False))
    return 1 if (total_unc or bad_files) else 0


def cmd_stats(a):
    allp = INV / "all.json"
    m = json.loads(allp.read_text())["meta"]
    print(json.dumps(m["totals"], indent=1))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="parse the reference and write the inventory")
    b.add_argument("--jobs", type=int, default=max(1, min(16, (os.cpu_count() or 2) - 2)))
    b.add_argument("--level-survey", default=None, help="light survey out.txt (OBJ lines)")
    b.add_argument("--no-cache", action="store_true", help="re-run clang for every translation unit")
    c = sub.add_parser("check", help="verify the coverage maps against the inventory")
    c.add_argument("-v", "--verbose", action="store_true")
    sub.add_parser("stats", help="print inventory totals")
    a = ap.parse_args(argv)
    return {"build": cmd_build, "check": cmd_check, "stats": cmd_stats}[a.cmd](a) or 0


if __name__ == "__main__":
    sys.exit(main())
