#include "kernel/types.h"
#include "kernel/stat.h"
#include "user.h"
#include "kernel/syscall.h"

int main(int argc, char *argv[]) {
  int i;
  int buffSize = 4096;
  char *pages[24];  
  printf( "\n\n\n#########################\n\tTests:\n#########################\n\n\n");

  printf( "\tAnalyze after init...\n\n\n");
  //sleep(500);

  // TEST1
  printf( "\n\nTest1: Allocation\n");
  for(i = 0; i<24; i++) {
    pages[i] = sbrk(buffSize);
    if (i == 5) {
      printf( "\tAnalyze after 5th allocation...\n\n\n");
   //   sleep(500);
    }
  }
  printf( "\tAnalyze end of allocation...\n\n\n");
  //sleep(500);

  // TEST2
  printf( "\n\nTest2: Writing\n");
  for(i = 0; i<24; i++) {
    pages[i][0] = 10;
  }
  printf( "\tAnalyze after writing...\n\n\n");
 // sleep(500);

  // TEST3
  printf( "\n\nTest3: Reading\n");
  for(i = 0; i<24; i++)
    printf( "\t\tReading values... (%d)\n", pages[i][0]);
  printf( "\n\tAnalyze after reading...\n\n\n");
  //sleep(500);

  for(i = 0; i<24; i++) {
    free(pages[i]);
  }

  // TEST4
  printf( "\n\nTest4: Fork\n");
  for(i = 0; i<24; i++) {
    pages[i] = sbrk(buffSize);
    pages[i][0] = 10;
  }
  int pid = fork();
  if(pid == 0) {
    
    for(i = 0; i<3; i++) 
      pages[i][0] = 10;
    printf( "\tAnalyze after child writing...\n\n\n");
    //sleep(500);

    for(i = 0; i<24; i++) 
      printf( "\t\tReading values... (%d)\n", pages[i][0]);
    printf( "\n\tAnalyze after child reading...\n\n\n");
    //sleep(500);
    
    exit(1);
  }
  wait(0);
  printf( "\n\tAnalyze after child exit...\n\n\n");
  
  for(i = 0; i<24; i++) {
    free(pages[i]);
  }

  exit(1);
}
// #include "kernel/param.h"
// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user.h"
// #include "kernel/fs.h"
// #include "kernel/fcntl.h"
// #include "kernel/syscall.h"
// #include "kernel/memlayout.h"

// #define N_PAGES 24

// char* data[N_PAGES];

// volatile int main(int argc, char *argv[]) {

// 	int i = 0;
// 	int n = N_PAGES;

// 	for (i = 0; i < n ;)
// 	{
// 		data[i] = sbrk(4096);
// 		data[i][0] = 00 + i;
// 		data[i][1] = 10 + i;
// 		data[i][2] = 20 + i;
// 		data[i][3] = 30 + i;
// 		data[i][4] = 40 + i;
// 		data[i][5] = 50 + i;
// 		data[i][6] = 60 + i;
// 		data[i][7] = 70 + i;
// 		printf("allocated new page #%d in: %x\n", i, data[i]);
// 		i++;
// 	}

	
// 	int j;
// 	for(j = 1; j < n; j++)
// 	{
// 		printf("j:  %d\n",j);

// 		for(i = 0; i < j; i++) {
// 			data[i][10] = 2; // access to the i-th page
// 			printf("%d, ",i);
// 		}
// 		printf("\n");
// 	}
//     printf("Starting Fork Test XD\n");
// 	int k;
// 	int pid = fork();
// 	if (pid)
// 		wait(0);
// 	else {
// 		printf("\nGo through same 8 pages and different 8 others\n");
// 		for(j = 0; j < 8; j++){
// 			for(i = 20; i < 24; i++) {
// 				data[i][10] = 1;
// 				printf("%d, ",i);
// 			}
// 			printf("\n");
// 			switch (j%4) {
// 			case 0:
// 				for(k = 0; k < 4; k++) {
// 					data[k][10] = 1;
// 					printf("%d, ",k);
// 				}
// 				break;
// 			case 1:
// 				for(k = 4; k < 8; k++) {
// 					data[k][10] = 1;
// 					printf("%d, ",k);
// 				}
// 				break;
// 			case 2:
// 				for(k = 8; k < 12; k++) {
// 					data[k][10] = 1;
// 					printf("%d, ",k);
// 				}
// 				break;
// 			case 3:
// 				for(k = 12; k < 16; k++) {
// 					data[k][10] = 1;
// 					printf("%d, ",k);
// 				}
// 				break;
// 			}
// 			printf("\n");
// 		}
// 	}
// 	exit(0);
// 	return 0;
// }


// #include "kernel/types.h"
// #include "kernel/stat.h"
// #include "user/user.h"

// #define PGSIZE 4096
// #define MEM_LIMIT 16
// #define LIMIT 24

// int func(int p){
//     p++;
//     return p;
// }

// int main(int argc, char *argv[]){
 
//     int i, j, pid, num;
//     // int pid = 0;
//     char *pages[MEM_LIMIT*2];


//     printf("\n\nSTRART TESTS!!!!!!!!!\n\n"); 


//     printf("------------------- allocate memory and write to it -------------------\n\n"); 

//     int p = 0;

//     for (i = 0; i < LIMIT; i++) {

//         //pages[i] = malloc(PGSIZE);
//         pages[i] = sbrk(PGSIZE);
//         *pages[i] = i;
//         if(i % 5 == 0){
//             func(p);
//             p++;
//         }
        
//     }

//     printf("------------------- test fork -------------------\n\n"); 


//     if((pid = fork()) < 0){
//         printf("fork return negative value - failed\n");
//     }
//     else if (pid == 0) { // child
//         for(j = 0; j < LIMIT; j++){
//             if(pages[j]){
//                 num = j;
//                 *pages[j] = num;
//                 printf("process %d: pages array [%d] is %d \n", getpid(), j, (int)*(pages[j])); 
//             } 
//         }
//         exit(0);
//     }
//     else {  // parent
//         wait(0);
//         for(j = 0; j < LIMIT; j++){
//             if(pages[j]){
//                 printf("Process %d: pages array [%d] is %d \n", getpid(), j, (int)*(pages[j])); 
//             } 
//         }
//         printf("------------------- end test OK -------------------\n\n");  
//         exit(0);
//     }
//     exit(0);
// }