#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

#define ARRAY_SIZE 10 // Number of elements in the shared array used by the requester and resolver threads to communicate
#define MAX_INPUT_FILES 100 // Maximum number of hostname file arguments allowed
#define MAX_REQUESTER_THREADS 10 // Maximum number of concurrent requester threads allowed
#define MAX_RESOLVER_THREADS 10 // Maximum number of concurrent resolver threads allowed
#define MAX_NAME_LENGTH 255 // Maximum size of a hostname including the null terminator
#define MAX_IP_LENGTH INET6_ADDRSTRLEN // INET6_ADDRSTRLEN is the maximum size IP address string util.c will return

#ifndef MULTI_LOOKUP_H
#define MULTI_LOOKUP_H

typedef struct host_stack{
    int maxElements; 
    int topElement;
    char* hostnames[MAX_NAME_LENGTH];
}host_stack_t;

struct stack* newHost_stack(){
    struct host_stack *stack = (struct host_stack*)malloc(sizeof(host_stack_t));

    stack -> maxElements = ARRAY_SIZE;
    stack -> topElement = -1;
    //stack -> hostnames = (char*)malloc(sizeof(char) * ARRAY_SIZE);
    for(int i = 0; i < ARRAY_SIZE; i++){
         stack -> hostnames[i] = malloc(MAX_NAME_LENGTH * sizeof(char));
    }
}

int host_stack_size(struct host_stack *stack){
    return stack -> topElement + 1;
}
int host_stack_isEmpty(struct host_stack *stack){
    return stack -> topElement == -1;
}
int host_stack_isFull(struct host_stack *stack){
    return stack -> topElement == stack -> maxElements -1 ;
}
void host_stack_push(struct host_stack *stack, char* hostname_){
    if(host_stack_isFull(stack)){
        //exit(EXIT_FAILURE);
    }else{
        strncpy(stack -> hostnames[++stack->topElement],hostname_,MAX_NAME_LENGTH);
    }
}
char* host_stack_pop(struct host_stack *stack){
    if(host_stack_isEmpty(stack)){
        //exit(EXIT_FAILURE);
        return NULL;
    }
    return stack -> hostnames[stack->topElement--];
}

typedef struct file_node{
    FILE * file;
    char* filename;
    struct file_node* next;
}file_node_t;

void print_file_list(file_node_t* head){
    file_node_t* currNode = head;
    while(currNode != NULL){
        printf("File node: %s -> ", currNode -> filename);
        currNode = currNode -> next;
    }
}

void push_file_list(char* filename_, file_node_t* head, FILE* data){
    if(head != NULL ){
        if(head -> file == NULL){
            head -> file = data;
            head -> filename = filename_;
        }else{
            file_node_t* currNode = head;
            while (currNode -> next != NULL){
                currNode = currNode -> next;
            }
            currNode -> next = (file_node_t*)malloc(sizeof(file_node_t));
            currNode -> next -> file = data;
            currNode -> next -> filename = filename_;
            currNode -> next -> next = NULL;
        }
    }else{
        file_node_t* newNode = (file_node_t*)malloc(sizeof(file_node_t));
        newNode -> file = data;
        newNode -> filename = filename_;
        newNode -> next = NULL;
        head = newNode;
    }
}


FILE* pop_file_list(file_node_t** head){
    FILE* retVal = NULL;
    file_node_t* next_node = NULL;

    if(*head == NULL){
        return NULL;
    }
    next_node = (*head) -> next;
    retVal = (*head) -> file;
    free(*head);
    *head = next_node;

    return retVal;
}
bool is_file_list_empty(file_node_t* head){
    if(head == NULL || head -> file == NULL){
        return true;
    }else{
        return false;
    }
};

void remove_spaces(char* s){
    const char* d = s; 
    do{
        while(*d == ' ' || *d == '\n'){
            ++d;
        }
    }while(*s++ = *d++);
}

#endif MULTI_LOOKUP_H
