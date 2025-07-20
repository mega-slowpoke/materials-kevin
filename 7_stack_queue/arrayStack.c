#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

int INITIAL_SIZE = 6;

typedef struct {
    int *arr;
    int size;
    int nextInsertIdx;
    int capacity;
} arrayStack;

arrayStack *createArrayStack(void) {
    arrayStack *q = malloc(sizeof(*q));
    q->capacity = INITIAL_SIZE;
    q->arr = malloc(sizeof(int) * q->capacity);
    q->size = 0;
    q->nextInsertIdx = 0;
}

void push(arrayStack *stack, int val) {
    if (stack->nextInsertIdx >= stack->capacity) {
        int new_capacity = stack->capacity * 2;
        int *new_arr = malloc(sizeof(int) * new_capacity);
        memcpy(new_arr, stack->arr, sizeof(int) * stack->capacity); 
        //AI, a more efficient way than iterating
        free(stack->arr);
        stack->arr = new_arr;
        stack->capacity = new_capacity;
    }
    stack->arr[stack->nextInsertIdx++] = val;
    stack->nextInsertIdx++;
    stack->size++;
}

int pop(arrayStack *stack) {
    if (stack->size == 0) {
        return INT_MIN; //AI,哨兵值，调用者需检查
    }
    int temp = stack->arr[stack->nextInsertIdx - 1];
    stack->nextInsertIdx--;
    stack->size--;
    return temp;
}

int top(arrayStack *stack) {
    if (stack->size == 0) {
        return INT_MIN;
    }
    return stack->arr[stack->nextInsertIdx-1];
}

int size(arrayStack *stack) {
    return stack->size;
}