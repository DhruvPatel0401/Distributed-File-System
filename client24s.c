#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#define IP "10.60.8.51"
#define PORT 41581
#define BUFFER_SIZE 2048
#define ACK_BUFFER_SIZE 10
#define TIMEOUT_SECONDS 10 // Timeout duration in seconds
#define ACK_MESSAGE "ACK"

void handle_ufile(int sock, const char *command);
void handle_dfile(int sock, const char *command);
void handle_rmfile(int sock, const char *command);
void handle_dtar(int sock, const char *command);
void handle_display(int sock, const char *command);
int is_valid_extension(const char *filename);
int wait_for_ack(int sock);

int main()
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char command[BUFFER_SIZE];

    // Creating socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Socket creation error\n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, IP, &serv_addr.sin_addr) < 0)
    {
        fprintf(stderr, " inet_pton() has failed\n");
        exit(2);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Connection failed\n");
        return -1;
    }

    printf("Connected to server.....\n");

    while (1)
    {
        printf("client24s$ ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;

        // Handle ufile command
        if (strncmp(command, "ufile ", 6) == 0)
        {
            handle_ufile(sock, command);
        }
        // Handle dfile command
        else if (strncmp(command, "dfile ", 6) == 0)
        {
            handle_dfile(sock, command);
        }
        // Handle rmfile command
        else if (strncmp(command, "rmfile ", 7) == 0)
        {
            handle_rmfile(sock, command);
        }
        // Handle dtar command
        else if (strncmp(command, "dtar ", 5) == 0)
        {
            handle_dtar(sock, command);
        }
        // Handle display command
        else if (strncmp(command, "display ", 8) == 0)
        {
            handle_display(sock, command);
        }
        else
        {
            printf("Invalid command\n");
        }
    }

    close(sock);
    return 0;
}

void handle_ufile(int sock, const char *command)
{
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char formatted_path[BUFFER_SIZE];
    char new_command[BUFFER_SIZE];

    // Extract the filename and destination path from the command
    sscanf(command, "ufile %s %s", filename, destination_path);

    // Validate and format the destination path
    if (strncmp(destination_path, "~smain", 6) == 0)
    {
        if (destination_path[6] == '\0') // Path is "~smain" or "~/smain"
        {
            snprintf(formatted_path, sizeof(formatted_path), "~/smain/");
        }
        else if (destination_path[6] == '/') // Path is "~smain/" or "~/smain/"
        {
            snprintf(formatted_path, sizeof(formatted_path), "~/smain%s", destination_path + 6);
        }
        else
        {
            printf("Invalid destination path.\n");
            return;
        }
    }
    else if (strncmp(destination_path, "~/smain", 7) == 0)
    {
        if (destination_path[7] == '\0') // Path is "~/smain"
        {
            snprintf(formatted_path, sizeof(formatted_path), "~/smain/");
        }
        else if (destination_path[7] == '/') // Path is "~/smain/"
        {
            snprintf(formatted_path, sizeof(formatted_path), "~/smain%s", destination_path + 7);
        }
        else
        {
            printf("Invalid destination path.\n");
            return;
        }
    }
    else
    {
        // Path does not start with "~smain" or "~/smain"
        printf("Invalid destination path. Destination path must start with '~smain/' or '~smain' or '~/smain/' or '~/smain'.\n");
        return;
    }

    // Check if the file extension is valid
    if (is_valid_extension(filename) == -1)
    {
        printf("Invalid file type. Only .txt, .c, and .pdf files are allowed.\n");
        return;
    }

    // Open the file to determine its size
    FILE *file = fopen(filename, "rb");
    if (file == NULL)
    {
        printf("File %s does not exist.\n", filename);
        return;
    }

    // Determine the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allocate a dynamic buffer based on the file size
    char *buffer = (char *)malloc(file_size);
    if (buffer == NULL)
    {
        printf("Memory allocation failed\n");
        fclose(file);
        return;
    }

    // Read the file content into the buffer
    size_t bytesRead = fread(buffer, 1, file_size, file);
    if (bytesRead != file_size)
    {
        printf("Failed to read the entire file\n");
        free(buffer);
        fclose(file);
        return;
    }

    // Replace the old destination path in the command with the new formatted path
    snprintf(new_command, sizeof(new_command), "ufile %s %s", filename, formatted_path);
    // Send the command to the server
    if (send(sock, new_command, strlen(new_command), 0) < 0)
    {
        perror("Send command error");
        close(sock);
        return;
    }
    if (wait_for_ack(sock) < 0)
    {
        close(sock);
        return;
    }

    // Send the file size to the server
    long file_size_network = htonl(file_size); // Convert to network byte order
    if (send(sock, &file_size_network, sizeof(file_size_network), 0) < 0)
    {
        perror("Send file size error");
        close(sock);
        return;
    }
    if (wait_for_ack(sock) < 0)
    {
        close(sock);
        return;
    }

    // Send the file content to the server
    if (send(sock, buffer, file_size, 0) < 0)
    {
        perror("Send file content error");
        close(sock);
        return;
    }
    if (wait_for_ack(sock) < 0)
    {
        close(sock);
        return;
    }

    // Free the buffer and close the file after sending
    free(buffer);
    fclose(file);
}

void handle_dfile(int sock, const char *command)
{
}

void handle_rmfile(int sock, const char *command)
{
}

void handle_dtar(int sock, const char *command)
{
}

void handle_display(int sock, const char *command)
{
}

// Helper function to check if the file extension is valid
int is_valid_extension(const char *filename)
{
    // Find the last dot in the filename
    const char *ext = strrchr(filename, '.');
    if (ext == NULL || ext == filename)
    {
        return -1; // No extension found or filename starts with a dot
    }

    // Check if the extension is valid
    return (strcmp(ext, ".txt") == 0 || strcmp(ext, ".c") == 0 || strcmp(ext, ".pdf") == 0);
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