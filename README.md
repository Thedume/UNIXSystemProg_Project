# UNIXSystemProg_Project
## 파일 공유 서비스

UNIX System Programming 텀프로젝트

이 프로젝트는 파일 공유 서비스를 구현한 시스템입니다. 서버-클라이언트 간 통신 방식으로 다음 세 가지 버전을 지원합니다:

1. 공유 메모리 버전 (Shared Memory)
2. 파이프 버전 (Pipe)
3. 메시지 큐 버전 (Message Queue)
---
  

## 프로젝트 구조

### 공유 메모리 버전
- server_shm.c: 서버 코드
- client_shm.c: 클라이언트 코드

### 파이프 버전
- server_pipe.c: 서버 코드
- client_pipe.c: 클라이언트 코드

### 메시지 큐 버전
- server_mp.c: 서버 코드
- client_mp.c: 클라이언트 코드


Makefile: 빌드 자동화를 위한 파일

----------------------------------------  
## 빌드 및 실행

1. 빌드 방법
이 프로젝트는 Makefile을 사용하여 빌드를 자동화합니다.

- 모든 버전 빌드:  
  make

- 특정 버전 빌드:  
    - 공유 메모리 버전:  
    make shm_version

    - 파이프 버전:  
    make pipe_version

    - 메시지 큐 버전:  
    make mp_version

- 빌드 파일 정리:  
  make clean


2. 실행 방법

\*파일 명에 사용되는 파일은 현재 디렉토리에 있는 파일만 사용 해야한다.
\*상대 경로와 절대 경로를 사용해선 안된다.
	ex) ./a.txt -> (x) / a.txt -> (o)

\*C 파일 테스트시 사용할 파일 생성 명령어 : dd if=/dev/zero of=(파일명).txt bs=1M count=400

공유 메모리 버전
- 서버 실행:
  ./server_shm
- 클라이언트 실행:
  ./client_shm <UPLOAD/DOWNLOAD> <파일명>
  예: 1. ./client_shm 
      2. UPLOAD example.txt

파이프 버전
- 서버 실행:
  ./server_pipe
- 클라이언트 실행:
  ./client_pipe
  클라이언트 실행 후 명령어 입력:
  업로드: UPLOAD <파일명>
  다운로드: DOWNLOAD <파일명>

메시지 큐 버전
- 서버 실행:
  ./server_mp
- 클라이언트 실행:
  ./client_mp <UPLOAD/DOWNLOAD> <파일명>
  예: 1. ./client_mp 
      2. DOWNLOAD example.txt

----------------------------------------
주요 특징
- 멀티스레딩 지원: 서버는 클라이언트 요청을 처리하기 위해 pthread를 사용하여 병렬 처리.
- 락 메커니즘: 파일의 동시 접근을 방지하기 위해 pthread_rwlock 사용.
- 플랫폼 독립적 설계: POSIX API를 사용하여 다양한 유닉스 기반 시스템에서 동작 가능.

----------------------------------------
필요 조건
- C 컴파일러 (gcc)
- POSIX 환경 (Linux/Mac)

----------------------------------------
문제 해결
1. 빌드 에러 발생 시:
   - gcc가 설치되었는지 확인합니다.
   - 라이브러리 의존성 확인 (-lpthread).

2. 서버-클라이언트 연결 문제:
   - 공유 자원 (FIFO, Message Queue, Shared Memory)이 제대로 생성되었는지 확인합니다.
   - 적절한 권한이 설정되었는지 확인합니다.

----------------------------------------
디렉터리 구조
server_shm.c
client_shm.c
server_pipe.c
client_pipe.c
server_mp.c
client_mp.c
Makefile
README.txt
