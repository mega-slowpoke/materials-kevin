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



// O(n)

// 接口
// get(int idx) ->  (arr[idx])    ll.get(idx) -> 返回链表索引为idx的元素
// set(int idx, int val) -> (arr[idx] = val)  ll.set(idx, val) : 设置链表索引为idx的元素为val 
// size() -> 想知道链表中多少个元素
// addFront()
// addLast()
// removeFront()
// removeLast()



// addAtIndex(int idx, int val)
// removeAtIndex(int idx)

// field

typedef struct ListNode { 
    int val;
    struct ListNode *next;
} ListNode;

// dummy 哨兵节点，假的头节点，把所有的情况都归一成了middle的情况
typedef struct MyLinkedList {
    ListNode* dummy;
    ListNode* head;
    ListNode* tail;
    int size;
} MyLinkedList;


MyLinkedList* myLinkedListCreate() {
    MyLinkedList* lst = (MyLinkedList*) malloc(sizeof(MyLinkedList));
    lst->head = NULL;
    lst->tail = NULL;
    lst->size = 0;
    return lst;
}


int addLast(MyLinkedList* lst, int val) {
    // 链表大小为0
    // head = tail

    ListNode* cur = lst->dummy;
    while (cur->next != NULL) {
        cur = cur->next;
    }
    ListNode* new_node = (ListNode*) malloc(sizeof(ListNode));
    cur = new_node;


    // if (lst->head == NULL) { 
    //     ListNode* node = (ListNode*) malloc(sizeof(ListNode));
    //     node->val = val;
    //     node->next = NULL;
    //     lst->head = node;
    //     lst->tail = node;
    
    // } else {
    // // 链表大小不为0
    // // 不是空链表
    //     ListNode* new_node = (ListNode*) malloc(sizeof(ListNode));
    //     new_node->val = val;
        
    //     ListNode* last_node = lst->tail;
    //     last_node->next = new_node;

    //     lst->tail = new_node;
    // }
}

// 1.分类讨论 （空链表，有元素？区别？）
// 2. 一步一步来，注意操作顺序 


// O(n)
int get(MyLinkedList* lst, int idx) {
    // idx out of range
    if (idx < 0 || idx >= lst->size) {
        return NULL;
    }
 
    int i = 0;
    ListNode* cur = lst->head;

    while (i < idx) {
        cur = cur->next;
        i++;
    }

    return cur->val;
}

// 如果成功，返回1，如果不成功，返回0
int set(MyLinkedList* lst, int idx, int val) { 
    if (idx < 0 || idx >= lst->size) {
        return 0;
    }

    int i = 0;
    ListNode* cur = lst->head;
    while (i < idx) {
        cur = cur->next;
        i++;
    }

    cur->val = val;

    return 1;
}

// size -> O(n) - O(1)
// 并且不用add/delete都去call size函数了
int size(MyLinkedList* lst) {
    return lst->size;
}

// 如果你是读取/修改，i == idx
// 但是如果你是想插入/删除, i == idx - 1
// 为什么呢？因为插入/删除，都涉及到和前一个，后一个节点的交互
// 因为单链表无法回头，所以只能在前一个就停下
int addAtIndex(MyLinkedList* lst, int idx, int val) { 
    if (idx < 0 || idx > lst->size) {
        return 0;
    }

    ListNode* newNode = malloc(sizeof(ListNode));
    if (idx == 0) {
        newNode->next = lst->head;
        lst->head = newNode;
    } else if (idx == lst->size) {
        // addLast(lst, val);
        lst->tail->next = newNode;
        lst->tail = newNode;
    } else {
        int i = 0;
        ListNode* cur = lst->head;
        while (i < idx - 1) {
            cur = cur->next;
            i++;
        }
        newNode->next = cur->next;
        cur->next = newNode;
    }

    lst->size++;
    return 1;
}

int removeAtIndex(MyLinkedList* lst, int idx) { 
    if (idx < 0 || idx >= lst->size) {
        return 0;
    }

<<<<<<< HEAD
}


typedef struct myListNode {
    int val;
    struct myListNode* next;
} myListNode;

typedef struct {
    myListNode* head;
    int size;
} MyLinkedList;



static myListNode* newNode(int val) {
    myListNode* n = (myListNode*)malloc(sizeof(myListNode));
    n->val = val;
    n->next = NULL;
    return n;
}

MyLinkedList* myLinkedListCreate() {
    MyLinkedList* lst = (MyLinkedList*)malloc(sizeof(MyLinkedList));
    lst->head = newNode(0);
    lst->size = 0;
    return lst;
}

int myLinkedListGet(MyLinkedList* obj, int index) {
    if (index < 0 || index >= obj->size) return -1;
    myListNode* cur = obj->head->next;
    while (index--) cur = cur->next;
    return cur->val;
}

void myLinkedListAddAtIndex(MyLinkedList* obj, int index, int val) {
    if (index > obj->size) return;
    if (index < 0) index = 0;
    myListNode* prev = obj->head;
    for (int i=0; i < index; ++i) prev = prev->next;
    myListNode* node = newNode(val);
    node->next = prev->next;
    prev->next = node;
    obj->size++;
}

void myLinkedListAddAtHead(MyLinkedList* obj, int val) {
    myLinkedListAddAtIndex(obj, 0, val);
}

void myLinkedListAddAtTail(MyLinkedList* obj, int val) {
    myLinkedListAddAtIndex(obj, obj->size, val);
}


void myLinkedListDeleteAtIndex(MyLinkedList* obj, int index) {
    if (index < 0 || index >= obj->size) return;

    myListNode* prev = obj->head;
    for (int i = 0; i < index; ++i) prev = prev -> next;

    myListNode* del = prev->next;
    prev->next = del->next;
    free(del);
    obj->size--;
}

void myLinkedListFree(MyLinkedList* obj) {
    myListNode* cur = obj->head;
    while (cur) {
        myListNode* next = cur->next;
        free(cur);
        cur = next;
    }
    free(obj);
=======
    if (idx == 0) {
        ListNode* first = lst->head;
        lst->head = first->next;
        free(first);
    } else {
        int i = 0;
        ListNode* cur = lst->head;
        while (i < idx - 1) {
            cur = cur->next;
            i++;
        }

        ListNode* toDelete = cur->next;
        cur->next = toDelete->next;
        free(toDelete);
    }

    lst->size--;
    return 1;
>>>>>>> 495f87f5ef5986025df880db15fb5c6d229b8d55
}