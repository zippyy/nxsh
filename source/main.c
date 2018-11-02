#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include <switch.h>
#include <nxsh.h>

int main(int argc, char **argv) {
    consoleInit(NULL);
    nifmInitialize();
    socketInitializeDefault();

    printf("                    __  \r\n");
    printf("   ____  _  _______/ /_ \r\n");
    printf("  / __ \\| |/_/ ___/ __ \\\r\n");
    printf(" / / / />  <(__  ) / / /\r\n");
    printf("/_/ /_/_/|_/____/_/ /_/ \r\n");
    printf("==========================\r\n");
    printf("Welcome to nxsh %s\r\n", NXSH_VERSION);  
    printf("==========================\r\n\r\n");
    consoleUpdate(NULL);

    int rc;
    struct sockaddr_in serv_addr;
    
    // Create socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        printf("Failed to initialize socket\n");
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(5050);

    // Reuse address
    int yes = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind to the socket
    rc = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (rc != 0) {
        printf("Failed to bind: error %d\n", errno);
    }

    // Listen on the appropriate address
    rc = listen(listenfd, 5);
    if (rc != 0) {
        printf("Failed to listen\n");
    }
    else {
        char hostname[128];
        gethostname(hostname, sizeof(hostname));
        printf("Listening on %s:%d...\n", hostname, 5050);
        consoleUpdate(NULL);

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Main loop
        for (;;) {

            int connfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
            printf("New connection established from %s\n", inet_ntoa(client_addr.sin_addr));
            consoleUpdate(NULL);

            // Send the welcome message
            send(connfd, NXSH_SEPARATOR, strlen(NXSH_SEPARATOR)+1, 0);
            send(connfd, NXSH_WELCOME, strlen(NXSH_WELCOME)+1, 0);
            send(connfd, NXSH_SEPARATOR, strlen(NXSH_SEPARATOR)+1, 0);

            // Here is where we'd ideally implement some multithreading logic
            nxsh_session(connfd);
        }
    }

    consoleUpdate(NULL);
    socketExit();
    nifmExit();
    consoleExit(NULL);

    return 0;
}

/*
    Initialize a new session
    @param connfd - client socket file descriptor
*/
void nxsh_session(int connfd) {

    char *recv_buf = malloc(sizeof(char) * 1024);
    size_t len;
    char *tok, *command_buf;
    char *argv[128];
    size_t argc;

    for (;;) {
        len = recv(connfd, recv_buf, 1024, 0);

        // Error occurred on socket receive
        if (len <= 0) {
            close(connfd);
            break;
        }
        else {
            
            // Strip the newline character, if it exists
            if (recv_buf[len-1] == '\n')
                recv_buf[len-1] = '\0';
            else
                recv_buf[len] = '\0';

            trim(recv_buf);
            len = strlen(recv_buf);
            argc = 0;

            // Command passed with arguments
            if (strstr(recv_buf, " ") != NULL) {
                char *cmd = strtok(recv_buf, " ");
                len = strlen(cmd);

                // Copy command into buffer
                command_buf = malloc(sizeof(char) * (len + 1));
                strcpy(command_buf, cmd);

                // Copy arguments into pointer array argv
                argc = 0; 
                while ((tok = strtok(NULL, " ")) != NULL) {
                    len = strlen(tok);
                    argv[argc] = malloc(sizeof(char) * (len + 1));
                    strcpy(argv[argc], tok);
                    argc++;
                }
            }

            // No arguments passed
            else {
                len = strlen(recv_buf);
                command_buf = malloc(len + 1);
                strcpy(command_buf, recv_buf);
            }

            if (strlen(command_buf) != 0) {

                char *output = nxsh_command(command_buf, argc, argv);

                // Exit the session
                if (output != NULL && strcmp(output, "_nxsh_exit") == 0) {
                    close(connfd);
                    free(output);
                    break;
                }
                else {

                    // Send the response to the client
                    if (output != NULL) {
                        send(connfd, output, strlen(output)+1, 0);
                        free(output);
                    }
                    send(connfd, NXSH_SEPARATOR, strlen(NXSH_SEPARATOR)+1, 0);
                }
            }

        }
    }

    free(recv_buf);
}

/*
    Respond to a given command (this can be done better :o)
    @param command - command string
    @param argc - argument count
    @param argv - array of pointers to arguments
    
    @returns malloc'd output buffer to be sent back to the client
*/
char *nxsh_command(char *command, int argc, char **argv) {

    char *output = NULL;

    // Build escape sequence for testing
    char escape[2];
    escape[0] = 0x04;
    escape[1] = '\0';

    // Directory listing
    if (strcmp(command, "ls") == 0) {
        output = nxsh_ls(argc, argv);
    }

    // Print working directory
    else if (strcmp(command, "pwd") == 0) {
        char *path = nxsh_cwd();
        size_t len = strlen(path);

        output = malloc(sizeof(char) * (len+3));
        strcpy(output, path);
        output[len++] = '\r';
        output[len++] = '\n';
        output[len] = '\0';
    }

    // Change directory
    else if (strcmp(command, "cd") == 0) {
        output = nxsh_cd(argc, argv);
    }

    // Make directory
    else if (strcmp(command, "mkdir") == 0) {
        output = nxsh_mkdir(argc, argv);
    }

    // Remove file/directory
    else if (strcmp(command, "rm") == 0) {
        output = nxsh_rm(argc, argv);
    }

    // Copy file/directory
    else if (strcmp(command, "cp") == 0) {
        output = nxsh_cp(argc, argv);
    }

    // Move file/directory
    else if (strcmp(command, "mv") == 0) {
        output = nxsh_cp(argc, argv);
    }

    // Print out file contents
    else if (strcmp(command, "cat") == 0) {
        output = nxsh_cat(argc, argv);
    }

    // Display help
    else if (strcmp(command, "help") == 0) {
        output = malloc(sizeof(char) * strlen(NXSH_HELP)+1);
        strcpy(output, NXSH_HELP);
    }

    // Display version number
    else if (strcmp(command, "version") == 0) {
        output = malloc(sizeof(char) * strlen(NXSH_VERSION)+16);
        sprintf(output, "nxsh version %s\r\n", NXSH_VERSION);
    }

    // End the session
    else if (strcmp(command, "exit") == 0) {
        return error("_nxsh_exit");
    }
    
    // End the session (Ctrl+D)
    else if (strcmp(command, escape) == 0) {
        return error("_nxsh_exit");
    }

    else {
        output = error("Error: command not found\r\n");
    }
    
    // Free the command arguments, as they're no longer needed
    for (int i=0; i<argc; i++) {
        free(argv[i]);
    }
    free(command);
    
    return output;
}