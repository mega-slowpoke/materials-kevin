#include <stdio.h>
#include <stdlib.h>

const int INITIAL_SIZE = 6;
typedef struct ArrayQueue {
    int arr[INITIAL_SIZE];
    int size;
    int nextInsertIdx;
    int frontIdx;
} ArrayQueue;


// 初始化ArrayQueue, 上面的这些字段应该怎么赋值?
ArrayQueue* createArrayQueue() { 
}

void enqueue(ArrayQueue* queue, int val) { 
    // 注意分类讨论
    // 1.达到最大大小， 扩容


    // 2.有足够大小
    queue->arr[queue->nextInsertIdx] = val;
    queue->nextInsertIdx++;
    queue->size++;
}

// 移除并且返回队首元素
int dequeue(ArrayQueue* queue) {

}

// 查看队首元素
int front(ArrayQueue* queue) {

}


// 查看队尾元素
int rear(ArrayQueue* queue) {

}

int size(ArrayQueue* queue) {

}