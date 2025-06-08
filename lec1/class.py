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

print("Hello World!")