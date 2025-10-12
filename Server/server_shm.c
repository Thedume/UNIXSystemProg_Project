#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <time.h>
#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"
#define STORAGE_DIR "/tmp/storage_team7/"
#define MAX_FILES 10
#define SEM_PERMS 0600

typedef struct {
    char filename[256];
    pthread_rwlock_t lock;
} FileLock;

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

FileLock file_locks[MAX_FILES];
int file_lock_count = 0;
pthread_mutex_t file_lock_list_mutex = PTHREAD_MUTEX_INITIALIZER;

void sem_wait(int semid, int sem_num) {
    struct sembuf sb = { sem_num, -1, 0 };
    if (semop(semid, &sb, 1) == -1) {
        perror("semop P failed");
        pthread_exit(NULL);
    }
}

void sem_signal(int semid, int sem_num) {
    struct sembuf sb = { sem_num, 1, 0 };
    if (semop(semid, &sb, 1) == -1) {
        perror("semop V failed");
        pthread_exit(NULL);
    }
}

FileLock* get_file_lock(const char* filename) {
    pthread_mutex_lock(&file_lock_list_mutex);
    for (int i = 0; i < file_lock_count; ++i) {
        if (strcmp(file_locks[i].filename, filename) == 0) {
            pthread_mutex_unlock(&file_lock_list_mutex);
            return &file_locks[i];
        }
    }

    if (file_lock_count >= MAX_FILES) {
        fprintf(stderr, "파일 락 수 초과\n");
        pthread_mutex_unlock(&file_lock_list_mutex);
        return NULL;
    }

    strcpy(file_locks[file_lock_count].filename, filename);
    pthread_rwlock_init(&file_locks[file_lock_count].lock, NULL);
    ++file_lock_count;
    pthread_mutex_unlock(&file_lock_list_mutex);

    return &file_locks[file_lock_count - 1];
}

void* client_handler(void* arg) {
    struct timespec start, end;
    double elapsed;
    char* buffer = (char*)arg;
    key_t key;
    char command[10], filename[256];
    sscanf(buffer, "%d %s %s", &key, command, filename);
    free(buffer);

    int shmid = shmget(key, sizeof(SharedData), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        pthread_exit(NULL);
    }

    int semid = semget(key, SEM_COUNT, 0666);
    if (semid == -1) {
        perror("semget failed");
        pthread_exit(NULL);
    }

    SharedData* shared_data = (SharedData*)shmat(shmid, NULL, 0);
    if (shared_data == (void*)-1) {
        perror("shmat failed");
        pthread_exit(NULL);
    }

    // 시작 시간 기록
    clock_gettime(CLOCK_MONOTONIC, &start);
    if (strcmp(command, "UPLOAD") == 0) {
        FileLock* file_lock = get_file_lock(filename);
        if (!file_lock) {
            shmdt(shared_data);
            pthread_exit(NULL);
        }
        printf("[서버] 업로드 요청: '%s' 락 대기중...\n", filename);
        pthread_rwlock_wrlock(&file_lock->lock);
        printf("'%s' 업로드 시작\n", filename);

        char filepath[512];
        sprintf(filepath, "%s%s", STORAGE_DIR, filename);
        FILE* file = fopen(filepath, "wb");

        if (!file) {
            perror("파일 생성 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            shmdt(shared_data);
            pthread_exit(NULL);
        }

        int continue_reading = 1;
        struct sembuf sops;
        sops.sem_flg = 0;

        while (continue_reading) {
            sops.sem_num = SEM_READ;
            sops.sem_op = -1;
            if (semop(semid, &sops, 1) == -1) {
                if (errno == EINVAL) {
                    break;
                }
                perror("semop P failed");
                break;
            }

            if (shared_data->size == 0) {
                sops.sem_num = SEM_WRITE;
                sops.sem_op = 1;
                semop(semid, &sops, 1);
                continue_reading = 0;
                break;
            }

            fwrite(shared_data->data, 1, shared_data->size, file);

            sops.sem_num = SEM_WRITE;
            sops.sem_op = 1;
            if (semop(semid, &sops, 1) == -1) {
                if (errno != EINVAL) {
                    perror("semop V failed");
                }
                break;
            }
        }

        fclose(file);
        pthread_rwlock_unlock(&file_lock->lock);

        printf("'%s' 업로드 완료\n", filename);

    }
    else if (strcmp(command, "DOWNLOAD") == 0) {
        FileLock* file_lock = get_file_lock(filename);
        if (!file_lock) {
            shmdt(shared_data);
            pthread_exit(NULL);
        }

        pthread_rwlock_rdlock(&file_lock->lock);
        printf("'%s' 다운로드 시작\n", filename);

        char filepath[512];
        sprintf(filepath, "%s%s", STORAGE_DIR, filename);
        FILE* file = fopen(filepath, "rb");

        if (!file) {
            perror("파일 열기 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            shmdt(shared_data);
            pthread_exit(NULL);
        }

        size_t bytes_read;
        int continue_writing = 1;
        struct sembuf sops;
        sops.sem_flg = 0;

        while (continue_writing) {
            sops.sem_num = SEM_WRITE;
            sops.sem_op = -1;
            if (semop(semid, &sops, 1) == -1) {
                if (errno == EINVAL) {
                    break;
                }
                perror("semop P failed");
                break;
            }

            bytes_read = fread(shared_data->data, 1, MAX_SIZE, file);
            shared_data->size = bytes_read;

            sops.sem_num = SEM_READ;
            sops.sem_op = 1;
            if (semop(semid, &sops, 1) == -1) {
                if (errno != EINVAL) {
                    perror("semop V failed");
                }
                break;
            }

            if (bytes_read < MAX_SIZE) {
                continue_writing = 0;
            }
        }

        fclose(file);

        // EOF 표시
        sops.sem_num = SEM_WRITE;
        sops.sem_op = -1;
        if (semop(semid, &sops, 1) != -1) {
            shared_data->size = 0;

            sops.sem_num = SEM_READ;
            sops.sem_op = 1;
            semop(semid, &sops, 1);
        }

        pthread_rwlock_unlock(&file_lock->lock);
        printf("'%s' 다운로드 완료\n", filename);
    }
    // 종료 시간 기록 및 소요 시간 계산
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
    //printf("소요시간 : %.9f초\n",elapsed);
    shmdt(shared_data);
    pthread_exit(NULL);
}

int main() {
    pthread_t thread;
    int server_fd;
    char buffer[MAX_SIZE];

    if (access(STORAGE_DIR, F_OK) != 0) {
        if (mkdir(STORAGE_DIR, 0700) < 0) {
            perror("mkdir 실패");
            return 1;
        }
    }

    if (access(SERVER_FIFO, F_OK) != 0) {
        if (mkfifo(SERVER_FIFO, 0600) < 0) {
            perror("mkfifo 실패");
            return 1;
        }
    }

    printf("파일 공유 서비스 대기 중...\n");

    while (1) {
        server_fd = open(SERVER_FIFO, O_RDONLY);
        read(server_fd, buffer, sizeof(buffer));
        close(server_fd);

        char* thread_buffer = strdup(buffer);
        pthread_create(&thread, NULL, client_handler, thread_buffer);
        pthread_detach(thread);
    }

    return 0;
}