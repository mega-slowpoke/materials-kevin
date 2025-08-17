#include <string>
#include "people.cpp"

class University {
    public:
        std::string name;
        int year;
        People student;
        People faculty;
        int publications;
        int citations;
        int patents;
        int grants;
        int research_output;
        int research_output_per_student;
        int research_output_per_faculty;
        int research_output_per_publication;
        int research_output_per_citations;
        int research_output_per_patents;


    void display_student_name1() {
        student.display_name();
    }

   void display_student_name2() {
        std::cout << student.name << std::endl;
    }
}