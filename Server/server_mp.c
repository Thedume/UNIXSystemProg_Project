#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <errno.h>
#include <time.h>

#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"
#define STORAGE_DIR "/tmp/storage_team7/"
#define MAX_FILES 10

// 메시지 타입 정의
#define MSG_DATA 1
#define MSG_EOF 2
#define MSG_ERROR 3
#define MSG_SUCCESS 4

typedef struct {
    char filename[256];
    pthread_rwlock_t lock;
} FileLock;

// 메시지 큐에서 사용할 구조체
typedef struct {
    long mtype;
    char mdata[MAX_SIZE];
    size_t size;
} MsgBuffer;

FileLock file_locks[MAX_FILES];
int file_lock_count = 0;
pthread_mutex_t file_lock_list_mutex = PTHREAD_MUTEX_INITIALIZER;

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

    // 클라이언트의 메시지 큐 접근
    int msgid = msgget(key, 0666);
    if (msgid == -1) {
        perror("msgget failed");
        pthread_exit(NULL);
    }

    MsgBuffer msg;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // 클라이언트에게 준비 완료 신호 전송
    msg.mtype = MSG_SUCCESS;
    msg.size = 0;
    if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("Failed to send ready signal");
        pthread_exit(NULL);
    }

    if (strcmp(command, "UPLOAD") == 0) {
        FileLock* file_lock = get_file_lock(filename);
        if (!file_lock) {
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
            pthread_exit(NULL);
        }

        while (1) {
            if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), 0, 0) == -1) {
                perror("msgrcv failed");
                break;
            }

            if (msg.mtype == MSG_EOF) {
                break;
            }

            fwrite(msg.mdata, 1, msg.size, file);

            // 클라이언트에게 수신 확인
            msg.mtype = MSG_SUCCESS;
            msg.size = 0;
            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Failed to send acknowledgment");
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
            pthread_exit(NULL);
        }

        pthread_rwlock_rdlock(&file_lock->lock);
        printf("'%s' 다운로드 시작\n", filename);

        char filepath[512];
        sprintf(filepath, "%s%s", STORAGE_DIR, filename);
        FILE* file = fopen(filepath, "rb");

        if (!file) {
            perror("파일 열기 실패");
            msg.mtype = MSG_ERROR;
            strcpy(msg.mdata, "파일을 열 수 없습니다.");
            msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0);
            pthread_rwlock_unlock(&file_lock->lock);
            pthread_exit(NULL);
        }

        size_t bytes_read;
        while ((bytes_read = fread(msg.mdata, 1, MAX_SIZE, file)) > 0) {
            msg.mtype = MSG_DATA;
            msg.size = bytes_read;

            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("msgsnd failed");
                break;
            }

            // 클라이언트의 수신 확인 대기
            if (msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), MSG_SUCCESS, 0) == -1) {
                perror("Failed to receive acknowledgment");
                break;
            }
        }

        // EOF 메시지 전송
        msg.mtype = MSG_EOF;
        msg.size = 0;
        msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0);

        fclose(file);
        pthread_rwlock_unlock(&file_lock->lock);
        printf("'%s' 다운로드 완료\n", filename);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
    //printf("소요시간 : %.9f초\n", elapsed);

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