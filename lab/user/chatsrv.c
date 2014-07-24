#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define PORT 7

#define BUFFSIZE 32
#define MAXPENDING 5    // Max connection requests

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

struct List {
	envid_t member;
	struct List *next;
};

#define MAX_CHAT_COUNT 10
struct List free_list_elems[MAX_CHAT_COUNT]
__attribute__ ((aligned(PGSIZE)));

int
get_free_list_elem(struct List **elem_store)
{
	int i;
	for (i = 0; i < MAX_CHAT_COUNT; i++)
		if (free_list_elems[i].member == -1) {
			*elem_store = free_list_elems + i;
			cprintf("no %d is free\n", i);
			return 0;
		}
	return -1;
}

int 
add_mem(envid_t env)
{
	struct List *elem = NULL;
	if (get_free_list_elem(&elem) < 0)
		return -1;
	elem->member = env;
	return 0;
}

void
remove_mem(envid_t env)
{
	int i;
	for (i = 0; i < MAX_CHAT_COUNT; i++)
		if (free_list_elems[i].member == env)
			free_list_elems[i].member = -1;
}

void 
send_to_all_members(char *buf, int size)
{
	int i;
	for (i = 0; i < MAX_CHAT_COUNT; i++)
		if (free_list_elems[i].member != -1) {
			cprintf("member = %d\n", free_list_elems[i].member);
			int r;
			ipc_send(free_list_elems[i].member, size, buf, PTE_P | PTE_U);
			cprintf("send to a member\n");
		}
}

void 
recv_client_ipc()
{
    char *recv_msg;
    if (sys_page_alloc(0, (void *)UTEMP, PTE_P | PTE_U | PTE_W) < 0)
		panic("sys_page_alloc return error");
	recv_msg = (char *)UTEMP;
	envid_t who;
	int size;
	while (1) {
		size = ipc_recv(&who, recv_msg, 0);
		cprintf("message from %08x", who);
		send_to_all_members(recv_msg, size);
	}
}

void
handle_client(envid_t sendto, int sock)
{
	char buffer[BUFFSIZE];
	int received = -1;
	
	
	char *data;
	if (sys_page_alloc(0, (void *)UTEMP, PTE_P | PTE_U | PTE_W) < 0)
		panic("sys_page_alloc return error");
	data = (char *)UTEMP;
	
	while(1){
		// Receive message
		if ((received = read(sock, buffer, BUFFSIZE)) < 0)
			die("Failed to receive initial bytes from client");
		cprintf("received = %d\n", received);
		if (received < 0) {
			cprintf("read return < 0\n");
			break;
		}
		/*
		if (received == 0) {
			cprintf("read return 0\n");
			break;
		}*/
		// Send bytes and check for more incoming data in loop
		while (received > 0) {
			// Send back received data
			if (write(sock, buffer, received) != received)
				die("Failed to send bytes to client");
			memmove(data, buffer, received);
			cprintf("[%08x]: send to other chat member\n", thisenv->env_id);
			ipc_send(sendto, received, data, PTE_P | PTE_U);
			// Check for more data
			if ((received = read(sock, buffer, BUFFSIZE)) < 0)
				die("Failed to receive additional bytes from client");
		}
	}
	close(sock);
}

void 
receive_from_others(int sock)
{
	char *recv_msg;
    if (sys_page_alloc(0, (void *)UTEMP, PTE_P | PTE_U | PTE_W) < 0)
		panic("sys_page_alloc return error");
	recv_msg = (char *)UTEMP;
	envid_t who;
	int size;
	while (1) {
		size = ipc_recv(&who, recv_msg, 0);
		cprintf("[%08x]: receive from others\n", thisenv->env_id);
		if (write(sock, recv_msg, size) != size)
				die("Failed to send bytes to client");
	}
}

void
umain(int argc, char **argv)
{
	int serversock, clientsock;
	struct sockaddr_in echoserver, echoclient;
	char buffer[BUFFSIZE];
	unsigned int echolen;
	int received = 0;
	
	int i;
	for (i = 0; i < MAX_CHAT_COUNT; i++)
		free_list_elems[i].member = -1;

	// Create the TCP socket
	if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		die("Failed to create socket");
	
	cprintf("[%08x]: I am chat server\n", thisenv->env_id);
	
	cprintf("opened socket\n");

	// Construct the server sockaddr_in structure
	memset(&echoserver, 0, sizeof(echoserver));       // Clear struct
	echoserver.sin_family = AF_INET;                  // Internet/IP
	echoserver.sin_addr.s_addr = htonl(INADDR_ANY);   // IP address
	echoserver.sin_port = htons(PORT);		  // server port

	cprintf("trying to bind\n");

	// Bind the server socket
	if (bind(serversock, (struct sockaddr *) &echoserver,
		 sizeof(echoserver)) < 0) {
		die("Failed to bind the server socket");
	}

	// Listen on the server socket
	if (listen(serversock, MAXPENDING) < 0)
		die("Failed to listen on server socket");

	cprintf("bound\n");
	
	
	int recv_client_pid = 0;
	
	
	if ((recv_client_pid = fork()) < 0)
		die("Failed to fork");
	
	if (recv_client_pid == 0) {
		recv_client_ipc();
		return;
	}
	
	free_list_elems[0].member = 0;
	if (sys_page_map(0, free_list_elems, recv_client_pid, 
			free_list_elems, PTE_U | PTE_P | PTE_W) < 0)
		panic("sys_page_map return error!");

	// Run until canceled
	while (1) {
		unsigned int clientlen = sizeof(echoclient);
		// Wait for client connection
		if ((clientsock =
		     accept(serversock, (struct sockaddr *) &echoclient,
			    &clientlen)) < 0) {
			die("Failed to accept client connection");
		}
		cprintf("Client connected: %s\n", inet_ntoa(echoclient.sin_addr));
		
		int client_pid;
		cprintf("clientsock = %d\n", clientsock);
		if ((client_pid = fork()) < 0)
			panic("fork error");
			
		if (client_pid == 0) {
			cprintf("clientsock = %d\n", clientsock);
			cprintf("[%08x]: i'll handle client request\n", thisenv->env_id);
			handle_client(recv_client_pid,clientsock);
			return;
		}
		
		if ((client_pid = fork()) < 0) {
			panic("fork error");
		}
		if (client_pid == 0) {
			cprintf("[%08x]: i'll receive other request\n", thisenv->env_id);
			receive_from_others(clientsock);
			return;
		}
		add_mem(client_pid);
	}
	close(serversock);
}
