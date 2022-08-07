#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>

void sigHandlerChild(int s) {
    printf("%i -> %i: Detected signal %i\n", getppid(), getpid(), s);
    exit(0);
}

const int processes = 5;
pid_t pids[processes] = {0};

void sigHandlerParent(int s) {
    printf("\nParent: %i: Detected signal %i\n", getpid(), s);
    for (int i = 0; i < processes; i++)
    {
        if (pids[i] != 0)
        {
            kill(pids[i], SIGUSR1);
        }
    }
    signal(SIGINT, sigHandlerParent);
}

void task1() {
    printf("Task 1: %i -> %i\n\n", getpid(), getppid());
    while (true) {
        puts("looping");
        sleep(1);
        if (getppid() == 1)
        {
            return;
        }
    }
}

void task2() {
    printf("Task 2: %i -> %i\n\n", getpid(), getppid());
    while (true) {
        puts("looping");
        sleep(1);
        if (getppid() == 1)
        {
            return;
        }
    }
}

void task3() {
    printf("Task 3: %i -> %i\n\n", getpid(), getppid());
    while (true) {
        puts("looping");
        sleep(1);
        if (getppid() == 1)
        {
            return;
        }
    }
}

void task4() {
    printf("Task 4: %i -> %i\n\n", getpid(), getppid());
    while (true) {
        puts("looping");
        sleep(1);
        if (getppid() == 1)
        {
            return;
        }
    }
}

void task5() {
    printf("Task 5: %i -> %i\n\n", getpid(), getppid());
    while (true) {
        puts("looping");
        sleep(1);
        if (getppid() == 1)
        {
            return;
        }
    }
}

int main() {

    void (*taskList[processes])() = {task1, task2, task3, task4, task5};

    struct sigaction sigHandler;
    sigHandler.sa_handler = sigHandlerChild;
    sigemptyset(&sigHandler.sa_mask);
    sigHandler.sa_flags = 0;
    
    sigset_t set;
    sigaddset(&set, SIGINT);

    for (int i = 0; i < processes; i++)
    {
        pids[i] = fork();
        if (pids[i] == 0) {
            sigaction(SIGUSR1, &sigHandler, NULL);
            sigprocmask(SIG_BLOCK, &set, NULL);
            taskList[i]();
            return 0; // if a child process somehow exits early, it will just terminate
        } else {
            printf("Parent: %i, %i\n", getpid(), pids[i]);
        }
    }

    sigHandler.sa_handler = sigHandlerParent;
    sigaction(SIGINT, &sigHandler, NULL);

    pid_t exited[5] = {0};
    while (!exited[0] && !exited[1] && !exited[2] && !exited[3] && !exited[4])
    {
        exited[0] = waitpid(pids[0], nullptr, WNOHANG);
        exited[1] = waitpid(pids[1], nullptr, WNOHANG);
        exited[2] = waitpid(pids[2], nullptr, WNOHANG);
        exited[3] = waitpid(pids[3], nullptr, WNOHANG);
        exited[4] = waitpid(pids[4], nullptr, WNOHANG);
    }
}