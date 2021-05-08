#include "layout.h"
struct shm_layout *str;
int msgid, msgid1;
int nextid = -1;
float weights[33];

void handler1 (int sig)
{
	shmdt(str);
	str = NULL;
	
	exit(0);
}

void add_times (unsigned int *s, unsigned int *ns, unsigned int addn)
{
	unsigned long end = (unsigned long)(*ns) + (unsigned long)addn;

	while (end >= 1000000000) {
		*s += 1;
		end -= 1000000000;
	}
	
	*ns = end;
}

int get_message ()
{
	struct message message1;
	if (msgrcv(msgid, &message1, sizeof(message1), nextid + 1, 0) < 0)
		return -1;
	
	return 0;
}

/* The second memory request scheme tries to favor certain pages over others. You should implement a scheme where each of the 32 pages of a process’ memory space has a different weight of being selected. In particular, the weight of a processes page n should be 1/n. So the first page of a process would have a weight of 1, the second page a weight of 0.5, the third page a weight of 0.3333 and so on. Your code then needs to select one of those pages based on these weights. This can be done by storing those values in an array and then, starting from the beginning, adding to each index of the array the value in the preceding index. For example, if our weights started off as [1,0.5,0.3333,0.25,...] then after doing this process we would get an array of [1, 1.5, 1.8333, 2.0833, . . .]. Then you generate a random number from 0 to the last value in the array and then travel down the array until you find a value greater than that value and that index is the page you should request.
Now you have the page of the request, but you still need the offset. Multiply that page number by 1024 and then add a random offset of from 0 to 1023 to get the actual memory address requested.
 */

int get_index (int mode)
{
	if (!mode)
		return (rand() % 32000);
		
	int boundary = rand() % (int)((weights[31] * 100000.0f));
	float lim = (float)boundary / 100000.0f;
	int i;
	for (i = 0; i < 32; i++)
		if (lim < weights[i])
			return (i * 1000) + (rand() % 1000);
	return i;
}


int main (int argc, const char **argv)
{
	int shm, i;
	key_t key, key1, key2;
	struct message reply;
	memset(&reply, 0, sizeof(reply));
	
	for (i = 0; i < 33; i++) 
		weights[i] = 1.0f / (float)(i + 1);

	for (i = 0; i < 32; i++)
		weights[i + 1] += weights[i];
	
	alarm(10);
	
	struct sigaction sa1;

	memset(&sa1, 0, sizeof(sa1));
	sa1.sa_handler = handler1;

	sigaction(SIGINT, &sa1, NULL);	
	sigaction(SIGALRM, &sa1, NULL);	

	key = ftok("oss.c", 1235);
		
	if ((shm = shmget(key, sizeof(struct shm_layout), 0666 | IPC_CREAT)) < 0) {
		perror("shmget");
		return -1;
	}
	
	if ((str = (struct shm_layout *)shmat(shm, NULL, 0)) == NULL) {
		perror("shmat");
		return -1;
	}
		
	key1 = ftok("oss.c", 3215);
	key2 = ftok("child.c", 3225);

	if ((msgid = msgget(key1, 0666 | IPC_CREAT)) < 0) {
		perror("msgget child");
		shmdt(str);
		return -1;
	}
	if ((msgid1 = msgget(key2, 0666 | IPC_CREAT)) < 0) {
		perror("msgget child");
		shmdt(str);
		return -1;
	}

	int iters1 = 0;
	do {
		for (i = 0; i < NUM_PCBS; i++) {
			if (str->realpids[i] == getpid()) {
				nextid = i;
				break;
			}
		}
		if (++iters1 > 100000000) {
			iters1 = -1;

			shmdt(str);
			str = NULL;
			break;
		}
	} while (nextid == -1);
	
	if (iters1 == -1)
		return 0;
	
	srand(time(NULL) * (1 + nextid));
	reply.type = 1 + nextid;
	
	str->waiting[nextid] = 0;
	
	int j;
	for (j = 0; j < 100; j++) {
		reply.type = 1 + nextid;
		int is_read = ((rand() % 100) < 60) ? 1 : 0;

		snprintf(reply.text, 16, "%d %d", is_read, get_index(str->mode));
		if (msgsnd(msgid1, &reply, sizeof(reply), 0) < 0) {
			perror("msgsnd");
			break;
		}
		
		str->waiting[nextid] = 1;
		
		struct message message1;
		if (msgrcv(msgid, &message1, sizeof(message1), nextid + 1, 0) < 0) {
			perror("msgrcv");
			break;
		}

		str->waiting[nextid] = 0;
	}
	
	shmdt(str);
	str = NULL;
				
	return 0;
}