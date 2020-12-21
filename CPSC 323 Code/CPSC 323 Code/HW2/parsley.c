// HW2 - Parsley. Ben Foxman, netid btf28
// An implementation of a subset of the bash lexer/parser for c

#define _GNU_SOURCE
// exit on error, print a message if this was the first fatal error
#define ERROR(message)  {\
         if (!fatal){\
        fprintf(stderr, "parsley: %s\n", message);\
        }\
    fatal = true;\
    freeList(tokens);\
    return NULL;\
}

//for making locally
//#include "parsley.h"
//#include "mainParsley.c"

//for making on zoo
#include "/c/cs323/Hwk2/parsley.h"
#include "/c/cs323/Hwk2/mainParsley.c"

#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// linked list
typedef struct node{
    struct node *next;
    char *token;
    int type;
}Node;

void printList(Node *n); // print a list (debugging)
void freeList(Node *n);  // free a list
Node *chopList(Node *n, Node *token); //chop a list (returns the latter half)

char *token_value; // a buffer which will hold the characters of the current token
bool tokenize(char *line, Node *tokens); // tokenize the string, store in tokens
int consumeToken(char *line); // get next token (for tokenizing)

//methods which match a particular [element] in the phase structure grammar, and construct tree
CMD *command(Node *tokens);
CMD *sequence(Node *tokens);
CMD *andOr(Node *tokens);
CMD *pipeline(Node *token);
CMD *stage(Node *tokens);
CMD *simple(Node *tokens);
CMD *subcmd(Node *tokens);

bool redirect(Node *input); // check if a command is a redirection operator
int local(Node *input); // check if token is a local variable
char *heredoc(char *name); // process heredoc contents, store in name

bool fatal = false; //check for fatal errors in parsing
bool inputUsed = false; //for i/o
bool outputUsed = false; //for i/o

CMD *parse (char *line){
    token_value = malloc(strlen(line) + 1); // upper bound on the size of a token

    // initialize list to hold token information
    Node *tokens = malloc(sizeof(Node));
    tokens -> type = -1;
    tokens -> next = NULL;
    tokens -> token = NULL;


    tokenize(line, tokens); // false if error in tokenization
    Node *trash = tokens;
    tokens = tokens -> next; // knock off empty first node
    free(trash); // dump first node
    if (!tokens){ // no commands, reset
        fatal = false;
        inputUsed = false;
        outputUsed = false;
        free(token_value);
        free(tokens);
        return NULL;
    }
    CMD *tree = command(tokens); // initiate chain to build the tree
    // reset these
    inputUsed = false;
    outputUsed = false;
    free(token_value);

    if (!fatal){ // no errors encountered, return the tree
        fatal = false;
        return tree;
    }

    fatal = false;
    freeCMD(tree); // error encountered, we must free
    return NULL;
}

bool tokenize(char *line, Node *tokens){
    Node *a = tokens;
    while (strlen(line) > 0){ // while line isnt over
        int tokenType = consumeToken(line);
        if (tokenType == 18){
            fprintf(stderr, "Tokenization error: invalid token found.\n");
            return false;
        }
        if (tokenType == 17){ // no more tokens
            return true;
        }
        Node *newToken = malloc(sizeof(Node)); // the new token
        newToken -> type = tokenType;
        newToken -> token = strdup(token_value);
        newToken -> next = NULL;
        a -> next = newToken;
        a = a -> next;
    }
    return true;
}

CMD *command(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    if (!tokens || (tokens -> type >= 8 && tokens -> type <= 12)){ //can't start with a symbolic char
        ERROR("null command")
    }

    Node *n = tokens;
    Node *p;
    while(n -> next){ // go to last token, check if it's a ; or &
        p = n;
        n = n -> next;
    }
    if (n -> type == SEP_END){
        p -> next = NULL;
        freeList(n);
        return mallocCMD(SEP_END, sequence(tokens), NULL);
    }
    else if (n -> type == SEP_BG){
        p -> next = NULL;
        freeList(n);
        return mallocCMD(SEP_BG, sequence(tokens), NULL);
    }

    return sequence(tokens);
}

CMD *sequence(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    if (tokens -> type == SEP_END || tokens -> type == SEP_BG){
        ERROR("null sequence");
    }
    Node *n = tokens;
    Node *cut = NULL;
    int inSub = 0;
    while(n){ //decide if cut should be made
        if (inSub == 0 && (n -> type == SEP_END || n -> type == SEP_BG)){
            cut = n;
        }
        else if (n -> type == PAR_LEFT){
            inSub++;
        }
        else if (inSub > 0 && n -> type == PAR_RIGHT){
            inSub--;
        }
        n = n -> next;
    }
    if (!cut){
        return andOr(tokens);
    }
    if (!cut -> next){ // ERROR: nothing one right side of cut
        ERROR("null sequence")
    }
    if (cut -> type == SEP_END){
        Node *ao = chopList(tokens, cut);
        CMD *left = sequence(tokens);
        CMD *right = andOr(ao);
        return mallocCMD(SEP_END, left, right);
    }
    else {
        Node *ao = chopList(tokens, cut);
        CMD *left = sequence(tokens);
        CMD *right = andOr(ao);
        return mallocCMD(SEP_BG, left, right);
    }
}

CMD *andOr(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    // only pipelines, stages care about i/o redirection
    inputUsed = false;
    outputUsed = false;

    if (tokens -> type == SEP_AND || tokens -> type == SEP_OR){
        ERROR("null and-or")
    }
    Node *n = tokens;
    Node *cut = NULL;
    int inSub = 0;
    while(n){ //decide if cut should be made
        if (inSub == 0 && (n -> type == SEP_AND || n -> type == SEP_OR)){
            cut = n;
        }
        else if (n -> type == PAR_LEFT){
            inSub++;
        }
        else if (inSub > 0 && n -> type == PAR_RIGHT){
            inSub--;
        }
        n = n -> next;
    }
    if (!cut){
        return pipeline(tokens);
    }
    if (!cut -> next){ // ERROR: nothing one right side of cut
        ERROR("null and-or")
    }
    if (cut -> type == SEP_AND){
        Node *pl = chopList(tokens, cut);
        CMD *left = andOr(tokens);
        CMD *right = pipeline(pl);
        return mallocCMD(SEP_AND, left, right);
    }
    else {
        Node *pl = chopList(tokens, cut);
        CMD *left = andOr(tokens);
        CMD *right = pipeline(pl);
        return mallocCMD(SEP_OR, left, right);
    }
}

CMD *pipeline(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    if (tokens -> type == PIPE){
        ERROR("null pipeline")
    }
    Node *n = tokens;
    Node *cut = NULL;
    int inSub = 0;
    while(n){ //decide if cut should be made
        if (inSub == 0 && n -> type == PIPE){
            cut = n;
        }
        else if (n -> type == PAR_LEFT){
            inSub++;
        }
        else if (inSub > 0 && n -> type == PAR_RIGHT){
            inSub--;
        }
        n = n -> next;
    }
    if (!cut){
        return stage(tokens);
    }
    if (!cut -> next){ // ERROR: nothing one right side of cut
        ERROR("null pipeline")
    }
    Node *s = chopList(tokens, cut);

    //turn inputUsed/outputUsed on/off to follow semantic convention
    inputUsed = true;
    CMD *right = stage(s);
    inputUsed = false;
    outputUsed = true;
    CMD *left = pipeline(tokens);
    outputUsed = false;
    return mallocCMD(PIPE, left, right);
}

CMD *stage(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    Node *n = tokens;
    while (n){ //[subcmd] has paren, [simple] does not
        if (n -> type == PAR_LEFT || n -> type == PAR_RIGHT){
            return subcmd(tokens);
        }
        n = n -> next;
    }
    return simple(tokens);
}

CMD *simple(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    if (!tokens){
        ERROR("null simple: this is a real problem")
    }
    Node *start = tokens;
    while(!start -> token){ // skip to first token with entry (sometimes a null will start)
        start = start -> next;
    }
    //for now, simple is just one token
    CMD *new = mallocCMD(SIMPLE, NULL, NULL);
    bool textIsLocal = true;

    while (start){
         int varLength = local(start);
         if (textIsLocal && varLength != -1){ // local variable
             new -> nLocal++;
             if (new -> nLocal == 1){
                 new -> locVal = malloc (sizeof(char *));
                 new -> locVar = malloc (sizeof(char *));
                 new -> locVal[0] = NULL;
                 new -> locVar[0] = NULL;
             }
             else {
                 new -> locVal = realloc(new -> locVal, sizeof(char *) * new -> nLocal);
                 new -> locVar = realloc(new -> locVar, sizeof(char *) * new -> nLocal);
             }
             new -> locVal[new -> nLocal - 1] = malloc(varLength + 1);
             new -> locVar[new -> nLocal - 1] = malloc(strlen(start -> token) - varLength);
             strncpy(new -> locVar[new -> nLocal - 1], start -> token, varLength);
             new -> locVar[new -> nLocal - 1][strlen(start -> token) - varLength - 1] = '\0';
             strcpy(new -> locVal[new -> nLocal - 1], start -> token + varLength + 1);
         }
         else if (redirect(start)){ // [redirect] element

             if (start -> type == RED_IN || start -> type == RED_IN_HERE) {
                 if (!inputUsed){
                     if (start->type == RED_IN) {
                         new->fromType = RED_IN;
                         new->fromFile = strdup(start->next->token);
                     }
                     else if (start->type == RED_IN_HERE) {
                         new->fromType = RED_IN_HERE;
                         new->fromFile = heredoc(start->next->token);
                     }
                 }
                 else { // error
                     freeCMD(new);
                     ERROR("2+ input redirects")
                 }
                 inputUsed = true;
             }
             else if (start -> type == RED_OUT || start -> type == RED_OUT_APP){
                 if (!outputUsed){
                     if (start -> type == RED_OUT){
                         new -> toType = RED_OUT;
                         new->toFile = strdup(start->next->token);
                     }
                     else if (start -> type == RED_OUT_APP){
                         new -> toType = RED_OUT_APP;
                         new->toFile = strdup(start->next->token);
                     }
                 }
                 else { // error
                     freeCMD(new);
                     ERROR("2+ output redirects")
                 }
                 outputUsed = true;
             }

             start = start -> next; //redirect is 2 tokens
         }
         else if (start -> type == TEXT) { // TEXT token
             textIsLocal = false;
             new -> argc++;
             new -> argv = realloc(new -> argv, sizeof(char *) * (new -> argc + 1));
             new -> argv[new -> argc - 1] = malloc(strlen(start -> token) + 1);
             strcpy(new ->argv[new -> argc - 1], start -> token);
             new -> argv[new -> argc] = NULL;
         }
         else {
             freeCMD(new);
             ERROR("simple has unexpected token")
         }
        start = start -> next;
    }
    if (textIsLocal){ //no TEXT tokens found - this constitutes an error
        freeCMD(new);
        ERROR("simple missing text token")
    }

    freeList(tokens);
    return new;
}

CMD *subcmd(Node *tokens){
    if (fatal){
        ERROR("safety")
    }
    Node *start = tokens;

    CMD *new = mallocCMD(SUBCMD, NULL, NULL);
    while (start) {
        int varLength = local(start);
        if (varLength != -1) { // local variable
            new->nLocal++;
            if (new->nLocal == 1) {
                new->locVal = malloc(sizeof(char *));
                new->locVar = malloc(sizeof(char *));
                new->locVal[0] = NULL;
                new->locVar[0] = NULL;
            } else {
                new->locVal = realloc(new->locVal, sizeof(char *) * new->nLocal);
                new->locVar = realloc(new->locVar, sizeof(char *) * new->nLocal);
            }
            new->locVal[new->nLocal - 1] = malloc(varLength + 1);
            new->locVar[new->nLocal - 1] = malloc(strlen(start->token) - varLength);
            strncpy(new->locVar[new->nLocal - 1], start->token, varLength);
            new->locVar[new->nLocal - 1][strlen(start->token) - varLength - 1] = '\0';
            strcpy(new->locVal[new->nLocal - 1], start->token + varLength + 1);

        } // locals can only come before
        else if (redirect(start)) { // [redirect] element
            if (start->type == RED_IN || start->type == RED_IN_HERE) {
                if (!inputUsed){
                    if (start->type == RED_IN) {
                        new->fromType = RED_IN;
                        new->fromFile = strdup(start->next->token);
                    }
                    else if (start->type == RED_IN_HERE) {
                        new->fromType = RED_IN_HERE;
                        new->fromFile = heredoc(start->next->token);
                    }
                }
                else { // error
                    freeCMD(new);
                    ERROR("2+ input redirects")
                }
                inputUsed = true;
            }
            else if (start->type == RED_OUT || start->type == RED_OUT_APP) {
                if (!outputUsed){
                    if (start -> type == RED_OUT){
                        new -> toType = RED_OUT;
                        new->toFile = strdup(start->next->token);
                    }
                    else if (start -> type == RED_OUT_APP){
                        new -> toType = RED_OUT_APP;
                        new->toFile = strdup(start->next->token);
                    }
                }
                else { // error
                    freeCMD(new);
                    ERROR("2+ output redirects")
                }
                outputUsed = true;
            }
            start = start -> next;; //redirect is 2 tokens
        }
        else {
            break;
        }
        start = start -> next;
    }
    //onto the command
    if (start -> type != PAR_LEFT){
        freeCMD(new);
        ERROR("subcmd missing left parenthesis/illegal start")
    }

    Node *arg = start -> next; //marks the start pos of the command
    Node *post = start -> next; // marks the end of the [command]
    Node *cut = NULL;
    while (post) { //skip to after command
        if (post -> type == PAR_RIGHT){
            cut = post;
        }
        post = post -> next;
    }
    if (!cut || cut == arg) {
        freeCMD(new);
        ERROR("subcmd empty/no right parenthesis")
    }
    // post is now position of ")"
    post = cut;
    post = post -> next;
    while (post) {
        if (redirect(post)) { // [redirect] element
            if (post->type == RED_IN || post->type == RED_IN_HERE) {
                if (!inputUsed){
                    if (post->type == RED_IN) {
                        new->fromType = RED_IN;
                        new->fromFile = strdup(post->next->token);
                    }
                    else if (post->type == RED_IN_HERE) {
                        new->fromType = RED_IN_HERE;
                        new->fromFile = heredoc(post->next->token);
                    }
                }
                else { // error
                    freeCMD(new);
                    ERROR("2+ input redirects")
                }
                inputUsed = true;
            }
            else if (post->type == RED_OUT || post->type == RED_OUT_APP) {
                if (!outputUsed){
                    if (post -> type == RED_OUT){
                        new -> toType = RED_OUT;
                        new->toFile = strdup(post->next->token);
                    }
                    else if (post -> type == RED_OUT_APP){
                        new -> toType = RED_OUT_APP;
                        new->toFile = strdup(post->next->token);
                    }
                }
                else { // error
                    freeCMD(new);
                    ERROR("2+ output redirects")
                }
                outputUsed = true;
            }
            post = post -> next; //redirect is 2 tokens
        }
        else {
            freeCMD(new);
            ERROR("subcmd ending is not empty, or a sequence of redirects")
        }
        post = post -> next;
    }

    //build a new linked list of commands
    Node *newCommand = malloc(sizeof(Node));
    newCommand -> type = -1;
    newCommand -> next = NULL;
    newCommand -> token = NULL;
    Node *p = newCommand;
    while (arg != cut){
        Node *t = malloc(sizeof(Node));
        t -> type = arg -> type;
        t-> token = strdup(arg -> token);
        t -> next = NULL;
        p -> next = t;
        p = p -> next;
        arg = arg -> next;
    }
    Node *trash = newCommand;
    newCommand = newCommand -> next;
    free(trash);
    CMD *left = command(newCommand);

    new -> left = left;
    freeList(tokens);
    return new;
}

Node *chopList(Node *n, Node *tar){ // return latter half of list
    Node *t = n;
    Node *p = NULL;

    while (t != tar){ // set p to be previous
        p = t;
        t = t -> next;
    }
    Node *match = tar;
    tar = tar -> next;
    if (p){
        p -> next = NULL; //make a cut
    }
    if (match -> token){
        free (match -> token);
    }
    free(match);
    return tar;
}

//check if a TEXT token is in the form A=B (assumes escape chars accounted for)
//if true, return the length of variable name
int local(Node *input){
    if (!input || input -> type != TEXT){
        return -1;
    }
    int length = strlen(input -> token);
    int index = 0;
    while (index < length){
        if (index == 0 && isdigit(input -> token[index])){ // can't start with a digit
            return -1;
        }
        if (input -> token[index] == '=' && index > 0){ // equals found
            return index;
        }
        if (!strchr(VARCHR, input -> token[index])){ // character not a valid for a variable name
            return -1;
        }
        index++;
    }
    return -1;
}

// [redirect] found, a [red-op] followed by a TEXT
bool redirect(Node *input) {
    return input && input -> next && RED_OP(input -> type) && input -> next -> type == TEXT;
}

//given a string, return the next token type. store the chars of the token in token_value
int consumeToken(char *line){
    strcpy(token_value, ""); // clear the token value
    size_t length = strlen(line);
    size_t index = 0;
    bool current_escaped = false; // check if the current index has been escaped
    while (index < length) { // examine the current character, see if it is the start of a token

        if (!current_escaped){ // things which only have meaning when they are not escaped

            if (line[index] == '\\'){ //escape character found, increment index again
                current_escaped = true;
                index++;
                continue;
            }

            if (line[index] == ' ') { // ignore whitespace
                index++;
                continue;
            }

            if (line[index] == '#'){ // beginning of a text token is # ->  nothing was found
                return NONE;
            }

            if (strchr(METACHAR, line[index])) { // metacharacter found and not escaped
                if (line[index] == '<') { // some input redirection
                    if (index != length - 1 && line[index + 1] == '<'){
                        strcpy(token_value, "<<");
                        memmove(line, line + index + 2, strlen(line));
                        return RED_IN_HERE;
                    }
                    strcpy(token_value, "<");
                    memmove(line, line + index + 1, strlen(line));
                    return RED_IN;
                }
                else if (line[index] == '>') { // some output redirection
                    if (index != length - 1 && line[index + 1] == '>'){
                        strcpy(token_value, ">>");
                        memmove(line, line + index + 2, strlen(line));
                        return RED_OUT_APP;
                    }
                    strcpy(token_value, ">");
                    memmove(line, line + index + 1, strlen(line));
                    return RED_OUT;
                }
                else if (line[index] == '|') { // pipe OR sep_or
                    if (index != length - 1 && line[index + 1] == '|'){
                        strcpy(token_value, "||");
                        memmove(line, line + index + 2, strlen(line));
                        return SEP_OR;
                    }
                    strcpy(token_value, "|");
                    memmove(line, line + index + 1, strlen(line));
                    return PIPE;
                }
                else if (line[index] == '&') { // sep_and OR sep_bg
                    if (index != length - 1 && line[index + 1] == '&'){
                        strcpy(token_value, "&&");
                        memmove(line, line + index + 2, strlen(line));
                        return SEP_AND;
                    }
                    strcpy(token_value, "&");
                    memmove(line, line + index + 1, strlen(line));
                    return SEP_BG;
                }
                else if (line[index] == ';') { // sep_end
                    strcpy(token_value, ";");
                    memmove(line, line + index + 1, strlen(line));
                    return SEP_END;
                }
                else if (line[index] == '(') { // par_left
                    strcpy(token_value, "(");
                    memmove(line, line + index + 1, strlen(line));
                    return PAR_LEFT;
                }
                else if (line[index] == ')') { // par_right
                    strcpy(token_value, ")");
                    memmove(line, line + index + 1, strlen(line));
                    return PAR_RIGHT;
                }
            }
        }

        if (isgraph(line[index])){ // sequence of printing characters
            int temp = index + 1;
            int offset = 0; // to handle not adding escape characters
            bool text_escape = false; //...unless they're escaped
            token_value[0] = line[index];
            // conditions: string not over, printing character (non metachar) or escaped whitespace/metachar
            while (temp < length && ((isgraph(line[temp]) && !strchr(METACHAR, line[temp])) \
            || ((line[temp] == ' ' || strchr(METACHAR, line[temp]) ) && text_escape) )) {
                if (line[temp] != '\\' || text_escape){ // escaped char or not an escape char, print it to buffer
                    token_value[temp - index - offset] = line[temp];
                }
                else { // escape char, move one forward
                    text_escape = true;
                    offset++;
                    temp++;
                    continue;
                }
                temp++;
                text_escape = false;
            }
            token_value[temp - index - offset] = '\0'; // null terminate
            memmove(line, line + temp, strlen(line));
            return TEXT;
        }
        index++;
        current_escaped = false;
    }
    memmove(line, line + strlen(line), strlen(line));
    return NONE; // no token found
}

void printList(Node *n){
    Node *c = n;
    int count = 0;
    while (c){
        count ++;
        if(c -> token){
            printf(" %s: %d, ", c -> token, count);
        }
        c = c -> next;
    }
    printf("\n");
}

void freeList(Node *n){
    Node *curr = n;
    Node *next;
    while (curr){
        if (curr -> token){
            free(curr -> token);
        }
        next = curr -> next;
        free(curr);
        curr = next;
    }
}

char *heredoc(char *name){ //return the heredoc string
    char *line = NULL;
    char *out = malloc(1);
    strcpy(out, "");
    ssize_t length;
    size_t bufferSize = 0;
    while((length = getline(&line, &bufferSize, stdin))){
        if (!strncmp(line, name, strlen(line) - 1)){
            break;
        }
        char *newOut = strdup(out);
        free(out);
        out = malloc(strlen(newOut) + length + 1);

        strcpy(out, newOut);
        strcat(out, line);
        free(newOut);
        bufferSize = 0;
        free(line);
    }
    free(line);
    return out;
}