#include <netdb.h>
#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <fstream>

#define MAX_MEM_BUFFER_SIZE 8192
#define PORT_DEFAULT 8765

std::ofstream outfile("out.txt");

bool is_exit_command(std::string &cmd) { return cmd == "exit" || cmd == "exit;" || cmd == "bye" || cmd == "bye;"; }

int init_unix_sock(const char *unix_sock_path) {
    int sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "failed to create unix socket. %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sun_family = PF_UNIX;
    snprintf(sockaddr.sun_path, sizeof(sockaddr.sun_path), "%s", unix_sock_path);

    if (connect(sockfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0) {
        fprintf(stderr, "failed to connect to server. unix socket path '%s'. error %s", sockaddr.sun_path,
                strerror(errno));
        close(sockfd);
        return -1;
    }
    return sockfd;
}

int init_tcp_sock(const char *server_host, int server_port) {
    struct hostent *host;
    struct sockaddr_in serv_addr;

    if ((host = gethostbyname(server_host)) == NULL) {
        fprintf(stderr, "gethostbyname failed. errmsg=%d:%s\n", errno, strerror(errno));
        return -1;
    }

    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "create socket error. errmsg=%d:%s\n", errno, strerror(errno));
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr = *((struct in_addr *)host->h_addr);
    bzero(&(serv_addr.sin_zero), 8);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "Failed to connect. errmsg=%d:%s\n", errno, strerror(errno));
        close(sockfd);
        return -1;
    }
    return sockfd;
}

std::string sendReceive(std::string& command, int sockfd) {
    char recv_buf[MAX_MEM_BUFFER_SIZE];
    ssize_t send_bytes;

    if (!command.empty()) {
        if ((send_bytes = write(sockfd, command.c_str(), command.length() + 1)) == -1) {
            std::cerr << "send error: " << errno << ":" << strerror(errno) << " \n" << std::endl;
            exit(1);
        }
        int len = recv(sockfd, recv_buf, MAX_MEM_BUFFER_SIZE, 0);
        if (len < 0) {
            fprintf(stderr, "Connection was broken: %s\n", strerror(errno));
            exit(1);
        } else if (len == 0) {
            printf("Connection has been closed\n");
            exit(0);
        } else {
            std::string response;
            for (int i = 0; i < len; i++) {
                if (recv_buf[i] == '\0') {
                    break;
                } else {
                    response.push_back(recv_buf[i]);
                }
            }
            memset(recv_buf, 0, MAX_MEM_BUFFER_SIZE);
            return response;
        }
    }
    return std::string();
}

void test1(int sockfd, std::string table_name, int sum, bool index) {
    // 创建表
    std::string send = "CREATE TABLE " + table_name + " (c1 int, c2 char(30));";
    send = sendReceive(send, sockfd);
    //std::cout << send << std::endl;
    // 加入索引
    if (index) {
        send = "CREATE INDEX " + table_name + " (c1);";
        send = sendReceive(send, sockfd);
    }

    //std::cout << send << std::endl;
    // 插入值
    
    for (int i=0;i<sum;i++) {
        std::string send;
        char buffer[20];
        char buffer2[20];
        buffer[0] = 'a';
        std::sprintf(buffer+1, "%d", i);
        std::sprintf(buffer2, "%d", i);
        send += "INSERT INTO " + table_name + " VALUES (";
        send += buffer2;
        send += ",\'";
        send += buffer;
        send += "\');";
        send = sendReceive(send, sockfd);
        //send = std::string() + "SELECT * FROM " + table_name + " WHERE c1 = " + buffer2 + ";";
        //send = sendReceive(send, sockfd);
        //std::cout << send << std::endl;
    }
    // 结束输出查看
    // send = "SELECT * FROM  " + table_name + " ;";
    // send = sendReceive(send, sockfd);
    // std::cout << send << std::endl;
    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    // 查询
    for (int i=0;i<sum;i+=3) {
        std::string send;
        //char buffer[20];
        char buffer2[20];
        //buffer[0] = 'a';
        //std::sprintf(buffer+1, "%d", i);
        std::sprintf(buffer2, "%d", i);
        send = std::string() + "SELECT * FROM  " + table_name + " WHERE c1 = " + buffer2 + ";";
        send = sendReceive(send, sockfd);
        // send = std::string() + "SELECT * FROM  " + table_name + " WHERE c1 = " + buffer2 + ";";
        // send = sendReceive(send, sockfd);
        std::cout << "input is:" << buffer2 << " ans: " << std::endl << send << std::endl;
        outfile << "input is:" << buffer2 << " ans: " << std::endl << send << std::endl;

    }
    // 删除
    // for (int i=0;i<sum-50;i++) {
    //     std::string send;
    //     //char buffer[20];
    //     char buffer2[20];
    //     //buffer[0] = 'a';
    //     //std::sprintf(buffer+1, "%d", i+1);
    //     std::sprintf(buffer2, "%d", i);
    //     send = std::string() + "DELETE FROM " + table_name + " WHERE c1 = " + buffer2 + ";";
    //     send = sendReceive(send, sockfd);
    //     send = std::string() + "SELECT * FROM " + table_name + " WHERE c1 = " + buffer2 + ";";
    //     send = sendReceive(send, sockfd);
    //     //std::cout << send << std::endl;
    //     outfile << "input is:" << buffer2 << " ans: " << std::endl << send << std::endl;
    // }
    // 更新
    for (int i=0;i<sum;i++) {
        std::string send;
        char buffer[20];
        char buffer2[20];
        buffer[0] = 'b';
        std::sprintf(buffer+1, "%d", i);
        std::sprintf(buffer2, "%d", i);
        send = std::string() + "UPDATE " + table_name + " SET c2 = \'" + buffer + "\' WHERE c1 = " + buffer2 + ";";
        send = sendReceive(send, sockfd);
        send = std::string() + "SELECT * FROM " + table_name + " WHERE c1 = " + buffer2 + ";";
        send = sendReceive(send, sockfd);
        //std::cout << send << std::endl;
        outfile << "input is:" << buffer2 << " ans: " << std::endl << send << std::endl;
    }
    // 结束输出查看
    send = "SELECT * FROM  " + table_name + " ;";
    send = sendReceive(send, sockfd);
    std::cout << send << std::endl;
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    // 计算并输出运行时间
    std::cout << "Time difference = " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() 
              << "[µs]" << std::endl;
    std::cout << "Time difference = " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() 
              << "[ms]" << std::endl;
}



int main(int argc, char *argv[]) {
    int ret = 0;  // set_terminal_noncanonical();
                  //    if (ret < 0) {
                  //        printf("Warning: failed to set terminal non canonical. Long command may be "
                  //               "handled incorrect\n");
                  //    }

    const char *unix_socket_path = nullptr;
    const char *server_host = "127.0.0.1";  // 127.0.0.1 192.168.31.25
    int server_port = PORT_DEFAULT;
    int opt;

    while ((opt = getopt(argc, argv, "s:h:p:")) > 0) {
        switch (opt) {
            case 's':
                unix_socket_path = optarg;
                break;
            case 'p':
                char *ptr;
                server_port = (int)strtol(optarg, &ptr, 10);
                break;
            case 'h':
                server_host = optarg;
                break;
            default:
                break;
        }
    }

    // const char *prompt_str = "RucBase > ";

    int sockfd, send_bytes;
    // char send[MAXLINE];

    if (unix_socket_path != nullptr) {
        sockfd = init_unix_sock(unix_socket_path);
    } else {
        sockfd = init_tcp_sock(server_host, server_port);
    }
    if (sockfd < 0) {
        return 1;
    }
    //                 "Supported SQL syntax:\n"
    //                "  command ;\n"
    //                "command:\n"
    //                "  CREATE TABLE table_name (column_name type [, column_name type ...])\n"
    //                "  DROP TABLE table_name\n"
    //                "  CREATE INDEX table_name (column_name)\n"
    //                "  DROP INDEX table_name (column_name)\n"
    //                "  INSERT INTO table_name VALUES (value [, value ...])\n"
    //                "  DELETE FROM table_name [WHERE where_clause]\n"
    //                "  UPDATE table_name SET column_name = value [, column_name = value ...] [WHERE where_clause]\n"
    //                "  SELECT selector FROM table_name [WHERE where_clause]\n"
    //                "type:\n"
    //                "  {INT | FLOAT | CHAR(n)}\n"
    //                "where_clause:\n"
    //                "  condition [AND condition ...]\n"
    //                "condition:\n"
    //                "  column op {column | value}\n"
    //                "column:\n"
    //                "  [table_name.]column_name\n"
    //                "op:\n"
    //                "  {= | <> | < | > | <= | >=}\n"
    //                "selector:\n"
    //                "  {* | column [, column ...]}\n";

    if (!outfile) {
        std::cerr << "打开文件失败" << std::endl;
        return 1;
    }
    // test1(sockfd, "table1", 5000, 0);
    test1(sockfd, "table2", 20000, 1);

    close(sockfd);
    printf("Bye.\n");
    return 0;
}