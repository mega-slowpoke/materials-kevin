#include <stdio.h>


// struct 的复习：
#include <stdio.h>
#include <string.h>

// 最基本的情况
struct Person {
    char name[20];
};

typedef struct  {
    char name[20];
} Person1;

typedef struct {
    char name[20];
} Person2;



struct {
    char name[20];
};

struct {
    char name[30];
    int age;
};

struct Person p0;
Person1 p1;
Person2 p2;
struct p3;



// ---- 指针 ----- 

void change_x_value(int x) {
    x = 20;
}

// 传进来任何值，我们都把他变成20
// 1. 传进来指针p，
// 2. *p 变为20
void change_x_ptr(int p) {
    int *q = &p;
    *q = 20;
}

void change_x_ptr_corect(int *p) {
    *p = 20;
}


// a = 10, b = 20 -> swap(a, b) -> a = 20, b = 10
void swap(int* p, int* q) {
    int temp = *p;
    // int temp = *p;
    *p = *q;
    *q = temp;

}


int main() {
    
    int a;
    printf("%d\n", a);


    // int x = 10;
    // int y = 23;
    
    // int* p = &x;
    // *p = 20; 

    // swap(&x, &y); 
    // printf("x = %d\n", x);      // 10
    // change_x_ptr_corect(&x);
    // printf("x = %d\n", x);     // 10
   
   
    // int *p = &x;
    // int *q = &y;
    // printf("x = %d, p = %p\n", x, p);
    // printf("y = %d, q = %p\n", y, q);


    // int m = 26;
    // int *mp;
    // mp = &m;

    // printf("m = %d\n", *mp);
}