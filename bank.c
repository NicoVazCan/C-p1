#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "options.h"

#define MAX_AMOUNT 20
#define MAX_UTIL 3
#define LOCK(m0, m1) if(m0 > m1) { pthread_mutex_lock(m0); pthread_mutex_lock(m1); } else { pthread_mutex_lock(m1); pthread_mutex_lock(m0); }

struct bank
{
	int num_accounts;        // number of accounts
	int *accounts;           // balance array
};

struct args 
{
	int		thread_num;       // application defined thread #
	int		delay;			  // delay between operations
	int		iterations;       // number of operations
	int     net_total;        // total amount deposited by this thread
	struct bank *bank;        // pointer to the bank (shared with other threads)
	pthread_mutex_t *mutexv;
	pthread_cond_t *condv;
	int *noMorMoney;
};

struct thread_info 
{
	pthread_t    id;        // id returned by pthread_create()
	struct args *args;      // pointer to the arguments
};

void printAccount(struct bank bank, int account, int amount)
{
	int balance = bank.accounts[account];

	printf("·Account %d with a balance of %d before, and %d after\n",
	       account, balance, balance + amount);
}

// Threads run on this function
void *deposit(void *ptr)
{
	struct args *args =  ptr;
	int amount, account, balance;

	while(args->iterations--) 
	{
		amount  = rand() % MAX_AMOUNT;
		account = rand() % args->bank->num_accounts;

		printf("Thread %d tries to lock %d account\n\n",
			args->thread_num, account);
		pthread_mutex_lock(&args->mutexv[account]);

		printf("Thread %d depositing %d on:\n",
			args->thread_num, amount);
		printAccount(*args->bank, account, amount);
		printf("\n");

		balance = args->bank->accounts[account];
		if(args->delay) { usleep(args->delay); } // Force a context switch

		balance += amount;
		if(args->delay) { usleep(args->delay); }

		args->bank->accounts[account] = balance;
		if(args->delay) { usleep(args->delay); }

		args->net_total += amount;

		pthread_cond_broadcast(&args->condv[account]);

		pthread_mutex_unlock(&args->mutexv[account]);
	}
	return NULL;
}

void *transfer(void *ptr)
{
	struct args *args =  ptr;
	int amount, account0, account1;

	while(args->iterations--) 
	{
		account0 = rand() % args->bank->num_accounts;
		account1 = rand() % args->bank->num_accounts;

		if(account0 != account1)
		{
			printf("Thread %d tries to lock %d and %d accounts\n\n",
			args->thread_num, account0, account1);
			LOCK(&args->mutexv[account0], &args->mutexv[account1]);

			amount = args->bank->accounts[account0];
			if(amount > 0) { amount = rand() % amount; }
			if(args->delay) { usleep(args->delay); }

			printf("Thread %d transfering %d from:\n",
			       args->thread_num, amount);
			printAccount(*args->bank, account0, amount * -1);
			printf("to:\n");
			printAccount(*args->bank, account1, amount);
			printf("\n");

			args->bank->accounts[account0] -= amount;
			if(args->delay) { usleep(args->delay); }

			args->bank->accounts[account1] += amount;
			if(args->delay) { usleep(args->delay); }

			args->net_total += amount;
			if(args->delay) { usleep(args->delay); }
			
			pthread_cond_broadcast(&args->condv[account1]);

			pthread_mutex_unlock(&args->mutexv[account0]);
			pthread_mutex_unlock(&args->mutexv[account1]);
		}
	}
	return NULL;
}

void *withdrawals(void *ptr)
{
	struct args *args =  ptr;
	int amount, account;

	amount  = rand() % MAX_AMOUNT;
	account = rand() % args->bank->num_accounts;

	printf("Thread %d tries to lock %d account\n\n",
			args->thread_num, account);
	pthread_mutex_lock(&args->mutexv[account]);

	while(args->bank->accounts[account] < amount && !(*args->noMorMoney))
	{
		printf("Thread %d tried to withdraw %d from:\n",
		       args->thread_num, amount);
		printAccount(*args->bank, account, amount * -1);
		printf("but it doesn't have enough money so must wait\n\n");

		pthread_cond_wait(&args->condv[account], &args->mutexv[account]);

		printf("Thread %d has awaken\n", args->thread_num);
	}
	if(args->bank->accounts[account] >= amount)
	{
		printf("Thread %d withdraws %d from:\n",
		       args->thread_num, amount);
		printAccount(*args->bank, account, amount * -1);
		if(args->delay) { usleep(args->delay); }

		args->bank->accounts[account] -= amount;
		if(args->delay) { usleep(args->delay); }

		args->net_total = amount;
		if(args->delay) { usleep(args->delay); }
	}
	pthread_mutex_unlock(&args->mutexv[account]);

	return NULL;
}

// start opt.num_threads threads running on deposit.
struct thread_info *start_threads(struct options opt,struct bank *bank)
{
	int i;
	struct thread_info *utilThreads;
	void *(*utilidad[])(void *) = {deposit, transfer, withdrawals};
	pthread_mutex_t *mutexv = malloc(sizeof(pthread_mutex_t) * bank->num_accounts);
	pthread_cond_t *condv = malloc(sizeof(pthread_cond_t) * bank->num_accounts);
	int *pbool = malloc(sizeof(int));

	printf("Creating %d threads for deposits\n", opt.num_threads);
	printf("Creating %d threads for transferences\n", opt.num_threads);
	printf("Creating %d threads for withdrawals\n\n", opt.num_threads);
	utilThreads = malloc(sizeof(struct thread_info) * opt.num_threads * MAX_UTIL);

	if(utilThreads == NULL) 
	{
		printf("Not enough memory\n");
		exit(1);
	}

	for(i = 0; i < bank->num_accounts; i++)
	{
		pthread_mutex_init(&mutexv[i], NULL);
		pthread_cond_init(&condv[i], NULL);
	}
	*pbool = 0;

	// Create num_thread threads running swap()
	for(i = 0; i < opt.num_threads * MAX_UTIL; i++)
	{
		utilThreads[i].args = malloc(sizeof(struct args));

		utilThreads[i].args -> thread_num = i;
		utilThreads[i].args -> net_total  = 0;
		utilThreads[i].args -> bank       = bank;
		utilThreads[i].args -> delay      = opt.delay;
		utilThreads[i].args -> iterations = opt.iterations;
		utilThreads[i].args -> mutexv     = mutexv;
		utilThreads[i].args -> condv		 = condv;
		utilThreads[i].args -> noMorMoney = pbool;

		if (0 != pthread_create(&utilThreads[i].id, NULL, utilidad[i/opt.num_threads],
		                        utilThreads[i].args))
		{
			printf("Could not create thread #%d", i);
			exit(1);
		}
	}

	return utilThreads;
}

// Print the final balances of accounts and threads
void print_balances(struct bank *bank, struct thread_info *thrs, int num_threads)
{
	int total_deposits=0, bank_total=0, total_wthdrwls=0;
	printf("\nNet deposits by thread\n");

	for(int i=0; i < num_threads; i++) 
	{
		printf("%d: %d\n", i, thrs[i].args->net_total);
		total_deposits += thrs[i].args->net_total;
	}
	printf("Total: %d\n", total_deposits);

    printf("\nAccount balance\n");
	for(int i=0; i < bank->num_accounts; i++)
	{
		printf("%d: %d\n", i, bank->accounts[i]);
		bank_total += bank->accounts[i];
	}
	printf("Total: %d\n", bank_total);

	printf("\nWithdrawals by thread\n");

	for(int i=num_threads*2; i < num_threads*3; i++) 
	{
		printf("%d: %d\n", i, thrs[i].args->net_total);
		total_wthdrwls += thrs[i].args->net_total;
	}
	printf("Total: %d\n", total_wthdrwls);
}

// wait for all threads to finish, print totals, and free memory
void wait(struct options opt, struct bank *bank, struct thread_info *threads)
{
	// Wait for the threads to finish
	int i, k;

	for(i = 0; i < opt.num_threads * (MAX_UTIL-1); i++)
	{
		pthread_join(threads[i].id, NULL);
	}
	*(threads[0].args->noMorMoney) = 1;
	printf("Los threads que ingresan dinero han finalizado\n\n");
	for(k = 0; k < bank->num_accounts; k++)
	{
		pthread_cond_broadcast(&threads[0].args->condv[k]);
	}
	for(; i < opt.num_threads * MAX_UTIL; i++)
	{
		pthread_join(threads[i].id, NULL);
	}

	print_balances(bank, threads, opt.num_threads);

	for(i = 0; i < bank->num_accounts; i++)
	{
		pthread_mutex_destroy(&threads[0].args->mutexv[i]);
		pthread_cond_destroy(&threads[0].args->condv[i]);
	}

	for(i = 0; i < opt.num_threads * MAX_UTIL; i++)
	{
		free(threads[i].args);
	}

	free(threads[0].args->noMorMoney);
	free(threads[0].args->mutexv);
	free(threads[0].args->condv);
	free(threads);
	free(bank->accounts);
}

// allocate memory, and set all accounts to 0
void init_accounts(struct bank *bank, int num_accounts)
{
	bank->num_accounts = num_accounts;
	bank->accounts     = malloc(bank->num_accounts * sizeof(int));

	for(int i=0; i < bank->num_accounts; i++)
	{
		bank->accounts[i] = 0;

	}
}

int main (int argc, char **argv)
{
	struct options      opt;
	struct bank         bank;
	struct thread_info *thrs;

	srand(time(NULL));

	// Default values for the options
	opt.num_threads  = 5;
	opt.num_accounts = 10;
	opt.iterations   = 100;
	opt.delay        = 10;

	read_options(argc, argv, &opt);

	init_accounts(&bank, opt.num_accounts);

	thrs = start_threads(opt, &bank);
   wait(opt, &bank, thrs);

	return 0;
}
