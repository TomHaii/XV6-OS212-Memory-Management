#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MEM_IN_RAM 16
#define LIMIT_PAGES 24

int main(int argc, char *argv[]){
 
    char *mem_pages[LIMIT_PAGES];
    int pid;

    printf("Welcome to assignment 3 sanity tests!\n");

    printf( "\nTest 1 : Fill the pages\n");
    for(int i = 0; i<LIMIT_PAGES; i++) {
        mem_pages[i] = sbrk(PGSIZE);
    }
    printf("Test 1 - Fill the pages ended. :)\n\n");

    printf("\nTest 2: Write to pages\n");
    for(int i = 0; i < LIMIT_PAGES; i++){
        printf("Writing -");
        for(int j = 0; j < 5; j++){
            mem_pages[i][j] = i + j;
            printf(" %d to [%d][%d]", i + j, i, j);
        }
        printf("\n");
    }

    printf("\n Test 2 - Finished writing to pages\n");
    printf("\nTest 3: Read from pages\n");
    for(int i = 0; i < LIMIT_PAGES; i++){
        printf("Reading -");
        for(int j = 0; j < 5; j++){
            if(mem_pages[i][j] != i + j){
                printf("pid:%d, bad reading\n", getpid());
                printf("TEST FAILED\n");
                exit(-1);
            }
            else{
                printf(" %d from [%d][%d]", mem_pages[i][j], i, j);
            }
        }
        printf("\n");
    }
    printf("\n Test 3 - Finished reading from pages\n");

    for(int i = 0; i<24; i++) {
        sbrk(-PGSIZE);
    }
    printf("\n Test 4 - Fork\n"); 
    printf("Writing -");
    for(int i = 0; i < 16; i++){
        mem_pages[i] = sbrk(PGSIZE);
        mem_pages[i][0] = i;
        printf("%d to [%d][%d]", i , i, 0);
        printf("\n");
    }

    if((pid = fork()) < 0){
        printf("Fork failed\n");
    }
    else if (pid == 0) {
        printf("Child is reading -\n");
        for(int i = 0; i < 16; i++){
         printf("%d from [%d][0]\n", mem_pages[i][0], i);
        
        }
        exit(0);
    }
    else {
        wait(0);
        printf("Parent is reading -\n");
        for(int i = 0; i < 16; i++){
            printf("%d from [%d][0]\n", mem_pages[i][0], i);
        }
        
        printf("\n Test 4 - Finished Fork\n");  
        printf("\nTests finished successfully\nGood Job!\n");
        exit(0);
    }
    exit(0);
    return 0;
}