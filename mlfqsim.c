/*
 * Minimal MLFQ simulator (3 levels: L0/L1/L2) for teaching
 * --------------------------------------------------------
 * This small program simulates a Multi-Level Feedback Queue (MLFQ) scheduler.
 * It is intentionally tiny and focuses only on the core scheduling mechanics:
 *
 *   - There are 3 queues (highest to lowest): L0, L1, L2
 *   - Each queue has a round-robin time slice (aka quantum):
 *       L0: 1 tick, L1: 2 ticks, L2: 4 ticks
 *   - A process starts in L0; if it consumes its whole slice, it is demoted
 *     to the next lower queue (L0→L1, L1→L2). L2 never demotes further.
 *   - If a process does not finish within a tick, it is re-enqueued at the
 *     tail of its current queue (round-robin).
 *   - A process exits the system when its CPU work budget reaches zero or less.
 *
 * Output format (consumed by o1viz.py with --mode=mlfq):
 *   Process <name> <pid> has consumed 10 ms in L<level>
 *   Process <name> <pid> EXIT
 *
 * Build: gcc -O2 -Wall -Wextra -std=c11 -o mlfqsim mlfqsim.c
 * Run:   ./mlfqsim "spin 10000 &; spin 200000 &; spin 3000000 &;"
 *
 * Mapping to xv6:
 *   - Think of L0/L1/L2 as separate run queues stored in proc.c
 *   - The per-proc field ticks_left matches each level's quantum
 *   - On timer interrupt, decrement ticks_left; if it hits 0, perform RR/demotion
 *   - The scheduler always prefers the highest non-empty queue first
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// A minimal process structure that mirrors just what we need for scheduling.
// In xv6, this would be part of struct proc and include many more fields.
typedef struct proc proc_t;
struct proc {
  int pid;             // Process ID (monotonic counter here)
  char name[32];       // Short name (e.g., "spin")
  int work_left;       // Remaining CPU work in milliseconds
  int ticks_left;      // Remaining ticks in the current quantum for this level
  int level;           // Which MLFQ level the process is in (0/1/2)
  proc_t *next;        // Intrusive next pointer for O(1) queues
};

// A simple FIFO queue (O(1) push/pop) implemented with intrusive links above.
typedef struct { proc_t *head, *tail; } queue_t;

// Each tick is 10ms to keep numbers readable. The visualizer assumes this
// when converting tick counts to milliseconds in the timeline.
#define TICK_MS 10

// Per-level time quantums (in ticks). You can play with these values during
// lecture to show how latency and throughput change.
#define Q_L0 1
#define Q_L1 2
#define Q_L2 4

static queue_t L0={0}, L1={0}, L2={0}; // Highest priority first
static int next_pid=1;                 // Simple PID allocator

// Enqueue a process at the tail in O(1) time.
static void q_push(queue_t *q, proc_t *p){
  p->next=NULL;
  if(!q->head){ q->head=q->tail=p; }
  else { q->tail->next=p; q->tail=p; }
}

// Pop the head in O(1) time.
static proc_t* q_pop(queue_t *q){
  proc_t* p=q->head;
  if(!p) return NULL;
  q->head=p->next;
  if(!q->head) q->tail=NULL;
  p->next=NULL;
  return p;
}

// Helper to check the command name; illustrative here (not strictly needed).
static bool is_spin(const char *s){return strncmp(s,"spin",4)==0;}

// Create a new process starting at L0 with L0's quantum.
static proc_t* new_proc(const char*name,int ms){
  proc_t *p=calloc(1,sizeof(*p));
  p->pid=next_pid++;
  snprintf(p->name,sizeof(p->name),"%s",name);
  p->work_left=ms;
  p->level=0;             // start at top level
  p->ticks_left=Q_L0;     // initialize its quantum
  q_push(&L0,p);
  return p;
}

// Parse a tiny subset of shell-like input to create spin processes.
// Example accepted input: "spin 10000 &; spin 200000 &; spin 3000000 &;"
// We ignore separators like '&' and ';' and only look for: spin <integer>
static void userinit_spin(const char *cmd){
  const char *s=cmd;
  while(*s){
    // Skip whitespace and separators
    while(*s==' '||*s=='\t'||*s==';'||*s=='&') s++;
    if(!*s) break;

    // Recognize the command name
    if(strncmp(s,"spin",4)==0){
      s += 4;
      while(*s==' '||*s=='\t') s++;
      // Parse decimal integer for work in ms
      int ms = 0;
      while(*s>='0'&&*s<='9') { ms = ms*10 + (*s-'0'); s++; }
      if(ms>0) new_proc("spin", ms);
    }

    // Skip to next separator
    while(*s && *s!=';') s++;
    if(*s==';') s++;
  }
}

// Book-keeping for one tick of CPU time: decrease remaining work and quantum,
// and print a line the visualizer will parse.
static void on_tick(proc_t *p){
  p->work_left -= TICK_MS;
  p->ticks_left -= 1;
  printf("Process %s %d has consumed %d ms in L%d\n", p->name, p->pid, TICK_MS, p->level);
}

// Free a process and announce exit. In a real OS you'd transition to ZOMBIE
// and reap later; here we just free immediately after logging.
static void proc_exit(proc_t *p){
  printf("Process %s %d EXIT\n", p->name, p->pid);
  free(p);
}

// Run exactly one tick of CPU time:
//   1) Pick from highest non-empty queue (L0 -> L1 -> L2)
//   2) Ensure the process has a non-zero quantum for its current level
//   3) Account for the tick (reduce work/ticks_left and print a log line)
//   4) If finished, EXIT; otherwise re-enqueue (RR) and demote if slice expired
static void schedule_one_tick(void){
  proc_t *p=NULL; int qid=-1;

  // 1) Highest non-empty queue first
  if(L0.head){ p=q_pop(&L0); qid=0; p->ticks_left = p->ticks_left ? p->ticks_left : Q_L0; }
  else if(L1.head){ p=q_pop(&L1); qid=1; p->ticks_left = p->ticks_left ? p->ticks_left : Q_L1; }
  else if(L2.head){ p=q_pop(&L2); qid=2; p->ticks_left = p->ticks_left ? p->ticks_left : Q_L2; }
  else {
    // No runnable process this tick (all done or waiting)
    printf("Process idle 0 has consumed %d ms in IDLE\n", TICK_MS);
    return;
  }

  // 3) Run for one tick
  on_tick(p);

  // 4) Finished? Exit early.
  if(p->work_left<=0){ proc_exit(p); return; }

  // Otherwise, perform RR and demotion as needed.
  if(qid==0){ // L0
    if(p->ticks_left>0){
      // Still has slice: stay in L0, RR to tail
      q_push(&L0,p);
    } else {
      // Slice expired: demote to L1 with fresh L1 slice
      p->level=1; p->ticks_left=Q_L1; q_push(&L1,p);
    }
  } else if(qid==1){ // L1
    if(p->ticks_left>0){
      q_push(&L1,p);
    } else {
      p->level=2; p->ticks_left=Q_L2; q_push(&L2,p);
    }
  } else { // L2
    if(p->ticks_left>0){
      // RR within L2
      q_push(&L2,p);
    } else {
      // L2 never demotes further; just refresh its L2 quantum
      p->ticks_left=Q_L2; q_push(&L2,p);
    }
  }
}

int main(int argc, char **argv){
  // Accept a single string argument that contains a mini command list, e.g.:
  //   "spin 10000 &; spin 200000 &; spin 3000000 &;"
  const char *cmdline = (argc>=2)? argv[1] : "spin 10000 &; spin 200000 &; spin 3000000 &;";
  userinit_spin(cmdline);

  // A simple termination policy: if there are no runnable processes for more
  // than ~10 ticks in a row, we exit. There's also a hard cap on total ticks
  // to avoid accidental infinite loops while experimenting.
  int idle=0, ticks=0;
  while(1){
    if(ticks>100000) break; // safety cap

    if(!L0.head && !L1.head && !L2.head){
      idle++; ticks++;
      if(idle>10) break; // all done
      printf("Process idle 0 has consumed %d ms in IDLE\n", TICK_MS);
      continue;
    }

    idle=0; ticks++;
    schedule_one_tick();
  }
  return 0;
}
