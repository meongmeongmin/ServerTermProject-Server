#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <stdio.h>
#include <stdbool.h>
#include <windows.h>
#include <winsock2.h>

#define PORT 8888
#define MAX_CLIENTS 2
#define MAX_CARD_COUNT 24

typedef struct Card
{
    bool matched;   // 카드가 짝 맞춰져 사라졌는지 여부
    char name[20];
} Card;

typedef struct Player
{
    int score;
    bool myTurn;
} Player;

typedef struct ClientInfo
{
    SOCKET socket;
    struct sockaddr_in addr;
    Player player;
} ClientInfo;

SOCKET g_server_socket;
ClientInfo* g_clients[MAX_CLIENTS];
int g_clientCount = 0;

volatile bool g_startGame = false;  // 게임 시작 여부
Card g_cards[MAX_CARD_COUNT];

DWORD WINAPI WaitForGameStart(LPVOID arg)
{
    printf("WaitForGameStart\n");
    while (true)
    {
        Sleep(1000);

        if (g_startGame)
            continue;
        
        // 인원이 2명 모였는지 확인
        // if (g_clientCount != 2)
        // {
        //     g_startGame = false;
        //     continue;
        // }

        // Test
        if (g_clientCount <= 0)
        {
            g_startGame = false;
            continue;
        }

        // 먼저 시작할 플레이어 정하기
        // srand(time(NULL));
        // int idx = rand() % MAX_CLIENTS; // (0 ~ 1) 정수 범위
        // g_clients[idx]->player.myTurn = true;

        // Test
        g_clients[0]->player.myTurn = true;
        g_startGame = true;
    }

    ExitThread(0);
}

void Init()
{
    WSADATA wsa;
    printf("Initializing Winsock...\n");
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed. Error Code : %d\n", WSAGetLastError());
        return;
    }

    // socket
    g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_socket == INVALID_SOCKET)
    {
        printf("socket() failed : %d\n", WSAGetLastError());
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // bind
    int b = bind(g_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (b == SOCKET_ERROR)
    {
        printf("bind() failed : %d\n", WSAGetLastError());
        return;
    }

    // listen
    int r = listen(g_server_socket, MAX_CLIENTS);
    if (r == SOCKET_ERROR)
    {
        printf("listen() failed : %d\n", WSAGetLastError());
        return;
    }

    printf("Echo server running on port %d\n", PORT);

    // 게임 시작 대기 스레드 생성
    HANDLE thread_id = CreateThread(NULL, 0, WaitForGameStart, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        CloseHandle(thread_id);
        exit(1);
    }
}

void PlayGame(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("%d: PlayGame\n", ntohs(info->addr.sin_port));

    while (true)
    {
        Sleep(1000);

        if (g_startGame == false)
            break;
        
        if (info->player.myTurn == false)
            continue;

        // TODO

        printf("Test!\n");
        break;
    }
}

void ExitGame(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("%d: ExitGame\n", ntohs(info->addr.sin_port));

    g_clientCount--;
    g_startGame = false;

    closesocket(info->socket);
    free(info);
    ExitThread(0);
}

DWORD WINAPI HandleClient(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("Connected from %s: %d\n", inet_ntoa(info->addr.sin_addr), ntohs(info->addr.sin_port));

    char* msg = "Server connection successful!";
    send(info->socket, msg, strlen(msg), 0);
    
    // Wait
    while (true)
    {
        if (g_startGame)
        {
            printf("%d: Start Game!\n", ntohs(info->addr.sin_port));
            break;
        }
    }

    PlayGame(info);
    ExitGame(info);
}

void Connect()
{
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client_socket;
    DWORD threadId;

    // 초기화된 플레이어 기본 정보
    Player p;
    p.myTurn = false;
    p.score = 0;

    while (true)
    {
        client_socket = accept(g_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET)
        {
            perror("accept() failed");
            continue;
        }

        // 인원 두 명으로 제한
        if (g_clientCount >= MAX_CLIENTS)
        {
            char* msg = "Sorry! Server full";
            send(client_socket, msg, strlen(msg), 0);
            closesocket(client_socket);
            continue;
        }

        // 연결한 클라이언트 정보 등록
        ClientInfo* ci = malloc(sizeof(ClientInfo));
        ci->socket = client_socket;
        ci->addr = client_addr;
        ci->player = p;

        // 클라이언트 스레드 생성
        HANDLE client_thread_id = CreateThread(NULL, 0, HandleClient, ci, 0, &threadId);
        if (client_thread_id == NULL)
        {
            perror("CreateThread() failed");

            closesocket(client_socket);
            CloseHandle(client_thread_id);
            free(ci);
            continue;
        }

        CloseHandle(client_thread_id);
        g_clients[g_clientCount++] = ci;
    }
}

int main()
{
    Init();
    Connect();
    WSACleanup();
}