#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

#define NUMBS_MAX 400
#define PROCESS_MAX 8
#define BUFFER_SIZE 128
static int numbers[NUMBS_MAX];
static int channelOfPipe[PROCESS_MAX][2];

int add(int a, int b)
{
    return a + b;
}

int read_numbers(char *filename)
{
    int numbersCounter = 0;
    int number = 0;
    int sign = 1;
    char store_character;

    int file = open(filename, O_RDONLY);
    if (file < 0) {
        printf(1, "file cannot be opened: %s\n", filename);
        return -1;
    }

    while (read(file, &store_character, 1) == 1)
    {
        if (store_character == '-')
        {
            sign = -1;
        }
        else if (store_character >= '0' && store_character <= '9')
        {
            number = number * 10 + (store_character - '0');
        }
        else if (number != 0 || sign == -1)
        {
            numbers[numbersCounter++] = number * sign;
            if (numbersCounter >= NUMBS_MAX) 
            {
            break;
            }
            number = 0;
            sign = 1;
        }
    }
    if (number != 0 || sign == -1)
    {
        numbers[numbersCounter++] = number * sign;
    }

    close(file);
    return numbersCounter;
}

void compute_partial(int start, int end, int (*fp)(int,int), int read_pipe[2])
{
    close(read_pipe[0]);
    if (start >= end)
    {
        exit();
    }

    int number = numbers[start];
    for (int i = start + 1; i < end; i++)
    {
        number = fp(number, numbers[i]);
    }

    write(read_pipe[1], &number, sizeof(int));
    close(read_pipe[1]);
    exit();
}

int parallel_compute(char *filename, int n_proc, int (*fp)(int, int))
{
    int count = read_numbers(filename);
    int branch, answer = 0;
    int begin = 1;

    if (count <= 0)
    {
        printf(1, "file has no numbers\n");
        return 0;
    }

    if (n_proc > PROCESS_MAX)
    {
        n_proc = PROCESS_MAX;
    }

    if (n_proc > count)
    {
        n_proc = count;
    }

    branch = (count + n_proc - 1) / n_proc;

    for (int i = 0; i < n_proc; i++)
    {
        if (pipe(channelOfPipe[i]) < 0)
        {
            printf(1, "failed creating pipe\n");
            exit();
        }

        int pid = fork();
        if (pid == 0)
        {
            int start = i * branch;
            int end = start + branch;
            if (end > count)
            {
                end = count;
            }
            compute_partial(start, end, fp, channelOfPipe[i]);
        }
        else if (pid > 0)
        {
            close(channelOfPipe[i][1]);
        }
        else
        {
            printf(1, "fail creating fork\n");
            exit();
        }
    }

    for (int i = 0; i < n_proc; i++)
    {
        int part = 0;
        read(channelOfPipe[i][0], &part, sizeof(int));
        close(channelOfPipe[i][0]);
        if (begin)
        {
            answer = part;
            begin = 0;
        }
        else
        {
            answer = fp(answer, part);
        }
    }

    for (int i = 0; i < n_proc; i++)
    {
        wait();
    }

    return answer;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf(1, "need file name and number of process\n");
        exit();
    }

    char *file = argv[1];
    int answer = 0;
    int n_proc = 0;
    char *s = argv[2];
    while (*s >= '0' && *s <= '9') 
    {
        n_proc = n_proc * 10 + (*s - '0');
        s++;
    }

    answer = parallel_compute(file, n_proc, add);
    printf(1, "Parallel result = %d\n", answer);
    exit();
}
