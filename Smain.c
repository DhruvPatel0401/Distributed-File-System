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

#define SERVER_IP "10.60.8.51"
#define PORT 41581
#define STEXT_PORT 51238
#define SPDF_PORT 39462
#define BUFFER_SIZE 2048
#define LOCAL_DIR "~/smain"
#define ACK_MESSAGE "ACK"
#define ACK_BUFFER_SIZE 10
#define TIMEOUT_SECONDS 10

void create_directory_if_not_exists(const char *dir);
void handle_error(const char *msg);
void sigchld_handler(int signo);
void send_ack(int sock);
void process_client(int client_sock);
void handle_ufile(int client_sock, const char *command);
void handle_dfile(int client_sock, const char *command);
void handle_rmfile(int client_sock, const char *command);
void handle_dtar(int client_sock, const char *command);
void handle_display(int client_sock, const char *command);
int wait_for_ack(int sock);
int get_file_type(const char *filename);

int main()
{
    // Resolve the home directory and append "/smain" to it
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL)
    {
        fprintf(stderr, "Failed to get HOME environment variable.\n");
        exit(EXIT_FAILURE);
    }

    char smain_dir[BUFFER_SIZE];
    snprintf(smain_dir, sizeof(smain_dir), "%s/smain", home_dir);

    // Create ~/smain directory if it doesn't exist
    create_directory_if_not_exists(smain_dir);

    int server_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    // Set up SIGCHLD handler
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR)
    {
        perror("Signal handler setup failed");
        exit(EXIT_FAILURE);
    }

    // Creating socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        handle_error("socket");
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        close(server_fd);
        handle_error("bind");
    }

    if (listen(server_fd, 20) < 0)
    {
        close(server_fd);
        handle_error("listen");
    }

    printf("Main Server is listening on %s:%d\n", SERVER_IP, PORT);

    while (1)
    {
        if ((client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0)
        {
            perror("Accept failed");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("Fork failed");
            close(client_sock);
        }
        else if (pid == 0)
        {
            // Child process
            close(server_fd);            // Close the listening socket in the child
            process_client(client_sock); // Handle the client
            close(client_sock);          // Close the connection
            exit(0);                     // Exit child process when done
        }
    }

    close(server_fd);
    return 0;
}

void create_directory_if_not_exists(const char *dir)
{
    struct stat st = {0};

    // Save the current umask
    mode_t old_umask = umask(0);

    // Check if the directory exists
    if (stat(dir, &st) == -1)
    {
        // Directory does not exist, so create it
        if (mkdir(dir, 0700) != 0)
        {
            perror("Failed to create directory");
            umask(old_umask);
            exit(EXIT_FAILURE);
        }
    }
}

void handle_error(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// SIGCHLD handler to clean up zombie processes
void sigchld_handler(int signo)
{
    (void)signo; // Suppress unused parameter warning
    int status;

    // Reap all terminated child processes
    while (waitpid(-1, &status, WNOHANG) > 0)
    {
        if (WIFEXITED(status))
        {
            printf("Client exited with status %d\n", WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status))
        {
            printf("Client terminated by signal %d\n", WTERMSIG(status));
        }
        else if (WIFSTOPPED(status))
        {
            printf("Client stopped by signal %d\n", WSTOPSIG(status));
        }
    }
}

// Function to send acknowledgment to client
void send_ack(int client_sock)
{
    if (send(client_sock, ACK_MESSAGE, strlen(ACK_MESSAGE), 0) < 0)
    {
        perror("Send acknowledgment error");
    }
}

// Handle client connection
void process_client(int client_sock)
{
    char command[BUFFER_SIZE];
    int n;

    while (1)
    {
        // Receive the command from the client
        n = recv(client_sock, command, sizeof(command) - 1, 0);
        if (n <= 0)
        {
            if (n == 0)
            {
                printf("Client disconnected.\n");
            }
            else
            {
                perror("Failed to receive command");
            }
            break;
        }
        command[n] = '\0';     // Null-terminate the command
        send_ack(client_sock); // Send acknowledgment

        // Handle ufile command
        if (strncmp(command, "ufile ", 6) == 0)
        {
            handle_ufile(client_sock, command);
        }
        // Handle dfile command
        else if (strncmp(command, "dfile ", 6) == 0)
        {
            handle_dfile(client_sock, command);
        }
        // Handle rmfile command
        else if (strncmp(command, "rmfile ", 7) == 0)
        {
            handle_rmfile(client_sock, command);
        }
        // Handle dtar command
        else if (strncmp(command, "dtar ", 5) == 0)
        {
            handle_dtar(client_sock, command);
        }
        // Handle display command
        else if (strncmp(command, "display ", 8) == 0)
        {
            handle_display(client_sock, command);
        }
        else
        {
            printf("Invalid command: %s\n", command);
        }
    }
}

// Handle ufile command
void handle_ufile(int client_sock, const char *command)
{
    char filename[BUFFER_SIZE];
    char dest_path[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];
    long file_size_network;
    long file_size;
    char *buffer;

    // Extract filename and destination path from the command
    if (sscanf(command, "ufile %s %s", filename, dest_path) != 2)
    {
        printf("Invalid command format.\n");
        return;
    }

    // Expand home directory and append smain
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL)
    {
        fprintf(stderr, "Failed to get HOME environment variable.\n");
        return;
    }

    // Extract the relative destination path
    char *relative_path = strstr(dest_path, "~/smain");
    if (relative_path != NULL)
    {
        // Move past "~/smain" to get the relative path
        relative_path += strlen("~/smain");
        // Copy the relative path back into dest_path
        snprintf(dest_path, sizeof(dest_path), "%s", relative_path);
    }
    else
    {
        printf("Invalid destination path: %s\n", dest_path);
        return;
    }

    // Receive the file size from the client
    if (recv(client_sock, &file_size_network, sizeof(file_size_network), 0) <= 0)
    {
        perror("Error receiving file size");
        return;
    }
    file_size = ntohl(file_size_network); // Convert from network byte order
    send_ack(client_sock);                // Send acknowledgment

    // Allocate memory to store the incoming file content
    buffer = (char *)malloc(file_size);
    if (buffer == NULL)
    {
        printf("Memory allocation failed\n");
        return;
    }

    // Receive the file content from the client
    long total_bytes_received = 0;
    int bytes_received;
    while (total_bytes_received < file_size)
    {
        bytes_received = recv(client_sock, buffer + total_bytes_received, file_size - total_bytes_received, 0);
        if (bytes_received <= 0)
        {
            perror("Error receiving file content");
            free(buffer);
            return;
        }
        total_bytes_received += bytes_received;
    }
    send_ack(client_sock); // Send acknowledgment

    // Check file extension and get the corresponding integer
    int file_type = get_file_type(filename);
    // Handle based on file type
    switch (file_type)
    {
    case 0: // .c file
        break;
    case 1: // .txt file
        break;
    case 2: // .pdf file
        break;
    default:
        printf("Unexpected file type: %d\n", file_type);
        break;
    }
}

void handle_dfile(int client_sock, const char *command)
{
}

void handle_rmfile(int client_sock, const char *command)
{
}

void handle_dtar(int client_sock, const char *command)
{
}

void handle_display(int client_sock, const char *command)
{
}

// Function to receive acknowledgment from server
int wait_for_ack(int sock)
{
    fd_set read_fds;
    struct timeval timeout;
    char ack_buffer[ACK_BUFFER_SIZE];

    // Initialize the file descriptor set
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);

    // Set timeout duration
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    // Wait for data to be available on the socket
    int select_result = select(sock + 1, &read_fds, NULL, NULL, &timeout);

    if (select_result < 0)
    {
        perror("Select error");
        return -1;
    }
    else if (select_result == 0)
    {
        // Timeout occurred
        fprintf(stderr, "No acknowledgment received within %d seconds\n", TIMEOUT_SECONDS);
        return -1;
    }
    else
    {
        // Data is available to be read
        int bytes_received = recv(sock, ack_buffer, sizeof(ack_buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            perror("Error receiving acknowledgment");
            return -1;
        }

        ack_buffer[bytes_received] = '\0'; // Null-terminate the buffer
        return 0;
    }
}

// Helper function to check the file extension and return a corresponding integer
int get_file_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (ext == NULL)
    {
        return -1; // No extension found
    }

    // Compare the extension and return the corresponding integer
    if (strcmp(ext, ".c") == 0)
    {
        return 0; // .c file
    }
    else if (strcmp(ext, ".txt") == 0)
    {
        return 1; // .txt file
    }
    else if (strcmp(ext, ".pdf") == 0)
    {
        return 2; // .pdf file
    }
    else
    {
        return -1; // Unsupported file extension
    }
}