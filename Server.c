#define _CRT_NONSTDC_NO_DEPRECATE
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#include <time.h>

#define PORT 8888
#define MAX_CLIENTS 2
#define MAX_CARD_COUNT 24

#define START_GAME 'S'
#define WAIT_FOR_MY_TURN 'W'
#define UPDATE 'U'
#define YOUR_TURN 'Y'
#define PICK_CARD 'P'
#define EXIT 'E'

#pragma comment(lib, "ws2_32.lib")

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
    int id;
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

ClientInfo* g_nextClient;   // 다음 차례 클라이언트
Card g_cards[MAX_CARD_COUNT];

// CPU 점유 방지
HANDLE g_readyGameEvent;    // 게임 시작 전 준비(카드 배치)
HANDLE g_playerInfoEvent;   // 플레이어 데이터 브로드캐스트 타이밍 체크를 위한 이벤트 핸들

#pragma region ShutdownServer
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
    CloseHandle(g_readyGameEvent);
    exit(0);
}

DWORD WINAPI WaitForShutdownServer(LPVOID arg)
{
    while (true)
    {
        // q 입력 감지
        if (kbhit() && _getch() == 'q')
            break;
    }

    ShutdownServer();
    return 0;
}
#pragma endregion

#pragma region Util
bool IsRecvSuccess(LPVOID arg, void* data, int size)
{
    ClientInfo* info = (ClientInfo*)arg;
    int total = 0;
    char* ptr = (char*)data;

    while (total < size)
    {
        int len = recv(info->socket, ptr + total, size - total, 0);
        if (len <= 0)
        {
            perror("recv() failed");
            return false;
        }

        total += len;
    }

	ptr = NULL;
    return true;
}

void ExitGame(LPVOID arg);

bool WaitForClientMessage(LPVOID arg, const char MESSAGE)   // 신호(메시지)를 기다린다
{
    ClientInfo* info = (ClientInfo*)arg;
    
    char msg;
    if (IsRecvSuccess(info, &msg, sizeof(msg)) == false) // 메시지 수신 실패하면 게임 종료
    {
        ExitGame(info);
        return false;
    }
    
    if (msg == EXIT)
    {
        printf("From Client(%d): EXIT\n", ntohs(info->addr.sin_port));
        ExitGame(info);
        return false;
    }
    
    if (msg != MESSAGE)
        printf("Error %c! From Client(%d): %c\n", MESSAGE, ntohs(info->addr.sin_port), msg);

    return true;
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
            send(g_clients[i]->socket, (char*)g_cards, totalSize, 0);
        }
    }
}

void BroadcastPlayerInfo()
{
    Player players[MAX_CLIENTS];
    for (int i = 0; i < g_clientCount; i++)
    {
        players[i] = g_clients[i]->player;
    }

    printf("BroadcastPlayerInfo\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        send(g_clients[i]->socket, (char*)players, sizeof(players), 0);
    }
}
#pragma endregion

#pragma region Card
void Shuffle(int* ids)  // 카드 id 섞음
{
    srand(time(NULL));
    int size = MAX_CARD_COUNT - 1;

    printf("Shuffle\n");

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
#pragma endregion

DWORD WINAPI WaitForGameStart(LPVOID arg) // 클라이언트 접속 전에 먼저 호출
{
    printf("WaitForGameStart\n\n");

    while (true)
    {
        if (g_startGame)
            continue;
        
        // 인원이 모두 모였는지 확인, 인원이 모두 모일때까지 무한대로 기다림, 해당 이벤트가 signal상태가 될때까지 대기, 서버 시작시 여기서 대기하게 됨. -> WaitForGameStart
        WaitForSingleObject(g_readyGameEvent, INFINITE);

        ResetEvent(g_readyGameEvent);   // 다시 잠금, non_signal
        GenerateCards(); // 카드 생성

        BroadcastMessage(START_GAME);
		BroadcastCards();

        // 먼저 시작할 플레이어 정하기
       	int idx = rand() % MAX_CLIENTS; // (0 ~ 1) 정수 범위
        g_clients[idx]->player.myTurn = true;
        printf("Your turn => %d\n", ntohs(g_clients[idx]->addr.sin_port));
        
        // 다음 차례 클라이언트 설정
        if (idx != 0)
            g_nextClient = g_clients[0];
        else
            g_nextClient = g_clients[1];

        g_startGame = true;
    }

    return 0;
}

DWORD WINAPI BroadcastPlayerInfoThread(LPVOID arg)
{
    while (true)
    {
        // 이벤트 대기
        WaitForSingleObject(g_playerInfoEvent, INFINITE);
        BroadcastPlayerInfo(); // 플레이어 정보 전송
        ResetEvent(g_playerInfoEvent); // 이벤트 비활성화
    }

    return 0;
}

void ExitGame(LPVOID arg)
{
    if (g_clientCount <= 0)
        return;

    ClientInfo* info = (ClientInfo*)arg;
    printf("%d: ExitGame\n\n", ntohs(info->addr.sin_port));

    g_clientCount--;

    if (g_startGame)
    {
        g_nextClient = NULL;
        g_startGame = false;
        
        ClientInfo* otherClient;
        if (info != g_clients[0])
            otherClient = g_clients[0];
        else
            otherClient = g_clients[1];

        closesocket(info->socket);
        free(info);

        ExitGame(otherClient);
        ExitThread(0);
    }
    else
    {
        char msg = EXIT;
        send(info->socket, &msg, sizeof(msg), 0);
        printf("To Client(%d): EXIT\n", ntohs(info->addr.sin_port));

        closesocket(info->socket);
        free(info);
        ExitThread(0);
    }
}

void SwitchTurn()
{
    printf("SwitchTurn");
	
    if (g_clients[0]->player.myTurn == true)
    {
        g_clients[0]->player.myTurn = false;
        g_clients[1]->player.myTurn = true;
		printf(": client2(%d)\n", ntohs(g_clients[1]->addr.sin_port));

        g_nextClient = g_clients[0];
    }
    else
    {
        g_clients[1]->player.myTurn = false;
        g_clients[0]->player.myTurn = true;
		printf(": client1(%d)\n", ntohs(g_clients[0]->addr.sin_port));

        g_nextClient = g_clients[1];
    }
}

bool WaitForCardPick(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    int count = 0;
    int card1 = -1;
    int card2 = -1;

    printf("%d: WaitForCardPick\n", ntohs(info->addr.sin_port));
    printf("======================================================================================================\n");
    
    // 카드가 짝이 아닐 때까지 계속 자기 차례이다.
    while (info->player.myTurn)
    {
        if (WaitForClientMessage(info, PICK_CARD) == false)
            return false;

        char index;
        if (IsRecvSuccess(info, &index, sizeof(index)) == false)
        {
            ExitGame(info);
            return false;
        }

        if (index < 0 || index >= MAX_CARD_COUNT)
        {
            printf("Invalid card index received: %d\n", index);
            ExitGame(info);
            return false;
        }

        printf("From Client(%d): selected card index %d (id: %d)\n", ntohs(info->addr.sin_port), index, g_cards[index].id);

        // if (g_startGame == false)
        // {
        //     printf("The other player has left the game.\n");
        //     return false;
        // }

        // 상대방 플레이어에게도 전달
        char msg = PICK_CARD;
        send(g_nextClient->socket, (char*)&msg, sizeof(msg), 0);
        send(g_nextClient->socket, (char*)&index, sizeof(index), 0);
        printf("To Client(%d): send card index %d (id: %d)\n", ntohs(g_nextClient->addr.sin_port), index, g_cards[index].id);

        if (card1 == -1)
            card1 = g_cards[index].id;
        else 
            card2 = g_cards[index].id;

        // 카드를 두 장 뽑았는지 확인
        if (++count == 2)
        {
            printf("Two Pick Card\n");
            if (card1 != card2)
            {
                SetEvent(g_playerInfoEvent);
                break;   
            }         

            info->player.score++;
            printf("+Points! => score: %d\n", info->player.score);
            SetEvent(g_playerInfoEvent); 

            count = 0;
            card1 = -1;
            card2 = -1;
        }
    }

    printf("======================================================================================================\n");
    return true;
}

void PlayGame(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("%d: PlayGame\n", ntohs(info->addr.sin_port));

    while (true)
    {
        if (WaitForClientMessage(info, WAIT_FOR_MY_TURN) == false)
            return;

        if (info->player.myTurn == false)
        {
            if (WaitForClientMessage(info, UPDATE) == false)
                return;
            
            continue;
        }

        char msg = YOUR_TURN;
        send(info->socket, &msg, sizeof(msg), 0);

        if (WaitForCardPick(info) == false)
            return;

        SwitchTurn();
    }
}

DWORD WINAPI HandleClient(LPVOID arg)
{
    ClientInfo* info = (ClientInfo*)arg;
    printf("\nConnected from %s: %d\n", inet_ntoa(info->addr.sin_addr), ntohs(info->addr.sin_port));

    char* message = "Server connection successful!";
    send(info->socket, message, strlen(message), 0);

    info->player.id = g_clientCount; // 플레이어 id 생성
    printf("player id : %d\n", info->player.id);
    
    // 인원이 모두 모였는지 확인
    if (g_clientCount == MAX_CLIENTS)
    {
        SetEvent(g_readyGameEvent);  // 잠금 해제, signal 상태로 전환
        printf("Player Cnt : 2\n");
    }

    PlayGame(info); // 게임 시작
    return 0;
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

    // 서버 셧다운 대기 스레드 생성
    HANDLE thread_id = CreateThread(NULL, 0, WaitForShutdownServer, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }

    g_readyGameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // 게임 시작 대기 스레드 생성
    thread_id = CreateThread(NULL, 0, WaitForGameStart, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }

    g_playerInfoEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // 플레이어 정보를 실시간으로 보내기 위한 이벤트
    if (g_playerInfoEvent == NULL)
    {
        perror("CreateEvent() failed");
        ShutdownServer();
    }

    thread_id = CreateThread(NULL, 0, BroadcastPlayerInfoThread, NULL, 0, NULL); // 플레이어 브로드캐스트 쓰레드 생성
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }

}

void Connect()
{
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    int client_socket;
    DWORD threadId;

    // 초기화된 플레이어 기본 정보
    Player p;

    p.id = 0;
    p.myTurn = false;
    p.score = 0;

    while (true)
    {
        client_socket = accept(g_server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET)
        {
            //perror("accept() failed");
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

        g_clients[g_clientCount++] = ci;
        CloseHandle(client_thread_id);
    }
}

int main()
{
    Init();
    Connect();
    ShutdownServer();

	return 0;
}
