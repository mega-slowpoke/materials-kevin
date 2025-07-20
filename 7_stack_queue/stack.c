#include <stdio.h>
#include <stdlib.h>

typedef struct StackNode {
    int val;
    StackNode* next;
} StackNode;

typedef struct Stack {
    StackNode* dummyhead;
    int size;
} Stack;


Stack* createStack() {
    Stack* stack = (Stack*)malloc(sizeof(Stack));
    stack->dummyhead = (StackNode*) malloc(sizeof(StackNode));
    stack->dummyhead->val = -99999;
    stack->dummyhead->next = NULL;
    stack->size = 0;
    return stack;
}

void push(Stack* stack, int val) {
    // 新建
    // 
    StackNode* node = (StackNode*) malloc(sizeof(StackNode));
    node->val = val;
    node->next = stack->dummyhead->next;
    stack->dummyhead->next = node;
    stack->size++;
}

int pop(Stack* stack) {
    if (stack->size == 0) {
        perror("stack is empty");
        return;
    }

    StackNode* first = stack->dummyhead->next;
    int x = first->val;
    stack->dummyhead->next = first->next;
    free(first);
    stack->size--;
    return x;
}

int top(Stack* stack) {
    if (stack->size == 0) {
        perror("stack is empty");
        return;
    }

    return stack->dummyhead->next->val;
}

int size(Stack* stack) {
    return stack->size;
}