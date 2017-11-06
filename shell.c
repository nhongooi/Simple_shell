#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "shell.h" // shell's header
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFSIZE 256
#define TOK_DELIM " \t\r\n\v\f"
static int builtin_num;
const char* builtin_str[] = {"cd","clr","dir","printenv","echo","help","mypause","quit","pwd"};
static struct environ *shell_e;//needs to be accessable by every func
const int (*builtin_func[])(char**) = {&cd,&clr,&dir,&printenv,&echo,&help,&mypause,&quit,&pwd};

//everything starts here
int main(int argc, char **argv){
	//set up main process
	builtin_num = get_num_builtin();// get dat sweet # of builtin functions

	//load in environ config
	shell_e = malloc(sizeof(struct environ));
	getenvs();//where are my identities!?

	//run shell loop
	shell_loop(argc,argv);

	//exit V
	return (EXIT_SUCCESS);
}
// main shell loop func ------------------------------------------------
void shell_loop(int argc, char **argv){
	char *line; // getting argv from input;
	int status; // exit method
	char **user_argv; // split line into user_argv
	//check for file
	if(argc > 1){
		execute_file(argc, argv);
	}
	else{
		do{//do this loop
			line = read_line(); // getting args into strings
			user_argv = split_line(line); // split line into token into shell_p
			status= execute(user_argv); //passing argv
			line =NULL;// best
		}while(status!=-1);// as long as it is not quit status will be 1
		free(user_argv);
	}
}
// load in file line by line and execute
//will only load standard shell format without any continioning
//no if, then, loop or similar mechianism
void execute_file(int argc,char **argv){
	char *line = malloc(BUFFSIZE*sizeof(char)); // init line
	FILE *file_buffer;//file
	char **file_input;// split argv
	if((file_buffer = fopen(argv[1],"r")) == NULL) raise_error("fopen batch");

	//this does not work with things like more because this doesnt wait
	//for each child to finish.
	// will work with anything that works in the background
	//things that touch the terminal will be brought into the fore ground like
	//more
	//therefore, help doesnt work with this.
	while(fgets(line,BUFFSIZE,file_buffer)){
		file_input = split_line(line); // split line into token into shell_p
		execute(file_input); //passing argv
	}
	//free it
	fclose(file_buffer);
	free(file_input);
}

//execute input through builtin or external exec
int execute(char **argv){
	int i;// for loop

	// empty command loopback
	if(argv[0]==NULL){
		printf("Empty Command\n");
		return 1;
	}
	// search through builtin functions before external exec
	for(i =0;i < builtin_num;i++){
		if(strcmp(argv[0],builtin_str[i]) == 0){//find builtin with strcmp
			return (*builtin_func[i])(argv);//found builtin
		}
	}
	//if not builtin go and find exec
	return launch(argv);
}

//exec external exec
int launch(char **argv){
	pid_t pid;
	int status;
	//init child process into shell process

	//check if background and remove "&"
	int isBG = find_sym(argv, "&");
	if(isBG>0)
		argv[isBG] = NULL;

	//start forking
	if((pid = fork()) == 0){
		//set up child process for the child
		char **buffer_argv;
		int isin;// is redirect in
		int isouta; // is redirect out with  append
		int isoutw; // is redirect out with  wipe
		int ispipe = find_sym(argv,"|");

		//gotta check for background, so they dont touch the terminal with their filthy stdout
		if(isBG > 0){
			redir_null();
		}

		if(ispipe > 0){// the child will never comes back after the pipe
			buffer_argv = split_argv(argv,"|");
			piping(argv, buffer_argv);
		}
		// no need to do this if we pipe
		isin   = find_sym(argv,"<");
		isoutw = find_sym(argv,">");
		isouta = find_sym(argv,">>");
		// consider going from right to left
		// stdout will be dup first
		if(isoutw>0){
			buffer_argv = split_argv(argv, ">");
			redir_out(buffer_argv[0], 1);
		}
		else if(isouta>0){
			buffer_argv = split_argv(argv, ">>");
			redir_out(buffer_argv[0], 0);
		}

		//stdin will be dup second
		if(isin>0){
				buffer_argv = split_argv(argv, "<");
				redir_in(buffer_argv[0]);
		}
		//start exec

		if((execvp(argv[0],argv))==-1) raise_error("exec: child launch");
	}

	//if failed
	else if(pid <0){raise_error("forking");}
	//is parent
	else if(isBG < 0) { // well, i thought "&" means wait, but i was wrong
			    // so, if "&" is not found, wait for child process.
		//find if shell process needs to wait
			waitpid(pid,&status,0);
	}
	return 1;
}

//this only pipe between two processes because i asked
void piping(char **p1,char **p2){
	pid_t pid;
	char **buffer_argv;
	int fd[2]; // two pipes
	//simple means for me to not get confused when piping
	int in = 0;
	int out= 1;
	int status;
	//if check for redirection
	int isin   = find_sym(p1,"<");
	int isoutw = find_sym(p2,">");
	int isouta = find_sym(p2,">>");

	pipe(fd);//pipe it up
	// child will do the first argv
	if((pid =fork())==0){
		if(isin>0){// dup file for child first
				buffer_argv = split_argv(p1, "<");
				redir_in(buffer_argv[0]);
		}
		//dup out abd close in
		dup2(fd[out],out);
		close(fd[in]);
		if((execvp(p1[0],p1))==-1) raise_error("piping: child");
	}
	else{
			//check for redirect out
			if(isoutw>0){
				buffer_argv = split_argv(p2, ">");
				redir_out(buffer_argv[0], 1);
			}
			else if(isouta>0){
				buffer_argv = split_argv(p2, ">>");
				redir_out(buffer_argv[0], 0);
			}
			//dup in and close out
			dup2(fd[in],in);
			close(fd[out]);
			//i dont know if this is true, so i wait anyway
			//i read that the parent will wait for the child to finish
			//then start running because it will wait for stdin
			//this is the safest bet
			waitpid(pid,&status,0);
			if((execvp(p2[0],p2))==-1) raise_error("piping: parent");
		}
}
//main shell loop end---------------------------------------------------

// utility func ---------------------------------------------------
//get args from user
//write in the dependency in for makefile***************************
char *read_line(void){
	// make line buffer
	char *line = (char *)NULL; // cast null char
	char prompt[BUFFSIZE]; //string literal for modification
	strncpy(prompt, shell_e->cwd, BUFFSIZE); // str copy cwd into prompt
	strncat(prompt, " > ", BUFFSIZE);// add >
  	line = readline (prompt);

  	/* If the line has any text in it, save it on the history. */
  	if (line && *line)
  		  add_history(line);
	return line;
}
//split line into token to be use as argv
char **split_line(char *argv){
	int buff_size = BUFFSIZE;
	int pos = 0;
	char **tokens = malloc(buff_size*sizeof(char));
	char *line = argv; // need something to to cut to pieces
	char *rest; // the rest of the line after strtok_r
	check_ptr(tokens);

	// split string into piece with delim specifiy as global on top
	while(tokens[pos] = strtok_r(line,TOK_DELIM,&rest)){
		if(strcmp(tokens[pos],"\0") == 0)
			tokens[pos] = NULL;// we dont want \0 now
			if((pos++) >=buff_size){
			//realloc if input is larger than BUFFSIZE
			buff_size += BUFFSIZE;
			tokens = realloc(tokens, buff_size*sizeof(char));
			check_ptr(tokens);
		}
		line = rest;//reassign the rest of string to line to be cut again
	}
	tokens[pos] = NULL;//gotta do this for other functions and exec
	return tokens;
}
//find a specific char or string in argv
int find_sym(char **argv, char *sym){
	int pos;
	//return pos of symbol to caller
	for(pos =0; argv[pos] != NULL;pos++)
		if(strcmp(argv[pos],sym)== 0)
			return pos;
	return -1;// nothing was found
}

//split the argv into two different argv based on input sym for piping
char **split_argv(char **p1, char *sym){
	int pos = find_sym(p1,sym); //start at position sym
	int pos2 = 0; // position for second string ptr
	size_t len;
	int buff_size = BUFFSIZE;
	char **buffer_vector= malloc(buff_size * sizeof(char));// string literal
	check_ptr(buffer_vector);

	//split time
	while(p1[pos] != NULL){
		//made sure for p1 no longer have sym
		if(strcmp(p1[pos],sym) == 0)
			p1[pos] = NULL;
		else{
			//store char ptr after sym to the buffer
			len = strlen(p1[pos])+1; //find exact len
			buffer_vector[pos2] = strndup(p1[pos], len); //duplicate string at pos to buffer_vector
			p1[pos] = NULL;
			//realloc if too many strings
			if((pos2++) >=buff_size){
				buff_size += BUFFSIZE;
				buffer_vector = realloc(buffer_vector, buff_size*sizeof(char));
				check_ptr(buffer_vector);
			}
		}
		pos++;
	}
	//for exec and other funtions
	buffer_vector[pos2] = NULL;
	return buffer_vector;
}

//checking pointer for null
void check_ptr(void* ptr){
	if(!ptr)
		raise_error("Failed memory allocation");
}
//less work to call error
void raise_error(const char* err){
	fprintf(stderr, "%s\n",err);
	exit(EXIT_FAILURE);
}
// get current working dir
char *cwd(void){
	char *cwd = malloc(BUFFSIZE*sizeof(char));
	check_ptr(cwd);

	if(getcwd(cwd, BUFFSIZE * sizeof(char)) ==NULL) raise_error("cwd");

	return cwd;
}
//get environ through calls and other funcs
void getenvs(void){
	struct passwd *pass = getpwuid(getuid()); //set up pawwrd to get user name
	size_t len = BUFFSIZE;
	char path[BUFFSIZE];
	char *dest = malloc(BUFFSIZE*sizeof(char)); // find this exec location
	memset(dest,0,sizeof(dest)); // set mem to zero
	struct stat stat;
	sprintf(path,"/proc/self/exe");//path finds the exec in running
	//too confusing to condense the code so i leave it leveled
	if(shell_e == NULL)
		raise_error("shell_e malloc");
	if((shell_e->path=getenv("PATH"))== NULL) //get path
		raise_error("path environ");
	shell_e->cwd = cwd();//get cwd
	if(readlink(path,dest,BUFFSIZE) ==-1){//get this shell directory
		raise_error("readlink");
	}
	else{
		shell_e->shell = dest;
	}
	if((shell_e->home=getenv("HOME"))== NULL)// get home
		raise_error("home environ");
	if((shell_e->user=pass->pw_name) ==NULL)//get current username
		raise_error("user environ");
}

// inputs are string literals
//use to modifity string to remove a substring
//only use this once in help to find readme no matter where cwd is
void removesubstr(char *str,const char *remove){
	int remove_len = strlen(remove);
	while((str = strstr(str,remove))){
		memmove(str,str+remove_len,1+strlen(str+remove_len));
	}
}
//return calculated number of builtin func
int get_num_builtin(void){
	return sizeof(builtin_str)/sizeof(*builtin_str);
}
// dup file into stdin
void redir_in(char *infile){
	int in;
	in = open(infile, O_RDONLY);//read only
	dup2(in, 0);
	close(in);
}
// send in the file name and if wipe > 0, wipe if already exist, put filei into stdout fd
void redir_out(char *outfile, int iswipe){
	int out;
	//give user permission to write and group permission
	if(iswipe == 1)
		out = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	else
		out = open(outfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
	dup2(out, 1);
	close(out);
}
void redir_null(void){
	int toNull = open("/dev/null", O_WRONLY);// aparently, you can put fd into a null fd
						//tried closing stdout, didnt work
	dup2(toNull,1); // stdout to null
	dup2(toNull,2); // stderr to null
	close(toNull); // close null fd
}
// utility func end-------------------------------------------------

//builtin func------------------------------------------------------
//clear terminal screen
int clr(char **argv){
	system("clear");// shouldnt use this but not sure what else to do
	return 1;
}

//change directory
int cd(char **argv){
	if(argv[1] == NULL){
		if(chdir(shell_e->home) != 0)//change cwd to home if no user directory
			raise_error("cd: home");
	}
	else{
		if(chdir(argv[1]) !=  0)//change cwd to argv
			printf("directory does not exist\n");
	}
	//change cwd environ here
	shell_e->cwd = cwd();
	return 1;
}
//print out shell envirion
int printenv(char **argv){
	char **buffer_argv;
	pid_t pid;
	// fork to dup and print to file
	if(find_sym(argv,">") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">");
			redir_out(buffer_argv[0], 1);
			printenv(buffer_argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	else if(find_sym(argv, ">>") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">>");
			redir_out(buffer_argv[0], 0);
			printenv(buffer_argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	//print environ
	printf("USER: %s\n",shell_e->user);
	printf("HOME: %s\n",shell_e->home);
	printf("SHELL: %s\n",shell_e->shell);
	printf("PATH: %s\n",shell_e->path);
	printf("CWD: %s\n",shell_e->cwd);
	return 1;
}
//i dont know what you do more than just print this out
int pwd(char **argv){
	printf("Current Dir: %s\n",shell_e->cwd);
	return 1;
}
// quit shell
int quit(char **argv){
	int status;
	waitpid(-1,&status, WNOHANG); // kill all zombie children that have not seppuku
	return -1;//what else can i do
//pause all child pauses
}
//do not allow anymore input by busy waiting
int mypause(char **arv){
	char buffer_char;

	//if(kill(-1, SIGSTOP) == -1) raise_error("pausing");

	printf("Enter to continue");
	while((buffer_char = getchar()) != '\n'){}

	//if(kill(-1,SIGCONT) ==-1) raise_error("continuing");

	return 1;
}

//print help manual using more or just man if not in readme
int help(char **argv){
	pid_t pid;
	int status;
	int pos;
	int max_intput = 10;
	char *start_line;
	char *num_line;
	char **manual_ptr;
	char readme_dir[BUFFSIZE];
	char **buffer_argv;
	//find readme anywhere by replace shell environ with readme
	strncpy(readme_dir, shell_e->cwd, BUFFSIZE);
	removesubstr(readme_dir,"myshell");
	strncat(readme_dir, "/readme", BUFFSIZE);
	// get builtin func position
	if(argv[1] !=NULL){
		for(pos =0;pos < builtin_num;pos++){
			if(strcmp(argv[1],builtin_str[pos]) == 0) break;
		}
	}
	//switch to find which part is builtin function manual part
	switch(pos){
		case 0: start_line ="+38" ;num_line = "-4"; break;//cd
		case 1: start_line ="+41" ;num_line = "-4"; break;//clr
		case 2: start_line ="+44" ;num_line = "-4"; break;//dir
		case 3: start_line ="+47" ;num_line = "-4";break;//printenv
		case 4: start_line ="+50" ;num_line = "-4";break;//echo
		case 5: start_line ="+53" ;num_line = "-4";break;//help
		case 6: start_line ="+56" ;num_line = "-4";break;//mypause
		case 7: start_line ="+59" ;num_line = "-4";break;//quit
		case 8: start_line ="+62" ;num_line = "-4";break;//pwd
	}

	// make argv
	if(argv[1] == NULL){// help without argv
		char *option[] = {"more","-c","-d",readme_dir, NULL};
		manual_ptr = option;
	}
	else if(pos > 8){ // man outside func
		char *option[] = {"man",argv[1],NULL};
		manual_ptr = option;
	}
	else{// get builtin manaual
		char *option[] = {"more","-c","-d",start_line, num_line, readme_dir, NULL};
		manual_ptr = option;
	}

	//if stdout to file
	if((pid = fork()) == 0){
		if(find_sym(argv,">") >0){
			buffer_argv = split_argv(argv, ">");
			redir_out(buffer_argv[0], 1);
		}
		else if(find_sym(argv, ">>") >0){
			buffer_argv = split_argv(argv, ">>");
			redir_out(buffer_argv[0], 0);
		}

		if((execvp(manual_ptr[0],manual_ptr)) == -1) raise_error("more");
	}
	else if(pid < 0){raise_error("help:forking");}
	else{
		waitpid(pid,&status,0);
	}
	return 1;
}

//list all files in cwd
int dir(char **argv){
	char **buffer_argv;
	pid_t pid;
	// if stdout to file
	if(find_sym(argv,">") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">");
			redir_out(buffer_argv[0], 1);
			dir(argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	else if(find_sym(argv, ">>") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">>");
			redir_out(buffer_argv[0], 0);
			dir(argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	//set up struct dirent and init dir
	DIR *dir;
	struct dirent *dirent;
	char * dir_str = argv[1];
	//if dir given argv, dir cwd
	if(dir_str != NULL)
		dir = opendir(dir_str);
	else
		dir = opendir(shell_e->cwd);
	//to identify file header
	printf("File Type\tFile Name\n");
	while((dirent = readdir(dir)) != NULL)// start printing out file list
		printf("%u\t\t%s\n",(unsigned char)dirent->d_type,dirent->d_name);
	closedir(dir);
	return 1;
}
//need to do this for redirection and piping
int echo(char **argv){
	int pos = 1;
	char * str_buffer;
	char **buffer_argv;
	pid_t pid;
	//if stdout to file
	if(find_sym(argv,">") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">");
			redir_out(buffer_argv[0], 1);
			echo(argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	else if(find_sym(argv, ">>") >0){
		if((pid = fork())==0){
			buffer_argv = split_argv(argv, ">>");
			redir_out(buffer_argv[0], 0);
			echo(argv);
			exit(EXIT_SUCCESS);
		}
		else return 1;
	}
	//while not null printto stdout
	while((str_buffer = argv[pos])!= NULL){
		printf("%s ",str_buffer);
		pos++;
	}
	//make sure to print EOF
	printf("\0");
	return 1;
}

//builtin func end--------------------------------------------------
