CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11

all: o1sim_skeleton mlfqsim

o1sim_skeleton: o1sim_skeleton.c
	$(CC) $(CFLAGS) -o $@ $<

mlfqsim: mlfqsim.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f o1sim_skeleton mlfqsim *.o *.png *.gif

.PHONY: visualize-o1 visualize-mlfq

visualize-o1: o1sim_skeleton o1viz.py
	./o1viz.py --bin ./o1sim_skeleton --src o1sim_skeleton.c --mode o1 --max-ms 500 --out-gantt o1_timeline_500ms.png --out-queues o1_queues_500ms.gif

visualize-mlfq: mlfqsim o1viz.py
	./o1viz.py --bin ./mlfqsim --src mlfqsim.c --mode mlfq --max-ms 500 --out-gantt mlfq_timeline_500ms.png --out-queues mlfq_queues_500ms.gif
