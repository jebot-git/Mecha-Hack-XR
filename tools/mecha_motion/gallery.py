"""Build the offline review gallery from completed and verified actor reels."""
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
import json
import math
from pathlib import Path
import subprocess
import tempfile


FPS = 24
TILE_WIDTH, TILE_HEIGHT = 576, 352


def local_file(out, relative):
    path = (out / relative).resolve()
    if Path(relative).is_absolute() or not path.is_relative_to(out) or not path.is_file():
        raise ValueError(f"Missing or non-local review file: {relative}")
    return path


def probe(path, expected_frames, expected_fps, concat=False):
    # Count actual video packets as well as checking the H.264 container entry.
    raw = subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0", "-count_packets",
        "-show_entries", "stream=codec_name,nb_read_packets,nb_frames,avg_frame_rate,r_frame_rate,width,height",
        "-of", "json", str(path),
    ])
    stream, = json.loads(raw)["streams"]
    frames = int(stream["nb_read_packets"])
    fps = Fraction(stream["avg_frame_rate"])
    # The original reference montage is stream-copied via the concat demuxer,
    # which rounds chapter boundaries to milliseconds.
    tolerance = Fraction(1, 100) if concat else 0
    if (stream["codec_name"] != "h264" or frames != expected_frames
            or int(stream["nb_frames"]) != frames or abs(fps - expected_fps) > tolerance
            or Fraction(stream["r_frame_rate"]) != expected_fps):
        raise ValueError(f"Unexpected video stream in {path}: {stream}; expected {expected_frames} frames at {expected_fps} fps")
    return {"frames": frames, "fps": str(fps), "width": stream["width"], "height": stream["height"]}


def verify_sources(out, records, refs):
    if not records or set(refs) - set(records):
        raise ValueError("Invalid actor roster")
    videos = {}
    for actor, row in records.items():
        for field in ("poster", "glb", "native"):
            if field != "native" or field in row:
                local_file(out, row[field])
        offset = 0
        names = set()
        for action in row["actions"]:
            if (action["name"] in names or action["start"] != offset or action["frames"] < 1
                    or not action["name"]):
                raise ValueError(f"Invalid chapter in {actor}: {action}")
            offset += action["frames"]
            names.add(action["name"])
        if offset != row["frames"] or not offset:
            raise ValueError(f"Incomplete chapters in {actor}")
        videos[row["video"]] = (offset, row["fps"], False)
    for row in refs.values():
        local_file(out, row["poster"])
        videos[row["file"]] = (row["frames"], row["fps"], False)
    if refs:
        names = sorted(refs, key=lambda name: (refs[name]["family"], name))
        rates = {ref["fps"] for ref in refs.values()}
        if len(rates) != 1:
            raise ValueError("Mixed frame rates in reference montage")
        frames = sum(max(refs[n]["frames"] for n in names[i:i+9]) for i in range(0, len(names), 9))
        videos["reference-roster.mp4"] = (frames, rates.pop(), True)
    def check(item):
        name, expectation = item
        return name, probe(local_file(out, name), *expectation)
    with ThreadPoolExecutor(max_workers=4) as pool:
        return dict(pool.map(check, videos.items()))


def overview_boards(records, refs):
    # Take a minimally reused actor for each action. Rare/unique actions get
    # first pick, so generic poses can fill in examples of otherwise unseen rigs.
    candidates = defaultdict(list)
    for actor, row in sorted(records.items()):
        for clip in row["actions"]:
            candidates[clip["name"]].append(dict(clip, actor=actor, family=row["family"],
                                                 file=row["video"], fps=row["fps"], source="actions"))
    for actor, row in sorted(refs.items()):
        candidates[row["action"]].append(dict(name=row["action"], start=0, frames=row["frames"], actor=actor,
                                              family=row["family"], file=row["file"], fps=row["fps"], source="reference"))
    usage = Counter()
    by_family = defaultdict(list)
    for action in sorted(candidates, key=lambda key: (len(candidates[key]), key)):
        clip = min(candidates[action], key=lambda c: (usage[c["actor"]], c["actor"], c["source"]))
        usage[clip["actor"]] += 1
        by_family[clip["family"]].append(clip)
    boards = []
    offset = 0
    for family, clips in sorted(by_family.items()):
        clips.sort(key=lambda c: c["name"])
        for i in range(0, len(clips), 4):
            batch = clips[i:i+4]
            seconds = max(3, math.ceil(max(c["frames"] / c["fps"] for c in batch)))
            count = seconds * FPS
            boards.append(dict(family=family, start=offset, frames=count, tiles=batch))
            offset += count
    return boards


def encode_board(out, path, board):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-y", "-filter_complex_threads", "1"]
    filters, tiles = [], []
    for i, clip in enumerate(board["tiles"]):
        args += ["-ss", f"{clip['start']/clip['fps']:.9f}", "-threads", "1", "-i", str(out / clip["file"])]
        # Actor and action names are already burned into every source frame.
        filters.append(
            f"[{i}:v]trim=end_frame={clip['frames']},setpts=PTS-STARTPTS,fps={FPS},"
            f"scale={TILE_WIDTH}:{TILE_HEIGHT}:force_original_aspect_ratio=decrease,"
            f"pad={TILE_WIDTH}:{TILE_HEIGHT}:(ow-iw)/2:(oh-ih)/2,setsar=1,"
            f"tpad=stop_mode=clone:stop={board['frames']},trim=end_frame={board['frames']}[t{i}]")
        tiles.append(f"[t{i}]")
    if len(tiles) > 1:
        layout = "|".join(f"{i%2*TILE_WIDTH}_{i//2*TILE_HEIGHT}" for i in range(len(tiles)))
        filters.append(f"{''.join(tiles)}xstack=inputs={len(tiles)}:layout={layout}:fill=0x10191f[stack]")
    else:
        filters.append(f"{tiles[0]}null[stack]")
    filters.append(f"[stack]pad={TILE_WIDTH*2}:{TILE_HEIGHT*2}:0:0:color=0x10191f[v]")
    subprocess.run(args + ["-filter_complex", ";".join(filters), "-map", "[v]", "-an", "-frames:v",
                           str(board["frames"]), "-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                           "-threads", "2", "-pix_fmt", "yuv420p", str(path)], check=True)


def finish(out, records, references):
    out = Path(out).resolve()
    media = verify_sources(out, records, references)
    boards = overview_boards(records, references)
    total = sum(board["frames"] for board in boards)
    overview = "action-overview.mp4"
    with tempfile.TemporaryDirectory(dir=out, prefix=".gallery-") as temp:
        stage = Path(temp)
        parts = []
        for i, board in enumerate(boards):
            part = stage / f"page-{i:03}.mp4"
            encode_board(out, part, board)
            parts.append(f"file '{part.name}'\n")
        (stage / "pages.txt").write_text("".join(parts))
        subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-y", "-f", "concat", "-safe", "1",
                        "-i", str(stage / "pages.txt"), "-c", "copy", "-movflags", "+faststart",
                        str(stage / overview)], check=True)
        media[overview] = probe(stage / overview, total, FPS)
        gallery_records = {name: {key: row[key] for key in ("family", "kind", "video", "poster", "glb",
                                                           "native", "fps", "actions") if key in row}
                           for name, row in records.items()}
        data = {"actors": gallery_records, "references": references, "boards": boards, "fps": FPS}
        (stage / "review-media.json").write_text(json.dumps({"videos": media, "boards": boards, "fps": FPS}, indent=2))
        payload = json.dumps(data, separators=(",", ":")).replace("<", "\\u003c")
        (stage / "index.html").write_text(HTML.replace("__GALLERY_DATA__", payload), encoding="utf-8")
        for name in (overview, "review-media.json", "index.html"):
            (stage / name).replace(out / name)


HTML = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mecha Hack XR · Motion library</title>
<style>
:root{color-scheme:dark;--base:#111a20;--surface:#1a272f;--line:#40545b;--ink:#edf3f1;--dim:#b0c1bf;--accent:#93e7c2}
*{box-sizing:border-box}body{margin:0;background:var(--base);color:var(--ink);font:1rem/1.45 system-ui,sans-serif}
main{width:min(86rem,100%);margin:auto;padding:clamp(1rem,3vw,3rem)}
header{display:flex;align-items:baseline;gap:1rem;justify-content:space-between;border-bottom:1px solid var(--line);padding-bottom:1rem;flex-wrap:wrap}
h1,h2{margin:0;font-weight:600;line-height:1.2}h1{font-size:clamp(1.45rem,3vw,2.4rem)}h2{font-size:1.32rem}
nav,.links,.tools{display:flex;gap:.7rem 1.25rem;align-items:center;flex-wrap:wrap}a{color:var(--accent);text-underline-offset:.2rem}
.layout{display:grid;grid-template-columns:minmax(13rem,1fr) minmax(0,3fr);gap:clamp(1rem,3vw,2.6rem);margin-top:1.5rem}
aside{display:flex;flex-direction:column;gap:.65rem}label,.meta,#matches{font-size:.875rem;color:var(--dim)}
input,select,button{font:inherit;color:var(--ink);background:var(--surface);border:1px solid var(--line);border-radius:.3rem;padding:.48rem .65rem}
input:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
select{max-width:100%}#rig{width:100%;max-height:63vh}#rig option{padding:.32rem}.stage{min-width:0}.meta{margin:.25rem 0 .6rem}
video{display:block;width:100%;aspect-ratio:18/11;background:#0c1317;border:1px solid var(--line);margin:.5rem 0 .9rem}
.tools label{display:flex;gap:.45rem;align-items:center}.links{margin-top:1rem}#jump{cursor:pointer}#chapter{min-width:8rem}
[hidden]{display:none!important}@media(max-width:750px){.layout{grid-template-columns:1fr}#rig{max-height:9rem}#search{width:100%}}
</style></head><body><main>
<header><h1>Mecha Hack XR · Motion library</h1><nav><a href="action-overview.mp4">Action overview</a>
<a id="refRoster" href="reference-roster.mp4">Reference roster</a><a href="review-media.json">Media index</a></nav></header>
<div class="layout"><aside><label for="search">Search rig, family or motion</label>
<input id="search" type="search" placeholder="Search motions" autocomplete="off">
<label for="rig" id="matches">Rigs</label><select id="rig" size="18" aria-label="Matching rigs"></select></aside>
<section class="stage"><h2 id="title">Motion review</h2><div class="meta" id="meta"></div>
<video id="player" controls preload="metadata" playsinline></video>
<div class="tools"><label>Reel <select id="mode"></select></label>
<label>Chapter <select id="chapter"></select></label><button type="button" id="jump">Play chapter</button>
<label><input type="checkbox" id="loop">Loop chapter</label></div>
<div class="links"><a id="clipLink">Open full reel</a><a id="glb">Animated GLB</a><a id="native">Native tracks</a></div>
</section></div><noscript>Enable JavaScript to browse individual rigs. Both overview films are linked above.</noscript>
</main><script type="application/json" id="catalog">__GALLERY_DATA__</script>
<script>
"use strict";
const {actors,references,boards,fps} = JSON.parse(document.getElementById("catalog").textContent);
const byId = id => document.getElementById(id);
const pretty = s => s.replace(/[-_]/g," ").replace(/^./, c => c.toUpperCase());
const rig = byId("rig"), search = byId("search"), mode = byId("mode"), chapter = byId("chapter"), player = byId("player");
const names = Object.keys(actors).sort((a,b) => a.localeCompare(b));
const add = (el, value, text) => el.add(new Option(text,value));
const clock = seconds => Math.floor(seconds/60) + ":" + String(Math.floor(seconds)%60).padStart(2,"0");
let chapters = [], pending = null, activePath = "", rate = fps, actor = "";
byId("refRoster").hidden = !Object.keys(references).length;
function selectTime(play){
  const item = chapters[chapter.selectedIndex];
  if(!item)return;
  pending = {time:item.start/rate, play:play};
  if(player.readyState >= 1)applyTime();
}
function applyTime(){
  if(!pending)return;
  const {time,play} = pending;
  pending=null;
  player.currentTime=time;
  if(play)player.play().catch(()=>{});
}
player.addEventListener("loadedmetadata",applyTime);
function loadReel(){
  if(!actor)return;
  const a = actors[actor], r = references[actor], type=mode.value;
  let path, poster = "";
  if(type==="overview"){
    path="action-overview.mp4"; rate=fps;
    chapters=boards.map(b=>({name:pretty(b.family)+" · "+b.tiles.map(c=>pretty(c.name)).join(" / "), start:b.start, frames:b.frames}));
  }else if(type==="reference"){
    path=r.file; poster=r.poster; rate=r.fps;
    chapters=[{name:pretty(r.action),start:0,frames:r.frames}];
  }else{
    path=a.video; poster=a.poster; rate=a.fps;
    chapters=a.actions.map(c=>({...c,name:pretty(c.name)}));
  }
  chapter.replaceChildren();
  chapters.forEach((c,i)=>add(chapter,String(i),clock(c.start/rate)+" · "+c.name));
  if(type==="actions" && search.value.trim()){
    const hit=chapters.findIndex(c=>normalized(c.name).includes(normalized(search.value.trim())));
    if(hit>=0)chapter.selectedIndex=hit;
  }
  byId("title").textContent=type==="overview" ? "Action overview" : pretty(actor);
  byId("meta").textContent=type==="overview" ? boards.length+" groups" : pretty(a.family)+" · "+pretty(a.kind);
  byId("clipLink").href=path;
  for(const key of ["glb","native"]){
    const el=byId(key);el.hidden=type==="overview" || !a[key]; if(a[key])el.href=a[key];
  }
  player.pause();
  pending=null;
  if(path!==activePath){
    activePath=path;
    if(poster)player.poster=poster;else player.removeAttribute("poster");
    player.src=path;player.load();
  }
  selectTime(false);
}
function loadActor(){
  const previous=mode.value;
  actor=rig.value;
  mode.replaceChildren();
  const empty=!actor;
  [mode,chapter,byId("jump"),byId("loop")].forEach(el=>el.disabled=empty);
  if(empty){
    chapters=[];chapter.replaceChildren();player.pause();pending=null;activePath="";
    player.removeAttribute("src");player.removeAttribute("poster");player.load();
    byId("title").textContent="No matching rigs";byId("meta").textContent="";
    for(const id of ["clipLink","glb","native"])byId(id).hidden=true;
    return;
  }
  byId("clipLink").hidden=false;
  add(mode,"actions","Action library");
  if(references[actor])add(mode,"reference","Original reference");
  add(mode,"overview","Action overview");
  if([...mode.options].some(o=>o.value===previous))mode.value=previous;
  loadReel();
}
const normalized=s=>s.toLowerCase().replace(/[-_]/g," ").trim();
function filter(){
  const query=normalized(search.value);
  const matches=names.filter(n=>[n, actors[n].family,actors[n].kind,...actors[n].actions.map(c=>c.name),
    references[n]?.action || ""].some(s=>normalized(s).includes(query)));
  rig.replaceChildren();
  matches.forEach(n=>add(rig,n,pretty(n)));
  if(matches.includes(actor))rig.value=actor;else if(matches.length)rig.selectedIndex=0;
  byId("matches").textContent=matches.length+" / "+names.length+" rigs";
  loadActor();
}
function enforceLoop(){
  if(!byId("loop").checked || player.paused || player.seeking)return;
  const c=chapters[chapter.selectedIndex];
  if(c && player.currentTime>=(c.start+c.frames)/rate)selectTime(true);
}
player.addEventListener("timeupdate",enforceLoop);
player.addEventListener("ended",()=>{if(byId("loop").checked)selectTime(true)});
chapter.addEventListener("change",()=>selectTime(!player.paused));
byId("jump").addEventListener("click",()=>selectTime(true));
search.addEventListener("input",filter);rig.addEventListener("change",loadActor);mode.addEventListener("change",loadReel);
filter();
</script></body></html>"""
