// Benjamin Foxman HW1 - netid: btf28
/*
Problem Set 1 - Fiend
In this problem set, I implement a subset of the linux utility find
*/
#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/dir.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fnmatch.h>


// Linked List - FILENAMES/EXPRESSIONS require a flexible size list
typedef struct node{
	char *data;
	struct node *next;
	time_t time; // -only for use by -newer
	time_t nanotime; //only for use by newer
	ino_t inode; // only to check symlink loops
} Node;

// Parsing Stats
typedef enum {
	PARSE_OPTIONS, // 1st state, handle -P and -L flags
	PARSE_FILENAMES, // 2nd state, handle 0+ directory trees
	PARSE_EXPRESSIONS, // 3rd state, handle expression(s)
	PARSE_MAXDEPTH, // 3a - when a number is required for -maxdepth
	PARSE_NEWER, // 3b - when a filename is required for -newer
	PARSE_NAME, // 3c - when a regex is required for -name
	PARSE_EXEC, // 3d - when an exec ending in \; is required for -exec
	PARSE_PRINT, // 3e - when -print seen
	PARSE_DEPTH // 3f - when -depth seen
} parsing_state;

// Linked List Create/Add/Delete Methods
Node *newLinkedList(); // creation

// add nodes
void add_node(Node *node, char *information); 
void add_time_node(Node *node, char *information, time_t time, time_t nanotime); 
void add_inode(Node *node, ino_t inode);

// remove inode
void remove_inode(Node *node, ino_t inode);

bool searchInodeList(Node *node, ino_t target); // search
void deleteLinkedList(Node *node); // deletion


// Parser for the input
void parseInput(int argc, char *argv[], Node *expressions, Node *filenames);

// Directory Traversal
bool traverse_dir(char *filename, long depth, Node *expressions, Node *inodes);

// Evaluate an expression for a given filename
void eval_expressions(char *filename, Node *expressions, time_t filetime, time_t fileNtime);

//helper for string concatentation
char *combine(char *a, char *b);

//helper for string replacement
char *replace(char *expression, int index, char *new);

long maxdepth = LONG_MAX; // -maxdepth global variable
bool preorder_traversal = true; // -depth global variable
bool follow_symbolic_links = false; // -P, -L
bool depth_warning = false; // true if depth/maxdepth appears after a test/action/operator

int main(int argc, char *argv[]){
	Node *expressions = newLinkedList(); // linked list of all expressions
	Node *filenames = newLinkedList(); // linked list of all filenames

	parseInput(argc, argv, expressions, filenames); // parse the input

	if (!filenames -> next){ // no filename, default to -root directory
		add_node(filenames, combine(".", ""));
	}

	Node *curr = filenames -> next; // first node is a NULL head
	Node *inode_numbers; // list which will store inodes for loop detection

	// traverse each file
	while (curr){
		inode_numbers = newLinkedList(); // store the inode numbers for each traversal
		struct stat buf;

		// stat/lstat files according to -P, -L
		if (!follow_symbolic_links){
	    	if (lstat(curr -> data, &buf) < 0){
		    	fprintf (stderr, "lstat(%s) failed\n", curr -> data); 
		    	curr = curr -> next;
		    	deleteLinkedList(inode_numbers);
		    	continue;
			}
			else {
				curr -> time = buf.st_mtim.tv_sec;
				curr -> nanotime = buf.st_mtim.tv_nsec;
			}
	    } 
	    else {
	    	if (stat(curr -> data, &buf) < 0){
		    	// broken symlink, use -P
	    		if (lstat(curr -> data, &buf) < 0){

	    			fprintf (stderr, "stat(%s) failed\n", curr -> data); 
	    			curr = curr -> next;
	    			deleteLinkedList(inode_numbers);
		    		continue;
	    		}
			}
			else {
				curr -> time = buf.st_mtim.tv_sec;
				curr -> nanotime = buf.st_mtim.tv_nsec;
			}
	    } 

	    // root inode has to be add explicitly
	    inode_numbers -> inode = buf.st_ino;

	    // directory traversal + expression evaluation for root
		if (!preorder_traversal && S_ISDIR(buf.st_mode)){
			traverse_dir(curr -> data, 0, expressions, inode_numbers);
		}
		
		eval_expressions(curr -> data, expressions, curr -> time, curr -> nanotime); 

		if (preorder_traversal && S_ISDIR(buf.st_mode)){
			traverse_dir(curr -> data, 0, expressions, inode_numbers);
		}
		
		curr = curr -> next;
		deleteLinkedList(inode_numbers);
	}

	// free memory
	deleteLinkedList(filenames);
	deleteLinkedList(expressions);	
}

void parseInput(int argc, char *argv[], Node *expressions, Node *filenames){
	parsing_state current_state = PARSE_OPTIONS; // initial state
	int current_argument = 1; // first relevant command line argument
	int end_filenames = -1; // argument where filenames end

	while(current_argument < argc){
		char *curr = argv[current_argument]; // keep track of where we are

		// NOTE: current_argument--; accompanies a state change with no net increment
		switch(current_state){
			case PARSE_OPTIONS:
				if (strcmp("-P", curr) == 0){
					follow_symbolic_links = false;
				}
				else if (strcmp("-L", curr) == 0){
					follow_symbolic_links = true;
				}
				else if (strlen(curr) >= 2 && curr[0] == '-'){
					end_filenames = current_argument;
					current_state = PARSE_EXPRESSIONS;
					current_argument--; 
				}
				else {
					current_state = PARSE_FILENAMES;
					current_argument--; 
				}
				break;
			case PARSE_FILENAMES:
				// beginning of expression
				if (strlen(curr) >= 2 && curr[0] == '-'){
					current_state = PARSE_EXPRESSIONS;
					end_filenames = current_argument;
					current_argument--; 
				}
				else {
					char *curr_file = malloc(strlen(curr) + 1);
					strcpy(curr_file, curr);
					add_node(filenames, curr_file);
				}
				break;
			case PARSE_EXPRESSIONS:;
				// NOTE - commands which can be the final argument (no parameter): -print, -depth
				bool final_argument = false;
				if (current_argument == argc - 1){
					final_argument = true;
				} 

				if (strcmp("-maxdepth", curr) == 0){
					if(depth_warning){
						fprintf(stderr, "Warning: Maxdepth follows non-option\n");
					}

					if (final_argument){
						fprintf(stderr, "-maxdepth missing argument\n");
						exit(1);
					}
					current_state = PARSE_MAXDEPTH;
				}
				else if (strcmp("-depth", curr) == 0){
					if(depth_warning){
						fprintf(stderr, "Warning: Depth follows non-option\n");
					}
					current_state = PARSE_DEPTH;
					current_argument--; //depth takes no arguments
				}
				else if (strcmp("-name", curr) == 0){
					if (final_argument){
						fprintf(stderr, "-name missing argument\n");
						exit(1);
					}
					depth_warning = true; // depth no longer at top
					current_state = PARSE_NAME;
				}
				else if (strcmp("-newer", curr) == 0){
					if (final_argument){
						fprintf(stderr, "-newer missing argument\n");
						exit(1);
					}
					depth_warning = true; // depth no longer at top
					current_state = PARSE_NEWER;
				}
				else if (strcmp("-exec", curr) == 0){
					if (final_argument){
						fprintf(stderr, "-exec missing argument\n");
						exit(1);
					}
					depth_warning = true; // depth no longer at top
					current_state = PARSE_EXEC;
				}
				else if (strcmp("-print", curr) == 0){
					depth_warning = true; // depth no longer at top
					current_state = PARSE_PRINT;
					current_argument--; //-print takes no arguments
				}
				else if (strcmp("-o", curr) == 0){
					if(end_filenames == current_argument){
						fprintf(stderr, "-o missing left operand\n");
						exit(1);
					}
					else if (final_argument){
						fprintf(stderr, "-o missing right operand\n");
						exit(1);
					}
					else if(strcmp(argv[current_argument + 1], "-a") == 0 || strcmp(argv[current_argument + 1], "-o") == 0){
						fprintf(stderr, "-o missing right operand\n");
						exit(1);
					}
					else {
						add_node(expressions, combine("OR", ""));
					}
					depth_warning = true; // depth no longer at top
				}
				else if (strcmp("-a", curr) == 0){
					if(end_filenames == current_argument){
						fprintf(stderr, "-a missing left operand\n");
						exit(1);
					}
					else if (final_argument){
						fprintf(stderr, "-a missing right operand\n");
						exit(1);
					}
					else if(strcmp(argv[current_argument + 1], "-a") == 0 || strcmp(argv[current_argument + 1], "-o") == 0){
						fprintf(stderr, "-a missing right operand\n");
						exit(1);
					}
					depth_warning = true; // depth no longer at top
				}
				else {
					fprintf(stderr, "Invalid expression/option: %s\n", curr);
					exit(1);
				}
				break;
			case PARSE_MAXDEPTH:
				// n must be a sequence of digits
				for (int i = 0; i < strlen(curr); i++){
					if (!isdigit(curr[i])){
						fprintf(stderr, "Invalid maxdepth: %s\n", curr);
						exit(1);
					}
				}
				maxdepth = strtoul(curr, NULL, 10);
				if (errno == ERANGE){
					fprintf(stderr, "Maxdepth argument out of range: %s\n", curr);
					exit(1);
				}
				add_node(expressions, combine("MAXDEPTH: ALWAYS TRUE", ""));
				current_state = PARSE_EXPRESSIONS;
				break;
			case PARSE_NEWER: ;
				struct stat buf; 

				// stat/lstat to get time
				if (!follow_symbolic_links){
					if (lstat(curr, &buf) < 0){
						fprintf(stderr, "lstat(%s) failed\n", curr);
						exit(1);
					}
				}
				else {
					if (stat(curr, &buf) < 0){
						fprintf(stderr, "stat(%s) failed\n", curr);
						exit(1);
					}
				}
				add_time_node(expressions, combine("NE ", curr), buf.st_mtim.tv_sec, buf.st_mtim.tv_nsec);
				current_state = PARSE_EXPRESSIONS;
				break;
			case PARSE_NAME: 
				add_node(expressions, combine("NA ", curr));
				current_state = PARSE_EXPRESSIONS;
				break;
			case PARSE_EXEC:
				if (strcmp(curr, ";") == 0){
					fprintf(stderr, "No command follows -exec\n");
					exit(1);
				}
				char *command = combine("EX", "");
				char *temp;

				//exec must be delimited by a ;
				while (strcmp(curr, ";")){
					temp = combine(command, " ");
					free(command);
					command = combine(temp, curr);
					free(temp);
					current_argument++;
					if (current_argument == argc){
						fprintf(stderr, "No ; following command\n");
						exit(1);
					}
					curr = argv[current_argument];
				}
				add_node(expressions, command);
				current_state = PARSE_EXPRESSIONS;
				break;
			case PARSE_PRINT:
				add_node(expressions, combine("PRINT: ALWAYS TRUE", ""));
				current_state = PARSE_EXPRESSIONS;
				break;
			case PARSE_DEPTH:
				add_node(expressions, combine("DEPTH: ALWAYS TRUE", ""));
				preorder_traversal = false;
				current_state = PARSE_EXPRESSIONS;
				break;
		}
		current_argument++;
	}
}

Node *newLinkedList(){
    Node *new = malloc(sizeof(Node));
    new -> data = NULL;
    new -> next = NULL;
    return new;
}

void add_node(Node *node, char *information){
	Node *curr = node;
	while (curr -> next){
		curr = curr -> next;
	}
	Node *new = malloc(sizeof(Node));
	new -> data = information;
	new -> next = NULL;
	curr -> next = new;
}

void add_time_node(Node *node, char *information, time_t time, time_t nanotime){
	Node *curr = node;
	while (curr -> next){
		curr = curr -> next;
	}
	Node *new = malloc(sizeof(Node));
	new -> data = information;
	new -> next = NULL;
	new -> time = time;
	new -> nanotime = nanotime;
	curr -> next = new;
}

void add_inode(Node *node, ino_t inode){
	Node *curr = node;
	while (curr -> next){
		curr = curr -> next;
	}
	Node *new = malloc(sizeof(Node));
	new -> inode = inode;
	new -> next = NULL;
	new -> data = NULL;
	curr -> next = new;
}

// precondition, inode exists in list
void remove_inode(Node *node, ino_t inode){
	Node *curr = node;
	while (curr -> next){
		if (curr -> next -> inode == inode){
			break;
		}
		curr = curr -> next;
	}
	if (curr -> next){
		Node *new = curr -> next -> next;
		if (curr -> next -> data){
			free(curr -> next -> data);
		}
		free(curr -> next);
		curr -> next = new;
	}
}

bool searchInodeList(Node *node, ino_t target){
	Node *curr = node;
	while (curr){
		if (curr -> inode == target){
			return true;
		}
		curr = curr -> next;
	}
	return false;
}

void deleteLinkedList(Node *node){
	Node *curr = node;
	Node *next;
	while (curr){
		next = curr -> next;
		if (curr -> data){
			free(curr -> data);
		}
		free(curr);
		curr = next;
	}
}

char *combine(char *a, char *b){
	char *c = malloc(strlen(a) + strlen(b) + 1);
	strcpy(c, a);
	strcat(c, b);
	return c;
}

char *replace(char *expression, int index, char *new){
	char *ans = malloc(strlen(expression) + strlen(new) - 1);
	strncpy(ans, expression, index);
	ans[index] = '\0';
	strcat(ans, new);
	strcat(ans, expression + index + 2); //for {}
	return ans;
}

bool traverse_dir(char *filename, long depth, Node *expressions, Node *inode_numbers){
	if (depth == maxdepth){
		return true;
	}

    DIR *dp = opendir(filename);
    if (dp == NULL){
    	fprintf(stderr, "Couldn't traverse directory: %s\n", filename);
    	return false;
    }    
    
    struct dirent *entry;
    struct stat buf;

    lstat(filename, &buf);

    // don't explore symlinks if -P
    if (S_ISLNK(buf.st_mode) && !follow_symbolic_links){
    	closedir (dp);
    	return false;
    }
    ino_t inode = buf.st_ino;
    add_inode(inode_numbers, inode); // add the new inode

    time_t filetime = -1; // file time in seconds
    time_t fileNtime = -1; // file time in nanoseconds

    // read the directory
    while ((entry = readdir (dp))) {

    	// construct absolute filename from relative + local filename
    	char *newRoot1 = strdup(filename);
		char *newRoot2;

		if (filename[strlen(filename) - 1] != '/'){
    		newRoot2 = combine(newRoot1, "/");
    	}
    	else {
    		newRoot2 = combine(newRoot1, "");
    	}
        
        char *abs_path = combine(newRoot2, entry -> d_name);
        free(newRoot1);
        free(newRoot2);

        // stat/lstat files according to -P, -L
	    if (!follow_symbolic_links){
	    	if (lstat(abs_path, &buf) < 0){
		    	fprintf (stderr, "lstat(%s) failed\n", abs_path);
				
			}
			else {
				filetime = buf.st_mtim.tv_sec;
				fileNtime = buf.st_mtim.tv_nsec;
			}
	    } 
	    else {
	    	if (stat(abs_path, &buf) < 0){ 
	    		// broken symlink - use -P
	    		if (lstat(abs_path, &buf) < 0){
	    			fprintf (stderr, "stat(%s) failed\n", abs_path); 
	    		}
	    		//symlink points to itself
	    		if (errno == ELOOP){
	    			fprintf(stderr, "Too many levels of symlinks: %s\n", abs_path);
	    			free(abs_path);
	    			continue;
	    		}
			}
			else {
				filetime = buf.st_mtim.tv_sec;
				fileNtime = buf.st_mtim.tv_nsec;
			}
	    } 
	 
	    if (strcmp(entry -> d_name, ".") && strcmp(entry -> d_name, "..")){
		    ino_t newNum = buf.st_ino; // inode of current file

		    // check for loops in the symbolic links
		    if (searchInodeList(inode_numbers, newNum)){
		    	fprintf(stderr, "Loop detected: %s\n", abs_path);
		    	free(abs_path);
		    	continue;
		    } 
		    

	    	// preorder traversal 
	    	if (preorder_traversal){
	    		eval_expressions(abs_path, expressions, filetime, fileNtime);
	    	}

	    	if (S_ISDIR(buf.st_mode)){
    			traverse_dir(abs_path, depth + 1, expressions, inode_numbers);
	    	}
	    	
	    	// postorder traversal 
	    	if (!preorder_traversal){
	    		eval_expressions(abs_path, expressions, filetime, fileNtime);
	    	}
	    }
	    free(abs_path);
    }
    remove_inode(inode_numbers, inode); // remove the inode after traversal
    closedir (dp);
    return true;
}

void eval_expressions(char *filename, Node *expressions, time_t filetime, time_t fileNtime){
	Node *curr = expressions -> next;

	bool add_print = true; // print by default
	bool evaluation = true; // expression evaluation
	while (curr){
		if (!strcmp(curr -> data, "PRINT: ALWAYS TRUE")){
			add_print = false;
			printf("%s\n", filename);
		}
		else if (!strcmp(curr -> data, "DEPTH: ALWAYS TRUE") || !strcmp(curr -> data, "MAXDEPTH: ALWAYS TRUE")){
			// do nothing
		}
		else if (curr -> data[0] == 'N' && curr -> data[1] == 'E'){

			// if test fails, skip to next -o or EOE
			if (filetime < curr -> time || (filetime == curr -> time && fileNtime <= curr -> nanotime)){
				while(curr && strcmp(curr -> data, "OR")){
					if (!strcmp(curr -> data, "PRINT: ALWAYS TRUE") || (curr -> data[0] == 'E' && curr -> data[1] == 'X')){
						add_print = false;
					}
					curr = curr -> next;
				}
				if (!curr){
					evaluation = false;
				}			
			}
		}
		else if (curr -> data[0] == 'N' && curr -> data[1] == 'A'){
			// get local name to match
			char *local = strrchr(filename, '/');
			char *toCmp;
			if (!local){
				toCmp = filename;
			}
			else {
				toCmp = local + 1;
			}
			char *regex = curr -> data + 3;

			// if test fails, skip to next -o or EOE
			if (fnmatch(regex, toCmp, 0)){
				while(curr && strcmp(curr -> data, "OR")){
					if (!strcmp(curr -> data, "PRINT: ALWAYS TRUE") || (curr -> data[0] == 'E' && curr -> data[1] == 'X')){
						add_print = false;
					}
					curr = curr -> next;
				}
				if (!curr){
					evaluation = false;
				}			
			}

		}
		else if (curr -> data[0] == 'E' && curr -> data[1] == 'X'){
			add_print = false;

			// {} replacement
			char *modified_expression = strdup(curr -> data);
			char *temp = strstr(modified_expression, "{}");
		    while(temp){
		        int index = temp - modified_expression;
		        char *newest = replace(modified_expression, index, filename);
		        free(modified_expression);
		        modified_expression = strdup(newest);
		        free(newest);
		        temp = strstr(modified_expression + index + strlen(filename), "{}");
		    }

			bool failure = system(modified_expression + 2); // -exec implementation uses system()
			free(modified_expression);

			// if test fails, skip to next -o or EOE
			if (failure){
				while(curr && strcmp(curr -> data, "OR")){
					if (!strcmp(curr -> data, "PRINT: ALWAYS TRUE") || (curr -> data[0] == 'E' && curr -> data[1] == 'X')){
						add_print = false;
					}
					curr = curr -> next;
				}
				if (!curr){
					evaluation = false;
				}			
			}
		}
		// only would ever reach -o explicitly if an entire clause was true, in which case we are done
		else if (!strcmp(curr -> data, "OR")){
			evaluation = true;
			break; 
		}

		if(!curr){
			break;
		}
		curr = curr -> next;
	}

	if (add_print && evaluation){
		printf("%s\n", filename);
	}
}
