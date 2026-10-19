#!/usr/bin/env python3
"""The library's one tool. No dependencies beyond Python 3.

  library.py check            check every entry (CI runs this on every pull request)
  library.py ids [--listed]   the entries as a JSON list (CI's build matrix); --listed: only those on the list
  library.py core             which entries carry the core in core/ unchanged, and what differs
  library.py core-copy ID...  copy core/ into those entries (after a core change)
  library.py collect ID D     after a build (and the simulator): copy what the page needs into D/ID/: the
                              files named in firmware/build/flash_args, flash_args itself, and the
                              simulator's results.json. ID "samplooler" is the Samplooler's own firmware/
  library.py site [--dist D] [--local] [--out O]
                              build the web page into O (default _site): the list (library.json, which the
                              app in app/ reads too), the images, README and manual, and per entry a flasher
                              manifest with its files. The Samplooler itself is the first entry, so a board
                              can be flashed back from the page or the app. --dist takes what `collect` put
                              in D (CI); --local takes each entry's own build/ and tools/sim/out/.
  library.py readme           rewrite the list in instruments/README.md from the entries
"""
import json, os, re, shutil, sys, hashlib, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INST = os.path.join(ROOT, "instruments")
CORE = os.path.join(ROOT, "core")
REPO = "https://github.com/willbearfruits/samplooler"

USES = {"mic", "tilt", "joystick", "speaker", "wifi", "bluetooth"}
STATUS = {"played": "Played on a board", "board": "Runs on a board", "simulator": "Simulator only"}
LICENCES = {"GPL-3.0", "GPL-3.0-or-later"}          # the core is GPL-3.0, so everything built on it is too
IMG_EXT = (".gif", ".png", ".jpg", ".jpeg", ".webp")
IMG_MAX = 2 * 1024 * 1024
SAMPLOOLER_PID = 0x4020

# The Samplooler as an entry of the list, so the page can put it back on a board. Its firmware is firmware/,
# its pictures media/, its manual docs/MANUAL.md; the version is the latest v* tag.
SAMPLOOLER = {
    "name": "Samplooler",
    "summary": "The looper the Stack comes with. Hold the button and make a sound; let go and it loops. Tip it "
               "forward and it runs faster, back and it runs backwards. Its input can go through SING's voices.",
    "author": "willbearfruits",
    "licence": "GPL-3.0",
    "chip": "esp32s3",
    "uses": ["mic", "tilt", "speaker"],
    "status": "played",
    "status_note": "What a bought Stack comes loaded with.",
    "image": "media/tour.gif",
    "more": ["media/screens/recording.png", "media/screens/reverse.png", "media/screens/grain.png"],
}


def source_dir(i):
    """Where an entry's files are: instruments/<id>/, or the repository's root for the Samplooler."""
    return ROOT if i == "samplooler" else os.path.join(INST, i)


def samplooler_version():
    try:
        import subprocess
        v = subprocess.run(["git", "describe", "--tags", "--match", "v*", "--abbrev=0"], cwd=ROOT,
                           capture_output=True, text=True).stdout.strip()
    except OSError:
        v = ""
    return v[1:] if v.startswith("v") else (v or "0.1.0")


def ids():
    return sorted(d for d in os.listdir(INST) if os.path.isfile(os.path.join(INST, d, "entry.json")))


def listed(i):
    """On the page, in the app and in instruments/README.md. An entry with "listed": false stays in the
    repository, is checked and built, but is not offered for install until it has been tried on a board."""
    return load(i).get("listed", True) is not False


def load(i):
    with open(os.path.join(INST, i, "entry.json"), encoding="utf-8") as f:
        return json.load(f)


def cmake_value(i, key):
    p = os.path.join(INST, i, "firmware", "main", "CMakeLists.txt")
    if not os.path.isfile(p):
        return None
    m = re.search(r"set\(\s*%s\s+\"?([^\")\s]+)\"?\s*\)" % key, open(p).read())
    return m.group(1) if m else None


def core_files():
    with open(os.path.join(ROOT, "tools", "core_files.txt")) as f:
        return [l.strip() for l in f if l.strip() and not l.startswith("#")]


def digest(p):
    with open(p, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


# ------------------------------------------------------------------ check
def check():
    errors, warnings = [], []
    names, pids, serials = {}, {}, {}
    for i in sorted(os.listdir(INST)):
        d = os.path.join(INST, i)
        if not os.path.isdir(d):
            continue
        def err(msg): errors.append(f"{i}: {msg}")
        def warn(msg): warnings.append(f"{i}: {msg}")
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{1,30}", i):
            err("the folder name must be lower case letters, digits and dashes")
        if not os.path.isfile(os.path.join(d, "entry.json")):
            err("no entry.json")
            continue
        try:
            e = load(i)
        except ValueError as x:
            err(f"entry.json is not valid JSON: {x}")
            continue
        for k in ("name", "summary", "author", "licence", "version", "chip", "uses", "status", "image"):
            if k not in e:
                err(f"entry.json has no '{k}'")
        name = e.get("name", "")
        if not re.fullmatch(r"[A-Z0-9][A-Z0-9 -]{0,11}", name):
            err("name: capitals, digits, spaces and dashes, at most 12 characters")
        # The core keeps each instrument's settings under its name in lower case: two entries with one name
        # would share them, and "samplooler" is the Samplooler's own.
        ns = name.lower()
        if ns == "samplooler":
            err("the name SAMPLOOLER is taken by the Samplooler itself")
        if ns in names:
            err(f"the name {name} is taken by {names[ns]}")
        names[ns] = i
        if len(e.get("summary", "")) > 200:
            err("summary: at most 200 characters")
        if e.get("licence") not in LICENCES:
            err(f"licence must be one of {sorted(LICENCES)} (the core is GPL-3.0)")
        if e.get("chip") != "esp32s3":
            err("chip: only esp32s3 for now (the Samplooler's S3 boards)")
        bad = set(e.get("uses", [])) - USES
        if bad:
            err(f"uses: unknown {sorted(bad)}; known: {sorted(USES)}")
        if e.get("status") not in STATUS:
            err(f"status must be one of {sorted(STATUS)}")
        if "listed" in e and not isinstance(e["listed"], bool):
            err("listed: true or false")
        for img in [e.get("image")] + list(e.get("more", [])):
            if not img:
                continue
            p = os.path.join(d, img)
            if not os.path.isfile(p):
                err(f"no file {img}")
            elif not img.lower().endswith(IMG_EXT):
                err(f"{img}: images are gif, png, jpg or webp")
            elif os.path.getsize(p) > IMG_MAX:
                err(f"{img} is over 2 MB")
        for need in ("README.md", "firmware/CMakeLists.txt"):
            if not os.path.isfile(os.path.join(d, need)):
                err(f"no {need}")
        # The same partition table as the Samplooler, so flashing an instrument keeps its sample slots.
        pt = os.path.join(d, "firmware", "partitions.csv")
        if not os.path.isfile(pt) or digest(pt) != digest(os.path.join(CORE, "firmware", "partitions.csv")):
            err("firmware/partitions.csv must be the Samplooler's (core/firmware/partitions.csv)")
        cname = cmake_value(i, "INSTRUMENT_NAME")
        if cname is None:
            warn("no INSTRUMENT_NAME in firmware/main/CMakeLists.txt (not on the core?)")
        elif cname != name:
            err(f"entry.json says {name}, firmware/main/CMakeLists.txt says {cname}")
        pid = cmake_value(i, "INSTRUMENT_PID")
        if pid is None:
            warn("no INSTRUMENT_PID in firmware/main/CMakeLists.txt")
        else:
            try:
                v = int(pid, 0)
            except ValueError:
                err(f"INSTRUMENT_PID {pid} is not a number")
            else:
                if v == SAMPLOOLER_PID:
                    err("USB product ID 0x4020 is the Samplooler's")
                if v in pids:
                    err(f"USB product ID {pid} is taken by {pids[v]}")
                pids[v] = i
        ser = cmake_value(i, "INSTRUMENT_SERIAL")
        if ser is not None:
            if not re.fullmatch(r"[A-Z]{2}", ser):
                err(f"INSTRUMENT_SERIAL {ser}: two capital letters")
            elif ser == "SL":
                err("INSTRUMENT_SERIAL SL is the Samplooler's")
            elif ser in serials:
                err(f"INSTRUMENT_SERIAL {ser} is taken by {serials[ser]}")
            serials[ser] = i
        sd = os.path.join(d, "firmware", "sdkconfig.defaults")
        if not os.path.isfile(sd) or 'CONFIG_IDF_TARGET="esp32s3"' not in open(sd).read():
            err('firmware/sdkconfig.defaults must set CONFIG_IDF_TARGET="esp32s3"')
        if not os.path.isfile(os.path.join(d, "tools", "sim", "run.sh")):
            warn("no simulator (tools/sim/run.sh): only the build is checked")
    for w in warnings:
        print("note:", w)
    for x in errors:
        print("ERROR:", x)
    n = len(ids())
    print(f"{n} entries, {len(errors)} errors")
    if pids:
        print(f"next free USB product ID: {max(pids) + 1:#06x}")
    return 1 if errors else 0


# ------------------------------------------------------------------ core
def core_status():
    files = core_files()
    for i in ids():
        diff = [p for p in files if not os.path.isfile(os.path.join(INST, i, p))
                or digest(os.path.join(INST, i, p)) != digest(os.path.join(CORE, p))]
        if not diff:
            print(f"{i}: the core, unchanged")
        elif len(diff) == len(files):
            print(f"{i}: not on the core")
        else:
            print(f"{i}: {len(diff)} of {len(files)} core files differ: {' '.join(diff)}")
    return 0


def core_copy(targets):
    for i in targets:
        if i not in ids():
            print(f"no entry {i}")
            return 1
        for p in core_files():
            dst = os.path.join(INST, i, p)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(CORE, p), dst)
        print(f"{i}: core copied; build it and run its simulator")
    return 0


# ------------------------------------------------------------------ firmware files
# The flasher writes the parts at their own offsets (bootloader, partition table, app), not one merged image
# from 0x0: a merged image fills the gaps with 0xFF, and the gap at 0x9000 is the NVS partition, where the
# Samplooler and every instrument keep their settings and saved sounds. Writing the parts leaves it alone.
def flash_parts(build):
    """[(offset, path relative to build)] from build/flash_args, or None."""
    p = os.path.join(build, "flash_args")
    if not os.path.isfile(p):
        return None
    parts = []
    for line in open(p):
        m = re.match(r"\s*(0x[0-9a-fA-F]+)\s+(\S+)", line)
        if m:
            parts.append((int(m.group(1), 16), m.group(2)))
    return sorted(parts) or None


def collect(i, dest):
    d = source_dir(i)
    build = os.path.join(d, "firmware", "build")
    parts = flash_parts(build)
    if not parts:
        print(f"{i}: no firmware/build/flash_args (build it first)")
        return 1
    out = os.path.join(dest, i)
    os.makedirs(out, exist_ok=True)
    shutil.copy2(os.path.join(build, "flash_args"), out)
    for _, rel in parts:
        os.makedirs(os.path.dirname(os.path.join(out, rel)) or out, exist_ok=True)
        shutil.copy2(os.path.join(build, rel), os.path.join(out, rel))
    res = os.path.join(d, "tools", "sim", "out", "results.json")
    if os.path.isfile(res):
        shutil.copy2(res, out)
    print(f"{i}: {len(parts)} parts collected in {out}")
    return 0


# ------------------------------------------------------------------ site
MD_IMAGE = re.compile(r"!\[[^\]]*\]\(([^)\s]+)")


def copy_docs(src, dst):
    """Copy README.md and docs/MANUAL.md, and the pictures they show from inside src, to dst (the app shows
    them). Returns {"readme_md": path, "manual_md": path} relative to dst, for those that exist."""
    found = {}
    for key, rel in (("readme_md", "README.md"), ("manual_md", os.path.join("docs", "MANUAL.md"))):
        p = os.path.join(src, rel)
        if not os.path.isfile(p):
            continue
        os.makedirs(os.path.dirname(os.path.join(dst, rel)) or dst, exist_ok=True)
        shutil.copy2(p, os.path.join(dst, rel))
        found[key] = rel.replace(os.sep, "/")
        for link in MD_IMAGE.findall(open(p, encoding="utf-8").read()):
            if "://" in link or link.startswith("/"):
                continue
            img = os.path.normpath(os.path.join(os.path.dirname(p), link))
            if not img.startswith(os.path.abspath(src) + os.sep) or not os.path.isfile(img):
                continue
            out = os.path.join(dst, os.path.relpath(img, src))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copy2(img, out)
    return found


def site(args):
    out = os.path.join(ROOT, "_site")
    dist, local = None, False
    a = iter(args)
    for x in a:
        if x == "--dist":
            dist = os.path.abspath(next(a))
        elif x == "--out":
            out = os.path.abspath(next(a))
        elif x == "--local":
            local = True
        else:
            print("unknown option", x)
            return 1
    if os.path.isdir(out):
        shutil.rmtree(out)
    shutil.copytree(os.path.join(ROOT, "site"), out)
    entries = []
    for i in ["samplooler"] + [x for x in ids() if listed(x)]:
        e = dict(SAMPLOOLER, version=samplooler_version()) if i == "samplooler" else load(i)
        d = source_dir(i)
        os.makedirs(os.path.join(out, i), exist_ok=True)
        imgs = []
        for img in [e["image"]] + list(e.get("more", [])):
            dst = os.path.join(out, i, img)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(os.path.join(d, img), dst)
            imgs.append(f"{i}/{img}")
        build = src_res = None
        if dist:
            build = os.path.join(dist, i)
            src_res = os.path.join(dist, i, "results.json")
        elif local:
            build = os.path.join(d, "firmware", "build")
            src_res = os.path.join(d, "tools", "sim", "out", "results.json")
        parts = flash_parts(build) if build else None
        manifest = firmware = None
        if parts and all(os.path.isfile(os.path.join(build, rel)) for _, rel in parts):
            fw = os.path.join(out, i, "firmware")
            os.makedirs(fw, exist_ok=True)
            mparts, fparts = [], []
            for off, rel in parts:
                src = os.path.join(build, rel)
                shutil.copy2(src, os.path.join(fw, os.path.basename(rel)))
                mparts.append({"path": f"firmware/{os.path.basename(rel)}", "offset": off})
                fparts.append({"path": f"firmware/{os.path.basename(rel)}", "offset": off,
                               "size": os.path.getsize(src), "sha256": digest(src)})   # the app checks what it downloads
            firmware = {"chip": "esp32s3", "parts": fparts}
            with open(os.path.join(out, i, "manifest.json"), "w") as f:
                json.dump({"name": e["name"], "version": e["version"], "new_install_prompt_erase": True,
                           "builds": [{"chipFamily": "ESP32-S3", "parts": mparts}]}, f, indent=2)
            manifest = f"{i}/manifest.json"
        else:
            print(f"note: no firmware for {i}: its flash button is left out")
        checks = None
        if src_res and os.path.isfile(src_res):
            try:
                r = json.load(open(src_res))
                if r.get("failed") == 0:
                    checks = r.get("checks")
            except ValueError:
                pass
        docs = {k: f"{i}/{v}" for k, v in copy_docs(d, os.path.join(out, i)).items()}
        where = "" if i == "samplooler" else f"/instruments/{i}"
        tree = f"{REPO}/tree/main{where}"
        blob = f"{REPO}/blob/main{where}"
        entries.append({
            "id": i, "name": e["name"], "summary": e["summary"], "author": e["author"],
            "licence": e["licence"], "version": e["version"], "uses": e.get("uses", []),
            "hardware_note": e.get("hardware_note"), "status": e["status"], "status_label": STATUS[e["status"]],
            "status_note": e.get("status_note"), "image": imgs[0], "more": imgs[1:], "manifest": manifest,
            "firmware": firmware, "checks": checks, "source": tree, "readme": f"{blob}/README.md",
            "manual": f"{blob}/docs/MANUAL.md" if os.path.isfile(os.path.join(d, "docs", "MANUAL.md")) else None,
            "readme_md": docs.get("readme_md"), "manual_md": docs.get("manual_md"),
            "added": e.get("added"),
        })
    rank = {"played": 0, "board": 1, "simulator": 2}
    entries.sort(key=lambda x: (x["id"] != "samplooler", rank[x["status"]], x["name"]))
    with open(os.path.join(out, "library.json"), "w") as f:
        json.dump({"updated": datetime.date.today().isoformat(), "repo": REPO, "entries": entries}, f, indent=1, ensure_ascii=False)
    print(f"site: {len(entries)} entries in {out}")
    return 0


# ------------------------------------------------------------------ readme
def readme():
    p = os.path.join(INST, "README.md")
    s = open(p, encoding="utf-8").read()
    rows = ["| | What | Status |", "| - | - | - |"]
    rest = []
    for i in ids():
        e = load(i)
        if not listed(i):
            rest.append(f"[{e['name']}]({i})")
            continue
        rows.append(f"| [{e['name']}]({i}) | {e['summary']} | {STATUS[e['status']]} |")
    if rest:
        rows += ["", "Also in this folder, still in progress and not on the list: " + ", ".join(rest) +
                 " ([ROADMAP.md](../ROADMAP.md))."]
    new = re.sub(r"(<!-- list -->\n).*?(\n<!-- /list -->)", lambda m: m.group(1) + "\n".join(rows) + m.group(2), s, flags=re.S)
    if new == s and "<!-- list -->" not in s:
        print("instruments/README.md has no <!-- list --> markers")
        return 1
    open(p, "w", encoding="utf-8").write(new)
    print("instruments/README.md: list rewritten")
    return 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    c, rest = argv[0], argv[1:]
    if c == "check":
        return check()
    if c == "ids":
        print(json.dumps([i for i in ids() if listed(i)] if "--listed" in rest else ids()))
        return 0
    if c == "core":
        return core_status()
    if c == "core-copy":
        return core_copy(rest)
    if c == "collect" and len(rest) == 2:
        return collect(rest[0], rest[1])
    if c == "site":
        return site(rest)
    if c == "readme":
        return readme()
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
