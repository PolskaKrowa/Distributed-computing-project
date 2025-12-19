#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

void mc_pi(int64_t samples, int32_t seed, int64_t *inside);

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <samples> <workers>\n", argv[0]);
        return 1;
    }

    int64_t total = atoll(argv[1]);
    int workers = atoi(argv[2]);

    int64_t base = total / workers;
    int64_t rem  = total % workers;

    int pipes[workers][2];
    pid_t pids[workers];

    for (int i = 0; i < workers; ++i) {
        pipe(pipes[i]);
        pid_t pid = fork();

        if (pid == 0) {
            close(pipes[i][0]);

            int64_t local_samples = base + (i < rem ? 1 : 0);
            int32_t seed = (int32_t)(time(NULL) ^ (getpid() + i));

            int64_t inside = 0;
            mc_pi(local_samples, seed, &inside);

            write(pipes[i][1], &inside, sizeof(inside));
            close(pipes[i][1]);
            _exit(0);
        }

        close(pipes[i][1]);
        pids[i] = pid;
    }

    int64_t total_inside = 0;
    for (int i = 0; i < workers; ++i) {
        int64_t v = 0;
        read(pipes[i][0], &v, sizeof(v));
        close(pipes[i][0]);
        total_inside += v;
        waitpid(pids[i], NULL, 0);
    }

    double pi = 4.0 * (double)total_inside / (double)total;
    printf("Samples: %lld\n", (long long)total);
    printf("Workers: %d\n", workers);
    printf("Pi estimate: %.12f\n", pi);

    return 0;
}