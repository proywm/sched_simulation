#!/usr/bin/env python3
# Copied from o1-scheduler-sim/o1viz.py (supports --mode o1|mlfq)
# Minimal dependencies: matplotlib, pillow (for GIF). Optional: numpy<2.

import argparse, json, os, re, subprocess, sys
from dataclasses import dataclass
from typing import List, Optional, Tuple, Dict

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

TICK_MS_DEFAULT = 10

JSON_RECOMMENDED = """
(Optional) Add a JSON line per tick in your C code for richer parsing, e.g.:
printf("{\"t\":%d,\"pid\":%d,\"name\":\"%s\",\"queue\":\"%s\",\"work_left\":%d,\"ticks_left\":%d}\n",
       tick, p->pid, p->name, qname, p->work_left, p->ticks_left);
"""

HUMAN_LINE = re.compile(r"Process\s+(?P<name>\S+)\s+(?P<pid>\d+)\s+has\s+consumed\s+(?P<ms>\d+)\s+ms\s+in\s+(?P<queue>\S+)", re.IGNORECASE)
EXIT_LINE = re.compile(r"Process\s+(?P<name>\S+)\s+(?P<pid>\d+)\s+EXIT", re.IGNORECASE)

@dataclass
class TickEvent:
    t: int
    pid: int
    name: str
    queue: str
    ms: int
    work_left: Optional[int] = None
    ticks_left: Optional[int] = None

def parse_stdout(stdout: str, tick_ms: int, mode: str = "o1") -> Tuple[List[TickEvent], Dict[int,int]]:
    events: List[TickEvent] = []
    t = 0
    exit_tick: Dict[int,int] = {}
    def map_queue(q: str) -> str:
        q = q.upper()
        if mode == "mlfq":
            if q in ("L0","HIGH","Q0"): return "FQ"
            if q in ("L1","MID","Q1"): return "AQ"
            if q in ("L2","LOW","Q2"): return "EQ"
        return q
    for line in stdout.splitlines():
        line = line.strip()
        if not line: continue
        if line.startswith("{") and line.endswith("}"):
            try:
                obj = json.loads(line)
                qname = map_queue(str(obj.get("queue","FQ")))
                events.append(TickEvent(
                    t=int(obj.get("t", t)),
                    pid=int(obj["pid"]),
                    name=str(obj["name"]),
                    queue=qname,
                    ms=int(obj.get("ms", tick_ms)),
                    work_left=obj.get("work_left"),
                    ticks_left=obj.get("ticks_left"),
                ))
                t = events[-1].t + 1
                continue
            except Exception:
                pass
        m_exit = EXIT_LINE.search(line)
        if m_exit:
            try:
                pid = int(m_exit.group("pid"))
                exit_tick[pid] = t
            except Exception:
                pass
            continue
        m = HUMAN_LINE.search(line)
        if m:
            pid = int(m.group("pid"))
            name = m.group("name")
            queue = map_queue(m.group("queue"))
            ms = int(m.group("ms"))
            events.append(TickEvent(t=t, pid=pid, name=name, queue=queue, ms=ms))
            t += 1
    return events, exit_tick

# Gantt timeline
def make_gantt(events: List[TickEvent], out_path: str):
    if not events: raise SystemExit("No events parsed. " + JSON_RECOMMENDED)
    used = [e for e in events if e.queue != "IDLE"]
    if not used: raise SystemExit("Parsed only IDLE events. " + JSON_RECOMMENDED)
    by_pid: Dict[int, List[TickEvent]] = {}
    for e in used: by_pid.setdefault(e.pid, []).append(e)
    pids = sorted(by_pid.keys())
    labels = {pid: f"{by_pid[pid][0].name} ({pid})" for pid in pids}
    fig, ax = plt.subplots(figsize=(12,4))
    ymap = {pid:i for i,pid in enumerate(pids)}; yticks=[]; yticklabels=[]
    for pid in pids:
        seq = by_pid[pid]
        groups=[]; last_q=None; start=None; last=None
        for e in seq:
            if last_q==e.queue and last==e.t-1: last=e.t
            else:
                if last_q is not None: groups.append((last_q,start,last+1))
                last_q=e.queue; start=e.t; last=e.t
        if last_q is not None: groups.append((last_q,start,last+1))
        y=ymap[pid]*10; yticks.append(y+4); yticklabels.append(labels[pid])
        for q,ts,te in groups:
            start_ms = ts * events[0].ms
            width_ms = (te-ts) * events[0].ms
            hatch = {"FQ":"","AQ":"//","EQ":"xx"}.get(q,"")
            coll = ax.broken_barh([(start_ms,width_ms)], (y,8), facecolors="tab:blue", edgecolors="black")
            if hatch:
                try: coll.set_hatch(hatch)
                except Exception: pass
    ax.set_ylim(0,(len(pids)+1)*10)
    ax.set_xlim(0,(max(e.t for e in used)+1)*events[0].ms)
    ax.set_yticks(yticks); ax.set_yticklabels(yticklabels)
    ax.set_xlabel("Time (ms)"); ax.set_title("Scheduler Timeline (FQ=solid, AQ=//, EQ=xx)")
    import matplotlib.patches as mpatches
    ax.legend(handles=[mpatches.Patch(label="FQ (solid)"), mpatches.Patch(hatch="//", label="AQ (//)"), mpatches.Patch(hatch="xx", label="EQ (xx)")], loc="upper right")
    plt.tight_layout(); plt.savefig(out_path, dpi=150); plt.close(fig)

# Queue animation (rows)
def make_queue_animation(events: List[TickEvent], out_path: str, max_frames: int = 600, exit_tick: Optional[Dict[int,int]] = None):
    from collections import deque, defaultdict
    if exit_tick is None: exit_tick={}
    # Names
    pid_names: Dict[int,str]={}
    for e in events:
        if e.queue!="IDLE": pid_names[e.pid]=e.name
    # Queues
    FQ, AQ, EQ = deque(), deque(), deque()
    seen=set()
    for e in events:
        if e.queue=="FQ" and e.pid not in seen: FQ.append(e.pid); seen.add(e.pid)
    for e in events:
        if e.queue=="AQ" and e.pid not in seen: AQ.append(e.pid); seen.add(e.pid)
    for e in events:
        if e.queue=="EQ" and e.pid not in seen: EQ.append(e.pid); seen.add(e.pid)

    def purge_all(dq, pid):
        if not dq: return
        filtered=[x for x in dq if x!=pid]; dq.clear(); dq.extend(filtered)
    def dedupe_all():
        s=set()
        def dedupe(dq):
            out=[]
            for x in dq:
                if x in s: continue
                s.add(x); out.append(x)
            dq.clear(); dq.extend(out)
        dedupe(FQ); dedupe(AQ); dedupe(EQ)

    frames=[]; tick=0; streak=defaultdict(int); exited=set();
    for e in events[:max_frames]:
        dedupe_all()
        frames.append((tick, list(FQ), list(AQ), list(EQ), e.pid if e.queue!="IDLE" else None, e.queue))
        tick+=1
        if e.queue=="IDLE": continue
        if e.queue=="AQ" and not AQ and EQ: AQ, EQ = EQ, AQ
        # Seed first time
        # Remove pid everywhere then apply movement
        purge_all(FQ,e.pid); purge_all(AQ,e.pid); purge_all(EQ,e.pid)
        if e.pid in exit_tick and exit_tick[e.pid] <= tick: # finished
            continue
        if e.queue=="FQ":
            streak[e.pid]=0; AQ.append(e.pid)
        elif e.queue=="AQ":
            streak[e.pid]+=1
            if streak[e.pid] >= 3: streak[e.pid]=0; EQ.append(e.pid)
            else: AQ.append(e.pid)
        else: # EQ
            streak[e.pid]+=1; EQ.append(e.pid)
        dedupe_all()
        # purge any exited by this tick
        for pid, xt in list(exit_tick.items()):
            if xt <= tick:
                purge_all(FQ,pid); purge_all(AQ,pid); purge_all(EQ,pid)

    # Draw
    import matplotlib.patches as patches
    import matplotlib.cm as cm
    palette = cm.get_cmap('tab20')
    pid_list = sorted(pid_names.keys()); pid_color={pid: palette(i % palette.N) for i,pid in enumerate(pid_list)}
    fig, ax = plt.subplots(figsize=(9,4.5))
    def draw_row(y, items, running_pid=None, label="", max_items=14):
        ax.text(-1.0, y+0.5, label, ha="right", va="center", fontsize=10, fontweight='bold')
        for i,pid in enumerate(items[:max_items]):
            face = pid_color.get(pid, (0.9,0.9,0.9,1.0))
            edge = 'red' if (running_pid is not None and pid==running_pid) else 'black'
            lw = 2.5 if edge=='red' else 1.0
            rect = patches.FancyBboxPatch((i,y),0.9,0.8, boxstyle="round,pad=0.2", linewidth=lw, edgecolor=edge, facecolor=face)
            ax.add_patch(rect)
            ax.text(i+0.45, y+0.4, pid_names.get(pid, f"P{pid}"), ha="center", va="center", fontsize=9)
    def draw(frame):
        tick, fq, aq, eq, running_pid, qname = frame
        ax.clear(); ax.set_xlim(-1.2, 15.0); ax.set_ylim(-0.2, 4.2); ax.axis('off')
        title=f"Tick {tick}"; title += (f"  CPU: {pid_names.get(running_pid, f'P{running_pid}')} ({qname})" if running_pid is not None else "  CPU: idle")
        ax.set_title(title)
        draw_row(3, [running_pid] if running_pid is not None else [], running_pid=running_pid, label="CPU")
        draw_row(2, fq, running_pid=running_pid, label="FQ")
        draw_row(1, aq, running_pid=running_pid, label="AQ")
        draw_row(0, eq, running_pid=running_pid, label="EQ")
    ani = FuncAnimation(fig, draw, frames=frames, interval=350, repeat=False)
    ani.save(out_path, writer="pillow", fps=2)
    plt.close(fig)

def try_build(binary: str, c_file: str, extra_cflags: List[str]):
    if os.path.exists("Makefile"):
        print("[o1viz] Running make...")
        try:
            subprocess.check_call(["make"])
            if not os.path.exists(binary):
                print(f"[o1viz] make finished, but '{binary}' not found. Will try gcc fallback.")
            else:
                return
        except Exception as e:
            print("[o1viz] make failed, will try gcc fallback:", e)
    print("[o1viz] Building with gcc fallback...")
    cmd=["gcc","-O2","-Wall","-Wextra","-o",binary,c_file]+extra_cflags
    subprocess.check_call(cmd)

def run_program(binary: str, cmdline: str) -> str:
    print(f"[o1viz] Running: {binary} {cmdline!r}")
    proc = subprocess.run([binary, cmdline], capture_output=True, text=True, check=True)
    return proc.stdout

def main():
    ap = argparse.ArgumentParser(description="Visualizer for xv6-like scheduler sims")
    ap.add_argument("--bin", default="./o1sim_skeleton")
    ap.add_argument("--src", default="o1sim_skeleton.c")
    ap.add_argument("--tick-ms", type=int, default=TICK_MS_DEFAULT)
    ap.add_argument("--cmd", default="spin 10000 &; spin 200000 &; spin 3000000 &;")
    ap.add_argument("--out-gantt", default="o1_timeline.png")
    ap.add_argument("--out-queues", default="o1_queues.gif")
    ap.add_argument("--cflags", nargs="*", default=[])
    ap.add_argument("--max-ms", type=int, default=None)
    ap.add_argument("--max-ticks", type=int, default=None)
    ap.add_argument("--mode", choices=["o1","mlfq"], default="o1")
    args = ap.parse_args()

    try:
        try_build(args.bin, args.src, args.cflags)
    except Exception as e:
        print("[o1viz] Build failed:", e)
        sys.exit(1)

    try:
        stdout = run_program(args.bin, args.cmd)
    except subprocess.CalledProcessError as e:
        print("[o1viz] Program run failed:\n", e.stdout, e.stderr)
        sys.exit(1)

    events, exit_tick = parse_stdout(stdout, args.tick_ms, mode=args.mode)
    if not events:
        print("[o1viz] No events parsed from stdout.")
        print("Expected lines like: Process spin 1 has consumed 10 ms in FQ")
        print(JSON_RECOMMENDED); sys.exit(1)

    max_ticks=None
    if args.max_ticks is not None: max_ticks=max(0,int(args.max_ticks))
    elif args.max_ms is not None: max_ticks=max(0,int(args.max_ms//args.tick_ms))
    if max_ticks is not None and len(events)>max_ticks:
        events=events[:max_ticks]
        print(f"[o1viz] Trimmed to first {max_ticks} ticks (~{max_ticks*args.tick_ms} ms)")

    print(f"[o1viz] Parsed {len(events)} tick events.")
    make_gantt(events, args.out_gantt); print(f"[o1viz] Wrote {args.out_gantt}")
    make_queue_animation(events, args.out_queues, exit_tick=exit_tick); print(f"[o1viz] Wrote {args.out_queues}")

if __name__ == "__main__":
    main()
