#include<iostream>
#include<string>

// 类 = 属性 + 行为
// 访问修饰符 (Access Modifiers): public, private, protected
// 四大支柱: 封装，抽象，继承、多态
// 类和对象的区别是什么？
// 继承、多态


#include <iostream>
#include <vector>
using namespace std;

// 父类 People
class People {

protected:
    string name;
    int age;

public:
    People(string n, int a) : name(n), age(a) {}

    // 虚函数，子类可以overwrite
    virtual void introduce() {
        cout << "I am " << name << ", " << age << " years old." << endl;
    }

    // 静态多态
    virtual void introduce(string prefix) {
        cout << prefix << " " << name << ", " << age << " years old." << endl;
    }

    // destructor，通常设为虚函数，避免内存泄漏
    virtual ~People() {}
};

// 子类 Student
class Student : public People {
private:
    string major;
public:
    Student(string n, int a, string m) : People(n, a), major(m) {}

    void introduce() override {
        cout << "I am student " << name 
             << ", majoring in " << major 
             << ", age " << age << "." << endl;
    }

   
};

// 子类 Faculty
class Faculty : public People {
private:
    string department;
public:
    Faculty(string n, int a, string d) : People(n, a), department(d) {}

    void introduce() override {
        cout << "I am faculty " << name 
             << ", from " << department 
             << " department, age " << age << "." << endl;
    }


};

int main() {
    cout << "==== 静态多态 (函数重载) ====" << endl;
    People p("Alice", 20);
    p.introduce();                  // 调用第一个版本
    p.introduce("Hello, I am");     // 调用重载版本

    cout << "\n==== 动态多态 (虚函数) ====" << endl;
    vector<People*> group;
    group.push_back(new Student("Bob", 19, "Computer Science"));
    group.push_back(new Faculty("Dr. Smith", 50, "Mathematics"));

    for (auto person : group) {
        person->introduce();   // 运行时决定调用哪个函数
    }

    for (auto person : group) delete person;

    return 0;
}

