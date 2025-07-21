#include <stdio.h>
#include <stdlib.h>


typedef struct QueueNode {
    int val;
    QueueNode* next;
} QueueNode;


typedef struct Queue {
    QueueNode* dummyhead;
    QueueNode* tail;
    int size;
} Queue;



Queue* queueCreate() {
    Queue* queue = (Queue*) malloc(sizeof(Queue));
    queue->dummyhead = malloc(sizeof(QueueNode));
    queue->dummyhead->next = NULL;
    queue->dummyhead->val = -99999;
    queue->tail = queue->dummyhead;
    queue->size = 0;
    return queue;
}


void enqueue(Queue* queue, int x) { 
    // 创建new_node
    // new_node->next == NULL
    // 原tail->new_node
    // size++;
    QueueNode* new_node = malloc(sizeof(QueueNode));
    new_node->next = NULL;
    new_node->val = x;
    queue->tail->next = new_node;
    queue->tail = new_node;
    queue->size++;
}

int dequeue(Queue* queue) {
    if (queue->size == 0) {
        perror("queue is empty");
        return;
    }

    QueueNode* first = queue->dummyhead->next;
    int x = first->val;
    queue->dummyhead->next = queue->dummyhead->next->next;
    if (queue->size == 1) {
        queue->tail = queue->dummyhead;
    }
    free(first);
    queue->size--;
    return x;
}

int front(Queue* queue) {
    if (queue->size == 0) {
        perror("queue is empty");
        return INT_MIN;
    }

    return queue->dummyhead->next->val;
}

int rear(Queue* queue) {
    if (queue->size == 0) {
        perror("queue is empty");
        // 返回最小值代表队列为空
        return INT_MIN;
    }

    return queue->tail->val;
}

int size(Queue* queue) { 
    return queue->size;
}


