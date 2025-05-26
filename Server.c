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

#pragma pack(push, 1) // 1����Ʈ ����
typedef struct Card
{
    int id;
    bool isFlip;    // ī�尡 ������ �����ΰ� (�ո��ΰ�?)
    bool isLock;    // ���� ī���ΰ�
} Card;
#pragma pack(pop)

#pragma pack(push, 1)
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

volatile bool g_startGame = false;  // ���� ���� ����
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
        // q �Է� ����
        if (kbhit() && getch() == 'q')
            break;
    }

    ShutdownServer();
    return 0;
}

void Shuffle(int* ids)  // ī�� id ����
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

void GenerateCards()    // ī�� id ����
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

void BroadcastPlayerScore()
{
    int size = sizeof(Player);
    int totalSize = size * g_clientCount;

    Player players[MAX_CLIENTS];

    for (int i = 0; i < g_clientCount; i++)
    {
        players[i] = g_clients[i]->player;
    }

    printf("Brodcast Player Score\n");

    for (int i = 0; i < g_clientCount; i++)
    {
        if (g_clients[i])
        {
            send(g_clients[i]->socket, (const char*)players, totalSize, 0);
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

        // �ο��� 2�� �𿴴��� Ȯ��
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

        // �ʱ� ����
        GenerateCards();

        char message = START_GAME;
        BroadcastMessage(message);
        BroadcastCards();

        Sleep(1000);

        // ���� ������ �÷��̾� ���ϱ�
        // srand(time(NULL));
        // int idx = rand() % MAX_CLIENTS; // (0 ~ 1) ���� ����
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

    // ���� �˴ٿ� ��� ������ ����
    HANDLE thread_id = CreateThread(NULL, 0, WaitForShutdownServer, NULL, 0, NULL);
    if (thread_id == NULL)
    {
        perror("CreateThread() failed");
        ShutdownServer();
    }

    // ���� ���� ��� ������ ����
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
    int card1Idx = -1;  // ù��°�� �� ī�� �ε���

    printf("%d: WaitForCardPick\n", ntohs(info->addr.sin_port));
    printf("======================================================================================================\n");

    // ī�带 �� �� ���� ������ ��ٸ���
    while (count < 2)
    {
        int index;  // ������ ī�� �ε���
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
        // TODO: ���õ� ī�� �ε����� ���� �÷��̾�� ����
        printf("Client: selected card index %d => %d\n", index, g_cards[index].id);

        if (card1Idx == -1)
            card1Idx = index;
        else if (g_cards[card1Idx].id == g_cards[index].id)
        {
            // ī�尡 ��ġ�ϹǷ�, ���� ����
            info->player.score++;
            // TODO: �÷��̾� ���� ������Ʈ�� ���� �÷��̾�� ����s
            BroadcastPlayerScore(); // ���, Send() �ι�

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
        // TODO: �ؿ� break�� ����� �÷��̾� �� ��ȯ
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

    // �ʱ�ȭ�� �÷��̾� �⺻ ����
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

        // �ο� �� ������ ����
        if (g_clientCount >= MAX_CLIENTS)
        {
            char* msg = "Sorry! Server full";
            send(client_socket, msg, strlen(msg), 0);
            closesocket(client_socket);
            continue;
        }

        // ������ Ŭ���̾�Ʈ ���� ���
        ClientInfo* ci = malloc(sizeof(ClientInfo));
        ci->socket = client_socket;
        ci->addr = client_addr;
        ci->player = p;

        // Ŭ���̾�Ʈ ������ ����
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