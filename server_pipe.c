#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"
#define STORAGE_DIR "/tmp/storage_team7/" // 파일 저장소 디렉터리[공유]
#define MAX_FILES 10

// 파일 락 구조체 정의
typedef struct {
    char filename[256];
    pthread_rwlock_t lock;
} FileLock;

FileLock file_locks[MAX_FILES];
int file_lock_count = 0;
pthread_mutex_t file_lock_list_mutex = PTHREAD_MUTEX_INITIALIZER;

// 파일 락을 얻거나 새로 생성하는 함수
FileLock* get_file_lock(const char* filename) {
    pthread_mutex_lock(&file_lock_list_mutex);
    for (int i = 0; i < file_lock_count; ++i) {
        if (strcmp(file_locks[i].filename, filename) == 0) {
            pthread_mutex_unlock(&file_lock_list_mutex);
            return &file_locks[i];
        }
    }

    if (file_lock_count >= MAX_FILES) {
        fprintf(stderr, "[서버] 파일 락 수 초과\n");
        pthread_mutex_unlock(&file_lock_list_mutex);
        return NULL;
    }

    strcpy(file_locks[file_lock_count].filename, filename);
    pthread_rwlock_init(&file_locks[file_lock_count].lock, NULL);
    ++file_lock_count;
    pthread_mutex_unlock(&file_lock_list_mutex);

    return &file_locks[file_lock_count - 1];
}

// 클라이언트 요청 처리
void* client_handler(void* arg) {
    struct timespec start, end;
    double elapsed;
    char client_fifo[256];
    char buffer[MAX_SIZE];
    int fd;

    strcpy(client_fifo, (char*)arg);
    free(arg);

    // 클라이언트 요청 읽기
    fd = open(client_fifo, O_RDONLY);
    if (fd < 0) {
        perror("[서버] 클라이언트 FIFO 열기 실패");
        pthread_exit(NULL);
    }

    read(fd, buffer, MAX_SIZE);
    close(fd);

    // 요청 처리
    char command[10], filename[256];
    sscanf(buffer, "%s %s", command, filename);

    // 시작 시간 기록
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (strcmp(command, "UPLOAD") == 0) {
        // 업로드 요청 처리
        FileLock* file_lock = get_file_lock(filename);
        if (!file_lock) {
            perror("[서버] 파일 락 생성 실패");
            pthread_exit(NULL);
        }

        printf("[서버] 업로드 요청: '%s' 락 대기중...\n", filename);
        pthread_rwlock_wrlock(&file_lock->lock);
        printf("[서버] '%s' 업로드 시작\n", filename);

        char filepath[512];
        sprintf(filepath, "%s%s", STORAGE_DIR, filename);

        int file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd < 0) {
            perror("[서버] 파일 저장 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            pthread_exit(NULL);
        }

        fd = open(client_fifo, O_RDONLY);
        if (fd < 0) {
            close(file_fd);
            perror("[서버] 클라이언트 FIFO 열기 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            pthread_exit(NULL);
        }

        ssize_t bytes_read;
        while ((bytes_read = read(fd, buffer, MAX_SIZE)) > 0) {
            write(file_fd, buffer, bytes_read);
        }

        close(fd);
        close(file_fd);

        pthread_rwlock_unlock(&file_lock->lock);

        fd = open(client_fifo, O_WRONLY);
        write(fd, "업로드 완료", strlen("업로드 완료") + 1);
        close(fd);

        printf("[서버] 파일 '%s' 업로드 완료\n", filename);

    } else if (strcmp(command, "DOWNLOAD") == 0) {
        // 다운로드 요청 처리
        FileLock* file_lock = get_file_lock(filename);
        if (!file_lock) {
            perror("[서버] 파일 락 생성 실패");
            pthread_exit(NULL);
        }

        printf("[서버] 다운로드 요청: '%s' 락 대기중...\n", filename);
        pthread_rwlock_rdlock(&file_lock->lock);
        printf("[서버] '%s' 다운로드 시작\n", filename);

        char filepath[512];
        sprintf(filepath, "%s%s", STORAGE_DIR, filename);

        FILE* file = fopen(filepath, "rb");
        if (!file) {
            perror("[서버] 파일 열기 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            pthread_exit(NULL);
        }

        fd = open(client_fifo, O_WRONLY);
        if (fd < 0) {
            fclose(file);
            perror("[서버] 클라이언트 FIFO 열기 실패");
            pthread_rwlock_unlock(&file_lock->lock);
            pthread_exit(NULL);
        }

        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, MAX_SIZE, file)) > 0) {
            write(fd, buffer, bytes_read);
        }

        fclose(file);
        close(fd);

        pthread_rwlock_unlock(&file_lock->lock);

        printf("[서버] 파일 '%s' 다운로드 완료\n", filename);
    }
    // 종료 시간 기록 및 소요 시간 계산
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / (double)1000000000L;
    //printf("소요시간 : %.9f초\n",elapsed);
//    unlink(client_fifo);
    pthread_exit(NULL);
}

int main() {
    pthread_t thread;
    int server_fd;
    char client_fifo[256];

    // 저장소 디렉터리 생성
    if (access(STORAGE_DIR, F_OK) != 0) { // 경로가 존재하지 않으면
        if (mkdir(STORAGE_DIR, 0700) < 0) { // 디렉토리 생성
            perror("mkdir 실패");
            return 1; // 오류 발생 시 종료
        }
    }
    // 서버용 FIFO 생성
    if (access(SERVER_FIFO, F_OK) != 0) { // 파일이 없으면
        if (mkfifo(SERVER_FIFO, 0600) < 0) {
            perror("mkfifo 실패");
            return 1; // 오류 시 종료
        }
    }
    printf("[서버] 파일 공유 서비스 대기 중...\n");
    while (1) {
        server_fd = open(SERVER_FIFO, O_RDONLY);
        read(server_fd, client_fifo, sizeof(client_fifo));
        close(server_fd);

        char* fifo_path = strdup(client_fifo);
        pthread_create(&thread, NULL, client_handler, fifo_path);
        pthread_detach(thread);
    }

    // 종료 시 모든 락 해제
    pthread_mutex_lock(&file_lock_list_mutex);
    for (int i = 0; i < file_lock_count; ++i) {
        pthread_rwlock_destroy(&file_locks[i].lock);
    }
    pthread_mutex_unlock(&file_lock_list_mutex);

    unlink(SERVER_FIFO);
    return 0;
}

