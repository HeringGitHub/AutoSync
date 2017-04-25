#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <boost/format.hpp>
using namespace std;

enum TOKEN{ _endl_ };

class Log
{
public:
    void set_level(int level = 0, string log_file = "");
    template<typename T>
    Log& operator<<(const T& t);
    string time();
private:
    //level:
    // 0: output to log file, 
    // 1: output to console 
    int level;
    int count;
    fstream fout;
};

template<typename T>
Log& Log::operator<< (const T& t)
{
    if (this->count == 0)
    {
        this->fout << "[" << this->time() << "] ";
    }
    if (typeid(t).name() == typeid(_endl_).name())
    {
        this->count = 0;
        this->level == 0 ? this->fout << endl : cout << endl;
    }
    else
    {
        ++this->count;
        this->level == 0 ? this->fout << t: cout << t;
    }
    return *this;
}