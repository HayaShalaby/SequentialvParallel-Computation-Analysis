#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>


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

// Example function to pass to mmap_compute
unsigned long add_func(int a, int b) {
    return (unsigned long)(a + b);
}

int main() {
    unsigned long result = mmap_compute(4, "numbers.txt", add_func);
    printf("Sum = %lu\n", result);
    return 0;
}
