#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <ncurses.h>
#include <ctype.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int max_length_input = 50;
int max_commands = 50;
int max_arguments = 10;
int command_count = 0;

int count_words(const char *line) {
    int count = 0;
    int in_word = 0;

    while (*line) {
        if (isspace(*line)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            count++;
        }
        line++;
    }

    return count;
}

void vi_editor(const char *filename, int *line_count, int *word_count, int *char_count) {
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();

    // Create or open the file for editing
    int file_descriptor = open(filename, O_RDWR | O_CREAT, 0400 | 0200 | 0040 | 0004);

    if (file_descriptor == -1) {
        mvprintw(0, 0, "Error opening/creating file: %s", filename);
        refresh();
        getch();
        endwin();
        return;
    }

    // Load the file content
    FILE *file = fdopen(file_descriptor, "r+");
    if (!file) {
        mvprintw(0, 0, "Error opening/creating file: %s", filename);
        refresh();
        getch();
        endwin();
        return;
    }

    // Read the file content into a buffer
    char *buffer = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&buffer, &len, file)) != -1) {
        printw("%s", buffer);
    }

    fclose(file);

    int cursor_x = 0, cursor_y = 0;
    move(cursor_y, cursor_x);

    int ch;
    while ((ch = getch()) != 27) { // 27 is ASCII code for ESC key
        switch (ch) {
            case KEY_UP:
                if (cursor_y > 0) {
                    cursor_y--;
                }
                break;
            case KEY_DOWN:
                cursor_y++;
                break;
            case KEY_LEFT:
                if (cursor_x > 0) {
                    cursor_x--;
                }
                break;
            case KEY_RIGHT:
                cursor_x++;
                break;
            case KEY_DC: // Delete key
                mvdelch(cursor_y, cursor_x);
                break;
            case 19: // Ctrl + S
                // Save the file
                file = fopen(filename, "w");
                if (!file) {
                    mvprintw(0, 0, "Error saving file: %s", filename);
                    refresh();
                    getch();
                    endwin();
                    return;
                }
                for (int i = 0; i < LINES; i++) {
                    char line[COLS];
                    mvinnstr(i, 0, line, COLS);
                    fprintf(file, "%s\n", line);
                }
                fclose(file);
                break;
            case 24: // Ctrl + X
                endwin();
                *line_count = LINES;
                *word_count = *char_count = 0;
                for (int i = 0; i < LINES; i++) {
                    char line[COLS];
                    mvinnstr(i, 0, line, COLS);
                    *char_count += strlen(line);
                    *word_count += count_words(line);
                }
                return;
            default:
                mvaddch(cursor_y, cursor_x, ch);
                cursor_x++;
                break;
        }

        move(cursor_y, cursor_x);
        refresh();
    }
    file = fopen(filename, "w");
    if (!file) {
        mvprintw(0, 0, "Error saving file: %s", filename);
        refresh();
        getch();
        endwin();
        return;
    }

    for (int i = 0; i < LINES; i++) {
        char line[COLS];
        mvinnstr(i, 0, line, COLS);
        fprintf(file, "%s\n", line);
    }

    fclose(file);

    endwin();
}

void execute_vi(char *filename) {
    int line_count, word_count, char_count;
    vi_editor(filename, &line_count, &word_count, &char_count);
    printf("Number of lines: %d\n", line_count);
    printf("Number of words: %d\n", word_count);
    printf("Number of characters: %d\n", char_count);
}

void process_token(char *token) {
    // removes leading spaces
    int i = 0, j = 0;
    size_t token_len = strlen(token);
    while (j < token_len && token[j] == ' ') {
        j++;
    }
    for (i = 0; j < token_len; i++, j++) {
        token[i] = token[j];
    }
    token[i] = '\0';

    // remove trailing spaces
    int new_token_len = strlen(token);
    if (token[new_token_len - 1] == ' ') {
        token[new_token_len - 1] = '\0';
    }
}

char **check_pipes(char *temp_command) {
    char **pipe_commands = (char **)malloc(max_length_input * sizeof(char *));
    if (pipe_commands == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < max_commands; i++) {
        pipe_commands[i] = (char *)malloc(max_length_input * sizeof(char));
        if (pipe_commands[i] == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
    }
    char *token = strtok(temp_command, "|");

    while (token != NULL && command_count < 100) {
        process_token(token);
        strcpy(pipe_commands[command_count], token);
        token = strtok(NULL, "|");
        command_count++;
    }
    return pipe_commands;
}

void executeCommand(char *command, char *arguments[], int background) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("Fork failed");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        if (execvp(command, arguments) == -1) {
            perror("Command execution failed");
            exit(EXIT_FAILURE);
        }
    } else {
        if (background == 0) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                printf("Child process exited with status %d\n", WEXITSTATUS(status));
            } else {
                printf("Child process did not terminate normally\n");
            }
        }
    }
}

void execute_single(char *single_input) {
    int and_checker = 0;
    size_t len_inp = strlen(single_input);
    if (single_input[len_inp - 1] == '&') {
        and_checker = 1;
    }

    char *token = strtok(single_input, " ");
    char *command = token;
    char *arguments[max_arguments];

    int argCount = 0;

    while (token != NULL) {
        arguments[argCount++] = token;
        token = strtok(NULL, " ");
    }

    int background = 0;
    if (argCount > 1 && strcmp(arguments[argCount - 1], "&") == 0) {
        background = 1;
        arguments[argCount - 1] = NULL;
    } else {
        if (and_checker == 1) {
            background = 1;
        }
        arguments[argCount] = NULL;
        if (background == 1) {
            size_t temp_len = strlen(arguments[argCount - 1]);
            arguments[argCount - 1][temp_len - 1] = '\0';
        }
    }

    if (strcmp(command, "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("getcwd() error");
        }
    } else if (strcmp(command, "cd") == 0) {
        if (argCount < 2) {
            fprintf(stderr, "cd: missing argument\n");
        } else {
            if (chdir(arguments[1]) != 0) {
                perror("chdir() error");
            }
        }
    } else if (strcmp(command, "mkdir") == 0) {
        if (arguments[1] == NULL) {
            fprintf(stderr, "mkdir: expected Directory Name in the argument\n");
        } else {
            pid_t pid = fork();

            if (pid == -1) {
                perror("Fork failed");
                exit(EXIT_FAILURE);
            } else if (pid == 0) {
                // This is the child process
                execlp("mkdir", "mkdir", arguments[1], NULL);

                // If execlp fails, it will reach this point
                perror("execlp");
                exit(EXIT_FAILURE);
            } else {
                // This is the parent process
                int status;
                waitpid(pid, &status, 0);

                if (WIFEXITED(status)) {
                    printf("Child process exited with status %d\n", WEXITSTATUS(status));
                } else {
                    printf("Child process did not terminate normally\n");
                }
            }
        }
    } else if (strcmp(command, "ls") == 0) {
        executeCommand("/bin/ls", arguments, background);
    } else if (strcmp(command, "exit") == 0) {
        exit(0);
    } else if (strcmp(command, "help") == 0) {
        printf("Commands:\n");
        printf("pwd = print working directory\n");
        printf("ls = list all directories\n");
        printf("cd = change directory\n");
        printf("likewise\n");

    } else {
        executeCommand(command, arguments, background);
    }

    if (!background) {
        wait(NULL);
    }
}

void execute_pipe(char **pipe_commands) {
    int num_commands = command_count;

    int fd[2];
    int in = 0;

    for (int i = 0; i < num_commands; i++) {
        char *command = strtok(pipe_commands[i], " ");
        char *arguments[max_arguments];
        int argCount = 0;

        while (command != NULL) {
            arguments[argCount++] = command;
            command = strtok(NULL, " ");
        }
        arguments[argCount] = NULL;

        if (i < num_commands - 1) {
            if (pipe(fd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {
            if (i > 0) {
                dup2(in, STDIN_FILENO);
                close(in);
            }

            if (i < num_commands - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            execvp(arguments[0], arguments);
            perror("execvp");
            exit(EXIT_FAILURE);
        } else {
            if (i > 0) {
                close(in);
            }

            if (i < num_commands - 1) {
                close(fd[1]);
                in = fd[0];
            }

            wait(NULL);
        }
    }
}

int main() {
    char *input_command;
    while (true) {
        input_command = readline("my_shell> ");
        if (!input_command) {
            break; // User pressed Ctrl+D to exit
        }

        // Handle multi-line commands
        while (input_command && input_command[strlen(input_command) - 1] == '\\') {
            input_command[strlen(input_command) - 1] = ' '; // Remove '\'
            char *next_line = readline("> ");
            if (!next_line) {
                break; // User pressed Ctrl+D to exit
            }
            input_command = realloc(input_command, strlen(input_command) + strlen(next_line) + 1);
            strcat(input_command, next_line);
            free(next_line);
        }

        // Add command to history
        add_history(input_command);

        // Process the command
        command_count = 0;
        char temp_command[max_length_input];
        strcpy(temp_command, input_command);
        if (strcmp(input_command, "exit") == 0) {
            free(input_command);
            exit(0);
        }
        char **pipe_commands = check_pipes(temp_command);
        if (command_count == 1) {
            char single_input[max_length_input];
            strcpy(single_input, input_command);
            if (strncmp(single_input, "vi", 2) == 0) {
                execute_vi(single_input + 3); // Skip "vi " in the command
            } else {
                execute_single(single_input);
            }
        } else {
            execute_pipe(pipe_commands);
        }

        free(input_command);
    }

    return 0;
}
