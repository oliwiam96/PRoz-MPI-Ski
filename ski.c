#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#define MSG_SIZE 2
#define MSG_HELLO 100
#define TAG_REQ 123
#define TAG_ACK 456
#define TAG_RELEASE 789
#define Capacity 150
#define GOUPTIME 10

int clockLamport = 0;
int stop = 0;


pthread_mutex_t	mutexClock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

typedef struct queue_element
{
	int id;
	int time;
	int weight;
	struct queue_element *next;
	struct queue_element *previous;
} queue_el;

struct data
{
	int rank; // my own rank
	int size; // how many skiers
	int myWeight; // my own weight
	int * tab_ack; // did I receive ack from a skier with that id? 1/0
	queue_el *head; // pointer to a head of a queue
};


queue_el * insert(queue_el *head, queue_el *insert_element)
{
	queue_el *current = head;
	int insert_next = 0;
	int i = 0;
	int first = 0;

	while (1)
	{
		if (head == NULL)
		{
			head = insert_element;
			first = 1;
			break;
		}

		i = i + 1;
		if (current->time < insert_element->time)
		{
			if (current->next != NULL)
			{
				current = current->next;
			}
			else
			{
				insert_next = 1;
				break;
			}
		}
		else if (current->time == insert_element->time)
		{
			if (current->id < insert_element->id)
			{
				if (current->next != NULL)
				{
					current = current->next;
				}
				else
				{
					insert_next = 1;
					break;
				}
			}
			else
			{
				break;
			}
		}
		else
		{
			break;
		}
	}

	if ((i == 1) && (insert_next == 0))
	{
		head->previous = insert_element;
		insert_element->next = head;
		head = insert_element;
	}
	else if (first != 1)
	{
		if (insert_next == 1)
		{
			current->next = insert_element;
			insert_element->previous = current;
		}
		else
		{
			insert_element->previous = current->previous;
			current->previous->next = insert_element;
			current->previous = insert_element;
			insert_element->next = current;
		}
	}

	return head;

}

int queueCount(queue_el *head)
{
	int count = 0;
	queue_el *current = head;

	while (current != NULL)
	{
		count += 1;
		current = current->next;
	}

	return count;
}


queue_el * delete(queue_el *head, int id)
{
	queue_el *current = head;

	while (1)
	{
		if (current == NULL)
		{
			break;
		}

		if (current->id == id)
		{
			if (queueCount(head)) // TO TEN IF
			{
				head = NULL;
			}
			else if (current->previous == NULL)
			{
				head = current->next;
			}
			else if (current->next == NULL)
			{
				current->previous->next = NULL;
			}
			else
			{
				current->next->previous = current->previous;
				current->previous->next = current->next;
			}

			free(current);
			break;
		}
		current = current->next;
	}

	return head;
}


void print(queue_el *head)
{
	queue_el *current = head;

	while (current != NULL)
	{
		printf("id: %d time: %d;\t", current->id, current->time);
		current = current->next;
	}
	printf("\n");
}
int checkWeights(queue_el *head, int myId)
{
	queue_el *current = head;
	int sum = 0;
	while (current != NULL && current->id != myId)
	{

		sum = sum + current->weight;
		current = current->next;
	}
	return sum;
}

queue_el * new_element(int id, int time, int weight)
{
	queue_el *new = malloc(sizeof(queue_el));
	new->id = id;
	new->time = time;
	new->weight = weight;
	new->next = NULL;
	new->previous = NULL;

	return new;
}


// ten watek to 2 w naszym sprawku
void* receiveAndSendAck(void* arg)
{


	while (!stop)
	{
		MPI_Status status;
		int msg[MSG_SIZE], receivedClock, receivedWeight;
		struct data* dane = (struct data*)arg;


		MPI_Recv(msg, MSG_SIZE, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		printf("[Wątek %d - ack] otrzymał wiadomość od  %d o TAGU: %d.  [zegar z wiadomosci = %d]\n", dane->rank, status.MPI_SOURCE, status.MPI_TAG, receivedClock);

		receivedClock = msg[0];
		receivedWeight = msg[1];

		// wstaw do kolejki
		if (status.MPI_TAG == TAG_REQ)
		{
			pthread_mutex_lock(&mutexClock);

			clockLamport = (clockLamport > receivedClock) ? clockLamport : receivedClock;
			clockLamport += 1;

			printf("[Wątek %d - ack] wstawia do kolejki zgłoszenie %d. [zegar = %d]\n", dane->rank, status.MPI_SOURCE, clockLamport);
			dane->head = insert(dane->head, new_element(status.MPI_SOURCE, receivedClock, receivedWeight));
			printf("[wątek %d] moja kolejka to: [zegar = %d]\n", dane->rank, clockLamport);
			print(dane->head);

			msg[0] = clockLamport;
			msg[1] = -1;
			MPI_Send(msg, MSG_SIZE, MPI_INT, status.MPI_SOURCE, TAG_ACK, MPI_COMM_WORLD);
			pthread_mutex_unlock(&mutexClock);
		}
		else if (status.MPI_TAG == TAG_ACK)
		{
			printf("[Wątek %d - ack] ustawia w tablicy ack od  %d. [zegar = %d]\n", dane->rank, status.MPI_SOURCE, clockLamport);

			pthread_mutex_lock(&mutexClock);
			dane->tab_ack[status.MPI_SOURCE] = 1;

			//print tab_ack
			int j;
			printf("Tab_ack: [ %d", dane->tab_ack[0]);
			for (j = 1; j < dane->size; j++)
			{
				printf(", %d", dane->tab_ack[j]);
			}
			printf("]\n");


			int success = 1;
			int i;
			for (i = 0; i < dane->size; i++)
			{
				if (dane->tab_ack[i] != 1)
				{
					success = 0;
					break;
				}
			}
			if ((checkWeights(dane->head, dane->rank) + dane->myWeight) > Capacity) {
				success = 0;
			}

			if (success)
			{
				printf("[Wątek %d - ack] ACK probuje wybudzić wątek :D [zegar = %d]\n", dane->rank, clockLamport);

				pthread_cond_signal(&cond); // Should wake up *one* thread
			}
			pthread_mutex_unlock(&mutexClock);
		}
		else if (status.MPI_TAG == TAG_RELEASE)
		{
			pthread_mutex_lock(&mutexClock);
			printf("[Wątek %d - ack] usuwa z kolejki zgłoszenie %d.[zegar = %d]\n", dane->rank, status.MPI_SOURCE, clockLamport);
			printf("[wątek %d] moja kolejka PRZED USUNIECIEM to: [zegar = %d]\n", dane->rank, clockLamport);
			print(dane->head);

			
			dane->head = delete(dane->head, status.MPI_SOURCE);
			printf("[wątek %d] moja kolejka PO USUNIECIU to: [zegar = %d]\n", dane->rank, clockLamport);
			print(dane->head);

			int success = 1;
			int i;
			for (i = 0; i < dane->size; i++)
			{
				if (dane->tab_ack[i] != 1)
				{
					success = 0;
					break;
				}
			}

			printf("[Wątek %d - ack] Suma wag: %d .[zegar = %d]\n", dane->rank, checkWeights(dane->head, dane->rank), clockLamport);

			if ((checkWeights(dane->head, dane->rank) + dane->myWeight) > Capacity) {
				success = 0;
			}

			if (success)
			{
				printf("[Wątek %d - ack] RELEASE probuje wybudzić wątek :D [zegar = %d]\n", dane->rank, clockLamport);

				pthread_cond_signal(&cond); // Should wake up *one* thread
			}
			pthread_mutex_unlock(&mutexClock); // Zmiana- unlock po signal
		}


	}
	return NULL;
}

void* mainSkiThread(void* arg)
{

	while (!stop)
	{
		int msg[MSG_SIZE];
		struct data* dane = (struct data*)arg;

		int i;
		int receivedClock, receivedStatus;
		// semafor P
		pthread_mutex_lock(&mutexClock);
		clockLamport += 1;
		msg[0] = clockLamport;
		msg[1] = dane->myWeight;
		printf("[Wątek %d - main] wysłała do wszystkich request. [zegar = %d]\n", dane->rank, clockLamport);

		for (i = 0; i < dane->size; i++)
		{
			if (i != dane->rank) // do not send to yourself
			{
				MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_REQ, MPI_COMM_WORLD);

			}
		}
		// semafor V
		//wstaw do kolejki wlasne zadanie
		//printf("[Wątek %d - main] chce wstawić do swojej kolejki swój request. [zegar = %d]\n", dane->rank, clockLamport);
		dane->head = insert(dane->head, new_element(dane->rank, clockLamport, dane->myWeight));
		printf("[Wątek %d - main] wstawił do swojej kolejki swój request. [zegar = %d]\n", dane->rank, clockLamport);

		//sprawdz warunek bazujacy na kolejce (suma wag) i czy od wszystkich ack

		printf("[Wątek %d - main] sprawdza czy wszytskie wątki odebrały wiadomości. [zegar = %d]\n", dane->rank, clockLamport);
		do
		{
			int success = 1;
			int i;
			for (i = 0; i < dane->size; i++)
			{
				if (dane->tab_ack[i] != 1)
				{
					success = 0;
					break;
				}
			}
			if ((checkWeights(dane->head, dane->rank) + dane->myWeight) > Capacity) {
				success = 0;
			}

			if (success)
			{
				break;
			}
			else
			{
				printf("[Wątek %d - main] zasypiam sobie... zzz... [zegar = %d]\n", dane->rank, clockLamport);

				pthread_cond_wait(&cond, &mutexClock);
			}


		} while (1);

		//dodałem tutaj ten mutex unlock
		pthread_mutex_unlock(&mutexClock);

		printf("[Wątek %d - main] wszytskie wątki odebrały moją wiadomość wiadomości. [zegar = %d] Pozdrawiam, watek %d\n", dane->rank, clockLamport, dane->rank);

		printf("[Wątek %d - main] wyzerowuje tablice ack i wejeżdza do góry. SEKCJA KRYTYCZNA [zegar = %d]\n", dane->rank, clockLamport);
		// wyzerowanie ACK- mutex niepotrzebny, bo póki nie wyślę nowego REQUESTA, to nikt nie odpowie ACK
		for (i = 0; i < dane->size; i++)
		{
			dane->tab_ack[i] = 0;
		}
		dane->tab_ack[dane->rank] = 1; //set ack to 1 from yourself
		// GO!
		printf("\n[Wątek %d - main] wjeżdzam do góry przez %d sekund [zegar = %d]\n\n", dane->rank, GOUPTIME, clockLamport);
		sleep(GOUPTIME);

		// send RELEASE
		pthread_mutex_lock(&mutexClock);
		msg[0] = clockLamport;
		printf("\n[Wątek %d - main] KONIEC SEKCJI KRYTYCZNEJ [zegar = %d]\n\n", dane->rank, clockLamport);

		for (i = 0; i < dane->size; i++)
		{
			if (i != dane->rank) // do not send to yourself
			{
				MPI_Send(msg, MSG_SIZE, MPI_INT, i, TAG_RELEASE, MPI_COMM_WORLD);
			}
		}
		printf("[Wątek %d - main] wysyłał release synał do wszystkich i zjeżdża na dół. [zegar = %d]\n", dane->rank, clockLamport);

		// sleep random przy zjeździe
		printf("[Wątek %d - main] usuwa swoje zgłoszenie ze swojej kolejki. [zegar = %d]\n", dane->rank, clockLamport);
		printf("[wątek %d] moja kolejka PRZED USUNIECIEM to: [zegar = %d]\n", dane->rank, clockLamport);
			print(dane->head);

		//  usun swoje zadanie z kolejki
		dane->head = delete(dane->head, dane->rank);
printf("[wątek %d] moja kolejka PO USUNIECIU to: [zegar = %d]\n", dane->rank, clockLamport);
			print(dane->head);
		pthread_mutex_unlock(&mutexClock);

		int randomTime = 8 - (rand() % 7);
		printf("[Wątek %d - main] zjedża z góry przez %d sekund................ [zegar = %d]\n", dane->rank, randomTime, clockLamport);
		sleep(randomTime); // czy to jest potzrebne?
		printf("[Wątek %d - main] zjechał i znowy  ustawia się do kolejki narciarzy. [zegar = %d]\n", dane->rank, clockLamport);

	}
	return NULL;
}



int main(int argc, char **argv)
{

	srand(time(NULL));
	int rank, size;


	int provided = 0;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	if (provided < MPI_THREAD_MULTIPLE)
	{
		printf("ERROR: The MPI library does not have full thread support\n");

	}
	else
	{
		printf("Full support for multiple threads!\n");
	}


	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	struct data dane;
	dane.rank = rank;
	dane.size = size;
	dane.head = NULL;
	srand(rank);
	dane.myWeight = 70 + (30 - (rand() % 60));
	dane.tab_ack = malloc(dane.size * sizeof(int));
	int i;
	for (i = 0; i < dane.size; i++)
	{
		dane.tab_ack[i] = 0;
	}
	dane.tab_ack[dane.rank] = 1; // set ack from yourself to 1

	printf("Wątek %d zainicjował zmienne (waga = %d) i rozpocząl działnie.\n", dane.rank, dane.myWeight);
	pthread_t watek1, watek2;
	pthread_create(&watek1, NULL, receiveAndSendAck, &dane);
	pthread_create(&watek2, NULL, mainSkiThread, &dane);
	pthread_join(watek1, NULL);
	pthread_join(watek2, NULL);
	pthread_mutex_destroy(&mutexClock);
	pthread_cond_destroy(&cond);
	free(dane.tab_ack);
	MPI_Finalize();
	printf("Koniec programu");
}
