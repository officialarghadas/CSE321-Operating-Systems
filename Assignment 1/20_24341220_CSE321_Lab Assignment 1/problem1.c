#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int n, s;                  // n = fibonacci term, s = number of searches
int *fib_sequence = NULL;  // dynamically allocated Fibonacci array
int *search_indices = NULL; // the indices user wants to search

// Thread 1: Compute Fibonacci sequence up to n-th term
void* generate_fibonacci(void *arg) {
    fib_sequence = (int*)malloc((n + 1) * sizeof(int));

    if (n >= 0) fib_sequence[0] = 0;
    if (n >= 1) fib_sequence[1] = 1;

    for (int i = 2; i <= n; i++) {
        fib_sequence[i] = fib_sequence[i-1] + fib_sequence[i-2];
    }

    pthread_exit(NULL);
}

// Thread 2: Search values in Fibonacci sequence
void* search_fibonacci(void *arg) {
    for (int i = 0; i < s; i++) {
        int idx = search_indices[i];

        if (idx >= 0 && idx <= n) {
            printf("result of search #%d = %d\n", i + 1, fib_sequence[idx]);
        } else {
            printf("result of search #%d = -1\n", i + 1);
        }
    }

    pthread_exit(NULL);
}

int main() {
    pthread_t t1, t2;

    printf("Enter the term of fibonacci sequence:\n");
    scanf("%d", &n);

    if (n < 0 || n > 40) {
        printf("Invalid input! n must be between 0 and 40.\n");
        return 0;
    }

    // Create thread 1 (Fibonacci generator)
    pthread_create(&t1, NULL, generate_fibonacci, NULL);
    pthread_join(t1, NULL);

    // Print Fibonacci array
    for (int i = 0; i <= n; i++) {
        printf("a[%d] = %d\n", i, fib_sequence[i]);
    }

    // Search part
    printf("How many numbers you are willing to search?:\n");
    scanf("%d", &s);

    search_indices = (int*)malloc(s * sizeof(int));

    for (int i = 0; i < s; i++) {
        printf("Enter search %d:\n", i + 1);
        scanf("%d", &search_indices[i]);
    }

    // Create thread 2 (searcher)
    pthread_create(&t2, NULL, search_fibonacci, NULL);
    pthread_join(t2, NULL);

    free(fib_sequence);
    free(search_indices);

    return 0;
}
