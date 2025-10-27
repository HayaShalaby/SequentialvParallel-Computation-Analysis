#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

#define NUMBS_MAX 200000 // max number of integers that can be read from the file
#define PROCESS_MAX 64 // max number of processes allowed for parallel execution
static int numbers[NUMBS_MAX]; // arr to hold numbers read from file
static int channelOfPipe[PROCESS_MAX][2]; // // pipes for parent-child communication

// 'Heavy" add function
int add(int a, int b) 
{
    int sum = a + b;
    // artificial (dummy) workload to expose parallel overhead (wasn't showcased when add was too simple)
    for (volatile int i = 0; i < 500; i++) 
    {
        sum = (sum * 31 + i) % 100000;
    }
    return sum;
}

// Sequential Compute
struct Node { int num; struct Node *next; };
struct LL { struct Node *head; };

int sequential_compute(char *filename, int (*fp)(int, int)){
    struct LL nums;
    nums.head = NULL;

    FILE* fptr = fopen(filename, "r");
    if (fptr == NULL) {
        printf("The file is not opened.\n");
        return 0;
    }

    while(!feof(fptr)){
        struct Node* new_node = (struct Node*)malloc(sizeof(struct Node));
        if (fscanf(fptr, "%d", &new_node->num) == 1) {
            new_node->next = nums.head;
            nums.head = new_node;
        } else {
            free(new_node);
            break;
        }
    }
    fclose(fptr);

    if (nums.head == NULL) return 0;

    int sum = nums.head->num;
    struct Node* current_num = nums.head->next;

    while(current_num != NULL){
        sum = fp(sum, current_num->num);
        current_num = current_num->next;
    }

    struct Node* temp = nums.head;
    while (temp != NULL) {
        struct Node* node_to_free = temp;
        temp = temp->next;
        free(node_to_free);
    }
    nums.head = NULL;

    return sum;
}

// Parallel Compute
int read_numbers(char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Cannot open %s\n", filename);
        return -1;
    }

    int numbersCounter = 0;
    while (fscanf(file, "%d", &numbers[numbersCounter]) == 1) {
        numbersCounter++;
        if (numbersCounter >= NUMBS_MAX) break;
    }

    fclose(file);
    return numbersCounter;
}

void compute_partial(int start, int end, int (*fp)(int,int), int pipefd[2]) {
    close(pipefd[0]);  // Close read end
    if (start >= end) exit(0);

    int number = numbers[start];
    for (int i = start + 1; i < end; i++) {
        number = fp(number, numbers[i]);
    }

    write(pipefd[1], &number, sizeof(int));
    close(pipefd[1]);
    exit(0);
}

int parallel_compute(char *filename, int n_proc, int (*fp)(int, int)) {
    int count = read_numbers(filename);
    if (count <= 0) {
        printf("File has no numbers\n");
        return 0;
    }

    if (n_proc > PROCESS_MAX) n_proc = PROCESS_MAX;
    if (n_proc > count) n_proc = count;

    int branch = (count + n_proc - 1) / n_proc;
    int answer = 0, begin = 1;

    for (int i = 0; i < n_proc; i++) {
        if (pipe(channelOfPipe[i]) < 0) {
            perror("Pipe creation failed");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == 0) { // Child
            int start = i * branch;
            int end = start + branch;
            if (end > count) end = count;
            compute_partial(start, end, fp, channelOfPipe[i]);
        } else if (pid > 0) { // Parent
            close(channelOfPipe[i][1]);  // Close write end
        } else {
            perror("Fork failed");
            exit(1);
        }
    }

    for (int i = 0; i < n_proc; i++) {
        int part = 0;
        read(channelOfPipe[i][0], &part, sizeof(int));
        close(channelOfPipe[i][0]);
        if (begin) {
            answer = part;
            begin = 0;
        } else {
            answer = fp(answer, part);
        }
    }

    for (int i = 0; i < n_proc; i++) {
        wait(NULL);
    }

    return answer;
}

// Timer function to return the current time in milliseconds
double now_ms() 
{
    struct timeval t; gettimeofday(&t, NULL);
    return (t.tv_sec * 1000.0) + (t.tv_usec / 1000.0);
}

// Main Experiment
int main(void) 
{
    srand(time(NULL));

    // part 1: N vs. Time
    FILE *csv1 = fopen("results_N_vs_Time.csv", "w");
    fprintf(csv1, "N,Sequential_ms,Parallel_ms,Speedup\n");

    int fixed_proc = 8;
    for (int N = 10; N <= 50000; N += (N < 1000 ? 100 : 2000)) 
    {
        char filename[64];
        sprintf(filename, "data_%d.txt", N);
        FILE *f = fopen(filename, "w");
        for (int i = 0; i < N; i++)
            fprintf(f, "%d\n", rand() % 1000);
        fclose(f);

        double seq_sum = 0, par_sum = 0;
        for (int r = 0; r < 5; r++) 
        {
            double s1 = now_ms();
            sequential_compute(filename, add);
            double s2 = now_ms();
            seq_sum += (s2 - s1);

            double p1 = now_ms();
            parallel_compute(filename, fixed_proc, add);
            double p2 = now_ms();
            par_sum += (p2 - p1);
        }

        double seq_avg = seq_sum / 5.0;
        double par_avg = par_sum / 5.0;
        double speedup = seq_avg / par_avg;

        fprintf(csv1, "%d,%.6f,%.6f,%.3f\n", N, seq_avg, par_avg, speedup);
        printf(" N = %d done (Proc = %d)\n", N, fixed_proc);
        fflush(csv1);
    }
    fclose(csv1);

    // part 2: n_proc vs. Time
    FILE *csv2 = fopen("results_nproc_vs_Time.csv", "w");
    fprintf(csv2, "n_proc,Sequential_ms,Parallel_ms,Speedup\n");

    int fixed_N = 3000;
    char filename2[64];
    sprintf(filename2, "data_fixedN.txt");
    FILE *f = fopen(filename2, "w");
    for (int i = 0; i < fixed_N; i++)
    {
        fprintf(f, "%d\n", rand() % 1000);
    }
    fclose(f);

    int procs[] = {1, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 32, 40};
    int num_procs = sizeof(procs) / sizeof(procs[0]);

    double seq_sum = 0;
    for (int r = 0; r < 5; r++) 
    {
        double s1 = now_ms();
        sequential_compute(filename2, add);
        double s2 = now_ms();
        seq_sum += (s2 - s1);
    }
    double seq_avg = seq_sum / 5.0;

    for (int i = 0; i < num_procs; i++) 
    {
        int n_proc = procs[i];
        double par_sum = 0;

        for (int r = 0; r < 5; r++) 
        {
            double p1 = now_ms();
            parallel_compute(filename2, n_proc, add);
            double p2 = now_ms();
            par_sum += (p2 - p1);
        }

        double par_avg = par_sum / 5.0;
        double speedup = seq_avg / par_avg;

        fprintf(csv2, "%d,%.6f,%.6f,%.3f\n", n_proc, seq_avg, par_avg, speedup);
        printf(" n_proc = %d done (N = %d)\n", n_proc, fixed_N);
        fflush(csv2);
    }

    fclose(csv2);

    printf("\nFiles generated:\n");
    printf("   results_N_vs_Time.csv\n");
    printf("   results_nproc_vs_Time.csv\n");
    return 0;
}


// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/time.h>
// #include <unistd.h>
// #include <sys/wait.h>
// #include <time.h>

// #define NUMBS_MAX 200000 // maximum number of integers that can be read from the file
// #define PROCESS_MAX 64 // maximum number of processes allowed for parallel execution
// static int numbers[NUMBS_MAX]; // array to hold numbers read from file
// static int channelOfPipe[PROCESS_MAX][2]; // pipes for parent-child communication

// // "Heavy" add function
// int add(int a, int b) 
// {
//     int sum = a + b;
//     // artificial workload (computationally expensive operation) to make timing differences between sequential and parallel execution noticeable
//     for (volatile int i = 0; i < 1000; i++) // volatile to prevent optimization from the compiler (intentionally making it computationally expensive)
//     {
//         sum = (sum * 31 + i) % 100000;
//     }
//     return sum;
// }

// // Sequential Compute

// struct Node { int num; struct Node *next; };
// struct LL { struct Node *head; };

// int sequential_compute(char *filename, int (*fp)(int, int)){
//     struct LL nums;
//     nums.head = NULL;

//     FILE* fptr = fopen(filename, "r");
//     if (fptr == NULL) {
//         printf("The file is not opened.\n");
//         return 0;
//     }

//     while(!feof(fptr)){
//         struct Node* new_node = (struct Node*)malloc(sizeof(struct Node));
//         if (fscanf(fptr, "%d", &new_node->num) == 1) {
//             new_node->next = nums.head;
//             nums.head = new_node;
//         } else {
//             free(new_node);
//             break;
//         }
//     }
//     fclose(fptr);

//     if (nums.head == NULL) return 0;

//     int sum = nums.head->num;
//     struct Node* current_num = nums.head->next;
//     while(current_num != NULL){
//         sum = fp(sum, current_num->num);
//         current_num = current_num->next;
//     }

//     struct Node* temp = nums.head;
//     while (temp != NULL) {
//         struct Node* node_to_free = temp;
//         temp = temp->next;
//         free(node_to_free);
//     }
//     nums.head = NULL;

//     return sum;
// }

// // Parallel Compute
// int read_numbers(char *filename) 
// {
//     FILE *file = fopen(filename, "r");
//     if (!file) 
//     {
//         printf("Cannot open %s\n", filename);
//         return -1;
//     }

//     int numbersCounter = 0;
//     while (fscanf(file, "%d", &numbers[numbersCounter]) == 1) 
//     {
//         numbersCounter++;
//         if (numbersCounter >= NUMBS_MAX)
//         {
//             break;
//         }
//     }

//     fclose(file);
//     return numbersCounter;
// }

// void compute_partial(int start, int end, int (*fp)(int,int), int pipefd[2]) 
// {
//     close(pipefd[0]);
//     if (start >= end) exit(0);

//     int number = numbers[start];
//     for (int i = start + 1; i < end; i++) 
//     {
//         number = fp(number, numbers[i]);
//     }

//     write(pipefd[1], &number, sizeof(int));
//     close(pipefd[1]);
//     exit(0);
// }

// int parallel_compute(char *filename, int n_proc, int (*fp)(int, int)) 
// {
//     int count = read_numbers(filename);
//     if (count <= 0) 
//     {
//         printf("File has no numbers\n");
//         return 0;
//     }

//     if (n_proc > PROCESS_MAX)
//     {
//         n_proc = PROCESS_MAX;
//     }
//     if (n_proc > count)
//     { 
//         n_proc = count;
//     }

//     int branch = ((count + n_proc - 1) / n_proc);
//     int answer = 0, begin = 1;

//     for (int i = 0; i < n_proc; i++) 
//     {
//         if (pipe(channelOfPipe[i]) < 0) 
//         {
//             perror("Pipe creation failed");
//             exit(1);
//         }

//         pid_t pid = fork();
//         if (pid == 0) // Child process
//         {
//             int start = i * branch;
//             int end = start + branch;
//             if (end > count) end = count;
//             compute_partial(start, end, fp, channelOfPipe[i]);
//         } 
//         else if (pid > 0) // Parent process
//         { 
//             close(channelOfPipe[i][1]);  // Parent closes write end
//         } 
//         else 
//         {
//             perror("Fork failed");
//             exit(1);
//         }
//     }

//     // Collect partial results from all children
//     for (int i = 0; i < n_proc; i++) 
//     {
//         int part = 0;
//         read(channelOfPipe[i][0], &part, sizeof(int));
//         close(channelOfPipe[i][0]);

//         if (begin) 
//         {
//             answer = part;
//             begin = 0;
//         } 
//         else 
//         {
//             answer = fp(answer, part);
//         }
//     }

//     // Wait for all children to complete
//     for (int i = 0; i < n_proc; i++) 
//     {
//         wait(NULL);
//     }

//     return answer;
// }

// // Timer function to return the current time in milliseconds
// double now_ms() 
// {
//     struct timeval t; 
//     gettimeofday(&t, NULL);
//     return (t.tv_sec * 1000.0) + (t.tv_usec / 1000.0);
// }

// // main experiment
// /* runs two sets of experiments and writes the data into .csv files:
//    1. N vs. Time  (increasing input size and fixed number of processes)
//    2. n_proc vs. Time (fixed input size and increasing number of processes)
// */
// int main(void) 
// {
//     srand(time(NULL));

//     /* Part 1: N vs. Time
//        Tests how computation time varies with input size (N) (while fixing the number of processes)
//     */
//     FILE *csv1 = fopen("results_N_vs_Time.csv", "w");
//     fprintf(csv1, "N,Sequential_ms,Parallel_ms,Speedup\n");

//     int fixed_proc = 4;
//     for (int N = 10; N <= 50000; N += (N < 1000 ? 100 : 2000)) 
//     {
//         // generate a random input file with N integers
//         char filename[64];
//         sprintf(filename, "data_%d.txt", N);
//         FILE *f = fopen(filename, "w");
//         for (int i = 0; i < N; i++)
//         {
//             fprintf(f, "%d\n", rand() % 1000);
//         }
//         fclose(f);

//         // Run both sequential and parallel versions multiple times
//         double seq_sum = 0, par_sum = 0;
//         for (int r = 0; r < 5; r++) 
//         {
//             double s1 = now_ms();
//             sequential_compute(filename, add);
//             double s2 = now_ms();
//             seq_sum += (s2 - s1);

//             double p1 = now_ms();
//             parallel_compute(filename, fixed_proc, add);
//             double p2 = now_ms();
//             par_sum += (p2 - p1);
//         }

//         // Compute averages and speedup
//         double seq_avg = seq_sum / 5.0;
//         double par_avg = par_sum / 5.0;
//         double speedup = seq_avg / par_avg;

//         fprintf(csv1, "%d,%.6f,%.6f,%.3f\n", N, seq_avg, par_avg, speedup);
//         printf(" N = %d done (Proc = %d)\n", N, fixed_proc);
//         fflush(csv1);
//     }
//     fclose(csv1);

//     /* Part 2: n_proc vs. Time
//        Tests how performance varies with the number of processes for a fixed N
//     */
//     FILE *csv2 = fopen("results_nproc_vs_Time.csv", "w");
//     fprintf(csv2, "n_proc,Sequential_ms,Parallel_ms,Speedup\n");

//     int fixed_N = 6000;  // Keep workload constant
//     char filename2[64];
//     sprintf(filename2, "data_fixedN.txt");
//     FILE *f = fopen(filename2, "w");
//     for (int i = 0; i < fixed_N; i++)
//     {
//         fprintf(f, "%d\n", rand() % 1000);
//     }
//     fclose(f);

//     // List of process counts to test
//     int procs[] = {1, 2, 4, 6, 8, 10, 12, 16, 20, 24, 28, 32, 40};
//     int num_procs = sizeof(procs) / sizeof(procs[0]);

//     // baseline sequential execution time
//     double seq_sum = 0;
//     for (int r = 0; r < 5; r++) 
//     {
//         double s1 = now_ms();
//         sequential_compute(filename2, add);
//         double s2 = now_ms();
//         seq_sum += (s2 - s1);
//     }
//     double seq_avg = (seq_sum / 5.0);

//     // measure parallel performance for varying process counts
//     for (int i = 0; i < num_procs; i++) 
//     {
//         int n_proc = procs[i];
//         double par_sum = 0;

//         for (int r = 0; r < 5; r++) 
//         {
//             double p1 = now_ms();
//             parallel_compute(filename2, n_proc, add);
//             double p2 = now_ms();
//             par_sum += (p2 - p1);
//         }

//         double par_avg = par_sum / 5.0;
//         double speedup = seq_avg / par_avg;

//         fprintf(csv2, "%d,%.6f,%.6f,%.3f\n", n_proc, seq_avg, par_avg, speedup);
//         printf(" n_proc = %d done (N = %d)\n", n_proc, fixed_N);
//         fflush(csv2);
//     }

//     fclose(csv2);

//     printf("\nFiles generated:\n");
//     printf("results_N_vs_Time.csv\n");
//     printf("results_nproc_vs_Time.csv\n");
//     return 0;
// }
