#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>



#define NUMBS_MAX 200000 // max number of integers that can be read from the file
#define THREADS_MAX 64 // max number of processes allowed for parallel execution
static int numbers[NUMBS_MAX]; // arr to hold numbers read from file


unsigned long int add(unsigned long int a, unsigned long int b) 
{
    unsigned long int sum = a + b;
    return sum;
}
    
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


void *partial_thread_compute(void* arg)
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
