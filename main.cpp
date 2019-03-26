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
//测试多进程epoll
//g++ -o test main.cpp -lpthread

int client_epfd        = 0;       //客户端接收链接数据事件
#define MAX_EVENTS     100
#define EPOLL_TIME_OUT 200

//写独占文件锁
inline int AcquireWriteLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_WRLCK; // 加写锁
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	return fcntl(fd, F_SETLKW, &arg);
#else
	return -1;
#endif
}

//释放独占文件锁
inline int ReleaseLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_UNLCK; //  解锁
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	return fcntl(fd, F_SETLKW, &arg);
#else
	return -1;
#endif
}

//查看写锁
inline int SeeLock(int fd, int start, int len)
{
#ifndef WIN32
	struct flock arg;
	arg.l_type = F_WRLCK;
	arg.l_whence = SEEK_SET;
	arg.l_start = start;
	arg.l_len = len;
	arg.l_pid = getpid();

	if (fcntl(fd, F_GETLK, &arg) != 0) // 获取锁
	{
		return -1; // 测试失败
	}

	if (arg.l_type == F_UNLCK)
	{
		return 0; // 无锁
	}
	else if (arg.l_type == F_RDLCK)
	{
		return 1; // 读锁
	}
	else if (arg.l_type == F_WRLCK)
	{
		return 2; // 写所
	}

	return 0;
#else
	return -1;
#endif
}

//add by freeeyes
//设置为非缓冲方式（epoll）
void setnonblocking(int sock) 
{
	int opts; 
	opts=fcntl(sock, F_GETFL); 
	if(opts<0) 
	{ 
		perror("fcntl(sock，getfl)"); 
		exit(1); 
	}
}

void * deal_ter_up_tcp(void *arg)
{
	int ret;
//	fd_set fdset;
	struct timespec rqt;
	
	rqt.tv_sec = 0;
	rqt.tv_nsec = 10000000L; // 0.01秒

	printf("[deal_ter_up_tcp]deal_ter_up_tcp thread begin .................................\n");
	
	struct epoll_event events[MAX_EVENTS];
	while (1)
	{
		int nfds = epoll_wait(client_epfd, events, MAX_EVENTS, EPOLL_TIME_OUT);

		//如果事件没有发生
		if (nfds <= 0)
		{
   		continue;
   	}
		
		//处理所发生的所有事件 		
		for(int i = 0; i < nfds; i++)
		{
			if(events[i].events&EPOLLIN)
			{
				int client_socket = events[i].data.fd;
				if (client_socket < 0)
				{
					continue;
				}
				
				//接收数据
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
	//当前监控子线程个数
	int nNumChlid = 2;
	
	//检测时间间隔参数
	struct timespec tsRqt;
	
	//文件锁
	int fd_lock = 0;
	
	int nRet = 0;

	//主进程检测时间间隔（设置每隔5秒一次）
	tsRqt.tv_sec  = 5;
	tsRqt.tv_nsec = 0;
	
	//获得当前路径
	char szWorkDir[255] = {0};
  if(!getcwd(szWorkDir, 260))  
  {  
		exit(1);
  }	
	
	// 打开（创建）锁文件
	char szFileName[200] = {'\0'};
	memset(szFileName, 0, sizeof(flock));
	sprintf(szFileName, "%s/lockwatch.lk", szWorkDir);
	fd_lock = open(szFileName, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	if (fd_lock < 0)
	{
		printf("open the flock and exit, errno = %d.", errno);
		exit(1);
	}
	
	//查看当前文件锁是否已锁
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
			//测试每个子进程的锁是否还存在
			nRet = SeeLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int));
			if (nRet == -1 || nRet == 2)
			{
				continue;
			}
			//如果文件锁没有被锁，则设置文件锁，并启动子进程
			int npid = fork();
			if (npid == 0)
			{
				//上文件锁
				if(AcquireWriteLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int)) != 0)
				{
					printf("child %d AcquireWriteLock failure.\n", nChlidIndex);
					exit(1);
				}
				
				//子进程代码
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
					//设置用于读操作的文件描述符 
					ev_client.data.fd = remote_sock_tcp; 
					//设置用于注测的读操作事件 
					ev_client.events = EPOLLIN|EPOLLET|EPOLLERR|EPOLLHUP;
					//注册ev到client_epfd事件中去
					epoll_ctl(client_epfd, EPOLL_CTL_ADD, remote_sock_tcp, &ev_client);		
				}				
				
				//子进程在执行完任务后必须退出循环和释放锁 
				ReleaseLock(fd_lock, nChlidIndex * sizeof(int), sizeof(int));	        
			}
		}
		
		//printf("child count(%d) is ok.\n", nNumChlid);
		//检查间隔
		nanosleep(&tsRqt, NULL);
	}  
  
  /*
	//主进程测试代码
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
		//设置用于读操作的文件描述符 
		ev_client.data.fd = remote_sock_tcp; 
		//设置用于注测的读操作事件 
		ev_client.events = EPOLLIN|EPOLLET|EPOLLERR|EPOLLHUP;
		//注册ev到client_epfd事件中去
		epoll_ctl(client_epfd, EPOLL_CTL_ADD, remote_sock_tcp, &ev_client);		
	}	  
	*/
  
  return 0;
}