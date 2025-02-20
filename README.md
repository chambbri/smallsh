# About this project
Smallsh is a shell designed to function as a simple command line interface similar to Bash.

Smallsh does the following:

* Prints an interactive input prompt
* Parses command line input into semantic tokens
* Implements parameter expansion
  - Handles shell special parameters '$$', '$?', and '$!'
  - Tilde (~) expansion
* Implements two shell built-in commands: exit and cd
* Executes non-built-in commands using the the appropriate EXEC(3) function.
* Implements redirection operators ‘<’ and ‘>’
* Implements the ‘&’ operator to run commands in the background
* Implements custom behavior for SIGINT and SIGTSTP signals

