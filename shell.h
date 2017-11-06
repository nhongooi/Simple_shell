//shell.h
//shell header
//data structures
//basic shell environment

typedef struct environ{
	char *user;
	char *home;
	char *shell;
	char *path;
	char *cwd;
}environ;

//declaration
// comment on func == worked
void getenvs(void);//
int get_num_builtin(void);
void shell_loop(int argc, char **argv);//
void check_ptr(void* ptr);//
void raise_error(const char* err);//
char *read_line(void);//
char **split_line(char *argv);//
char *cwd(void);//
int execute(char **argv);//
int print_env(char **argv);//
int clr(char **argv);//
int cd(char **argv);//
int quit(char **argv);//
int pwd(char **argv);//
int mypause(char **argv);//
int help(char **argv);//
int dir(char **argv);//
int printenv(char **argv);//
int launch(char **argv);//
int find_sym(char**argv, char *sym);//
void removesubstr(char *s, const char *remove);//
int echo(char **argv);
void piping(char **p1, char **p2);
char **split_argv(char **p1, char *sym);
void execute_file(int argc,char **argv);
void redir_in(char *infile);
void redir_out(char *outfile, int iswipe);
void redir_null(void);