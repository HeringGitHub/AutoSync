#include "Log.h"
#include <time.h>
using namespace std;
void Log::set_level(int level, string log_file)
{
    this->level = level;
    if (level == 0)
    {
        fout = fstream("/tmp/" + log_file.length() == 0 ? this->time() : log_file, ios::out | ios::app);
    }
}

string Log::time()
{
    const time_t t = ::time(NULL);
    struct tm* current_time = localtime(&t);
    return boost::str(boost::format("%1%%2%%3%-%4%:%5%:%6%")
        % (current_time->tm_year + 1900) % (current_time->tm_mon + 1) % current_time->tm_mday
        % current_time->tm_hour % current_time->tm_min % current_time->tm_sec);
}