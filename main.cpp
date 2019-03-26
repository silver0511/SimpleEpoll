#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>  
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

//add by freeeyes
//���Զ����epoll
//g++ -o test main.cpp -lpthread

int client_epfd        = 0;       //�ͻ��˽������������¼�
#define MAX_EVENTS     100
#define EPOLL_TIME_OUT 200

//д��ռ�ļ���
inline int AcquireWriteLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_WRLCK; // ��д��
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	return fcntl(fd, F_SETLKW, &arg);
#else
	return -1;
#endif
}

//�ͷŶ�ռ�ļ���
inline int ReleaseLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_UNLCK; //  ����
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	return fcntl(fd, F_SETLKW, &arg);
#else
	return -1;
#endif
}

//�鿴д��
inline int SeeLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_WRLCK;
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	if (fcntl(fd, F_GETLK, &arg) != 0) // ��ȡ��
	{
		return -1; // ����ʧ��
	}

	if (arg.l_type == F_UNLCK)
	{
		return 0; // ����
	}
	else if (arg.l_type == F_RDLCK)
	{
		return 1; // ����
	}
	else if (arg.l_type == F_WRLCK)
	{
		return 2; // д��
	}

	return 0;
#else
	return -1;
#endif
}

//add by freeeyes
//����Ϊ�ǻ��巽ʽ��epoll��
void setnonblocking(int sock) 
{
	int opts; 
	opts=fcntl(sock, F_GETFL); 
	if(opts<0) 
	{ 
		perror("fcntl(sock��getfl)"); 
		exit(1); 
	}
}

void * deal_ter_up_tcp(void *arg)
{
	int ret;
//	fd_set fdset;
	struct timespec rqt;
	
	rqt.tv_sec = 0;
	rqt.tv_nsec = 10000000L; // 0.01��

	printf("[deal_ter_up_tcp]deal_ter_up_tcp thread begin .................................\n");
	
	struct epoll_event events[MAX_EVENTS];
	while (1)
	{
		int nfds = epoll_wait(client_epfd, events, MAX_EVENTS, EPOLL_TIME_OUT);

		//����¼�û�з���
		if (nfds <= 0)
		{
   		continue;
   	}
		
		//�����������������¼� 		
		for(int i = 0; i < nfds; i++)
		{
			if(events[i].events&EPOLLIN)
			{
				int client_socket = events[i].data.fd;
				if (client_socket < 0)
				{
					continue;
				}
				
				//��������
				char buf_rcv[300] = {'\0'};
				int len_rcv = read(client_socket, buf_rcv, sizeof(buf_rcv));
				{
					printf("[deal_ter_up_tcp]pid=%d,client_socket=%d,len_rcv=%d.\n", getpid(), client_socket, len_rcv);
					if(len_rcv <= 0)
					{
						printf("[deal_ter_up_tcp]close client_socket=%d.\n", client_socket);
						close(client_socket);
					}	
				}
			}
		}		
	}
}

int main(int argc, char *argv[])
{
	//��ǰ������̸߳���
	int nNumChlid = 2;
	
	//���ʱ��������
	struct timespec tsRqt;
	
	//�ļ���
	int fd_lock = 0;
	
	int nRet = 0;

	//�����̼��ʱ����������ÿ��5��һ�Σ�
	tsRqt.tv_sec  = 5;
	tsRqt.tv_nsec = 0;
	
	//��õ�ǰ·��
	char szWorkDir[255] = {0};
  if(!getcwd(szWorkDir, 260))  
  {  
		exit(1);
  }	
	
	// �򿪣����������ļ�
	char szFileName[200] = {'\0'};
	memset(szFileName, 0, sizeof(flock));
	sprintf(szFileName, "%s/lockwatch.lk", szWorkDir);
	fd_lock = open(szFileName, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (fd_lock < 0)
	{
		printf("open the flock and exit, errno = %d.", errno);
		exit(1);
	}
	
	//�鿴��ǰ�ļ����Ƿ�����
	nRet = SeeLock(fd_lock, 0, sizeof(int));
	if (nRet == -1 || nRet == 2) 
	{
		printf("file is already exist!");
		exit(1);
	}	
	
	
	int local_sock_tcp  = 0;
	int remote_sock_tcp = 0;
	struct sockaddr_in client_addr, server_addr;
	socklen_t clen = sizeof(client_addr);
  if((local_sock_tcp = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
  {
  	perror("tcp socket");
  	exit(1);
  }	
  
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(10002);
  if(bind(local_sock_tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    perror("tcp bind");
    exit(1);
  }
  
  if(listen(local_sock_tcp, 100) < 0)
  {
    perror("tcp listen");
    exit(1);
  }  
  
  printf("[main]listen ok.\n");

	
	while (1)
	{
		for (int nChlidIndex = 1; nChlidIndex <= nNumChlid; nChlidIndex++)
		{
			//����ÿ���ӽ��̵����Ƿ񻹴���
			nRet = SeeLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int));
			if (nRet == -1 || nRet == 2)
			{
				continue;
			}
			//����ļ���û�б������������ļ������������ӽ���
			int npid = fork();
			if (npid == 0)
			{
				//���ļ���
				if(AcquireWriteLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int)) != 0)
				{
					printf("child %d AcquireWriteLock failure.\n", nChlidIndex);
					exit(1);
				}
				
				//�ӽ��̴���
				client_epfd = epoll_create(1024);
				
				pthread_t tid;
				pthread_attr_t attr;				

				pthread_attr_init(&attr);
				pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); 
				pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);					
				
				pthread_create(&tid, &attr, deal_ter_up_tcp, NULL);
				while(true)
				{	
					
					remote_sock_tcp = accept(local_sock_tcp, (struct sockaddr *)&client_addr, &clen);
					if(remote_sock_tcp <=  0)
					{
						continue;
					}
					
					sockaddr_in sin;
					memcpy(&sin, &client_addr, sizeof(sin));
					printf("[accept]pid=%d, IP=%s, port=%d, remote_sock_tcp=%d.\n", getpid(), inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), remote_sock_tcp);
				
					setnonblocking(remote_sock_tcp);
				
					struct epoll_event ev_client;
					//�������ڶ��������ļ������� 
					ev_client.data.fd = remote_sock_tcp; 
					//��������ע��Ķ������¼� 
					ev_client.events = EPOLLIN|EPOLLET|EPOLLERR|EPOLLHUP;
					//ע��ev��client_epfd�¼���ȥ
					epoll_ctl(client_epfd, EPOLL_CTL_ADD, remote_sock_tcp, &ev_client);		
				}				
				
				//�ӽ�����ִ�������������˳�ѭ�����ͷ��� 
				ReleaseLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int));	        
			}
		}
		
		//printf("child count(%d) is ok.\n", nNumChlid);
		//�����
		nanosleep(&tsRqt, NULL);
	}  
  
  /*
	//�����̲��Դ���
	client_epfd = epoll_create(1024);
	
	pthread_t tid;
	pthread_attr_t attr;				
	
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); 
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_PROCESS);	
	
	pthread_create(&tid, &attr, deal_ter_up_tcp, NULL);
	while(true)
	{	
		printf("accept begin.\n");
		remote_sock_tcp = accept(local_sock_tcp, (struct sockaddr *)&client_addr, &clen);
		if(remote_sock_tcp <=  0)
		{
			continue;
		}
		
		sockaddr_in sin;
		memcpy(&sin, &client_addr, sizeof(sin));
		printf("[accept]IP=%s, port=%d, remote_sock_tcp=%d.", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), remote_sock_tcp);
	
		setnonblocking(remote_sock_tcp);
	
		struct epoll_event ev_client;
		//�������ڶ��������ļ������� 
		ev_client.data.fd = remote_sock_tcp; 
		//��������ע��Ķ������¼� 
		ev_client.events = EPOLLIN|EPOLLET|EPOLLERR|EPOLLHUP;
		//ע��ev��client_epfd�¼���ȥ
		epoll_ctl(client_epfd, EPOLL_CTL_ADD, remote_sock_tcp, &ev_client);		
	}	  
	*/
  
  return 0;
}