#include <iostream>
#include <queue>
#include <stack>
    


int main() {
    std::stack<int> s;

    // 入栈
    s.push(10);
    s.push(20);
    s.push(30);

    std::cout << "Stack top: " << s.top() << std::endl; // 输出 30

    // 出栈
    s.pop(); // 移除 30

    std::cout << "After pop, stack top: " << s.top() << std::endl; // 输出 20

    // 判断栈是否为空
    while (!s.empty()) {
        std::cout << "Popping: " << s.top() << std::endl;
        s.pop();
    }


    
    
    std::queue<int> q;

    // 入队
    q.push(100);
    q.push(200);
    q.push(300);

    std::cout << "Queue front: " << q.front() << std::endl; // 输出 100
    std::cout << "Queue back: " << q.back() << std::endl;   // 输出 300

    // 出队
    q.pop(); // 移除 100

    std::cout << "After pop, queue front: " << q.front() << std::endl; // 输出 200

    // 判断队列是否为空
    while (!q.empty()) {
        std::cout << "Dequeuing: " << q.front() << std::endl;
        q.pop();
    }

    return 0;



}