// C风格
struct StudentC {
    char name[20];
    int age;
};
void introduce(StudentC s) {
    printf("Hi, I'm %s, %d years old.\n", s.name, s.age);
}

// C++ OOP风格
#include <iostream>
#include <string>
using namespace std;

class Student {
public:
    string name;
    int age;

    void introduce() {
        cout << "Hi, I'm " << name << ", " << age << " years old." << endl;
    }
};