#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>

#define NUMBS_MAX 200000 // max number of integers that can be read from the file
#define PROCESS_MAX 64 // max number of processes allowed for parallel execution
#define THREADS_MAX 64 // not sure of this
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

// Sequential Compute ------------------------------------------------------------------------------------------------------------
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

// Parallel Compute ---------------------------------------------------------------------------------------------------------------------
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


// Mmap ----------------------------------------------------------------------------------------------------------------------------------
unsigned long mmap_compute(int num_processes, const char *filename, unsigned long (*func)(int, int)) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    int *numbers = NULL;
    int n = 0, capacity = 16;
    numbers = malloc(capacity * sizeof(int));
    if (!numbers) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    while (fscanf(fp, "%d", &numbers[n]) == 1) {
        n++;
        if (n >= capacity) {
            capacity *= 2;
            numbers = realloc(numbers, capacity * sizeof(int));
            if (!numbers) {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }
    }
    fclose(fp);

    if (n < 1) {
        fprintf(stderr, "File must contain at least one number.\n");
        free(numbers);
        exit(EXIT_FAILURE);
    }

    // -Setup shared memory for results 
    size_t shm_size = num_processes * sizeof(unsigned long);
    unsigned long *shared_results = mmap(
        NULL, shm_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1, 0
    );

    if (shared_results == MAP_FAILED) {
        perror("mmap");
        free(numbers);
        exit(EXIT_FAILURE);
    }

    //  Divide work among child processes ---
    int chunk = n / num_processes;
    int remainder = n % num_processes;

    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        if (pid == 0) {  // Child process
            int start = i * chunk + (i < remainder ? i : remainder);
            int end = start + chunk + (i < remainder ? 1 : 0);

            unsigned long partial = 0;
            for (int j = start; j < end; j++) {
                partial += (unsigned long)numbers[j]; // accumulate this process’s portion
            }

            shared_results[i] = partial; // store child result in shared memory
            munmap(shared_results, shm_size);
            free(numbers);
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) {
            perror("fork");
            munmap(shared_results, shm_size);
            free(numbers);
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all children to finish ---
    for (int i = 0; i < num_processes; i++)
        wait(NULL);

    // combine all partial results ---
    unsigned long total = 0;
    for (int i = 0; i < num_processes; i++)
        total += shared_results[i];

    munmap(shared_results, shm_size);
    free(numbers);

    return total;
}

// // Example function to pass to mmap_compute
// unsigned long add_func(int a, int b) {
//     return (unsigned long)(a + b);
// }


// unsigned long int add(unsigned long int a, unsigned long int b) 
// {
//     unsigned long int sum = a + b;
//     return sum;
// }
    

// Threads -------------------------------------------------------------------------------------------------------------------------------
int read_numbers_from_file(char *filename) 
{
    FILE *file = fopen(filename, "r");
    if (!file) 
    {
        printf("Cannot open %s\n", filename);
        return -1;
    }

    int numbers_counter = 0;
    while (fscanf(file, "%d", &numbers[numbers_counter]) == 1) 
    {
        numbers_counter++;
        if (numbers_counter >= NUMBS_MAX) 
        break;
    }

    fclose(file);
    return numbers_counter;
}

// Note: Threads share code, data, files
// Each Thread has its own stack, registers, PC

typedef struct 
{
    int start_idx; // start index for this thread's chunk
    int end_idx; // end index for this thread's chunk  
    unsigned long result; // to store thread's partial result
} thread_data_struct;


void *partial_thread_compute(void *arg)
{
    thread_data_struct* data = (thread_data_struct*) arg;
    
    if (data->start_idx >= data->end_idx) 
    {
        data->result = 0;
        return NULL;
    }
    
    data->result = numbers[data->start_idx];
    for (int i = data->start_idx + 1; i < data->end_idx; i++) 
    {
        data->result = add(data->result, numbers[i]);
    }
    
    return NULL;
}

unsigned long int threads_compute(int num_threads, char *filename, unsigned long int (*func)(unsigned long int, unsigned long int))
{
    int count = read_numbers_from_file(filename);
    if (count <= 0) 
    {
    printf("File has no numbers\n");
    return 0;
}
    // int shared_mem[THREADS_MAX];

    if (num_threads > THREADS_MAX)
    {
        num_threads = THREADS_MAX;
    }

    pthread_t threads[THREADS_MAX];
    thread_data_struct thread_data[THREADS_MAX];
    
    // calculate work distribution per thread
    int base_chunk = count / num_threads;
    int remainder = count % num_threads;
    
    int current_start = 0;
    
    // Create threads with their work assignments
    for (int i = 0; i < num_threads; i++) 
    {
        int chunk_size = base_chunk + (i < remainder ? 1 : 0);
        
        thread_data[i].start_idx = current_start;
        thread_data[i].end_idx = current_start + chunk_size;
        current_start = thread_data[i].end_idx;
        
        // create thread
        if (pthread_create(&threads[i], NULL, partial_thread_compute, &thread_data[i]) != 0) 
        {
            perror("Failed to create thread");
            return 0;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) 
    {
        pthread_join(threads[i], NULL);
    }
    
    // Combine results from all threads
    unsigned long final_result = thread_data[0].result;
    for (int i = 1; i < num_threads; i++) 
    {
        final_result = func(final_result, thread_data[i].result);
    }
    
    return final_result;
}


// Main Experiment -----------------------------------------------------------------------------------------------------------------------
// Timer function to return the current time in milliseconds
double now_ms() 
{
    struct timeval t; gettimeofday(&t, NULL);
    return (t.tv_sec * 1000.0) + (t.tv_usec / 1000.0);
}


int main(void) 
{
    srand(time(NULL));

    // part 1: N vs. Time
    FILE *csv1 = fopen("N_vs_Time_p5.csv", "w");
    if (access("N_vs_Time_p5.csv", F_OK) != -1) {
    // file exists → open in append mode (no header)
    csv1 = fopen("N_vs_Time_p5.csv", "a");
    } else {
        // file does not exist → create and write header
        csv1 = fopen("N_vs_Time_p5.csv", "w");
        fprintf(csv1, "N,Sequential_ms,Parallel_ms,Speedup,Mmap_ms,Threads_ms\n");
    }

    int fixed_proc = 8;
    int fixed_threads = 8;
    for (int N = 10; N <= 50000; N += (N < 1000 ? 100 : 1225)) 
    {
        char filename[64];
        sprintf(filename, "data_%d.txt", N);
        FILE *f = fopen(filename, "w");
        for (int i = 0; i < N; i++)
            fprintf(f, "%d\n", rand() % 1000);
        fclose(f);

        double seq_sum = 0, par_sum = 0, mmap_sum = 0, thread_sum = 0;
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

            double m1 = now_ms();
            mmap_compute(fixed_proc, filename, add);
            double m2 = now_ms();
            mmap_sum += (m2 - m1);

            double t1 = now_ms();
            threads_compute(fixed_threads, filename, add);
            double t2 = now_ms();
            thread_sum += (t2 - t1);

        }

        double seq_avg = seq_sum / 5.0;
        double par_avg = par_sum / 5.0;
        double mmap_avg = mmap_sum / 5.0;
        double thread_avg = thread_sum / 5.0;
        double speedup = seq_avg / par_avg;

        fprintf(csv1, "%d,%.6f,%.6f,%.3f,%.6f,%.6f\n", N, seq_avg, par_avg, speedup, mmap_avg, thread_avg);
        printf(" N = %d done (Proc = %d) (Threads = %d)\n", N, fixed_proc, fixed_threads);
        fflush(csv1);
    }
    fclose(csv1);

    // part 2: n_workers vs. Time
    FILE *csv2 = fopen("nworkers_vs_Time_P5.csv", "w");
    if (access("nworkers_vs_Time_P5.csv", F_OK) != -1) {
    csv2 = fopen("nworkers_vs_Time_P5.csv", "a");
    } else {
        csv2 = fopen("nworkers_vs_Time_P5.csv", "w");
        fprintf(csv2, "n_proc,Sequential_ms,Parallel_ms,Speedup,Mmap_ms,Threads_ms\n");
    }

    int fixed_N = 3000;
    char filename2[64];
    sprintf(filename2, "data_fixedN.txt");
    FILE *f = fopen(filename2, "w");
    for (int i = 0; i < fixed_N; i++)
    {
        fprintf(f, "%d\n", rand() % 5);
    }
    fclose(f);

    int procs[50] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18,
    20, 22, 24, 26, 28, 30, 32, 34, 36, 38,
    40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
    60, 62, 64, 66, 68, 70, 72, 74, 76, 78,
    80, 82, 84, 86, 88, 90, 92, 94, 96, 100
    };

    int threads[50] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18,
    20, 22, 24, 26, 28, 30, 32, 34, 36, 38,
    40, 42, 44, 46, 48, 50, 52, 54, 56, 58,
    60, 62, 64, 66, 68, 70, 72, 74, 76, 78,
    80, 82, 84, 86, 88, 90, 92, 94, 96, 100
    };

    int num_procs = sizeof(procs) / sizeof(procs[0]); // = num_threads
    int num_threads = num_procs;

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
        int n_threads = threads[i];
        double par_sum = 0;
        double mmap_sum = 0;
        double thread_sum = 0;

        for (int r = 0; r < 5; r++) 
        {
            double p1 = now_ms();
            parallel_compute(filename2, n_proc, add);
            double p2 = now_ms();
            par_sum += (p2 - p1);

            double m1 = now_ms();
            mmap_compute(n_proc, filename2, add);
            double m2 = now_ms();
            mmap_sum += (m2 - m1);

            double t1 = now_ms();
            threads_compute(fixed_threads, filename2, add);
            double t2 = now_ms();
            thread_sum += (t2 - t1);
        }
        double par_avg = par_sum / 5.0;
        double mmap_avg = mmap_sum / 5.0;
        double speedup = seq_avg / par_avg;
        double thread_avg = thread_sum / 5.0;

        fprintf(csv2, "%d,%.6f,%.6f,%.3f,%.6f,%.6f\n", n_proc, seq_avg, par_avg, speedup, mmap_avg, thread_avg);
        printf(" n_proc = %d, n_threads = %d done (N = %d)\n", n_proc, n_threads, fixed_N);
        fflush(csv2);
    }

    fclose(csv2);

    printf("\nFiles generated:\n");
    printf("   N_vs_Time_p5.csv\n");
    printf("   nworkers_vs_Time_p5.csv\n");
    return 0;
}
