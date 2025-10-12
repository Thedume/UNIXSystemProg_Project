#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"

int main() {
    char client_fifo[256];
    char buffer[MAX_SIZE];
    int server_fd, client_fd;
    struct timespec start, end;
    double elapsed;
    sprintf(client_fifo, "/tmp/client_fifo_%d", getpid());
    mkfifo(client_fifo, 0600);

    while (1) {
        printf("[클라이언트] 명령 입력 (UPLOAD filename / DOWNLOAD filename / q 종료): ");
        fgets(buffer, MAX_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0; // 개행 문자 제거

        // 'q' 입력 시 종료 처리
        if (strcmp(buffer, "q") == 0) {
            printf("[클라이언트] 클라이언트 파이프 해제 및 종료합니다.\n");
            unlink(client_fifo);
            break;
        }

        if (strcmp(buffer, "exit") == 0) {
            printf("[클라이언트] 종료합니다.\n");
            unlink(client_fifo);
            break;
        }

        char command[10], filename[256];
        sscanf(buffer, "%s %s", command, filename);

        // 서버에 클라이언트 FIFO 경로 전달
        server_fd = open(SERVER_FIFO, O_WRONLY);
        write(server_fd, client_fifo, strlen(client_fifo) + 1);
        close(server_fd);

        // 서버에 명령 전달
        client_fd = open(client_fifo, O_WRONLY);
        write(client_fd, buffer, strlen(buffer) + 1);
        close(client_fd);

        // 시작 시간 기록
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (strcmp(command, "UPLOAD") == 0) {
            // 파일 읽어 전송
            FILE* file = fopen(filename, "rb");
            if (!file) {
                printf("[클라이언트] 파일 '%s' 열기 실패\n", filename);
                continue;
            }

            client_fd = open(client_fifo, O_WRONLY);
            while (!feof(file)) {
                size_t bytes_read = fread(buffer, 1, MAX_SIZE, file);
                if (bytes_read > 0) {
                    write(client_fd, buffer, bytes_read);
                }
            }
            fclose(file);
            close(client_fd);

            // 완료 응답 대기
            client_fd = open(client_fifo, O_RDONLY);
            read(client_fd, buffer, MAX_SIZE);
            close(client_fd);
            printf("[클라이언트] %s\n", buffer);
            clock_gettime(CLOCK_MONOTONIC, &end);
            elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
            printf("소요시간 : %.9f초\n", elapsed);

        } else if (strcmp(command, "DOWNLOAD") == 0) {
            // 서버로부터 데이터 수신
            client_fd = open(client_fifo, O_RDONLY);
            FILE* file = fopen(filename, "wb");
            if (!file) {
                printf("[클라이언트] 파일 '%s' 저장 실패\n", filename);
                close(client_fd);
                continue;
            }

            ssize_t bytes_read;
            while ((bytes_read = read(client_fd, buffer, MAX_SIZE)) > 0) {
                fwrite(buffer, 1, bytes_read, file);
            }

            fclose(file);
            close(client_fd);

            printf("[클라이언트] 파일 '%s' 다운로드 완료\n", filename);
            // 종료 시간 기록 및 소요 시간 계산
            clock_gettime(CLOCK_MONOTONIC, &end);
            elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
            printf("소요시간 : %.9f초\n", elapsed);
        }
    }
    

    unlink(client_fifo);
    return 0;
}

