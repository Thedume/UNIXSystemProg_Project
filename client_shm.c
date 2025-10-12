#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"
#define SEM_PERMS 0600

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

typedef struct {
    char data[MAX_SIZE];
    size_t size;
} SharedData;

enum {
    SEM_WRITE = 0,
    SEM_READ = 1,
    SEM_COUNT = 2
};

void sem_wait(int semid, int sem_num) {
    struct sembuf sb = { sem_num, -1, 0 };
    if (semop(semid, &sb, 1) == -1) {
        perror("semop P failed");
        exit(1);
    }
}

void sem_signal(int semid, int sem_num) {
    struct sembuf sb = { sem_num, 1, 0 };
    if (semop(semid, &sb, 1) == -1) {
        perror("semop V failed");
        exit(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <UPLOAD/DOWNLOAD> <filename>\n", argv[0]);
        return 1;
    }
    struct timespec start, end;
    double elapsed;
    char* command = argv[1];
    char* filename = argv[2];
    int server_fd;

    

    key_t key = ftok("/tmp", getpid());

    // 공유 메모리 생성
    int shmid = shmget(key, sizeof(SharedData), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    // 세마포어 생성
    int semid = semget(key, SEM_COUNT, IPC_CREAT | SEM_PERMS);
    if (semid == -1) {
        perror("semget failed");
        exit(1);
    }

    // 세마포어 초기화
    if (errno != EEXIST) {
        union semun arg;
        arg.val = 1;
        if (semctl(semid, SEM_WRITE, SETVAL, arg) == -1) {
            perror("semctl SETVAL SEM_WRITE failed");
            exit(1);
        }
        arg.val = 0;
        if (semctl(semid, SEM_READ, SETVAL, arg) == -1) {
            perror("semctl SETVAL SEM_READ failed");
            exit(1);
        }
    }

    // 공유 메모리 연결
    SharedData* shared_data = (SharedData*)shmat(shmid, NULL, 0);
    if (shared_data == (void*)-1) {
        perror("shmat failed");
        exit(1);
    }

    // 서버에 명령어 전달
    char msg[MAX_SIZE];
    sprintf(msg, "%d %s %s", key, command, filename);
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (write(server_fd, msg, strlen(msg) + 1) == -1) {
        perror("Failed to write to server");
        goto cleanup;
    }
    close(server_fd);
    // 시작 시간 기록
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (strcmp(command, "UPLOAD") == 0) {
        FILE* file = fopen(filename, "rb");
        if (!file) {
            printf("파일 '%s' 열기 실패\n", filename);
            goto cleanup;
        }

        size_t bytes_read;
        do {
            sem_wait(semid, SEM_WRITE);
            bytes_read = fread(shared_data->data, 1, MAX_SIZE, file);
            shared_data->size = bytes_read;
            sem_signal(semid, SEM_READ);
        } while (bytes_read == MAX_SIZE);
        fclose(file);

        // 전송 완료 표시
        sem_wait(semid, SEM_WRITE);
        shared_data->size = 0;
        sem_signal(semid, SEM_READ);

        printf("업로드 완료\n");

    }
    else if (strcmp(command, "DOWNLOAD") == 0) {
        FILE* file = fopen(filename, "wb");
        if (!file) {
            printf("파일 '%s' 저장 실패\n", filename);
            goto cleanup;
        }

        do {
            sem_wait(semid, SEM_READ);
            if (shared_data->size == 0) {
                sem_signal(semid, SEM_WRITE);
                break;
            }
            fwrite(shared_data->data, 1, shared_data->size, file);
            sem_signal(semid, SEM_WRITE);
        } while (1);

        fclose(file);
        printf("다운로드 완료\n");
    }
    // 종료 시간 기록 및 소요 시간 계산
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
    printf("소요시간 : %.9f초\n", elapsed);
cleanup:
    // 리소스 정리
    shmdt(shared_data);
    shmctl(shmid, IPC_RMID, NULL);
    semctl(semid, 0, IPC_RMID);

    return 0;
}