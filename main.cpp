#if (defined(WIN32) /*||defined(_WIN32)*/  || defined(__WIN32) || defined(_WIN64) || defined(__WIN64)) && !defined(WINDOWS_ENV)
#define WINDOWS_ENV
#endif

#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <iomanip>


#include "Log.h"
#ifdef WINDOWS_ENV
#include <conio.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/inotify.h>
#endif
using namespace std;

const unsigned long DIR_IN_MASK = IN_CREATE | IN_DELETE | IN_MOVE;
const unsigned long FIL_IN_MASK = IN_ATTRIB | IN_MODIFY;

string localpath;
string remotepath;
string hostname;
string username;
bool debug_mode = false;
int port = 22;
Log log;
/* Structure describing an inotify event.  */
struct _inotify_event
{
    int wd;		/* Watch descriptor.  */
    uint32_t mask;	/* Watch mask.  */
    uint32_t cookie;	/* Cookie to synchronize two events.  */
    uint32_t len;		/* Length (including NULs) of name.  */
    string name;	/* Name.  */
};

int Recur_Dir(const string& path, const int& fd, map<int, string>& wd_map)
{
#ifdef WINDOWS_ENV
#else
    DIR *dir;
    if ((dir = opendir(path.c_str())) == NULL)
    {
        return -1;
    }
    int wd = inotify_add_watch(fd, path.c_str(), DIR_IN_MASK);
    if (wd < 0)
    {
        return -1;
    }
    wd_map[wd] = path;
    log << "Add wd: " << wd << ", name: " << wd_map[wd] << _endl_;
    struct dirent *ptr;
    while ((ptr = readdir(dir)) != NULL)
    {
        if (strcmp(ptr->d_name, ".") == 0 || strcmp(ptr->d_name, "..") == 0) continue;
        if (ptr->d_type == DT_DIR)
        {      
            if (Recur_Dir(path + "/" + ptr->d_name, fd, wd_map) < 0)
            {
                return -1;
            }  
        }
        else
        {
            wd = inotify_add_watch(fd, (path + "/" + ptr->d_name).c_str(), FIL_IN_MASK);      
            if (wd < 0)
            {
                return -1;
            }
            wd_map[wd] = path + "/" + ptr->d_name;
            log << "Add wd: " << wd << ", name: " << wd_map[wd] << _endl_;
        }
    }
    closedir(dir); 
#endif 
    return 0;
}

int Read_Event(
#ifdef WINDOWS_ENV
#else
    queue<_inotify_event*>& event_que
#endif
    , const int& fd)
{
    char buffer[16384];
    int count = 0;
#ifdef WINDOWS_ENV
#else
    ssize_t rd_size = read(fd, buffer, 16384);
    if (rd_size <= 0)
        return rd_size;
    
    for (size_t buffer_i = 0; buffer_i < rd_size; ++count)
    {
        _inotify_event* pevent = new _inotify_event;
        inotify_event* tmp = (inotify_event*)(buffer + buffer_i);
        memmove(pevent, tmp, offsetof(inotify_event, name));
        pevent->name = tmp->name;
        buffer_i += offsetof(inotify_event, name) + tmp->len;
        event_que.push(pevent);
    }
#endif
    return count;
}

void Handle_Events(
#ifdef WINDOWS_ENV 
#else
    queue<_inotify_event*>& event_que,
#endif
    const int& fd, map<int, string>& wd_map
)
{
#ifdef WINDOWS_ENV
#else
    while (event_que.size() > 0)
    {
        _inotify_event* event = event_que.front();
        int cur_event_wd = event->wd;
        string cur_event_filename = event->name;
        if (wd_map.find(cur_event_wd) == wd_map.end())
        {
            log << "Warning: couldn't find the wd: " << cur_event_wd << _endl_;
            break;
        }
        else
        {
            log << "Handle wd: " << cur_event_wd << ", name: " << cur_event_filename << _endl_;
            //cout << hex << (event->mask &(IN_ALL_EVENTS | IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED)) << dec << endl;
            switch (event->mask &(IN_ALL_EVENTS | IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED))
            {
            case IN_CREATE:
            case IN_MOVED_FROM:
            {
                boost::format fmt = (event->mask & IN_ISDIR) ?
                    boost::format(" -r -p %1% %2% %3%@%4%:%5%") : boost::format(" -p %1% %2% %3%@%4%:%5%");

                string full_path = wd_map[cur_event_wd] + "/" + cur_event_filename;
                string re_path = full_path.substr(localpath.length());
                string option = boost::str(fmt % port % full_path % username % hostname % (remotepath + re_path));
                log << "Exec cmd: scp" + option << _endl_;
                //system(("scp" + option).c_str());

                //Add these new file or directory to listen event
                if ((event->mask & IN_ISDIR))
                {
                    Recur_Dir(wd_map[cur_event_wd] + "/" + cur_event_filename, fd, wd_map);
                }
                else
                {
                    int wd = inotify_add_watch(fd, (wd_map[cur_event_wd] + "/" + cur_event_filename).c_str(), FIL_IN_MASK);
                    if (wd < 0)
                    {
                        log << "Add watch failed: " << wd_map[cur_event_wd] + "/" + cur_event_filename << _endl_;
                        break;
                    }
                    wd_map[wd] = wd_map[cur_event_wd] + "/" + cur_event_filename;
                    log << "Add wd: " << wd << ", name: " << wd_map[wd] << _endl_;
                }

                break;
            }
            case IN_MOVED_TO:
            case IN_DELETE:
            {
                boost::format option_fmt(" -p %1% %2%@%3% ");
                string option = boost::str(option_fmt % port % username % hostname);
                boost::format cmd_fmt("'rm -rf %1%'");
                string cmd_string = boost::str(cmd_fmt % (wd_map[cur_event_wd] + "/" + cur_event_filename));
                log << "Exec cmd: ssh" + option + cmd_string << _endl_;
                //system(("ssh" + option + cmd_string).c_str());
                break;
            }
            case IN_IGNORED:
                inotify_rm_watch(fd, cur_event_wd);
                wd_map.erase(wd_map.find(cur_event_wd));
                log << "Remove wd: " << cur_event_wd << _endl_;
                break;
            case IN_MODIFY:
            case IN_ATTRIB:
            {
                boost::format fmt = boost::format(" -p %1% %2% %3%@%4%:%5%");
                string full_path = wd_map[cur_event_wd];
                string re_path = full_path.substr(localpath.length());
                string option = boost::str(fmt % port % full_path % username % hostname % (remotepath + re_path));
                log << "Exec cmd: scp" + option << _endl_;
                //system(("scp" + option).c_str());
                break;
            }
            default:
                break;
            }
        }
        delete event;
        event_que.pop();
    }
#endif
}

int File_Monitor()
{
#ifdef WINDOWS_ENV
#else
    int fd = inotify_init();
    if (fd < 0)
    {
        perror("inotify_init: ");
        return -1;
    }
    map<int, string> wd_map;
    if (Recur_Dir(localpath, fd, wd_map) < 0)
    {
        cout << "Recurse directory failed!" << endl;
        return -1;
    }
    queue<_inotify_event*> event_que;
    
    while (true)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        if (select(FD_SETSIZE, &rfds, NULL, NULL, NULL) > 0)
        {
            if (Read_Event(event_que, fd) < 0)
            {
                return -1;
            }
            else
            {
                Handle_Events(event_que, fd, wd_map);
            }
        }
    }
#endif
}

int main(int argc, char** argv)
{
    char ch;
    string host;
    while ((ch = getopt(argc, argv, "h:l:r:p:d")) != -1)
    {
        switch (ch)
        {
        case 'h':
            host = optarg;
            break;
        case 'l':
            localpath = optarg;
            break;
        case 'r':
            remotepath = optarg;
            break;
        case 'p':
            sscanf(optarg, "%d", &port);
            break;
        case 'd':
            debug_mode = true;
            log.set_level(1);
            break;
        default:
            cout << "usage: autosync <-h username@hostname> <-l localpath> <-r remotepath> [-p port] [-d]" << endl;
            return -1;
        }
    }
    char cur_user[1024] = { 0 };
#ifdef WINDOWS_ENV
    unsigned long dwNameLen = 0;
    GetUserName(cur_user, &dwNameLen);
#else // UNIX like
    struct passwd* pwd = getpwuid(getuid());
    strcpy(cur_user, pwd->pw_name);
#endif 

    int pos = host.find('@');
    if (pos == 0 || pos == host.length() - 1 || localpath[0] != '/' || remotepath[0] != '/') {
        cout << "Invalid arguments!" << endl;
        return -1;
    }
    else if (pos == string::npos)
    {
        username = cur_user;
        hostname = host;
    }
    else
    {
        username = host.substr(0, pos);
        hostname = host.substr(pos + 1);
    }

    string key_dir = strcmp(cur_user, "root") == 0 ? "/root/.ssh/" : "/home/" + username + "/.ssh/";
    if (access((key_dir + "id_rsa.pub").c_str(), F_OK) != 0 || access((key_dir + "id_rsa").c_str(), F_OK) != 0)
    {
        system("ssh-keygen");
    }

    string cmd_option = boost::str(boost::format(" -p %1% %2%@%3% ") % port % username % hostname);
    if (access((key_dir + "id_rsa.pub").c_str(), F_OK) == 0 || access((key_dir + "id_rsa").c_str(), F_OK) == 0)
    {
        system(("ssh-copy-id" + cmd_option).c_str());
    }

    if (access(localpath.c_str(), F_OK) != 0)
    {
        cout << "Local path is not exist!" << endl;
        return -1;
    }
    boost::format fmt("'if [ ! -d %1% ];then mkdir -p %1%;elif [ ! -x %1% ];then echo error;fi'");
    string remote_cmd = boost::str(fmt % remotepath);
    FILE* fp = popen(("ssh" + cmd_option + remote_cmd).c_str(), "r");
    if (fp)
    {
        char buff[1024] = { 0 };
        if (fgets(buff, sizeof(buff), fp))
        {
            cout << buff << endl;
            return -1;
        }
    }
    else
    {
        cout << "Check remote path failed!" << endl;
    }
    if (File_Monitor() < 0)
    {
        cout << "Exception occurred" << endl;
    }
    return 0;
}