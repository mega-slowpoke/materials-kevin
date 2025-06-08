#include <stdio.h>
#include <string.h>

// 定义结构体学生，需要有id, name，和gpa
typedef struct Student {
    int id;
    char name[50];
    float gpa;
} Student;

// 函数声明
Student create_student(int id, const char *name, float gpa);
void print_student(const Student *stu);
void update_gpa(Student *stu, float new_gpa);
int compare_gpa(const Student *stu1, const Student *stu2);
void print_all_students(const Student *students, int count);

// 函数实现
// TODO: 初始化学生
Student create_student(int id, const char *name, float gpa) {
   
}

// TODO: 打印学生信息 
// 现在我们还没有学习指针，如果一个参数前面是*(比如下面的const Student *stu)，采用stu->id获取成员变量的值
// 如果不是，采用stu.id获取成员变量的值  
void print_student(const Student *stu) {
    printf("ID: %d\n", stu->id);
    // TODO: 打印剩下的信息
    printf("----------------------\n");
}

// 3️⃣ TODO:更新 GPA
void update_gpa(Student *stu, float new_gpa) {
    
}

//  TODO:比较两个学生 GPA，返回 GPA 更高的学生 id
int compare_gpa(const Student *stu1, const Student *stu2) {
    
}



// 6️⃣ TODO: 打印所有学生信息
void print_all_students(const Student *students, int count) {
   
}

// TODO: 主程序已经写完，请你采用gcc 编译后运行代码
int main() {
    // 创建3个学生
    Student s1 = create_student(1, "Alice", 3.8);
    Student s2 = create_student(2, "Bob", 3.5);
    Student s3 = create_student(3, "Charlie", 3.9);

    // 打印学生信息
    printf("Initial Students:\n");
    print_student(&s1);
    print_student(&s2);
    print_student(&s3);

    // 更新 GPA
    update_gpa(&s2, 3.85);
    printf("After updating Bob's GPA:\n");
    print_student(&s2);

    // 比较 GPA
    int higher_gpa_id = compare_gpa(&s1, &s2);
    printf("Student with higher GPA between Alice and Bob: ID = %d\n", higher_gpa_id);

   
    // 批量处理学生数组(数组，我们暂时没学，但是如果你用过python，students[0]和python中的list[0]是一样的)
    Student students[] = {s1, s2, s3};
    int student_count = sizeof(students) / sizeof(students[0]);

    printf("All students:\n");
    print_all_students(students, student_count);

    return 0;
}
