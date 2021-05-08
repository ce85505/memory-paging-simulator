#include "layout.h"

unsigned long s_total_access_time, s_access_count, s_finishtime, s_faults;
unsigned long s_access_times[NUM_PCBS], s_access_counts[NUM_PCBS];
int logfi;
int last_ran;
static int maxchildren = 100;
struct shm_layout *str;
int counter, shm;
int msgid, msgid1;
unsigned int bv = 0;
int lines = 0;
unsigned int next_s, next_ns;
int exitf = 0, stop;
int last_printed;
struct queue qs;

struct stat_object {
	unsigned long s_total;
	unsigned long n_total;
	int id;
	int addr;
	int dirty;
};

void bv_delete_pid (unsigned int *bv, unsigned int pid)
{
	(*bv) &= ~(1 << pid);
}

void bv_add_pid (unsigned int *bv, unsigned int pid)
{
	(*bv) |= (1 << pid);
}

int bv_check_pid (unsigned int bv, unsigned int pid)
{
	return (bv & (1 << pid));
}

int bv_get_pid (unsigned int bv)
{
	int i;
	for (i = 0; i < NUM_PCBS; i++)
		if (!bv_check_pid(bv, i))
			return i;
	return -1;
}
int bv_count (unsigned int bv)
{
	int i, total = 0;
	for (i = 0; i < NUM_PCBS; i++)
		if (bv_check_pid(bv, i))
			total++;
	return total;
}

struct node {
	struct stat_object level;
	struct node *next;
};
struct queue {
	struct node *head, *tail;
};
int pop (struct queue *q, struct stat_object *level)
{
	struct node *tmp;
	if (q->head == NULL)
		return -1;
	
	level->s_total = q->head->level.s_total;
	level->n_total = q->head->level.n_total;
	level->id = q->head->level.id;
	level->addr = q->head->level.addr;
	level->dirty = q->head->level.dirty;
	tmp = q->head;
	if (q->head == q->tail)
		q->head = q->tail = NULL;
	else
		q->head = q->head->next;
	
	free(tmp);
	return 0;
}
int push (struct queue *q, struct stat_object level)
{
	struct node *nw;
	if (!(nw = (struct node *)calloc(sizeof(struct node), 1))) {
		perror("calloc");
		return -1;
	}
	nw->level.s_total = level.s_total;
	nw->level.n_total = level.n_total;
	nw->level.id = level.id;
	nw->level.addr = level.addr;
	nw->level.dirty = level.dirty;
	nw->next = NULL;

	if (!q->tail && !q->head) {
		q->tail = q->head = nw;
		return 0;
	}
	q->tail->next = nw;
	q->tail = nw;
	
	return 0;
}
int qcount (struct queue *q)
{
	struct node *ptr;
	int c = 0;
	if (!q || !q->head)
		return -1;
	for (ptr = q->head; ptr; ptr = ptr->next)
		++c;
	return c;
}

void write_log (const char *str)
{
	if (++lines > 1000000) {
		fprintf(stderr, "not written: %s", str);
		return;
	}
	int lenb = (int)strlen(str);
	if (lenb <= 0)
		return;
		
	if ((int)write(logfi, str, lenb) != lenb) {
		perror("write log");
		return;
	}
	
	write(STDOUT_FILENO, str, lenb);
}

void make_end_stats (void)
{
	char buf5[256];
	unsigned long coef = (unsigned long)(s_access_count ? s_access_count : 1);		
	float fts = (float)s_finishtime / 1000000000.f;
	fts = (fts == 0.f) ? 1.f : fts;
	
	snprintf(buf5, 256, "\n\tprogram finished\n");
	write_log(buf5);
	
	snprintf(buf5, 256, "access per second (%lu in %.3fs): \t%.3f\n", 
		s_access_count, fts, (float)s_access_count / fts);
	write_log(buf5);
	
	snprintf(buf5, 256, "faults per access: \t\t%.5f\n", (float)s_faults / (float)coef);
	write_log(buf5);
	
	snprintf(buf5, 256, "average access time: \t\t%lu us\n", (s_total_access_time / coef) / 1000);
	write_log(buf5);
}

void handler1 (int sig)
{
	make_end_stats();
	stop = 1;
	int i;
	for (i = 0; i < NUM_PCBS; i++) {
		if (str->realpids[i] > 0)
			kill(str->realpids[i], SIGUSR1);
	}
	while ((waitpid(-1, NULL, WNOHANG)) > 0) { }

	shmdt(str);
	shmctl(shm, IPC_RMID, NULL);
	
	msgctl(msgid, IPC_RMID, NULL);
	msgctl(msgid1, IPC_RMID, NULL);
	close(logfi);
	
	str = 0;
	
	exit(0);
}

void handler2 (int sig)
{
	pid_t pid;
	int st, j, i;
	
	while ((pid = waitpid(-1, &st, WNOHANG)) > 0) {
		if (!str || exitf)
			continue;
		for (j = 0; j < NUM_PCBS; j++) {
			if (str->realpids[j] != pid)
				continue;
			
			char buf5[256];
			unsigned long coef = (unsigned long)(s_access_counts[j] ? 
				s_access_counts[j] : 1);		
			snprintf(buf5, 256, "\nMaster: pid %d terminated, average access time: %lu us\n", 
				j, (s_access_times[j] / coef) / 1000);
			write_log(buf5);
				
			for (i = 0; i < 256; i++)
				if (str->frame_table[i].pid == j)
					str->frame_table[i].active = 0;

			bv_delete_pid(&bv, j);
			str->realpids[j] = 0;
		}
	}
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

void init_process (struct stat_object s, int i, unsigned long dif, int mode)
{
	int pnum = ((int)s.addr);
	char buf[256];
	
	if (pnum < 0 || pnum > 32) {
		fprintf(stderr, "invalid address\n");
		return;
	}
	
	snprintf(buf, 256, "Master: Clearing frame %d and swapping %d page %d in p%d at %u:%u\n", 
		i, mode, pnum, s.id, str->seconds, str->nanoseconds);
	s_total_access_time += dif;
	s_access_times[s.id] += dif;
	s_access_counts[s.id]++;
	s_access_count++;
	s_faults++;
	write_log(buf);
	
	str->frame_table[i].active = 1;
	str->frame_table[i].pid = s.id;
	str->frame_table[i].secondchance = 0;
	str->frame_table[i].dirty = s.dirty;
	str->page_tables[s.id].pages[pnum].active = 1;
	str->page_tables[s.id].pages[pnum].pid = i;
	
	struct message reply;
	reply.type = s.id + 1;
	snprintf(reply.text, 16, "1");
	msgsnd(msgid, &reply, sizeof(reply), 0);
}

int handle_queue ()
{
	int co = qcount(&qs);
	if (co < 0 || !qs.head)
		return 0;
	
	struct stat_object s;
	
	/* found a new element at head */
	if ((qs.head->level.s_total == 0 && qs.head->level.n_total == 0)) {
		qs.head->level.s_total = str->seconds;
		qs.head->level.n_total = str->nanoseconds;
		return 0;
	} 
	
	unsigned long d0 = ((unsigned long)str->seconds * 1000000000UL) + 
			    (unsigned long)str->nanoseconds;
	unsigned long d1 = ((unsigned long)qs.head->level.s_total * 1000000000UL) + 
			    (unsigned long)qs.head->level.n_total;
	unsigned long dif = d0 - d1;
	/* hasnt been 14ms yet (or 28 for write) */
	if (dif < (14000000 * (1 + qs.head->level.dirty))) 
		return 0;

	if (pop(&qs, &s) < 0)
		return 0;
		
	int i;
	for (i = 0; i < 256; i++) {
		if (str->frame_table[i].active) 
			continue;
		init_process(s, i, dif, 0);
		return 0;
	}

	for (i = 0; i < 256; i++) {
		if (str->frame_table[i].secondchance) {
			str->frame_table[i].secondchance = 0;
			continue;
		}
		init_process(s, i, dif, 1);

		return 0;
	}
	
	fprintf(stderr, "something went wrong...\n");
	return 0;
}

int handle_msgs ()
{
	char buf[256];
	struct message message1;
	memset(&message1, 0, sizeof(message1));
	if (msgrcv(msgid1, &message1, sizeof(message1), 0, IPC_NOWAIT) < 0)
		return 0;
		
	int fid, i1;
	sscanf(message1.text, "%d %d", &fid, &i1);
	int id = (int)(message1.type - 1);
	
	if (id < 0 || id > 18) {
		fprintf(stderr, "invalid process %d\n", id);
		return 0;
	}
	
	snprintf(buf, 256, "Master: P%d requesting %s of address %d at time %u:%u\n",
		id, (fid ? "read" : "write"), i1, str->seconds, str->nanoseconds);
	write_log(buf);
		
	int pnum = (i1 / 1000);
	int mapped_addr = str->page_tables[id].pages[pnum].active;
	int mapped_pid = str->page_tables[id].pages[pnum].pid;
		
/* oss will monitor all memory references from user processes and if the reference results in a page fault, the process will be suspended till the page has been brought in. In case of page fault, oss queues the request to the device. Each request for disk read/write takes about 14ms to be fulfilled. In case of page fault, the request is queued for the device and the process is suspended as no signal is sent on the semaphore. The request at the head of the queue is fulfilled once the clock has advanced by disk read/write time since the time the request was found at the head of the queue. */
	if (!mapped_addr || str->frame_table[mapped_pid].pid != id) {
		snprintf(buf, 256, "Master: address %d requested by %d not in a frame (pagefault)\n", i1, id);
		write_log(buf);
		
		struct stat_object stmp;
		stmp.s_total = 0;
		stmp.n_total = 0;
		stmp.id = id;
		stmp.addr = pnum;
		stmp.dirty = !fid;
		push(&qs, stmp);
		return 0;
	}
		
	snprintf(buf, 256, "Master: Address %d in frame %d, %s data to P%d at time %u:%u\n",
		i1, mapped_pid, str->frame_table[mapped_pid].dirty ? "writing" : "giving",
		id, str->seconds, str->nanoseconds);
	write_log(buf);
		
	str->frame_table[mapped_pid].secondchance = 1;

	add_times(&(str->seconds), &(str->nanoseconds), 10);
	
	s_total_access_time += 10;
	s_access_count++;
	s_access_times[id] += 10;
	s_access_counts[id]++;
	
	struct message reply;
	reply.type = id + 1;
	snprintf(reply.text, 16, "1");
	msgsnd(msgid, &reply, sizeof(reply), 0);

	return 1;
}

void print_frame_table (void)
{
	if (!str || str->seconds <= last_printed)
		return;
		
	last_printed = str->seconds;
	char buf[256];
	snprintf(buf, 256, "Current memory layout at time %u:%u is:\n"
			"           Occupied\tRef\tDirty\n", 
			str->seconds, str->nanoseconds);
	write_log(buf);
	int i;
	for (i = 0; i < 256; i++) {
		snprintf(buf, 256, "Frame %2d:\t%s\t%d\t%d\n", i, 
			(str->frame_table[i].active ? "Yes" : "No"), 
			str->frame_table[i].pid, str->frame_table[i].dirty);
		write_log(buf);
	}
}

/* oss should periodically check if all the processes are queued for device and if so, advance the clock to fulfill the request at the head. We need to do this to resolve any possible deadlock in case memory is low and all processes end up waiting. */
int check_processes (void)
{
	int i, cw = 0, ct = 0;
	for (i = 0; i < NUM_PCBS; i++) {
		if (!str->realpids[i])
			continue;
		ct++;
		if (!str->waiting[i])
			continue;
		cw++;
	}
	if (ct && (ct == cw)) {
		//fprintf(stderr, "deadlock detected, %d / %d processes waiting\n", cw, ct);
		add_times(&(str->seconds), &(str->nanoseconds), 14000000);
	}
	return 0;
}

int do_processes (void)
{
	pid_t pid;
	char buf[256];
	if (!str || counter > maxchildren)
		return 0;
		
	int sd = str->seconds - next_s;
	int nd = str->nanoseconds - next_ns;
	
	if (sd < 0 || (sd == 0 && nd < 0))
		return 0;
			
	int nextid = bv_get_pid(bv);
	if (nextid < 0) {
		add_times(&next_s, &next_ns, (rand() % 50000000) + 1000000);
		return 0;
	}
		
	pid = fork();
	if (pid == -1) {
		perror("fork");
		kill(getpid(), SIGINT);
		wait(NULL);
		shmdt(str);
		shmctl(shm, IPC_RMID, NULL);
		str = 0;
		return -1;
	}
	if (pid != 0) {
		if (!str)
			return -1;
		bv_add_pid(&bv, nextid);
		if (str->realpids[nextid]) {
			fprintf(stderr, "ERROR\n");
			kill(pid, SIGUSR1);
			return -1;
		}
	
		snprintf(buf, 256, "OSS: Generating process with PID %d %d at time %d:%d\n", 
			nextid, pid, str->seconds, str->nanoseconds);
		write_log(buf);
		add_times(&next_s, &next_ns, (rand() % 50000000) + 1000000);
		counter++;
		str->realpids[nextid] = pid;
		s_access_times[nextid] = 0;
		s_access_counts[nextid] = 0;
	} else if (pid == 0) { // child
		execl("./child", "./child", NULL);
		perror("execl");
		return -1;
	}

	return 0;
}

int main (int argc, const char **argv)
{
	key_t key, key1, key2;

	alarm(4);
		
	if (argc < 2) {
		fprintf(stderr, "usage: ./oss [mode, either 0 or 1]\n");
		return 0;
	}
	int opt, mode = 0;
	while ((opt = getopt(argc, (char *const *)argv, "m:")) != -1) {
		switch (opt) {
			case 'm':
				mode = atoi(optarg);
				break;
			default:
				fprintf(stderr, "usage: ./oss [mode, either 0 or 1]\n");
				return -1;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "usage: ./oss [mode, either 0 or 1]\n");
		return -1;
	} 
	
	srand(time(NULL));
	
	struct sigaction sa2, sa1;

	memset(&sa2, 0, sizeof(sa2));
	memset(&sa1, 0, sizeof(sa1));
	sa2.sa_handler = handler2;
	sa1.sa_handler = handler1;

	sigaction(SIGCHLD, &sa2, NULL);	
	sigaction(SIGINT, &sa1, NULL);	
	sigaction(SIGALRM, &sa1, NULL);	
		
	key = ftok("oss.c", 1235);
	key1 = ftok("oss.c", 3215);
	key2 = ftok("child.c", 3225);
		
	if ((shm = shmget(key, sizeof(struct shm_layout), 0666 | IPC_CREAT)) < 0) {
		perror("shmget");
		return -1;
	}
	
	if ((str = (struct shm_layout *)shmat(shm, NULL, 0)) == NULL) {
		perror("shmat");
		return -1;
	}

	if ((msgid = msgget(key1, 0666 | IPC_CREAT)) < 0) {
		perror("msgget");
		return -1;
	}
	if ((msgid1 = msgget(key2, 0666 | IPC_CREAT)) < 0) {
		perror("msgget");
		return -1;
	}

	if ((logfi = open("log.txt", O_RDWR|O_CREAT|O_TRUNC, 0666)) < 0) {
		perror("open");
		return -1;
	}
	
	memset(str, 0, sizeof(struct shm_layout));
	counter = 0;

	next_ns = (rand() % 50000000) + 1000000;
	next_s = 0;
	
	str->mode = mode;
		
	do {
		if (do_processes() < 0)
			break;

		if (!str || stop)
			break;
			
		handle_msgs();
		
		print_frame_table();
		
		handle_queue();
					
		add_times(&(str->seconds), &(str->nanoseconds), 100000);
		
		check_processes();
	} while (!stop && !exitf && str && (counter < maxchildren || bv));
	exitf = 1;
		
	s_finishtime = ((unsigned long)str->seconds * 1000000000UL) + 
			(unsigned long)str->nanoseconds;

	handler1(0);

	return 0;
}
