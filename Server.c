#define _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdbool.h>
#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#include <time.h>

#define PORT 8888
#define MAX_CLIENTS 2
#define MAX_CARD_COUNT 24

#define START_GAME 'S'
#define YOUR_TURN 'Y'

#pragma pack(push, 1) // 1바이트 정렬
typedef struct Card
{
    int id;
    bool isFlip;    // 카드가 뒤집힌 상태인가 (앞면인가?)
    bool isLock;    // 맞춘 카드인가
} Card;
#pragma pack(pop)

#pragma pack(push, 1) // 1바이트 정렬
typedef struct Player
{
    int score;
    bool myTurn;
} Player;
#pragma pack(pop)

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

void ShutdownServer()
{
    printf("Shutting down server...\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        if (g_clients[i])
        {
            closesocket(g_clients[i]->socket);
            free(g_clients[i]);
        }
    }

    closesocket(g_server_socket);
    WSACleanup();
    exit(0);
}

DWORD WINAPI WaitForShutdownServer(LPVOID arg)
{
    while (true)
    {
        // q 입력 감지
        if (kbhit() && getch() == 'q')
            break;
    }

    ShutdownServer();
    return 0;
}

void Shuffle(int* ids)  // 카드 id 섞음
{
    printf("Shuffle\n");
    int size = MAX_CARD_COUNT - 1;

    for (int i = size; i > 0; i--)
    {
        int randIndex = rand() % (i + 1);

        // Swap
        int swap = ids[i];
        ids[i] = ids[randIndex];
        ids[randIndex] = swap;
    }
}

void GenerateCards()    // 카드 id 생성
{
    printf("GenerateCards\n");

    int ids[MAX_CARD_COUNT];
    int count = MAX_CARD_COUNT / 2;

    for (int i = 0; i < count; i++) 
    { 
        ids[i * 2] = i;
        ids[i * 2 + 1] = i;
    }

    Shuffle(ids);
    
    printf("Card id: ");
    for (int i = 0; i < MAX_CARD_COUNT; i++)
    {
        g_cards[i].id = ids[i];
        g_cards[i].isFlip = false;
        g_cards[i].isLock = false;

        printf("%d, ", g_cards[i].id);
    }

    printf("\n");
}

void BroadcastMessage(char msg)
{
    printf("BroadcastMessage\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        if (g_clients[i])
            send(g_clients[i]->socket, &msg, sizeof(msg), 0);
    }
}

void BroadcastCards()
{
    int size = sizeof(Card);
    int totalSize = size * MAX_CARD_COUNT;
    printf("BroadcastCards\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        if (g_clients[i])
        {
            send(g_clients[i]->socket, (const char*)g_cards, totalSize, 0);
        }
    }
}

//feat by mundi: 점수 브로드캐스트 기능 추가 
void BroadcastPlayerScore()
{
    int size = sizeof(Player);
    int totalSize = size * g_clientCount;

    Player players[MAX_CLIENTS];

    for (int i = 0; i < g_clientCount; i++)
    {
        players[i] = g_clients[i]->player; // 각 플레이어 정보를 배열에 저장
    }

    printf("Brodcast Player Score\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        if (g_clients[i])
        {
            send(g_clients[i]->socket, (const char*)players, totalSize, 0); // 클라이언트에 점수 보냄
        }
    }
}

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

        GenerateCards();

        char message = START_GAME;
        BroadcastMessage(message);
        BroadcastCards();

        Sleep(1000);

        // 먼저 시작할 플레이어 정하기
        // srand(time(NULL));
        // int idx = rand() % MAX_CLIENTS; // (0 ~ 1) 정수 범위
        // g_clients[idx]->player.myTurn = true;

        // Test
        g_clients[0]->player.myTurn = true;
        g_startGame = true;
    }

    return 0;
}

void Init()
{
    srand(time(NULL));

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

    // 서버 셧다운 대기 스레드 생성
    HANDLE thread_id = CreateThread(NULL, 0, WaitForShutdownServer, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }

    // 게임 시작 대기 스레드 생성
    thread_id = CreateThread(NULL, 0, WaitForGameStart, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }
}

void WaitForCardPick(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    int count = 0;
    int card1Idx = -1;  // 첫번째로 고른 카드 인덱스

    printf("%d: WaitForCardPick\n", ntohs(info->addr.sin_port));
    printf("======================================================================================================\n");
    
    // 카드를 두 번 뽑을 때까지 기다린다
    while (count < 2)
    {
        int index;  // 선택한 카드 인덱스
        int len = recv(info->socket, (char*)&index, sizeof(index), 0);

        if (len != sizeof(index))
        {
            printf("data corrupted : %ld bytes expected, %d bytes received\n", sizeof(index), len);
            continue;
        }

        if (len <= 0)
        {
            perror("recv() failed");
            continue;
        }

        count++;
        // TODO: 선택된 카드 인덱스를 상대방 플레이어에게 전달
        printf("Client: selected card index %d => %d\n", index, g_cards[index].id);

        if (card1Idx == -1)
            card1Idx = index;
        else if (g_cards[card1Idx].id == g_cards[index].id)
        {
            // 카드가 일치하므로, 점수 증가
            info->player.score++;
			BroadcastPlayerScore(); // 각 플레이어에게 점수 뿌림
            printf("+Points! => score: %d\n", info->player.score);
        }
    }

    printf("======================================================================================================\n");
}

void PlayGame(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("%d: PlayGame\n", ntohs(info->addr.sin_port));

    while (true)
    {
        if (g_startGame == false)
            break;
        
        if (info->player.myTurn == false)
            continue;

        char msg = YOUR_TURN;
        send(info->socket, &msg, sizeof(msg), 0);

        WaitForCardPick(info);
        // TODO: 밑에 break를 지우고 플레이어 턴 전환
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
    ShutdownServer();

	return 0;
}
