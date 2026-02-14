#include<bits/stdc++.h>
#include<vector>
using namespace std;
/* =========================*/
class person{
    protected:
        string name;
    public:
        person(){name = "NO NAME";};
        person(string _name){name = _name;};
        virtual void getInfo(){
            cout<< name << endl;
        };
};
class course; // forward declaration
/* =========================*/
class student:public person{
    private:
        int studentID;
    public:
        student(string _name, int _stdid):
            person(_name){
            studentID = _stdid;
        };
        void getInfo() override;
        void enroll(course* erl_course);
};
/* =========================*/
class professor:public person{
    private:
        int employId;
    public:
        professor(string _name, int _empid):
            person(_name){
            employId = _empid;
        };
        void teach(course* teach_course);
};
class course{
    private:
        string name;
        vector<student*> students;
    public:
    course(string _name) : name(_name) {}
    string getCourseName(){return name;};
    void addStudent(student* s) {
        students.push_back(s);
    }
    void showStudents() {
        cout << "Course [" << name << "] - Student list:" << endl;
        for (auto s : students) {
            s->getInfo();
            cout << endl;
        }
    }
    ~course() {
        cout << "Course \"" << name << "\" is destroyed" << endl;
    }
};
/* =========================*/
void student::enroll(course* X) {
    X->addStudent(this);
}
void student::getInfo(){
    cout << name << " [" << studentID << "]" << endl;
}
void professor::teach(course* X) {
    cout << "Prof. " << name << "[" << employId << "]" << 
    " teaches the course[" <<
    X->getCourseName() << "]" << endl;
}

int main(){
    student s1("An",111);
    student s2("Minh",222);
    professor p1("Dr. Tuan",111);
    course c1("C++ Programming");
    s1.enroll(&c1);
    s2.enroll(&c1);
    p1.teach(&c1);
    c1.showStudents();
    return 0;
};
