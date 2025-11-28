#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdio.h>

/* ========== グローバル変数 ========== */
int g_sockfd;                    // リスニングソケット
int g_max_fd;                    // 監視する最大fd
int g_next_id = 0;               // 次に割り当てるクライアントID
fd_set g_master;                 // 監視するfdの集合

typedef struct s_client {
    int     id;                  // クライアントID
    char    *buf;                // 受信バッファ
} t_client;

t_client g_clients[4096];        // クライアント情報（fdをインデックスに）

/* ========== 提供された関数 ========== */
int extract_message(char **buf, char **msg)
{
    char    *newbuf;
    int     i;

    *msg = 0;
    if (*buf == 0)
        return (0);
    i = 0;
    while ((*buf)[i])
    {
        if ((*buf)[i] == '\n')
        {
            newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
            if (newbuf == 0)
                return (-1);
            strcpy(newbuf, *buf + i + 1);
            *msg = *buf;
            (*msg)[i + 1] = 0;
            *buf = newbuf;
            return (1);
        }
        i++;
    }
    return (0);
}

char *str_join(char *buf, char *add)
{
    char    *newbuf;
    int     len;

    if (buf == 0)
        len = 0;
    else
        len = strlen(buf);
    newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
    if (newbuf == 0)
        return (0);
    newbuf[0] = 0;
    if (buf != 0)
        strcat(newbuf, buf);
    free(buf);
    strcat(newbuf, add);
    return (newbuf);
}

/* ========== エラー処理 ========== */
void fatal_error(void)
{
    write(2, "Fatal error\n", 12);
    exit(1);
}

/* ========== 全クライアントへの送信 ========== */
void send_to_all(int except_fd, char *msg)
{
    for (int fd = 0; fd <= g_max_fd; fd++)
    {
        // masterに含まれ、リスニングソケットでなく、送信元でないfdに送信
        if (FD_ISSET(fd, &g_master) && fd != g_sockfd && fd != except_fd)
        {
            send(fd, msg, strlen(msg), 0);
        }
    }
}

/* ========== 新規接続処理 ========== */
void handle_new_connection(void)
{
    struct sockaddr_in cli;
    socklen_t len = sizeof(cli);
    
    int client_fd = accept(g_sockfd, (struct sockaddr *)&cli, &len);
    if (client_fd < 0)
        return;  // 接続失敗は無視（他のクライアントに影響させない）
    
    // クライアント情報を初期化
    g_clients[client_fd].id = g_next_id++;
    g_clients[client_fd].buf = NULL;
    
    // 監視対象に追加
    FD_SET(client_fd, &g_master);
    if (client_fd > g_max_fd)
        g_max_fd = client_fd;
    
    // 到着通知を全員に送信
    char notification[64];
    sprintf(notification, "server: client %d just arrived\n", g_clients[client_fd].id);
    send_to_all(client_fd, notification);
}

/* ========== クライアント切断処理 ========== */
void handle_disconnection(int fd)
{
    // 退出通知を全員に送信
    char notification[64];
    sprintf(notification, "server: client %d just left\n", g_clients[fd].id);
    send_to_all(fd, notification);
    
    // クリーンアップ
    free(g_clients[fd].buf);
    g_clients[fd].buf = NULL;
    FD_CLR(fd, &g_master);
    close(fd);
}

/* ========== メッセージ受信処理 ========== */
void handle_message(int fd)
{
    char temp[4096];
    int bytes = recv(fd, temp, sizeof(temp) - 1, 0);
    
    if (bytes <= 0)
    {
        // 切断
        handle_disconnection(fd);
        return;
    }
    
    temp[bytes] = '\0';
    
    // バッファに追加
    g_clients[fd].buf = str_join(g_clients[fd].buf, temp);
    if (g_clients[fd].buf == NULL)
        fatal_error();  // メモリ割り当て失敗
    
    // 完全なメッセージを取り出して転送
    char *msg;
    int result;
    while ((result = extract_message(&g_clients[fd].buf, &msg)) == 1)
    {
        // "client X: " + メッセージ を作成
        char *full_msg = NULL;
        char prefix[32];
        sprintf(prefix, "client %d: ", g_clients[fd].id);
        
        full_msg = str_join(NULL, prefix);
        if (full_msg == NULL)
            fatal_error();
        full_msg = str_join(full_msg, msg);
        if (full_msg == NULL)
            fatal_error();
        
        // 送信元以外の全員に送信
        send_to_all(fd, full_msg);
        
        free(full_msg);
        free(msg);
    }
    if (result == -1)
        fatal_error();  // extract_messageでメモリエラー
}

/* ========== メイン関数 ========== */
int main(int ac, char **av)
{
    // 1. 引数チェック
    if (ac != 2)
    {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }
    
    // 2. ソケット作成
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0)
        fatal_error();
    
    // 3. アドレス設定
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(2130706433);  // 127.0.0.1
    servaddr.sin_port = htons(atoi(av[1]));
    
    // 4. バインド
    if (bind(g_sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
        fatal_error();
    
    // 5. リッスン
    if (listen(g_sockfd, 128) < 0)
        fatal_error();
    
    // 6. fd_set初期化
    FD_ZERO(&g_master);
    FD_SET(g_sockfd, &g_master);
    g_max_fd = g_sockfd;
    
    // 7. メインループ
    while (1)
    {
        fd_set read_fds = g_master;  // コピー（selectが書き換えるため）
        
        if (select(g_max_fd + 1, &read_fds, NULL, NULL, NULL) < 0)
            continue;  // シグナルで中断された場合などはリトライ
        
        for (int fd = 0; fd <= g_max_fd; fd++)
        {
            if (FD_ISSET(fd, &read_fds))
            {
                if (fd == g_sockfd)
                    handle_new_connection();
                else
                    handle_message(fd);
            }
        }
    }
    
    return 0;
}
