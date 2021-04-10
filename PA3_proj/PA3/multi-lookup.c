#include "multi-lookup.h"
#include "util.h"
#include <semaphore.h>


static file_node_t* head_file_node;
static host_stack_t* host_stack_;


FILE* resolver_log;
FILE* requester_log;

sem_t wrt;
sem_t mutex; sem_t mutex2;
sem_t finalReleaseswait;
sem_t readBlock;
sem_t temporaryStop_mutex;
pthread_mutex_t conditional_mutex;
pthread_cond_t cond;
pthread_mutex_t conditional_mutex2;
pthread_cond_t cond2;
pthread_mutex_t new_mutex;
int readCount = 0;
bool temporaryStop = false;

int writerswaiting = 0; int readerswaiting = 0;
int writersrunning = 0; int readersrunning = 0;

int itemnumber = 0;
int resolveitemnumber = 0;

void *requester(void *argument) {
    //printf("Requester Thread %d running\n" ,pthread_self());
    FILE *file; 
    int files_serviced = 0;
    bool run = true;
    while(run) {
        file = pop_file_list(&head_file_node);

        if (file != NULL) {
            files_serviced++;
            char buffer[MAX_NAME_LENGTH];
            while(true){
                usleep(1000);
                pthread_mutex_lock(&conditional_mutex2);
                if(host_stack_isFull(host_stack_) ){
                    pthread_mutex_lock(&new_mutex);
                    --writersrunning;
                    ++writerswaiting;
                    if(writersrunning > 0){
                        temporaryStop = true;
                    }
                    pthread_mutex_unlock(&new_mutex);
                    
                    if(buffer == NULL && is_file_list_empty(head_file_node)){//End of file and no more files to grab 
                        pthread_mutex_unlock(&conditional_mutex2);
                        run = false;
                        break;
                    }
                    
                    while(true){//wait until the stack is not full this is signaled by resolver
                        pthread_cond_wait(&cond2,&conditional_mutex2);
                        if(!host_stack_isFull(host_stack_) ){
                            ++writersrunning;
                            --writerswaiting;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&conditional_mutex2);
                }else{
                    pthread_mutex_unlock(&conditional_mutex2);
                    sem_wait(&readBlock);
                    sem_wait(&wrt);
                    
                    pthread_mutex_lock(&new_mutex);
                                    
                    if(temporaryStop == true && host_stack_isFull(host_stack_) ){//if another requester filled stack in between waiting and here
                        sem_post(&wrt);
                        sem_post(&readBlock);
                        pthread_mutex_unlock(&new_mutex);
                        sem_wait(&temporaryStop_mutex);//wait for a requester to signal you 
                    }else{
                        if(!host_stack_isFull(host_stack_)){
                            if(temporaryStop){// if stack not full and temporary stop is true fix this
                                temporaryStop = false;
                            }
                            pthread_mutex_unlock(&new_mutex);
                            if(fgets(buffer, sizeof(buffer), file) != NULL){//if you get something from file 
                                remove_spaces(&buffer);
                                fprintf(requester_log, "%s\n", buffer);
                                host_stack_push(host_stack_, buffer);
                            }else{
                                sem_post(&wrt);
                                sem_post(&readBlock);
                                break;
                            }
                            pthread_mutex_lock(&conditional_mutex);
                            if(!host_stack_isEmpty(host_stack_) && readerswaiting > 0){//if the stack isnt empty signal waiting resolvers
                                pthread_cond_signal(&cond);
                            }
                            pthread_mutex_unlock(&conditional_mutex);
                            sem_post(&wrt);
                            sem_post(&readBlock);
                        }else{
                            pthread_mutex_unlock(&new_mutex);
                            sem_post(&wrt);
                            sem_post(&readBlock);
                        }
                         
                    }
                }
            }
        }else {
            //INVALID FILE
        }
        if(file != NULL){ // when you end your work close the file you openeed 
            fclose(file);
        }else{
            run = false;
        }
    }
    pthread_mutex_lock(&conditional_mutex2);
    --writersrunning;
    if((writersrunning+writerswaiting) == 0){//if im the last requester signal the remaining resolvers
        while(readerswaiting > 0){
            pthread_cond_signal(&cond);
            sem_wait(&finalReleaseswait);
        }
    }
    pthread_mutex_unlock(&conditional_mutex2);
    //Finally it prints out its own id  and number of files serviced
    printf("Requester Thread %d serviced %d files\n",pthread_self(),files_serviced);
    pthread_exit(0);
}

void *resolver(void* argument) {
    //printf("Resolver Thread %d running\n" ,pthread_self());
    bool run = true;
    char hostname_[MAX_NAME_LENGTH]; 
    char IpAddress[MAX_IP_LENGTH];
    int hostnamesServiced = 0;
    
    while(run) {
        usleep(1000);
        sem_wait(&readBlock);
        sem_wait(&mutex);
        readCount++;
        if(readCount == 1){ 
            sem_wait(&wrt);
        }
        sem_post(&mutex);
        sem_post(&readBlock);
        pthread_mutex_lock(&conditional_mutex);
        if(host_stack_isEmpty(host_stack_)){
            sem_wait(&mutex);
            readCount--;
            if(readCount == 0){
               sem_post(&wrt);
            }
            sem_post(&mutex);
            --readersrunning;
            ++readerswaiting;
            if(is_file_list_empty(head_file_node) && host_stack_isEmpty(host_stack_) && (writerswaiting+writersrunning) == 0){
                readerswaiting--;
                pthread_mutex_unlock(&conditional_mutex);
                sem_post(&finalReleaseswait);
                run = false;
                break;
            }else{
                while(true){
                    pthread_cond_wait(&cond,&conditional_mutex);
                    if(!host_stack_isEmpty(host_stack_)){
                        ++readersrunning;
                        --readerswaiting;
                        break;
                    }
                    if(writerswaiting+writersrunning == 0){
                        readerswaiting--;
                        sem_post(&finalReleaseswait);
                        run = false;
                        break;
                    }
                }
                pthread_mutex_unlock(&conditional_mutex);
            }
        }else{
            hostnamesServiced++;
            strncpy(hostname_, host_stack_pop(host_stack_),MAX_NAME_LENGTH);
            fprintf(resolver_log,"%s, ",hostname_);
            if(dnslookup(hostname_,&IpAddress,MAX_NAME_LENGTH) == 0){
                fprintf(resolver_log," %s\n",IpAddress);
            }else{
                fprintf(resolver_log," NOT_RESOLVED\n");
            }
            pthread_mutex_unlock(&conditional_mutex);//this is under the read since there was a rare error when 2 resolvers somehow add at the exact same time when only 1 item is in array
            pthread_mutex_lock(&conditional_mutex2);
            if(!host_stack_isFull(host_stack_) && writerswaiting > 0){
                pthread_mutex_lock(&new_mutex);
                if(temporaryStop){
                    if(writersrunning == 0){
                        temporaryStop = false;
                        pthread_cond_signal(&cond2);
                    }else{
                        temporaryStop = false;
                        sem_post(&temporaryStop_mutex);
                    }
                }else{
                    pthread_cond_signal(&cond2);
                }
                pthread_mutex_unlock(&new_mutex);
            }
            pthread_mutex_unlock(&conditional_mutex2);
            sem_wait(&mutex);
            readCount--;
            if(readCount == 0){
                sem_post(&wrt);
            }
            sem_post(&mutex);
        }
    }
    //Finally it prints out its own id and the number of resolved host names 
    printf("Resolver Thread %d serviced %d hostnames\n",pthread_self(),hostnamesServiced);
    pthread_exit(0);
}

int get_Integer_value(char* input) {
    char* end;
    int output = strtol(input,&end,0);
    if(*end == '\0'){
        return output;
    }else{
        return -1;
    }
}


int main(int argc, char* argv[]) { 
    struct timeval t0;
    struct timeval t1;

    gettimeofday(&t0,0);
    //CHECK COMMAND LINE INPUTS
    if (argc < 5) {
        printf("Not enough arguments\n");
        return -1;
    }
	head_file_node = malloc(sizeof(file_node_t));
    head_file_node -> file = NULL;
    head_file_node -> filename = NULL;
    head_file_node -> next = NULL;
    host_stack_ = newHost_stack();
    sem_init(&wrt,0,1);
    sem_init(&readBlock,0,1);
    sem_init(&mutex,0,1);
    sem_init(&mutex2,0,1);
    sem_init(&temporaryStop_mutex,0,0);
    pthread_mutex_init(&conditional_mutex,NULL);
    pthread_cond_init(&cond,NULL);
    pthread_mutex_init(&conditional_mutex2,NULL);
    pthread_cond_init(&cond2,NULL);
    pthread_mutex_init(&new_mutex,NULL);
    //GET NUMBER OF REQUESTERS AND RESOLVERS TO CREATE
    int requester_thread_count = get_Integer_value(argv[1]);
    int resolver_thread_count = get_Integer_value(argv[2]);

    if (requester_thread_count == -1) {
        printf("Not Valid argument for # of requester threads\n");
        return -1;
    }
    if (resolver_thread_count == -1) {
        printf("Not Valid argument for # of resolver threads\n");
        return -1;
    }
    writersrunning = requester_thread_count;
    readersrunning = resolver_thread_count;
    //GET LOG FILE NAMES
    requester_log = fopen(argv[3], "w");
    resolver_log = fopen(argv[4], "w");
    //SAVE ALL INPUT FILES

    for (int i = 0; i < argc - 5; i++) {
        if(i < MAX_INPUT_FILES){
            printf("Argument %d: %s\n",5+i,argv[5 + i]);
            FILE* newFile = fopen(argv[5 + i], "r");
            push_file_list(argv[5 + i],head_file_node,newFile);
        }
    }
    //print_file_list(head_file_node);
    //CHECK IF THEY OPENED 
    if (requester_log == NULL) {
        //NO LOG FILE
        printf("No REQUESTER_LOG File\n");
        return -1;
    }
    if (resolver_log == NULL) {
        //NO LOG FILE
        printf("No resolver log File\n");
        return -1;
    }
    //DECLARE ARRAY OF THREADS
    pthread_t requesters[MAX_REQUESTER_THREADS];
    pthread_t resolvers[MAX_RESOLVER_THREADS];



    //check if input files exist if they dont print "invalid file "filename" " and move on to the next file 
    // 
    //CREATE THREADS
	printf("CREATING THREADS\n");
    for (int count = 0; count < requester_thread_count; ++count) {
       if (pthread_create(&requesters[count], NULL, requester, NULL) != 0) {
            fprintf(stderr, "error: Cannot create thread # %d\n", count);
            break;
        }
    }
    sleep(1);//This is so the requesters are first to lock wrtma
    for (int count = 0; count < resolver_thread_count; ++count) {
       if (pthread_create(&resolvers[count], NULL, resolver, NULL) != 0) {
            fprintf(stderr, "error: Cannot create thread # %d\n", count);
            break;
        }
    }
    
    printf("CLOSING THREADS\n");
    //REJOIN THREADS
    for (int count = 0; count < requester_thread_count; ++count) {
        if (pthread_join(requesters[count], NULL) != 0) {
            fprintf(stderr, "error: Cannot join thread # %d\n", count);
        }
    }
    for (int count = 0; count < resolver_thread_count; ++count) {
        if (pthread_join(resolvers[count], NULL) != 0) {
            fprintf(stderr, "error: Cannot join thread # %d\n", count);
        }
    }
    gettimeofday(&t1,0);
    float time_of_computation = (t1.tv_sec-t0.tv_sec)*1000000+ t1.tv_usec-t0.tv_usec;
    time_of_computation = time_of_computation / 1000000;
    printf("./multi-lookup: total time is %.4f seconds\n",time_of_computation);
    //then rejoin the resolver threads to conserve resources 
    //TO DO
    //* this needs to be done after requesters as requesters decide when program is finished
    return 0;
}
