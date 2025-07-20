class Student:

    def __init__(self, id, name, class_year, major):   
        self.id = id
        self.name = name
        self.class_year = class_year
        self.major = major

    
    def register_course(self, course_id):
        print("Registering " + self.name + " for " + course_id)

    
    def drop_course(self, course_id):
        print("Dropping " + self.name + " from " + course_id)

# 行为 + 性质
# stu1 = Student(1, "John Doe", 2023, "Computer Science")
# stu2 = Student(2, "Jane Doe", 2024, "Computer Science")
# stu3 = Student(3, "Jim Doe", 2025, "Computer Science")
# stu1.register_course(1)

# print("Hello World!")


# a = 10, b = 20 -> swap(a,b) -> a = 20, b = 10
# def swap(x, y):
#     temp = x
#     x = y
#     y = temp

# def update_student_gpa(stu, new_id):
#     stu.id = new_id

# stu1 = Student(1, "John Doe", 2023, "Computer Science")
# print(stu1.id)          # 1
# update_student_gpa(stu1, 2)
# print(stu1.id)          # 2



# a = 10
# b = 20
# print(a, b)  # 10 20
# swap(a, b)
# print(a, b)  # 10 20


