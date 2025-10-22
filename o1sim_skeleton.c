// Simplified O(1)-style scheduler simulator skeleton for students
// Build: gcc -O2 -Wall -Wextra -o o1sim_skeleton o1sim_skeleton.c
// Run:   ./o1sim_skeleton "spin 10000 &; spin 200000 &; spin 3000000 &;"
// Output lines are parsed by o1viz.py. Keep the format stable.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Ticks are 10ms each to keep numbers small
#define TICK_MS 10

// Quantum per queue (in ticks)
#define FQ_Q 1   // 1 tick = 10ms
#define AQ_Q 3   // 3 ticks = 30ms
#define EQ_Q 3   // 3 ticks = 30ms

// Intrusive single-linked queue used for FQ/AQ/EQ (like xv6's run queue list)
typedef struct proc proc_t;
struct proc {
  int pid;
  char name[32];
  int work_left;     // total ms of CPU work left
  int ticks_left;    // ticks left in current time slice
  const char *in_queue; // "FQ" | "AQ" | "EQ" | NULL
  proc_t *next;
};

typedef struct { proc_t *head, *tail; } queue_t;

static queue_t FQ={0}, AQ={0}, EQ={0};
static int next_pid=1;

// Queue helpers (students fill these two)
static void q_push(queue_t *q, proc_t *p) {
  // TODO: enqueue p at tail in O(1)
  // Hints: if queue empty, head=tail=p; else tail->next=p; tail=p; p->next=NULL
}

static proc_t* q_pop(queue_t *q) {
  // TODO: pop from head in O(1)
  // Hints: remove head; if becomes empty set tail=NULL; return removed proc
  return NULL;
}

static proc_t* new_proc(const char *name, int work_ms) {
  proc_t *p = (proc_t*)calloc(1, sizeof(proc_t));
  p->pid = next_pid++;
  snprintf(p->name, sizeof(p->name), "%s", name);
  p->work_left = work_ms;
  p->ticks_left = FQ_Q; // start in FQ
  p->in_queue = "FQ";
  q_push(&FQ, p);
  return p;
}

static bool is_spin(const char *name) { return strncmp(name, "spin", 4)==0; }

// Simple shell-ish command parser for: "spin 100 &; spin 200 &;" style
static void userinit_spin(const char *cmd) {
  // TODO: parse tokens and create spin procs with work_ms from integers
  // Suggested minimal implementation sufficient for provided examples:
  // - Scan for "spin" then read next integer as work_ms; ignore '&;' separators.
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

static void proc_exit(proc_t *p) {
  printf("Process %s %d EXIT\n", p->name, p->pid);
  free(p);
}

static void maybe_swap_queues(void) {
  // TODO: O(1) trick: if AQ empty and EQ non-empty, swap their identities
  // Hints: swap the queue_t structs (head/tail) so next picks come from old EQ
}

static void on_tick_run(const char *qname, proc_t *p) {
  p->work_left -= TICK_MS;
  p->ticks_left -= 1;
  printf("Process %s %d has consumed %d ms in %s\n", p->name, p->pid, TICK_MS, qname);
}

static void schedule_one_tick(void) {
  // TODO: choose a process to run for one tick, and manage queue transitions.
  // Policy:
  // 1) Always prefer FQ, else AQ, else EQ (after maybe_swap_queues()).
  // 2) When a process runs 1 tick in FQ, move it to AQ with ticks_left=AQ_Q.
  // 3) In AQ, round-robin with quantum AQ_Q. On expiry, demote to EQ.
  // 4) In EQ, round-robin with quantum EQ_Q (no lower level).
  // 5) If work_left <= 0, EXIT and do not requeue.
}

int main(int argc, char **argv) {
  const char *cmdline = (argc>=2)? argv[1] : "spin 10000 &; spin 200000 &; spin 3000000 &;";
  userinit_spin(cmdline);

  int idle_ticks = 0; int total_ticks = 0;
  while (1) {
    // stop after a reasonable limit to avoid infinite runs when no procs
    if (total_ticks > 100000) break;
    if (!FQ.head && !AQ.head && !EQ.head) {
      idle_ticks++; total_ticks++;
      if (idle_ticks > 10) break; // all done
      printf("Process idle 0 has consumed %d ms in IDLE\n", TICK_MS);
      continue;
    }
    idle_ticks = 0; total_ticks++;
    maybe_swap_queues();
    schedule_one_tick();
  }
  return 0;
}
