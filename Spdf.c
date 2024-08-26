#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

void create_directory_if_not_exists(const char *dir);
void sigchld_handler(int signo);
void process_client(int client_sock);
void handle_error(const char *msg);
void handle_ufile(int client_sock, const char *command);
void handle_dfile(int client_sock, const char *command);
void handle_rmfile(int client_sock, const char *command);
void handle_dtar(int client_sock, const char *command);
void handle_display(int client_sock, const char *command);
int wait_for_ack(int sock);
int get_file_type(const char *filename);

int main()
{
}