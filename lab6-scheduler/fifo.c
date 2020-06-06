/*
  Simple FIFO queue example from
  https://gist.github.com/ryankurte/61f95dc71133561ed055ff62b33585f8#file-safe_queue-c
*/

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "fifo.h"

Fifo fifo_create() {
  return (Fifo) {NULL, NULL};
}

int fifo_empty(Fifo *f) {
  return f->first == NULL ? 1 : 0;
}

void fifo_print(Fifo *f) {
  Node *p;
  if(fifo_empty(f)){
    printf("[empty]");
  } else {
    printf("[");
    for(p = f->first; p->next != NULL; p = p->next) {
      printf("%d, ", p->data );
    }
    printf("%d]", p->data);
  }
}

void fifo_put(Fifo *f, int value) {
  // create new fifo node
  Node* new = (Node*) malloc(sizeof(Node));
  *new = (Node) {NULL, value};

  if(fifo_empty(f)) {
    // we are inserting the first node of queue
    f->first = new;
  } else {
    // insert new node after last node
    f->last->next = new;
  }
  // in any case, the new node must be the last node of the fifo
  f->last = new;
}


int fifo_take(Fifo *f) {
  if(fifo_empty(f)) {
    // impossible to take data from an empty queue
    return -1;
  }

  // remove first node from fifo and return it
  Node temp = *(f->first);
  free(f->first);
  f->first = temp.next;
  return temp.data;
}

void fifo_free(Fifo *f) {
  while(fifo_take(f) != -1);
}

// used to test
void fifo_test() {
  int x;
  Fifo f = fifo_create();
  fifo_print(&f);
  printf("\n");

  for(int i=1; i<5; i++) {
    printf("Adding %d\n", i);
    fifo_put(&f, i);
    fifo_print(&f);
    printf("\n");
  }

  for(int i=1; i<6; i++) {
    printf("Taking %d\n", i);
    x = fifo_take(&f);
    printf("Took %d\n", x);
    fifo_print(&f);
    printf("\n");
  }
}
