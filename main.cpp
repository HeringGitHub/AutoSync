
#include "libssh2_config.h"
#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/types.h>
#ifdef HAVE_STDLIB_H
#include <cstdlib>
#endif

#ifdef WIN32
#include <conio.h>
#include <windows.h>
#else
#include <pwd.h>
#endif

#include <libssh2.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <cctype>
#include <string>
#include <iostream>
#include <fstream>
using namespace std;

static int waitsocket(int socket_fd, LIBSSH2_SESSION *session)
{
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    FD_ZERO(&fd);
    FD_SET(socket_fd, &fd);

    /* now make sure we wait in the correct direction */
    dir = libssh2_session_block_directions(session);


    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        readfd = &fd;
    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        writefd = &fd;
    rc = select(socket_fd + 1, readfd, writefd, NULL, &timeout);

    return rc;
}

int clean(LIBSSH2_SESSION* session, int sock)
{
    libssh2_session_disconnect(session,

        "Normal Shutdown, Thank you for playing");
    libssh2_session_free(session);


#ifdef WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    fprintf(stderr, "all done\n");

    libssh2_exit();
    return 0;
}

void input_passwd(string& passwd)
{
    char ch;
    passwd.clear();
#ifdef WIN32
    while ((ch = getch()) != 'r')
    {
        passwd += ch;
    }
#else
    system("stty -echo");
    cin >> passwd;
    cout << endl;
    system("stty echo");
#endif
}

int main(int argc, char *argv[])
{

#ifdef WIN32
    WSADATA wsadata;
    int err = WSAStartup(MAKEWORD(2, 0), &wsadata);
    if (err != 0) {
        fprintf(stderr, "WSAStartup failed with error: %d\n", err);
        return 1;
    }
#endif
    
    if (argc != 3)
    {
        cout << "Lack of arguments!" << endl;
        return -1;
    }

    string host = argv[1];
    string path = argv[2];
    int rc = libssh2_init(0);
    if (rc != 0) {
        perror("libssh2 initialization failed: ");
        return -1;
    }

    char cur_user[1024] = { 0 };
#ifdef WIN32
    unsigned long dwNameLen = 0;
    GetUserName(cur_user, &dwNameLen);
#else // UNIX like
    struct passwd* pwd = getpwuid(getuid());
    strcpy(cur_user, pwd->pw_name);
#endif 

    string username;
    string hostname;
    int pos = host.find('@');
    if (pos == 0 || pos == host.length() - 1) {
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
    
    unsigned long hostaddr = inet_addr(hostname.c_str());

    /* Ultra basic "connect to port 22 on localhost"
    * Your code is responsible for creating the socket establishing the
    * connection
    */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(22);
    sin.sin_addr.s_addr = hostaddr;
    if (connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in)) != 0) {
        perror("failed to connect:");
        return -1;
    }

    /* Create a session instance */
    LIBSSH2_SESSION* session = libssh2_session_init();

    if (!session)
    {
        perror("libssh session init failed:");
        return -1;
    }
    /* tell libssh2 we want it all done non-blocking */
    libssh2_session_set_blocking(session, 0);


    /* ... start it up. This will trade welcome banners, exchange keys,
    * and setup crypto, compression, and MAC layers
    */
    while ((rc = libssh2_session_handshake(session, sock)) == LIBSSH2_ERROR_EAGAIN);
    if (rc) {
        perror("Failure establishing SSH session: ");
        return -1;
    }

    LIBSSH2_KNOWNHOSTS* nh = libssh2_knownhost_init(session);
    if (!nh) {
        /* eeek, do cleanup here */
        perror("libssh knownhost init failed: ");
        return -1;
    }

    /* read all hosts from here */
    //libssh2_knownhost_readfile(nh, "known_hosts", LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    /* store all known hosts to here */
    //libssh2_knownhost_writefile(nh, "dumpfile", LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    size_t len;
    int type;
    const char* fingerprint = libssh2_session_hostkey(session, &len, &type);

    if (fingerprint) 
    {
        struct libssh2_knownhost *host;
#if LIBSSH2_VERSION_NUM >= 0x010206
        /* introduced in 1.2.6 */
        int check = libssh2_knownhost_checkp(nh, hostname.c_str(), 22, fingerprint, len,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
#else
        /* 1.2.5 or older */
        int check = libssh2_knownhost_check(nh, hostname.c_str(), fingerprint, len,
            LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);
#endif
        fprintf(stderr, "Host check: %d, key: %s\n", check,
            (check <= LIBSSH2_KNOWNHOST_CHECK_MISMATCH) ?  host->key : "<none>");

        /*****
        * At this point, we could verify that 'check' tells us the key is
        * fine or bail out.
        *****/
    }
    else 
    {
        /* eeek, do cleanup here */
        return -1;
    }
    libssh2_knownhost_free(nh);



    string keypath = strcmp(cur_user, "root") == 0 ? "/root/.ssh/" : "/home/" + username + "/.ssh/";
    //if(access((keypath+"id_rsa.pub").c_str(), F_OK) != 0 || access((keypath + "id_rsa").c_str(), F_OK) != 0)
    //{
    //    cout << "Lack of key pairs or be damaged." << endl;
    //    cout << "Do you want to generate the key pairs (yes/no)?" << endl;
    //    string str;
    //    getline(cin, str);
    //    if (str == "yes")
    //    {
    //        //Generate key pairs.
    //    }
    //}
    int signal = 0;
    //while (access((keypath + "id_rsa.pub").c_str(), F_OK) == 0 && access((keypath + "id_rsa").c_str(), F_OK) == 0)
    //{
    //    /* Or by public key */
    //    while ((rc = libssh2_userauth_publickey_fromfile(session, username.c_str(), 
    //        (keypath + "id_rsa.pub").c_str(), (keypath + "id_rsa").c_str(), NULL)) == LIBSSH2_ERROR_EAGAIN);
    //    if (rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED) {
    //        cout << "The username/public key combination was invalid or the password is not null." << endl;
    //        if (signal == 0)
    //        {
    //            cout << "Do you want to generate the key pairs (yes/no)?" << endl;
    //            string str;
    //            getline(cin, str);
    //            if (str == "yes")
    //            {
    //                //Generate key pairs.
    //                continue;
    //            }
    //        }
    //        signal = 0;
    //        break;
    //    }
    //    else if (rc)
    //    {
    //        fprintf(stderr, "\tAuthentication by public key failed\n");
    //        return clean(session, sock);
    //    }
    //    signal = 1;
    //}
    if (signal == 0)
    {
        cout << host << "'s password:";
        string password;
        input_passwd(password);
        if (password.length() > 0)
        {
            /* We could authenticate via password */
            while ((rc = libssh2_userauth_password(session, username.c_str(), password.c_str())) == LIBSSH2_ERROR_EAGAIN);
            if (rc) {
                perror("Authentication by password failed: ");
                return clean(session, sock);
            }
        }
    }
#if 0
    libssh2_trace(session, ~0);

#endif
    LIBSSH2_CHANNEL* channel;
    /* Exec non-blocking on the remote host */
    while ((channel = libssh2_channel_open_session(session)) == NULL &&
        libssh2_session_last_error(session, NULL, NULL, 0) == LIBSSH2_ERROR_EAGAIN)
    {
        waitsocket(sock, session);
    }
    if (channel == NULL)
    {
        fprintf(stderr, "Error\n");
        exit(1);
    }
    char commandline[1024] = { 0 };
    string pub_key;
    if (signal == 1)
    {
        fstream fin(keypath + "id_rsa.pub");
        getline(fin, pub_key);
        string remote_authorized_keys = (username == "root" ? "/root/" : "/home/" + username) + "/.ssh/authorized_keys";
        snprintf(commandline, sizeof(commandline),
            "echo %s >> %s;if [ ! -d %s ];then mkdir -p %s;elif [ ! -x %s ];then echo error;fi", 
            pub_key.c_str(), remote_authorized_keys, path.c_str(), path.c_str(), path.c_str());
    }
    else
    {
        snprintf(commandline, sizeof(commandline),
            "if [ ! -d %s ];then mkdir -p %s;elif [ ! -x %s ];then echo error;fi", path.c_str(), path.c_str(), path.c_str());
    }
    while ((rc = libssh2_channel_exec(channel, commandline)) == LIBSSH2_ERROR_EAGAIN)
    {
        waitsocket(sock, session);
    }
    if (rc != 0)
    {
        fprintf(stderr, "Error\n");
        exit(1);
    }

    int bytecount = 0;
    char buffer[1024] = { 0 };
    while(true)
    {
        /* loop until we block */
        int rc; 
        do
        {
            rc = libssh2_channel_read_stderr(channel, buffer + bytecount, sizeof(buffer) - bytecount);
            if (rc > 0)
            {
                bytecount += rc;
            }
            else 
            {
                if (rc != LIBSSH2_ERROR_EAGAIN)
                {   /* no need to output this for the EAGAIN case */
                    fprintf(stderr, "libssh2_channel_read returned %d\n", rc);
                }
            }
        } while (rc > 0);

        /* this is due to blocking that would occur otherwise so we loop on
        this condition */
        if (rc == LIBSSH2_ERROR_EAGAIN)
        {
            waitsocket(sock, session);
        }
        else
        {
            break;
        }
    }

    int exitcode = 127;
    while ((rc = libssh2_channel_close(channel)) == LIBSSH2_ERROR_EAGAIN)
    {
        waitsocket(sock, session);
    }
    char *exitsignal = (char *)"none";
    if (rc == 0)
    {
        exitcode = libssh2_channel_get_exit_status(channel);
        libssh2_channel_get_exit_signal(channel, &exitsignal, NULL, NULL, NULL, NULL, NULL);
    }

    if (exitsignal)
    {
        fprintf(stderr, "\nGot signal: %s\n", exitsignal);
    }
    else
    {
        fprintf(stderr, "\nEXIT: %d bytecount: %d\n", exitcode, bytecount);
    }
    cout << buffer << endl;
    libssh2_channel_free(channel);

    channel = NULL;
    clean(session, sock);
    return 0;
}