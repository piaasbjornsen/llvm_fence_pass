#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int global_a = 0;
int global_b = 0;

void* thread_func(void* ptr) {
    int* alias_ptr = (int*)ptr;

    for (int i = 0; i < 100000; ++i) {
        *alias_ptr += 1;  // Modify global variable through aliased pointer
        global_b += 1;    // Direct modification of another global variable
    }
    return NULL;
}

int main() {
    pthread_t thread1, thread2;

    // Both threads modify global_a and global_b, possibly at the same time
    if (pthread_create(&thread1, NULL, thread_func, &global_a) != 0) {
        perror("Failed to create thread1");
        return 1;
    }
    if (pthread_create(&thread2, NULL, thread_func, &global_a) != 0) {
        perror("Failed to create thread2");
        return 1;
    }

    pthread_join(thread1, NULL);
    pthread_join(thread2, NULL);

    printf("Final values: global_a = %d, global_b = %d\n", global_a, global_b);
    return 0;
}
