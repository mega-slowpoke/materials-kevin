#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

int INITIAL_SIZE = 6;

typedef struct {
    int *arr;
    int headIdx;
    int nextInsertIdx;
    int size;
    int capacity;
} arrayQueue;

arrayQueue* create(void) {
    // TODO: 
    arrayQueue* q = malloc(sizeof(arrayQueue));
    q->capacity = INITIAL_SIZE;
    q->arr = malloc(sizeof(int) * q->capacity);
    q->headIdx = 0;
    q->nextInsertIdx = 0;
    q->size = 0;
    return q;
}

void enqueue(arrayQueue* q, int val) {
    if (q->nextInsertIdx >= q->capacity) {
        int new_capacity = q->capacity*2;
        int* new_arr = malloc(sizeof(int) * new_capacity);
         // TODO: 
        memcpy(new_arr, q->arr, sizeof(int) * q->capacity); 
        free(q->arr);
        q->arr = new_arr;
        q->capacity = new_capacity;
    }
     // TODO: 
    q->arr[q->nextInsertIdx] = val;
    q->nextInsertIdx = (q->nextInsertIdx + 1) % q->capacity;
    q->size++;
}

int dequeue(arrayQueue* q) {
    if (q->size == 0) {
        return INT_MIN;
    }
    int temp = q->arr[q->headIdx];
    q->headIdx = (q->headIdx + 1) % q->capacity;
    q->size--;
    return temp;
}

int front(arrayQueue* q) {
    if (q->size == 0) {
        return INT_MIN;
    }
    return q->arr[q->headIdx];
}

int rear(arrayQueue* q) {
    if (q->size == 0) {
        return INT_MIN;
    }
    int idx = (q->nextInsertIdx - 1 + q->capacity) % q->capacity;
    return q->arr[idx]; //AI revised
}

int size(arrayQueue* q) {
    return q->size;
}