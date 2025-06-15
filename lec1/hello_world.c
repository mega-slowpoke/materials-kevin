#include <stdio.h>



// 比特与字节
// 1byte = 8bits
// 计算机最小的运算单位就是1字节

// 数据类型  static-type  language
// int      4字节       4 5 6 
// long     8字节       
// float    4字节
// double   8字节
// char     1字节
// void     
// 指针     4字节 （32位） / 8字节  

// unsigned int / long / short


// 把性质聚合在一起
// 只有性质
struct Student {
    int id;
    char name[20];
    int class;
    char major[20];
};


student1 = {01, "Kevin", 2028, "stats"};
student2 = {01, "Kevin", 2028, "stats"};
student3 = {01, "Kevin", 2028, "stats"};

register_course(struct Student stu, int course_id) {
    printf("stu %d register course %d", stu.id, course_id)
}

register_course(student1, 1);


// 在编码结构体的同时，可以给我们结构体重命名，节省一些时间
typedef struct Point {
    int x;
    int y;
} P;




// 变量的declaration -> 类型+变量名
// 函数 -> 函数签名(signature) -> 返回值+函数名+参数(既有参数名，还有参数类型)
int add(int a, int b) {
    int result = a + b;
    return result;
}



// 程序的入口
int main() {
    printf("Hello World!\n");
    return 0;
}


// 编译与运行程序
// 解释器与编译器
// 运行速度快（因为直接是机器码）
// 源代码保密
// gcc -o hello_world hello_world.c 
// gcc 代表编译器名称
// -o hello_world 代表输出可执行文件文件名(位置无所谓)
// hello_world.c 源文件名称

// 默认输出文件名 a.out
// 可执行文件 exectuable
//   windows -> 后缀是exe
//   linux/Mac -> 后缀是out，或者没有后置

// 条件控制
if (i == 0) {
    ....
} else if (....) {
    
} else if (....) {

} else {

}



// C没有true和false关键字，NULL, 0, ''  都是false
// 任何除了以上东西都代表true

// 循环
// for (int i = 0; i < 10; i++) {

// }

// int x = 0
// while (x < 10)  {
//     ...
//     x++;
// }


// 指针
// 计算机存储本质是一系列的地址与数据。
// 本质上指针存的是另一个变量的内存地址。
int x = 10;
int y = 23;


int *p = &x; 

*p = 20;



// 值传递 reference by value
// C里面所有的东西都是值传递，没有引用传递
void change(int p) {
    p = 100;
}

int main() {
    // int a = 5;
    // change(a);  
    // printf("%d\n", a);  // 输出多少呢？


}

// 那Python呢？

// 动态内存管理
// 1. 初始化：随机的 -> 申请 -> 赋值 -> 释放

