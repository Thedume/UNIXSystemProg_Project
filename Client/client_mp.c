#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <errno.h>
#include <time.h>

#define MAX_SIZE 1024
#define SERVER_FIFO "/tmp/server_fifo_team7"

// 메시지 타입 정의
#define MSG_DATA 1
#define MSG_EOF 2
#define MSG_ERROR 3
#define MSG_SUCCESS 4

// 메시지 큐에서 사용할 구조체
typedef struct {
    long mtype;
    char mdata[MAX_SIZE];
    size_t size;
} MsgBuffer;

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

    // 클라이언트 고유 키 생성 (PID 기반)
    key_t key = ftok("/tmp", getpid());

    // 메시지 큐 생성
    int msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        perror("msgget failed");
        exit(1);
    }

    // 서버에 명령 전달
    char msg[MAX_SIZE];
    sprintf(msg, "%d %s %s", key, command, filename);
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (write(server_fd, msg, strlen(msg) + 1) == -1) {
        perror("Failed to write to server");
        goto cleanup;
    }
    close(server_fd);

    MsgBuffer msgbuf;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // 서버 준비 응답 대기
    if (msgrcv(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), MSG_SUCCESS, 0) == -1) {
        perror("Failed to receive server ready signal");
        goto cleanup;
    }

    if (strcmp(command, "UPLOAD") == 0) {
        FILE* file = fopen(filename, "rb");
        if (!file) {
            printf("파일 '%s' 열기 실패\n", filename);
            goto cleanup;
        }

        size_t bytes_read;
        while ((bytes_read = fread(msgbuf.mdata, 1, MAX_SIZE, file)) > 0) {
            msgbuf.mtype = MSG_DATA;
            msgbuf.size = bytes_read;

            if (msgsnd(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), 0) == -1) {
                if (errno == EIDRM) {
                    printf("서버와의 연결이 끊어졌습니다.\n");
                }
                else {
                    perror("msgsnd failed");
                }
                fclose(file);
                goto cleanup;
            }
            // 서버 응답 대기
            if (msgrcv(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), MSG_SUCCESS, 0) == -1) {
                if (errno == EIDRM) {
                    printf("서버와의 연결이 끊어졌습니다.\n");
                }
                else {
                    perror("msgrcv failed");
                }
                fclose(file);
                goto cleanup;
            }
        }
        // EOF 메시지 전송
        msgbuf.mtype = MSG_EOF;
        msgbuf.size = 0;
        msgsnd(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), 0);

        fclose(file);
        printf("업로드 완료\n");
    }
    else if (strcmp(command, "DOWNLOAD") == 0) {
        FILE* file = fopen(filename, "wb");
        if (!file) {
            printf("파일 '%s' 저장 실패\n", filename);
            goto cleanup;
        }
        while (1) {
            if (msgrcv(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), 0, 0) == -1) {
                if (errno == EIDRM) {
                    printf("서버와의 연결이 끊어졌습니다.\n");
                }
                else {
                    perror("msgrcv failed");
                }
                fclose(file);
                goto cleanup;
            }
            if (msgbuf.mtype == MSG_EOF) {
                break;
            }
            else if (msgbuf.mtype == MSG_ERROR) {
                printf("서버에서 오류가 발생했습니다: %s\n", msgbuf.mdata);
                fclose(file);
                goto cleanup;
            }
            fwrite(msgbuf.mdata, 1, msgbuf.size, file);
            // 데이터 수신 확인 메시지 전송
            msgbuf.mtype = MSG_SUCCESS;
            msgbuf.size = 0;
            if (msgsnd(msgid, &msgbuf, sizeof(msgbuf) - sizeof(long), 0) == -1) {
                perror("Failed to send acknowledgment");
                fclose(file);
                goto cleanup;
            }
        }

        fclose(file);
        printf("다운로드 완료\n");
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / (double)1000000000L;
    printf("소요시간 : %.9f초\n", elapsed);

cleanup:
    // 메시지 큐 제거
    msgctl(msgid, IPC_RMID, NULL);
    return 0;
}