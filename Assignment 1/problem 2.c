#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>


/* Shared table state (0 or 1 for absence/presence) */
int table_bread = 0;
int table_cheese = 0;
int table_lettuce = 0;

/* Synchronization primitives */
pthread_mutex_t table_mutex;
sem_t semA; /* signals Maker A (missing bread -> other two placed) */
sem_t semB; /* signals Maker B (missing cheese) */
sem_t semC; /* signals Maker C (missing lettuce) */
sem_t semSupplier; /* maker signals supplier when done */

/* Control */
int N = 0;              /* number of times supplier places ingredients */
int finished = 0;       /* set by supplier when done placing N times */

/* Helper to sleep a little to simulate making sandwich */
void make_delay(void) {
    sleep(1); /* simulate time to make & eat */
}

/* Maker A (has Bread) -> should pick Cheese + Lettuce */
void* makerA(void* arg) {
    while (1) {
        sem_wait(&semA);

        pthread_mutex_lock(&table_mutex);
        /* If supplier signalled final termination and table empty -> exit */
        if (finished && !table_bread && !table_cheese && !table_lettuce) {
            pthread_mutex_unlock(&table_mutex);
            break;
        }

        /* Expect Cheese + Lettuce available */
        if (table_cheese && table_lettuce) {
            printf("Maker A picks up Cheese and Lettuce\n");
            table_cheese = 0;
            table_lettuce = 0;
            printf("Maker A is making the sandwich...\n");
            pthread_mutex_unlock(&table_mutex);

            make_delay();

            printf("Maker A finished making the sandwich and eats it\n");
            printf("Maker A signals Supplier\n\n");

            /* signal supplier that table is free and supplier can place next */
            sem_post(&semSupplier);
        } else {
            /* Not expected under correct signaling, but release mutex and continue */
            pthread_mutex_unlock(&table_mutex);
        }
    }
    return NULL;
}

/* Maker B (has Cheese) -> should pick Bread + Lettuce */
void* makerB(void* arg) {
    while (1) {
        sem_wait(&semB);

        pthread_mutex_lock(&table_mutex);
        if (finished && !table_bread && !table_cheese && !table_lettuce) {
            pthread_mutex_unlock(&table_mutex);
            break;
        }

        if (table_bread && table_lettuce) {
            printf("Maker B picks up Bread and Lettuce\n");
            table_bread = 0;
            table_lettuce = 0;
            printf("Maker B is making the sandwich...\n");
            pthread_mutex_unlock(&table_mutex);

            make_delay();

            printf("Maker B finished making the sandwich and eats it\n");
            printf("Maker B signals Supplier\n\n");

            sem_post(&semSupplier);
        } else {
            pthread_mutex_unlock(&table_mutex);
        }
    }
    return NULL;
}

/* Maker C (has Lettuce) -> should pick Bread + Cheese */
void* makerC(void* arg) {
    while (1) {
        sem_wait(&semC);

        pthread_mutex_lock(&table_mutex);
        if (finished && !table_bread && !table_cheese && !table_lettuce) {
            pthread_mutex_unlock(&table_mutex);
            break;
        }

        if (table_bread && table_cheese) {
            printf("Maker C picks up Bread and Cheese\n");
            table_bread = 0;
            table_cheese = 0;
            printf("Maker C is making the sandwich...\n");
            pthread_mutex_unlock(&table_mutex);

            make_delay();

            printf("Maker C finished making the sandwich and eats it\n");
            printf("Maker C signals Supplier\n\n");

            sem_post(&semSupplier);
        } else {
            pthread_mutex_unlock(&table_mutex);
        }
    }
    return NULL;
}

/* Supplier thread: places two random ingredients N times */
void* supplier(void* arg) {
    for (int i = 0; i < N; ++i) {
        /* Randomly pick which pair to place: 0 => Bread+Cheese, 1 => Cheese+Lettuce, 2 => Bread+Lettuce */
        int choice = rand() % 3;

        pthread_mutex_lock(&table_mutex);
        if (choice == 0) {
            table_bread = 1;
            table_cheese = 1;
            printf("Supplier places: Bread and Cheese\n\n");
            /* Notify Maker C (who has Lettuce) */
            pthread_mutex_unlock(&table_mutex);
            sem_post(&semC);
        } else if (choice == 1) {
            table_cheese = 1;
            table_lettuce = 1;
            printf("Supplier places: Cheese and Lettuce\n\n");
            pthread_mutex_unlock(&table_mutex);
            sem_post(&semA); /* Maker A has Bread */
        } else { /* choice == 2 */
            table_bread = 1;
            table_lettuce = 1;
            printf("Supplier places: Bread and Lettuce\n\n");
            pthread_mutex_unlock(&table_mutex);
            sem_post(&semB); /* Maker B has Cheese */
        }

        /* Wait until a maker finishes and signals supplier that the table is free */
        sem_wait(&semSupplier);
        /* Loop continues to place next pair */
    }

    /* After finishing N placements, signal termination to makers */
    pthread_mutex_lock(&table_mutex);
    finished = 1;
    pthread_mutex_unlock(&table_mutex);

    /* Post each maker once so any still-blocked threads can wake, check 'finished' and exit */
    sem_post(&semA);
    sem_post(&semB);
    sem_post(&semC);

    return NULL;
}

int main() {
    pthread_t sup, A, B, C;

    srand((unsigned)time(NULL));

    printf("Enter number of times supplier places ingredients: ");
    if (scanf("%d", &N) != 1 || N <= 0) {
        fprintf(stderr, "Invalid input. N must be an integer > 0.\n");
        return EXIT_FAILURE;
    }
    printf("\n");

    /* Init synchronization primitives */
    pthread_mutex_init(&table_mutex, NULL);
    sem_init(&semA, 0, 0);
    sem_init(&semB, 0, 0);
    sem_init(&semC, 0, 0);
    sem_init(&semSupplier, 0, 0);

    /* Create threads */
    pthread_create(&A, NULL, makerA, NULL);
    pthread_create(&B, NULL, makerB, NULL);
    pthread_create(&C, NULL, makerC, NULL);
    pthread_create(&sup, NULL, supplier, NULL);

    /* Wait for supplier to finish, then join makers */
    pthread_join(sup, NULL);
    pthread_join(A, NULL);
    pthread_join(B, NULL);
    pthread_join(C, NULL);

    /* Cleanup */
    sem_destroy(&semA);
    sem_destroy(&semB);
    sem_destroy(&semC);
    sem_destroy(&semSupplier);
    pthread_mutex_destroy(&table_mutex);

    /*printf("All done. Supplier placed ingredients %d times.\n", N);*/
    return EXIT_SUCCESS;
}
