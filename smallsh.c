#define _POSIX_C_SOURCE 200809L
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <signal.h>

char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
bool is_token(char *input_word);

// establish empty handler for sigint as we do not want to terminate process during getline
void handle_SIGINT(int signo){
}

int main(){\

  // set needed parameters for loop
  char *line = NULL;
  size_t n = 0;
  char *prompt_string = getenv("PS1");
  char *ifs = getenv("IFS");
  char *home_path = getenv("HOME");
  char last_fg_cmd[10] = "0";
  char last_bg_cmd[10] = "";
  pid_t parent_pid = getpid();
  char parent_pid_str[8];
  sprintf(parent_pid_str, "%d", parent_pid);

  // set default value for prompt string if PS1 is not set in env
  if (!prompt_string){
    prompt_string = "";
  }

  // default value for ifs
  if (!ifs){
    ifs = " \t\n";
  }

  for (;;) {
    // find changes to any background child processes and notify user of status
    int bg_child_status;
    int bg_child_pid = waitpid(0, &bg_child_status, WNOHANG | WUNTRACED);

    // loop until there are no more background child processes with changed states
    while (bg_child_pid > 0) {

      if (WIFEXITED(bg_child_status)) {
        fprintf(stderr, "Child process %d done. Exit status %d.\n", bg_child_pid, WEXITSTATUS(bg_child_status));
      }

      else if (WIFSIGNALED(bg_child_status)) {
        fprintf(stderr, "Child process %d done. Signaled %d.\n", bg_child_pid, WTERMSIG(bg_child_status));
      }

      else if (WIFSTOPPED(bg_child_status)) {
        kill(bg_child_pid, SIGCONT);
        fprintf(stderr, "Child process %d stopped. Continuing.\n", bg_child_pid);
      }

      // ignore any other signals. break out of loop so we do not get in infinite loop
      else {
        break;
      }

      bg_child_pid = waitpid(0, &bg_child_status, WNOHANG | WUNTRACED);
    }

    // set up signal handling for sigint and sigtstp
    struct sigaction ignore_action = {0};
    struct sigaction old_SIGTSTP = {0};
    struct sigaction default_SIG = {0};
    default_SIG.sa_handler = SIG_DFL;
    ignore_action.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &ignore_action, &old_SIGTSTP);
    struct sigaction SIGINT_action = {0};
    struct sigaction oldSIGINT_action = {0};

    int errno = 0;

    // print command string
    fprintf(stderr, "%s", prompt_string);

    // set sigint to do nothing if getline has error at getline
    SIGINT_action.sa_handler = handle_SIGINT;
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, &oldSIGINT_action);

    //getline from user
    ssize_t line_length = getline(&line, &n, stdin);

    // verify not eof of stdin or error during getline
    if (feof(stdin)) exit(1);
    if (line_length == -1) {
      clearerr(stdin);
      putchar('\n');
      errno = 0;
      continue;
    }

    //reset sigint to ignore
    sigaction(SIGINT, &ignore_action, NULL);
    
    // token and loop count for word splitting
    char *token = "true";
    ssize_t loop_count = 0;

    // array for taking in input from getline. use memset to make sure there are no garbage variables
    char *input_words[512];
    memset(input_words, '\0', sizeof(input_words));

    // for redirected input/output later
    int sourceFD;
    int targetFD;
    char *word = NULL;

    while(token){
      
      // loop through input. Break up string using strtok and store each different word broken up by IFS in token
      if (loop_count == 0){
        token = strtok(line, ifs);
      }

      else {
        token = strtok(NULL, ifs);
      }
      
      // do not need to deal with comments
      if (!token || strcmp(token, "#") == 0) {
        break;
      }
      
      // perform expansion of tokens. Store in input words
      word = strdup(token);
      input_words[loop_count] = word;
      char c = input_words[loop_count][0];
      char c2 = input_words[loop_count][1];
      if((c == '~') && (c2 == '/')) { 
          input_words[loop_count] = str_gsub(&input_words[loop_count], "~", home_path);
      }
      input_words[loop_count] = str_gsub(&input_words[loop_count], "$$", parent_pid_str);
      input_words[loop_count] = str_gsub(&input_words[loop_count], "$?", last_fg_cmd);
      input_words[loop_count] = str_gsub(&input_words[loop_count], "$!", last_bg_cmd);

      loop_count ++;
    }

    //set parameters for parsing input
    ssize_t i = 0;
    char *given_command = NULL;
    bool background_process = false;
    bool red_input = false;
    char *input_file = NULL;
    bool red_output = false;
    char *output_file = NULL;
    char *user_args[512];
    memset(user_args, '\0', sizeof(user_args));
    ssize_t num_args = 0;

    while (input_words[i]) {
      
      // add to args, will remove later if it is not an arg
      user_args[i] = input_words[i];
      num_args ++;

      if (i == 0) {
        given_command = input_words[i]; // store command for use later
      }
      
      // if last word is background command, set flag, remove from args
      else if ((strcmp(input_words[i], "&") == 0) && !input_words[i+1]) {
        background_process = true;
        user_args[i] = NULL;
      }

      // function is_token will determine if words were redirect for i/o and remove from args if so
      else {
        if (!input_words[i+1] || (strcmp(input_words[i+1], "&") == 0 && !input_words[i+2])) {
          if (!is_token(input_words[i]) && is_token(input_words[i-1])) {
            if (strcmp(input_words[i-1], "<") == 0) {
              red_input = true;
              input_file = input_words[i];
              user_args[i] = user_args[i-1] = NULL;
            }

            else if (strcmp(input_words[i-1], ">") == 0) {
              red_output = true;
              output_file = input_words[i];
              user_args[i] = user_args[i-1] = NULL;
            }

            if (!is_token(input_words[i-2]) && is_token(input_words[i-3])) {
              if (strcmp(input_words[i-3], "<") == 0) {
                red_input = true;
                input_file = input_words[i-2];
                user_args[i-2] = user_args[i-3] = NULL;
              }

              else if (strcmp(input_words[i-3], ">") == 0) {
                red_output = true;
                output_file = input_words[i-2];
                user_args[i-2] = user_args[i-3] = NULL;
              }

            }
          }

        }
      }
      i++;
    }
    
    // for empty input
    if (!given_command) continue;

    // built in command for exit. Send sigint to all background child processes still running
    if (strcmp(given_command, "exit") == 0){
      if (num_args > 2) {
        fprintf(stderr, "Number of args exceeded for %s\n", given_command);
        continue;
      }
      else if (num_args == 1) {
        fprintf(stderr, "\nexit\n");
        kill(getpgrp(), SIGINT);
        exit(atoi(last_fg_cmd));
      }
      else {
        fprintf(stderr, "\nexit\n");
        kill(getpgrp(), SIGINT);
        exit(atoi(user_args[1]));
      }
    }

    // built in command for change directory using chdir
    if (strcmp(given_command, "cd") == 0){
      if (num_args > 2){
        fprintf(stderr, "Number of args exceeded for %s\n", given_command);
      }
      else if (num_args == 1) chdir(home_path);
      else chdir(user_args[1]);
      continue;
    }

    // open files for redirected i/o as determined during parsing loop
    if (red_input) {
      sourceFD = open(input_file, O_RDONLY);
      if (sourceFD == -1) {
        perror("source open()");
        continue;
      }
    }
    if (red_output) {
      targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0777);
      if (targetFD == -1) {
        perror("target open()");
        continue;
      }
    } 

    // non built in commands will be a child process. use fork() to create child
    int child_status;
    pid_t child_pid = fork();
    
    switch(child_pid){
      // handle error for fork()
      case -1:
        perror("fork(\n");
        exit(1);
        break;
      case 0:
        
        // reset adjusted signals for child processes
        sigaction(SIGINT, &default_SIG, NULL);
        sigaction(SIGTSTP, &old_SIGTSTP, NULL);

        // redirect input and output
        if (red_input) {
          int redirectIn = dup2(sourceFD, 0);
          if (redirectIn == -1) {
            perror("source dup2()");
            continue;
          }
        }

        if (red_output) {
          int redirectOut = dup2(targetFD, 1);
          if (redirectOut == -1) {
            perror("target dup2()");
            continue;
          }
        }
        close(sourceFD);
        close(targetFD);
        // execute child process with command and arguments given
        execvp(given_command, user_args);
        perror("execv");
        exit(EXIT_FAILURE);

      default:
        // handle foreground commands and update $? as appropriate
        if (!background_process) {
          child_pid = waitpid(child_pid, &child_status, 0);
          
          // handle signals for processes
          if (WIFEXITED(child_status)) {
            sprintf(last_fg_cmd, "%d", WEXITSTATUS(child_status));
          }

          else if (WIFSIGNALED(child_status)) {
            sprintf(last_fg_cmd, "%d", 128 + WTERMSIG(child_status));
          }

          if (WIFSTOPPED(child_status)) {
            kill(child_pid, SIGCONT);
            fprintf(stderr, "Child process %d stopped. Continuing \n", child_pid);
            int spawn_pid = waitpid(child_pid, &child_status, WNOHANG);
            sprintf(last_bg_cmd, "%d", spawn_pid);
          }
        }

        else {
          // background commands will not be waited for. update $!
          int spawn_pid = waitpid(child_pid, &child_status, WNOHANG);
          sprintf(last_bg_cmd, "%d", child_pid);
          }
        break;
    }
  }  
}

// is_token is used for parsing input. Will return true if the word sent is either <, > or & for i/o redirection and background processes
bool is_token(char *input_word){
    bool token_check = false;
    if (strcmp(input_word, "<") == 0) token_check = true;
    else if (strcmp(input_word, ">") == 0) token_check = true;
    else if (strcmp(input_word, "&") == 0) token_check = true;
    return token_check;
}

// str_gsub used for expansion. function was created from this video: https://www.youtube.com/watch?v=-3ty5W_6-IQ
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub){ 
  char *str = *haystack;
  size_t haystack_len = strlen(*haystack);
  size_t needle_len = strlen(needle),
         sub_len = strlen(sub);

  for (; (str = strstr(str, needle));) {
  ptrdiff_t off = str - *haystack;
    if (sub_len > needle_len){
      str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
      if (!str) goto exit;
      *haystack = str;
      str = *haystack + off;
    }

    memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
    memcpy(str, sub, sub_len);
    haystack_len = haystack_len + sub_len - needle_len;
    str += sub_len;
  }

  str = *haystack;
  if (sub_len < needle_len) {
    str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
    if (!str) goto exit;
    *haystack = str;
  }

exit:
  return str;
}
