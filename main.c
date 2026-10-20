#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include "timsort.h"

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

typedef struct {
    int *arr;
    size_t start;
    size_t end;
    sem_t *sem;
} task_t;

static void *worker(void *arg) {
    task_t *t = (task_t *)arg;
    sem_wait(t->sem);
    timsort(t->arr + t->start, t->end - t->start, sizeof(int), cmp_int);
    sem_post(t->sem);
    return NULL;
}

int main(int argc, char *argv[]) {
    int num_threads = 4;
    size_t array_size = 1000000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            num_threads = atoi(argv[++i]);
        else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc)
            array_size = (size_t)atoll(argv[++i]);
    }

    printf("Потоков: %d, размер массива: %zu\n", num_threads, array_size);

    int *arr = malloc(array_size * sizeof(int));
    if (!arr) { perror("malloc"); return 1; }

    srand((unsigned)time(NULL));
    for (size_t i = 0; i < array_size; i++)
        arr[i] = rand() % 10000000;

    sem_t sem;
    sem_init(&sem, 0, num_threads);

    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    task_t *tasks = malloc(num_threads * sizeof(task_t));
    if (!threads || !tasks) { perror("malloc threads"); return 1; }

    size_t chunk = array_size / num_threads;

    for (int i = 0; i < num_threads; i++) {
        tasks[i].arr = arr;
        tasks[i].start = i * chunk;
        tasks[i].end = (i == num_threads - 1) ? array_size : (i + 1) * chunk;
        tasks[i].sem = &sem;

        if (pthread_create(&threads[i], NULL, worker, &tasks[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    size_t step = chunk;
    while (step < array_size) {
        for (size_t i = 0; i + step < array_size; i += 2 * step) {
            size_t mid = i + step;
            size_t right = (i + 2 * step < array_size) ? i + 2 * step : array_size;
            timsort_merge_blocks(arr, i, mid, right, sizeof(int), cmp_int);
        }
        step *= 2;
    }

    clock_gettime(CLOCK_MONOTONIC, &t2);
    double elapsed = (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9;

    printf("Время: %.4f секунд\n", elapsed);

    // Проверка сортировки
    int is_sorted = 1;
    for (size_t i = 1; i < array_size; i++) {
        if (arr[i - 1] > arr[i]) {
            is_sorted = 0;
            break;
        }
    }
    if (is_sorted) printf("Массив успешно отсортирован!\n");
    else printf("ОШИБКА: Массив не отсортирован!\n");

    free(arr);
    free(threads);
    free(tasks);
    sem_destroy(&sem);
    return 0;
}