# Student Pack: xv6 Scheduling Project Helpers

This folder contains the minimal materials you can share with students:

Contents
- o1sim_skeleton.c — Simplified O(1) scheduler skeleton (with TODOs and hints)
- mlfqsim.c — Complete 3-level MLFQ simulator, a stepping stone to O(1)
- o1viz.py — Visualizer for simulator output (timeline + queue GIF)
- Makefile — Builds both simulators
- README.md — This guide, plus running instructions and mapping tips
 - examples/ — Pre-generated 500ms visuals for O(1) and MLFQ

How to build

```
make
```

How to run

- O(1) skeleton (will produce limited output until TODOs are filled):
```
./o1sim_skeleton "spin 10000 &; spin 200000 &; spin 3000000 &;"
```
- MLFQ simulator:
```
./mlfqsim "spin 10000 &; spin 200000 &; spin 3000000 &;"
```
- Visualize (500 ms):
```
# For O(1) skeleton
python3 o1viz.py --bin ./o1sim_skeleton --src ./o1sim_skeleton.c --mode o1 --max-ms 500 \
  --cmd "spin 10000 &; spin 200000 &; spin 3000000 &;" --out-gantt o1_timeline_500ms.png --out-queues o1_queues_500ms.gif

# For MLFQ sim
python3 o1viz.py --bin ./mlfqsim --src ./mlfqsim.c --mode mlfq --max-ms 500 \
  --cmd "spin 10000 &; spin 200000 &; spin 3000000 &;" --out-gantt mlfq_timeline_500ms.png --out-queues mlfq_queues_500ms.gif
```

Pre-generated visuals
- See the `examples/` folder for ready-made outputs: `o1_timeline_500ms.png`, `o1_queues_500ms.gif`, `mlfq_timeline_500ms.png`, `mlfq_queues_500ms.gif`.

Mapping to xv6
- Add to `struct proc` (proc.h): ticks_left, qnext, in_queue
- Create O(1) queues in proc.c: FQ, AQ, EQ with O(1) push/pop
- Initialize new procs into FQ in allocproc()
- In timer interrupt (trap.c), decrement ticks and yield()
- In yield() (proc.c), re-enqueue based on queue/quantum status
- In scheduler() (proc.c), pick from FQ else AQ; swap AQ<->EQ when AQ empty and EQ non-empty

Notes
- The visualizer accepts both the O(1) and MLFQ formats and maps to FQ/AQ/EQ for comparison.
- Emit lines like `Process spin 12 has consumed 10 ms in FQ` per tick; EXIT lines are optional but recommended.
