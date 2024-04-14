#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

volatile int shared_data = 0; // Shared variable between threads

int increment(void* arg) {
    for (int i = 0; i < 1000000; i++) {
        shared_data++; // Non-atomic increment
    }
    return 0;
}

int decrement(void* arg) {
    for (int i = 0; i < 1000000; i++) {
        shared_data--; // Non-atomic decrement
    }
    return 0;
}

int main() {
    thrd_t thr1, thr2;

    // Create two threads that modify shared_data concurrently
    if (thrd_create(&thr1, increment, NULL) != thrd_success) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    if (thrd_create(&thr2, decrement, NULL) != thrd_success) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    // Wait for threads to finish
    thrd_join(thr1, NULL);
    thrd_join(thr2, NULL);

    printf("Final value of shared_data: %d\n", shared_data);
    return 0;
}
