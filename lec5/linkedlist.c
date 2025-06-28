#include <stdio.h>
#include <stdlib.h>


// list -> python 列表 
// array -> java/C/golang/C++ 数组 (topic 1)
// linkedlist -> 链表 (topic 2)   去作为底层数据结构来实现python中的list


// array
int main() {
    int* arr[10];

    arr[0] = (int*)malloc(sizeof(int));
    arr[1] = (int*)malloc(sizeof(int));

    *arr[0] = 10;
    *arr[1] = 20;

    printf("%d\n", *arr[0]);
    printf("%d\n", *arr[1]);

    free(arr[0]);
    free(arr[1]);


}

void intro_to_arr() {
  // 分配array的大小
    // 分配到栈上
    // 分配连续的空间
    int arr_name[10];

    // python 相似
    int first = arr_name[0];
    arr_name[1] = 20;


    // 在static typed (c/java/golang/c++) 
    arr_name[0] = 10;
    arr_name[2] = "c";
    printf("%d %d", arr_name[0], arr_name[2]);

}

// 难点
// 数组的duality 数组 <-> 指针
// 
// pointer pointing to a array <-> an array conisting of pointers 
int arr_pointer() {
       int arr_name[10];
    int* ptr = arr_name;

    int i = 0;
    while (i < 10) {
        ptr[i] = i;
        ptr += 1;
    }


    return 0;
   
}



// 优点
// 1. 速度会不会快？ -> 查找/修改: O(1)
// 2. 用起来很方便 
// 3. 紧凑，占用空间小

// 缺点
// 1. 都是同一个类型
// 2. 大小是固定 -> 重新分配一个数组，这个数组的大小51。并且要复制原数组元素到新数组， O(n) 
// 3. 插入/删除   sx


// 读多增减少  -> 效率很高 O(1)
// 经常在中间插入/删除 ，经常改变数组大小 -> O(n)

// O(n)
void insert_value(int arr[], int index, int value) {
    for (int i = 100; i >= index; i--) {
        arr[i] = arr[i - 1];
    }

    arr[index] = value;
}

// O(n)
void delete_value(int arr[], int index);


// 链表 linkedlist
// 为了解决数组的缺点


// 特点
// 1. 只有知道一个节点，那么我们就知道了它右边的所有节点
// 2. 对于单向链表，只能从上一个走到下一个，不能反过来


// 接口
// get(int idx) ->  (arr[idx])    ll.get(idx) -> 返回链表索引为idx的元素
// set(int idx, int val) -> (arr[idx] = val)  ll.set(idx, val) : 设置链表索引为idx的元素为val 
// size() -> 想知道链表中多少个元素
// addAtIndex(int idx, int val)
// removeAtIndex(int idx)

typedef struct MyLinkedList {
    ListNode* head;
    ListNode* tail;
} MyLinkedList;

typedef struct ListNode { 
    int val;
    struct ListNode *next;

} ListNode;



MyLinkedList* myLinkedListCreate() {
    MyLinkedList* lst = (MyLinkedList*) malloc(sizeof(MyLinkedList));
    lst->head = NULL;
    lst->tail = NULL;

    return lst;
}

int addLast(MyLinkedList* lst, int val) {
    // 链表大小为0
    // head = tail
    if (lst->head == NULL) { 
        ListNode* node = (ListNode*) malloc(sizeof(ListNode));
        node->val = val;
        node->next = NULL;
        lst->head = node;
        lst->tail = node;
    
    } else {
    // 链表大小不为0
    // 不是空链表
        ListNode* new_node = (ListNode*) malloc(sizeof(ListNode));
        new_node->val = val;
        
        ListNode* last_node = lst->tail;
        last_node->next = new_node;

        lst->tail = new_node;
    }
}

// 1.分类讨论 （空链表，有元素？区别？）
// 2. 一步一步来，注意操作顺序 

int get(MyLinkedList* lst, int idx) {
    
}

int set(MyLinkedList* lst, int idx, int val) { 

}

int size(MyLinkedList* lst) {

}

int addAtIndex(MyLinkedList* lst, int idx, int val) { 

}

int removeAtIndex(MyLinkedList* lst, int idx) { 

}