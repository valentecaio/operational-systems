
typedef struct node {
  struct node *next;
  int data;
} Node;

typedef struct {
  Node *first;
  Node *last;
} Fifo;

// returns an empty fifo queue
Fifo fifo_create();

// is the queue empty?
int fifo_empty(Fifo *f);

void fifo_print(Fifo *f);

// add an element to the end of the queue
void fifo_put(Fifo *f, int value);

// remove and return the first element of the queue
// returns -1 if the queue is empty
int fifo_take(Fifo *f);

// free all nodes from the queue
void fifo_free(Fifo *f);
