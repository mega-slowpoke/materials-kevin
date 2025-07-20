#include <stdio.h>
#include <stdlib.h>


const int INITIAL_SIZE = 6;
typedef struct ArrayStack {
    int *arr;
    int capacity;
    int size;
    int nextInsertIdx;
} ArrayStack;


// 初始化栈
ArrayStack* createStack() {
    ArrayStack* stack = (ArrayStack*)malloc(sizeof(ArrayStack));
    stack->arr = (int*)malloc(sizeof(int) * INITIAL_SIZE);
    stack->capacity = INITIAL_SIZE;
    // TODO: 初始化剩下的代码
}


// 入栈
void push(ArrayStack* stack, int val) {

}

// 出栈
int pop(ArrayStack* stack) {

}

// 查看栈顶元素
int top(ArrayStack* stack) {

}

int size(ArrayStack* stack) {

}