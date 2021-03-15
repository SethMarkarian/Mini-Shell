/* $begin shellmain */
#include <sys/resource.h>
#include "csapp.h"
#include "LinkedList.h"
#define MAXARGS 128

/* Function prototypes */
void eval(char *cmdline);
int parseline(char *buf, char **argv);
int builtin_command(char **argv);
struct Job *getJob(int jID);
char *argvToString(char **argv);
char **locationNextPipe(char **argv);
struct Job *pidGetJob(int pID);
void updateLists();

typedef struct Job
{
	int jobID;
	int pid;
	int pgid;
	char *command;
	volatile int state;
	volatile time_t startTime;
	volatile time_t endTime;
	volatile struct rusage *r;
} job;

LinkedList *completedJobs;
LinkedList *jobs;
int numJobs = 0;
int numPipes = 0;
volatile int foregroundPID = 0;
volatile int sigCaught = 0;

void signalHandler(int sig)
{
	if (sigCaught)
	{
		sigCaught = 0;
		return;
	}
	if (killpg(foregroundPID, sig))
		fprintf(stderr, "pgid: %d, sig: %d, killpg error: %s\n", foregroundPID, sig, strerror(errno));
	sigCaught = 1;
	return;
}

void signalChildHandler(int sig)
{
	int status;
	int pid;
	struct rusage ru;
	while ((pid = wait4(-1, &status, WNOHANG, &ru)) > 0)
	{
		if (WIFSIGNALED(status))
		{
			fprintf(stderr, "Process %d terminated due to signal %s\n", pid, strsignal(WTERMSIG(status)));
			((pidGetJob(pid))->state) = 4; /* abort */
		}
		if (WIFEXITED(status))
		{
			if (status)						   /* non zero */
				((pidGetJob(pid))->state) = 3; /* error */
			else
				((pidGetJob(pid))->state) = 2; /* ok */
		}
		if (WIFSTOPPED(status))
			((pidGetJob(pid))->state) = 1; /* stopped */
		else
		{ /* terminated */
			*((pidGetJob(pid))->r) = ru;
			((pidGetJob(pid)))->endTime = time(NULL);
		}
	}

	return;
}

int main()
{
	foregroundPID = getpgid(getpid());
	printf("Parent PGID: %d\n", foregroundPID);
	if (signal(SIGCHLD, signalChildHandler) == SIG_ERR)
	{
		fprintf(stderr, "SIGCHLD signal error: %s", strerror(errno));
		exit(0);
	}
	if (signal(SIGINT, signalHandler) == SIG_ERR)
	{
		fprintf(stderr, "SIGINT signal error: %s", strerror(errno));
		exit(0);
	}
	if (signal(SIGTSTP, signalHandler) == SIG_ERR)
	{
		fprintf(stderr, "SIGTSTP signal error: %s", strerror(errno));
		exit(0);
	}
	jobs = (LinkedList *)malloc(sizeof(LinkedList));
	createLinkedList(jobs);
	completedJobs = (LinkedList *)malloc(sizeof(LinkedList));
	createLinkedList(completedJobs);
	char cmdline[MAXLINE]; /* Command line */
	while (1)
	{
		numPipes = 0;
		/* Read */
		char *prompt = "lsh";
		if (getenv("LSHPROMPT") != NULL)
			prompt = getenv("LSHPROMPT");
		printf("%s> ", prompt);
		Fgets(cmdline, MAXLINE, stdin);
		if (feof(stdin))
			exit(0);

		/* Evaluate */
		eval(cmdline);
		updateLists();
	}
}
/* $end shellmain */

/* $begin eval */
/* eval - Evaluate a command line */
void eval(char *cmdline)
{
	char *argv[MAXARGS]; /* Argument list execve() */
	char buf[MAXLINE];	 /* Holds modified command line */
	int bg;				 /* Should the job run in bg or fg? */
	pid_t pid;			 /* Process id */

	strcpy(buf, cmdline);
	bg = parseline(buf, argv);
	if (bg == 2) /* don't execute */
		return;
	if (argv[0] == NULL)
		return; /* Ignore empty lines */
	if (!builtin_command(argv))
	{
		if (numPipes == 0)
		{
			if ((pid = Fork()) == 0)
			{	/* Child runs user job */
				/*Fork Process Here */
				if (setpgid(getpid(), getpid()) != 0)
				{
					fprintf(stderr, "%d setpgid error %s\n", getpid(), strerror(errno));
					exit(1);
				}
				if (execvpe(argv[0], argv, environ) < 0)
				{
					printf("%s: Command not found.\n", argv[0]);
					exit(0);
				}
			}
			struct Job *k = (struct Job *)calloc(1, sizeof(struct Job)); // calloc not malloc so status will be 0
			k->jobID = ++numJobs;
			k->pid = pid;
			k->pgid = pid;
			char *com = (char *)malloc((1 + strlen(cmdline)) * sizeof(char));
			strcpy(com, cmdline);
			k->command = com;
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			k->r = (struct rusage *)malloc(sizeof(struct rusage));
			k->startTime = time(NULL);
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			insertInLinkedList(jobs, k);
		} /* end of no-pipe section */
		else if (numPipes > 0)
		{ /* piping!  */
			int pipefds[2 * numPipes];
			for (int i = 0; i < numPipes; i++)
			{ /* setup pipes */
				if (pipe(&pipefds[i * 2]) < 0)
				{
					fprintf(stderr, "%s pipe setup error %s\n", cmdline, strerror(errno));
					exit(1);
				}
			}
			char **pend = locationNextPipe(argv);
			*pend = NULL;
			int ctr = 0;
			char **p = argv;
			/* setup write end of first pipe */
			if ((pid = Fork()) == 0)
			{ /* child process */
				if (setpgid(getpid(), getpid()) != 0)
				{
					fprintf(stderr, "%d setpgid error %s\n", getpid(), strerror(errno));
					exit(1);
				}
				if (dup2(pipefds[(2 * ctr) + 1], STDOUT_FILENO) < 0)
				{
					fprintf(stderr, "%s dup pipe error %s\n", cmdline, strerror(errno));
					exit(1);
				}
				for (int i = 0; i < 2 * numPipes; i++)
				{
					close(pipefds[i]);
				}
				if (execvpe(p[0], p, environ) < 0)
				{
					printf("%s: Command not found.\n", p[0]);
					exit(0);
				}
			}
			struct Job *k = (struct Job *)calloc(1, sizeof(struct Job));
			k->jobID = ++numJobs;
			k->pid = pid;
			k->pgid = pid;
			k->command = argvToString(argv);
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			k->r = (struct rusage *)malloc(sizeof(struct rusage));
			k->startTime = time(NULL);
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			insertInLinkedList(jobs, k);
			ctr++;
			p = ++pend;
			pend = locationNextPipe(p);
			*pend = NULL;
			/* setup read and write ends of middle pipes */
			while (ctr < numPipes)
			{
				if ((pid = Fork()) == 0)
				{ /* child process */
					if (setpgid(getpid(), getpid()) != 0)
					{
						fprintf(stderr, "%s setpgid error %s\n", cmdline, strerror(errno));
						exit(1);
					}
					if (dup2(pipefds[(2 * ctr) - 2], STDIN_FILENO) < 0)
					{
						fprintf(stderr, "%s dup pipe error %s\n", cmdline, strerror(errno));
						exit(1);
					}
					if (dup2(pipefds[(2 * ctr) + 1], STDOUT_FILENO) < 0)
					{
						fprintf(stderr, "%s dup pipe error %s\n", cmdline, strerror(errno));
						exit(1);
					}
					for (int i = 0; i < 2 * numPipes; i++)
					{
						close(pipefds[i]);
					}
					if (execvpe(p[0], p, environ) < 0)
					{
						printf("%s: Command not found.\n", p[0]);
						exit(0);
					}
				}
				/* in parent process */
				k = (struct Job *)calloc(1, sizeof(struct Job));
				k->jobID = ++numJobs;
				k->pid = pid;
				k->pgid = pid;
				k->command = argvToString(argv);
				if (sigemptyset(&maskSignal))
				{
					fprintf(stderr, "sigemptyset error: %s", strerror(errno));
				}
				if (sigaddset(&maskSignal, SIGCHLD))
				{
					fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
				}
				if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
				{
					fprintf(stderr, "sigprocmask error: %s", strerror(errno));
				}
				k->r = (struct rusage *)malloc(sizeof(struct rusage));
				k->startTime = time(NULL);
				if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
				{
					fprintf(stderr, "sigprocmask error: %s", strerror(errno));
				}
				insertInLinkedList(jobs, k);
				ctr++;
				p = ++pend;
				pend = locationNextPipe(p);
				*pend = NULL;
			}
			/* setup read end of last pipe */
			if ((pid = Fork()) == 0)
			{ /* child process */
				if (setpgid(getpid(), getpid()) != 0)
				{
					fprintf(stderr, "%d setpgid error %s\n", getpid(), strerror(errno));
					exit(1);
				}
				if (dup2(pipefds[(2 * ctr) - 2], STDIN_FILENO) < 0)
				{
					fprintf(stderr, "%s dup pipe error %s\n", cmdline, strerror(errno));
					exit(1);
				}
				for (int i = 0; i < 2 * numPipes; i++)
				{
					close(pipefds[i]);
				}
				if (execvpe(p[0], p, environ) < 0)
				{
					printf("%s: Command not found.\n", p[0]);
					exit(0);
				}
			}
			/* in parent process */
			for (int i = 0; i < numPipes * 2; i++)
			{
				close(pipefds[i]);
			}
			k = (struct Job *)calloc(1, sizeof(struct Job));
			k->jobID = ++numJobs;
			k->pid = pid;
			k->pgid = pid;
			k->command = argvToString(argv);
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			k->r = (struct rusage *)malloc(sizeof(struct rusage));
			k->startTime = time(NULL);
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			insertInLinkedList(jobs, k);
			fprintf(stderr, "Process no: %d, read from %d\n", pid, ((2 * ctr) - 2));
		}
		/* Parent waits for foreground job to terminate */
		if (!bg)
		{
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "%s sigemptyset error: %s", argv[0], strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGINT))
			{
				fprintf(stderr, "%s SIGINT sigaddset error: %s", argv[0], strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGTSTP))
			{
				fprintf(stderr, "%s SIGTSTP sigaddset error: %s", argv[0], strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
			}
			foregroundPID = pid;
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
			}
			int status;
			int waited;
			struct rusage ru;
			if ((waited = wait4(pid, &status, 0, &ru)) < 0)
				unix_error("waitfg: waitpid error");

			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
			}
			foregroundPID = getpgid(getpid());
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
			}
			/* set state of process from wait */
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			if (WIFSIGNALED(status))
			{
				fprintf(stderr, "Process %d terminated due to signal %s\n", pid, strsignal(WTERMSIG(status)));
				((pidGetJob(pid))->state) = 4; /* abort */
			}
			if (WIFEXITED(status))
			{
				if (status)						   /* non zero */
					((pidGetJob(pid))->state) = 3; /* error */
				else
					((pidGetJob(pid))->state) = 2; /* ok */
			}
			if (WIFSTOPPED(status))
				((pidGetJob(pid))->state) = 1; /* stopped */
			else
			{ /* terminated */
				*((pidGetJob(pid))->r) = ru;
				((pidGetJob(pid)))->endTime = time(NULL);
			}
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
		}
		else
			printf("%d %s", pid, cmdline);
	}
	return;
}

/* If first arg is a builtin command, run it and return true */
int builtin_command(char **argv)
{
	if (!strcmp(argv[0], "quit")) /* quit command */
		exit(0);
	if (!strcmp(argv[0], "&")) /* Ignore singleton & */
		return 1;
	if (!strcmp(argv[0], "jobs"))
	{
		Node *curr = jobs->head;
		while (curr)
		{
			struct Job *j = (struct Job *)(curr->ptr);
			int jID = (j->jobID);
			int p = (j->pid);
			char *c = (j->command);
			char *st;
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			int s = j->state;
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			if (s == 2)
			{ /* terminated, shouldn't be here */
				curr = (curr->next);
				continue;
			}
			if (s == 1)
			{
				st = "Stopped";
			}
			else
			{
				st = "Running";
			}
			printf("[%d] %6d %8s %s\n", jID, p, st, c);
			curr = (curr->next);
		}
		return 1;
	}
	if (!strcmp(argv[0], "jsum"))
	{
		Node *curr = completedJobs->head;
		printf("%5s %6s %12s %12s %12s %s\n", "PID", "STATUS", "ELAPSED TIME", "MIN FAULTS", "MAJ FAULTS", "COMMAND");
		while (curr)
		{
			struct Job *j = (struct Job *)(curr->ptr);
			int p = (j->pid);
			char *c = (j->command);
			long minFault = j->r->ru_minflt;
			long majFault = j->r->ru_majflt;

			char time[12];
			time_t diff = difftime(j->startTime, j->endTime);
			strftime(time, 11, "%X", gmtime(&diff));

			char *st;
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			int s = j->state;
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			if (s == 2)
			{
				st = "OK";
			}
			if (s == 3)
			{
				st = "Error";
			}
			if (s == 4)
			{
				st = "Abort";
			}
			printf("%5d %6s %12s %12ld %12ld %s", p, st, time, minFault, majFault, c);
			curr = (curr->next);
		}
		return 1;
	}
	if (!strcmp(argv[0], "bg"))
	{
		if (atoi(argv[1]) != 0)
		{
			if (atoi(argv[1]) == getpid())
			{
				fprintf(stderr, "%s same as parent process\n", argv[1]);
				return 1;
			}
			sigset_t maskSignal;
			sigset_t prevSignal;
			if (sigemptyset(&maskSignal))
			{
				fprintf(stderr, "sigemptyset error: %s", strerror(errno));
			}
			if (sigaddset(&maskSignal, SIGCHLD))
			{
				fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
			}
			if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
			if (killpg(atoi(argv[1]), SIGCONT) != 0)
			{
				fprintf(stderr, "%s killpgid error: %s\n", argv[1], strerror(errno));
				exit(1);
			}
			pidGetJob(atoi(argv[1]))->state = 0;
			if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
			{
				fprintf(stderr, "sigprocmask error: %s", strerror(errno));
			}
		}
		else
		{
			fprintf(stderr, "%s Invalid job identifier: %s\n", argv[1], strerror(errno));
			return 1;
		}
		return 1;
	}

	if (!strcmp(argv[0], "fg"))
	{
		if (atoi(argv[1]) != 0)
		{
			if (atoi(argv[1]) == getpid())
			{
				fprintf(stderr, "%s same as parent process\n", argv[1]);
				return 1;
			}
			if (killpg(atoi(argv[1]), SIGCONT) != 0)
			{
				fprintf(stderr, "%s killpgid error: %s\n", argv[2], strerror(errno));
				exit(1);
			}
		}
		else
		{
			fprintf(stderr, "%s Invalid job identifier: %s\n", argv[2], strerror(errno));
			return 1;
		}
		/* get here if everything valid and no killpgid error */
		int status;
		int pid = atoi(argv[1]);
		struct rusage ru;
		sigset_t maskSignal;
		sigset_t prevSignal;
		if (sigemptyset(&maskSignal))
		{
			fprintf(stderr, "%s sigemptyset error: %s", argv[0], strerror(errno));
		}
		if (sigaddset(&maskSignal, SIGINT))
		{
			fprintf(stderr, "%s SIGINT sigaddset error: %s", argv[0], strerror(errno));
		}
		if (sigaddset(&maskSignal, SIGTSTP))
		{
			fprintf(stderr, "%s SIGTSTP sigaddset error: %s", argv[0], strerror(errno));
		}
		if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
		{
			fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
		}
		foregroundPID = pid;
		if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
		{
			fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
		}

		if ((pid = wait4(atoi(argv[1]), &status, 0, &ru)) < 0)
		{
			unix_error("waitfg: waitpid error");
			return 1;
		}
		if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
		{
			fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
		}
		foregroundPID = getpgid(getpid());
		if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
		{
			fprintf(stderr, "%s sigprocmask error: %s", argv[0], strerror(errno));
		}
		if (sigemptyset(&maskSignal))
		{
			fprintf(stderr, "sigemptyset error: %s", strerror(errno));
		}
		if (sigaddset(&maskSignal, SIGCHLD))
		{
			fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
		}
		if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
		{
			fprintf(stderr, "sigprocmask error: %s", strerror(errno));
		}
		if (WIFSIGNALED(status))
		{
			fprintf(stderr, "Process %d terminated due to signal %s\n", pid, strsignal(WTERMSIG(status)));
			((pidGetJob(pid))->state) = 4; /* abort */
		}
		if (WIFEXITED(status))
		{
			if (status)						   /* non zero */
				((pidGetJob(pid))->state) = 3; /* error */
			else
				((pidGetJob(pid))->state) = 2; /* ok */
		}
		if (WIFSTOPPED(status))
			((pidGetJob(pid))->state) = 1; /* stopped */
		else
		{ /* terminated */
			*((pidGetJob(pid))->r) = ru;
			((pidGetJob(pid)))->endTime = time(NULL);
		}
		if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
		{
			fprintf(stderr, "sigprocmask error: %s", strerror(errno));
		}

		return 1;
	}

	if (strchr(argv[0], '=') != NULL)
	{ /* there is '=' */

		if (strlen(strchr(argv[0], '=')) == 1)
		{ /* will edit argv[0] to remove = */
			argv[0][strlen(argv[0]) - 1] = '\0';
			if (unsetenv(argv[0]) == -1) /* error in unsetting variable */
				fprintf(stderr, "%s unsetenv error: %s\n", argv[0], strerror(errno));
			return 1; /* regardless of error, no other command should be attempted */
		}
		/* there is stuff after = */
		char *delim = strchr(argv[0], '=');
		*delim = '\0';
		delim++;
		if (setenv(argv[0], delim, 1) != 0)
		{ /* error in putenv variable */
			delim--;
			*delim = '=';
			fprintf(stderr, "%s setenv error: %s\n", argv[0], strerror(errno));
		}
		return 1; /* regardless of error, no other command should be attempted */
	}
	return 0; /* Not a builtin command */
}
/* $end eval */

/* $begin parseline */
/* parseline - Parse the command line and build the argv array */
/* return 0 if fg, return 1 if bg, return 2 if invalid command */
int parseline(char *buf, char **argv)
{
	char *delim; /* Points to first space delimiter */
	int argc;	 /* Number of args */
	int bg;		 /* Background job? */

	buf[strlen(buf) - 1] = ' ';	  /* Replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* Ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	while ((delim = strchr(buf, ' ')))
	{
		*delim = '\0'; /* separate the string */
		if (*buf == '%')
		{
			if (atoi(++buf) != 0)
			{
				if (getJob(atoi(buf)))
				{
					struct Job *a = getJob(atoi(buf));
					if (sprintf(argv[argc++], "%d", a->pid) < 0)
					{
						fprintf(stderr, "Sprintf error %d\n", a->pid); /* issue with stored pid */
						exit(1);
					}
					printf("ProcessID %d\n", a->pid);
					printf("The arg: %s\n", argv[argc - 1]);
				}
				else
				{
					fprintf(stderr, "JobID does not exist %s", buf);
					return 2;
				}
			}
			else
			{
				fprintf(stderr, "Invalid  jobID %s", --buf);
				return 2;
			}
		}
		else if (*buf == '$')
		{
			if (getenv(++buf) != NULL)
				argv[argc++] = getenv(buf);
			else
			{
				argv[argc++] = "";
			}
		}
		else if (!strcmp(buf, "|"))
		{
			numPipes++;
			argv[argc++] = buf;
		}
		else if (*buf == '|')
		{
			fprintf(stderr, "Invalid pipe formatting %s\n", buf);
			return 2;
		}
		else
		{
			argv[argc++] = buf;
		}
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* Ignore spaces */
			buf++;
	}
	argv[argc] = NULL;

	if (argc == 0) /* Ignore blank line */
		return 1;

	/* Should the job run in the background? */
	if ((bg = (*argv[argc - 1] == '&')) != 0)
		argv[--argc] = NULL;

	return bg;
}
/* $end parseline */

struct Job *getJob(int jID)
{
	Node *curr = jobs->head;
	while (curr)
	{
		if (((struct Job *)(curr->ptr))->jobID == jID)
		{
			return ((struct Job *)(curr->ptr));
		}
		curr = curr->next;
	}
	return NULL;
}

char *argvToString(char **argv)
{
	char tmp[MAXLINE];
	strcpy(tmp, "");
	while (*argv)
	{
		strcat(tmp, *argv);
		strcat(tmp, " ");
		argv++;
	}
	char *ret = (char *)malloc((1 + strlen(tmp)) * sizeof(char));
	strcpy(ret, tmp);
	return ret;
}

char **locationNextPipe(char **argv)
{
	while (*argv && strcmp(*argv, "|"))
	{
		argv++;
	}
	/* found | or end of argv */
	return argv;
}

struct Job *pidGetJob(int pID)
{
	Node *curr = jobs->head;
	while (curr)
	{
		if (((struct Job *)(curr->ptr))->pid == pID)
		{
			return ((struct Job *)(curr->ptr));
		}
		curr = curr->next;
	}
	return NULL;
}

void updateLists()
{
	Node *curr = jobs->head;
	while (curr)
	{
		struct Job *j = (struct Job *)(curr->ptr);
		sigset_t maskSignal;
		sigset_t prevSignal;
		if (sigemptyset(&maskSignal))
		{
			fprintf(stderr, "sigemptyset error: %s", strerror(errno));
		}
		if (sigaddset(&maskSignal, SIGCHLD))
		{
			fprintf(stderr, "SIGCHLD sigaddset error: %s", strerror(errno));
		}
		if (sigprocmask(SIG_BLOCK, &maskSignal, &prevSignal))
		{
			fprintf(stderr, "sigprocmask error: %s", strerror(errno));
		}
		int s = j->state;
		if (sigprocmask(SIG_SETMASK, &prevSignal, &prevSignal))
		{
			fprintf(stderr, "sigprocmask error: %s", strerror(errno));
		}
		if (s == 2)
		{ /* terminated */
			removeFromLinkedList(jobs, j);
			insertInLinkedList(completedJobs, j);
		}
		curr = curr->next;
	}
	return;
}
