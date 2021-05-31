#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define PGSIZE 4096
#define MEM_IN_RAM 16
#define LIMIT_PAGES 24
int alloc_write_read_fork_test();
int fifo_test();
int nfua_test();
int lapa_test();

void main(int argc, char *argv[]){

    printf("Welcome to assignment 3 sanity tests!\n");
    int pid;
    if((pid = fork()) < 0){
        printf("Fork failed\n");
    }
    else{
        if(pid == 0){
            alloc_write_read_fork_test();
        }
        else{
            wait(&pid);
        }
    }
    if((pid = fork()) < 0){
        printf("Fork failed\n");
    }
    else {
        if(pid == 0){
        #ifdef SCFIFO
            fifo_test();
        #endif
        #ifdef NFUA
            nfua_test();
        #endif
        #ifdef LAPA
            lapa_test();
        #endif
        } else {
            wait(&pid);
            printf("\nTests finished successfully\nGood Job!\n");
            exit(0);
        }
    }
    exit(0);
}

int alloc_write_read_fork_test(){
    char *mem_pages[LIMIT_PAGES];
    int pid;
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
        exit(0);
    }
    exit(0);
    return 0;
}

int 
fifo_test(){
    int* mem[18];
    printf("Starting SCFIFO Test\n");
    for(int i = 0; i < 16; i++){
        mem[i] = (int*)sbrk(PGSIZE);
    }
    sleep(20);
    printf("Adding another page, page 0 is the first in the que so he moves to the swap file\n");
    mem[16] = (int*)sbrk(PGSIZE);
    printf("We accessing page 1 now, so he gets a second chance\n");
    printf("Mem at position 2 address -> %p, value -> %d\n", mem[1], *mem[1]);
    sleep(10);
    printf("Page 2 should move to the swap file now\n");
    mem[17] = (int*)sbrk(PGSIZE);
    printf("Page 2 should cause pagefault since he is in the swap file\n");
    printf("Mem at position 2 address -> %p, value -> %d\n", mem[2], *mem[2]);
    exit(0);
    return 0;
}

int
nfua_test(){
    int* mem[17];
    printf("Starting NFUA Test\n");
    for(int i = 0; i < 16; i++){
        mem[i] = (int*)sbrk(PGSIZE);
    }
    sleep(20);
    printf("We accessing every page besides 4 now\n");
    for(int i = 0; i < 16; i++){
        if(i != 4)
            *mem[i] = 10;
    }
    sleep(10);
    printf("Allocating new page and accessing it, page 4 should move to the swap file now\n");
    mem[16] = (int*)sbrk(PGSIZE);
    *mem[16] = 10;
    printf("Accessing page 4, should cause page fault\n");
    sleep(5);
    printf("Mem at position 4 address -> %p, value -> %d\n", mem[4], *mem[4]);
    exit(0);
    return 0;
}

int
lapa_test(){
    int* mem[16];
    printf("Starting LAPA Test\n");
    for(int i = 0; i < 16; i++){
        mem[i] = (int*)sbrk(PGSIZE);
        *mem[i] = i;
    }
    sleep(20);
    printf("We accessing every page besides 5 now\n");
    for(int i = 0; i < 16; i++){
        if(i != 5)
            *mem[i] = 10;
    }
    printf("Allocating new page and accessing it, page 5 should move to the swap file now\n");
    sleep(10);
    mem[16] = (int*)sbrk(PGSIZE);
    *mem[16] = 10;
    printf("Accessing page 5, should cause page fault\n");
    sleep(5);
    printf("Mem at position 5 address -> %p, value -> %d\n", mem[5], *mem[5]);
    exit(0);
    return 0;
}