#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#endif // __linux
#ifdef WIN32
#include <winsock2.h>
#include "../mingw_net.h"
#endif // WIN32
#include <iostream>
#include <thread>
#include <list>
#include <mutex>

using namespace std;

list<int> cli_conn_list;
mutex m;

#ifdef WIN32
void perror(const char* msg) { fprintf(stderr, "%s %ld\n", msg, GetLastError()); }
#endif // WIN32

void usage() {
    cout << "syntax : echo-server <port> [-e[-b]]\n";
    cout << " -e : echo\n";
    cout << " -b : broadcast\n";
    cout << "sample : echo-server 1234 -e -b\n";
}

struct Param {
    uint16_t port = 0;
    bool echo = false;
    bool broadcast = false;

    bool parse(int argc, char* argv[]) {
        if (argc >= 3) {
            port = stoi(argv[1]);
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "-e") == 0)
                    echo = true;
                else if (strcmp(argv[i], "-b") == 0)
                    broadcast = true;

            }
        }
        return port != 0;
    }
} param;

void recvThread(int sd) {
    cout << "connected\n";
    static const int BUFSIZE = 65536;
    char buf[BUFSIZE];
    while (true) {
        ssize_t res = recv(sd, buf, BUFSIZE - 1, 0);
        if (res == 0 || res == -1) {
            cerr << "recv return " << res;
            perror(" ");
            break;
        }
        buf[res] = '\0';
        cout << buf;
        cout.flush();
        if (param.broadcast) {
            // 연결된 클라이언트 소켓 iter 돌면서 send + exception 처리
            m.lock();
            for(int cli_sd: cli_conn_list){
                res = send(cli_sd, buf, res, 0);
                if (res == 0 || res == -1) {
                    cerr << "send return " << res;
                    perror(" ");
                }
            }
            m.unlock();
        }
        else if (param.echo) {
            res = send(sd, buf, res, 0);
            if (res == 0 || res == -1) {
                cerr << "send return " << res;
                perror(" ");
                break;
            }
        }
    }
    cout << "disconnected\n";
    close(sd);

    // cli_conn_list에서 해당 sd 제거 + mutex 적용
    m.lock();
    cli_conn_list.remove(sd);
    m.unlock();
}

int main(int argc, char* argv[]) {
    if (!param.parse(argc, argv)) {
        usage();
        return -1;
    }

#ifdef WIN32
    WSAData wsaData;
    WSAStartup(0x0202, &wsaData);
#endif // WIN32

    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1) {
        perror("socket");
        return -1;
    }

    int res;
#ifdef __linux__
    int optval = 1;
    res = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (res == -1) {
        perror("setsockopt");
        return -1;
    }
#endif // __linux

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(param.port);

    ssize_t res2 = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
    if (res2 == -1) {
        perror("bind");
        return -1;
    }

    res = listen(sd, 5);
    if (res == -1) {
        perror("listen");
        return -1;
    }

    while (true) {
        struct sockaddr_in cli_addr;
        socklen_t len = sizeof(cli_addr);
        int cli_sd = accept(sd, (struct sockaddr *)&cli_addr, &len);
        if (cli_sd == -1) {
            perror("accept");
            break;
        }
        // cli_conn_list에서 해당 sd 삽입 + mutex 적용
        m.lock();
        cli_conn_list.push_back(cli_sd);
        m.unlock();

        thread* t = new thread(recvThread, cli_sd);
        t->detach();
    }
    close(sd);
}
